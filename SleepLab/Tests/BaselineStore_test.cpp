/**
 * Host tests for the personal baseline.
 *
 * Two properties, and the second matters more than the first: the median is
 * computed correctly, and the store *refuses to say anything* until it has
 * earned the right to. A small delta shown with a caveat is a number that gets
 * remembered without the caveat.
 */

#include <gtest/gtest.h>

#include "Engine/BaselineStore.hpp"

namespace {

using Engine::BaselineStore;
using Engine::kAbsent;

BaselineStore::Sample night(int32_t hrMinX10, int32_t effPct = 85,
                            int32_t sleepMin = 420)
{
    BaselineStore::Sample s;
    s.hrMinX10      = hrMinX10;
    s.efficiencyPct = effPct;
    s.totalSleepMin = sleepMin;
    return s;
}

TEST(BaselineStore, RefusesToReportADeltaUntilEnoughNightsExist)
{
    BaselineStore b;

    for (size_t i = 1; i < BaselineStore::kMinNights; ++i) {
        b.add(night(520));
        const auto d = b.hrMin(530);
        EXPECT_FALSE(d.available) << "after " << i << " night(s)";
        EXPECT_EQ(d.delta, kAbsent);
        EXPECT_EQ(d.baseline, kAbsent);
        EXPECT_EQ(d.nightsNeeded, BaselineStore::kMinNights - i);
    }

    b.add(night(520));
    const auto d = b.hrMin(530);
    EXPECT_TRUE(d.available);
    EXPECT_EQ(d.nightsNeeded, 0u);
}

TEST(BaselineStore, TheBaselineIsTheMedianNotTheMean)
{
    // One night on a plane should not move a baseline that four normal nights
    // agree on. With a 28-night window a mean would carry it for four weeks.
    BaselineStore b;
    for (int i = 0; i < 5; ++i) {
        b.add(night(500));
    }
    b.add(night(900));   // the outlier

    const auto d = b.hrMin(505);
    ASSERT_TRUE(d.available);
    EXPECT_EQ(d.baseline, 500) << "the median ignores the outlier";
    EXPECT_EQ(d.delta, 5);
}

TEST(BaselineStore, TheDeltaIsTonightMinusTheBaseline)
{
    BaselineStore b;
    for (int i = 0; i < 6; ++i) {
        b.add(night(480));
    }

    EXPECT_EQ(b.hrMin(510).delta, 30)  << "a raised nocturnal minimum";
    EXPECT_EQ(b.hrMin(455).delta, -25) << "a lowered one";
}

TEST(BaselineStore, NightsMissingAFieldDoNotContributeASentinelToItsBaseline)
{
    // A night recorded with the heart-rate sensor off contributes nothing to
    // an HR baseline. Averaging kAbsent in would drag the baseline towards -1
    // and make every subsequent delta enormous.
    BaselineStore b;
    for (int i = 0; i < 6; ++i) {
        auto s = night(500);
        s.hrMinX10 = kAbsent;      // HR was off these nights
        b.add(s);
    }

    const auto hr = b.hrMin(520);
    EXPECT_FALSE(hr.available) << "six nights, none of them measured HR";

    // The other fields were measured, so they are ready.
    EXPECT_TRUE(b.efficiency(80).available);
}

TEST(BaselineStore, TheWindowRollsSoTheBaselineTracksAChangingSleeper)
{
    // An all-time average would take months to notice a new job or a new baby
    // -- exactly when the number matters most.
    BaselineStore b;
    for (size_t i = 0; i < BaselineStore::kWindowNights; ++i) {
        b.add(night(500));
    }
    EXPECT_EQ(b.hrMin(500).baseline, 500);
    EXPECT_EQ(b.nights(), BaselineStore::kWindowNights);

    // A month at a new level replaces the window entirely.
    for (size_t i = 0; i < BaselineStore::kWindowNights; ++i) {
        b.add(night(560));
    }
    EXPECT_EQ(b.hrMin(560).baseline, 560);
    EXPECT_EQ(b.nights(), BaselineStore::kWindowNights)
        << "and never grows past the window";
}

TEST(BaselineStore, ABaselineIsUsableEvenWhenTonightHasNoValue)
{
    // A night recorded with HR off still deserves to see the baseline; what is
    // missing is the comparison, and the caller can tell those apart.
    BaselineStore b;
    for (int i = 0; i < 6; ++i) {
        b.add(night(500));
    }

    const auto d = b.hrMin(kAbsent);
    EXPECT_TRUE(d.available);
    EXPECT_EQ(d.baseline, 500);
    EXPECT_EQ(d.delta, kAbsent);
}

TEST(BaselineStore, RestoreRoundTripsThroughThePersistedForm)
{
    BaselineStore a;
    for (int i = 0; i < 10; ++i) {
        a.add(night(500 + i * 3));
    }

    BaselineStore b;
    b.restore(a.samples(), a.count(), a.nextSlot());

    EXPECT_EQ(b.nights(), a.nights());
    EXPECT_EQ(b.hrMin(520).baseline, a.hrMin(520).baseline);
    EXPECT_EQ(b.hrMin(520).delta,    a.hrMin(520).delta);
}

TEST(BaselineStore, ACorruptStoreCostsTheBaselineAndNotTheNight)
{
    // A truncated or nonsensical persisted store must not take the app down or
    // produce a wild baseline; it should simply have fewer nights in it.
    BaselineStore b;
    b.restore(nullptr, 9999, 9999);

    EXPECT_LE(b.nights(), BaselineStore::kWindowNights);
    EXPECT_FALSE(b.hrMin(500).available);
}

} // namespace
