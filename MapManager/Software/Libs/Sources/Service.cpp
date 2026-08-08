#include "Service.hpp"

#include <cstdio>
#include <cstring>

#include "SDK/Messages/MessageGuard.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel)
    , mGuiStarted(false)
{
}

void Service::run()
{
    LOG_INFO("Started\n");

    while (true) {
        // AthensRun's original verifier hit ~112KB/s on this same 4096B/step,
        // getMessage()-gated design, but only because its Service has
        // constant sensor message traffic that wakes the 500ms wait early on
        // almost every iteration. MapManager subscribes to no sensors, so
        // nothing ever wakes it early: it was blocking the full 500ms per
        // iteration, capping verification to ~2 steps/sec (~8KB/s -- ~7h for
        // a 200MB pack, confirmed on-device via mapmanager_verify.log).
        // Shortening the wait while work is actually pending fixes that
        // without busy-spinning (0ms) or waking needlessly once idle.
        const bool hasPendingWork = mCurrentIndex < mEntries.size();
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, hasPendingWork ? 50 : 500)) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    mGuiStarted = true;
                    publish();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    mGuiStarted = false;
                    // Deliberately does NOT end the service here (unlike a
                    // typical utility app) -- verification must keep making
                    // progress in the background whether the GUI is open or
                    // not. That's the whole point of autostart.
                    break;

                default:
                    handleCommand(msg);
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        // Bounded, resumable background work -- runs every loop iteration
        // (not gated on mGuiStarted or the 500ms idle branch above; other
        // message traffic must not starve this).
        scanForNewPacks();
        driveCurrentEntry();

        const uint32_t nowMs = mKernel.sys.getTimeMs();
        if (mGuiStarted && (nowMs - mLastPublishAtMs) >= kPublishPeriodMs) {
            publish();
            mLastPublishAtMs = nowMs;
        }
    }
}

void Service::handleCommand(SDK::MessageBase *msg)
{
    if (msg->getType() != CustomMessage::MAP_MANAGER_REQUEST) {
        return; // Not one of ours.
    }
    publish();
}

void Service::scanForNewPacks()
{
    const uint32_t nowMs = mKernel.sys.getTimeMs();
    if (mScannedOnce && (nowMs - mLastScanAtMs) < kRescanPeriodMs) {
        return;
    }
    mScannedOnce  = true;
    mLastScanAtMs = nowMs;

    std::unique_ptr<SDK::Interface::IDirectory> dir = mKernel.fs.dir(kMapsDir);
    if (!dir || !dir->open()) {
        // Nothing to do yet -- SharedData/maps might not exist until someone
        // deploys a pack there. Not an error worth logging every 30s.
        return;
    }

    const size_t extLen = std::strlen(kPackExtension);
    SDK::Interface::IFileSystem::ObjectInfo item{};
    size_t discovered = 0;
    while (dir->readNext(item)) {
        if (item.isDir) {
            continue;
        }
        const size_t nameLen = std::strlen(item.name);
        if (nameLen <= extLen
                || std::strcmp(item.name + (nameLen - extLen), kPackExtension) != 0) {
            continue;
        }

        char fullPath[SDK::Interface::IFileSystem::skMaxPathLen];
        std::snprintf(fullPath, sizeof(fullPath), "%s/%s", kMapsDir, item.name);

        bool alreadyTracked = false;
        for (const PackCrcVerifier &entry : mEntries) {
            if (entry.path() == fullPath) {
                alreadyTracked = true;
                break;
            }
        }
        if (!alreadyTracked) {
            mLog.logf("scanForNewPacks() discovered %s\n", fullPath);
            mEntries.emplace_back(mKernel, std::string(fullPath));
            ++discovered;
        }
    }
    dir->close();

    if (discovered > 0) {
        mLog.logf("scanForNewPacks() %zu new pack(s), %zu tracked total\n",
                  discovered, mEntries.size());
    }
}

void Service::driveCurrentEntry()
{
    // Advance past anything already finished (Verified/Mismatched/IoError).
    while (mCurrentIndex < mEntries.size() && mEntries[mCurrentIndex].done()
            && mEntries[mCurrentIndex].status() != PackCrcVerifier::Status::Idle) {
        ++mCurrentIndex;
    }
    if (mCurrentIndex >= mEntries.size()) {
        return; // Nothing pending right now.
    }

    PackCrcVerifier &current = mEntries[mCurrentIndex];
    if (current.status() == PackCrcVerifier::Status::Idle) {
        current.start(); // Cheap: either resolves via a cached marker, or begins a scan.
    } else if (current.status() == PackCrcVerifier::Status::InProgress) {
        current.step();
    }
}

void Service::publish()
{
    // Built through the guard rather than the message's own constructor:
    // this app targets SDK 1.3 (see the README), where allocateMessage
    // cannot forward constructor arguments -- the snapshot is assigned into
    // the default-constructed message instead.
    auto msg = SDK::make_msg<CustomMessage::MapManagerProgress>(mKernel);
    if (!msg) {
        return;
    }

    uint16_t verified = 0;
    for (const PackCrcVerifier &entry : mEntries) {
        if (entry.status() == PackCrcVerifier::Status::Verified) {
            ++verified;
        }
    }
    msg->packsVerified = verified;
    msg->packsTotal    = static_cast<uint16_t>(mEntries.size());

    if (mCurrentIndex < mEntries.size()) {
        const PackCrcVerifier &current = mEntries[mCurrentIndex];
        const char *base = std::strrchr(current.path().c_str(), '/');
        base = base ? base + 1 : current.path().c_str();
        std::snprintf(msg->packName, CustomMessage::kMaxPackNameLen, "%s", base);

        msg->anyInProgress = (current.status() == PackCrcVerifier::Status::InProgress);
        if (msg->anyInProgress) {
            msg->bytesDone  = current.bytesDone();
            msg->bytesTotal = current.bytesTotal();
            msg->elapsedMs  = mKernel.sys.getTimeMs() - current.startedAtMs();
        }
        // else: a just-started (Idle) or already-finished entry reports the
        // default-constructed 0/0/0 -- the GUI shows "starting..." rather
        // than a bogus 0% / infinite ETA.
    } else {
        msg->packName[0]   = '\0';
        msg->anyInProgress = false;
    }

    msg.send();
}
