#include <gtest/gtest.h>

#include "LastNight.hpp"

using namespace SleepPeriods;
using LastNight::choose;
using LastNight::Kind;

namespace
{

constexpr uint32_t kNow = 1'791'640'000u;
constexpr uint32_t kH   = 3600u;

Period period(uint32_t endsAgoS, uint32_t hours, Worn worn)
{
    Period p;
    p.endUtc   = kNow - endsAgoS;
    p.startUtc = p.endUtc - hours * kH;
    p.worn     = worn;
    return p;
}

} // namespace

TEST(LastNight, NothingRecentIsNothing)
{
    const Period p[] = {period(LastNight::kRecentS + 60, 8, Worn::Worn)};
    EXPECT_EQ(choose(p, 1, kNow).kind, Kind::None);
}

/// A night split by a long awakening reports its larger half, not its later one.
TEST(LastNight, TheLongerHalfOfASplitNightWins)
{
    const Period p[] = {period(5 * kH, 5, Worn::Worn), period(1 * kH, 3, Worn::Worn)};
    const auto   c   = choose(p, 2, kNow);
    EXPECT_EQ(c.kind, Kind::Worn);
    EXPECT_EQ(c.period, &p[0]);
}

/// 23:00-03:00, up for 61 minutes, 04:01-07:30: one night of the rest of both.
TEST(LastNight, ASplitNightIsReportedWhole)
{
    Period p[] = {period(4 * kH + 29 * 60, 4, Worn::Worn), period(0, 3, Worn::Worn)};
    p[1].startUtc = p[0].endUtc + 61 * 60;
    p[0].restMin  = 230;
    p[1].restMin  = 200;
    p[1].settled  = true;
    const auto c = choose(p, 2, kNow);
    ASSERT_EQ(c.kind, Kind::Worn);
    EXPECT_EQ(c.sleep.startUtc, p[0].startUtc);
    EXPECT_EQ(c.sleep.endUtc, p[1].endUtc);
    EXPECT_EQ(c.sleep.restMin, 430);
    EXPECT_EQ(c.sleep.periods, 2);
    EXPECT_TRUE(c.sleep.settled);
    EXPECT_EQ(c.sleep.last, &p[1]);
}

TEST(LastNight, ASplitNightKeepsItsAwakening)
{
    const Period p[] = {period(4 * kH, 5, Worn::Worn), period(0, 3, Worn::Worn)};
    const auto   s   = choose(p, 2, kNow).sleep;
    EXPECT_EQ(s.awakeFromUtc[0], p[0].endUtc);
    EXPECT_EQ(s.awakeToUtc[0], p[1].startUtc);
    EXPECT_EQ(s.awakeFromUtc[1], 0u);
}

TEST(LastNight, AWholeNightHasNoAwakening)
{
    const Period p[] = {period(0, 8, Worn::Worn)};
    EXPECT_EQ(choose(p, 1, kNow).sleep.awakeFromUtc[0], 0u);
}

TEST(LastNight, OnlyTheLongestAwakeningsAreKept)
{
    constexpr uint32_t kM = 60u;
    Period p[4];
    const uint32_t gapsMin[] = {70, 150, 100};
    uint32_t at = kNow - 14 * kH;
    for (size_t i = 0; i < 4; ++i) {
        p[i].startUtc = at;
        p[i].endUtc   = at + 2 * kH;
        p[i].worn     = Worn::Worn;
        at = p[i].endUtc + (i < 3 ? gapsMin[i] * kM : 0);
    }
    const auto s = choose(p, 4, kNow).sleep;
    ASSERT_EQ(s.periods, 4);
    EXPECT_EQ(s.awakeToUtc[0] - s.awakeFromUtc[0], 150 * kM);
    EXPECT_EQ(s.awakeToUtc[1] - s.awakeFromUtc[1], 100 * kM);
    EXPECT_EQ(s.awakeFromUtc[0], p[1].endUtc);
}

TEST(LastNight, ANightAndANapAreTwoSleeps)
{
    const Period p[] = {period(8 * kH, 7, Worn::Worn), period(1 * kH, 1, Worn::Worn)};
    const auto   c   = choose(p, 2, kNow);
    EXPECT_EQ(c.sleep.startUtc, p[0].startUtc);
    EXPECT_EQ(c.sleep.endUtc, p[0].endUtc);
    EXPECT_EQ(c.sleep.periods, 1);
}

TEST(LastNight, AnAfternoonNapDoesNotReplaceTheNight)
{
    const Period p[] = {period(8 * kH, 7, Worn::Worn), period(1 * kH, 1, Worn::Worn)};
    EXPECT_EQ(choose(p, 2, kNow).period, &p[0]);
}

TEST(LastNight, AShorterNightAfterALongOneIsLastNight)
{
    const Period p[] = {period(19 * kH, 9, Worn::Worn), period(1 * kH, 5, Worn::Worn)};
    EXPECT_EQ(choose(p, 2, kNow).period, &p[1]);
}

TEST(LastNight, ASleepUnderHalfTheLongestDoesNotReplaceIt)
{
    const Period p[] = {period(18 * kH, 9, Worn::Worn), period(1 * kH, 4, Worn::Worn)};
    EXPECT_EQ(choose(p, 2, kNow).period, &p[0]);
}

/// The halves of a split sleep count together against the half-length rule.
TEST(LastNight, ASplitSleepIsJudgedWhole)
{
    const Period p[] = {period(19 * kH, 9, Worn::Worn), period(4 * kH, 3, Worn::Worn),
                        period(1 * kH, 2, Worn::Worn)};
    EXPECT_EQ(choose(p, 3, kNow).period, &p[1]);
}

/// 2026-10-16: the watch clock lost its zone and ran 4 h behind, so the night
/// before last ended 19.5 h "ago" and, being longer, was shown in place of last night.
TEST(LastNight, AClockThatLostItsZoneShowsTheLatestNight)
{
    Period p[2];
    p[0].startUtc = 1792036485u;
    p[0].endUtc   = 1792069575u;
    p[1].startUtc = 1792106943u;
    p[1].endUtc   = 1792135803u;
    p[0].worn = p[1].worn = Worn::Worn;
    EXPECT_EQ(choose(p, 2, 1792139949u).period, &p[1]);
}

TEST(LastNight, AWornNightOutranksALongerTable)
{
    const Period p[] = {period(14 * kH, 9, Worn::NotWorn), period(2 * kH, 6, Worn::Worn)};
    EXPECT_EQ(choose(p, 2, kNow).period, &p[1]);
}

TEST(LastNight, OnlyATableReportsNotWorn)
{
    const Period p[] = {period(2 * kH, 8, Worn::NotWorn)};
    const auto   c   = choose(p, 1, kNow);
    EXPECT_EQ(c.kind, Kind::NotWorn);
    EXPECT_EQ(c.sleep.startUtc, p[0].startUtc);
    EXPECT_EQ(c.sleep.endUtc, p[0].endUtc);
}

TEST(LastNight, AnUncertainWristIsNeverReported)
{
    const Period p[] = {period(2 * kH, 8, Worn::Uncertain)};
    EXPECT_EQ(choose(p, 1, kNow).kind, Kind::None);
}

TEST(LastNight, AsleepNowIsTheWornPeriodStillRunning)
{
    Period p[2] = {period(30 * kH, 8, Worn::Worn), period(0, 2, Worn::Worn)};
    p[0].settled = true;
    EXPECT_EQ(LastNight::asleepSince(p, 2, p[1].endUtc - kEpochS), p[1].startUtc);
}

TEST(LastNight, APeriodThatEndedIsNotAsleepNow)
{
    Period p[1] = {period(kH, 2, Worn::Worn)};
    EXPECT_EQ(LastNight::asleepSince(p, 1, kNow), 0u);
}

TEST(LastNight, ATableIsNotAsleep)
{
    Period p[1] = {period(0, 2, Worn::NotWorn)};
    EXPECT_EQ(LastNight::asleepSince(p, 1, p[0].endUtc - kEpochS), 0u);
}
