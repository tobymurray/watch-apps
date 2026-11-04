#include <gtest/gtest.h>

#include "Schedule.hpp"

namespace
{

constexpr int32_t kOff = -240;

/// 2026-10-12, a Monday, as days since 1970-01-01.
constexpr int32_t kMonday = 20738;

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
void sleep(Rhythm::Recent &r, int32_t day, int bedH, int bedM, int minutes = 480)
{
    const uint32_t s = at(day, bedH, bedM);
    r.add(s, s + static_cast<uint32_t>(minutes) * 60, kOff, static_cast<uint16_t>(minutes));
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

TEST(Schedule, TheWeekIsTheSevenEveningsBeforeTonight)
{
    Rhythm::Recent r;
    for (int32_t d = 0; d < 9; ++d) {
        if (d != 5) {
            sleep(r, kMonday + d, 22, d);
        }
    }
    const Schedule::Bars b = Schedule::bars(r, kMonday + 9);
    EXPECT_EQ(b.newestWeekday, Schedule::weekday(kMonday + 8));
    for (size_t i = 0; i < Schedule::kWeekNights; ++i) {
        const int d = static_cast<int>(i) + 2;
        if (d == 5) {
            EXPECT_EQ(b.bed[i], Schedule::kNoNight);
            EXPECT_EQ(b.wake[i], Schedule::kNoNight);
            continue;
        }
        EXPECT_EQ(b.bed[i], clock(22, d)) << i;
        EXPECT_EQ(b.wake[i], clock(22, d) + 480) << i;
    }
}

TEST(Schedule, AWeekBarWakingAfterNoonRunsPastTheDay)
{
    Rhythm::Recent r;
    sleep(r, kMonday, 4, 0, 10 * 60);
    const Schedule::Bars b = Schedule::bars(r, kMonday + 1);
    EXPECT_EQ(b.bed[Schedule::kWeekNights - 1], clock(4, 0));
    EXPECT_EQ(b.wake[Schedule::kWeekNights - 1], clock(4, 0) + 600);
    EXPECT_GT(b.wake[Schedule::kWeekNights - 1], 1440);
}

TEST(Schedule, AWeekBarIsTheDaysMainNightNotItsNap)
{
    Rhythm::Recent r;
    sleep(r, kMonday, 23, 0);
    sleep(r, kMonday, 14, 0, 40);
    const Schedule::Bars b = Schedule::bars(r, kMonday + 1);
    EXPECT_EQ(b.bed[Schedule::kWeekNights - 1], clock(23, 0));
}

TEST(Schedule, AWeekBeforeAnyNightIsEmpty)
{
    const Schedule::Bars b = Schedule::bars(Rhythm::Recent{}, kMonday);
    for (size_t i = 0; i < Schedule::kWeekNights; ++i) {
        EXPECT_EQ(b.bed[i], Schedule::kNoNight);
    }
}
