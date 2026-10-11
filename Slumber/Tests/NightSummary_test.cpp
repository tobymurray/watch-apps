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
