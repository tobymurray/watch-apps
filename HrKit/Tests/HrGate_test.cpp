/**
 ******************************************************************************
 * @file    HrGate_test.cpp
 * @brief   The two rules that decide what a heart rate is and how long it lasts.
 *
 * The shapes here come from the recordings under `Squash/Tests/pulled`: a
 * nominal arrival every ~1005 ms, untrusted runs of one to four seconds, and a
 * largest observed gap of 2117 ms.
 ******************************************************************************
 */

#include "HrGate.hpp"

#include <gtest/gtest.h>

using HrGate::Hold;

// -- isPlausible -------------------------------------------------------------

TEST(IsPlausible, AcceptsEveryReadingTheRecordingsContain)
{
    // Non-zero bpm in the pulled recordings spans 64 to 160, at trust 1, 2 or 3.
    for (float trust = 1.0f; trust <= 3.0f; trust += 1.0f) {
        EXPECT_TRUE(HrGate::isPlausible(64.0f, trust));
        EXPECT_TRUE(HrGate::isPlausible(160.0f, trust));
    }
}

TEST(IsPlausible, RejectsTheZeroThatMeansNoReading)
{
    // Four rows of the pulled recordings carry exactly this, at trust 0.
    EXPECT_FALSE(HrGate::isPlausible(0.0f, 0.0f));
    EXPECT_FALSE(HrGate::isPlausible(0.0f, 1.0f)) << "a zero bpm is not a heart rate";
}

TEST(IsPlausible, RejectsAnUntrustedReadingWhateverItSays)
{
    EXPECT_FALSE(HrGate::isPlausible(142.0f, 0.0f));
}

TEST(IsPlausible, RejectsATrustValueOffTheKernelsScale)
{
    EXPECT_FALSE(HrGate::isPlausible(142.0f, 4.0f));
    EXPECT_FALSE(HrGate::isPlausible(142.0f, -1.0f));
}

TEST(IsPlausible, RejectsBpmOutsideTheAcceptedRange)
{
    EXPECT_FALSE(HrGate::isPlausible(HrGate::kMinBpm, 3.0f)) << "the floor is exclusive";
    EXPECT_TRUE(HrGate::isPlausible(HrGate::kMinBpm + 0.5f, 3.0f));
    EXPECT_TRUE(HrGate::isPlausible(HrGate::kMaxBpm, 3.0f));
    EXPECT_FALSE(HrGate::isPlausible(HrGate::kMaxBpm + 0.5f, 3.0f));
}

TEST(IsPlausible, RejectsNaN)
{
    // Every comparison against NaN is false, which lands on "not a reading" --
    // the same answer EffortKit's clamp_bpm gives it.
    const float nan = 0.0f / 0.0f;
    EXPECT_FALSE(HrGate::isPlausible(nan, 3.0f));
    EXPECT_FALSE(HrGate::isPlausible(142.0f, nan));
}

// -- Hold: nothing yet -------------------------------------------------------

TEST(Hold, ShowsNothingBeforeTheFirstFrame)
{
    Hold h;
    const Hold::View v = h.view(0);
    EXPECT_FLOAT_EQ(v.bpm, 0.0f);
    EXPECT_FALSE(v.held);
    EXPECT_FALSE(v.measured);
}

TEST(Hold, ShowsNothingWhileOnlyUntrustedFramesHaveArrived)
{
    // The opening seconds of a session, before the sensor has settled.
    Hold h;
    h.onReading(0.0f, 0.0f, 1000);
    const Hold::View v = h.view(1000);
    EXPECT_FLOAT_EQ(v.bpm, 0.0f) << "showed a reading it never had";
    EXPECT_FALSE(v.measured);
}

// -- Hold: the ordinary case -------------------------------------------------

TEST(Hold, PassesATrustedReadingStraightThrough)
{
    Hold h;
    h.onReading(142.0f, 3.0f, 1000);
    const Hold::View v = h.view(1000);
    EXPECT_FLOAT_EQ(v.bpm, 142.0f);
    EXPECT_FALSE(v.held);
    EXPECT_TRUE(v.measured);
}

TEST(Hold, KeepsShowingAReadingBetweenArrivals)
{
    // Arrivals are ~1005 ms apart and the screen ticks faster than that.
    Hold h;
    h.onReading(142.0f, 3.0f, 1000);
    for (uint32_t t = 1000; t < 1000 + Hold::kArrivalGateMs; t += 100) {
        EXPECT_FLOAT_EQ(h.view(t).bpm, 142.0f) << "blanked at " << t;
    }
}

// -- Hold: the trust window, which Spin had and Squash did not ---------------

TEST(Hold, BridgesTheOneSecondDipThatCausedThis)
{
    // The exact shape from the ride files: 62, 62, [untrusted], 62, 62.
    Hold h;
    h.onReading(62.0f, 2.0f, 1000);
    h.onReading(0.0f, 0.0f, 2005);
    const Hold::View dip = h.view(2005);
    EXPECT_FLOAT_EQ(dip.bpm, 62.0f) << "blanked on a single-second dip";
    EXPECT_TRUE(dip.held);
    EXPECT_FALSE(dip.measured) << "a held second must not be recorded as measured";

    h.onReading(62.0f, 2.0f, 3010);
    EXPECT_TRUE(h.view(3010).measured);
    EXPECT_FALSE(h.view(3010).held);
}

TEST(Hold, BridgesTheLongestUntrustedRunEverRecorded)
{
    // Four seconds, from Tools/hr_analyse.py over the pulled recordings.
    Hold h;
    h.onReading(150.0f, 3.0f, 1000);
    uint32_t t = 1000;
    for (int i = 0; i < 4; ++i) {
        t += 1005;
        h.onReading(0.0f, 0.0f, t);
        EXPECT_FLOAT_EQ(h.view(t).bpm, 150.0f) << "blanked at untrusted second " << i;
    }
}

TEST(Hold, GivesUpOnceTheTrustWindowPasses)
{
    // A watch off the wrist still publishes frames, so the arrival gate never
    // fires; this is the window that has to end it.
    Hold h;
    h.onReading(150.0f, 3.0f, 1000);
    uint32_t t = 1000;
    while (t - 1000 < Hold::kTrustHoldMs) {
        t += 1005;
        h.onReading(0.0f, 0.0f, t);
    }
    EXPECT_FLOAT_EQ(h.view(t).bpm, 0.0f) << "still holding past the trust window";
}

TEST(Hold, RecoversTheMomentSomethingTrustedArrives)
{
    Hold h;
    h.onReading(150.0f, 3.0f, 1000);
    h.onReading(0.0f, 0.0f, 30000);
    EXPECT_FLOAT_EQ(h.view(30000).bpm, 0.0f);
    h.onReading(148.0f, 2.0f, 31005);
    const Hold::View v = h.view(31005);
    EXPECT_FLOAT_EQ(v.bpm, 148.0f);
    EXPECT_TRUE(v.measured);
}

// -- Hold: the arrival gate, which Squash had and Spin did not ---------------

TEST(Hold, BlanksWhenFramesStopArrivingAltogether)
{
    // The failure the trust window cannot see: the last frame was trusted, so
    // nothing ever marks the reading as doubtful -- it simply stops being
    // renewed.
    Hold h;
    h.onReading(142.0f, 3.0f, 1000);
    EXPECT_FLOAT_EQ(h.view(1000 + Hold::kArrivalGateMs - 1).bpm, 142.0f);
    EXPECT_FLOAT_EQ(h.view(1000 + Hold::kArrivalGateMs).bpm, 0.0f)
        << "showed a heart rate the sensor had stopped producing";
}

TEST(Hold, SurvivesTheLargestGapEverObserved)
{
    // 2117 ms, from the 7-minute accessory-flap recording.
    Hold h;
    h.onReading(142.0f, 3.0f, 1000);
    EXPECT_FLOAT_EQ(h.view(1000 + 2117).bpm, 142.0f)
        << "a real delivery gap was reported as a dead sensor";
}

TEST(Hold, TheArrivalGateOutranksTheTrustWindow)
{
    // Held on trust, then the stream stops: the hold must not outlive it.
    Hold h;
    h.onReading(142.0f, 3.0f, 1000);
    h.onReading(0.0f, 0.0f, 2005);
    EXPECT_FLOAT_EQ(h.view(2005).bpm, 142.0f);
    EXPECT_FLOAT_EQ(h.view(2005 + Hold::kArrivalGateMs).bpm, 0.0f)
        << "a trust hold survived the stream stopping";
}

TEST(Hold, StopsReportingMeasuredWhenTheStreamStops)
{
    // What a file records, as distinct from what the screen shows.
    Hold h;
    h.onReading(142.0f, 3.0f, 1000);
    EXPECT_TRUE(h.view(1000).measured);
    EXPECT_FALSE(h.view(1000 + Hold::kArrivalGateMs).measured);
}

// -- Hold: housekeeping ------------------------------------------------------

TEST(Hold, ResetForgetsEverything)
{
    Hold h;
    h.onReading(140.0f, 3.0f, 1000);
    h.reset();
    EXPECT_FLOAT_EQ(h.view(1000).bpm, 0.0f) << "a new session inherited the last one's beat";
}

TEST(Hold, NeverInventsAValueBetweenTwoReadings)
{
    // It holds, it does not interpolate: the number shown is always one the
    // sensor actually produced.
    Hold h;
    h.onReading(100.0f, 3.0f, 1000);
    h.onReading(0.0f, 0.0f, 2005);
    EXPECT_FLOAT_EQ(h.view(2005).bpm, 100.0f);
    h.onReading(160.0f, 3.0f, 3010);
    EXPECT_FLOAT_EQ(h.view(3010).bpm, 160.0f);
}

TEST(Hold, SurvivesTheUptimeCounterWrapping)
{
    // Uptime is 32-bit and wraps at ~49.7 days; a session across the wrap must
    // not read as a sensor that has been quiet for seven weeks.
    const uint32_t justBefore = 0xFFFFFFFFu - 500u;
    Hold h;
    h.onReading(142.0f, 3.0f, justBefore);
    const uint32_t justAfter = justBefore + 1005u;   // wraps
    EXPECT_FLOAT_EQ(h.view(justAfter).bpm, 142.0f) << "the wrap blanked a live reading";
    EXPECT_FLOAT_EQ(h.view(justBefore + Hold::kArrivalGateMs).bpm, 0.0f)
        << "the wrap made the gate stop firing";
}
