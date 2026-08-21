/**
 ******************************************************************************
 * @file    StreamStats.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Layers 3 and 4 for one subscribed sensor: delivery, dt, clocks.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Three clocks, and this file is where they meet
 *
 *  - **Uptime** (`kernel.sys.getTimeMs()`) is 32-bit, wraps at ~49.7 days, and
 *    is the only clock any duration here comes from. Every comparison is an
 *    unsigned or signed *difference*, never a magnitude compare, so the wrap
 *    passes through without a 49-day interval appearing anywhere. Proved
 *    against a synthetic wrap in host tests, the way SleepLab's row P9a was --
 *    this app cannot wait 49.7 days and does not have to.
 *
 *  - **Sample timestamps** are the sensor pipeline's own clock, and the whole
 *    point of layer 4 is that they are a *third* clock rather than a view of
 *    the first. `DataView::getTimestampUs()` computes
 *    `mTimeStamp * 1000 + mTimeStampUs`, which encodes an assumption: that
 *    `mTimeStamp` is milliseconds and `mTimeStampUs` is a sub-millisecond
 *    remainder under 1000. **Nobody has checked it.** If `mTimeStampUs` ever
 *    exceeds 999, every microsecond timestamp in every app on this platform is
 *    wrong. `usOver999()` is that check, and it costs one compare a sample.
 *
 *  - **Wall clock** appears here only as a label. No duration is ever derived
 *    from two readings of it.
 *
 * ---------------------------------------------------------------------------
 * Why the longest gap is not optional
 *
 * A sensor delivering its nominal average in two bursts an hour apart is not
 * delivering at that rate, and an epoch-based consumer would be silently
 * wrong. SleepLab rows S14 and S15 are the consequence of exactly this. So
 * `longestGapMs()` sits next to every rate, and the report writer refuses to
 * print one without the other.
 *
 * ---------------------------------------------------------------------------
 * Streaming or event, measured rather than assumed
 *
 * `TOUCH_DETECT` was assumed streaming, delivered zero samples in a minute, and
 * read as "not worn" -- which would have suppressed every night SleepLab ever
 * recorded (ledger row S12). One touch sample in 507 minutes is the measured
 * behaviour (row S7). So the classification is derived from the delivered dt
 * distribution and nothing else, and for an event sensor the dt claims become
 * INAPPLICABLE rather than staying UNVERIFIED for ever.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_STREAMSTATS_HPP
#define SENSORLAB_STREAMSTATS_HPP

#include <cstddef>
#include <cstdint>

#include "Stats/Histogram.hpp"

namespace SensorLab::Stats
{

/// What a stream turned out to be.
enum class Cadence : uint8_t
{
    /// Not enough delivery to say. The honest state, and where a sensor that
    /// resolved and then said nothing stays.
    Unknown = 0,
    /// Periodic samples: the dt distribution is tight around a period.
    Streaming,
    /// Publishes on a change of state. dt statistics do not apply and the rate
    /// to report is events per hour.
    Event,
};

const char *toString(Cadence c);

/// dt above which a sample is not part of a periodic stream.
///
/// One second. Justification: the fastest event sensor on this platform is
/// requested at 1000 ms by every app in this repository, and the slowest thing
/// anyone would call streaming is `HEART_RATE`, which delivered exactly its
/// requested 1 Hz on hardware (ledger row S3). So a stream whose median dt is
/// at or under a second is periodic and one above it is not. TODO: re-derive
/// from the layer 3 sweep across all 37 types -- this is the only threshold in
/// the classifier and it is currently reasoned rather than measured.
constexpr uint32_t kStreamingMedianMaxMs = 1000;

/// Samples needed before a cadence is asserted at all.
///
/// Sixty: one minute of the slowest plausible stream. Below it,
/// `Cadence::Unknown`, which is what `TOUCH_DETECT`'s one sample in 507 minutes
/// correctly reports.
constexpr uint32_t kCadenceMinSamples = 60;

/// A stream whose dt p95 is within this multiple of its p50 is periodic.
///
/// Three. A perfectly periodic stream has p95/p50 = 1; the accelerometer's
/// measured ~48 Hz against a 25 Hz request is still tight (row S3), so the
/// factor is not about rate accuracy but about *shape*. An event sensor's dt
/// distribution spans seconds to hours, which is orders of magnitude, so three
/// separates them with room to spare. TODO: replace with the measured
/// distribution of p95/p50 across all types once Tier 2 has run one.
constexpr float kPeriodicSpreadFactor = 3.0f;

/**
 * @brief Delivery and timing for one subscribed sensor type.
 *
 * One of these per connection, held by value in the service. No allocation, and
 * the sample path is a handful of adds and compares -- which matters because
 * this app's sample path is the hottest in the repository.
 */
class StreamStats
{
public:
    StreamStats() { reset(); }

    void reset();

    // -- Batch arrival -------------------------------------------------------

    /**
     * @brief A batch arrived.
     *
     * Call before feeding its samples. @p arrivalUptimeMs is the loop's clock,
     * not the sensor's: batch *arrival* jitter is a different quantity from
     * sample dt and conflating them was how a requested 5000 ms latency read as
     * honoured when the batches were 195 ms apart (ledger row S17).
     */
    void onBatch(uint32_t arrivalUptimeMs, uint16_t sampleCount, uint16_t fieldCount);

    // -- Samples -------------------------------------------------------------

    /**
     * @brief One sample's timestamps, exactly as the frame carried them.
     *
     * @param tsMs   `Data::mTimeStamp`.
     * @param tsUs   `Data::mTimeStampUs`.
     *
     * Both are recorded raw. The us-invariant check needs `tsUs` unmodified,
     * and a helper that had already folded them into a single microsecond value
     * would have destroyed the evidence on the way in.
     */
    void onSample(uint32_t tsMs, uint32_t tsUs);

    /// Uptime at the moment `connect()` returned, for `firstSampleMs`.
    void onConnected(uint32_t uptimeMs);

    // -- Layer 3 -------------------------------------------------------------

    uint32_t samples() const { return mSamples; }
    uint32_t batches() const { return mBatches; }

    /// connect() to first sample, or false if none has arrived. An event sensor
    /// may legitimately never produce one; a streaming sensor taking 40 s to
    /// start is a finding.
    bool     hasFirstSample()  const { return mHaveFirst; }
    uint32_t firstSampleMs()   const { return mFirstSampleMs; }

    /// Largest gap between consecutive sample timestamps, in ms. Never report a
    /// rate without this.
    uint32_t longestGapMs() const { return mLongestGapMs; }

    /// Span of sensor timestamps from first to last, in ms. Rates are derived
    /// from this rather than from the loop's clock, so a rate is a property of
    /// the sensor pipeline rather than of the service's scheduling.
    uint32_t timestampSpanMs() const;

    /// Samples per minute over the timestamp span, or 0 if the span is too
    /// short to divide by. Uses the *sensor's* span, deliberately.
    float samplesPerMinute() const;
    /// Batches per minute over the arrival span.
    float batchesPerMinute() const;

    const BatchHistogram &batchSizes() const { return mBatchSizes; }

    /// The classification, from the data. See the header note.
    Cadence cadence() const;

    // -- Layer 2 -------------------------------------------------------------

    /// Delivered field count, from the batch stride. Zero until a batch has
    /// arrived.
    uint16_t fieldCount() const { return mFieldCount; }
    /// Whether the delivered field count ever changed within this run.
    /// `RUNNING_CADENCE`'s 4 -> 2 shrink between firmware lines says the answer
    /// is not automatically no.
    bool     fieldCountStable() const { return !mFieldCountChanged; }
    /// The other field count seen, if it changed. Both are reported, because
    /// "it changed" without saying to what is not actionable.
    uint16_t fieldCountAlternate() const { return mFieldCountAlt; }

    // -- Layer 4 -------------------------------------------------------------

    const DtHistogram &dt() const { return mDt; }
    /// Batch arrival intervals, separately from sample dt.
    const DtHistogram &batchIntervals() const { return mBatchDt; }

    /// Samples whose `mTimeStampUs` exceeded 999, breaking the assumption
    /// `DataView::getTimestampUs()` is built on. Expected zero; a non-zero
    /// value means every microsecond timestamp in every app on this platform is
    /// wrong.
    uint32_t usOver999() const { return mUsOver999; }

    /// Samples whose timestamp went backwards relative to the previous one,
    /// within a batch or across batches. Counted rather than corrected: a
    /// pipeline that reorders is a finding, and silently sorting would hide it.
    uint32_t nonMonotonic() const { return mNonMonotonic; }

    /// Skew between the sensor's clock and the device's uptime.
    ///
    /// The offset `arrival - sampleTimestamp` at the first and most recent
    /// batch. A constant offset is a fact worth knowing; a growing one means
    /// they are different oscillators and no app should mix them. Reported in
    /// parts per million of the elapsed uptime.
    bool  hasSkew() const { return mSkewSamples >= 2; }
    float skewPpm() const;
    int32_t firstOffsetMs() const { return mFirstOffsetMs; }
    int32_t lastOffsetMs()  const { return mLastOffsetMs; }

private:
    DtHistogram    mDt;
    DtHistogram    mBatchDt;
    BatchHistogram mBatchSizes;

    uint32_t mSamples          = 0;
    uint32_t mBatches          = 0;

    uint32_t mConnectedAt      = 0;
    bool     mHaveConnected    = false;
    uint32_t mFirstSampleMs    = 0;
    bool     mHaveFirst        = false;

    uint32_t mFirstTs          = 0;
    uint32_t mLastTs           = 0;
    bool     mHaveTs           = false;
    uint32_t mLongestGapMs     = 0;

    uint32_t mFirstBatchAt     = 0;
    uint32_t mLastBatchAt      = 0;
    bool     mHaveBatchAt      = false;

    uint16_t mFieldCount       = 0;
    uint16_t mFieldCountAlt    = 0;
    bool     mFieldCountChanged = false;

    uint32_t mUsOver999        = 0;
    uint32_t mNonMonotonic     = 0;

    /// Set on the batch currently being fed, so a sample knows what uptime its
    /// batch arrived at without every caller passing it again.
    uint32_t mBatchArrivalMs   = 0;
    bool     mHaveBatchArrival = false;

    int32_t  mFirstOffsetMs    = 0;
    int32_t  mLastOffsetMs     = 0;
    uint32_t mSkewFirstUptime  = 0;
    uint32_t mSkewLastUptime   = 0;
    uint32_t mSkewSamples      = 0;
};

} // namespace SensorLab::Stats

#endif // SENSORLAB_STREAMSTATS_HPP
