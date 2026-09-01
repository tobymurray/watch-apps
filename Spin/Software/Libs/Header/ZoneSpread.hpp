/**
 ******************************************************************************
 * @file    ZoneSpread.hpp
 * @brief   Zone floors for a count, spread over the watch's own range.
 ******************************************************************************
 *
 * A wearer who sets a zone count and leaves the floors alone has said how many
 * zones they want, not where they fall. The floors are spread evenly from half
 * the maximum heart rate up to it -- which is the watch's own rule, its ladder
 * being 50/60/70/80/90/100% of maximum.
 *
 * That it reproduces the watch's floors exactly at five zones is the reason to
 * trust it at three or eight: it is the same rule at a different count, not a
 * training model invented here. `ZoneSpreadMatchesTheWatchAtFiveZones` is that
 * claim as a test, so it stays true.
 *
 * Header-only over no SDK types, so the one piece of arithmetic here can be
 * checked without a kernel. See Tests/ZoneSpread_test.cpp.
 *
 ******************************************************************************
 */

#ifndef ZONE_SPREAD_HPP
#define ZONE_SPREAD_HPP

#include <cstddef>
#include <cstdint>

namespace ZoneSpread {

/// Fill @p out with @p count floors spread over [maxHr/2, maxHr].
/// @return false, leaving @p out untouched, when there is nothing to spread.
inline bool floors(uint8_t maxHr, uint8_t count, uint8_t *out, size_t capacity)
{
    if (maxHr == 0 || count < 2 || out == nullptr || count > capacity) {
        return false;
    }

    const float top  = static_cast<float>(maxHr);
    const float base = top * 0.5f;
    const float span = top - base;

    for (uint8_t i = 0; i < count; ++i) {
        // The top zone's floor is the last step below the maximum, not the
        // maximum itself: a zone starting at the maximum could never be
        // entered.
        out[i] = static_cast<uint8_t>(
            base + span * (static_cast<float>(i) / static_cast<float>(count)) + 0.5f);
    }
    return true;
}

} // namespace ZoneSpread

#endif // ZONE_SPREAD_HPP
