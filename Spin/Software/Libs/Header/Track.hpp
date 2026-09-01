/**
 ******************************************************************************
 * @file    Track.hpp
 * @brief   Ride state and the once-a-second metrics snapshot.
 ******************************************************************************
 */

#ifndef TRACK_HPP
#define TRACK_HPP

#include <cstdint>
#include <ctime>

namespace Track
{

/// Where the ride is. INACTIVE covers both "not started yet" and "finished":
/// the GUI distinguishes those two by whether it has been handed a summary,
/// because the Service has nothing to say about a ride that never began.
enum class State : uint8_t {
    INACTIVE = 0,
    ACTIVE,
    PAUSED
};

/// Everything the screen draws while a ride is running. Published every second
/// by the Service; the GUI never derives a number of its own from it.
struct Data {
    std::time_t totalTime = 0;      ///< active (unpaused) seconds

    float   hr           = 0.0f;    ///< current bpm, arbitrated by the kernel
    float   hrTrustLevel = 0.0f;    ///< kernel's confidence, 1..3 is usable
    uint8_t hrSource     = 0;       ///< HeartRateEx::Source: 0 none, 1 optical, 2 external

    float avgHR = 0.0f;             ///< bpm over the ride so far
    float maxHR = 0.0f;             ///< bpm

    /// 0 = below zone 1 (or no usable reading), 1..5 = the zone the current
    /// heart rate falls in, against the wearer's own thresholds from the watch.
    uint8_t hrZone = 0;

    /// The wearer has zone thresholds set on the watch. Without them there are
    /// no zones to be in, which is a different thing from being below zone 1.
    bool hasZones = false;

    /// Seconds spent in each bucket. [0] is below zone 1, [1..5] the zones.
    std::time_t zoneSeconds[6] = {};

    float calories        = 0.0f;   ///< kcal, active, over the ride
    float restingCalories = 0.0f;   ///< kcal at MET 1.0, accrued every active second

    uint32_t lapNum = 0;            ///< laps closed so far (auto-lap only)

    /// The target time has been passed. Owned here rather than recomputed by
    /// the GUI: this is the same flag that fired the buzz, so the screen can
    /// never say "target met" a second before or after the wrist felt it.
    bool targetReached = false;
};

} // namespace Track

#endif // TRACK_HPP
