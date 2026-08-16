/**
 * Tests for the "how deep can this pack actually draw" rule.
 *
 * The case that motivated it is the last one: a pack that declares a deep
 * zoom_max and holds nothing must not outrank a pack that can draw.
 */
#include <MapKit/PackDepth.hpp>
#include <MapKit/PackSelection.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>

namespace
{

using Header = std::array<uint8_t, MapKit::kPackHeaderBytes>;

/// A header with `count` tiles recorded at `zoom` in the zoom directory.
Header headerWith(std::initializer_list<std::pair<uint8_t, uint32_t>> zooms)
{
    Header h{};
    h.fill(0);
    for (const auto& zc : zooms) {
        uint8_t* entry = h.data() + MapKit::kPackZoomDirOffset + (zc.first * 8);
        const uint32_t count = zc.second;
        entry[4] = static_cast<uint8_t>(count & 0xFF);
        entry[5] = static_cast<uint8_t>((count >> 8) & 0xFF);
        entry[6] = static_cast<uint8_t>((count >> 16) & 0xFF);
        entry[7] = static_cast<uint8_t>((count >> 24) & 0xFF);
    }
    return h;
}

TEST(PackDepth, ReportsTheDeepestZoomHoldingTiles)
{
    const Header h = headerWith({{12, 6}, {13, 20}, {14, 70}});
    uint8_t z = 0;
    ASSERT_TRUE(MapKit::deepestZoomWithTiles(h.data(), h.size(), z));
    EXPECT_EQ(z, 14);
}

TEST(PackDepth, IgnoresDeeperZoomsThatAreEmpty)
{
    // Declares depth to 16 in its header, but the tiles stop at 14.
    const Header h = headerWith({{14, 70}, {15, 0}, {16, 0}});
    uint8_t z = 0;
    ASSERT_TRUE(MapKit::deepestZoomWithTiles(h.data(), h.size(), z));
    EXPECT_EQ(z, 14);
}

TEST(PackDepth, ZoomZeroIsALegalAnswer)
{
    const Header h = headerWith({{0, 1}});
    uint8_t z = 9;
    ASSERT_TRUE(MapKit::deepestZoomWithTiles(h.data(), h.size(), z));
    EXPECT_EQ(z, 0);
}

TEST(PackDepth, APackWithNoTilesAnywhereFails)
{
    const Header h = headerWith({});
    uint8_t z = 9;
    EXPECT_FALSE(MapKit::deepestZoomWithTiles(h.data(), h.size(), z));
}

TEST(PackDepth, ShortBufferFails)
{
    const Header h = headerWith({{14, 70}});
    uint8_t z = 0;
    EXPECT_FALSE(MapKit::deepestZoomWithTiles(h.data(), h.size() - 1, z));
    EXPECT_FALSE(MapKit::deepestZoomWithTiles(nullptr, h.size(), z));
}

/// The regression this exists for, expressed the way it happened on a real
/// watch: an empty fixture and a real map, both covering the fix.
TEST(PackDepth, AnEmptyPackDoesNotOutrankAMapThatCanDraw)
{
    const Header empty = headerWith({});
    const Header real  = headerWith({{13, 8}, {14, 25}, {15, 33}});

    uint8_t emptyZoom = 0;
    uint8_t realZoom  = 0;
    // The empty one never becomes a candidate at all...
    EXPECT_FALSE(MapKit::deepestZoomWithTiles(empty.data(), empty.size(), emptyZoom));
    ASSERT_TRUE(MapKit::deepestZoomWithTiles(real.data(), real.size(), realZoom));

    // ...so selection sees one candidate, and picks it.
    MapKit::PackFacts facts{};
    facts.bboxMinLonUDeg = -76'015'000;
    facts.bboxMinLatUDeg =  44'590'000;
    facts.bboxMaxLonUDeg = -75'889'000;
    facts.bboxMaxLatUDeg =  44'662'000;
    facts.zoomMax        = realZoom;
    facts.name           = "real.rawtiles";

    const size_t pick = MapKit::selectPack(&facts, 1, 44'626'000, -75'952'000);
    ASSERT_NE(pick, MapKit::kNoPack);
    EXPECT_EQ(facts.zoomMax, 15);
}

} // namespace
