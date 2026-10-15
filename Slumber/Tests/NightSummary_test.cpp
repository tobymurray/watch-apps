#include <gtest/gtest.h>

#include <vector>

#include "NightSummary.hpp"

using namespace SleepPeriods;
using NightSummary::summarise;

namespace
{

constexpr uint32_t kStart = 1'791'600'000u;
constexpr uint32_t kStill = 300;
constexpr uint32_t kMove  = 5000;

/// Eight hours asleep, with `moving` epochs (by index) above the rest ceiling.
std::vector<Epoch> night(const std::vector<size_t> &moving = {})
{
    std::vector<Epoch> v;
    for (uint32_t i = 0; i < 8 * 120; ++i) {
        Epoch e;
        e.utc        = kStart + i * kEpochS;
        e.count      = kStill;
        e.hasHr      = true;
        e.hrAsked    = true;
        e.hrTrustX10 = 30;
        v.push_back(e);
    }
    for (size_t i : moving) {
        v[i].count = kMove;
    }
    return v;
}

Period whole(const std::vector<Epoch> &v)
{
    Period p;
    p.startUtc = v.front().utc;
    p.endUtc   = v.back().utc + kEpochS;
    return p;
}

} // namespace

TEST(NightSummary, AStillNightIsOneStretch)
{
    const auto v = night();
    const auto s = summarise(v.data(), v.size(), whole(v));
    EXPECT_EQ(s.restlessSpells, 0u);
    EXPECT_EQ(s.longestRestMin, 8u * 60u);
}

/// Turning over is one epoch; a spell is a minute of movement.
TEST(NightSummary, OneRestlessEpochIsNotASpell)
{
    const auto v = night({100, 400});
    EXPECT_EQ(summarise(v.data(), v.size(), whole(v)).restlessSpells, 0u);
}

TEST(NightSummary, EachMinuteOfMovementIsOneSpell)
{
    const auto v = night({100, 101, 400, 401, 402, 403, 404, 405});
    EXPECT_EQ(summarise(v.data(), v.size(), whole(v)).restlessSpells, 2u);
}

/// The rolling median keeps a short spell inside the rest it interrupts.
TEST(NightSummary, ASpellLongEnoughToWakeSplitsTheLongestStretch)
{
    std::vector<size_t> up;
    for (size_t i = 600; i < 620; ++i) {
        up.push_back(i);
    }
    const auto v = night(up);
    const auto s = summarise(v.data(), v.size(), whole(v));
    EXPECT_EQ(s.restlessSpells, 1u);
    EXPECT_NEAR(s.longestRestMin, 300, 3);
}

TEST(NightSummary, MissingDataBreaksTheStretch)
{
    auto v = night();
    v.erase(v.begin() + 300, v.begin() + 310);
    EXPECT_EQ(summarise(v.data(), v.size(), whole(v)).longestRestMin, (960u - 310u) / 2u);
}

TEST(NightSummary, OnlyThePeriodCounts)
{
    const auto v = night({10, 11, 900, 901});
    Period     p = whole(v);
    p.startUtc += 60 * kEpochS;
    p.endUtc -= 70 * kEpochS;
    EXPECT_EQ(summarise(v.data(), v.size(), p).restlessSpells, 0u);
}

namespace
{

/// `night()` with every reading at `bpmX10` but those in `dip`, at `dipX10`.
std::vector<Epoch> beating(uint16_t bpmX10, size_t dipFrom = 0, size_t dipTo = 0, uint16_t dipX10 = 0)
{
    std::vector<Epoch> v = night();
    for (size_t i = 0; i < v.size(); ++i) {
        v[i].hrX10 = i >= dipFrom && i < dipTo ? dipX10 : bpmX10;
    }
    return v;
}

} // namespace

TEST(NightHeart, TheMedianAndTheLowestTenMinutes)
{
    const std::vector<Epoch> v = beating(574, 600, 640, 488);  // 20 min at 48.8 from 5 h in
    const NightSummary::Heart h = NightSummary::heart(v.data(), v.size(), whole(v));
    EXPECT_EQ(h.medianBpm, 57);
    EXPECT_EQ(h.lowBpm, 49);
    EXPECT_GE(h.lowUtc, v[600].utc + NightSummary::kHrLowWindowS / 2);
    EXPECT_LT(h.lowUtc, v[620].utc + NightSummary::kHrLowWindowS / 2);
}

TEST(NightHeart, AShortDipIsAveragedIntoItsWindow)
{
    const std::vector<Epoch> v = beating(600, 300, 304, 400);  // 2 min at 40 inside 10 at 60
    const NightSummary::Heart h = NightSummary::heart(v.data(), v.size(), whole(v));
    EXPECT_EQ(h.lowBpm, 56);  // (16 x 60 + 4 x 40) / 20
}

TEST(NightHeart, UntrustedAndImplausibleReadingsDoNotCount)
{
    std::vector<Epoch> v = beating(560);
    for (size_t i = 0; i < 200; ++i) {
        v[i].hrX10 = 900;
        v[i].hrTrustX10 = kWornTrustX10 - 1;
    }
    for (size_t i = 200; i < 230; ++i) {
        v[i].hrX10 = 200;  // 20 bpm
    }
    const NightSummary::Heart h = NightSummary::heart(v.data(), v.size(), whole(v));
    EXPECT_EQ(h.medianBpm, 56);
    EXPECT_EQ(h.lowBpm, 56);
}

TEST(NightHeart, UnderHalfTheEpochsTrustedIsUnknown)
{
    std::vector<Epoch> v = beating(560);
    for (size_t i = 0; i < v.size() / 2 + 1; ++i) {
        v[i].hasHr = false;
    }
    const NightSummary::Heart h = NightSummary::heart(v.data(), v.size(), whole(v));
    EXPECT_EQ(h.medianBpm, 0);
    EXPECT_EQ(h.lowBpm, 0);
    EXPECT_EQ(h.lowUtc, 0u);
}

TEST(NightHeart, NoWindowWithEnoughReadingsHasNoLow)
{
    std::vector<Epoch> v = beating(560);
    for (size_t i = 0; i < v.size(); ++i) {
        v[i].hasHr = i % 2 == 0;  // 10 readings in any 10 minutes
    }
    const NightSummary::Heart h = NightSummary::heart(v.data(), v.size(), whole(v));
    EXPECT_EQ(h.medianBpm, 56);
    EXPECT_EQ(h.lowBpm, 0);
    EXPECT_EQ(h.lowUtc, 0u);
}

TEST(NightHeart, OnlyThePeriodCounts)
{
    const std::vector<Epoch> v = beating(600, 0, 100, 450);
    Period                   p = whole(v);
    p.startUtc                 = v[100].utc;
    const NightSummary::Heart h = NightSummary::heart(v.data(), v.size(), p);
    EXPECT_EQ(h.medianBpm, 60);
    EXPECT_EQ(h.lowBpm, 60);
}
