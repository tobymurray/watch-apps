/**
 * Host tests for the "what next" policy.
 *
 * The days here are made up rather than computed: this is the file that decides
 * what a glance says, and it should fail when that decision changes, not when
 * the astronomy is refined by four seconds. Real days go through Solar_test.
 */

#include <gtest/gtest.h>

#include "Schedule.hpp"

namespace {

constexpr int64_t kMidnight = 1787011200;          // 2026-08-18 00:00:00 UTC
constexpr int64_t kRise     = kMidnight + 6 * 3600;
constexpr int64_t kSet      = kMidnight + 20 * 3600;

Sun::Day normalDay(int64_t rise, int64_t set)
{
    Sun::Day d;
    d.kind    = Sun::DayKind::Normal;
    d.riseUtc = rise;
    d.setUtc  = set;
    d.noonUtc = (rise + set) / 2;
    return d;
}

Sun::Day polarDay(Sun::DayKind kind)
{
    Sun::Day d;
    d.kind    = kind;
    d.noonUtc = kMidnight + 12 * 3600;
    return d;
}

const Sun::Day kToday    = normalDay(kRise, kSet);
const Sun::Day kTomorrow = normalDay(kRise + 86400 + 120, kSet + 86400 - 120);

TEST(Schedule, BeforeSunriseBothOfTodaysEventsAreAhead)
{
    const Sun::Next n = Sun::nextEvent(kMidnight + 5 * 3600, kToday, kTomorrow);
    EXPECT_EQ(n.kind, Sun::EventKind::Rise);
    EXPECT_EQ(n.whenUtc, kRise);
    EXPECT_EQ(n.secondUtc, kSet);
    EXPECT_FALSE(n.nextDay);
}

TEST(Schedule, DuringTheDayTheSecondEventIsTomorrowsSunrise)
{
    const Sun::Next n = Sun::nextEvent(kMidnight + 12 * 3600, kToday, kTomorrow);
    EXPECT_EQ(n.kind, Sun::EventKind::Set);
    EXPECT_EQ(n.whenUtc, kSet);
    // Not this morning's sunrise, which has been and gone. Both rows on the
    // screen are things that have not happened yet.
    EXPECT_EQ(n.secondUtc, kTomorrow.riseUtc);
    EXPECT_FALSE(n.nextDay);
}

TEST(Schedule, AfterSunsetBothEventsAreTomorrows)
{
    const Sun::Next n = Sun::nextEvent(kSet + 1, kToday, kTomorrow);
    EXPECT_EQ(n.kind, Sun::EventKind::Rise);
    EXPECT_EQ(n.whenUtc, kTomorrow.riseUtc);
    EXPECT_EQ(n.secondUtc, kTomorrow.setUtc);
    EXPECT_TRUE(n.nextDay);
}

TEST(Schedule, NothingReturnedIsEverInThePast)
{
    // The property the whole file is really about, checked across a day rather
    // than at the three instants the cases above happen to pick.
    for (int64_t now = kMidnight; now < kMidnight + 86400; now += 137) {
        const Sun::Next n = Sun::nextEvent(now, kToday, kTomorrow);
        ASSERT_GT(n.whenUtc, now) << now;
        if (n.secondUtc >= 0) {
            ASSERT_GT(n.secondUtc, n.whenUtc) << now;
        }
    }
}

TEST(Schedule, TheLastSunsetBeforeTheMidnightSunHasNoSecondEvent)
{
    // Today's sunset happens; tomorrow the sun never comes back down, so it
    // never comes up either. There is no second row to draw, and inventing one
    // would mean showing a sunrise that is not coming.
    const Sun::Next n = Sun::nextEvent(kMidnight + 12 * 3600, kToday,
                                       polarDay(Sun::DayKind::AlwaysUp));
    EXPECT_EQ(n.kind, Sun::EventKind::Set);
    EXPECT_EQ(n.whenUtc, kSet);
    EXPECT_EQ(n.secondUtc, -1);
}

TEST(Schedule, TheEventItselfHasNotHappenedYet)
{
    // Exactly at sunrise the sun is still coming up, so sunrise is still the
    // answer; a second later the answer is sunset. An off-by-one here is a
    // glance that flickers to the wrong event for one minute a day, which is
    // the kind of thing nobody ever catches on hardware.
    EXPECT_EQ(Sun::nextEvent(kRise - 1, kToday, kTomorrow).kind, Sun::EventKind::Rise);
    EXPECT_EQ(Sun::nextEvent(kRise, kToday, kTomorrow).kind, Sun::EventKind::Set);
    EXPECT_EQ(Sun::nextEvent(kSet - 1, kToday, kTomorrow).kind, Sun::EventKind::Set);
    EXPECT_EQ(Sun::nextEvent(kSet, kToday, kTomorrow).kind, Sun::EventKind::Rise);
}

TEST(Schedule, MidnightSunIsReportedWheneverYouLook)
{
    const Sun::Day today = polarDay(Sun::DayKind::AlwaysUp);
    for (int hour = 0; hour < 24; hour++) {
        const Sun::Next n = Sun::nextEvent(kMidnight + hour * 3600, today, kTomorrow);
        EXPECT_EQ(n.kind, Sun::EventKind::MidnightSun) << hour;
        EXPECT_EQ(n.whenUtc, -1) << hour;
    }
}

TEST(Schedule, PolarNightIsReportedWheneverYouLook)
{
    const Sun::Day today = polarDay(Sun::DayKind::AlwaysDown);
    for (int hour = 0; hour < 24; hour++) {
        const Sun::Next n = Sun::nextEvent(kMidnight + hour * 3600, today, kTomorrow);
        EXPECT_EQ(n.kind, Sun::EventKind::PolarNight) << hour;
    }
}

TEST(Schedule, TheLastSunsetOfTheAutumnIsFollowedByNoSunrise)
{
    // North of the Arctic circle the sun sets one afternoon and the next day
    // has no sunrise at all. Looking after that sunset must not report a
    // sunrise that is not coming.
    const Sun::Next n = Sun::nextEvent(kSet + 60, kToday, polarDay(Sun::DayKind::AlwaysDown));
    EXPECT_EQ(n.kind, Sun::EventKind::PolarNight);
    EXPECT_EQ(n.whenUtc, -1);
    EXPECT_EQ(n.secondUtc, -1);
    EXPECT_TRUE(n.nextDay);
}

TEST(Schedule, AZoneNearItsOwnLongitudeAgrees)
{
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(-75.7, -5 * 3600));   // Ottawa, EST
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(-75.7, -4 * 3600));   // Ottawa, EDT
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(-0.13, 0));           // London, GMT
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(151.2, 10 * 3600));   // Sydney
}

TEST(Schedule, GeographyIsNotMistakenForTravel)
{
    // The zones that really are a long way from their sun. None of these people
    // should be told their watch is confused.
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(75.99, 8 * 3600));    // Kashgar on Beijing time, +3.0
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(-3.7, 2 * 3600));     // Madrid in summer, +2.2
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(-68.3, -3 * 3600));   // Ushuaia, +1.6
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(-157.4, 14 * 3600));  // Kiritimati: 24.5 raw, 0.5 the short way
}

TEST(Schedule, TravelIsNoticed)
{
    // The case this exists for: a home position in Ottawa, a watch that has
    // synced to European time.
    EXPECT_FALSE(Sun::zoneAgreesWithLongitude(-75.7, 2 * 3600));
    EXPECT_FALSE(Sun::zoneAgreesWithLongitude(-75.7, 9 * 3600));
    EXPECT_FALSE(Sun::zoneAgreesWithLongitude(151.2, 0));
}

TEST(Schedule, TheToleranceIsWhereItSaysItIs)
{
    // Straddling the default at longitude zero, so the arithmetic is visible.
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(0.0, 4 * 3600));
    EXPECT_FALSE(Sun::zoneAgreesWithLongitude(0.0, 4 * 3600 + 60));
    EXPECT_TRUE(Sun::zoneAgreesWithLongitude(0.0, -4 * 3600));
    EXPECT_FALSE(Sun::zoneAgreesWithLongitude(0.0, -4 * 3600 - 60));
}

} // namespace
