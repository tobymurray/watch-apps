/**
 ******************************************************************************
 * @file    PackDepth.hpp
 * @brief   How deep a pack can actually draw, as opposed to how deep it says
 *          it is.
 *
 * `PackSelection` ranks candidates on `zoom_max`, which is right only while
 * that field describes something drawable. It is a declaration, not a
 * measurement: a pack may carry `zoom_max = 16` and hold no tiles at 16, or
 * no tiles at all. The rawtiles spec permits both -- `tile_count = 0` is a
 * legal pack -- and the header's zoom directory says which it is.
 *
 * Ranking on the declaration lets an empty pack win. Measured on a real
 * `SharedData/maps/`: a 364-byte fixture with `tile_count = 0` and
 * `zoom_max = 16` beat a 2 MB pack of the same region whose `zoom_max` was
 * 15, and the map face drew nothing while reporting itself live -- the
 * catalog accepted it, selection preferred it, `findTile()` found nothing,
 * and no layer of that has an error to report.
 *
 * So the fact the rule wants is the deepest zoom the pack has tiles at, which
 * the header answers for free: `zoom_offsets[24]` at byte 96 carries a
 * `(offset, count)` pair per zoom, and `PackCatalog::peek()` already reads the
 * whole 292-byte header to get the bbox.
 *
 * Pure by design: no SDK, no kernel, no filesystem -- header bytes in, one
 * number out -- so it is tested in `mapkit-pure-tests` beside the selection
 * rule it feeds.
 ******************************************************************************
 */

#ifndef MAPKIT_PACKDEPTH_HPP
#define MAPKIT_PACKDEPTH_HPP

#include <cstddef>
#include <cstdint>

namespace MapKit
{

/// Bytes of header the rawtiles v1 wire format defines, and the size of the
/// buffer these functions expect.
constexpr size_t kPackHeaderBytes = 292;

/// Zoom directory: 24 × (u32 offset, u32 count), starting at byte 96.
constexpr size_t kPackZoomDirOffset = 96;
constexpr size_t kPackZoomDirCount  = 24;

/**
 * @brief Deepest zoom at which @p header's pack actually holds tiles.
 *
 * @param header: at least @c kPackHeaderBytes bytes of rawtiles v1 header.
 * @param length: bytes available at @p header.
 * @param out: set to the deepest zoom with a non-zero tile count.
 * @return false when the buffer is too short, or the pack holds no tiles at
 *         any zoom — in which case it cannot draw anything anywhere and is
 *         not a candidate for anything.
 */
bool deepestZoomWithTiles(const uint8_t* header, size_t length, uint8_t& out);

} // namespace MapKit

#endif // MAPKIT_PACKDEPTH_HPP
