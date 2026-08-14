/**
 * @file MapMath_test.cpp
 * @brief WebMercator projection and the zoom rescaling the trace depends on.
 *
 * The rescaling tests are the point of this file. The breadcrumb is stored at
 * one fixed zoom (TRACE_ZOOM) that belongs to the app rather than to any pack,
 * and is shifted to the display zoom at draw time. That choice is only sound
 * if the shift loses nothing a pack-native lattice would have kept.
 */
#include <gtest/gtest.h>

#include <MapKit/MapMath.hpp>

#include <cstdint>
#include <limits>

namespace {

namespace M = MapKit::MapMath;

// An arbitrary mid-northern-latitude reference point. Nothing depends on
// where it is; it exists so the projection tests have one concrete place to
// check every zoom against, away from the equator and the antimeridian where
// a sign error would cancel out.
constexpr int32_t kLatUDeg = 44625000;
constexpr int32_t kLonUDeg = -75955000;

TEST(MapMath, WorldIsTileDimPixelsAtZoomZero)
{
    EXPECT_EQ(M::worldSizePx(0), M::TILE_DIM);
    EXPECT_EQ(M::worldSizePx(1), M::TILE_DIM * 2);
    EXPECT_EQ(M::worldSizePx(16), static_cast<int64_t>(M::TILE_DIM) << 16);
}

TEST(MapMath, LongitudeMapsLinearlyAcrossTheWorld)
{
    EXPECT_EQ(M::lonToWorldX(-180000000, 10), 0);
    EXPECT_EQ(M::lonToWorldX(180000000, 10), M::worldSizePx(10));
    EXPECT_EQ(M::lonToWorldX(0, 10), M::worldSizePx(10) / 2);
}

TEST(MapMath, EquatorIsTheVerticalMidpoint)
{
    EXPECT_EQ(M::latToWorldY(0, 12), M::worldSizePx(12) / 2);
}

TEST(MapMath, LatitudeIsClampedToTheMercatorPoleLimit)
{
    // Beyond ~85.051 deg the projection runs to infinity. Clamping keeps the
    // tile index in range instead of producing a coordinate one row past the
    // bottom of the world.
    const uint8_t z = 14;
    const int64_t maxY = M::worldSizePx(z) - 1;
    EXPECT_GE(M::latToWorldY(89999999, z), 0);
    EXPECT_LE(M::latToWorldY(-89999999, z), maxY);
    EXPECT_EQ(M::latToWorldY(M::MAX_LAT_UDEG, z), M::latToWorldY(89999999, z));
}

TEST(MapMath, TileCoordAndSubTilePartitionAWorldPixel)
{
    const int64_t px = 12345;
    EXPECT_EQ(M::tileCoord(px) * M::TILE_DIM + M::subTile(px), px);
    EXPECT_GE(M::subTile(px), 0);
    EXPECT_LT(M::subTile(px), M::TILE_DIM);
}

TEST(MapMath, RescaleIsIdentityAtTheSameZoom)
{
    EXPECT_EQ(M::rescale(123456, 16, 16), 123456);
}

TEST(MapMath, RescalingOutHalvesPerZoomLevel)
{
    EXPECT_EQ(M::rescale(1024, 18, 17), 512);
    EXPECT_EQ(M::rescale(1024, 18, 16), 256);
    EXPECT_EQ(M::rescale(1024, 18, 14), 64);
}

TEST(MapMath, TraceStorageZoomLosesNothingAgainstAPackNativeLattice)
{
    // The load-bearing property. A point captured at TRACE_ZOOM and rescaled
    // down to a display zoom must land on the same pixel as the same fix
    // projected directly at that zoom -- otherwise storing the trace on an
    // app-fixed lattice would cost accuracy that a per-pack lattice kept.
    for (uint8_t z = 10; z <= M::TRACE_ZOOM; ++z) {
        const int64_t viaStorage = M::rescale(
            M::lonToWorldX(kLonUDeg, M::TRACE_ZOOM), M::TRACE_ZOOM, z);
        const int64_t native = M::lonToWorldX(kLonUDeg, z);
        EXPECT_LE(std::abs(viaStorage - native), 1) << "x at z" << int(z);

        const int64_t viaStorageY = M::rescale(
            M::latToWorldY(kLatUDeg, M::TRACE_ZOOM), M::TRACE_ZOOM, z);
        const int64_t nativeY = M::latToWorldY(kLatUDeg, z);
        EXPECT_LE(std::abs(viaStorageY - nativeY), 1) << "y at z" << int(z);
    }
}

TEST(MapMath, TraceStorageZoomFitsInTheInt32TheTraceBufferUses)
{
    // TraceBuffer stores points as int32. At TRACE_ZOOM the world must fit,
    // with room to spare, or a fix near the antimeridian would wrap.
    const int64_t world = M::worldSizePx(M::TRACE_ZOOM);
    EXPECT_LT(world, static_cast<int64_t>(std::numeric_limits<int32_t>::max()));
    EXPECT_EQ(world, 67108864) << "z18: 2^18 * 256";
}

TEST(MapMath, InverseProjectionRoundTrips)
{
    // The request log writes a tile back out as degrees, so the inverse has to
    // land where the forward projection came from -- to within the resolution
    // the forward projection actually has, which is one world pixel. That is
    // 360e6 microdegrees spread over (TILE_DIM << z) pixels, so it tightens by
    // a factor of two per zoom level; a fixed tolerance would either be
    // meaningless at z16 or unmeetable at z8.
    //
    // Latitude is bounded by the same figure rather than its own: in Mercator
    // the degrees covered by one pixel shrink with cos(lat), so away from the
    // equator the latitude round-trip is strictly tighter than the longitude
    // one.
    for (uint8_t z = 8; z <= 16; ++z) {
        const double onePixelUDeg = 360.0e6 / static_cast<double>(M::worldSizePx(z));
        EXPECT_NEAR(M::worldXToLonUDeg(M::lonToWorldX(kLonUDeg, z), z), kLonUDeg,
                    onePixelUDeg) << "lon at z" << int(z);
        EXPECT_NEAR(M::worldYToLatUDeg(M::latToWorldY(kLatUDeg, z), z), kLatUDeg,
                    onePixelUDeg) << "lat at z" << int(z);
    }
}

TEST(MapMath, InverseProjectionHitsTheKnownAnchors)
{
    const uint8_t z = 12;
    EXPECT_EQ(M::worldXToLonUDeg(0, z), -180000000);
    EXPECT_EQ(M::worldXToLonUDeg(M::worldSizePx(z), z), 180000000);
    EXPECT_EQ(M::worldXToLonUDeg(M::worldSizePx(z) / 2, z), 0);
    EXPECT_EQ(M::worldYToLatUDeg(M::worldSizePx(z) / 2, z), 0);
}

TEST(MapMath, ProjectionIsStableAcrossTheHemispheres)
{
    // Southern latitudes are below the midpoint, northern above it.
    const uint8_t z = 13;
    EXPECT_LT(M::latToWorldY(45000000, z), M::worldSizePx(z) / 2);
    EXPECT_GT(M::latToWorldY(-45000000, z), M::worldSizePx(z) / 2);
}

} // namespace
