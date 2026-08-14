/**
 * @file PackSelection_test.cpp
 * @brief The rule that decides which of several packs gets drawn.
 *
 * This is the one design decision these apps had to make that the proof of
 * concept did not (it hardcoded a single pack path), so it is pinned here
 * rather than left to be re-derived from the implementation.
 */
#include <gtest/gtest.h>

#include <MapKit/PackSelection.hpp>

namespace {

using MapKit::kNoPack;
using MapKit::PackFacts;
using MapKit::preferPack;
using MapKit::selectPack;

// Somewhere in eastern Ontario, in microdegrees. Only the arithmetic matters.
constexpr int32_t kLat = 44600000;
constexpr int32_t kLon = -76000000;

PackFacts pack(const char* name, uint8_t zoomMax,
               int32_t minLon, int32_t minLat, int32_t maxLon, int32_t maxLat,
               bool corrupt = false)
{
    PackFacts p{};
    p.name           = name;
    p.zoomMax        = zoomMax;
    p.bboxMinLonUDeg = minLon;
    p.bboxMinLatUDeg = minLat;
    p.bboxMaxLonUDeg = maxLon;
    p.bboxMaxLatUDeg = maxLat;
    p.knownCorrupt   = corrupt;
    return p;
}

/// A box of the given half-width centred on the reference fix.
PackFacts around(const char* name, uint8_t zoomMax, int32_t halfSpan, bool corrupt = false)
{
    return pack(name, zoomMax,
                kLon - halfSpan, kLat - halfSpan,
                kLon + halfSpan, kLat + halfSpan, corrupt);
}

TEST(PackSelection, NoPacksAtAllSelectsNothing)
{
    EXPECT_EQ(selectPack(nullptr, 0, kLat, kLon), kNoPack);
}

TEST(PackSelection, NothingCoveringTheFixSelectsNothing)
{
    // A pack that does not cover you is a blank screen either way, so it is
    // never preferable to an honest "no map for here".
    const PackFacts packs[] = {
        pack("elsewhere.rawtiles", 16, 0, 0, 1000000, 1000000),
    };
    EXPECT_EQ(selectPack(packs, 1, kLat, kLon), kNoPack);
}

TEST(PackSelection, PicksTheOnlyPackThatCoversTheFix)
{
    const PackFacts packs[] = {
        pack("elsewhere.rawtiles", 18, 0, 0, 1000000, 1000000),   // deeper, but wrong place
        around("here.rawtiles", 14, 100000),
    };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), 1u);
}

TEST(PackSelection, BoundingBoxEdgesCount)
{
    // A fix exactly on the boundary is inside. The alternative -- treating the
    // edge as outside -- would make a pack built to end at a coastline or a
    // trailhead stop working precisely where it was meant to.
    const PackFacts p = pack("edge.rawtiles", 16, kLon, kLat, kLon + 10, kLat + 10);
    EXPECT_TRUE(p.contains(kLat, kLon));
    EXPECT_TRUE(p.contains(kLat + 10, kLon + 10));
    EXPECT_FALSE(p.contains(kLat, kLon - 1));
    EXPECT_FALSE(p.contains(kLat + 11, kLon));
}

TEST(PackSelection, MoreDetailWinsFirst)
{
    const PackFacts packs[] = {
        around("coarse.rawtiles", 14, 50000),    // smaller box...
        around("detailed.rawtiles", 17, 500000), // ...but this one goes deeper
    };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), 1u);
}

TEST(PackSelection, AtEqualDetailTheMoreLocalPackWins)
{
    const PackFacts packs[] = {
        around("region.rawtiles", 16, 500000),
        around("city.rawtiles",   16,  20000),
    };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), 1u);
}

TEST(PackSelection, TotalTiesBreakOnNameSoTheChoiceIsReproducible)
{
    // Two packs identical in every respect the rule looks at. Order in the
    // directory listing must not decide it, or the same watch could draw a
    // different map after an unrelated file operation, and no bug report
    // would be reproducible.
    const PackFacts forwards[]  = { around("b.rawtiles", 16, 1000), around("a.rawtiles", 16, 1000) };
    const PackFacts backwards[] = { around("a.rawtiles", 16, 1000), around("b.rawtiles", 16, 1000) };
    EXPECT_STREQ(forwards[selectPack(forwards, 2, kLat, kLon)].name, "a.rawtiles");
    EXPECT_STREQ(backwards[selectPack(backwards, 2, kLat, kLon)].name, "a.rawtiles");
}

TEST(PackSelection, KnownCorruptIsSkippedEvenWhenItWouldOtherwiseWin)
{
    // Bad is the one verdict that is final: re-reading a corrupt file does not
    // make it whole. Preferring it would be choosing a permanently blank
    // screen over a working map.
    const PackFacts packs[] = {
        around("broken.rawtiles", 18, 1000, /*corrupt=*/true),
        around("fine.rawtiles",   13, 900000),
    };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), 1u);
}

TEST(PackSelection, AllCandidatesCorruptSelectsNothing)
{
    const PackFacts packs[] = {
        around("broken1.rawtiles", 16, 1000, true),
        around("broken2.rawtiles", 14, 2000, true),
    };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), kNoPack);
}

TEST(PackSelection, VerificationProgressIsNotPartOfTheRule)
{
    // Deliberate, and the reason PackFacts has no "trusted" field to test
    // against: ranking on how far a background scan has got would make the map
    // swap packs mid-activity at a moment governed by disk throughput. Trust
    // gates rendering, not choice. This test exists to fail loudly if anyone
    // adds trust to the ranking later.
    static_assert(sizeof(PackFacts) > 0, "PackFacts exists");
    const PackFacts packs[] = {
        around("unverified-detailed.rawtiles", 17, 1000),
        around("trusted-coarse.rawtiles",      12, 1000),
    };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), 0u)
        << "the deeper pack must win regardless of verification state";
}

TEST(PackSelection, PreferPackIsAStrictOrderingOnTheTieBreaks)
{
    const PackFacts a = around("a.rawtiles", 16, 1000);
    const PackFacts b = around("b.rawtiles", 16, 1000);
    EXPECT_TRUE(preferPack(a, b));
    EXPECT_FALSE(preferPack(b, a));
    EXPECT_FALSE(preferPack(a, a));
}

TEST(PackSelection, WholeWorldBboxAreaDoesNotOverflow)
{
    // 360e6 x 180e6 microdegrees overflows int32 many times over; the area
    // tie-break is computed in int64 for exactly this case.
    const PackFacts world = pack("world.rawtiles", 8,
                                 -180000000, -85000000, 180000000, 85000000);
    EXPECT_GT(world.areaUDeg2(), static_cast<int64_t>(6) * 1000 * 1000 * 1000 * 1000 * 1000);
    const PackFacts packs[] = { world, around("local.rawtiles", 8, 1000) };
    EXPECT_EQ(selectPack(packs, 2, kLat, kLon), 1u);
}

} // namespace
