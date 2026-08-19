/**
 ******************************************************************************
 * @file    Schedule.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Which sun event to put on the glance, and when not to trust it.
 ******************************************************************************
 *
 * A glance answers one question. For the sun that question is "when is it next
 * light, and when is it next dark" -- so what comes out of here is the next
 * *two* events, in the order they will happen, and never an event that has
 * already been. "Today's sunrise" at nine in the evening is a fact about the
 * past; the screen has two rows and both of them should be worth looking at.
 *
 * The two always alternate, so the second's kind is the first's opposite and is
 * not carried separately. The policy of choosing them out of a pair of days is
 * its own file, tested on its own, and kept out of both the astronomy and the
 * drawing.
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

/// The next two things the sun will do.
struct Next
{
    /// Which of the two the first one is. The second is the other kind.
    EventKind kind      = EventKind::Rise;
    int64_t   whenUtc   = -1;
    /// The event after that. -1 when there is not one to show: the day after a
    /// last sunset before the midnight sun has neither a sunrise nor a sunset,
    /// and a second row is better empty than invented.
    int64_t   secondUtc = -1;
    /// True when `whenUtc` belongs to tomorrow, so the caption can say so. A
    /// bare "06:14" on a Tuesday evening reads as this morning, which has been
    /// and gone.
    bool      nextDay   = false;
};

/**
 * @brief The next two events, in the order they happen.
 *
 * @param today    The local day containing @p nowUtc.
 * @param tomorrow The one after it. Needed even in the middle of the afternoon,
 *                 because the sunset in front of you is followed by tomorrow's
 *                 sunrise and both belong on the screen.
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
