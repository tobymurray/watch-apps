/**
 ******************************************************************************
 * @file    StreamStats.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Delivery and timing for one stream. Rationale is in the header.
 ******************************************************************************
 */

#include "Stats/StreamStats.hpp"

namespace SensorLab::Stats
{

namespace
{

/// Signed difference of two 32-bit uptimes, correct across the ~49.7-day wrap.
///
/// Unsigned subtraction then a cast: `b - a` in uint32 arithmetic is the true
/// difference modulo 2^32, and reinterpreting it as int32 recovers the signed
/// difference for any interval shorter than ~24.8 days. Every duration in this
/// file goes through here, which is what makes the wrap a non-event rather than
/// a 49-day interval appearing in a profile.
int32_t diff(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(b - a);
}

/// Forward elapsed ms between two uptimes, or 0 if @p b is not after @p a.
uint32_t elapsed(uint32_t a, uint32_t b)
{
    const int32_t d = diff(a, b);
    return (d > 0) ? static_cast<uint32_t>(d) : 0u;
}

constexpr float kMsPerMinute = 60000.0f;

} // namespace

const char *toString(Cadence c)
{
    switch (c) {
        case Cadence::Unknown:   return "unknown";
        case Cadence::Streaming: return "streaming";
        case Cadence::Event:     return "event";
    }
    return "?";
}

void StreamStats::reset()
{
    // 1 ms bins from zero for sample dt and batch arrival alike. Both are
    // intervals in milliseconds and both matter most in the 0-128 ms range: the
    // accelerometer's measured 21 ms sample period and its measured 195 ms
    // batch interval (ledger rows S3 and S17) sit either side of the top bin,
    // so the overflow count and the exact max carry the second one -- which is
    // the honest outcome, since 195 ms against a requested 5000 ms is a
    // finding about the request, not about the resolution of the histogram.
    mDt.reset(1.0f, 0.0f);
    mBatchDt.reset(1.0f, 0.0f);
    mBatchSizes.reset(1.0f, 0.0f);

    mSamples           = 0;
    mBatches           = 0;
    mConnectedAt       = 0;
    mHaveConnected     = false;
    mFirstSampleMs     = 0;
    mHaveFirst         = false;
    mFirstTs           = 0;
    mLastTs            = 0;
    mHaveTs            = false;
    mLongestGapMs      = 0;
    mFirstBatchAt      = 0;
    mLastBatchAt       = 0;
    mHaveBatchAt       = false;
    mFieldCount        = 0;
    mFieldCountAlt     = 0;
    mFieldCountChanged = false;
    mUsOver999         = 0;
    mNonMonotonic      = 0;
    mBatchArrivalMs    = 0;
    mHaveBatchArrival  = false;
    mFirstOffsetMs     = 0;
    mLastOffsetMs      = 0;
    mSkewFirstUptime   = 0;
    mSkewLastUptime    = 0;
    mSkewSamples       = 0;
}

void StreamStats::onConnected(uint32_t uptimeMs)
{
    mConnectedAt   = uptimeMs;
    mHaveConnected = true;
}

void StreamStats::onBatch(uint32_t arrivalUptimeMs, uint16_t sampleCount,
                          uint16_t fieldCount)
{
    mBatches++;
    mBatchArrivalMs   = arrivalUptimeMs;
    mHaveBatchArrival = true;

    mBatchSizes.add(static_cast<float>(sampleCount));

    if (mHaveBatchAt) {
        mBatchDt.add(static_cast<float>(elapsed(mLastBatchAt, arrivalUptimeMs)));
    } else {
        mFirstBatchAt = arrivalUptimeMs;
        mHaveBatchAt  = true;
    }
    mLastBatchAt = arrivalUptimeMs;

    // Field count, and whether it ever changes. Recorded from the batch rather
    // than from the parser, which is the whole of layer 2: a frame that does
    // not match the parser shipped to read it is the finding.
    if (fieldCount == 0) {
        return;
    }
    if (mFieldCount == 0) {
        mFieldCount = fieldCount;
    } else if (fieldCount != mFieldCount) {
        mFieldCountChanged = true;
        mFieldCountAlt     = fieldCount;
    }
}

void StreamStats::onSample(uint32_t tsMs, uint32_t tsUs)
{
    // The invariant `DataView::getTimestampUs()` is built on, and which nothing
    // in either repository has ever checked. One compare.
    if (tsUs > 999u) {
        mUsOver999++;
    }

    if (!mHaveFirst && mHaveConnected) {
        mHaveFirst     = true;
        mFirstSampleMs = elapsed(mConnectedAt,
                                 mHaveBatchArrival ? mBatchArrivalMs : mConnectedAt);
    }

    if (mHaveTs) {
        const int32_t d = diff(mLastTs, tsMs);
        if (d < 0) {
            // Backwards. Counted, never corrected: a pipeline that reorders is
            // a finding, and sorting it here would hide it. Contributes no dt,
            // because a negative interval is not an interval.
            mNonMonotonic++;
        } else {
            const uint32_t gap = static_cast<uint32_t>(d);
            mDt.add(static_cast<float>(gap));
            if (gap > mLongestGapMs) {
                mLongestGapMs = gap;
            }
        }
    } else {
        mFirstTs = tsMs;
        mHaveTs  = true;
    }
    mLastTs = tsMs;
    mSamples++;

    // Skew: the offset between the sensor's clock and the device's, at the
    // first sample and at the most recent. Two points is a slope; the constant
    // part is interesting on its own and the slope is what says whether they
    // are separate oscillators.
    if (mHaveBatchArrival) {
        const int32_t offset = diff(tsMs, mBatchArrivalMs);
        if (mSkewSamples == 0) {
            mFirstOffsetMs   = offset;
            mSkewFirstUptime = mBatchArrivalMs;
        }
        mLastOffsetMs   = offset;
        mSkewLastUptime = mBatchArrivalMs;
        mSkewSamples++;
    }
}

uint32_t StreamStats::timestampSpanMs() const
{
    return mHaveTs ? elapsed(mFirstTs, mLastTs) : 0u;
}

float StreamStats::samplesPerMinute() const
{
    const uint32_t span = timestampSpanMs();
    if (span == 0 || mSamples < 2) {
        return 0.0f;
    }
    // n-1 intervals across the span, not n: a rate is intervals per time, and
    // using n overstates a short run by 1/n. At the ten-thousand-sample minimum
    // that is 0.01 %, but the same arithmetic runs on a 60-sample event stream
    // where it would be 1.7 %.
    return static_cast<float>(mSamples - 1) * kMsPerMinute
           / static_cast<float>(span);
}

float StreamStats::batchesPerMinute() const
{
    if (!mHaveBatchAt || mBatches < 2) {
        return 0.0f;
    }
    const uint32_t span = elapsed(mFirstBatchAt, mLastBatchAt);
    if (span == 0) {
        return 0.0f;
    }
    return static_cast<float>(mBatches - 1) * kMsPerMinute
           / static_cast<float>(span);
}

Cadence StreamStats::cadence() const
{
    // Not enough delivery to say. This is where `TOUCH_DETECT`'s one sample in
    // 507 minutes correctly stays -- and it is a measurement, not a failure:
    // "this sensor speaks less than once a minute" is the answer.
    if (mSamples < kCadenceMinSamples || mDt.count() < 2) {
        return Cadence::Unknown;
    }

    const float p50 = mDt.quantile(0.5f);
    if (p50 > static_cast<float>(kStreamingMedianMaxMs)) {
        return Cadence::Event;
    }
    // A tight distribution around a period is a stream; one spanning orders of
    // magnitude is a state-change publisher that happened to change state
    // often.
    const float p95 = mDt.quantile(0.95f);
    if (p50 > 0.0f && p95 > p50 * kPeriodicSpreadFactor) {
        return Cadence::Event;
    }
    return Cadence::Streaming;
}

float StreamStats::skewPpm() const
{
    if (mSkewSamples < 2) {
        return 0.0f;
    }
    const uint32_t span = elapsed(mSkewFirstUptime, mSkewLastUptime);
    if (span == 0) {
        return 0.0f;
    }
    const int32_t drift = mLastOffsetMs - mFirstOffsetMs;
    return static_cast<float>(drift) * 1000000.0f / static_cast<float>(span);
}

} // namespace SensorLab::Stats
