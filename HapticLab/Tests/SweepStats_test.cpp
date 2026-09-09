#include <gtest/gtest.h>

#include <vector>

#include "SweepStats.hpp"

using namespace Haptic;

namespace {

constexpr uint32_t kPeriodUs = 10000;

/// A synthetic wrist: gravity on one axis, a little noise, and a burst of
/// actuator ringing between two sample indices.
std::vector<ImuSample> synthesise(uint16_t samples, uint16_t burstFrom, uint16_t burstTo,
                                  int16_t burstAmplitude, int16_t noise = 3,
                                  uint32_t periodUs = kPeriodUs, uint64_t firstUs = 0)
{
    std::vector<ImuSample> out;
    out.reserve(samples);
    for (uint16_t i = 0; i < samples; i++) {
        ImuSample sample {};
        sample.timestampUs = firstUs + static_cast<uint64_t>(i) * periodUs;
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

/// One repeat the way the Service runs it: the origin at the start of the
/// quiet stretch, the play a given number of samples in, every sample offered,
/// then the split.
SweepWindow run(const std::vector<ImuSample> &samples, uint16_t playIndex,
                uint32_t periodUs = kPeriodUs)
{
    SweepWindow window;
    window.begin(0);
    window.notePlay(static_cast<uint64_t>(playIndex) * periodUs);
    for (const ImuSample &sample : samples) {
        window.add(sample);
    }
    window.finish();
    return window;
}

/// The noise the synthetic wrist produces between samples, which is what the
/// quiet stretch should measure.
uint32_t quietDelta(int16_t noise)
{
    return static_cast<uint32_t>(2 * noise);
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
    const SweepWindow            window  = run(samples, /*playIndex=*/40);

    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    ASSERT_NE(window.offsetUs(), SweepWindow::kNoEdge);
    // Both edges are relative to the play, which is ten samples before the
    // burst. The onset needs `kOnsetRunLength` samples over the threshold
    // before it is believed, but it reports the first of them.
    EXPECT_EQ(window.onsetUs(), 10 * kPeriodUs);
    EXPECT_GE(window.offsetUs(), 89 * kPeriodUs);
    EXPECT_LE(window.offsetUs(), 92 * kPeriodUs);
    EXPECT_NEAR(static_cast<double>(window.durationUs()), 81.0 * kPeriodUs, 2.0 * kPeriodUs);
}

/// The defect the second sweep exposed. Samples reach the app several hundred
/// milliseconds after they were taken, so a whole repeat can be handed over
/// after its play instant is already known. Filing by arrival left the quiet
/// stretch empty and put an onset before the play in nearly two rows in five.
TEST(SweepWindow, ARepeatDeliveredEntirelyAfterThePlayStillFindsItsQuietStretch)
{
    const std::vector<ImuSample> samples = synthesise(310, 100, 112, 400);

    SweepWindow window;
    window.begin(0);
    window.notePlay(100 * kPeriodUs);
    // Nothing was offered before `notePlay`, which is what a delivery lag
    // longer than the quiet stretch looks like.
    for (const ImuSample &sample : samples) {
        window.add(sample);
    }
    window.finish();

    EXPECT_TRUE(window.hadBaseline()) << "the quiet stretch went uncounted";
    EXPECT_EQ(window.baselineDelta(), quietDelta(3))
        << "the burst was averaged into the quiet stretch it is measured against";
    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_GE(window.onsetUs(), 0) << "the effect started before it was asked for";
    EXPECT_EQ(window.onsetUs(), 0);
}

/// A missing baseline is reported rather than assumed, because the threshold
/// silently falls back to its floor and the row deserves the doubt.
TEST(SweepWindow, AWindowWithNothingBeforeThePlayAdmitsItHasNoBaseline)
{
    const SweepWindow window = run(synthesise(200, 50, 130, 400), /*playIndex=*/0);

    EXPECT_FALSE(window.hadBaseline());
    EXPECT_EQ(window.baselineDelta(), 0u);
    EXPECT_EQ(window.threshold(), kOnsetFloorLsb);
}

/// The previous repeat's ring-down, delivered late. Counting it as this
/// repeat's quiet stretch is the same contamination in a new disguise.
TEST(SweepWindow, TheTailOfThePreviousRepeatIsDroppedRatherThanCountedAsQuiet)
{
    const std::vector<ImuSample> samples = synthesise(200, 20, 40, 400);

    SweepWindow window;
    // The burst belongs to the repeat before this one, so it is behind the
    // origin even though it is in front of the play.
    window.begin(50 * kPeriodUs);
    window.notePlay(90 * kPeriodUs);
    for (const ImuSample &sample : samples) {
        window.add(sample);
    }
    window.finish();

    EXPECT_TRUE(window.hadBaseline());
    EXPECT_EQ(window.baselineDelta(), quietDelta(3));
    EXPECT_EQ(window.onsetUs(), SweepWindow::kNoEdge)
        << "the previous repeat's burst was reported as this one's effect";
}

/// An effect index that plays nothing has no onset, and that is a real result
/// rather than a missing measurement.
TEST(SweepWindow, AnEffectThatPlaysNothingHasNoEdgesAtAll)
{
    const SweepWindow window = run(synthesise(200, 0, 0, 0), /*playIndex=*/40);

    EXPECT_EQ(window.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(window.offsetUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(window.durationUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(window.samples(), 0);
    EXPECT_GT(window.windowSamples(), 0) << "the samples themselves went missing";
}

/// One stray sample over the threshold is a knock, not an effect.
TEST(SweepWindow, ASingleStraySampleIsNotAnOnset)
{
    std::vector<ImuSample> samples = synthesise(200, 0, 0, 0);
    samples[80].ax = 4000;

    const SweepWindow window = run(samples, /*playIndex=*/40);
    EXPECT_EQ(window.onsetUs(), SweepWindow::kNoEdge)
        << "one sample was enough to declare an effect";
}

TEST(SweepWindow, TheThresholdComesFromTheQuietStretchAndIsFloored)
{
    const SweepWindow quiet = run(synthesise(200, 50, 130, 400, /*noise=*/3), 40);
    EXPECT_EQ(quiet.baselineDelta(), quietDelta(3));
    EXPECT_EQ(quiet.threshold(), kOnsetFloorLsb)
        << "an implausibly quiet baseline made every sample an onset";

    const SweepWindow busy = run(synthesise(200, 50, 130, 1200, /*noise=*/60), 40);
    EXPECT_EQ(busy.baselineDelta(), quietDelta(60));
    EXPECT_EQ(busy.threshold(), quietDelta(60) * kOnsetFactorNumerator / kOnsetFactorDenominator);
}

/// A wrist that is already moving raises the bar, which is the point of
/// measuring the quiet stretch per repeat rather than once.
TEST(SweepWindow, AMovingWristRaisesTheBarRatherThanReportingItsOwnMotion)
{
    const SweepWindow still = run(synthesise(200, 50, 130, 1200, /*noise=*/3), 40);
    const SweepWindow moving = run(synthesise(200, 50, 130, 1200, /*noise=*/60), 40);

    EXPECT_GT(moving.threshold(), still.threshold());
    // With the bar raised the wrist's own motion is no longer an onset, and the
    // burst still is.
    ASSERT_NE(moving.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(moving.onsetUs(), 10 * kPeriodUs);
}

TEST(SweepWindow, TheEnergyProxyRisesWithTheBurstAmplitude)
{
    const SweepWindow weak   = run(synthesise(200, 50, 130, 200), 40);
    const SweepWindow strong = run(synthesise(200, 50, 130, 800), 40);
    EXPECT_LT(weak.meanDelta(), strong.meanDelta());
    EXPECT_LT(weak.peakDelta(), strong.peakDelta());
}

/// The energy proxy is the burst's, not the window's. Averaged over three
/// seconds of quiet, a click that peaks near 4600 comes out near 100, which is
/// what the first sweep's table said and why it said nothing useful.
TEST(SweepWindow, TheEnergyProxyIsTakenOverTheBurstRatherThanTheWholeWindow)
{
    const std::vector<ImuSample> samples = synthesise(310, 100, 112, 400);
    const SweepWindow            window  = run(samples, /*playIndex=*/90);

    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_GT(window.samples(), 0);
    EXPECT_LT(window.samples(), window.windowSamples())
        << "the burst is the whole window, so there was nothing to dilute";
    // The burst's own deltas are twice its amplitude; a window mean would be a
    // small fraction of that.
    EXPECT_GT(window.meanDelta(), 400u);
}

TEST(SweepWindow, AWindowLongerThanItsStorageSaysSoRatherThanDroppingTheTailQuietly)
{
    const std::vector<ImuSample> shortEnough = synthesise(kMaxWindowEntries, 50, 130, 400);
    EXPECT_FALSE(run(shortEnough, 40).overflowed());

    const std::vector<ImuSample> tooLong = synthesise(kMaxWindowEntries + 50, 50, 130, 400);
    const SweepWindow            window  = run(tooLong, 40);
    EXPECT_TRUE(window.overflowed());
    EXPECT_EQ(window.windowSamples(), kMaxWindowEntries);
}

/// The delivered rate is what decides the resolution of an edge, and it is
/// 10 ms per sample rather than the 20.6 ms an accelerometer-only subscription
/// gives. Both are far below an LRA carrier, so nothing here is a waveform.
TEST(SweepWindow, AnEdgeIsOnlyEverAsPreciseAsOneSamplePeriod)
{
    const SweepWindow a = run(synthesise(400, 100, 260, 400, 3, 10000), 90, 10000);
    const SweepWindow b = run(synthesise(200, 50, 130, 400, 3, 20625), 40, 20625);

    ASSERT_NE(a.durationUs(), SweepWindow::kNoEdge);
    ASSERT_NE(b.durationUs(), SweepWindow::kNoEdge);
    // The same 1.6 s burst, and the faster stream brackets it more tightly.
    EXPECT_NEAR(static_cast<double>(a.durationUs()), 1600000.0, 30000.0);
    EXPECT_NEAR(static_cast<double>(b.durationUs()), 1650000.0, 70000.0);
}

/// The defect the first sweep exposed: the offset was the last sample above
/// threshold anywhere in the window, so a wrist that moved a second after the
/// effect finished extended the effect. `STRONG_CLICK_100` came out as 2717 ms
/// in one repeat and 80 ms in the other.
TEST(SweepWindow, MotionLongAfterTheEffectDoesNotExtendIt)
{
    // A short burst, then a long quiet stretch, then the wrist moves.
    std::vector<ImuSample> samples = synthesise(300, 20, 28, 400);
    for (uint16_t i = 200; i < 220; i++) {
        samples[i].ax = static_cast<int16_t>((i % 2 == 0 ? 1 : -1) * 900);
    }

    const SweepWindow window = run(samples, /*playIndex=*/10);
    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    // The burst is samples 20 to 28, so about 90 ms at a 10 ms period. Without
    // the gap rule the offset would land at sample 220, near 2000 ms.
    EXPECT_LT(window.durationUs(), 200000)
        << "the later motion was counted as part of the effect";
    EXPECT_GT(window.durationUs(), 40000);
}

TEST(SweepWindow, AGapShorterThanTheRuleDoesNotEndTheBurst)
{
    // Two stretches 100 ms apart, which is inside kBurstGapMs, so they are one
    // effect: a pulsing waveform is exactly this shape.
    std::vector<ImuSample> samples = synthesise(300, 20, 30, 400);
    for (uint16_t i = 41; i < 52; i++) {
        samples[i].ax = static_cast<int16_t>((i % 2 == 0 ? 1 : -1) * 400);
    }

    const SweepWindow window = run(samples, /*playIndex=*/10);
    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_GT(window.durationUs(), 250000) << "a pulsing effect was cut at its first gap";
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

/// The window closes on data, not on a clock. A capture ended after a fixed
/// wall time ends on whatever has been delivered, and with a delivery lag the
/// burst can still be in flight.
TEST(SweepWindow, AWindowIsNotCoveredUntilASampleThatLateHasActuallyArrived)
{
    // A non-zero origin, because coverage is a span inside the window rather
    // than a reading of the sensor's own clock.
    const uint64_t               origin  = 500 * static_cast<uint64_t>(kPeriodUs);
    const std::vector<ImuSample> samples = synthesise(180, 100, 112, 400, 3, kPeriodUs, origin);

    SweepWindow empty;
    empty.begin(origin);
    empty.notePlay(origin);
    EXPECT_FALSE(empty.coversPlayPlus(0)) << "a window with no samples claimed to be complete";

    SweepWindow unplayed;
    unplayed.begin(origin);
    for (const ImuSample &sample : samples) {
        unplayed.add(sample);
    }
    EXPECT_FALSE(unplayed.coversPlayPlus(0))
        << "a window that has played nothing claimed to be complete";

    SweepWindow window;
    window.begin(origin);
    window.notePlay(origin + 100 * kPeriodUs);
    for (const ImuSample &sample : samples) {
        window.add(sample);
    }
    // Samples run to 179, so 79 periods past the play and no further.
    EXPECT_TRUE(window.coversPlayPlus(79 * kPeriodUs));
    EXPECT_FALSE(window.coversPlayPlus(80 * kPeriodUs));
}

/// A sample behind the origin belongs to the previous repeat, so it is no
/// evidence at all about how far this window has got.
TEST(SweepWindow, ThePreviousRepeatsTailDoesNotMakeAWindowLookCovered)
{
    SweepWindow window;
    window.begin(100 * kPeriodUs);
    window.notePlay(140 * kPeriodUs);
    for (const ImuSample &sample : synthesise(100, 20, 40, 400)) {
        window.add(sample);
    }
    EXPECT_FALSE(window.coversPlayPlus(0));
    EXPECT_EQ(window.windowSamples(), 0);
}

/// The actuator is still moving after it has stopped being driven, and a
/// threshold set by the wrist's quiet level cannot tell the two apart. Against
/// a baseline multiple the two effects that name a duration both read long, by
/// 5.7 % and 4.0 %; trimming the tail to a tenth of the burst's own peak puts
/// them within 1 % of what their names claim.
TEST(SweepWindow, TheRingDownIsNotCountedAsDrive)
{
    // Thirty samples of drive and then sixty of a tail at a twentieth of it,
    // which is above the wrist's quiet level and well under a tenth of the peak.
    std::vector<ImuSample> samples;
    for (uint16_t i = 0; i < 200; i++) {
        ImuSample sample {};
        sample.timestampUs = static_cast<uint64_t>(i) * kPeriodUs;
        const int16_t sign = (i % 2 == 0) ? 1 : -1;
        int16_t       amplitude = 3;
        if (i >= 50 && i <= 80) {
            amplitude = 400;
        } else if (i > 80 && i <= 140) {
            amplitude = 20;
        }
        sample.ax = static_cast<int16_t>(sign * amplitude);
        sample.az = 4096;
        samples.push_back(sample);
    }

    const SweepWindow window = run(samples, /*playIndex=*/40);

    ASSERT_NE(window.onsetUs(), SweepWindow::kNoEdge);
    EXPECT_EQ(window.onsetUs(), 10 * kPeriodUs);
    EXPECT_EQ(window.peakDelta(), 800u);
    // The tail's own deltas are 40, a twentieth of the peak, so the burst ends
    // at the last sample that still straddles the drive. Untrimmed the offset
    // would be at sample 140, a hundred periods on.
    EXPECT_EQ(window.offsetUs(), 41 * kPeriodUs);
    EXPECT_EQ(window.durationUs(), 31 * kPeriodUs);
}

/// The trim is a criterion and not a fitted constant: the answer has to be
/// insensitive to where in the reasonable range the cut is put. A burst with no
/// tail is unaffected by it at all.
TEST(SweepWindow, ABurstWithNoTailIsNotTrimmed)
{
    const SweepWindow window = run(synthesise(200, 50, 130, 400), /*playIndex=*/40);
    ASSERT_NE(window.offsetUs(), SweepWindow::kNoEdge);
    EXPECT_GE(window.offsetUs(), 89 * kPeriodUs)
        << "a flat burst lost samples to the ring-down trim";
}
