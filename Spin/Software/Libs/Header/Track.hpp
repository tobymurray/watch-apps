/**
 ******************************************************************************
 * @file    Track.hpp
 * @brief   Ride state and the once-a-second metrics snapshot.
 ******************************************************************************
 */

#ifndef TRACK_HPP
#define TRACK_HPP

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace Track
{

/// Zone buckets: [0] is time below zone 1 and [1..N] the zones, so the most
/// buckets is one more than the most zones. Eight is where the kernel's own
/// threshold table stops. Service::skZoneBuckets and ActivityWriter::
/// kZoneBuckets must agree, and Service.hpp static_asserts that they do.
constexpr size_t kZoneBuckets = 9;

/// INACTIVE covers both "not started yet" and "finished"; the GUI tells them
/// apart by whether it has been handed a summary.
enum class State : uint8_t {
    INACTIVE = 0,
    ACTIVE,
    PAUSED
};

/// Published every second by the Service; the GUI derives no number of its own.
struct Data {
    std::time_t totalTime = 0;      ///< active (unpaused) seconds

    float   hr           = 0.0f;    ///< current bpm, arbitrated by the kernel
    float   hrTrustLevel = 0.0f;    ///< kernel's confidence, 1..3 is usable
    uint8_t hrSource     = 0;       ///< HeartRateEx::Source: 0 none, 1 optical, 2 external

    float avgHR = 0.0f;             ///< bpm over the ride so far
    float maxHR = 0.0f;             ///< bpm

    /// 0 = below zone 1 or no usable reading, else 1..zone count.
    uint8_t hrZone = 0;

    /// Where hr sits within hrZone, 0..255 across that zone's span; the dial's
    /// needle. Meaningless unless hrZone >= 1.
    uint8_t hrZoneFraction = 0;

    /// Thresholds are set on the watch -- a different state from below zone 1.
    bool hasZones = false;

    /// Seconds in each bucket, indexed by hrZone.
    std::time_t zoneSeconds[kZoneBuckets] = {};

    float calories        = 0.0f;   ///< kcal, active, over the ride
    float restingCalories = 0.0f;   ///< kcal at MET 1.0, accrued every active second

    uint32_t lapNum = 0;            ///< laps closed so far (auto-lap only)

    /// The same flag that fired the buzz, so the screen can never announce the
    /// target a second before or after the wrist felt it.
    bool targetReached = false;
};

} // namespace Track

#endif // TRACK_HPP
