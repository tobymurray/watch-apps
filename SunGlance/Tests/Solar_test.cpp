/**
 * Host tests for the solar core.
 *
 * Two kinds of test, and the difference matters:
 *
 *   - **Fixtures**, checked against `astral` 3.2 -- an independent Python
 *     implementation, not a rearrangement of the code under test. These are the
 *     only evidence here that the numbers are *right* rather than merely
 *     self-consistent. `Tests/README.md` has the generator and how to re-run it.
 *   - **Invariants**, which no reference is needed for: symmetry about the
 *     transit, twelve hours of daylight at an equinox, four minutes per degree
 *     of longitude, and the day boundaries. These are what actually fail when
 *     somebody edits the algorithm, because they fail loudly and locally.
 *
 * The fixture tolerances are per latitude band and are the *observed* spread
 * against astral, rounded up, not a figure copied out of a paper. Near the
 * poles the sun approaches the horizon at a shallow angle, so a hundredth of a
 * degree of disagreement about its altitude is minutes of disagreement about
 * when it gets there; that is a property of the geometry rather than a defect
 * in either implementation, and pretending to a tighter bound there would be
 * the dishonest option.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "Solar.hpp"

namespace {

struct NormalCase {
    const char *place;
    double      lat;
    double      lon;
    int32_t     offsetSec;
    int         year;
    int         month;
    int         day;
    int64_t     riseUtc;
    int64_t     setUtc;
};

// Reference values from astral 3.2, asked for the same local calendar day in a
// fixed-offset zone. Zones are the whole hour nearest the longitude rather than
// the real civil zone: a fixture should not also be a test of somebody's DST
// database, and the real zone is exercised by the offset arithmetic instead.
constexpr NormalCase kNormal[] = {
    { "Ottawa",       45.4215,  -75.6972, -18000, 2026,  3, 20, 1774004758, 1774048509 },
    { "Ottawa",       45.4215,  -75.6972, -18000, 2026,  6, 21, 1782033281, 1782089676 },
    { "Ottawa",       45.4215,  -75.6972, -18000, 2026,  9, 23, 1790160683, 1790204280 },
    { "Ottawa",       45.4215,  -75.6972, -18000, 2026, 12, 21, 1797856791, 1797888125 },
    { "Ottawa",       45.4215,  -75.6972, -18000, 2026,  1, 15, 1768480741, 1768513552 },
    { "Ottawa",       45.4215,  -75.6972, -18000, 2026,  8, 18, 1787047657, 1787097885 },
    { "London",       51.5074,   -0.1278,      0, 2026,  3, 20, 1773986619, 1774030394 },
    { "London",       51.5074,   -0.1278,      0, 2026,  6, 21, 1782013407, 1782073272 },
    { "London",       51.5074,   -0.1278,      0, 2026,  9, 23, 1790142502, 1790186185 },
    { "London",       51.5074,   -0.1278,      0, 2026, 12, 21, 1797840246, 1797868383 },
    { "London",       51.5074,   -0.1278,      0, 2026,  1, 15, 1768463987, 1768494028 },
    { "London",       51.5074,   -0.1278,      0, 2026,  8, 18, 1787028657, 1787080601 },
    { "Null Island",   0.0000,    0.0000,      0, 2026,  3, 20, 1773986660, 1774030230 },
    { "Null Island",   0.0000,    0.0000,      0, 2026,  6, 21, 1782021499, 1782065119 },
    { "Null Island",   0.0000,    0.0000,      0, 2026,  9, 23, 1790142559, 1790186127 },
    { "Null Island",   0.0000,    0.0000,      0, 2026, 12, 21, 1797832470, 1797876098 },
    { "Null Island",   0.0000,    0.0000,      0, 2026,  1, 15, 1768457156, 1768500772 },
    { "Null Island",   0.0000,    0.0000,      0, 2026,  8, 18, 1787032840, 1787076422 },
    { "Sydney",      -33.8688,  151.2093,  36000, 2026,  3, 20, 1773950289, 1773994003 },
    { "Sydney",      -33.8688,  151.2093,  36000, 2026,  6, 21, 1781989211, 1782024815 },
    { "Sydney",      -33.8688,  151.2093,  36000, 2026,  9, 23, 1790106250, 1790149904 },
    { "Sydney",      -33.8688,  151.2093,  36000, 2026, 12, 21, 1797792053, 1797843911 },
    { "Sydney",      -33.8688,  151.2093,  36000, 2026,  1, 15, 1768417183, 1768468125 },
    { "Sydney",      -33.8688,  151.2093,  36000, 2026,  8, 18, 1786998687, 1787038031 },
    { "Reykjavik",    64.1466,  -21.9426,  -3600, 2026,  3, 20, 1773991739, 1774035782 },
    { "Reykjavik",    64.1466,  -21.9426,  -3600, 2026,  6, 21, 1782010580, 1782086570 },
    { "Reykjavik",    64.1466,  -21.9426,  -3600, 2026,  9, 23, 1790147635, 1790191482 },
    { "Reykjavik",    64.1466,  -21.9426,  -3600, 2026, 12, 21, 1797852191, 1797866913 },
    { "Reykjavik",    64.1466,  -21.9426,  -3600, 2026,  1, 15, 1768474532, 1768493967 },
    { "Reykjavik",    64.1466,  -21.9426,  -3600, 2026,  8, 18, 1787030909, 1787088752 },
    { "Quito",        -0.1807,  -78.4678, -18000, 2026,  3, 20, 1774005489, 1774049059 },
    { "Quito",        -0.1807,  -78.4678, -18000, 2026,  6, 21, 1782040353, 1782083935 },
    { "Quito",        -0.1807,  -78.4678, -18000, 2026,  9, 23, 1790161386, 1790204955 },
    { "Quito",        -0.1807,  -78.4678, -18000, 2026, 12, 21, 1797851290, 1797894956 },
    { "Quito",        -0.1807,  -78.4678, -18000, 2026,  1, 15, 1768475976, 1768519626 },
    { "Quito",        -0.1807,  -78.4678, -18000, 2026,  8, 18, 1787051680, 1787095242 },
    { "Ushuaia",     -54.8019,  -68.3030, -18000, 2026,  3, 20, 1774002888, 1774046713 },
    { "Ushuaia",     -54.8019,  -68.3030, -18000, 2026,  6, 21, 1782046755, 1782072653 },
    { "Ushuaia",     -54.8019,  -68.3030, -18000, 2026,  9, 23, 1790158752, 1790202778 },
    { "Ushuaia",     -54.8019,  -68.3030, -18000, 2026, 12, 21, 1797839511, 1797901855 },
    { "Ushuaia",     -54.8019,  -68.3030, -18000, 2026,  1, 15, 1768465371, 1768525288 },
    { "Ushuaia",     -54.8019,  -68.3030, -18000, 2026,  8, 18, 1787053654, 1787088438 },
    { "Kashgar",      39.4704,   75.9898,  18000, 2026,  3, 20, 1773968415, 1774012048 },
    { "Kashgar",      39.4704,   75.9898,  18000, 2026,  6, 21, 1781998157, 1782051980 },
    { "Kashgar",      39.4704,   75.9898,  18000, 2026,  9, 23, 1790124272, 1790167909 },
    { "Kashgar",      39.4704,   75.9898,  18000, 2026, 12, 21, 1797819168, 1797852912 },
    { "Kashgar",      39.4704,   75.9898,  18000, 2026,  1, 15, 1768443298, 1768478164 },
    { "Kashgar",      39.4704,   75.9898,  18000, 2026,  8, 18, 1787011880, 1787060874 },
    { "Tromso",       69.6492,   18.9553,   3600, 2026,  3, 20, 1773981862, 1774026062 },
    { "Tromso",       69.6492,   18.9553,   3600, 2026,  9, 23, 1790137701, 1790181760 },
    { "Longyearbyen", 78.2232,   15.6267,   3600, 2026,  3, 20, 1773982356, 1774027270 },
    { "Longyearbyen", 78.2232,   15.6267,   3600, 2026,  9, 23, 1790138153, 1790182800 },
    // Deliberately absent: Tromso on 2026-08-18, where astral returns exactly
    // 02:00:00 UTC for sunrise -- a round number to the second, which no real
    // sunrise is. It is astral falling back to an interval search there, not a
    // computed crossing, so it is not a reference value. The invariant tests
    // below cover that latitude and season instead.
};

struct PolarCase {
    const char   *place;
    double        lat;
    double        lon;
    int32_t       offsetSec;
    int           year;
    int           month;
    int           day;
    Sun::DayKind  kind;
};

constexpr PolarCase kPolar[] = {
    { "Tromso",       69.6492,   18.9553,   3600, 2026,  1,  5, Sun::DayKind::AlwaysDown },
    { "Tromso",       69.6492,   18.9553,   3600, 2026,  6, 10, Sun::DayKind::AlwaysUp },
    { "Tromso",       69.6492,   18.9553,   3600, 2026, 12, 21, Sun::DayKind::AlwaysDown },
    { "Longyearbyen", 78.2232,   15.6267,   3600, 2026,  1,  5, Sun::DayKind::AlwaysDown },
    { "Longyearbyen", 78.2232,   15.6267,   3600, 2026,  6, 10, Sun::DayKind::AlwaysUp },
    { "Longyearbyen", 78.2232,   15.6267,   3600, 2026,  8, 10, Sun::DayKind::AlwaysUp },
    { "Longyearbyen", 78.2232,   15.6267,   3600, 2026, 11, 20, Sun::DayKind::AlwaysDown },
    { "Alert",        82.5018,  -62.3481, -14400, 2026, 12, 21, Sun::DayKind::AlwaysDown },
    { "McMurdo",     -77.8419,  166.6863,  39600, 2026,  6, 21, Sun::DayKind::AlwaysDown },
    { "McMurdo",     -77.8419,  166.6863,  39600, 2026, 12, 21, Sun::DayKind::AlwaysUp },
    // Every date here is comfortably inside its polar period. The *transition*
    // days are deliberately absent: on the day the polar night ends the sun's
    // greatest altitude is within a hundredth of a degree of the -0.833 that
    // defines sunrise, so which side of it a given implementation lands on is
    // not a fact about the sky. A year-long sweep of five polar sites (1825
    // days) put this code and astral on the same verdict every day except ten,
    // and all ten were a single transition day. Tests/README.md has the sweep.
};


/// Observed spread against astral, with room to breathe. Over 107 place-days
/// the worst disagreement was 26 s below 55 degrees, 72 s to 66, and 118 s
/// beyond -- see the file comment for why that last band is wide and
/// Tests/README.md for the measurement.
int64_t toleranceSec(double latDeg)
{
    const double lat = std::fabs(latDeg);
    if (lat <= 55.0) { return 45;  }
    if (lat <= 66.0) { return 90;  }
    return 180;
}

Sun::Day dayOf(const NormalCase &c)
{
    return Sun::forLocalDay(c.year, c.month, c.day, c.offsetSec,
                            Sun::Position { c.lat, c.lon });
}

TEST(Solar, MatchesAnIndependentImplementation)
{
    for (const NormalCase &c : kNormal) {
        const Sun::Day day = dayOf(c);
        const int64_t  tol = toleranceSec(c.lat);

        ASSERT_EQ(day.kind, Sun::DayKind::Normal) << c.place << " " << c.year << "-" << c.month << "-" << c.day;
        EXPECT_LE(std::llabs(day.riseUtc - c.riseUtc), tol)
            << c.place << " sunrise " << c.year << "-" << c.month << "-" << c.day
            << ": got " << day.riseUtc << ", astral " << c.riseUtc;
        EXPECT_LE(std::llabs(day.setUtc - c.setUtc), tol)
            << c.place << " sunset " << c.year << "-" << c.month << "-" << c.day
            << ": got " << day.setUtc << ", astral " << c.setUtc;
    }
}

TEST(Solar, PolarDaysAreAStateAndNotATime)
{
    for (const PolarCase &c : kPolar) {
        const Sun::Day day = Sun::forLocalDay(c.year, c.month, c.day, c.offsetSec,
                                              Sun::Position { c.lat, c.lon });
        EXPECT_EQ(day.kind, c.kind) << c.place << " " << c.year << "-" << c.month << "-" << c.day;
        // The times are not merely wrong to use: they are not there to be used.
        EXPECT_EQ(day.riseUtc, -1);
        EXPECT_EQ(day.setUtc, -1);
        // The transit still exists, and is what the verdict was taken at.
        EXPECT_GT(day.noonUtc, 0);
    }
}

TEST(Solar, EventsStraddleTheTransitSymmetrically)
{
    for (const NormalCase &c : kNormal) {
        const Sun::Day day = dayOf(c);
        ASSERT_EQ(day.kind, Sun::DayKind::Normal);
        EXPECT_LT(day.riseUtc, day.noonUtc) << c.place;
        EXPECT_LT(day.noonUtc, day.setUtc) << c.place;

        // Not exactly symmetric, and the asymmetry is a fact rather than an
        // error: the declination moves by up to 0.4 degrees between the
        // morning and the evening crossing, so the two half-days differ. How
        // much that is worth in time depends on how steeply the sun meets the
        // horizon, which is why the allowance widens with latitude in step
        // with the fixture tolerances -- 4 minutes at Longyearbyen, half a
        // minute at the equator.
        const int64_t before = day.noonUtc - day.riseUtc;
        const int64_t after  = day.setUtc - day.noonUtc;
        EXPECT_LE(std::llabs(before - after), toleranceSec(c.lat) * 2) << c.place;
    }
}

TEST(Solar, DaylightIsTwelveHoursAtAnEquinox)
{
    // The half degree of refraction and the sun's own radius buy a few extra
    // minutes everywhere, and more of them the further from the equator, since
    // the sun crosses the horizon at a shallower angle. Twelve hours plus a bit
    // is right; twelve hours exactly would mean the altitude constant had been
    // dropped.
    const struct { double lat; int64_t maxExtraSec; } cases[] = {
        {   0.0,  9 * 60 },
        {  45.0, 15 * 60 },
        { -45.0, 15 * 60 },
        {  60.0, 25 * 60 },
    };

    for (const auto &c : cases) {
        const Sun::Day day = Sun::forLocalDay(2026, 3, 20, 0, Sun::Position { c.lat, 0.0 });
        ASSERT_EQ(day.kind, Sun::DayKind::Normal) << c.lat;
        const int64_t daylight = day.setUtc - day.riseUtc;
        EXPECT_GT(daylight, 12 * 3600) << c.lat;
        EXPECT_LT(daylight, 12 * 3600 + c.maxExtraSec) << c.lat;
    }
}

TEST(Solar, SunriseMovesFourMinutesPerDegreeOfLongitude)
{
    // The one relationship a user can check from the passenger seat, and the
    // one a sign error in the longitude breaks first.
    const Sun::Day here = Sun::forLocalDay(2026, 8, 18, 0, Sun::Position { 45.0, 0.0 });
    const Sun::Day east = Sun::forLocalDay(2026, 8, 18, 0, Sun::Position { 45.0, 1.0 });
    const Sun::Day west = Sun::forLocalDay(2026, 8, 18, 0, Sun::Position { 45.0, -1.0 });

    EXPECT_NEAR(static_cast<double>(here.riseUtc - east.riseUtc), 240.0, 5.0);
    EXPECT_NEAR(static_cast<double>(west.riseUtc - here.riseUtc), 240.0, 5.0);
}

TEST(Solar, TheDayAskedForIsTheLocalOne)
{
    // Ottawa in summer: sunset is after 20:00 local, which is past midnight UTC.
    // Asking for 18 August must return that evening's sunset and not the one 24
    // hours away -- the failure SleepLab's local-day index exists to prevent.
    const int32_t  edt = -4 * 3600;
    const Sun::Day day = Sun::forLocalDay(2026, 8, 18, edt, Sun::Position { 45.4215, -75.6972 });

    ASSERT_EQ(day.kind, Sun::DayKind::Normal);

    const int64_t localMidnight = Sun::daysFromCivil(2026, 8, 18) * 86400 - edt;
    EXPECT_GE(day.riseUtc, localMidnight);
    EXPECT_LT(day.setUtc, localMidnight + 86400);
    // And the sunset really is on the next UTC day, which is the whole point.
    EXPECT_GT(day.setUtc, Sun::daysFromCivil(2026, 8, 19) * 86400);
}

TEST(Solar, TheZoneChoosesTheDayAndNotTheAstronomy)
{
    // Same place, same instant of interest, described by two watches whose
    // zones differ by an hour: the sun does not move.
    const Sun::Position berlin { 52.52, 13.405 };
    const Sun::Day cet  = Sun::forLocalDay(2026, 8, 18, 1 * 3600, berlin);
    const Sun::Day cest = Sun::forLocalDay(2026, 8, 18, 2 * 3600, berlin);

    EXPECT_EQ(cet.riseUtc, cest.riseUtc);
    EXPECT_EQ(cet.setUtc, cest.setUtc);
}

TEST(Solar, ATimeZoneThatDisagreesWithItsLongitudeStillGetsItsOwnDay)
{
    // Kashgar: 76 east, five hours of sun ahead of UTC, but the civil zone is
    // Beijing's +8. The times must still be that local day's, not the previous
    // solar day's.
    const Sun::Day day = Sun::forLocalDay(2026, 8, 18, 8 * 3600,
                                          Sun::Position { 39.4704, 75.9898 });
    ASSERT_EQ(day.kind, Sun::DayKind::Normal);

    const int64_t localMidnight = Sun::daysFromCivil(2026, 8, 18) * 86400 - 8 * 3600;
    EXPECT_GE(day.riseUtc, localMidnight);
    EXPECT_LT(day.setUtc, localMidnight + 86400);
}

TEST(Solar, CivilTwilightIsBeforeSunriseAndAfterSunset)
{
    const Sun::Position london { 51.5074, -0.1278 };
    const Sun::Day sun      = Sun::forLocalDay(2026, 8, 18, 0, london);
    const Sun::Day twilight = Sun::forLocalDay(2026, 8, 18, 0, london, -6.0);

    ASSERT_EQ(twilight.kind, Sun::DayKind::Normal);
    EXPECT_LT(twilight.riseUtc, sun.riseUtc);
    EXPECT_GT(twilight.setUtc, sun.setUtc);
    // Half an hour or so at this latitude in August, not three minutes and not
    // three hours.
    EXPECT_GT(sun.riseUtc - twilight.riseUtc, 20 * 60);
    EXPECT_LT(sun.riseUtc - twilight.riseUtc, 60 * 60);
}

TEST(Solar, CivilCalendarRoundTrips)
{
    EXPECT_EQ(Sun::daysFromCivil(1970, 1, 1), 0);
    EXPECT_EQ(Sun::daysFromCivil(2026, 8, 18), 20683);
    EXPECT_EQ(Sun::daysFromCivil(2000, 2, 29) - Sun::daysFromCivil(2000, 2, 28), 1);
    EXPECT_EQ(Sun::daysFromCivil(1900, 3, 1) - Sun::daysFromCivil(1900, 2, 28), 1);  // not a leap year

    for (int64_t d = Sun::daysFromCivil(2020, 1, 1); d <= Sun::daysFromCivil(2030, 12, 31); d++) {
        int y = 0, m = 0, dd = 0;
        Sun::civilFromDays(d, y, m, dd);
        ASSERT_EQ(Sun::daysFromCivil(y, m, dd), d);
    }
}

TEST(Solar, OffsetComesOutOfWhatTheLocalClockSays)
{
    // 2026-08-18 12:00:00 UTC, read by a watch five hours behind.
    const int64_t utc = Sun::daysFromCivil(2026, 8, 18) * 86400 + 12 * 3600;
    EXPECT_EQ(Sun::utcOffsetFromCivil(2026, 8, 18, 7, 0, 0, utc), -5 * 3600);
    EXPECT_EQ(Sun::utcOffsetFromCivil(2026, 8, 18, 12, 0, 0, utc), 0);
    // A zone far enough east that the local reading is already tomorrow.
    EXPECT_EQ(Sun::utcOffsetFromCivil(2026, 8, 19, 1, 0, 0, utc), 13 * 3600);
    // And the half-hour zones, which are the ones an integer-hour assumption
    // quietly breaks.
    EXPECT_EQ(Sun::utcOffsetFromCivil(2026, 8, 18, 17, 30, 0, utc), 5 * 3600 + 1800);
}

} // namespace
