/**
 ******************************************************************************
 * @file    Stats_test.cpp
 * @brief   Every statistic, against synthetic input with a known answer.
 ******************************************************************************
 *
 * These tests are the reason to believe any number this app ever prints.
 *
 * The prompt's Tier 0 requirement, verbatim: a stream at exactly 20 ms, one
 * with an injected 4 s gap, one with a timestamp that goes backwards, one
 * quantised to a known LSB, one that never changes, one containing a NaN.
 * Every one of them is below, and each asserts the *answer* rather than merely
 * that nothing crashed.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cmath>

#include "Stats/FieldStats.hpp"
#include "Stats/Histogram.hpp"
#include "Stats/StreamStats.hpp"

using namespace SensorLab::Stats;

namespace
{

/// Feed @p n samples at exactly @p periodMs, one per batch arriving on the same
/// clock. The simplest possible stream, and the baseline every other test is a
/// perturbation of.
void feedPeriodic(StreamStats &s, uint32_t n, uint32_t periodMs,
                  uint32_t startTs = 100000)
{
    s.onConnected(startTs);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t ts = startTs + i * periodMs;
        s.onBatch(ts, 1, 3);
        s.onSample(ts, 0);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Histogram
// ---------------------------------------------------------------------------

TEST(Histogram, QuantilesOfAUniformRampAreTheRampsOwnQuantiles)
{
    Histogram<128> h;
    h.reset(1.0f, 0.0f);
    // 0..99 inclusive, one each. p50 by nearest rank is the 50th value, 49.
    for (int i = 0; i < 100; i++) {
        h.add(static_cast<float>(i));
    }

    EXPECT_EQ(h.count(), 100u);
    // Exact, not a bin midpoint: extrema are kept outside the bins precisely so
    // a single 4 s gap in a 20 ms stream is not lost to the top bin.
    EXPECT_FLOAT_EQ(h.min(), 0.0f);
    EXPECT_FLOAT_EQ(h.max(), 99.0f);
    EXPECT_FLOAT_EQ(h.mean(), 49.5f);
    // Bin midpoints, so within half a bin width of the true order statistic.
    EXPECT_NEAR(h.quantile(0.5f),  49.0f, 0.5f);
    EXPECT_NEAR(h.quantile(0.05f),  4.0f, 0.5f);
    EXPECT_NEAR(h.quantile(0.95f), 94.0f, 0.5f);
}

TEST(Histogram, AValueBeyondTheLastBinIsCountedAndTheExactMaxIsKept)
{
    Histogram<8> h;
    h.reset(1.0f, 0.0f);
    for (int i = 0; i < 7; i++) {
        h.add(1.0f);
    }
    h.add(4000.0f);   // far past the last bin

    EXPECT_EQ(h.overflow(), 1u);
    // The exact max survives, which is the whole point: a rate that looked fine
    // and a four-second gap have to be reportable from the same object.
    EXPECT_FLOAT_EQ(h.max(), 4000.0f);
    // ...and the top quantile reports the exact max rather than inventing a bin
    // centre it has no evidence for.
    EXPECT_FLOAT_EQ(h.quantile(0.999f), 4000.0f);
}

TEST(Histogram, AnEmptyHistogramReportsZeroRatherThanGarbage)
{
    Histogram<16> h;
    h.reset(1.0f);
    EXPECT_EQ(h.count(), 0u);
    EXPECT_FLOAT_EQ(h.mean(), 0.0f);
    EXPECT_FLOAT_EQ(h.quantile(0.5f), 0.0f);
}

// ---------------------------------------------------------------------------
// StreamStats: the six required streams
// ---------------------------------------------------------------------------

TEST(StreamStats, AStreamAtExactlyTwentyMsReportsFiftyHzAndNoGap)
{
    StreamStats s;
    feedPeriodic(s, 20000, 20);

    EXPECT_EQ(s.samples(), 20000u);
    EXPECT_EQ(s.nonMonotonic(), 0u);
    EXPECT_EQ(s.usOver999(), 0u);

    // dt is exactly 20 ms every time, so every quantile is the same bin.
    EXPECT_NEAR(s.dt().quantile(0.5f),  20.0f, 0.5f);
    EXPECT_NEAR(s.dt().quantile(0.05f), 20.0f, 0.5f);
    EXPECT_NEAR(s.dt().quantile(0.95f), 20.0f, 0.5f);
    EXPECT_FLOAT_EQ(s.dt().min(), 20.0f);
    EXPECT_FLOAT_EQ(s.dt().max(), 20.0f);

    // The longest gap is the period itself, which is the honest answer for a
    // perfectly regular stream -- and it is what makes "a rate is never reported
    // without its longest gap" a useful rule rather than a noisy one.
    EXPECT_EQ(s.longestGapMs(), 20u);

    // 50 Hz is 3000 samples a minute. Computed from n-1 intervals over the
    // sensor's own timestamp span, so a 20000-sample run is within 0.005 %.
    EXPECT_NEAR(s.samplesPerMinute(), 3000.0f, 1.0f);
    EXPECT_EQ(s.cadence(), Cadence::Streaming);
}

TEST(StreamStats, AFourSecondGapSurvivesInLongestGapAndNotInTheMedian)
{
    StreamStats s;
    s.onConnected(0);
    uint32_t ts = 1000;
    for (int i = 0; i < 500; i++) {
        s.onBatch(ts, 1, 3);
        s.onSample(ts, 0);
        ts += 20;
    }
    // The outage. Delivery stops for four seconds and then resumes.
    ts += 4000;
    for (int i = 0; i < 500; i++) {
        s.onBatch(ts, 1, 3);
        s.onSample(ts, 0);
        ts += 20;
    }

    // **This is the assertion the rule exists for.** The median is untouched --
    // 999 intervals of which one is long -- so a report that printed only a rate
    // would say the stream was healthy.
    EXPECT_NEAR(s.dt().quantile(0.5f), 20.0f, 0.5f);
    // ...and the gap is exact, because the extrema live outside the bins.
    EXPECT_EQ(s.longestGapMs(), 4020u);
    EXPECT_FLOAT_EQ(s.dt().max(), 4020.0f);
    // The gap is past the last 1 ms bin, so it is in the overflow -- and the
    // overflow count is what says the max is not a bin artefact.
    EXPECT_EQ(s.dt().overflow(), 1u);
}

TEST(StreamStats, ATimestampGoingBackwardsIsCountedAndNeverBecomesADt)
{
    StreamStats s;
    s.onConnected(0);
    s.onBatch(1000, 1, 3);
    s.onSample(1000, 0);
    s.onBatch(1020, 1, 3);
    s.onSample(1020, 0);
    // Backwards. Counted, never corrected: a pipeline that reorders is a
    // finding, and sorting it here would hide it.
    s.onBatch(1040, 1, 3);
    s.onSample(900, 0);
    s.onBatch(1060, 1, 3);
    s.onSample(920, 0);

    EXPECT_EQ(s.nonMonotonic(), 1u);
    EXPECT_EQ(s.samples(), 4u);
    // Three intervals were possible; the backwards one contributed none, because
    // a negative interval is not an interval.
    EXPECT_EQ(s.dt().count(), 2u);
    EXPECT_FLOAT_EQ(s.dt().max(), 20.0f);
}

TEST(StreamStats, ATimestampUsFieldOverNineNineNineIsCounted)
{
    StreamStats s;
    s.onConnected(0);
    for (uint32_t i = 0; i < 10; i++) {
        s.onBatch(1000 + i * 20, 1, 3);
        // The invariant `DataView::getTimestampUs()` is built on, and which
        // nothing in either repository has ever checked. If this is ever
        // non-zero on hardware, every microsecond timestamp in every app on this
        // platform is wrong.
        s.onSample(1000 + i * 20, (i % 2 == 0) ? 1500u : 500u);
    }
    EXPECT_EQ(s.usOver999(), 5u);
}

TEST(StreamStats, AnEventSensorWithOneSampleIsClassifiedUnknownNotStreaming)
{
    // `TOUCH_DETECT`'s measured behaviour: one sample in 507 minutes (row S7).
    // Classifying this as streaming is what made SleepLab read a sample-less
    // epoch as "not worn", which would have suppressed every night it recorded.
    StreamStats s;
    s.onConnected(0);
    s.onBatch(1000, 1, 1);
    s.onSample(1000, 0);

    EXPECT_EQ(s.cadence(), Cadence::Unknown);
    EXPECT_EQ(s.samples(), 1u);
    // No rate can be computed from one sample, and none is invented.
    EXPECT_FLOAT_EQ(s.samplesPerMinute(), 0.0f);
}

TEST(StreamStats, AStreamThatPublishesOnStateChangeIsClassifiedEvent)
{
    // Sixty events over an hour, irregularly: the shape of a state-change
    // publisher that happened to change state often enough to be counted.
    StreamStats s;
    s.onConnected(0);
    uint32_t ts = 1000;
    for (int i = 0; i < 100; i++) {
        s.onBatch(ts, 1, 1);
        s.onSample(ts, 0);
        // Intervals spanning three orders of magnitude, which is what separates
        // an event sensor from a jittery stream.
        ts += (i % 4 == 0) ? 50u : 30000u;
    }
    EXPECT_EQ(s.cadence(), Cadence::Event);
}

TEST(StreamStats, TheFieldCountComesFromTheStrideAndAChangeIsRecorded)
{
    StreamStats s;
    s.onConnected(0);
    s.onBatch(1000, 1, 4);
    s.onSample(1000, 0);
    EXPECT_EQ(s.fieldCount(), 4u);
    EXPECT_TRUE(s.fieldCountStable());

    // `RUNNING_CADENCE`'s `Field::COUNT` shrank from 4 to 2 between firmware
    // lines. A frame's width has already changed once, which is the single best
    // argument for this app existing -- so a change inside one run is recorded
    // rather than assumed impossible.
    s.onBatch(1200, 1, 2);
    s.onSample(1200, 0);
    EXPECT_FALSE(s.fieldCountStable());
    EXPECT_EQ(s.fieldCountAlternate(), 2u);
}

TEST(StreamStats, BatchArrivalJitterIsSeparateFromSampleDt)
{
    // Ledger row S17: 5000 ms latency requested, 195 ms delivered, at ~48 Hz.
    // Conflating the two quantities is how the requested latency read as
    // honoured.
    StreamStats s;
    s.onConnected(0);
    uint32_t at = 10000;
    for (int b = 0; b < 100; b++) {
        const uint16_t n = 9;          // ~195 ms / 21 ms
        s.onBatch(at, n, 3);
        for (uint16_t i = 0; i < n; i++) {
            s.onSample(at - 195 + i * 21, 0);
        }
        at += 195;
    }

    // Sample dt is ~21 ms; batch interval is ~195 ms. Both reported, separately.
    EXPECT_NEAR(s.dt().quantile(0.5f), 21.0f, 2.0f);
    EXPECT_NEAR(s.batchIntervals().quantile(0.5f), 195.0f, 1.0f);
    EXPECT_NEAR(s.batchSizes().quantile(0.5f), 9.0f, 1.0f);
}

TEST(StreamStats, ClockSkewIsReportedInPpmAndItsSignIsTheDriftDirection)
{
    // The sensor's clock runs slow against the device's by 1 ms per second, so
    // the offset grows by 1000 ppm.
    StreamStats s;
    s.onConnected(0);
    for (uint32_t sec = 0; sec < 600; sec++) {
        const uint32_t arrival = 100000 + sec * 1000;
        const uint32_t ts      = arrival - sec;    // drifting behind
        s.onBatch(arrival, 1, 3);
        s.onSample(ts, 0);
    }

    ASSERT_TRUE(s.hasSkew());
    EXPECT_NEAR(s.skewPpm(), 1000.0f, 20.0f);
    EXPECT_LT(s.firstOffsetMs(), s.lastOffsetMs());
}

TEST(StreamStats, EveryDurationSurvivesTheUptimeWrap)
{
    // `getTimeMs()` wraps at ~49.7 days and nobody has observed it. This app
    // cannot wait for it, but it can prove its own arithmetic against a
    // synthetic one -- the way SleepLab's ledger row P9a was earned.
    const uint32_t nearWrap = 0xFFFFFF00u;

    StreamStats across;
    feedPeriodic(across, 20000, 20, nearWrap);

    StreamStats away;
    feedPeriodic(away, 20000, 20, 100000);

    // Identical, because every comparison is an unsigned difference rather than
    // a magnitude compare. A single `>` anywhere in StreamStats would make the
    // first of these report a 49-day span.
    EXPECT_EQ(across.timestampSpanMs(), away.timestampSpanMs());
    EXPECT_EQ(across.longestGapMs(),    away.longestGapMs());
    EXPECT_EQ(across.nonMonotonic(),    away.nonMonotonic());
    EXPECT_NEAR(across.samplesPerMinute(), away.samplesPerMinute(), 0.01f);
    EXPECT_EQ(across.cadence(), away.cadence());
}

// ---------------------------------------------------------------------------
// FieldStats
// ---------------------------------------------------------------------------

TEST(FieldStats, AQuantisedFieldRecoversTheStepItWasQuantisedTo)
{
    // A float field fed from a 16-bit ADC at +-2 g: the LSB is 4/65536 g,
    // about 61 ug. Recovering it is how you learn the configured full-scale
    // range without reading a register.
    const float lsb = 4.0f / 65536.0f;

    FieldStats f;
    f.reset(false);
    for (int i = 0; i < 10000; i++) {
        // A slow ramp, so consecutive samples differ by exactly one step some of
        // the time and by more the rest.
        const float ideal   = static_cast<float>(i) * lsb * 0.37f;
        const float steps   = ideal / lsb;
        const float snapped = static_cast<float>(
                                  static_cast<long long>(steps + 0.5f)) * lsb;
        uint32_t bits;
        std::memcpy(&bits, &snapped, sizeof(bits));
        f.add(bits, snapped);
    }

    ASSERT_TRUE(f.hasLsb());
    // A **lower bound** on the quantisation step, converging downwards to it --
    // consecutive samples of a quantised signal differ by an integer multiple of
    // the step, so an observed difference can never be smaller than the truth.
    EXPECT_NEAR(f.lsb(), lsb, lsb * 0.01f);
    EXPECT_GE(f.lsb(), lsb * 0.999f);
}

TEST(FieldStats, AFieldThatNeverChangesReportsNoLsbAndItsWholeRunAsStuck)
{
    // `BATTERY_LEVEL` read 100.0 % at both ends of an 8.45 h night in which the
    // fuel gauge lost 10 mAh (row S18). Not broken enough to be absent, not
    // working enough to be usable: the hardest failure mode to notice and the
    // most important to publish.
    FieldStats f;
    f.reset(false);
    const float v = 100.0f;
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 5000; i++) {
        f.add(bits, v);
    }

    EXPECT_EQ(f.count(), 5000u);
    EXPECT_FALSE(f.everChanged());
    EXPECT_EQ(f.stuckMaxRun(), 5000u);
    // An LSB from an unvarying value would be meaningless, so none is reported
    // -- the caveat is enforced rather than documented.
    EXPECT_FALSE(f.hasLsb());
    EXPECT_FLOAT_EQ(f.min(), 100.0f);
    EXPECT_FLOAT_EQ(f.max(), 100.0f);
    EXPECT_FLOAT_EQ(f.mean(), 100.0f);
}

TEST(FieldStats, ANaNIsCountedAndNeverIntegrated)
{
    // One non-finite sample once made every *subsequent* SleepLab epoch exactly
    // zero. They are not hypothetical.
    FieldStats f;
    f.reset(false);

    const float ok = 2.0f;
    uint32_t okBits;
    std::memcpy(&okBits, &ok, sizeof(okBits));

    for (int i = 0; i < 99; i++) {
        f.add(okBits, ok);
    }
    f.add(0x7FC00000u, std::nanf(""));   // quiet NaN

    EXPECT_EQ(f.nonFinite(), 1u);
    // 99 finite samples, not 100: the NaN is excluded from the count as well as
    // from the sum, so a mean is never a mean of poison.
    EXPECT_EQ(f.count(), 99u);
    EXPECT_FLOAT_EQ(f.mean(), 2.0f);
    EXPECT_FALSE(std::isnan(f.min()));
    EXPECT_FALSE(std::isnan(f.max()));
}

TEST(FieldStats, InfinityIsCountedSeparatelyFromNaNsEffectOnTheMean)
{
    FieldStats f;
    f.reset(false);
    const float ok = 1.0f;
    uint32_t okBits;
    std::memcpy(&okBits, &ok, sizeof(okBits));
    for (int i = 0; i < 10; i++) {
        f.add(okBits, ok);
    }
    f.add(0x7F800000u, std::numeric_limits<float>::infinity());
    f.add(0xFF800000u, -std::numeric_limits<float>::infinity());

    EXPECT_EQ(f.nonFinite(), 2u);
    EXPECT_EQ(f.count(), 10u);
    EXPECT_FLOAT_EQ(f.mean(), 1.0f);
}

TEST(FieldStats, AStuckFieldThatEventuallyMovesReportsTheLongestRunNotTheLast)
{
    FieldStats f;
    f.reset(false);
    const float a = 1.0f, b = 2.0f;
    uint32_t aBits, bBits;
    std::memcpy(&aBits, &a, sizeof(aBits));
    std::memcpy(&bBits, &b, sizeof(bBits));

    for (int i = 0; i < 100; i++) { f.add(aBits, a); }   // a run of 100
    for (int i = 0; i < 5; i++)   { f.add(bBits, b); }   // then a run of 5

    EXPECT_TRUE(f.everChanged());
    EXPECT_EQ(f.stuckMaxRun(), 100u);
    ASSERT_TRUE(f.hasLsb());
    EXPECT_FLOAT_EQ(f.lsb(), 1.0f);
}

TEST(FieldStats, AnEnumFieldsDistinctValuesAreTrackedAndOverflowIsTheAnswer)
{
    // `MOTION_DETECT` documents four values. A field that produced more than
    // sixteen distinct ones is not an enum, and that is itself the finding.
    FieldStats f;
    f.reset(true);
    for (int i = 0; i < 1000; i++) {
        const uint32_t v = static_cast<uint32_t>(i % 3);
        f.add(v, static_cast<float>(v));
    }
    EXPECT_EQ(f.distinctCount(), 3u);
    EXPECT_FALSE(f.distinctOverflowed());
    // First-seen order, so a value outside the documented set can be named
    // rather than merely counted.
    EXPECT_EQ(f.distinctValue(0), 0u);
    EXPECT_EQ(f.distinctValue(1), 1u);
    EXPECT_EQ(f.distinctValue(2), 2u);

    FieldStats wide;
    wide.reset(true);
    for (uint32_t i = 0; i < 100; i++) {
        wide.add(i, static_cast<float>(i));
    }
    EXPECT_TRUE(wide.distinctOverflowed());
    EXPECT_EQ(wide.distinctCount(), SensorLab::Stats::kDistinctMax);
}

TEST(FieldStats, TheStuckTestIsOnTheBitsSoTwoDifferentNaNsAreNotTheSameValue)
{
    // Deliberate: a field stuck at one particular NaN is stuck, and a field
    // producing varying NaN payloads is doing something else. Comparing as
    // floats would make every NaN unequal to itself and every run length 1.
    FieldStats f;
    f.reset(false);
    for (int i = 0; i < 10; i++) {
        f.add(0x7FC00000u, std::nanf(""));
    }
    EXPECT_EQ(f.nonFinite(), 10u);
    EXPECT_FALSE(f.everChanged());
    EXPECT_EQ(f.stuckMaxRun(), 10u);
}
