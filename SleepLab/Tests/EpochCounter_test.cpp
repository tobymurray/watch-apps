/**
 * Host tests for the activity-count derivation.
 *
 * The properties that matter here are not "does it produce a number" but the
 * three the whole downstream analysis assumes:
 *
 *   - a still wrist produces near-zero counts whatever orientation it is in,
 *   - a moving wrist produces counts that scale with how much it moved,
 *   - and **the count does not depend on the delivered sample rate**, because
 *     on this platform the delivered rate is not the requested rate and can
 *     differ by a factor of two between one night and the next.
 *
 * The last one is the reason the integral is dt-weighted, and it is the test
 * that would catch someone "simplifying" that away.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "Engine/EpochCounter.hpp"

namespace {

using Engine::EpochCounter;

constexpr float kPi = 3.14159265358979f;

/// Feed a constant orientation for @p durationMs at @p periodMs.
void feedStill(EpochCounter &c, uint32_t &t, uint32_t durationMs, uint32_t periodMs,
               float x, float y, float z)
{
    const uint32_t end = t + durationMs;
    for (; t < end; t += periodMs) {
        c.add(t, x, y, z);
    }
}

/// Feed a sinusoidal wrist movement of @p amplitudeG at @p freqHz, on the X
/// axis, on top of 1 g of gravity on Z.
void feedSine(EpochCounter &c, uint32_t &t, uint32_t durationMs, uint32_t periodMs,
              float amplitudeG, float freqHz)
{
    const uint32_t end = t + durationMs;
    for (; t < end; t += periodMs) {
        const float phase = 2.0f * kPi * freqHz * (static_cast<float>(t) * 0.001f);
        c.add(t, amplitudeG * std::sin(phase), 0.0f, 1.0f);
    }
}

struct Closed { uint32_t count; uint32_t peak; uint16_t samples; };

Closed close(EpochCounter &c)
{
    Closed r{};
    c.closeEpoch(r.count, r.peak, r.samples);
    return r;
}

// -- Stillness ---------------------------------------------------------------

TEST(EpochCounter, AStillWristProducesNearlyNoCount)
{
    EpochCounter c;
    uint32_t t = 0;
    // Prime past the filter's settling, then measure a clean 30 s epoch.
    feedStill(c, t, 5000, 40, 0.0f, 0.0f, 1.0f);
    close(c);
    feedStill(c, t, 30000, 40, 0.0f, 0.0f, 1.0f);

    const Closed r = close(c);
    EXPECT_LT(r.count, 5u) << "a perfectly still wrist should integrate to ~0";
    EXPECT_EQ(r.samples, 750u);
}

TEST(EpochCounter, OrientationDoesNotMatter)
{
    // The count is deviation from the gravity vector, so a wrist held still in
    // any orientation reads the same. If it did not, a sleeper who rolled onto
    // their side would show a raised count for the whole rest of the night.
    const float kOrientations[][3] = {
        { 0.0f, 0.0f,  1.0f },
        { 1.0f, 0.0f,  0.0f },
        { 0.0f, 1.0f,  0.0f },
        { 0.0f, 0.0f, -1.0f },
        { 0.577f, 0.577f, 0.577f },
    };

    for (const auto &o : kOrientations) {
        EpochCounter c;
        uint32_t t = 0;
        feedStill(c, t, 5000, 40, o[0], o[1], o[2]);
        close(c);
        feedStill(c, t, 30000, 40, o[0], o[1], o[2]);
        EXPECT_LT(close(c).count, 5u);
    }
}

TEST(EpochCounter, APosturalChangeCostsOneEpochAndNotTheRest)
{
    // Rolling over is a step change in the gravity vector. The high-pass lets
    // the transient through -- that is the movement -- and then settles. The
    // epoch *after* the roll must be as quiet as the one before it.
    EpochCounter c;
    uint32_t t = 0;

    feedStill(c, t, 5000, 40, 0.0f, 0.0f, 1.0f);
    close(c);

    feedStill(c, t, 30000, 40, 0.0f, 0.0f, 1.0f);
    const uint32_t before = close(c).count;

    feedStill(c, t, 30000, 40, 1.0f, 0.0f, 0.0f);   // the roll
    const uint32_t during = close(c).count;

    feedStill(c, t, 30000, 40, 1.0f, 0.0f, 0.0f);   // settled in the new pose
    const uint32_t after = close(c).count;

    EXPECT_GT(during, before * 10u + 10u) << "the roll itself must register";
    EXPECT_LT(after, before + 5u) << "the new posture must not read as movement";
}

// -- Movement ----------------------------------------------------------------

TEST(EpochCounter, CountScalesWithMovementAmplitude)
{
    uint32_t counts[3] = {};
    const float kAmplitudes[3] = { 0.05f, 0.20f, 0.80f };

    for (int i = 0; i < 3; i++) {
        EpochCounter c;
        uint32_t t = 0;
        feedSine(c, t, 5000, 40, kAmplitudes[i], 1.0f);
        close(c);
        feedSine(c, t, 30000, 40, kAmplitudes[i], 1.0f);
        counts[i] = close(c).count;
    }

    EXPECT_LT(counts[0], counts[1]);
    EXPECT_LT(counts[1], counts[2]);
    // Roughly linear: quadrupling the amplitude should roughly quadruple the
    // integral. Loose bounds, because the filters have a real response.
    EXPECT_GT(counts[2], counts[0] * 8u);
}

TEST(EpochCounter, TheBandRejectsBothDriftAndShock)
{
    // 0.05 Hz is postural drift, 1 Hz is movement, 12 Hz is impact shock. Only
    // the middle one should register, and that is the entire justification for
    // filtering at all.
    auto measure = [](float freqHz) {
        EpochCounter c;
        uint32_t t = 0;
        feedSine(c, t, 20000, 20, 0.3f, freqHz);
        close(c);
        feedSine(c, t, 30000, 20, 0.3f, freqHz);
        return close(c).count;
    };

    const uint32_t drift  = measure(0.05f);
    const uint32_t motion = measure(1.0f);
    const uint32_t shock  = measure(12.0f);

    EXPECT_GT(motion, drift * 4u)
        << "0.05 Hz drift should be attenuated relative to 1 Hz movement";
    EXPECT_GT(motion, shock * 2u)
        << "12 Hz shock should be attenuated relative to 1 Hz movement";
}

// -- Rate independence: the property the platform forces -----------------------

TEST(EpochCounter, CountIsIndependentOfTheDeliveredSampleRate)
{
    // THE test in this file. The sample-rate gate thins delivery on a boundary
    // at half the expected period, so the same night can arrive at 25 Hz or at
    // 12.5 Hz. If the count tracked the delivered rate, halving the rate would
    // halve every count in the night and the scorer would read it as a quieter
    // night -- a whole-night error caused by nothing the wearer did.
    auto measure = [](uint32_t periodMs) {
        EpochCounter c;
        uint32_t t = 0;
        feedSine(c, t, 10000, periodMs, 0.25f, 1.0f);
        close(c);
        feedSine(c, t, 30000, periodMs, 0.25f, 1.0f);
        return close(c).count;
    };

    const uint32_t at25Hz   = measure(40);
    const uint32_t at12_5Hz = measure(80);
    const uint32_t at50Hz   = measure(20);

    // Within 20 % across a 4x span of delivered rates. Not exact: a coarser
    // grid genuinely samples a sine less faithfully, and the one-pole filters
    // are re-coefficiented per sample rather than being rate-invariant by
    // construction. 20 % is small against the dynamic range the scorer works
    // over, which spans two orders of magnitude between stillness and moving.
    const float ref = static_cast<float>(at25Hz);
    EXPECT_NEAR(static_cast<float>(at12_5Hz) / ref, 1.0f, 0.20f);
    EXPECT_NEAR(static_cast<float>(at50Hz)   / ref, 1.0f, 0.20f);
}

TEST(EpochCounter, ADeliveryGapContributesNothingRatherThanAHugeRectangle)
{
    // A four-second hole in delivery is not four seconds of the wrist holding
    // its last value. dt-weighting it as though it were would manufacture a
    // large count out of a delivery failure -- and a delivery failure looking
    // like violent movement is exactly the wrong direction to fail in.
    EpochCounter c;
    uint32_t t = 0;

    feedSine(c, t, 10000, 40, 0.3f, 1.0f);
    close(c);

    // Ten seconds of movement, a four-second hole, then ten more.
    feedSine(c, t, 10000, 40, 0.3f, 1.0f);
    const uint32_t beforeGap = c.pendingSamples();
    t += 4000;
    feedSine(c, t, 10000, 40, 0.3f, 1.0f);
    const Closed gapped = close(c);

    // The same 20 s of movement with no hole.
    EpochCounter c2;
    uint32_t t2 = 0;
    feedSine(c2, t2, 10000, 40, 0.3f, 1.0f);
    close(c2);
    feedSine(c2, t2, 20000, 40, 0.3f, 1.0f);
    const Closed clean = close(c2);

    EXPECT_GT(beforeGap, 0u);
    // The gap adds no area, so the two should be comparable -- the gapped run
    // is slightly lower because the filter re-seeds and loses a little signal
    // to the settling that follows.
    EXPECT_LT(gapped.count, clean.count + clean.count / 5u);
    EXPECT_GT(gapped.count, clean.count / 2u);
}

// -- Bookkeeping ---------------------------------------------------------------

TEST(EpochCounter, FilterStateSurvivesAnEpochBoundary)
{
    // Resetting the high-pass at every boundary would emit a settling
    // transient into the first second of every epoch -- 960 of them a night.
    EpochCounter c;
    uint32_t t = 0;
    feedStill(c, t, 5000, 40, 0.0f, 0.0f, 1.0f);
    close(c);

    uint32_t quiet[4];
    for (int i = 0; i < 4; i++) {
        feedStill(c, t, 30000, 40, 0.0f, 0.0f, 1.0f);
        quiet[i] = close(c).count;
    }
    for (int i = 0; i < 4; i++) {
        EXPECT_LT(quiet[i], 5u) << "epoch " << i << " carried a transient";
    }
}

TEST(EpochCounter, ResetClearsFilterStateAndReprimes)
{
    EpochCounter c;
    uint32_t t = 0;
    feedSine(c, t, 30000, 40, 0.5f, 1.0f);

    c.reset();
    EXPECT_EQ(c.pendingSamples(), 0u);

    // After a reset the first sample re-seeds, so a still stretch reads still
    // rather than carrying the previous session's movement into it.
    uint32_t t2 = 0;
    feedStill(c, t2, 5000, 40, 0.0f, 0.0f, 1.0f);
    close(c);
    feedStill(c, t2, 30000, 40, 0.0f, 0.0f, 1.0f);
    EXPECT_LT(close(c).count, 5u);
}

TEST(EpochCounter, SamplesSharingAnInstantDoNotDoubleCount)
{
    // Batches can carry samples stamped identically. There is no time for the
    // repeats to integrate over, so ninety-nine of them must contribute exactly
    // as much area as zero of them.
    auto measure = [](int repeats) {
        EpochCounter c;
        uint32_t t = 0;
        feedStill(c, t, 5000, 40, 0.0f, 0.0f, 1.0f);
        close(c);
        for (int i = 0; i < repeats; i++) {
            c.add(t, 0.5f, 0.0f, 1.0f);   // all at the same timestamp
        }
        return close(c);
    };

    const Closed one  = measure(1);
    const Closed many = measure(100);

    EXPECT_EQ(many.count, one.count)
        << "repeats at one instant added area they had no time for";
    EXPECT_EQ(many.samples, one.samples)
        << "and they are not counted as samples either";
}

TEST(EpochCounter, EveryAxisContributesEquallySoOrientationDoesNotSetTheScale)
{
    // Filtering the vector magnitude instead of the axes would make this fail
    // by a factor of about fifteen: |a| = sqrt(1 + A^2) ~ 1 + A^2/2 is second
    // order in a movement across gravity, so the count would depend mostly on
    // which way up the watch happened to be.
    auto measure = [](int movingAxis) {
        EpochCounter c;
        uint32_t t = 0;
        const uint32_t end0 = 10000;
        for (; t < end0; t += 20) {
            const float ph = 2.0f * kPi * 1.0f * (static_cast<float>(t) * 0.001f);
            const float a  = 0.3f * std::sin(ph);
            c.add(t, movingAxis == 0 ? a : 0.0f,
                     movingAxis == 1 ? a : 0.0f,
                     1.0f + (movingAxis == 2 ? a : 0.0f));
        }
        close(c);
        const uint32_t end1 = t + 30000;
        for (; t < end1; t += 20) {
            const float ph = 2.0f * kPi * 1.0f * (static_cast<float>(t) * 0.001f);
            const float a  = 0.3f * std::sin(ph);
            c.add(t, movingAxis == 0 ? a : 0.0f,
                     movingAxis == 1 ? a : 0.0f,
                     1.0f + (movingAxis == 2 ? a : 0.0f));
        }
        return close(c).count;
    };

    const uint32_t x = measure(0), y = measure(1), z = measure(2);
    EXPECT_EQ(x, y);
    EXPECT_EQ(x, z) << "the axis carrying gravity must not be special";
}

TEST(EpochCounter, PeakSeparatesOneHardMovementFromContinuousFidgeting)
{
    // Both integrate to a similar count; only one of them is a person turning
    // over. A summary that could not tell them apart would call both the same.
    EpochCounter fidget;
    uint32_t t = 0;
    feedSine(fidget, t, 10000, 40, 0.06f, 2.0f);
    close(fidget);
    feedSine(fidget, t, 30000, 40, 0.06f, 2.0f);
    const Closed f = close(fidget);

    EpochCounter jolt;
    uint32_t t2 = 0;
    feedStill(jolt, t2, 10000, 40, 0.0f, 0.0f, 1.0f);
    close(jolt);
    feedStill(jolt, t2, 14000, 40, 0.0f, 0.0f, 1.0f);
    feedSine(jolt, t2, 2000, 40, 0.9f, 2.0f);
    feedStill(jolt, t2, 14000, 40, 0.0f, 0.0f, 1.0f);
    const Closed j = close(jolt);

    EXPECT_GT(j.peak, f.peak * 3u)
        << "one hard movement must show a far higher peak than steady fidgeting";
}

} // namespace

// ---------------------------------------------------------------------------
// Hostile input
// ---------------------------------------------------------------------------

/// A count of exactly zero is the most dangerous value this class can produce:
/// it is what a rigid object on furniture looks like, it is what the soundest
/// sleep of the night looks like, and it is indistinguishable from either.
///
/// The filter state deliberately survives `closeEpoch()` -- which is right, and
/// which means one bad sample is not one bad epoch. A single non-finite value
/// propagates into all five filter poles and every subsequent epoch integrates
/// to NaN, whose conversion to `uint32_t` is undefined and in practice zero.
/// The sample count stays healthy, so neither the recorder's data-gap flag nor
/// the scorer's thin-epoch guard notices, and the rest of the night is scored
/// as perfect stillness.
TEST(EpochCounter, ANonFiniteSampleDoesNotSilenceTheRestOfTheNight)
{
    const uint32_t kPeriodMs = 21;   // ~48 Hz, the delivered rate
    const size_t   perEpoch  = 30000 / kPeriodMs;

    Engine::EpochCounter c;
    uint32_t count = 0, peak = 0;
    uint16_t samples = 0;

    // A clear, ordinary movement: 0.05 g at 1 Hz.
    auto feedEpoch = [&](size_t base, bool injectNaN) {
        for (size_t k = 0; k < perEpoch; ++k) {
            const uint32_t ts = static_cast<uint32_t>((base + k) * kPeriodMs);
            float x = 0.05f * std::sin(6.28318530718f *
                                       static_cast<float>(ts) * 0.001f);
            if (injectNaN && k == 10) {
                x = std::numeric_limits<float>::quiet_NaN();
            }
            c.add(ts, x, 0.0f, 1.0f);
        }
        c.closeEpoch(count, peak, samples);
    };

    feedEpoch(0, false);
    const uint32_t clean = count;
    ASSERT_GT(clean, 100u) << "the fixture is not producing a count to lose";

    feedEpoch(perEpoch, true);          // one NaN, somewhere in this epoch
    feedEpoch(perEpoch * 2, false);     // and then two entirely clean ones
    const uint32_t afterOne = count;
    feedEpoch(perEpoch * 3, false);
    const uint32_t afterTwo = count;

    // The epochs *after* the bad sample are the finding. A single glitch may
    // cost its own epoch; it must not cost the night.
    EXPECT_NEAR(afterOne, clean, clean / 2)
        << "the epoch after a non-finite sample counted " << afterOne
        << " against " << clean << " for identical movement";
    EXPECT_NEAR(afterTwo, clean, clean / 2)
        << "two epochs later the counter is still producing " << afterTwo;
}

TEST(EpochCounter, AnInfiniteSampleIsRejectedRatherThanSaturating)
{
    // The saturation guard tests `scaledSum >= kMax`, which is true for
    // infinity and false for NaN -- so an infinity saturates the epoch to
    // 0xFFFFFFFF, which is at least loud, and then leaves the filter state
    // infinite so every later epoch is NaN, which is silent.
    const uint32_t kPeriodMs = 21;
    Engine::EpochCounter c;
    uint32_t count = 0, peak = 0;
    uint16_t samples = 0;

    for (uint32_t k = 0; k < 200; ++k) {
        float x = 0.05f * std::sin(6.28318530718f *
                                   static_cast<float>(k * kPeriodMs) * 0.001f);
        if (k == 50) { x = std::numeric_limits<float>::infinity(); }
        c.add(k * kPeriodMs, x, 0.0f, 1.0f);
    }
    c.closeEpoch(count, peak, samples);

    for (uint32_t k = 200; k < 400; ++k) {
        const float x = 0.05f * std::sin(6.28318530718f *
                                         static_cast<float>(k * kPeriodMs) * 0.001f);
        c.add(k * kPeriodMs, x, 0.0f, 1.0f);
    }
    c.closeEpoch(count, peak, samples);

    EXPECT_GT(count, 0u) << "an infinite sample silenced the counter for good";
    EXPECT_LT(count, 0xFFFFFFFFu) << "the counter is stuck saturated";
}
