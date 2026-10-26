#include <gtest/gtest.h>

#include <vector>

#include "Rhythm.hpp"

using namespace Rhythm;

namespace
{

/// Minutes after noon for a local clock time.
constexpr int32_t hm(int32_t h, int32_t m)
{
    return (h * 60 + m + 720) % 1440;
}

Night night(int32_t bed, int32_t wake, uint16_t rest = 450)
{
    Night n;
    n.bed     = bed;
    n.wake    = wake;
    n.restMin = rest;
    return n;
}

/// Seven made-up nights, oldest first: a usual near 00:50, then one 1h22 early.
std::vector<Night> week()
{
    return {night(hm(0, 30), hm(8, 0)), night(hm(1, 40), hm(8, 30)), night(hm(0, 50), hm(7, 50)),
            night(hm(1, 10), hm(8, 10)), night(hm(23, 20), hm(7, 40)), night(hm(0, 55), hm(8, 5)),
            night(hm(23, 30), hm(8, 0))};
}

std::vector<Night> fortnight()
{
    std::vector<Night> v(4, night(hm(0, 50), hm(8, 0)));
    const auto         w = week();
    v.insert(v.end(), w.begin(), w.end());
    return v;
}

} // namespace

TEST(Rhythm, AfterNoonKeepsANightInOrder)
{
    constexpr uint32_t k2210Est = 1768533000u;  // 2026-01-15 22:10 at UTC-5
    EXPECT_EQ(afterNoon(k2210Est, -300), hm(22, 10));
    EXPECT_EQ(afterNoon(k2210Est + 6 * 3600, -300), hm(4, 10));
    EXPECT_LT(afterNoon(k2210Est, -300), afterNoon(k2210Est + 6 * 3600, -300));
}

TEST(Rhythm, NoUsualUnderSevenNights)
{
    const auto v = fortnight();
    EXPECT_FALSE(usual(v.data(), 6).known);
    EXPECT_FALSE(usual(v.data(), 6).varies);
    EXPECT_TRUE(usual(v.data(), 7).known);
}

TEST(Rhythm, TheUsualIsTheMedianOfTheLastSixNights)
{
    const auto v = fortnight();
    const Usual u = usual(v.data(), 10);
    ASSERT_TRUE(u.known);
    EXPECT_EQ(u.bed, (hm(0, 50) + hm(0, 55)) / 2);
    EXPECT_EQ(u.wake, (hm(8, 0) + hm(8, 5)) / 2);
    EXPECT_EQ(usual(v.data(), 11).bed, (hm(0, 50) + hm(0, 55)) / 2);
}

TEST(Rhythm, OnlySixNightsCount)
{
    auto v = fortnight();
    v.insert(v.begin(), night(hm(18, 0), hm(4, 0)));
    EXPECT_EQ(usual(v.data(), 12).bed, usual(v.data() + 1, 11).bed);
}

TEST(Rhythm, BedtimesEitherSideOfNoonAreOneUsual)
{
    std::vector<Night> v;
    for (int i = 0; i < 7; ++i) {
        v.push_back(i % 2 == 0 ? night(hm(11, 50), hm(19, 0)) : night(hm(12, 10), hm(19, 20)));
    }
    const Usual u = usual(v.data(), v.size());
    ASSERT_TRUE(u.known);
    EXPECT_EQ(minuteOfDay(u.bed), 12 * 60);
    EXPECT_EQ(offset(hm(11, 50), u), -10);
}

TEST(Rhythm, ARotaHasNoUsual)
{
    std::vector<Night> v;
    for (int i = 0; i < 12; ++i) {
        v.push_back((i / 3) % 2 == 0 ? night(hm(22, 30), hm(6, 30)) : night(hm(8, 30), hm(15, 30)));
    }
    const Usual u = usual(v.data(), v.size());
    EXPECT_FALSE(u.known);
    EXPECT_TRUE(u.varies);
}

TEST(Rhythm, AnOffsetIsNeverMoreThanTwelveHours)
{
    Usual u;
    u.bed = hm(23, 0);
    EXPECT_EQ(offset(hm(1, 0), u), 120);
    EXPECT_EQ(offset(hm(10, 0), u), 660);
    EXPECT_EQ(offset(hm(12, 0), u), -660);
}

TEST(Rhythm, RestFillsOneLapAtSevenHours)
{
    EXPECT_EQ(restFill(0), 0);
    EXPECT_EQ(restFill(210), 500);
    EXPECT_EQ(restFill(420), kFull);
    EXPECT_EQ(restFill(600), 1428);
}

TEST(Rhythm, TheWindowIsFullAndFadesToEmptyAtTwoHours)
{
    EXPECT_EQ(rhythmFill(0), kFull);
    EXPECT_EQ(rhythmFill(-30), kFull);
    EXPECT_EQ(rhythmFill(30), kFull);
    EXPECT_EQ(rhythmFill(75), 500);
    EXPECT_EQ(rhythmFill(-75), 500);
    EXPECT_EQ(rhythmFill(120), 0);
    EXPECT_EQ(rhythmFill(-400), 0);
}

TEST(Rhythm, ABedtimeInTheWindowClosesTheRingAndAnEarlyOneDoesNot)
{
    const auto v = fortnight();
    const Usual before9 = usual(v.data(), 9);
    EXPECT_EQ(offset(v[9].bed, before9), 5);
    EXPECT_EQ(rhythmFill(offset(v[9].bed, before9)), kFull);

    const Usual before10 = usual(v.data(), 10);
    EXPECT_EQ(offset(v[10].bed, before10), -82);
    EXPECT_EQ(rhythmFill(offset(v[10].bed, before10)), 423);
}

namespace
{

constexpr uint32_t k0030Edt = 1791520200u;  // 2026-10-09 00:30 at UTC-4

} // namespace

TEST(Rhythm, RecentKeepsTheLongestWornPeriodOfEachDay)
{
    Recent r;
    r.add(k0030Edt, k0030Edt + 8 * 3600, -240, 450);
    r.add(k0030Edt - 10 * 3600, k0030Edt - 9 * 3600, -240, 50);  // the 14:30 nap before it, same noon-to-noon day
    EXPECT_EQ(r.size(), 1u);
    const Usual u = r.usualBefore(UINT32_MAX);
    EXPECT_FALSE(u.known);
}

TEST(Rhythm, RecentHoldsAFortnightAndForgetsTheOldest)
{
    Recent r;
    for (uint32_t d = 0; d < 20; ++d) {
        r.add(k0030Edt + d * 86400, k0030Edt + d * 86400 + 8 * 3600, -240, 450);
    }
    EXPECT_EQ(r.size(), Recent::kCap);
    const Usual u = r.usualBefore(UINT32_MAX);
    ASSERT_TRUE(u.known);
    EXPECT_EQ(u.nights, kBaselineNights);
    EXPECT_EQ(minuteOfDay(u.bed), 30);
    EXPECT_EQ(minuteOfDay(u.wake), 8 * 60 + 30);
}

TEST(Rhythm, UsualBeforeLeavesOutTheNightScored)
{
    Recent r;
    for (uint32_t d = 0; d < 7; ++d) {
        r.add(k0030Edt + d * 86400, k0030Edt + d * 86400 + 8 * 3600, -240, 450);
    }
    EXPECT_TRUE(r.usualBefore(UINT32_MAX).known);
    EXPECT_FALSE(r.usualBefore(k0030Edt + 6 * 86400).known);
}

TEST(Rhythm, NightsToGoCountsEveryNightKept)
{
    Recent r;
    EXPECT_EQ(r.nightsToGo(), kMinBaselineNights);
    for (uint32_t d = 0; d < 5; ++d) {
        r.add(k0030Edt + d * 86400, k0030Edt + d * 86400 + 8 * 3600, -240, 450);
    }
    const uint32_t back = k0030Edt + 100 * 86400;
    r.add(back, back + 8 * 3600, -240, 450);
    EXPECT_EQ(r.nightsToGo(), 1);
    EXPECT_FALSE(r.usualBefore(UINT32_MAX).known);
    r.add(back + 86400, back + 86400 + 8 * 3600, -240, 450);
    EXPECT_EQ(r.nightsToGo(), 0);
    EXPECT_TRUE(r.usualBefore(UINT32_MAX).known);
}
