/**
 ******************************************************************************
 * @file    ZoneLadder_test.cpp
 * @brief   The claim that makes the derived floors trustworthy.
 ******************************************************************************
 */

#include "ZoneLadder.hpp"

#include <gtest/gtest.h>

TEST(ZoneLadder, MatchesTheWatchAtFiveZones)
{
    // THE test. The watch's own ladder for a 184 bpm maximum is
    // 92/110/129/147/166/184 -- 50/60/70/80/90/100% -- and the first five of
    // those are its zone floors. If this rule reproduces them exactly, then
    // using it at three or eight zones is the watch's rule at a different
    // count rather than a training model invented here. If it ever stops
    // matching, that argument is gone and so is the reason to trust it.
    uint8_t out[8] = {};
    ASSERT_TRUE(ZoneLadder::floors(184, 5, out, 8));
    EXPECT_EQ(out[0], 92);
    EXPECT_EQ(out[1], 110);
    EXPECT_EQ(out[2], 129);
    EXPECT_EQ(out[3], 147);
    EXPECT_EQ(out[4], 166);
}

TEST(ZoneLadder, StartsAtHalfTheMaximumAndStaysBelowIt)
{
    for (uint8_t maxHr : {150, 184, 200}) {
        for (uint8_t count = 2; count <= 8; ++count) {
            uint8_t out[8] = {};
            ASSERT_TRUE(ZoneLadder::floors(maxHr, count, out, 8));
            EXPECT_EQ(out[0], static_cast<uint8_t>(maxHr / 2 + (maxHr % 2)))
                << "max " << int(maxHr) << " count " << int(count);
            EXPECT_LT(out[count - 1], maxHr)
                << "a zone starting at the maximum could never be entered";
        }
    }
}

TEST(ZoneLadder, ClimbsStrictlyAtEveryCount)
{
    for (uint8_t count = 2; count <= 8; ++count) {
        uint8_t out[8] = {};
        ASSERT_TRUE(ZoneLadder::floors(184, count, out, 8));
        for (uint8_t i = 1; i < count; ++i) {
            EXPECT_GT(out[i], out[i - 1])
                << "count " << int(count) << " floor " << int(i) << " did not climb";
        }
    }
}

TEST(ZoneLadder, RefusesWhatItCannotSpread)
{
    uint8_t out[8] = {0xAA};
    EXPECT_FALSE(ZoneLadder::floors(0, 5, out, 8))    << "no maximum";
    EXPECT_FALSE(ZoneLadder::floors(184, 1, out, 8))  << "one zone is not zones";
    EXPECT_FALSE(ZoneLadder::floors(184, 9, out, 8))  << "past the array";
    EXPECT_FALSE(ZoneLadder::floors(184, 5, nullptr, 8));
    EXPECT_EQ(out[0], 0xAA) << "wrote to the output after refusing";
}

// -- Where the needle goes ---------------------------------------------------

namespace {
// The watch's own five-zone ladder for a 184 bpm maximum.
const uint8_t kFloors[5] = {92, 110, 129, 147, 166};
}

TEST(ZoneLadder, PlacesTheNeedleAcrossAClosedZone)
{
    // Zone 1 runs 92..110, so 101 is about half way.
    EXPECT_EQ(ZoneLadder::fraction(92.0f, 1, kFloors, 5, 184), 0);
    EXPECT_NEAR(ZoneLadder::fraction(101.0f, 1, kFloors, 5, 184), 128, 4);
    EXPECT_EQ(ZoneLadder::fraction(110.0f, 1, kFloors, 5, 184), 255);
}

TEST(ZoneLadder, TheTopZoneUsesTheMaximumSoTheNeedleStillMoves)
{
    // The bug this exists to prevent: with the top zone treated as unbounded,
    // every heart rate in it pinned to the end of the segment and 167 looked
    // the same as 200 on a dial whose whole job is telling them apart.
    const uint8_t low  = ZoneLadder::fraction(167.0f, 5, kFloors, 5, 184);
    const uint8_t mid  = ZoneLadder::fraction(175.0f, 5, kFloors, 5, 184);
    const uint8_t high = ZoneLadder::fraction(183.0f, 5, kFloors, 5, 184);
    EXPECT_LT(low, mid);
    EXPECT_LT(mid, high);
    EXPECT_LT(low, 30) << "just inside the top zone should sit near its start";
}

TEST(ZoneLadder, AboveTheMaximumItPins)
{
    // There is no scale beyond the top of the scale.
    EXPECT_EQ(ZoneLadder::fraction(184.0f, 5, kFloors, 5, 184), 255);
    EXPECT_EQ(ZoneLadder::fraction(220.0f, 5, kFloors, 5, 184), 255);
}

TEST(ZoneLadder, WithNoMaximumTheTopZonePinsRatherThanGuessing)
{
    // Membership is still open; only the needle has nowhere to go.
    EXPECT_EQ(ZoneLadder::fraction(170.0f, 5, kFloors, 5, 0), 255);
    // ...and the closed zones below it are unaffected.
    EXPECT_NEAR(ZoneLadder::fraction(101.0f, 1, kFloors, 5, 0), 128, 4);
}

TEST(ZoneLadder, ZoneZeroHasNoPosition)
{
    // Below zone 1 is not a zone, so there is nowhere on the scale to point.
    EXPECT_EQ(ZoneLadder::fraction(80.0f, 0, kFloors, 5, 184), 0);
    EXPECT_EQ(ZoneLadder::fraction(80.0f, 6, kFloors, 5, 184), 0);
}
