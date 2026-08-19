/**
 ******************************************************************************
 * @file    Schedule.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Which sun event to put on the glance, and when not to trust it.
 ******************************************************************************
 *
 * A glance answers one question. For the sun that question is "what happens
 * next, and when" -- not "here are today's two times", which is a table and
 * belongs on a screen somebody chose to open. So the policy of picking the
 * *next* event out of a pair of days is its own thing, tested on its own, and
 * kept out of both the astronomy and the drawing.
 *
 * Pure, and no clock of its own: `nowUtc` is passed in. Every interesting case
 * here -- the minute before sunrise, the minute after sunset, midnight, the day
 * the midnight sun starts -- is a specific instant, and a function that read the
 * clock itself could only be tested at whatever instant the test happened to run.
 *
 ******************************************************************************
 */

#ifndef SCHEDULE_HPP
#define SCHEDULE_HPP

#include <cstdint>

#include "Solar.hpp"

namespace Sun
{

enum class EventKind : uint8_t {
    Rise,         ///< The sun comes up at `whenUtc`.
    Set,          ///< It goes down at `whenUtc`.
    MidnightSun,  ///< Neither, today: it stays up. `whenUtc` is -1.
    PolarNight,   ///< Neither, today: it stays down. `whenUtc` is -1.
};

/// What the glance is about to say.
struct Next
{
    EventKind kind     = EventKind::Rise;
    int64_t   whenUtc  = -1;
    /// The day's other event, whether it has happened yet or not: the sunset
    /// still to come when the headline is a sunrise, the sunrise already past
    /// when it is a sunset. -1 when there is nothing sensible to pair with --
    /// after today's sunset the pair belongs to a day that has not started.
    int64_t   otherUtc = -1;
    /// True when `whenUtc` belongs to tomorrow, so the caption can say so. A
    /// bare "06:14" on a Tuesday evening reads as this morning, which has been
    /// and gone.
    bool      nextDay  = false;
};

/**
 * @brief Pick the event to show.
 *
 * @param today    The local day containing @p nowUtc.
 * @param tomorrow The one after it, consulted only once today's sun has set.
 */
Next nextEvent(int64_t nowUtc, const Day &today, const Day &tomorrow);

/**
 * @brief Does the watch's time zone belong to the position it is computing for?
 *
 * The check that keeps a configured home position honest. Sunrise is computed
 * from a latitude and longitude somebody typed in once, and rendered through
 * whatever time zone the watch is in now. Fly a few zones east and those two
 * stop describing the same place: the times are still right for home, and the
 * clock they are drawn against is not, so the glance is confidently hours wrong
 * while looking perfectly healthy. Nothing else in the app can notice this --
 * every individual part is behaving.
 *
 * Solar time and civil time genuinely differ by a lot in places: western China
 * runs about three hours ahead of its sun, Spain about two. The default
 * tolerance sits above all of them, so this fires on travel and not on
 * geography. Wrapped to the shorter way round the globe first, or the Line
 * Islands -- UTC+14 at 157 west -- would look like a 24-hour error.
 *
 * @return true when the zone is plausible for the longitude.
 */
bool zoneAgreesWithLongitude(double lonDeg, int32_t utcOffsetSec,
                             double toleranceHours = 4.0);

} // namespace Sun

#endif // SCHEDULE_HPP
