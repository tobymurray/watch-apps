/**
 ******************************************************************************
 * @file    MapMath.hpp
 * @brief   WebMercator lat/lon -> world-pixel math.
 *
 * All positions are converted into "world pixels" at a given zoom: the
 * WebMercator plane scaled so one map tile is TILE_DIM px, i.e. the world
 * is (2^z * TILE_DIM) px on a side. Tile coordinates and sub-tile offsets
 * are then bit operations on the world-pixel values.
 *
 * Everything is computed in double and returned as int64: float resolves
 * only ~0.36 px at z16 (measured during the rawtiles evaluation), which
 * makes a GPS trace visibly wobble; double is exact to far below a pixel at
 * every legal zoom. Inputs are integer microdegrees (1e-6 deg), the format
 * GPS fixes and the rawtiles header bbox both use.
 *
 * Pure code, no SDK dependencies: host-tested (MapKit/Tests/MapMath_test.cpp).
 ******************************************************************************
 */

#ifndef MAPKIT_MAPMATH_HPP
#define MAPKIT_MAPMATH_HPP

#include <cmath>
#include <cstdint>

namespace MapKit
{
namespace MapMath
{

/// Tile side length in pixels. Every pack MapKit will draw declares
/// tile_dim_px = 256 (PackCatalog rejects anything else, because the low
/// log2(TILE_DIM) bits of a world pixel are the sub-tile offset and that
/// identity is what makes the mosaic arithmetic shifts rather than divides).
constexpr int32_t TILE_DIM       = 256;
constexpr int32_t TILE_SHIFT     = 8;      // log2(TILE_DIM)
constexpr double  MICRODEG       = 1e-6;
/// WebMercator pole clamp, microdegrees (rawtiles spec § 4.9).
constexpr int32_t MAX_LAT_UDEG   = 85051129;

/**
 * @brief Zoom at which the breadcrumb trace is stored, independent of any
 *        pack.
 *
 * The trace outlives pack selection -- a run may cross from one pack to
 * another -- so it cannot be stored at "the current pack's zoom". z18 is
 * chosen because the world is then 2^18 * 256 = 67,108,864 px on a side,
 * comfortably inside int32, while being at least as fine as any pack these
 * apps will meet (the packs measured for this work are z12..z16). Rendering
 * shifts right by (18 - displayZoom), so a z16 render from z18 storage is
 * bit-identical to storing at z16 natively.
 *
 * A pack with zoom_max > 18 would draw its trace on this coarser lattice
 * (0.3 m/px at the equator) -- visible only under a magnifier, and no pack
 * that deep is plausible for a 240x240 panel.
 */
constexpr uint8_t TRACE_ZOOM = 18;

struct WorldPx {
    int64_t x;
    int64_t y;
};

/// World size in pixels at zoom z (z in [0, 23]).
inline int64_t worldSizePx(uint8_t z)
{
    return static_cast<int64_t>(TILE_DIM) << z;
}

/// Longitude (microdegrees) -> world-pixel X at zoom z.
inline int64_t lonToWorldX(int32_t lonUdeg, uint8_t z)
{
    const double lon = static_cast<double>(lonUdeg) * MICRODEG;
    const double n   = static_cast<double>(worldSizePx(z));
    return static_cast<int64_t>(std::llround((lon + 180.0) / 360.0 * n));
}

/// Latitude (microdegrees) -> world-pixel Y at zoom z (XYZ axis: y grows
/// southward). Latitude is clamped to the Mercator pole limit.
inline int64_t latToWorldY(int32_t latUdeg, uint8_t z)
{
    if (latUdeg > MAX_LAT_UDEG) {
        latUdeg = MAX_LAT_UDEG;
    } else if (latUdeg < -MAX_LAT_UDEG) {
        latUdeg = -MAX_LAT_UDEG;
    }
    const double latRad = static_cast<double>(latUdeg) * MICRODEG * (M_PI / 180.0);
    const double merc   = std::asinh(std::tan(latRad));       // in [-pi, pi]
    const double n      = static_cast<double>(worldSizePx(z));
    int64_t y = static_cast<int64_t>(std::llround((1.0 - merc / M_PI) / 2.0 * n));
    // llround at the exact poles can land on worldSize; keep y in range so
    // tileY never indexes one past the last row.
    const int64_t maxY = worldSizePx(z) - 1;
    if (y < 0) {
        y = 0;
    } else if (y > maxY) {
        y = maxY;
    }
    return y;
}

inline WorldPx toWorldPx(int32_t latUdeg, int32_t lonUdeg, uint8_t z)
{
    return WorldPx { lonToWorldX(lonUdeg, z), latToWorldY(latUdeg, z) };
}

/// Tile coordinate containing a world pixel.
inline uint32_t tileCoord(int64_t worldPx)
{
    return static_cast<uint32_t>(worldPx >> TILE_SHIFT);
}

/// Offset of a world pixel within its tile, [0, TILE_DIM).
inline int32_t subTile(int64_t worldPx)
{
    return static_cast<int32_t>(worldPx & (TILE_DIM - 1));
}

/// Rescale a world pixel from zoom `fromZ` to zoom `toZ` (shift by the
/// zoom delta; exact for zoom-outs of points captured at a finer zoom).
inline int64_t rescale(int64_t worldPx, uint8_t fromZ, uint8_t toZ)
{
    if (toZ >= fromZ) {
        return worldPx << (toZ - fromZ);
    }
    return worldPx >> (fromZ - toZ);
}

} // namespace MapMath
} // namespace MapKit

#endif // MAPKIT_MAPMATH_HPP
