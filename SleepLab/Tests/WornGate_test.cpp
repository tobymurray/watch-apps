/**
 * Host tests for the gate that decides whether a night is reportable at all.
 *
 * The case that matters most is the nightstand: a watch on furniture is
 * perfectly still, reports no awakenings, and would score 100 % sleep
 * efficiency across eight hours. Every number would be beautiful and every
 * number would be about a piece of furniture. These tests are mostly about
 * making sure that night is refused.
 */

#include <gtest/gtest.h>

#include <vector>

#include "Engine/WornGate.hpp"
#include "NightFixture.hpp"

namespace {

using Engine::ScoringInput;
using Engine::WornGate;
using Engine::WornVerdict;

/// A normal worn night: micro-movement present, heart rate present.
std::vector<ScoringInput> wornNight(size_t n = 400)
{
    std::vector<ScoringInput> v;
    for (size_t i = 0; i < n; ++i) {
        // Comfortably above WornGate::kMicroMovementFloor -- a sleeping human
        // is never perfectly still; respiration alone moves the wrist.
        v.push_back(Fixture::epoch(/*count=*/25, /*hrX10=*/540));
    }
    return v;
}

/// A watch on a nightstand: dead still, no pulse, and TOUCH_DETECT wrongly
/// reporting worn -- which is precisely the failure the plausibility check
/// exists to catch, since a gate that trusted TOUCH_DETECT would pass this.
std::vector<ScoringInput> tableNight(size_t n = 400)
{
    std::vector<ScoringInput> v;
    for (size_t i = 0; i < n; ++i) {
        v.push_back(Fixture::epoch(/*count=*/1, /*hrX10=*/
                                   static_cast<int16_t>(Engine::kAbsent)));
    }
    return v;
}

TEST(WornGate, ANormalNightPasses)
{
    const auto r = WornGate::evaluate(wornNight().data(), 400, /*hrSampled=*/true);
    EXPECT_EQ(r.verdict, WornVerdict::Worn);
    EXPECT_TRUE(r.mayReportSleep());
}

TEST(WornGate, ANightstandFailsEvenWhenTouchDetectSaysWorn)
{
    // THE test in this file. wornPct is 100 throughout -- the capacitive
    // sensor is wrong -- and the gate has to overrule it on the evidence that
    // nothing alive was there.
    const auto night = tableNight();
    const auto r = WornGate::evaluate(night.data(), night.size(), true);

    EXPECT_EQ(r.wornPct, 100) << "the fixture's premise: TOUCH_DETECT is wrong";
    EXPECT_EQ(r.verdict, WornVerdict::NotWorn);
    EXPECT_FALSE(r.mayReportSleep());
    EXPECT_STREQ(r.reason(), "no movement or pulse - watch was not on a wrist");
}

TEST(WornGate, AWatchTakenOffMidNightFails)
{
    auto night = wornNight(400);
    // Off the wrist from 02:00 onwards: half the night.
    for (size_t i = 200; i < 400; ++i) {
        night[i] = Fixture::epoch(1, static_cast<int16_t>(Engine::kAbsent),
                                  Fixture::kGoodSamples, /*wornPct=*/0);
    }

    const auto r = WornGate::evaluate(night.data(), night.size(), true);
    EXPECT_EQ(r.verdict, WornVerdict::NotWorn);
    EXPECT_STREQ(r.reason(), "not worn for enough of the night");
}

TEST(WornGate, ABriefDropoutDoesNotBlankARealNight)
{
    // TOUCH_DETECT's flicker behaviour on a loose sleeping wrist is unmeasured
    // (ledger row S7), so the gate has to tolerate some. Twenty minutes out of
    // an eight-hour night must not cost the night.
    auto night = wornNight(480);
    for (size_t i = 100; i < 120; ++i) {
        night[i].wornPct = 0;
    }

    EXPECT_EQ(WornGate::evaluate(night.data(), night.size(), true).verdict,
              WornVerdict::Worn);
}

TEST(WornGate, MovementAloneIsEnoughWhenHeartRateWasSampledAndSometimesMissing)
{
    // Optical HR drops out routinely -- a loose band, a cold wrist. Movement
    // carries the plausibility check on its own in that case, which is the
    // point of "either", and the night still passes.
    auto night = wornNight(400);
    for (size_t i = 0; i < 400; i += 2) {
        night[i].hrMeanX10 = static_cast<int16_t>(Engine::kAbsent);
    }

    const auto r = WornGate::evaluate(night.data(), night.size(), true);
    EXPECT_EQ(r.verdict, WornVerdict::Worn);
    EXPECT_EQ(r.hrEvidence, WornGate::HrEvidence::Present);
}

TEST(WornGate, WithHeartRateOffTheBestAvailableVerdictIsUncertain)
{
    // Half the plausibility check was never gathered. The night may well be
    // fine, but "probably worn" is not a basis for printing a sleep
    // efficiency -- and Uncertain suppresses the numbers exactly as NotWorn
    // does, differing only in what the screen says about why.
    auto night = wornNight(400);
    for (auto &e : night) {
        e.hrMeanX10 = static_cast<int16_t>(Engine::kAbsent);
    }

    const auto r = WornGate::evaluate(night.data(), night.size(),
                                      /*hrSampled=*/false);
    EXPECT_EQ(r.verdict, WornVerdict::Uncertain);
    EXPECT_FALSE(r.mayReportSleep());
    EXPECT_EQ(r.hrEvidence, WornGate::HrEvidence::Absent);
    EXPECT_STREQ(r.reason(), "heart rate was off - cannot confirm it was worn");
}

TEST(WornGate, WithHeartRateOffATableStillFails)
{
    // Losing HR must not make the gate *more* permissive: the nightstand case
    // has to keep failing on micro-movement alone.
    const auto night = tableNight();
    const auto r = WornGate::evaluate(night.data(), night.size(),
                                      /*hrSampled=*/false);
    EXPECT_EQ(r.verdict, WornVerdict::NotWorn);
}

TEST(WornGate, AWornSensorThatSaidNothingIsUncertainNotNotWorn)
{
    // Measured on hardware 2026-08-18: TOUCH_DETECT is an event sensor and can
    // deliver nothing for a whole minute while perfectly happily subscribed. If
    // it says nothing for a whole *night* there is no state to carry forward,
    // every epoch's worn fraction is a default rather than a measurement, and
    // reading that as "taken off" would send somebody to put on a watch they
    // are already wearing.
    auto night = wornNight(400);
    for (auto &e : night) {
        e.wornPct = 0;   // never set, because nothing was ever heard
    }

    const auto r = WornGate::evaluate(night.data(), night.size(),
                                      /*hrSampled=*/true,
                                      /*wornReported=*/false);
    EXPECT_EQ(r.verdict, WornVerdict::Uncertain);
    EXPECT_FALSE(r.mayReportSleep()) << "it still suppresses the numbers";
    EXPECT_STREQ(r.reason(), "worn sensor said nothing all night");
}

TEST(WornGate, ASilentSensorIsNotAnExcuseToPassANightstand)
{
    // The new branch must not become a way through. Uncertain suppresses the
    // numbers exactly as NotWorn does -- what changes is only what the screen
    // says about why.
    const auto night = tableNight();
    const auto r = WornGate::evaluate(night.data(), night.size(), true, false);
    EXPECT_FALSE(r.mayReportSleep());
}

TEST(WornGate, ATooShortNightIsUncertainRatherThanJudged)
{
    // A handful of epochs can be unanimous by chance, and a night that short
    // has nothing worth reporting anyway.
    const auto night = wornNight(10);
    const auto r = WornGate::evaluate(night.data(), night.size(), true);
    EXPECT_EQ(r.verdict, WornVerdict::Uncertain);
    EXPECT_STREQ(r.reason(), "too short to judge");
}

TEST(WornGate, NullInputIsUncertainRatherThanDereferenced)
{
    EXPECT_EQ(WornGate::evaluate(nullptr, 400, true).verdict,
              WornVerdict::Uncertain);
}

TEST(WornGate, TheCountsAreReportedSoAVerdictCanBeAudited)
{
    // The verdict is a judgement over thresholds that are all currently
    // guesses. Publishing the counts behind it is what lets those thresholds
    // be re-set later from a night already recorded, rather than needing the
    // night recorded again.
    const auto night = wornNight(400);
    const auto r = WornGate::evaluate(night.data(), night.size(), true);

    EXPECT_EQ(r.epochs, 400u);
    EXPECT_EQ(r.wornEpochs, 400u);
    EXPECT_EQ(r.movingEpochs, 400u);
    EXPECT_EQ(r.hrEpochs, 400u);
    EXPECT_EQ(r.wornPct, 100);
    EXPECT_EQ(r.plausiblePct, 100);
}

} // namespace
