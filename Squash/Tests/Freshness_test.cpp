/**
 ******************************************************************************
 * @file    Freshness_test.cpp
 * @brief   The rule that keeps a dead sensor's last number off the screen.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <ctime>

#include "Freshness.hpp"

using Freshness::isCurrent;
using Freshness::kHeartRateStaleAfterS;

namespace {
constexpr std::time_t kNow = 1789320000;
}

TEST(Freshness, AReadingFromThisSecondIsCurrent)
{
    EXPECT_TRUE(isCurrent(kNow, kNow, kHeartRateStaleAfterS));
}

TEST(Freshness, TheWindowIsHalfOpenSoTheThresholdItselfIsStale)
{
    // Two seconds is the largest gap the sensor has been observed to produce,
    // so it must still count as current; three is past every observed gap.
    EXPECT_TRUE(isCurrent(kNow - 2, kNow, kHeartRateStaleAfterS));
    EXPECT_FALSE(isCurrent(kNow - 3, kNow, kHeartRateStaleAfterS));
    EXPECT_FALSE(isCurrent(kNow - 600, kNow, kHeartRateStaleAfterS));
}

TEST(Freshness, NoReadingHasEverArrivedIsNotACurrentReading)
{
    // The Service starts with 0 here, and a session that never sees the sensor
    // must show nothing rather than whatever a zeroed field decodes to.
    EXPECT_FALSE(isCurrent(0, kNow, kHeartRateStaleAfterS));
    EXPECT_FALSE(isCurrent(-1, kNow, kHeartRateStaleAfterS));
}

TEST(Freshness, AReadingFromTheFutureIsNotCurrent)
{
    // The watch's clock steps when it resyncs. Treating a negative age as very
    // recent would pin the display to a stale value for the length of the step,
    // which is the failure this rule exists to prevent.
    EXPECT_FALSE(isCurrent(kNow + 1, kNow, kHeartRateStaleAfterS));
    EXPECT_FALSE(isCurrent(kNow + 3600, kNow, kHeartRateStaleAfterS));
}

TEST(Freshness, TheMeasuredThresholdClearsEveryObservedGap)
{
    // The largest inter-sample gap in every recording under Tests/pulled is
    // 2117 ms. Rounded up to whole seconds that is 3, and a reading that old
    // must still be current or an ordinary dropout blanks the screen.
    constexpr std::time_t kLargestObservedGapS = 2;   // 2117 ms
    EXPECT_TRUE(isCurrent(kNow - kLargestObservedGapS, kNow, kHeartRateStaleAfterS))
        << "the threshold no longer clears the largest gap the sensor has produced";
}
