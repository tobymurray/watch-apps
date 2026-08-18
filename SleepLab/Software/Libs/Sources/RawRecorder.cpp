/**
 ******************************************************************************
 * @file    RawRecorder.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Optional raw accelerometer capture. Rationale is in the header.
 ******************************************************************************
 */

#include "RawRecorder.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#define LOG_MODULE_PRX      "RawRec"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SleepLab
{
namespace {

constexpr char kHeader[] =
    "# SleepLab raw accelerometer capture. Acceleration in microgravities\n"
    "# (1 g = 1000000); divide by 1e6 for g. t_ms is relative to the first\n"
    "# sample and comes from the sensor's own timestamps, so the cadence here\n"
    "# is the sensor's rather than the message loop's.\n"
    "t_ms,ax_ug,ay_ug,az_ug\n";

/// g to microgravities, saturating. The BMI270 at +/-8 g resolves ~244 ug per
/// LSB, so this is finer than the sensor and loses nothing.
int32_t toMicroG(float g)
{
    const float v = g * 1000000.0f;
    if (v >  2.0e9f) { return  2000000000; }
    if (v < -2.0e9f) { return -2000000000; }
    return static_cast<int32_t>(v);
}

} // namespace

RawRecorder::RawRecorder(const SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

RawRecorder::~RawRecorder()
{
    stop();
}

const char *RawRecorder::state() const
{
    if (mOpen)   { return "recording"; }
    if (mHitCap) { return "stopped at its cap"; }
    return "off";
}

bool RawRecorder::start(int64_t startUtc, uint16_t maxMb, uint16_t maxMin)
{
    stop();

    mKernel.fs.mkdir(kRawDir);

    char stem[32] = "unknown";
    if (startUtc > 0) {
        const std::time_t t = static_cast<std::time_t>(startUtc);
        std::tm local {};
#if defined(_WIN32) || defined(_WIN64)
        const bool ok = (localtime_s(&local, &t) == 0);
#else
        const bool ok = (localtime_r(&t, &local) != nullptr);
#endif
        if (ok) {
            std::snprintf(stem, sizeof(stem), "%04d%02d%02dT%02d%02d%02d",
                          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                          local.tm_hour, local.tm_min, local.tm_sec);
        }
    }
    std::snprintf(mPath, sizeof(mPath), "%s/raw_%s.csv", kRawDir, stem);

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
    if (!file || !file->open(true, true)) {
        LOG_WARNING("cannot create %s\n", mPath);
        return false;
    }
    size_t     written = 0;
    const bool ok = file->write(kHeader, sizeof(kHeader) - 1, written);
    file->flush();
    file->close();
    if (!ok) {
        return false;
    }

    mBytes       = written;
    mMaxBytes    = static_cast<uint64_t>(maxMb) * 1024ull * 1024ull;
    mMaxMs       = static_cast<uint32_t>(maxMin) * 60u * 1000u;
    mHaveFirstTs = false;
    mBufLen      = 0;
    mHitCap      = false;
    mOpen        = true;

    LOG_INFO("raw capture open: %s (cap %u MB / %u min)\n", mPath,
             static_cast<unsigned>(maxMb), static_cast<unsigned>(maxMin));
    return true;
}

bool RawRecorder::flushBuffer()
{
    if (mBufLen == 0) {
        return true;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
    if (!file || !file->open(true, false)) {
        return false;
    }
    // open(write) positions at offset 0, not at the end -- see NightStore.
    if (!file->seek(file->size())) {
        file->close();
        return false;
    }

    size_t     written = 0;
    const bool ok = file->write(mBuf, mBufLen, written) && written == mBufLen;
    file->flush();
    file->close();

    if (ok) {
        mBytes += written;
    }
    mBufLen = 0;
    return ok;
}

void RawRecorder::add(uint32_t timestampMs, float x, float y, float z)
{
    if (!mOpen) {
        return;
    }

    if (!mHaveFirstTs) {
        mHaveFirstTs = true;
        mFirstTsMs   = timestampMs;
    }

    // Unsigned difference, so the sensor timestamp wrapping is handled.
    const uint32_t rel = timestampMs - mFirstTsMs;

    if (rel >= mMaxMs) {
        LOG_INFO("raw capture hit its duration cap at %lu ms\n",
                 static_cast<unsigned long>(rel));
        mHitCap = true;
        stop();
        return;
    }

    // Checked against the row's *worst case* before writing, so the file never
    // crosses its budget mid-row. A half row is worse than a missing one: a
    // parser can match the wrong half of it.
    if (mBytes + mBufLen + kRowWorstCase > mMaxBytes) {
        LOG_INFO("raw capture hit its byte cap at %llu bytes\n",
                 static_cast<unsigned long long>(mBytes));
        mHitCap = true;
        stop();
        return;
    }

    if (mBufLen + kRowWorstCase > sizeof(mBuf)) {
        if (!flushBuffer()) {
            LOG_WARNING("raw write failed; stopping capture\n");
            stop();
            return;
        }
    }

    // Integers, not "%.4f". The MCU's newlib may not link float formatting at
    // all, and a recorder that silently writes empty fields for its own samples
    // is worse than one that scales. BeatProbe reached the same conclusion and
    // Squash's recorder writes integers for the same reason.
    const int n = std::snprintf(mBuf + mBufLen, sizeof(mBuf) - mBufLen,
                                "%lu,%ld,%ld,%ld\n",
                                static_cast<unsigned long>(rel),
                                static_cast<long>(toMicroG(x)),
                                static_cast<long>(toMicroG(y)),
                                static_cast<long>(toMicroG(z)));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(mBuf) - mBufLen) {
        // The worst-case check above should make this unreachable; if it is
        // reached the assumption behind kRowWorstCase is wrong, and dropping
        // the row is better than emitting a truncated one.
        return;
    }
    mBufLen += static_cast<size_t>(n);
}

void RawRecorder::stop()
{
    if (!mOpen) {
        return;
    }
    flushBuffer();
    mOpen = false;
    LOG_INFO("raw capture closed: %llu bytes\n",
             static_cast<unsigned long long>(mBytes));
}

} // namespace SleepLab
