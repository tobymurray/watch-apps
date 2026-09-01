/**
 ******************************************************************************
 * @file    HrHold_test.cpp
 * @brief   The rule that decides what heart rate the screen shows.
 *
 * Written against what two real rides actually contained: 32 seconds the
 * arbiter did not stand behind, 31 of which had a sensor reading within a beat
 * or two of their neighbours, every one of them one or two seconds long.
 ******************************************************************************
 */

#include "HrHold.hpp"

#include <gtest/gtest.h>

TEST(HrHold, PassesATrustedReadingStraightThrough)
{
    HrHold h;
    EXPECT_FLOAT_EQ(h.update(true, 142.0f), 142.0f);
    EXPECT_FALSE(h.isHolding());
    EXPECT_FLOAT_EQ(h.update(true, 145.0f), 145.0f);
    EXPECT_FALSE(h.isHolding());
}

TEST(HrHold, BridgesTheOneSecondDipThatCausedThis)
{
    // The exact shape from the ride files: 62, 62, [untrusted], 62, 62.
    HrHold h;
    h.update(true, 62.0f);
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 62.0f) << "blanked on a single-second dip";
    EXPECT_TRUE(h.isHolding());
    EXPECT_FLOAT_EQ(h.update(true, 62.0f), 62.0f);
    EXPECT_FALSE(h.isHolding());
}

TEST(HrHold, BridgesTwoSecondsToo)
{
    HrHold h;
    h.update(true, 60.0f);
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 60.0f);
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 60.0f);
    EXPECT_EQ(h.heldFor(), 2);
}

TEST(HrHold, GivesUpAfterTheWindow)
{
    // A watch off the wrist, or a strap out of range, has to stop showing a
    // heart rate that is no longer anyone's.
    HrHold h;
    h.update(true, 150.0f);
    for (uint8_t i = 0; i < HrHold::skHoldSeconds; ++i) {
        EXPECT_FLOAT_EQ(h.update(false, 0.0f), 150.0f) << "gave up at second " << int(i);
    }
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 0.0f) << "still holding past the window";
}

TEST(HrHold, StaysGoneUntilSomethingTrustedArrives)
{
    HrHold h;
    h.update(true, 150.0f);
    for (int i = 0; i < 100; ++i) {
        h.update(false, 0.0f);
    }
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 0.0f);
    // ...and recovers the moment it can.
    EXPECT_FLOAT_EQ(h.update(true, 148.0f), 148.0f);
    EXPECT_FALSE(h.isHolding());
}

TEST(HrHold, HasNothingToHoldBeforeTheFirstReading)
{
    // The opening seconds of a ride, before the sensor has settled: there is no
    // previous value, so there is nothing to show.
    HrHold h;
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(h.update(false, 99.0f), 0.0f) << "used an untrusted reading";
}

TEST(HrHold, NeverInventsAValueBetweenTwoReadings)
{
    // It holds, it does not interpolate or average. A held second reports
    // exactly what was last measured, so the number on the screen is always one
    // the sensor actually produced.
    HrHold h;
    h.update(true, 100.0f);
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 100.0f);
    EXPECT_FLOAT_EQ(h.update(true, 160.0f), 160.0f);
}

TEST(HrHold, ResetForgetsEverything)
{
    HrHold h;
    h.update(true, 140.0f);
    h.reset();
    EXPECT_FLOAT_EQ(h.update(false, 0.0f), 0.0f) << "a new ride inherited the last one's beat";
}
