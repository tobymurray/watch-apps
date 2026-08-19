/**
 * @file Palette_test.cpp
 * @brief The 14 slots, against the spec table they were copied from.
 *
 * These tests are not checking arithmetic. They are checking a transcription:
 * every code here was typed in from MAP_CARTOGRAPHY_SPEC.md § 3, and a typo in
 * one of them would produce a card that looks plausible and answers the wrong
 * question. So each slot is asserted against its documented r/g/b triple
 * independently of the byte, which is the only way a single transcription
 * error can fail.
 */
#include <gtest/gtest.h>

#include <Palette.hpp>

namespace {

using namespace MapLab;

TEST(Palette, ByteLayoutIsAbgrMsbFirst)
{
    EXPECT_EQ(abgr2222(3, 3, 3), 0xFF);
    EXPECT_EQ(abgr2222(0, 0, 0), 0xC0);
    // The two the spec spells out in full, and the pair that pins the channel
    // order: swapping r and b would leave the first of these unchanged.
    EXPECT_EQ(abgr2222(1, 3, 1), 0xDD);   // wood_lt, "1 3 1"
    EXPECT_EQ(abgr2222(0, 1, 3), 0xF4);   // water,   "0 1 3"
    EXPECT_NE(abgr2222(3, 1, 0), 0xF4);
}

TEST(Palette, EverySlotRoundTripsThroughItsChannels)
{
    for (const SlotSpec& s : kSlots) {
        EXPECT_EQ(abgr2222(redOf(s.code), greenOf(s.code), blueOf(s.code)), s.code)
            << "slot " << s.name;
        EXPECT_EQ(alphaOf(s.code), 3) << "slot " << s.name << " must be opaque";
    }
}

TEST(Palette, SlotsMatchTheSpecTable)
{
    // r, g, b as printed in § 3's table. Written out rather than derived, so
    // that this test disagrees with a mistyped constant instead of agreeing
    // with it.
    struct Expect { Slot slot; uint8_t r, g, b; };
    const Expect kExpected[] = {
        { Slot::Paper,     3, 3, 3 },
        { Slot::Landuse,   2, 3, 2 },
        { Slot::WoodLight, 1, 3, 1 },
        { Slot::Building,  2, 2, 2 },
        { Slot::Wood,      0, 2, 1 },
        { Slot::Water,     0, 1, 3 },
        { Slot::Contour,   1, 1, 0 },
        { Slot::WaterDark, 0, 0, 3 },
        { Slot::Trace,     3, 0, 0 },
        { Slot::RoadMinor, 1, 0, 0 },
        { Slot::Path,      0, 0, 1 },
        { Slot::RoadMajor, 0, 0, 0 },
    };
    ASSERT_EQ(sizeof(kExpected) / sizeof(kExpected[0]), static_cast<size_t>(Slot::Count));
    for (const Expect& e : kExpected) {
        EXPECT_EQ(code(e.slot), abgr2222(e.r, e.g, e.b))
            << "slot " << kSlots[static_cast<int>(e.slot)].name;
    }
}

TEST(Palette, InkAndHaloAreTheCodesTheSpecSpends)
{
    // R1: road_major and label text share the darkest code; the halo is paper.
    EXPECT_EQ(kInk, code(Slot::RoadMajor));
    EXPECT_EQ(kHalo, code(Slot::Paper));
}

TEST(Palette, DayIsIdentityAcrossAllSixtyFour)
{
    uint8_t lut[kLutEntries];
    ASSERT_TRUE(buildLut(Variant::Day, lut));
    for (int i = 0; i < kLutEntries; ++i) {
        EXPECT_EQ(lut[i], 0xC0 | i);
    }
}

TEST(Palette, NightInvertsGroundAndInk)
{
    uint8_t lut[kLutEntries];
    ASSERT_TRUE(buildLut(Variant::Night, lut));
    EXPECT_EQ(lut[lutIndex(code(Slot::Paper))], code(Slot::RoadMajor));
    EXPECT_EQ(lut[lutIndex(code(Slot::RoadMajor))], code(Slot::Paper));
    EXPECT_EQ(lut[lutIndex(code(Slot::RoadMinor))], code(Slot::Paper));
}

TEST(Palette, TraceSurvivesEveryVariant)
{
    // R5: the trace is app-drawn over the blit and must win against every
    // basemap colour. A variant that remapped it would be mapping a code the
    // pack never contains, and would break the one card that checks it.
    for (int v = 0; v < static_cast<int>(Variant::Count); ++v) {
        uint8_t lut[kLutEntries];
        ASSERT_TRUE(buildLut(static_cast<Variant>(v), lut));
        EXPECT_EQ(lut[lutIndex(code(Slot::Trace))], code(Slot::Trace))
            << "variant " << variantName(static_cast<Variant>(v));
    }
}

TEST(Palette, UnusedCodesPassThroughUntouched)
{
    // 50 of the 64 codes are deliberately unspent. A variant that collapsed
    // them would be lying about what it changes.
    uint8_t lut[kLutEntries];
    ASSERT_TRUE(buildLut(Variant::Trail, lut));
    int changed = 0;
    for (int i = 0; i < kLutEntries; ++i) {
        if (lut[i] != (0xC0 | i)) {
            ++changed;
        }
    }
    EXPECT_LE(changed, static_cast<int>(Slot::Count));
}

TEST(Palette, AnOutOfRangeVariantIsRefusedAndLeavesIdentity)
{
    uint8_t lut[kLutEntries];
    EXPECT_FALSE(buildLut(Variant::Count, lut));
    for (int i = 0; i < kLutEntries; ++i) {
        EXPECT_EQ(lut[i], 0xC0 | i);
    }
}

} // namespace
