#include <gtest/gtest.h>

#include <vector>

#include "SweepStats.hpp"

using namespace Haptic;

namespace {

/// A synthetic wrist: gravity on one axis, a little noise, and a burst of
/// actuator ringing between two sample indices.
std::vector<ImuSample> synthesise(uint16_t samples, uint16_t burstFrom, uint16_t burstTo,
                                  int16_t burstAmplitude, int16_t noise = 3,
                                  uint32_t periodUs = 10000)
{
    std::vector<ImuSample> out;
    out.reserve(samples);
    for (uint16_t i = 0; i < samples; i++) {
        ImuSample sample {};
        sample.timestampUs = static_cast<uint64_t>(i) * periodUs;
        // Alternating so the sample-to-sample difference is the amplitude
        // rather than zero, which is what the actuator looks like at a carrier
        // frequency well above the sample rate.
        const int16_t sign = (i % 2 == 0) ? 1 : -1;
        sample.ax = static_cast<int16_t>(sign * noise);
        sample.ay = 0;
        sample.az = 4096;
        if (i >= burstFrom && i <= burstTo) {
            sample.ax = static_cast<int16_t>(sign * burstAmplitude);
        }
        out.push_back(sample);
    }
    return out;
}

SweepWindow run(const std::vector<ImuSample> &samples, uint32_t baseline)
{
    SweepWindow window;
    window.begin(0, baseline);
    for (const ImuSample &sample : samples) {
        window.add(sample);
    }
    return window;
}

} // namespace

TEST(DeltaStats, TheFirstSampleContributesNothingBecauseThereIsNothingToDifference)
{
    DeltaStats stats;
    ImuSample  sample {};
    sample.ax = 100;
    EXPECT_EQ(stats.add(sample), 0u);
    EXPECT_EQ(stats.count(), 0);
    EXPECT_EQ(stats.mean(), 0u);
    EXPECT_EQ(stats.peak(), 0u);
}

TEST(DeltaStats, TheDeltaIsTheSumOfTheThreeAxesAbsoluteChanges)
{
    DeltaStats stats;
    ImuSample  a {};
    a.ax = 10;
    a.ay = -20;
    a.az = 4096;
    ImuSample b = a;
    b.ax        = 13;
    b.ay        = -24;
    b.az        = 4090;

    stats.add(a);
    EXPECT_EQ(stats.add(b), 3u + 4u + 6u);
    EXPECT_EQ(stats.mean(), 13u);
    EXPECT_EQ(stats.peak(), 13u);
    EXPECT_EQ(stats.count(), 1);
}

TEST(DeltaStats, ResetForgetsThePreviousSampleRatherThanDifferencingAcrossTheGap)
{
    DeltaStats stats;
    ImuSample  a {};
    a.ax = 0;
    ImuSample b {};
    b.ax = 30000;

    stats.add(a);
    stats.reset();
    EXPECT_EQ(stats.add(b), 0u) << "the reset left the previous sample behind";
    EXPECT_EQ(stats.count(), 0);
}

TEST(SweepWindow, ABurstIsFoundWhereItWasPutAndItsDurationIsTheGapBetweenTheEdges)
{
    const std::vector<ImuSample> samples = synthesise(200, 50, 130, 400);
    const SweepWindow            window  = run(samples, /*baseline=*/6);

    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    ASSERT_NE(window.offsetUs(), SweepWindow::kNoEdge);
    // The onset needs `kOnsetRunLength` samples over the threshold before it is
    // believed, so it lands a sample or two into the burst.
    EXPECT_GE(window.onsetUs(), 50 * 10000);
    EXPECT_LE(window.onsetUs(), 53 * 10000);
    EXPECT_GE(window.offsetUs(), 129 * 10000);
    EXPECT_LE(window.offsetUs(), 131 * 10000);
    EXPECT_NEAR(static_cast<double>(window.durationUs()), 80.0 * 10000, 4.0 * 10000);
    EXPECT_EQ(window.samples(), 199);
}

/// An effect index that plays nothing has no onset, and that is a real result
/// rather than a missing measurement.
TEST(SweepWindow, AnEffectThatPlaysNothingHasNoEdgesAtAll)
{
    const std::vector<ImuSample> samples = synthesise(200, 0, 0, 0);
    const SweepWindow            window  = run(samples, /*baseline=*/6);

    EXPECT_EQ(window.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(window.offsetUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(window.durationUs(), SweepWindow::kNoEdge);
    EXPECT_GT(window.samples(), 0) << "the samples themselves went missing";
}

/// One stray sample over the threshold is a knock, not an effect.
TEST(SweepWindow, ASingleStraySampleIsNotAnOnset)
{
    std::vector<ImuSample> samples = synthesise(200, 0, 0, 0);
    samples[80].ax = 4000;

    const SweepWindow window = run(samples, /*baseline=*/6);
    EXPECT_EQ(window.onsetUs(), SweepWindow::kNoEdge)
        << "one sample was enough to declare an effect";
}

TEST(SweepWindow, TheThresholdIsTakenFromTheBaselineAndFloored)
{
    SweepWindow quiet;
    quiet.begin(0, /*baselineMeanDelta=*/0);
    EXPECT_EQ(quiet.threshold(), kOnsetFloorLsb)
        << "an implausibly quiet baseline made every sample an onset";

    SweepWindow busy;
    busy.begin(0, /*baselineMeanDelta=*/100);
    EXPECT_EQ(busy.threshold(), 100 * kOnsetFactorNumerator / kOnsetFactorDenominator);
}

/// A wrist that is already moving raises the bar, which is the point of
/// measuring the baseline per repeat rather than once.
TEST(SweepWindow, AMovingWristRaisesTheBarRatherThanReportingItsOwnMotion)
{
    // A burst that clears the raised bar; one that did not would be a quiet
    // effect on a moving wrist, which is a different result.
    const std::vector<ImuSample> samples = synthesise(200, 50, 130, 1200, /*noise=*/120);

    const SweepWindow strict = run(samples, /*baseline=*/240);
    const SweepWindow loose  = run(samples, /*baseline=*/6);
    EXPECT_GT(strict.threshold(), loose.threshold());
    // With the bar raised the wrist's own motion is no longer an onset, and the
    // burst still is.
    ASSERT_NE(strict.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_GE(strict.onsetUs(), 50 * 10000);
}

TEST(SweepWindow, TheEnergyProxyRisesWithTheBurstAmplitude)
{
    const SweepWindow weak   = run(synthesise(200, 50, 130, 200), 6);
    const SweepWindow strong = run(synthesise(200, 50, 130, 800), 6);
    EXPECT_LT(weak.meanDelta(), strong.meanDelta());
    EXPECT_LT(weak.peakDelta(), strong.peakDelta());
}

/// The delivered rate is what decides the resolution of an edge, and it is
/// 10 ms per sample rather than the 20.6 ms an accelerometer-only subscription
/// gives. Both are far below an LRA carrier, so nothing here is a waveform.
TEST(SweepWindow, AnEdgeIsOnlyEverAsPreciseAsOneSamplePeriod)
{
    const std::vector<ImuSample> fast = synthesise(400, 100, 260, 400, 3, /*periodUs=*/10000);
    const std::vector<ImuSample> slow = synthesise(200, 50, 130, 400, 3, /*periodUs=*/20625);

    const SweepWindow a = run(fast, 6);
    const SweepWindow b = run(slow, 6);
    ASSERT_NE(a.durationUs(), SweepWindow::kNoEdge);
    ASSERT_NE(b.durationUs(), SweepWindow::kNoEdge);
    // The same 1.6 s burst, and the faster stream brackets it more tightly.
    EXPECT_NEAR(static_cast<double>(a.durationUs()), 1600000.0, 30000.0);
    EXPECT_NEAR(static_cast<double>(b.durationUs()), 1650000.0, 70000.0);
}

TEST(SelfTimedEffects, OnlyTheTwoThatNameADurationDeclareOne)
{
    EXPECT_EQ(declaredDurationMs(15), 750);
    EXPECT_EQ(declaredDurationMs(16), 1000);
    for (uint8_t effect = 1; effect <= kMaxEffect; effect++) {
        if (effect != 15 && effect != 16) {
            EXPECT_EQ(declaredDurationMs(effect), 0) << int(effect);
        }
    }
}
