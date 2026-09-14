/**
 ******************************************************************************
 * @file    Freshness.hpp
 * @brief   Whether a reading is recent enough to still be worth showing.
 ******************************************************************************
 *
 * The screen must never show a number the sensor has stopped producing. A
 * stale heart rate is worse than none at all on an instrument, because it is
 * indistinguishable from a live one -- the wearer reads a plausible figure and
 * has no way to tell it is minutes old.
 *
 * Header-only and free of SDK types so the rule can be tested without a
 * kernel; the Service owns the clock and passes seconds in.
 *
 ******************************************************************************
 */

#ifndef FRESHNESS_HPP
#define FRESHNESS_HPP

#include <ctime>

namespace Freshness
{

/// Seconds after a heart-rate reading before the screen stops showing it.
///
/// MEASURED. Across the 6,101 inter-sample gaps in every recording under
/// `Tests/pulled`, the sensor delivers at a nominal 1 Hz: 96.7% of gaps are
/// ~1005 ms, 3.0% are ~2010 ms -- one tick missed -- and the largest gap ever
/// observed is 2117 ms. No gap of three seconds or more has occurred, so three
/// is above every one of them while still declaring a dead sensor within three
/// seconds. Falsified by a recording carrying a longer gap; re-derive from the
/// `_hr.csv` sidecars.
///
/// Erring short is deliberate. A false positive blanks the reading for a
/// second; a false negative leaves a number that means nothing.
constexpr std::time_t kHeartRateStaleAfterS = 3;

/**
 * @brief Is a reading taken at @p takenUtc still current at @p nowUtc?
 *
 * @param takenUtc  When the reading arrived; 0 means none has.
 * @param nowUtc    Now.
 * @param withinS   How many seconds count as current.
 *
 * A reading from the future is not treated as current. The clock can step
 * backwards when the watch resyncs, and treating a negative age as "very
 * recent" would pin the display to a stale value for as long as the step --
 * exactly the failure this exists to prevent, arriving by the back door.
 */
inline bool isCurrent(std::time_t takenUtc, std::time_t nowUtc, std::time_t withinS)
{
    if (takenUtc <= 0) {
        return false;
    }
    if (nowUtc < takenUtc) {
        return false;
    }
    return (nowUtc - takenUtc) < withinS;
}

} // namespace Freshness

#endif // FRESHNESS_HPP
