/**
 ******************************************************************************
 * @file    SecondsAccrual_test.cpp
 * @brief   The claim that makes time-in-zone add up.
 ******************************************************************************
 */

#include "SecondsAccrual.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(SecondsAccrual, TheFirstTickAttributesNothing)
{
    // THE bug. A ride's first tick fires before any time has passed, and
    // banking a second for it is what put 6 bucket-seconds on a 5-second ride
    // and 186 on a 185-second one.
    SecondsAccrual a;
    EXPECT_EQ(a.take(0), 0);
    EXPECT_EQ(a.take(1), 1);
}

TEST(SecondsAccrual, WhatIsHandedOutSumsToTheActiveTime)
{
    // The property the whole file exists for: whatever the tick sequence, the
    // seconds handed out total the last active time seen -- so a consumer can
    // check time_in_hr_zone against total_timer_time and have it balance.
    for (std::time_t total : {1, 5, 185, 2700}) {
        SecondsAccrual a;
        std::time_t sum = 0;
        for (std::time_t t = 0; t <= total; ++t) {
            sum += a.take(t);
        }
        EXPECT_EQ(sum, total) << "ride of " << total << " s";
        EXPECT_EQ(a.attributed(), total);
    }
}

TEST(SecondsAccrual, ATickThatRepeatsAttributesNothingTwice)
{
    // The Service ticks on a wall-clock second changing, but a paused ride
    // holds its active time still. Those ticks must add nothing rather than a
    // second each.
    SecondsAccrual a;
    a.take(10);
    EXPECT_EQ(a.take(10), 0);
    EXPECT_EQ(a.take(10), 0);
    EXPECT_EQ(a.take(11), 1);
}

TEST(SecondsAccrual, ALateTickCarriesEveryChangeItMissed)
{
    // A tick the Service was too busy to serve on time covers the whole gap.
    // A flat second per tick dropped the difference silently.
    SecondsAccrual a;
    a.take(1);
    EXPECT_EQ(a.take(9), 8);
    EXPECT_EQ(a.attributed(), 9);
}

TEST(SecondsAccrual, StallsAndJumpsStillBalance)
{
    // A sequence with both, to make the point that the sum does not depend on
    // the ticks being regular.
    const std::vector<std::time_t> ticks = {0, 0, 1, 1, 1, 2, 7, 7, 8, 20, 20, 41};
    SecondsAccrual a;
    std::time_t sum = 0;
    for (std::time_t t : ticks) {
        sum += a.take(t);
    }
    EXPECT_EQ(sum, ticks.back());
}

TEST(SecondsAccrual, TimeGoingBackwardsAttributesNothing)
{
    // A counter that ran backwards is a bug somewhere else. Handing back a
    // negative span would spread it through every bucket this feeds instead of
    // leaving it where it happened.
    SecondsAccrual a;
    a.take(100);
    EXPECT_EQ(a.take(60), 0);
    EXPECT_EQ(a.attributed(), 100);
    // ...and it picks up again once time passes the high-water mark.
    EXPECT_EQ(a.take(101), 1);
}

TEST(SecondsAccrual, ResetStartsTheNextRideFromZero)
{
    SecondsAccrual a;
    a.take(2700);
    a.reset();
    EXPECT_EQ(a.attributed(), 0);
    EXPECT_EQ(a.take(0), 0);
    EXPECT_EQ(a.take(1), 1);
}
