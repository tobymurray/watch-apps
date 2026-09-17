/**
 ******************************************************************************
 * @file    ImuMarkerLog.cpp
 * @brief   Research-mode marker sidecar: button-press timestamps for a recording.
 ******************************************************************************
 */

#include "ImuMarkerLog.hpp"

namespace {

/// Worst case the formatter can emit, from the field widths rather than
/// guessed: "4294967295" (10) + "65535" (5) + "255" (3) + 2 commas + '\n'.
constexpr size_t kWorstRowBytes = 10 + 5 + 3 + 2 + 1;

} // namespace

static_assert(ImuMarkerLog::skMaxRowBytes >= kWorstRowBytes,
              "skMaxRowBytes must cover the widest row the formatter can emit");

size_t ImuMarkerLog::formatUint(uint32_t value, char* out)
{
    char   digits[10]{};
    size_t n   = 0;
    size_t len = 0;

    do {
        digits[n++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && n < sizeof(digits));

    while (n > 0) {
        out[len++] = digits[--n];
    }

    return len;
}

bool ImuMarkerLog::begin(ISink& sink, uint32_t nowMs)
{
    mSink         = &sink;
    mStartMs      = nowMs;
    mBytesWritten = 0;
    mCount        = 0;
    mStop         = Stop::NONE;
    mRunning      = true;

    size_t headerLen = 0;
    while (skHeader[headerLen] != '\0') {
        ++headerLen;
    }

    if (!mSink->write(skHeader, headerLen)) {
        finish(Stop::SINK_ERROR);
        return false;
    }
    mBytesWritten += static_cast<uint32_t>(headerLen);

    // Flushed here as well as per-marker so that an armed-but-unmarked session
    // still leaves a well-formed (header-only) sidecar rather than a zero-byte
    // file that reads as a storage failure.
    if (!mSink->flush()) {
        finish(Stop::SINK_ERROR);
        return false;
    }

    return true;
}

bool ImuMarkerLog::mark(uint32_t nowMs, Kind kind)
{
    if (!mRunning) {
        return false;
    }

    if (mCount >= skMaxMarkers) {
        finish(Stop::LIMIT);
        return false;
    }

    // Unsigned subtraction: correct across the 32-bit tick wrap, and the same
    // arithmetic the sample recorder does, so the two files stay aligned.
    const uint32_t relMs = nowMs - mStartMs;

    char   row[skMaxRowBytes]{};
    size_t len = 0;

    len += formatUint(relMs, &row[len]);
    row[len++] = ',';
    len += formatUint(static_cast<uint32_t>(mCount) + 1u, &row[len]);
    row[len++] = ',';
    len += formatUint(static_cast<uint32_t>(kind), &row[len]);
    row[len++] = '\n';

    if (!mSink->write(row, len)) {
        finish(Stop::SINK_ERROR);
        return false;
    }
    mBytesWritten += static_cast<uint32_t>(len);

    // See the header: a marker is unreproducible, so it goes to storage now.
    if (!mSink->flush()) {
        finish(Stop::SINK_ERROR);
        return false;
    }

    ++mCount;
    return true;
}

bool ImuMarkerLog::finish(Stop reason)
{
    if (!mRunning) {
        return true;
    }

    mRunning = false;
    mStop    = reason;

    if (reason == Stop::SINK_ERROR) {
        return false;
    }

    return mSink != nullptr ? mSink->flush() : false;
}

bool ImuMarkerLog::end()
{
    if (!mRunning) {
        // Same contract as ImuCsvRecorder::end(): report whether the run that
        // ended is intact on storage, not whether this call did any work.
        return mStop != Stop::SINK_ERROR;
    }

    return finish(Stop::REQUESTED);
}
