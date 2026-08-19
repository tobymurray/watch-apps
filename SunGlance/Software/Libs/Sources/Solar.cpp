/**
 ******************************************************************************
 * @file    Solar.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   NOAA's solar position calculation, and nothing else.
 ******************************************************************************
 */

#include "Solar.hpp"

#include <cmath>

namespace Sun
{

namespace
{

constexpr double kPi         = 3.14159265358979323846;
constexpr double kSecPerDay  = 86400.0;
/// Seconds of time per degree of hour angle: 24 h / 360 deg.
constexpr double kSecPerDeg  = 240.0;
/// Julian Date of the Unix epoch. JD counts from noon, hence the half day.
constexpr double kJdAtEpoch  = 2440587.5;
/// JD of J2000.0, the epoch the polynomials below are fitted about.
constexpr double kJdJ2000    = 2451545.0;
constexpr double kDaysPerJulianCentury = 36525.0;

double rad(double deg) { return deg * kPi / 180.0; }
double deg(double rd)  { return rd * 180.0 / kPi; }

/// Wrap into [0, 360). `std::fmod` keeps the sign of its first argument, which
/// for a mean longitude that has gone negative would put every later term in
/// the wrong quadrant.
double norm360(double d)
{
    d = std::fmod(d, 360.0);
    return d < 0.0 ? d + 360.0 : d;
}

/// Julian centuries since J2000.0 at a UTC instant.
///
/// UTC is used where the algorithm wants Terrestrial Time, which it currently
/// leads by about 69 seconds. That difference moves the sun by 0.0003 degrees
/// -- under two seconds of sunrise -- so it is dropped rather than tracked
/// through a leap-second table this watch has no way to keep current.
double centuryAt(double utcSeconds)
{
    const double jd = utcSeconds / kSecPerDay + kJdAtEpoch;
    return (jd - kJdJ2000) / kDaysPerJulianCentury;
}

/// The two quantities the whole calculation turns on, at Julian century @p t.
struct Terms
{
    double eqTimeMin;  ///< Apparent solar time minus mean solar time, minutes.
    double declDeg;    ///< Declination of the sun.
};

Terms terms(double t)
{
    // Geometric mean longitude and mean anomaly of the sun.
    const double l0 = norm360(280.46646 + t * (36000.76983 + t * 0.0003032));
    const double m  = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    // Eccentricity of Earth's orbit -- dimensionless, and shrinking.
    const double e  = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);

    // Equation of centre: the correction from the fictitious mean sun, which
    // moves uniformly, to the real one, which does not.
    const double c = std::sin(rad(m))       * (1.914602 - t * (0.004817 + 0.000014 * t))
                   + std::sin(rad(2.0 * m)) * (0.019993 - 0.000101 * t)
                   + std::sin(rad(3.0 * m)) * 0.000289;

    // Apparent longitude: true longitude less aberration and the nutation term
    // in longitude, both driven by the moon's ascending node.
    const double omega   = 125.04 - 1934.136 * t;
    const double appLong = l0 + c - 0.00569 - 0.00478 * std::sin(rad(omega));

    // Obliquity of the ecliptic, with the same node's correction.
    const double meanObliq = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
    const double obliq     = meanObliq + 0.00256 * std::cos(rad(omega));

    Terms out {};
    out.declDeg = deg(std::asin(std::sin(rad(obliq)) * std::sin(rad(appLong))));

    const double y = std::tan(rad(obliq / 2.0)) * std::tan(rad(obliq / 2.0));
    out.eqTimeMin = 4.0 * deg(y * std::sin(2.0 * rad(l0))
                            - 2.0 * e * std::sin(rad(m))
                            + 4.0 * e * y * std::sin(rad(m)) * std::cos(2.0 * rad(l0))
                            - 0.5 * y * y * std::sin(4.0 * rad(l0))
                            - 1.25 * e * e * std::sin(2.0 * rad(m)));
    return out;
}

/// Half the length of the day, expressed as an angle: how far either side of
/// the transit the sun sits at @p altDeg.
///
/// Returns false when there is no such angle, which is the polar case rather
/// than an error; @p aboveOut then says which side of the horizon the sun spent
/// the whole day on.
bool hourAngleDeg(double latDeg, double declDeg, double altDeg,
                  double &haOut, bool &aboveOut)
{
    const double lat  = rad(latDeg);
    const double decl = rad(declDeg);
    const double cosH = (std::sin(rad(altDeg)) - std::sin(lat) * std::sin(decl))
                      / (std::cos(lat) * std::cos(decl));

    if (cosH < -1.0) {
        aboveOut = true;   // the horizon is never reached going down
        return false;
    }
    if (cosH > 1.0) {
        aboveOut = false;  // nor going up
        return false;
    }

    haOut = deg(std::acos(cosH));
    return true;
}

} // namespace

int64_t daysFromCivil(int year, int month, int day)
{
    year -= month <= 2;
    const int64_t  era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t days, int &year, int &month, int &day)
{
    days += 719468;
    const int64_t  era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5 * doy + 2) / 153;
    const unsigned d   = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m   = mp + (mp < 10 ? 3 : static_cast<unsigned>(-9));

    year  = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0));
    month = static_cast<int>(m);
    day   = static_cast<int>(d);
}

int32_t utcOffsetFromCivil(int year, int month, int day,
                           int hour, int minute, int second, int64_t utc)
{
    const int64_t local = daysFromCivil(year, month, day) * 86400
                        + static_cast<int64_t>(hour) * 3600
                        + static_cast<int64_t>(minute) * 60
                        + second;
    return static_cast<int32_t>(local - utc);
}

Day forLocalDay(int year, int month, int day, int32_t utcOffsetSec, Position pos,
                double altitudeDeg)
{
    Day out;

    // Local noon of the requested day, as a UTC instant. Everything below is
    // anchored to this rather than to midnight: midnight is where the day
    // boundary is and therefore where an hour of DST or a degree of longitude
    // can push the anchor across into a neighbouring solar day. Noon is the
    // furthest point from both boundaries.
    const double localNoon = static_cast<double>(daysFromCivil(year, month, day)) * kSecPerDay
                           + 43200.0 - static_cast<double>(utcOffsetSec);

    // Mean solar noon at this longitude, for the solar day containing it. Local
    // apparent time runs ahead of UTC by 4 minutes per degree east.
    const double lonSec   = pos.lonDeg * kSecPerDeg;
    const double solarDay = std::floor((localNoon + lonSec) / kSecPerDay);
    const double meanNoon = solarDay * kSecPerDay + 43200.0 - lonSec;

    // The transit itself: mean noon corrected by the equation of time, which is
    // evaluated at the transit, so it is computed twice. The first pass is
    // already within a second of the second; the second costs nothing and
    // removes the question.
    double transit = meanNoon - terms(centuryAt(meanNoon)).eqTimeMin * 60.0;
    transit        = meanNoon - terms(centuryAt(transit)).eqTimeMin * 60.0;
    out.noonUtc    = static_cast<int64_t>(std::llround(transit));

    const Terms noonTerms = terms(centuryAt(transit));

    double ha    = 0.0;
    bool   above = false;
    if (!hourAngleDeg(pos.latDeg, noonTerms.declDeg, altitudeDeg, ha, above)) {
        // The day's verdict is taken at the transit and nowhere else. Asking
        // again at an estimated rise time -- which does not exist -- is how an
        // implementation ends up reporting a sunrise on a day the sun never
        // came up.
        out.kind = above ? DayKind::AlwaysUp : DayKind::AlwaysDown;
        return out;
    }

    // One refinement per event. The declination and the equation of time are
    // evaluated at the transit above, but the events are hours away from it and
    // both quantities are still moving; recomputing them at each estimate is
    // NOAA's own iteration and is worth up to a minute at high latitudes.
    //
    // A refinement that degenerates -- the hour angle exists at noon but not at
    // the estimate, which can happen within a day or two of the polar
    // transitions -- keeps the unrefined estimate rather than discarding an
    // event the day is known to have.
    const auto refine = [&](double estimate, int sign) -> int64_t {
        const Terms t = terms(centuryAt(estimate));
        double haR    = 0.0;
        bool   aboveR = false;
        if (!hourAngleDeg(pos.latDeg, t.declDeg, altitudeDeg, haR, aboveR)) {
            return static_cast<int64_t>(std::llround(estimate));
        }
        const double transitR = meanNoon - t.eqTimeMin * 60.0;
        return static_cast<int64_t>(std::llround(transitR + sign * haR * kSecPerDeg));
    };

    out.riseUtc = refine(transit - ha * kSecPerDeg, -1);
    out.setUtc  = refine(transit + ha * kSecPerDeg, +1);
    return out;
}

} // namespace Sun
