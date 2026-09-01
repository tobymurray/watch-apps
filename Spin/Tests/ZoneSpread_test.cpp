/**
 ******************************************************************************
 * @file    ZoneSpread_test.cpp
 * @brief   The claim that makes the derived floors trustworthy.
 ******************************************************************************
 */

#include "ZoneSpread.hpp"

#include <gtest/gtest.h>

TEST(ZoneSpread, MatchesTheWatchAtFiveZones)
{
    // THE test. The watch's own ladder for a 184 bpm maximum is
    // 92/110/129/147/166/184 -- 50/60/70/80/90/100% -- and the first five of
    // those are its zone floors. If this rule reproduces them exactly, then
    // using it at three or eight zones is the watch's rule at a different
    // count rather than a training model invented here. If it ever stops
    // matching, that argument is gone and so is the reason to trust it.
    uint8_t out[8] = {};
    ASSERT_TRUE(ZoneSpread::floors(184, 5, out, 8));
    EXPECT_EQ(out[0], 92);
    EXPECT_EQ(out[1], 110);
    EXPECT_EQ(out[2], 129);
    EXPECT_EQ(out[3], 147);
    EXPECT_EQ(out[4], 166);
}

TEST(ZoneSpread, StartsAtHalfTheMaximumAndStaysBelowIt)
{
    for (uint8_t maxHr : {150, 184, 200}) {
        for (uint8_t count = 2; count <= 8; ++count) {
            uint8_t out[8] = {};
            ASSERT_TRUE(ZoneSpread::floors(maxHr, count, out, 8));
            EXPECT_EQ(out[0], static_cast<uint8_t>(maxHr / 2 + (maxHr % 2)))
                << "max " << int(maxHr) << " count " << int(count);
            EXPECT_LT(out[count - 1], maxHr)
                << "a zone starting at the maximum could never be entered";
        }
    }
}

TEST(ZoneSpread, ClimbsStrictlyAtEveryCount)
{
    for (uint8_t count = 2; count <= 8; ++count) {
        uint8_t out[8] = {};
        ASSERT_TRUE(ZoneSpread::floors(184, count, out, 8));
        for (uint8_t i = 1; i < count; ++i) {
            EXPECT_GT(out[i], out[i - 1])
                << "count " << int(count) << " floor " << int(i) << " did not climb";
        }
    }
}

TEST(ZoneSpread, RefusesWhatItCannotSpread)
{
    uint8_t out[8] = {0xAA};
    EXPECT_FALSE(ZoneSpread::floors(0, 5, out, 8))    << "no maximum";
    EXPECT_FALSE(ZoneSpread::floors(184, 1, out, 8))  << "one zone is not zones";
    EXPECT_FALSE(ZoneSpread::floors(184, 9, out, 8))  << "past the array";
    EXPECT_FALSE(ZoneSpread::floors(184, 5, nullptr, 8));
    EXPECT_EQ(out[0], 0xAA) << "wrote to the output after refusing";
}
