/**
 ******************************************************************************
 * @file    HrCsvLog.cpp
 * @brief   Research-mode heart-rate sidecar, on the recording's own clock.
 ******************************************************************************
 */

#include "HrCsvLog.hpp"

namespace {

/// Worst case the formatter can emit, from the field widths rather than
/// guessed: "4294967295" (10) + three of "-99999" (6) + "255" (3) + "255" (3)
/// + 5 commas + '\n'.
constexpr size_t kWorstRowBytes = 10 + (6 * 3) + 3 + 3 + 5 + 1;

/// Widest fixed-point reading the row budget allows, which is 999.99 bpm — an
/// order above any heart rate and two below the field's own limit.
constexpr int32_t kMaxHundredths = 99999;

} // namespace

static_assert(HrCsvLog::skMaxRowBytes >= kWorstRowBytes,
              "skMaxRowBytes must cover the widest row the formatter can emit");

size_t HrCsvLog::formatInt(int64_t value, char* out)
{
    size_t len = 0;

    uint64_t magnitude = static_cast<uint64_t>(value);
    if (value < 0) {
        out[len++] = '-';
        magnitude  = static_cast<uint64_t>(-value);
    }

    char   digits[20]{};
    size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + (magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0u && n < sizeof(digits));

    while (n > 0) {
        out[len++] = digits[--n];
    }

    return len;
}

int32_t HrCsvLog::hundredths(float bpm)
{
    const float scaled = bpm * 100.0f;
    if (scaled > static_cast<float>(kMaxHundredths)) {
        return kMaxHundredths;
    }
    if (scaled < -static_cast<float>(kMaxHundredths)) {
        return -kMaxHundredths;
    }
    return static_cast<int32_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

bool HrCsvLog::begin(ISink& sink, uint32_t nowMs)
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

    // Flushed here as well as per-row so a session that never saw a heart rate
    // still leaves a well-formed header-only file rather than a zero-byte one
    // that reads as a storage failure.
    if (!mSink->flush()) {
        finish(Stop::SINK_ERROR);
        return false;
    }

    return true;
}

bool HrCsvLog::onSample(uint32_t nowMs, const Sample& sample)
{
    if (!mRunning) {
        return false;
    }

    if (mCount >= skMaxSamples) {
        finish(Stop::LIMIT);
        return false;
    }

    // Unsigned subtraction: correct across the 32-bit tick wrap, and the same
    // arithmetic the sample recorder does, so the files stay aligned.
    const uint32_t relMs = nowMs - mStartMs;

    char   row[skMaxRowBytes]{};
    size_t len = 0;

    len += formatInt(static_cast<int64_t>(relMs), &row[len]);
    row[len++] = ',';
    len += formatInt(hundredths(sample.bpm), &row[len]);
    row[len++] = ',';
    len += formatInt(static_cast<int64_t>(sample.trust), &row[len]);
    row[len++] = ',';
    len += formatInt(static_cast<int64_t>(sample.source), &row[len]);
    row[len++] = ',';
    len += formatInt(hundredths(sample.opticalBpm), &row[len]);
    row[len++] = ',';
    len += formatInt(hundredths(sample.externalBpm), &row[len]);
    row[len++] = '\n';

    if (!mSink->write(row, len)) {
        finish(Stop::SINK_ERROR);
        return false;
    }
    mBytesWritten += static_cast<uint32_t>(len);

    // See the header: a reading is unreproducible, so it goes to storage now.
    if (!mSink->flush()) {
        finish(Stop::SINK_ERROR);
        return false;
    }

    ++mCount;
    return true;
}

bool HrCsvLog::finish(Stop reason)
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

bool HrCsvLog::end()
{
    if (!mRunning) {
        // Same contract as ImuCsvRecorder::end(): report whether the run that
        // ended is intact on storage, not whether this call did any work.
        return mStop != Stop::SINK_ERROR;
    }

    return finish(Stop::REQUESTED);
}
