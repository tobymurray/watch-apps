/**
 ******************************************************************************
 * @file    Schedule.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Which sun event to put on the glance, and when not to trust it.
 ******************************************************************************
 */

#include "Schedule.hpp"

#include <cmath>

namespace Sun
{

namespace
{

/// The polar days carry no instant, so they are reported as themselves.
Next polar(DayKind kind, bool nextDay)
{
    Next out;
    out.kind    = (kind == DayKind::AlwaysUp) ? EventKind::MidnightSun : EventKind::PolarNight;
    out.nextDay = nextDay;
    return out;
}

} // namespace

Next nextEvent(int64_t nowUtc, const Day &today, const Day &tomorrow)
{
    if (today.kind != DayKind::Normal) {
        return polar(today.kind, false);
    }

    if (nowUtc < today.riseUtc) {
        Next out;
        out.kind     = EventKind::Rise;
        out.whenUtc  = today.riseUtc;
        out.otherUtc = today.setUtc;
        return out;
    }

    if (nowUtc < today.setUtc) {
        Next out;
        out.kind     = EventKind::Set;
        out.whenUtc  = today.setUtc;
        out.otherUtc = today.riseUtc;
        return out;
    }

    // The sun is down for the night. What comes next belongs to tomorrow, and
    // tomorrow may not have a sunrise at all -- north of the Arctic circle the
    // last sunset of the autumn is followed by a day that has neither.
    if (tomorrow.kind != DayKind::Normal) {
        return polar(tomorrow.kind, true);
    }

    Next out;
    out.kind    = EventKind::Rise;
    out.whenUtc = tomorrow.riseUtc;
    // Deliberately not tomorrow's sunset. Pairing "sunrise 06:14" with a sunset
    // 20 hours away says nothing anybody wants at 11pm, and the caption's one
    // line is better spent saying which day this is.
    out.nextDay = true;
    return out;
}

bool zoneAgreesWithLongitude(double lonDeg, int32_t utcOffsetSec, double toleranceHours)
{
    const double solarHours = lonDeg / 15.0;
    const double zoneHours  = static_cast<double>(utcOffsetSec) / 3600.0;

    double diff = zoneHours - solarHours;
    // The shorter way round: a 23-hour disagreement is a 1-hour disagreement
    // across the date line.
    diff = std::fmod(diff + 12.0, 24.0);
    if (diff < 0.0) {
        diff += 24.0;
    }
    diff -= 12.0;

    return std::fabs(diff) <= toleranceHours;
}

} // namespace Sun
