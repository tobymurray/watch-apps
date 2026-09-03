/**
 ******************************************************************************
 * @file    SquashEngine.cpp
 * @brief   The C ABI of the Rust engine, and the profile file it is kept in.
 ******************************************************************************
 */

#include "SquashEngine.hpp"

#include <cstring>
#include <memory>

#define LOG_MODULE_PRX      "SquashEngine"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

/// Called from the Rust side's panic handler, which cannot unwind into C++.
extern "C" void squash_engine_host_panic(const uint8_t* msg, uint32_t len)
{
    LOG_ERROR("%.*s\n", static_cast<int>(len), reinterpret_cast<const char*>(msg));
}

SquashProfileStore::SquashProfileStore(const SDK::Kernel& kernel, const char* path)
    : mKernel(kernel), mPath(path)
{
}

SquashProfileStore::Load SquashProfileStore::load()
{
    if (!mKernel.fs.exist(mPath)) {
        squash_profile_load(nullptr, 0);
        return Load::ABSENT;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
    if (!file || !file->open(false, false)) {
        // An unreadable file is the same to the wearer as no file: a warm-up,
        // not a failure to start.
        squash_profile_load(nullptr, 0);
        return Load::READ_FAILED;
    }

    size_t read = 0;
    const bool ok = file->read(reinterpret_cast<char*>(mBuf), sizeof(mBuf), read);
    file->close();

    if (!ok) {
        squash_profile_load(nullptr, 0);
        return Load::READ_FAILED;
    }

    return static_cast<Load>(squash_profile_load(mBuf, static_cast<uint32_t>(read)));
}

bool SquashProfileStore::save()
{
    const int32_t len = squash_profile_write(mBuf, sizeof(mBuf));
    if (len <= 0) {
        LOG_ERROR("Profile did not fit its buffer; nothing written\n");
        return false;
    }

    // Written to a temporary and renamed over, so a battery pull mid-write
    // leaves the previous profile intact rather than half of the new one.
    char tmp[SDK::Interface::IFileSystem::skMaxPathLen]{};
    snprintf(tmp, sizeof(tmp), "%s.tmp", mPath);

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(tmp);
    if (!file || !file->open(true, true)) {
        LOG_ERROR("Could not open the profile temporary\n");
        return false;
    }

    size_t written = 0;
    bool ok = file->write(reinterpret_cast<const char*>(mBuf), static_cast<size_t>(len), written);
    ok = ok && written == static_cast<size_t>(len);
    // Folded into the result rather than assumed: on flash-backed FatFs the
    // buffered tail may only reach storage at flush or close, so a writer that
    // reported no error can still leave an incomplete file.
    ok = file->flush() && ok;
    ok = file->close() && ok;

    if (!ok) {
        mKernel.fs.remove(tmp);
        LOG_ERROR("Profile temporary is incomplete; the previous file is kept\n");
        return false;
    }

    // remove() first because rename onto an existing path fails on FatFs. The
    // window between the two is the one moment there is no profile, and it is
    // survivable: an absent file is a warm-up, not a corruption.
    mKernel.fs.remove(mPath);
    if (!mKernel.fs.rename(tmp, mPath)) {
        LOG_ERROR("Could not rename the profile into place\n");
        return false;
    }

    LOG_INFO("Profile saved: %d bytes, %u sessions\n",
             static_cast<int>(len), static_cast<unsigned>(squash_profile_sessions()));
    return true;
}
