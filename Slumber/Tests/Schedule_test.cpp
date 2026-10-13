#include <gtest/gtest.h>

#include "Schedule.hpp"

namespace
{

constexpr int32_t kOff = -240;

/// 2026-10-12, a Monday, as days since 1970-01-01.
constexpr int32_t kMonday = 20738;

constexpr uint8_t kWednesday = 3;

/// Local clock "hh:mm" on `day`, where a time before noon is the next morning.
uint32_t at(int32_t day, int hh, int mm)
{
    int64_t local = static_cast<int64_t>(day) * 86400 + (hh * 60 + mm) * 60;
    if (hh < 12) {
        local += 86400;
    }
    return static_cast<uint32_t>(local - static_cast<int64_t>(kOff) * 60);
}

/// The night begun on `day`, `bedH:bedM` to `minutes` later.
void sleep(Schedule::Learner &l, int32_t day, int bedH, int bedM, int minutes = 480)
{
    const uint32_t s = at(day, bedH, bedM);
    l.add(s, s + static_cast<uint32_t>(minutes) * 60, kOff);
}

/// `weeks` weeks from kMonday: every night at 23:00 but Wednesdays at `wedH:wedM`.
Schedule::Learner weeksWithWednesdays(int weeks, int wedH, int wedM)
{
    Schedule::Learner l;
    for (int32_t d = 0; d < weeks * 7; ++d) {
        const int32_t day = kMonday + d;
        if (Schedule::weekday(day) == kWednesday) {
            sleep(l, day, wedH, wedM);
        } else {
            sleep(l, day, 23, 0);
        }
    }
    return l;
}

int32_t clock(int hh, int mm)
{
    return Rhythm::afterNoon(at(kMonday, hh, mm), kOff);
}

} // namespace

TEST(Schedule, DaysAreTheWeekdaysTheyBeganOn)
{
    EXPECT_EQ(Schedule::weekday(0), 4);  // 1970-01-01
    EXPECT_EQ(Schedule::weekday(kMonday), 1);
    EXPECT_EQ(Schedule::weekday(kMonday - 1), Schedule::kSunday);
    EXPECT_EQ(Schedule::weekday(-1), 3);
    EXPECT_EQ(Rhythm::noonDay(at(kMonday, 0, 40), kOff), kMonday);
}

TEST(Schedule, TonightTurnsOverInTheSmallHours)
{
    const int32_t tue = kMonday + 1;
    EXPECT_EQ(Schedule::tonight(at(kMonday, 3, 0), kOff), kMonday);  // Tuesday 03:00
    EXPECT_EQ(Schedule::tonight(at(kMonday, 5, 0), kOff), tue);      // Tuesday 05:00
    EXPECT_EQ(Schedule::tonight(at(tue, 23, 0), kOff), tue);
}

TEST(Schedule, ThreeNightsBeforeThereIsAUsual)
{
    Schedule::Learner l;
    sleep(l, kMonday, 23, 0);
    sleep(l, kMonday + 1, 23, 0);
    EXPECT_FALSE(l.expect(kMonday + 2).known);
    sleep(l, kMonday + 2, 23, 0);
    const Schedule::Expected e = l.expect(kMonday + 3);
    ASSERT_TRUE(e.known);
    EXPECT_EQ(e.bed, clock(23, 0));
    EXPECT_EQ(e.wake, clock(7, 0));
    EXPECT_EQ(e.own, 0);
    EXPECT_EQ(e.nights, 3);
}

TEST(Schedule, OnlyNightsBeforeTheDayCount)
{
    Schedule::Learner l = weeksWithWednesdays(2, 1, 0);
    const Schedule::Expected e = l.expect(kMonday + 2);  // the first Wednesday
    ASSERT_FALSE(e.known);
}

TEST(Schedule, LateWednesdaysAreLearned)
{
    const Schedule::Learner l = weeksWithWednesdays(6, 0, 30);

    const Schedule::Pattern p = l.pattern(kWednesday);
    ASSERT_TRUE(p.known);
    EXPECT_EQ(p.bedShift, 90);
    EXPECT_EQ(p.wakeShift, 90);
    EXPECT_EQ(p.nights, 6);
    EXPECT_EQ(p.bedAgree, 6);
    EXPECT_EQ(l.pattern(1).bedShift, 0);  // Monday

    const Schedule::Week w = l.week();
    EXPECT_EQ(w.laterBed, 1u << kWednesday);
    EXPECT_EQ(w.laterWake, 1u << kWednesday);
    EXPECT_EQ(w.earlierBed, 0u);
    EXPECT_EQ(w.earlierWake, 0u);

    const int32_t            nextWednesday = kMonday + 44;
    const Schedule::Expected e             = l.expect(nextWednesday);
    ASSERT_TRUE(e.known);
    EXPECT_EQ(e.own, 6);
    EXPECT_EQ(e.bed, (6 * clock(0, 30) + 2 * clock(23, 0)) / 8);
    EXPECT_EQ(l.expect(nextWednesday + 1).bed, clock(23, 0));
}

TEST(Schedule, OneOddWednesdayIsNotAPattern)
{
    Schedule::Learner l = weeksWithWednesdays(6, 23, 0);
    sleep(l, kMonday + 44, 1, 30);
    EXPECT_EQ(l.week().laterBed, 0u);
    EXPECT_EQ(l.expect(kMonday + 51).bed, clock(23, 0));
}

TEST(Schedule, TwoWednesdaysOnlyLeanTheUsual)
{
    const Schedule::Learner l = weeksWithWednesdays(2, 0, 30);
    EXPECT_FALSE(l.pattern(kWednesday).known);
    const Schedule::Expected e = l.expect(kMonday + 16);
    ASSERT_TRUE(e.known);
    EXPECT_EQ(e.own, 2);
    EXPECT_EQ(e.bed, (2 * clock(0, 30) + 2 * clock(23, 0)) / 4);
}

TEST(Schedule, AHabitHeldTwoWeeksInThreeIsNotOne)
{
    Schedule::Learner l = weeksWithWednesdays(6, 0, 30);
    sleep(l, kMonday + 9, 23, 0);  // no longer than the night kept, so it stays
    sleep(l, kMonday + 16, 23, 0);
    const Schedule::Pattern p = l.pattern(kWednesday);
    EXPECT_EQ(p.bedAgree, 6);

    Schedule::Learner m;
    for (int32_t d = 0; d < 42; ++d) {
        const int32_t day   = kMonday + d;
        const bool    early = d == 9 || d == 16;
        if (Schedule::weekday(day) == kWednesday && !early) {
            sleep(m, day, 0, 30);
        } else {
            sleep(m, day, 23, 0);
        }
    }
    const Schedule::Pattern q = m.pattern(kWednesday);
    ASSERT_TRUE(q.known);
    EXPECT_EQ(q.bedShift, 90);
    EXPECT_EQ(q.bedAgree, 4);
    EXPECT_EQ(m.week().laterBed, 0u);
}

TEST(Schedule, LaterWeekendsAreSocialJetlag)
{
    Schedule::Learner l;
    for (int32_t d = 0; d < 28; ++d) {
        const int32_t day = kMonday + d;
        const uint8_t wd  = Schedule::weekday(day);
        if (wd == Schedule::kFriday || wd == Schedule::kSaturday) {
            sleep(l, day, 0, 30, 540);
        } else {
            sleep(l, day, 23, 0);
        }
    }
    const Schedule::Week w = l.week();
    EXPECT_EQ(w.laterBed, (1u << Schedule::kFriday) | (1u << Schedule::kSaturday));
    ASSERT_TRUE(w.jetlagKnown);
    EXPECT_EQ(w.socialJetlagMin, 120);  // bed 90 later, wake 150 later
}

TEST(Schedule, ANapBesideTheNightIsNotTheNight)
{
    Schedule::Learner l;
    for (int32_t d = 0; d < 3; ++d) {
        sleep(l, kMonday + d, 23, 0);
        sleep(l, kMonday + d, 14, 0, 40);
    }
    EXPECT_EQ(l.size(), 3u);
    EXPECT_EQ(l.expect(kMonday + 3).bed, clock(23, 0));
}

TEST(Schedule, AnOldRoutineIsForgotten)
{
    Schedule::Learner l = weeksWithWednesdays(4, 0, 30);
    for (int32_t d = 28; d < 28 + Schedule::kWindowDays; ++d) {
        sleep(l, kMonday + d, 23, 0);
    }
    EXPECT_EQ(l.size(), static_cast<size_t>(Schedule::kWindowDays));
    EXPECT_EQ(l.week().laterBed, 0u);
    EXPECT_EQ(l.pattern(kWednesday).bedShift, 0);
}

TEST(Schedule, ANightOlderThanTheWindowIsIgnored)
{
    Schedule::Learner l;
    sleep(l, kMonday + Schedule::kWindowDays, 23, 0);
    sleep(l, kMonday, 23, 0);
    EXPECT_EQ(l.size(), 1u);
    sleep(l, kMonday + 1, 23, 0);
    EXPECT_EQ(l.size(), 2u);
}

TEST(Schedule, DaySleepWrapsPastNoon)
{
    Schedule::Learner l;
    for (int32_t d = 0; d < 3; ++d) {
        sleep(l, kMonday + d, 8, 0, 420);  // 08:00 to 15:00
    }
    const Schedule::Expected e = l.expect(kMonday + 3);
    ASSERT_TRUE(e.known);
    EXPECT_EQ(Rhythm::minuteOfDay(e.bed), 8 * 60);
    EXPECT_EQ(Rhythm::minuteOfDay(e.wake), 15 * 60);
}
