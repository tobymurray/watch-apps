#include "Service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "SDK/Messages/MessageGuard.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

/// The verifier's internal status, as the wire enum. Written as a switch with
/// no default so that adding a Status the wire does not describe is a compile
/// error here rather than a pack silently reported as Pending forever.
CustomMessage::PackState toPackState(PackCrcVerifier::Status status)
{
    switch (status) {
        case PackCrcVerifier::Status::Idle:       return CustomMessage::PackState::Pending;
        case PackCrcVerifier::Status::InProgress: return CustomMessage::PackState::Scanning;
        case PackCrcVerifier::Status::Verified:   return CustomMessage::PackState::Verified;
        case PackCrcVerifier::Status::Mismatched: return CustomMessage::PackState::Mismatched;
        case PackCrcVerifier::Status::IoError:    return CustomMessage::PackState::Unreadable;
    }
    return CustomMessage::PackState::Pending;
}

} // namespace

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
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, nextWaitMs())) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    mGuiStarted = true;
                    publish();
                    // A GUI that just opened has no roster at all, so send one
                    // whether or not it changed while nobody was watching.
                    publishRoster();
                    mRosterDirty = false;
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
        // (not gated on mGuiStarted or on the idle branch above; other
        // message traffic must not starve this).
        poll();

        // publish() stamps mLastPublishAtMs itself, so an on-demand snapshot
        // (GUI start, or an explicit request) also restarts this interval
        // rather than being followed immediately by a duplicate.
        if (mGuiStarted && (mKernel.sys.getTimeMs() - mLastPublishAtMs) >= kPublishPeriodMs) {
            publish();
        }
    }
}

void Service::poll()
{
    scanForNewPacks();
    driveCurrentEntry();

    // Publish a changed roster only while someone is looking. Without a GUI
    // there is no consumer, and the flag survives until one opens -- at which
    // point COMMAND_APP_NOTIF_GUI_RUN publishes unconditionally anyway.
    //
    // Throttled, and coalescing: verdicts can land far faster than the GUI
    // drains its queue (a screenful of cached markers all resolve within a
    // few hundred milliseconds at boot), and an un-throttled burst per
    // transition would overrun that queue and lose the head of its own
    // roster. Sending the latest state a little later is strictly better than
    // sending every intermediate state and having some of them dropped.
    if (mRosterDirty && mGuiStarted
            && (mKernel.sys.getTimeMs() - mLastRosterAtMs) >= kRosterPeriodMs) {
        publishRoster();
        mRosterDirty = false;
    }
}

uint32_t Service::nextWaitMs() const
{
    // Something to verify: come back promptly, there is real work waiting.
    if (mCurrentIndex < mEntries.size()) {
        return kBusyWaitMs;
    }

    // Nothing to verify. The only thing left to wake up for is the next
    // directory rescan, so sleep until that is actually due rather than
    // polling. This service is APP_AUTOSTART and never exits, so "wake twice
    // a second forever to find nothing" is a cost the device pays for its
    // whole life -- the SDK's own autostart utility (Alarm) sleeps to its
    // next scheduled work for the same reason.
    uint32_t waitMs = kRescanPeriodMs;
    if (mScannedOnce) {
        const uint32_t sinceScanMs = mKernel.sys.getTimeMs() - mLastScanAtMs;
        waitMs = (sinceScanMs >= kRescanPeriodMs) ? 0 : (kRescanPeriodMs - sinceScanMs);
    }

    // ...but never sleep through the GUI's refresh cadence while it's open,
    // or an open screen would sit stale for up to a full rescan period.
    if (mGuiStarted && waitMs > kPublishPeriodMs) {
        waitMs = kPublishPeriodMs;
    }
    return waitMs;
}

uint16_t Service::verifiedCount() const
{
    uint16_t verified = 0;
    for (const std::unique_ptr<TrackedPack> &entry : mEntries) {
        if (entry->verifier.status() == PackCrcVerifier::Status::Verified) {
            ++verified;
        }
    }
    return verified;
}

void Service::handleCommand(SDK::MessageBase *msg)
{
    if (msg->getType() != CustomMessage::MAP_MANAGER_REQUEST) {
        return; // Not one of ours.
    }
    publish();
    publishRoster();
    mRosterDirty = false;
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

    for (std::unique_ptr<TrackedPack> &entry : mEntries) {
        entry->seenThisScan = false;
    }

    const size_t extLen = std::strlen(kPackExtension);
    SDK::Interface::IFileSystem::ObjectInfo item{};
    size_t discovered = 0;
    size_t rearmed    = 0;
    bool   structureChanged = false;

    while (dir->readNext(item)) {
        if (item.isDir) {
            continue;
        }
        const size_t nameLen = std::strlen(item.name);
        if (nameLen <= extLen
                || std::strcmp(item.name + (nameLen - extLen), kPackExtension) != 0) {
            continue;
        }

        // Reject rather than truncate: a truncated path would be tracked
        // forever as an entry whose file can never be opened, which reads as
        // a corrupt pack rather than as the too-long name it actually is.
        char fullPath[SDK::Interface::IFileSystem::skMaxPathLen];
        const int pathLen = std::snprintf(fullPath, sizeof(fullPath), "%s/%s", kMapsDir, item.name);
        if (pathLen < 0 || static_cast<size_t>(pathLen) >= sizeof(fullPath)) {
            mLog.logf("scanForNewPacks() skipping %s: path too long for %zu bytes\n",
                      item.name, sizeof(fullPath));
            continue;
        }

        const uint64_t sizeNow = static_cast<uint64_t>(item.size);

        TrackedPack *tracked = nullptr;
        for (const std::unique_ptr<TrackedPack> &entry : mEntries) {
            if (entry->verifier.path() == fullPath) {
                tracked = entry.get();
                break;
            }
        }

        if (tracked == nullptr) {
            mLog.logf("scanForNewPacks() discovered %s (%llu bytes)\n",
                      fullPath, static_cast<unsigned long long>(sizeNow));
            mEntries.push_back(
                std::unique_ptr<TrackedPack>(new TrackedPack(mKernel, std::string(fullPath), sizeNow)));
            mEntries.back()->seenThisScan = true;
            structureChanged = true;
            ++discovered;
            continue;
        }

        tracked->seenThisScan = true;

        // The file changed size under us since this entry was armed. The
        // common cause is benign and important: the pack was still being
        // copied over USB when it was first discovered, so the "footer" that
        // was read came out of the middle of a partial file and the scan
        // reached a verdict about bytes that no longer exist. Without this,
        // that write-off would stand until the next reboot -- and USB copy is
        // the documented way packs get here.
        if (tracked->sizeAtLastScan != sizeNow) {
            mLog.logf("scanForNewPacks() %s changed size %llu -> %llu, re-arming\n",
                      fullPath,
                      static_cast<unsigned long long>(tracked->sizeAtLastScan),
                      static_cast<unsigned long long>(sizeNow));
            tracked->sizeAtLastScan = sizeNow;
            tracked->verifier.reset();
            structureChanged = true;
            ++rearmed;
        }
    }
    dir->close();

    // Drop entries whose file is no longer there, so a deleted pack stops
    // being counted in the totals the GUI shows. Its .trust marker is left
    // alone deliberately: it is 16 bytes, it is self-invalidating via its own
    // (size, crc) guard, and it is re-adopted for free if the pack comes
    // back. Deleting files is not this app's job.
    for (size_t i = mEntries.size(); i > 0; --i) {
        TrackedPack &entry = *mEntries[i - 1];
        if (entry.seenThisScan) {
            continue;
        }
        mLog.logf("scanForNewPacks() %s is gone, dropping it\n", entry.verifier.path().c_str());
        mEntries.erase(mEntries.begin() + static_cast<std::ptrdiff_t>(i - 1));
        structureChanged = true;
    }

    // Any add, removal or re-arm can put a not-yet-done entry behind the
    // cursor (an erase shifts everything after it down; a re-arm makes an
    // already-passed entry pending again). Rewinding is the one obviously
    // correct response: driveCurrentEntry() skips finished entries in a
    // single cheap pass, and the list holds a handful of packs at most.
    if (structureChanged) {
        mCurrentIndex = 0;
        mRosterDirty  = true;
    }

    if (discovered > 0 || rearmed > 0) {
        mLog.logf("scanForNewPacks() %zu new, %zu re-armed, %zu tracked total\n",
                  discovered, rearmed, mEntries.size());
    }
}

void Service::driveCurrentEntry()
{
    // Advance past anything already finished (Verified/Mismatched/IoError).
    while (mCurrentIndex < mEntries.size() && mEntries[mCurrentIndex]->verifier.done()
            && mEntries[mCurrentIndex]->verifier.status() != PackCrcVerifier::Status::Idle) {
        ++mCurrentIndex;
    }
    if (mCurrentIndex >= mEntries.size()) {
        return; // Nothing pending right now.
    }

    PackCrcVerifier &current = mEntries[mCurrentIndex]->verifier;

    // A transition is what the roster shows, so notice it here rather than
    // re-deriving it later: start() can go Idle -> InProgress or straight to a
    // cached verdict, and step() can finish a scan.
    const PackCrcVerifier::Status before = current.status();
    if (before == PackCrcVerifier::Status::Idle) {
        current.start(); // Cheap: either resolves via a cached marker, or begins a scan.
    } else if (before == PackCrcVerifier::Status::InProgress) {
        current.step(kSliceBudgetBytes);
    }

    if (current.status() != before) {
        mRosterDirty = true;
    }
}

void Service::publishRoster()
{
    mLastRosterAtMs = mKernel.sys.getTimeMs();

    const uint16_t total = static_cast<uint16_t>(mEntries.size());
    uint16_t       sent  = 0;

    // Loops at least once even when there is nothing to describe: an empty
    // roster is still a roster, and without saying so the GUI could never
    // learn that the last pack was deleted -- it would keep showing rows for
    // files that are gone, which silence is indistinguishable from.
    do {
        // Allocated per chunk rather than once and reused: send() hands the
        // message to the kernel, which owns it from then on.
        auto msg = SDK::make_msg<CustomMessage::MapManagerPackStatus>(mKernel);
        if (!msg) {
            // Pool exhausted. Leave the roster dirty so the whole thing is
            // resent rather than the GUI being left holding half of one.
            mRosterDirty = true;
            return;
        }

        const uint16_t chunk = static_cast<uint16_t>(
            std::min<size_t>(CustomMessage::kRowsPerMessage, static_cast<size_t>(total - sent)));

        msg->firstIndex = sent;
        msg->total      = total;
        msg->count      = static_cast<uint8_t>(chunk);

        for (uint16_t i = 0; i < chunk; ++i) {
            const PackCrcVerifier &verifier = mEntries[sent + i]->verifier;

            const char *base = std::strrchr(verifier.path().c_str(), '/');
            base = base ? base + 1 : verifier.path().c_str();
            std::snprintf(msg->rows[i].name, CustomMessage::kMaxRowNameLen, "%s", base);

            msg->rows[i].state = static_cast<uint8_t>(toPackState(verifier.status()));
        }

        msg.send();
        sent = static_cast<uint16_t>(sent + chunk);
    } while (sent < total);
}

void Service::publish()
{
    // Built through the guard rather than the message's own constructor:
    // this app targets SDK 1.3 (see the README), where allocateMessage
    // cannot forward constructor arguments -- the snapshot is assigned into
    // the default-constructed message instead.
    mLastPublishAtMs = mKernel.sys.getTimeMs();

    auto msg = SDK::make_msg<CustomMessage::MapManagerProgress>(mKernel);
    if (!msg) {
        return;
    }

    msg->packsVerified = verifiedCount();
    msg->packsTotal    = static_cast<uint16_t>(mEntries.size());

    if (mCurrentIndex < mEntries.size()) {
        const PackCrcVerifier &current = mEntries[mCurrentIndex]->verifier;
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
