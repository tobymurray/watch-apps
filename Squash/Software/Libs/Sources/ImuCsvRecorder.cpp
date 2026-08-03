/**
 ******************************************************************************
 * @file    ImuCsvRecorder.cpp
 * @brief   Research-mode recorder: raw 100 Hz FUSION_RAW stream to CSV.
 ******************************************************************************
 */

#include "ImuCsvRecorder.hpp"

namespace {

/// Worst case the formatter can emit for one row, derived from the field widths
/// rather than guessed: "4294967295" (10) + 6 x "-32768" (6 each) + 6 commas + '\n'.
constexpr size_t kWorstRowBytes = 10 + (6 * 6) + 6 + 1;

} // namespace

static_assert(ImuCsvRecorder::skMaxRowBytes >= kWorstRowBytes,
              "skMaxRowBytes must cover the widest row the formatter can emit, "
              "or the buffer can overrun between flushes");
static_assert(ImuCsvRecorder::skBufferBytes > ImuCsvRecorder::skMaxRowBytes,
              "the staging buffer must hold at least one full row");

size_t ImuCsvRecorder::formatInt(int32_t value, char* out)
{
    // Build digits backwards into a scratch array, then reverse into out.
    // Negation is done in the wider unsigned domain so INT32_MIN is safe.
    char     digits[10]{};
    size_t   n     = 0;
    size_t   len   = 0;
    uint32_t magnitude;

    if (value < 0) {
        out[len++] = '-';
        magnitude  = static_cast<uint32_t>(-(static_cast<int64_t>(value)));
    } else {
        magnitude = static_cast<uint32_t>(value);
    }

    do {
        digits[n++] = static_cast<char>('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u && n < sizeof(digits));

    while (n > 0) {
        out[len++] = digits[--n];
    }

    return len;
}

bool ImuCsvRecorder::drain()
{
    if (mBufLen == 0) {
        return true;
    }

    const bool ok = mSink != nullptr && mSink->write(mBuf, mBufLen);
    if (ok) {
        mBytesWritten += static_cast<uint32_t>(mBufLen);
    }
    mBufLen = 0;
    return ok;
}

bool ImuCsvRecorder::stage(const char* data, size_t len)
{
    if (mBufLen + len > skBufferBytes && !drain()) {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        mBuf[mBufLen++] = data[i];
    }
    return true;
}

bool ImuCsvRecorder::begin(ISink& sink, uint32_t nowMs, const Limits& limits)
{
    mSink         = &sink;
    mLimits       = limits;
    mStartMs      = nowMs;
    mBytesWritten = 0;
    mSampleCount  = 0;
    mBufLen       = 0;
    mStop         = Stop::NONE;
    mRecording    = true;

    size_t headerLen = 0;
    while (skHeader[headerLen] != '\0') {
        ++headerLen;
    }

    if (!stage(skHeader, headerLen)) {
        finish(Stop::SINK_ERROR);
        return false;
    }

    return true;
}

bool ImuCsvRecorder::begin(ISink& sink, uint32_t nowMs)
{
    return begin(sink, nowMs, Limits{});
}

bool ImuCsvRecorder::onSample(uint32_t nowMs, const Sample& sample)
{
    if (!mRecording) {
        return false;
    }

    // Unsigned subtraction: correct across the 32-bit tick wrap.
    const uint32_t relMs = nowMs - mStartMs;

    if (relMs >= mLimits.maxDurationMs) {
        finish(Stop::DURATION_LIMIT);
        return false;
    }

    // Check the cap against the worst case for this row rather than its actual
    // width, so the file can never cross maxBytes.
    if (mBytesWritten + mBufLen + skMaxRowBytes > mLimits.maxBytes) {
        finish(Stop::SIZE_LIMIT);
        return false;
    }

    char   row[skMaxRowBytes]{};
    size_t len = 0;

    len += formatInt(static_cast<int32_t>(relMs & 0x7FFFFFFFu), &row[len]);

    const int16_t fields[6] = {sample.ax, sample.ay, sample.az,
                               sample.gx, sample.gy, sample.gz};
    for (const int16_t field : fields) {
        row[len++] = ',';
        len += formatInt(field, &row[len]);
    }
    row[len++] = '\n';

    if (!stage(row, len)) {
        finish(Stop::SINK_ERROR);
        return false;
    }

    ++mSampleCount;
    return true;
}

bool ImuCsvRecorder::finish(Stop reason)
{
    if (!mRecording) {
        return true;
    }

    mRecording = false;
    mStop      = reason;

    // Nothing can be salvaged once the sink has failed; trying again would only
    // report a second failure for the same cause.
    if (reason == Stop::SINK_ERROR) {
        mBufLen = 0;
        return false;
    }

    bool ok = drain();
    if (mSink != nullptr) {
        ok = mSink->flush() && ok;
    }
    return ok;
}

bool ImuCsvRecorder::end()
{
    if (!mRecording) {
        // Already stopped. Report whether the run that ended is intact on
        // storage: a cap stop is a complete, valid (if short) file, but a sink
        // error is not, and the caller must not be told otherwise just because
        // the failure happened earlier than this call.
        return mStop != Stop::SINK_ERROR;
    }

    return finish(Stop::REQUESTED);
}
