#include <gtest/gtest.h>

#include <vector>

#include "SleepPeriods.hpp"

using namespace SleepPeriods;

namespace
{

constexpr uint32_t kMidnight = 1'791'504'000u;  // any midnight will do
constexpr uint32_t kH        = 3600u;
constexpr uint32_t kMin      = 60u;

constexpr uint32_t kStill  = 300;    // a sleeping wrist
constexpr uint32_t kMoving = 20000;  // awake and about
constexpr uint32_t kTable  = 185;    // the accelerometer's own noise

/// A day of epochs from `from` to `to`, active unless a phase says otherwise.
class Day
{
public:
    Day(uint32_t from, uint32_t to)
    {
        for (uint32_t t = from; t < to; t += kEpochS) {
            Epoch e;
            e.utc        = t;
            e.count      = kMoving;
            e.hasHr      = true;
            e.hrAsked    = true;
            e.hrTrustX10 = 30;
            epochs.push_back(e);
        }
    }

    Day &still(uint32_t from, uint32_t to)
    {
        return each(from, to, [](Epoch &e) { e.count = kStill; });
    }

    Day &walk(uint32_t from, uint32_t to)
    {
        return each(from, to, [](Epoch &e) {
            e.count = kMoving;
            e.steps = 22;
        });
    }

    Day &trust(uint32_t from, uint32_t to, uint8_t t)
    {
        return each(from, to, [t](Epoch &e) { e.hrTrustX10 = t; });
    }

    /// Off the wrist: the count at the sensor floor, and heart rate untrusted.
    Day &table(uint32_t from, uint32_t to)
    {
        return each(from, to, [](Epoch &e) {
            e.count      = kTable;
            e.hrTrustX10 = 8;
        });
    }

    Day &hrNotAsked(uint32_t from, uint32_t to)
    {
        return each(from, to, [](Epoch &e) {
            e.hasHr   = false;
            e.hrAsked = false;
        });
    }

    Day &hrSilent(uint32_t from, uint32_t to)
    {
        return each(from, to, [](Epoch &e) { e.hasHr = false; });
    }

    Day &drop(uint32_t from, uint32_t to)
    {
        std::vector<Epoch> kept;
        for (const Epoch &e : epochs) {
            if (e.utc < from || e.utc >= to) {
                kept.push_back(e);
            }
        }
        epochs = kept;
        return *this;
    }

    std::vector<Period> periods(uint32_t now = UINT32_MAX) const
    {
        std::vector<Period> out(8);
        out.resize(find(epochs.data(), epochs.size(), out.data(), out.size(), now));
        return out;
    }

    std::vector<Epoch> epochs;

private:
    template <typename F>
    Day &each(uint32_t from, uint32_t to, F f)
    {
        for (Epoch &e : epochs) {
            if (e.utc >= from && e.utc < to) {
                f(e);
            }
        }
        return *this;
    }
};

const uint32_t kNoon     = kMidnight - 12 * kH;
const uint32_t kNextNoon = kMidnight + 12 * kH;

uint32_t at(int hours, int minutes = 0)
{
    return static_cast<uint32_t>(static_cast<int64_t>(kMidnight) + hours * 3600 + minutes * 60);
}

/// Within the rolling median's reach of the true edge.
void expectNear(uint32_t got, uint32_t want)
{
    const uint32_t d = got > want ? got - want : want - got;
    EXPECT_LE(d, kMedianHalfWidth * kEpochS) << "got " << got << " want " << want;
}

} // namespace

TEST(SleepPeriods, AQuietNightIsOnePeriod)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(7)).periods();
    ASSERT_EQ(p.size(), 1u);
    expectNear(p[0].startUtc, at(-1));
    expectNear(p[0].endUtc, at(7));
    EXPECT_EQ(p[0].worn, Worn::Worn);
    EXPECT_NEAR(p[0].restMin, 8 * 60, 6);
}

/// The failure that split half of SleepLab's recorded nights.
TEST(SleepPeriods, ABathroomTripStaysInsideTheNight)
{
    const auto p = Day(kNoon, kNextNoon)
                       .still(at(-1), at(7))
                       .walk(at(3, 10), at(3, 30))
                       .periods();
    ASSERT_EQ(p.size(), 1u);
    expectNear(p[0].startUtc, at(-1));
    expectNear(p[0].endUtc, at(7));
    EXPECT_NEAR(p[0].restMin, 8 * 60 - 20, 6);
}

TEST(SleepPeriods, AnHourAndAHalfUpMakesTwoPeriods)
{
    const auto p = Day(kNoon, kNextNoon)
                       .still(at(-1), at(7))
                       .walk(at(2), at(3, 30))
                       .periods();
    ASSERT_EQ(p.size(), 2u);
    expectNear(p[0].endUtc, at(2));
    expectNear(p[1].startUtc, at(3, 30));
}

/// No bedtime window, so a late night that runs past noon is not cut at a clock.
TEST(SleepPeriods, ALateNightRunsPastNoonWhole)
{
    const auto p = Day(kNoon, kNextNoon + 6 * kH).still(at(3), at(12, 30)).periods();
    ASSERT_EQ(p.size(), 1u);
    expectNear(p[0].startUtc, at(3));
    expectNear(p[0].endUtc, at(12, 30));
}

TEST(SleepPeriods, AnAfternoonNapIsFoundToo)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-9), at(-8)).periods();
    ASSERT_EQ(p.size(), 1u);
    expectNear(p[0].startUtc, at(-9));
}

TEST(SleepPeriods, RestShorterThanHalfAnHourIsNotAPeriod)
{
    EXPECT_TRUE(Day(kNoon, kNextNoon).still(at(-3), at(-3, 20)).periods().empty());
}

TEST(SleepPeriods, OneSpikeDoesNotWakeAnyone)
{
    Day d(kNoon, kNextNoon);
    d.still(at(-1), at(7));
    d.walk(at(3), at(3) + kEpochS);
    const auto p = d.periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_NEAR(p[0].restMin, 8 * 60, 6);
}

TEST(SleepPeriods, ANightstandIsNotWorn)
{
    const auto p = Day(kNoon, kNextNoon).table(at(-2), at(9)).periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].worn, Worn::NotWorn);
}

/// The watch taken off at 08:00 and left on a table: still, but not sleep.
TEST(SleepPeriods, ATableAfterTheNightIsNotPartOfIt)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(8)).table(at(8), at(11)).periods();
    ASSERT_EQ(p.size(), 2u);
    expectNear(p[0].endUtc, at(8));
    EXPECT_EQ(p[0].worn, Worn::Worn);
    EXPECT_EQ(p[1].worn, Worn::NotWorn);
}

TEST(SleepPeriods, OneLowTrustReadingDoesNotEndTheNight)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(7)).trust(at(3), at(3) + 2 * kEpochS, 8).periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].worn, Worn::Worn);
}

/// An hour lying on the watch: heart rate read but untrusted, the count above
/// the floor a table sits at.
TEST(SleepPeriods, AnArmOnTheSensorDoesNotSplitTheNight)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(7)).trust(at(1, 30), at(2, 30), 8).periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].worn, Worn::Worn);
    expectNear(p[0].startUtc, at(-1));
    expectNear(p[0].endUtc, at(7));
}

TEST(SleepPeriods, WithHeartRateNeverAskedForTheWristIsUncertain)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(7)).hrNotAsked(kNoon, kNextNoon).periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].worn, Worn::Uncertain);
}

/// Off the wrist the optical sensor can say nothing at all rather than say it
/// with low trust; asked and silent is a table, not an unknown.
TEST(SleepPeriods, ATableWhereHeartRateIsSilentIsNotWorn)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-4), at(0)).hrSilent(at(-4), at(0)).periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].worn, Worn::NotWorn);
}

/// The watch left on a table all evening, then worn to bed: two periods, never one.
TEST(SleepPeriods, ASilentTableBeforeTheNightIsNotPartOfIt)
{
    const auto p = Day(kNoon, kNextNoon)
                       .still(at(-4), at(-1) - 20 * 60)
                       .hrSilent(at(-4), at(-1) - 20 * 60)
                       .still(at(-1), at(7))
                       .periods();
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(p[0].worn, Worn::NotWorn);
    EXPECT_EQ(p[1].worn, Worn::Worn);
    expectNear(p[1].startUtc, at(-1));
}

/// Trust that hovers at the threshold all night gives no clear wrist either way.
TEST(SleepPeriods, AnUndecidedWristIsUncertain)
{
    Day d(kNoon, kNextNoon);
    d.table(at(-1), at(7));
    for (size_t i = 1; i < d.epochs.size(); i += 2) {
        d.epochs[i].hrTrustX10 = 30;
    }
    const auto p = d.periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].worn, Worn::Uncertain);
}

/// Missing data is not rest: two hours without epochs splits the night.
TEST(SleepPeriods, ALongDataGapSplitsThePeriod)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(7)).drop(at(2), at(4)).periods();
    ASSERT_EQ(p.size(), 2u);
}

/// A short dropout is merged across, and says how much was missing.
TEST(SleepPeriods, AShortDataGapIsMergedAndCounted)
{
    const auto p = Day(kNoon, kNextNoon).still(at(-1), at(7)).drop(at(2), at(2, 20)).periods();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_NEAR(p[0].missingMin, 20, 1);
}

TEST(SleepPeriods, AStillOpenNightIsNotSettled)
{
    const Day d = Day(kNoon, kNextNoon).still(at(-1), at(7));
    EXPECT_FALSE(d.periods(at(7, 30))[0].settled);
    EXPECT_TRUE(d.periods(at(8, 5))[0].settled);
}

TEST(SleepPeriods, OutputCapacityIsRespected)
{
    Day d(kNoon, kNextNoon);
    for (int h = -11; h < 11; h += 3) {
        d.still(at(h), at(h, 45));
    }
    std::vector<Period> out(2);
    EXPECT_EQ(find(d.epochs.data(), d.epochs.size(), out.data(), out.size(), UINT32_MAX), 2u);
}

TEST(SleepPeriods, NothingInNothingOut)
{
    Period out[1];
    EXPECT_EQ(find(nullptr, 0, out, 1, 0), 0u);
}

/// Still from 22:00 to 22:45, then asleep from 23:44: the night's first block
/// is only 30 minutes old at 00:14, so the evening must not settle before it.
TEST(SleepPeriods, AStillEveningWaitsForTheNightThatMayJoinIt)
{
    for (int after = 0; after <= 20; after += 5) {
        const uint32_t now = at(-1, 50 + after);
        const auto     p   = Day(kNoon, now).still(at(-2), at(-2, 45)).still(at(-1, 44), now).periods(now);
        ASSERT_FALSE(p.empty());
        EXPECT_FALSE(p.back().settled) << after << " min after 23:50";
    }
    const auto p = Day(kNoon, at(8, 10)).still(at(-2), at(-2, 45)).still(at(-1, 44), at(7)).periods(at(8, 10));
    ASSERT_EQ(p.size(), 1u);
    expectNear(p[0].startUtc, at(-2));
    EXPECT_TRUE(p[0].settled);
}

TEST(SleepPeriods, MovementAfterTheHourSettlesTheNight)
{
    const Day d = Day(kNoon, at(8, 5)).still(at(-1), at(7));
    EXPECT_TRUE(d.periods(at(8, 5))[0].settled);
}

/// Back to bed lying on the watch, so the second run's wrist is unknown until
/// trust returns at 08:05 and it joins the night.
TEST(SleepPeriods, ANightDoesNotSettleWhileAnUnknownWristCanStillJoinIt)
{
    const auto day = [](uint32_t now) {
        return Day(kNoon, now)
            .still(at(-1), at(7))
            .trust(at(6, 56), at(7, 10), 10)
            .still(at(7, 10), at(9))
            .trust(at(7, 10), at(8, 5), 10);
    };
    uint32_t settledEnd = 0;
    for (uint32_t now = at(7, 30); now <= at(10, 30); now += 5 * kMin) {
        const auto p = day(now).periods(now);
        ASSERT_FALSE(p.empty());
        if (p[0].settled && settledEnd == 0) {
            settledEnd = p[0].endUtc;
        }
        if (settledEnd != 0) {
            EXPECT_EQ(p[0].endUtc, settledEnd) << (now - at(7)) / kMin << " min after 07:00";
        }
    }
    EXPECT_NE(settledEnd, 0u);
    expectNear(settledEnd, at(9));
}

TEST(SleepPeriods, TheTableAfterWakingSettlesTheNight)
{
    const auto p = Day(kNoon, at(8, 10)).still(at(-1), at(7)).table(at(7, 5), at(8, 10)).periods(at(8, 10));
    ASSERT_EQ(p.size(), 2u);
    EXPECT_TRUE(p[0].settled);
}

TEST(SleepPeriods, NoLaterPeriodSettlesBeforeAnEarlierOne)
{
    const auto day = [](uint32_t now) {
        return Day(kNoon, now)
            .still(at(-1), at(7))
            .trust(at(6, 56), at(7, 45), 10)
            .still(at(7, 10), at(7, 40))
            .table(at(7, 45), at(13));
    };
    for (uint32_t now = at(7, 30); now <= at(13, 30); now += 5 * kMin) {
        const auto p = day(now).periods(now);
        for (size_t k = 0; k + 1 < p.size(); ++k) {
            if (p[k + 1].settled) {
                EXPECT_TRUE(p[k].settled) << "period " << k << " unsettled while " << k + 1 << " settled, "
                                          << (now - at(7)) / kMin << " min after 07:00";
            }
        }
    }
}

/// A dropout stamped a second late is the newest epoch at a tick, so the wrist it
/// gives the run is read from a window that the epochs after it still change.
TEST(SleepPeriods, AnEpochStampedLateSettlesNothingThatCanStillGrow)
{
    std::vector<Epoch> all;
    auto add = [&](uint32_t t, uint32_t count, bool hasHr, uint8_t trust) {
        Epoch e;
        e.utc        = t;
        e.count      = count;
        e.hasHr      = hasHr;
        e.hrAsked    = true;
        e.hrTrustX10 = trust;
        all.push_back(e);
    };
    const uint32_t dropout = at(7, 24) + 30;
    uint32_t       t       = at(-1);
    for (; t < at(6); t += kEpochS) add(t, kStill, true, 30);
    for (; t < at(6, 20); t += kEpochS) add(t, kMoving, true, 8);
    for (; t < dropout; t += kEpochS) add(t, 500, true, 8);
    add(dropout + 1, kStill, false, 0);
    for (t += kEpochS; t < at(9); t += kEpochS) add(t, kStill, true, 30);
    for (; t < at(13); t += kEpochS) add(t, kMoving, true, 30);

    uint32_t settledEnd = 0;
    for (uint32_t now = at(-1) + 15; now <= at(13); now += 5 * kMin) {
        std::vector<Epoch> seen;
        for (const Epoch &e : all) {
            if (e.utc + kEpochS <= now) {
                seen.push_back(e);
            }
        }
        if (seen.empty()) {
            continue;
        }
        std::vector<Period> p(8);
        p.resize(find(seen.data(), seen.size(), p.data(), p.size(), now));
        if (p.empty()) {
            continue;
        }
        if (settledEnd != 0) {
            EXPECT_EQ(p[0].endUtc, settledEnd) << (now - at(6)) / kMin << " min after 06:00";
        } else if (p[0].settled) {
            settledEnd = p[0].endUtc;
        }
    }
    expectNear(settledEnd, at(9));
}
