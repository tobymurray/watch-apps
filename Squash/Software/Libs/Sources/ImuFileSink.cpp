/**
 ******************************************************************************
 * @file    ImuFileSink.cpp
 * @brief   ImuCsvRecorder sink over the SDK file system.
 ******************************************************************************
 */

#include "ImuFileSink.hpp"

#include <cassert>
#include <cstdio>

#define LOG_MODULE_PRX      "ImuFileSink"

#include "SDK/UnaLogger/Logger.h"

ImuFileSink::ImuFileSink(const SDK::Kernel& kernel, const char* pathToDir,
                         const char* nameSuffix)
    : mKernel(kernel), mDir(pathToDir), mSuffix(nameSuffix)
{
    assert(pathToDir != nullptr);
}

ImuFileSink::~ImuFileSink()
{
    // The recorder is expected to have been ended already; this is only so a
    // dropped sink still lands what it has rather than truncating mid-row.
    close();
}

bool ImuFileSink::create(std::time_t utc)
{
    close();

    char buff[256]{};
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif

    // mkdir is single-level, so the base directory has to exist before the
    // month directory can be made. Unlike Activity/, nothing else on the app's
    // start-up path creates Imu/, so create it here rather than depending on
    // some other writer having gone first.
    if (!mKernel.fs.mkdir(mDir)) {
        LOG_ERROR("Failed to create dir [%s]\n", mDir);
        return false;
    }

    const int len = snprintf(buff, sizeof(buff), "%s/%04u%02u/", mDir,
                             static_cast<unsigned>(localTime.tm_year) + 1900u,
                             static_cast<unsigned>(localTime.tm_mon) + 1u);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(buff)) {
        LOG_ERROR("Recording path too long for [%s]\n", mDir);
        return false;
    }
    if (!mKernel.fs.mkdir(buff)) {
        LOG_ERROR("Failed to create dir [%s]\n", buff);
        return false;
    }

    snprintf(&buff[len], sizeof(buff) - static_cast<size_t>(len),
             "imu_%04u%02u%02uT%02u%02u%02u%s.csv",
             static_cast<unsigned>(localTime.tm_year) + 1900u,
             static_cast<unsigned>(localTime.tm_mon) + 1u,
             static_cast<unsigned>(localTime.tm_mday),
             static_cast<unsigned>(localTime.tm_hour),
             static_cast<unsigned>(localTime.tm_min),
             static_cast<unsigned>(localTime.tm_sec),
             mSuffix ? mSuffix : "");

    mFile = mKernel.fs.file(buff);
    if (!mFile || !mFile->open(/*wMode=*/true, /*override=*/true)) {
        LOG_ERROR("Failed to create file [%s]\n", buff);
        mFile.reset();
        return false;
    }

    LOG_INFO("Recording raw IMU to [%s]\n", buff);
    return true;
}

bool ImuFileSink::close()
{
    if (!mFile) {
        return true;
    }

    const bool flushed = mFile->flush();
    const bool closed  = mFile->close();
    mFile.reset();

    if (!flushed || !closed) {
        LOG_ERROR("Failed to close recording cleanly\n");
        return false;
    }
    return true;
}

const char* ImuFileSink::path() const
{
    return mFile ? mFile->getPath() : nullptr;
}

bool ImuFileSink::write(const char* data, size_t len)
{
    if (!mFile) {
        return false;
    }

    size_t written = 0;
    if (!mFile->write(data, len, written) || written != len) {
        LOG_ERROR("Short write: %u of %u bytes\n",
                  static_cast<unsigned>(written), static_cast<unsigned>(len));
        return false;
    }
    return true;
}

bool ImuFileSink::flush()
{
    return mFile ? mFile->flush() : false;
}
