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
    EXPECT_EQ(choose(p, 1, kNow).kind, Kind::NotWorn);
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
