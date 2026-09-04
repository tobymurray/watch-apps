#include "SharedLog.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#define LOG_MODULE_PRX      "TrainKit"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace TrainKit {

namespace {

char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

void copySlug(char* dst, size_t cap, const char* src, bool lowercase)
{
    size_t n = 0;
    for (const char* p = src; p != nullptr && *p != '\0' && n + 1 < cap; ++p) {
        const char c = lowercase ? lower(*p) : *p;
        // The name reaches both a FatFs path and a JSON string, so anything
        // needing an escape in either is dropped rather than encoded.
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (plain) {
            dst[n++] = c;
        }
    }
    dst[n] = '\0';
}

} // namespace

SharedLog::SharedLog(SDK::Interface::IFileSystem& fs, const char* app, const char* sport)
    : mFs(fs)
{
    copySlug(mFileSlug, sizeof(mFileSlug), app, true);
    copySlug(mApp, sizeof(mApp), app, false);
    copySlug(mSport, sizeof(mSport), sport, false);
}

void SharedLog::buildPath(char* out, size_t cap, const char* suffix) const
{
    std::snprintf(out, cap, "%s/%s%s%s", TRAINKIT_SHARED_DIR, mFileSlug,
                  TRAINKIT_STORE_SUFFIX, suffix);
}

SharedLog::Status SharedLog::record(const trainkit_session& session)
{
    // FatFs f_open does not create missing parents, and "already exists"
    // counts as success -- the same first step SDK::Calibration::
    // OutdoorStrideCalibrator::finalise() takes.
    if (!mFs.mkdir(TRAINKIT_SHARED_DIR)) {
        LOG_ERROR("Could not make %s\n", TRAINKIT_SHARED_DIR);
        return Status::WRITE_FAILED;
    }

    const uint32_t cap = trainkit_max_store_bytes();
    // Transient, and only at the end of a ride: the .fit is already closed by
    // the time anything here runs.
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[cap]);
    std::unique_ptr<uint8_t[]> hist(
        new (std::nothrow) uint8_t[trainkit_history_bytes()]);
    if (!buf || !hist) {
        LOG_ERROR("No room for the session log\n");
        return Status::NO_MEMORY;
    }

    trainkit_history_init(hist.get(), mApp, mSport);

    uint32_t len = 0;
    readExisting(buf.get(), cap, len);

    switch (trainkit_history_load(hist.get(), buf.get(), len)) {
        case TRAINKIT_LOAD_OK:
            break;

        case TRAINKIT_LOAD_NEWER:
            // Something knows more about this file than this build does, and
            // what it wrote is not recoverable once overwritten.
            LOG_WARNING("Session log is a newer schema; leaving it alone\n");
            return Status::REFUSED;

        default: {
            // Keep it as evidence rather than deleting it, then start fresh.
            char path[SDK::Interface::IFileSystem::skMaxPathLen];
            char bak[SDK::Interface::IFileSystem::skMaxPathLen];
            buildPath(path, sizeof(path), "");
            buildPath(bak, sizeof(bak), ".bak");
            mFs.remove(bak);
            mFs.rename(path, bak);
            LOG_WARNING("Session log was unreadable; kept as .bak\n");
        } break;
    }

    trainkit_history_add(hist.get(), &session);

    const int32_t written = trainkit_history_save(hist.get(), buf.get(), cap);
    if (written <= 0) {
        LOG_ERROR("Session log would not serialise\n");
        return Status::WRITE_FAILED;
    }

    if (!writeTmp(buf.get(), static_cast<uint32_t>(written))) {
        return Status::WRITE_FAILED;
    }
    return commit();
}

bool SharedLog::readExisting(uint8_t* buf, uint32_t cap, uint32_t& outLen) const
{
    outLen = 0;

    char path[SDK::Interface::IFileSystem::skMaxPathLen];
    buildPath(path, sizeof(path), "");
    if (!mFs.exist(path)) {
        return true;   // no file yet is the ordinary first ride
    }

    std::unique_ptr<SDK::Interface::IFile> file = mFs.file(path);
    if (!file || !file->open(false, false)) {
        LOG_WARNING("Could not open the session log to read\n");
        return false;
    }

    const size_t size = file->size();
    if (size == 0 || size > cap) {
        // Bigger than this build will ever write, so it is not a file this
        // build should try to hold in memory.
        LOG_WARNING("Session log is %u bytes; treating it as unreadable\n",
                    static_cast<unsigned>(size));
        file->close();
        return false;
    }

    size_t read = 0;
    const bool ok = file->read(reinterpret_cast<char*>(buf), size, read) && read == size;
    file->close();
    if (ok) {
        outLen = static_cast<uint32_t>(read);
    }
    return ok;
}

bool SharedLog::writeTmp(const uint8_t* buf, uint32_t len)
{
    char tmp[SDK::Interface::IFileSystem::skMaxPathLen];
    buildPath(tmp, sizeof(tmp), ".tmp");

    std::unique_ptr<SDK::Interface::IFile> file = mFs.file(tmp);
    if (!file || !file->open(true, true)) {
        LOG_ERROR("Could not open %s\n", tmp);
        return false;
    }

    size_t wrote = 0;
    bool ok = file->write(reinterpret_cast<const char*>(buf), len, wrote) && wrote == len;
    // Both, unconditionally, and folded into the result: on flash-backed FatFs
    // the buffered tail may only reach storage at flush() or close(), so a
    // writer that reported no error can still leave an incomplete file.
    ok = file->flush() && ok;
    ok = file->close() && ok;

    if (!ok) {
        LOG_ERROR("Could not write %s\n", tmp);
        // A half-written leftover would confuse the next attempt.
        mFs.remove(tmp);
    }
    return ok;
}

SharedLog::Status SharedLog::commit()
{
    char path[SDK::Interface::IFileSystem::skMaxPathLen];
    char tmp[SDK::Interface::IFileSystem::skMaxPathLen];
    char bak[SDK::Interface::IFileSystem::skMaxPathLen];
    buildPath(path, sizeof(path), "");
    buildPath(tmp, sizeof(tmp), ".tmp");
    buildPath(bak, sizeof(bak), ".bak");

    // Best-effort: losing the backup must not block the commit.
    if (mFs.exist(path)) {
        mFs.remove(bak);
        mFs.rename(path, bak);
    }

    // FIRMWARE: rename onto an existing path fails on FatFs, so the target has
    // to be clear first -- proven by Squash, which hit it and left the finding
    // in Squash/Software/Libs/Sources/SquashEngine.cpp. The rotation above has
    // usually moved the file already; this is what covers the rotation having
    // failed, which would otherwise turn a lost backup into a lost ride. The
    // instant with no file at all is survivable: an absent log is a first ride.
    if (mFs.exist(path)) {
        mFs.remove(path);
    }

    if (!mFs.rename(tmp, path)) {
        // Left in place deliberately: the new content is written, just not
        // live, and deleting it would throw away the only copy of this ride.
        LOG_ERROR("Session log commit failed; %s is left behind\n", tmp);
        return Status::COMMIT_FAILED;
    }
    return Status::OK;
}

} // namespace TrainKit
