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
    // THE test. The watch's ladder for a 184 bpm maximum is
    // 92/110/129/147/166/184, and the first five are its floors. If this stops
    // matching, the reason to trust the rule at three or eight zones is gone.
    uint8_t out[8] = {};
    ASSERT_TRUE(ZoneLadder::floors(184, 5, out, 8));
    EXPECT_EQ(out[0], 92);
    EXPECT_EQ(out[1], 110);
    EXPECT_EQ(out[2], 129);
    EXPECT_EQ(out[3], 147);
    EXPECT_EQ(out[4], 166);
}

TEST(ZoneLadder, SplitsTheLadderTheWatchActuallySent)
{
    // Pulled off the watch: settings.json held heartRateZones
    // [92,110,129,147,166,184], and recovery.log reported six floors beside
    // max_hr=0 -- which the old split produces only if heartRateCount was 7,
    // one more than the thresholds filled. It took the unfilled slot for the
    // maximum and left 184 standing as a sixth floor, and that is what made
    // every recovery window no_max_hr.
    const uint8_t sent[8] = {92, 110, 129, 147, 166, 184, 0, 0};
    uint8_t out[8] = {};
    uint8_t maxHr  = 0;

    EXPECT_EQ(ZoneLadder::fromWatch(sent, 8, 7, out, 8, maxHr), 5);
    EXPECT_EQ(maxHr, 184);

    uint8_t spread[8] = {};
    ASSERT_TRUE(ZoneLadder::floors(184, 5, spread, 8));
    for (uint8_t i = 0; i < 5; ++i) {
        EXPECT_EQ(out[i], spread[i]) << "floor " << int(i);
    }
}

TEST(ZoneLadder, LeavesTheMaximumAloneWhenTheWatchSentNoLadder)
{
    const uint8_t sent[8] = {};
    uint8_t out[8] = {};

    for (uint8_t zoneCount : {0, 1}) {
        uint8_t maxHr = 0;
        EXPECT_EQ(ZoneLadder::fromWatch(sent, 8, zoneCount, out, 8, maxHr), 0);
        EXPECT_EQ(maxHr, 0) << "zone count " << int(zoneCount);
    }
}

TEST(ZoneLadder, KeepsTheMaximumWhenThereIsRoomForNoFloors)
{
    // Two zones is one threshold, and that threshold is the maximum.
    const uint8_t sent[8] = {184, 0, 0, 0, 0, 0, 0, 0};
    uint8_t out[8] = {};
    uint8_t maxHr  = 0;

    EXPECT_EQ(ZoneLadder::fromWatch(sent, 8, 2, out, 8, maxHr), 0);
    EXPECT_EQ(maxHr, 184);
}

TEST(ZoneLadder, WritesNoMoreFloorsThanItWasGivenRoomFor)
{
    const uint8_t sent[8] = {92, 110, 129, 147, 166, 184, 0, 0};
    uint8_t out[3] = {};
    uint8_t maxHr  = 0;

    EXPECT_EQ(ZoneLadder::fromWatch(sent, 8, 7, out, 3, maxHr), 3);
    EXPECT_EQ(maxHr, 184);
    EXPECT_EQ(out[2], 129);
}

TEST(ZoneLadder, ReadsNoFurtherThanTheMessageItWasHanded)
{
    // The count comes from firmware and the array it indexes does not grow, so
    // a count past the end has to stop at the end.
    const uint8_t sent[4] = {92, 110, 129, 147};
    uint8_t out[8] = {};
    uint8_t maxHr  = 0;

    EXPECT_EQ(ZoneLadder::fromWatch(sent, 4, 200, out, 8, maxHr), 3);
    EXPECT_EQ(maxHr, 147);
    EXPECT_EQ(out[2], 129);
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
    // The bug this prevents: with the top zone unbounded, 167 and 200 both
    // pinned to the end of the segment.
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
