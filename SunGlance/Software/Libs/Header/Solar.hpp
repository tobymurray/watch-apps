/**
 ******************************************************************************
 * @file    Solar.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Sunrise and sunset for a day and a place. No SDK, no clock, no I/O.
 ******************************************************************************
 *
 * The whole app is this file plus a way to find out where you are. Everything
 * here is a pure function of its arguments -- no wall clock is read, no file is
 * opened, nothing is cached -- which is what makes a glance whose entire output
 * is two times of day arguable at a desk rather than only at dawn.
 *
 * The algorithm is NOAA's solar position calculation, the one behind their
 * published sunrise/sunset tables: geometric mean longitude and anomaly of the
 * sun, the equation of centre, the equation of time, and the hour angle at
 * which the sun's centre sits at `kStandardAltitudeDeg`. It is good to about a
 * minute at the latitudes people live at, and progressively less good towards
 * the poles, where a minute of time is a vanishing fraction of a degree of
 * altitude and every implementation disagrees with every other. `Tests/` says
 * what the observed disagreement with an independent implementation actually
 * is, per latitude band, rather than repeating the folklore figure.
 *
 * ## What "a day" means here
 *
 * `forLocalDay()` takes a local calendar date and the watch's offset from UTC,
 * and returns UTC instants. That is the only combination that is not a lie:
 *
 *   - The *question* is asked in local terms. "Today's sunset" means the sunset
 *     of the date the watch is showing, and west of Greenwich that sunset is
 *     already tomorrow in UTC. SleepLab learned this the expensive way -- see
 *     `SleepLab/Software/Libs/Sources/Service.cpp:131`, where a UTC day index
 *     filed every night in the Americas under the following day.
 *   - The *answer* has to be an instant, because it will be rendered by
 *     `localtime_r` at display time. Returning a local time-of-day instead
 *     would bake in whatever offset was in force when it was computed, which
 *     is exactly wrong on the two days a year that matter.
 *
 * The transit chosen is the sun's, nearest the *local* midday: the solar day
 * containing local noon. So a watch whose time zone disagrees with its
 * longitude still gets an internally consistent answer -- the right sun, on the
 * wrong clock -- and `Schedule.hpp` is where that disagreement is noticed and
 * said out loud.
 *
 ******************************************************************************
 */

#ifndef SOLAR_HPP
#define SOLAR_HPP

#include <cstdint>

namespace Sun
{

/**
 * @brief Altitude of the sun's centre at the instant called sunrise, degrees.
 *
 * Below the horizon, not on it, and it is two corrections rather than a fudge:
 * the sun's disc is about 0.53 degrees across, so its upper limb clears the
 * horizon while its centre is still 0.27 below, and atmospheric refraction
 * lifts the apparent disc by roughly another 0.57. The sum is the -0.833 that
 * NOAA, the USNO and every almanac use, and it is why a computed sunrise is
 * some minutes earlier than a naive "centre crosses zero" answer.
 */
constexpr double kStandardAltitudeDeg = -0.833;

/// Somewhere on the globe. East and north positive, the sign convention every
/// GNSS receiver and every map tile already uses; the sun does not care but a
/// mixed-up longitude sign is a four-hour error and looks plausible.
struct Position
{
    double latDeg = 0.0;
    double lonDeg = 0.0;
};

/**
 * @brief What kind of day it is at this latitude, this time of year.
 *
 * A separate state rather than a sentinel time, because the polar cases are not
 * failures and must not be rendered as one. Above the Arctic and below the
 * Antarctic circle the equation genuinely has no solution for parts of the
 * year: the sun does not reach the horizon, or does not drop to it. A glance
 * that prints 00:00 there is wrong; one that prints "the sun stays up" is
 * right, and it takes a type to keep the two apart.
 */
enum class DayKind : uint8_t {
    Normal,      ///< The sun rises and sets. `riseUtc` and `setUtc` are set.
    AlwaysUp,    ///< Midnight sun: never drops to the horizon this day.
    AlwaysDown,  ///< Polar night: never reaches it.
};

/// One local day's sun, as UTC instants.
struct Day
{
    DayKind kind    = DayKind::Normal;
    int64_t riseUtc = -1;  ///< -1 unless kind == Normal.
    int64_t setUtc  = -1;  ///< -1 unless kind == Normal.
    /// Solar noon -- the transit. Always set, including on polar days, because
    /// it is what the day was computed around and is the one moment that
    /// exists whatever the sun does with the horizon.
    int64_t noonUtc = -1;
};

/**
 * @brief Sunrise and sunset for the local calendar day @p year-@p month-@p day.
 *
 * @param utcOffsetSec What the watch's time zone adds to UTC, in seconds. This
 *                     defines which day is being asked about; it does not enter
 *                     the astronomy.
 * @param altitudeDeg  Override for twilight of another kind: -6 gives civil
 *                     twilight, -12 nautical, -18 astronomical. The default is
 *                     ordinary sunrise/sunset.
 */
Day forLocalDay(int year, int month, int day, int32_t utcOffsetSec, Position pos,
                double altitudeDeg = kStandardAltitudeDeg);

/**
 * @brief Days since 1970-01-01 for a proleptic Gregorian date.
 *
 * Howard Hinnant's `days_from_civil`, which is exact for every date this watch
 * will ever hold and is 10 lines with no table. Here rather than in a helper
 * because the calendar is not incidental to this app: it is how "today" and
 * "tomorrow" are named, and it has to be the *same* arithmetic that anchors the
 * solar computation or the two can disagree by a day at the edges.
 */
int64_t daysFromCivil(int year, int month, int day);

/// The inverse, for naming tomorrow.
void civilFromDays(int64_t days, int &year, int &month, int &day);

/**
 * @brief The offset from UTC implied by a local calendar reading of @p utc.
 *
 * Given what the local clock says at a known UTC instant, this is what the zone
 * adds. It exists so the service can get an offset out of `localtime_r` without
 * `timegm()` -- which newlib does not promise -- and so that the derivation is
 * testable without a machine in an interesting time zone.
 */
int32_t utcOffsetFromCivil(int year, int month, int day,
                           int hour, int minute, int second, int64_t utc);

} // namespace Sun

#endif // SOLAR_HPP
