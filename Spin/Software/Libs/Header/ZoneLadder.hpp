/**
 ******************************************************************************
 * @file    ZoneLadder.hpp
 * @brief   The arithmetic of a zone ladder: its floors, and where in one a
 *          heart rate sits.
 ******************************************************************************
 *
 * FIRMWARE: the watch's own ladder is 50/60/70/80/90/100% of maximum heart
 * rate, so floors nobody has set are spread evenly from half the maximum up to
 * it. At five zones that reproduces the watch's floors exactly, which is the
 * reason to trust the same rule at three or eight rather than calling it a
 * training model invented here. Falsified by the watch changing its ladder;
 * ZoneLadder.MatchesTheWatchAtFiveZones is the claim as a test.
 *
 ******************************************************************************
 */

#ifndef ZONE_LADDER_HPP
#define ZONE_LADDER_HPP

#include <cstddef>
#include <cstdint>

namespace ZoneLadder {

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
        // The last step below the maximum, not the maximum: a zone starting
        // there could never be entered.
        out[i] = static_cast<uint8_t>(
            base + span * (static_cast<float>(i) / static_cast<float>(count)) + 0.5f);
    }
    return true;
}

/// Where @p hr sits within zone @p zone, 0..255 across that zone's span.
///
/// The top zone is open, but a needle cannot be placed in an unbounded
/// interval, so this -- and only this -- treats @p maxHr as its ceiling and
/// pins above it. Membership and time-in-zone stay open.
///
/// @param maxHr  the wearer's maximum, or 0 if unknown.
inline uint8_t fraction(float hr, uint8_t zone, const uint8_t *floors,
                        uint8_t count, uint8_t maxHr)
{
    if (zone < 1 || zone > count || floors == nullptr) {
        return 0;
    }
    const float lo = static_cast<float>(floors[zone - 1]);

    // The next floor up, or the maximum for the top zone.
    const float hi = (zone < count) ? static_cast<float>(floors[zone])
                                    : static_cast<float>(maxHr);
    if (hi <= lo) {
        return 255;   // nothing to interpolate across
    }

    float f = (hr - lo) / (hi - lo);
    if (f < 0.0f) { f = 0.0f; }
    if (f > 1.0f) { f = 1.0f; }
    return static_cast<uint8_t>(f * 255.0f + 0.5f);
}

} // namespace ZoneLadder

#endif // ZONE_LADDER_HPP
