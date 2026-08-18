/**
 * Host tests for the summary metrics.
 *
 * These are the numbers a person reads in the morning and believes, so the two
 * things under test are the arithmetic -- against nights whose shape is known
 * by construction -- and the suppression: a night that failed the worn gate
 * must produce no sleep numbers at all, not zeroes and not a caveat.
 */

#include <gtest/gtest.h>

#include <vector>

#include "Engine/NightSummary.hpp"
#include "NightFixture.hpp"

namespace {

using Engine::kAbsent;
using Engine::NightAnalyser;
using Engine::NightSummary;
using Engine::ScoringInput;
using Engine::SleepWakeScorer;
using Engine::Verdict;
using Engine::WornGate;
using Engine::WornVerdict;

/// A gate result that passed, for tests about the arithmetic rather than the
/// gate.
WornGate::Result passingGate()
{
    WornGate::Result g;
    g.verdict    = WornVerdict::Worn;
    g.hrEvidence = WornGate::HrEvidence::Present;
    return g;
}

WornGate::Result failingGate(WornVerdict v)
{
    WornGate::Result g;
    g.verdict = v;
    return g;
}

/// Verdicts built directly rather than through the scorer, so a summary bug
/// and a scoring bug cannot mask each other.
std::vector<Verdict> verdicts(std::initializer_list<std::pair<size_t, Verdict>> runs)
{
    std::vector<Verdict> v;
    for (const auto &r : runs) {
        v.insert(v.end(), r.first, r.second);
    }
    return v;
}

std::vector<ScoringInput> matching(const std::vector<Verdict> &v)
{
    std::vector<ScoringInput> in;
    for (Verdict x : v) {
        in.push_back(Fixture::epoch(x == Verdict::Sleep ? Fixture::kQuiet
                                                        : Fixture::kActive));
    }
    return in;
}

// -- The arithmetic --------------------------------------------------------------

TEST(NightSummary, AKnownNightYieldsExactlyItsKnownNumbers)
{
    // 20 min awake, 180 asleep, 25 awake, 150 asleep, 15 awake.
    const auto v  = verdicts({ {20, Verdict::Wake},  {180, Verdict::Sleep},
                               {25, Verdict::Wake},  {150, Verdict::Sleep},
                               {15, Verdict::Wake} });
    const auto in = matching(v);

    const auto s = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                          passingGate(), 0);

    ASSERT_TRUE(s.hasSleep);
    EXPECT_EQ(s.timeInBedMin,    390);   // the whole session
    EXPECT_EQ(s.onsetEpoch,      20);
    EXPECT_EQ(s.onsetLatencyMin, 20);
    EXPECT_EQ(s.finalWakeEpoch,  374);   // last sleep epoch: 20+180+25+150-1
    EXPECT_EQ(s.totalSleepMin,   330);   // 180 + 150
    EXPECT_EQ(s.wasoMin,         25);
    EXPECT_EQ(s.awakenings,      1);
    ASSERT_EQ(s.awakeningsListed, 1u);
    EXPECT_EQ(s.awakeningMin[0], 25);
    EXPECT_EQ(s.efficiencyPct,   330 * 100 / 390);
}

TEST(NightSummary, EfficiencyIsAgainstTimeInBedNotAgainstTheSleepPeriod)
{
    // The two are both called sleep efficiency in the wild and the second is
    // systematically higher. Pinning which one this is stops it drifting.
    const auto v  = verdicts({ {60, Verdict::Wake}, {300, Verdict::Sleep} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.timeInBedMin, 360);
    EXPECT_EQ(s.totalSleepMin, 300);
    EXPECT_EQ(s.efficiencyPct, 83);       // 300/360, not 300/300
}

TEST(NightSummary, OnsetNeedsTenSustainedMinutesNotOneQuietOne)
{
    // A single still minute at 22:40 is not falling asleep. Without the run
    // requirement, onset latency would measure how still someone lies while
    // reading.
    const auto v  = verdicts({ {5,  Verdict::Wake},
                               {1,  Verdict::Sleep},   // a fluke quiet minute
                               {30, Verdict::Wake},
                               {200, Verdict::Sleep} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.onsetEpoch, 36) << "onset is the sustained block, not the fluke";
}

TEST(NightSummary, AwakeningsAreCountedAndTheirLengthsListed)
{
    const auto v  = verdicts({ {60, Verdict::Sleep}, {5,  Verdict::Wake},
                               {60, Verdict::Sleep}, {12, Verdict::Wake},
                               {60, Verdict::Sleep}, {3,  Verdict::Wake},
                               {60, Verdict::Sleep} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.awakenings, 3);
    EXPECT_EQ(s.wasoMin, 20);
    ASSERT_EQ(s.awakeningsListed, 3u);
    EXPECT_EQ(s.awakeningMin[0], 5);
    EXPECT_EQ(s.awakeningMin[1], 12);
    EXPECT_EQ(s.awakeningMin[2], 3);
}

TEST(NightSummary, MoreAwakeningsThanFitAreCountedEvenWhenNotListed)
{
    // The list is a screen-space limit. The count and total WASO must stay
    // complete, or a restless night would be under-reported.
    // Opens with a full ten-minute run so onset is epoch 0 and every one of
    // the twenty wake blocks falls between onset and final wake. Opening with
    // a single sleep epoch instead would put the first block *before* onset,
    // where it is correctly neither WASO nor an awakening -- true, but it
    // makes this test about the onset rule rather than about the cap.
    std::vector<Verdict> v(10, Verdict::Sleep);
    for (int i = 0; i < 20; ++i) {
        v.insert(v.end(), 3, Verdict::Wake);
        v.insert(v.end(), 10, Verdict::Sleep);
    }
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.awakenings, 20);
    EXPECT_EQ(s.wasoMin, 60);
    EXPECT_EQ(s.awakeningsListed, Engine::kMaxListedAwakenings);
}

TEST(NightSummary, WakeOutsideTheSleepPeriodIsNotWaso)
{
    // WASO is wake *after sleep onset* and before final wake. The 40 minutes
    // spent reading before sleep and the 20 spent lying in afterwards are
    // neither WASO nor awakenings, and counting them would make every night
    // look broken.
    const auto v  = verdicts({ {40, Verdict::Wake}, {200, Verdict::Sleep},
                               {20, Verdict::Wake} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.wasoMin, 0);
    EXPECT_EQ(s.awakenings, 0);
    EXPECT_EQ(s.onsetLatencyMin, 40);
}

TEST(NightSummary, StillnessIsReportedAlongsideEstimatedSleep)
{
    // The honest pair: totalSleepMin is an estimate biased high, stillInBedMin
    // is what was actually observed. Both, always -- reporting only the first
    // is the overclaim.
    const auto v  = verdicts({ {200, Verdict::Sleep} });
    auto in = matching(v);
    // Half the epochs genuinely moved, above NightAnalyser::kMovementFloor.
    for (size_t i = 0; i < in.size(); i += 2) {
        in[i].count = NightAnalyser::kMovementFloor + 100;
    }

    const auto s = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                          passingGate(), 0);
    EXPECT_EQ(s.totalSleepMin, 200);
    EXPECT_EQ(s.stillInBedMin, 100);
    EXPECT_EQ(s.movementIndexPct, 50);
}

// -- Unscorable epochs --------------------------------------------------------------

TEST(NightSummary, UnscorableEpochsAreNeitherSleepNorAnAwakening)
{
    // "We do not know" and "the wearer was awake" are different claims. A
    // delivery outage counted as an awakening reports a worse night than
    // happened.
    const auto v  = verdicts({ {100, Verdict::Sleep}, {15, Verdict::Unscorable},
                               {100, Verdict::Sleep} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.unscorable, 15u);
    EXPECT_EQ(s.awakenings, 0)   << "an outage is not an awakening";
    EXPECT_EQ(s.wasoMin,    0)   << "an outage is not wake";
    EXPECT_EQ(s.totalSleepMin, 200);
}

TEST(NightSummary, UnscorableEpochsBreakTheOnsetRun)
{
    // Treating them as sleep would let a delivery outage at 23:05 declare
    // sleep onset.
    const auto v  = verdicts({ {5, Verdict::Sleep}, {5, Verdict::Unscorable},
                               {5, Verdict::Sleep}, {200, Verdict::Sleep} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_EQ(s.onsetEpoch, 10) << "the run restarts after the outage";
}

// -- Suppression: the honesty contract -----------------------------------------------

TEST(NightSummary, AFailedWornGateSuppressesEveryLastSleepNumber)
{
    // THE test in this file. A watch on a nightstand scores a flawless night,
    // so the gate has to leave nothing behind that a caller could print.
    const auto v  = verdicts({ {400, Verdict::Sleep} });
    const auto in = matching(v);

    for (WornVerdict bad : { WornVerdict::NotWorn, WornVerdict::Uncertain }) {
        const auto s = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                              failingGate(bad), 0);

        EXPECT_FALSE(s.hasSleep);
        EXPECT_EQ(s.worn, bad);
        EXPECT_EQ(s.totalSleepMin,    kAbsent);
        EXPECT_EQ(s.timeInBedMin,     kAbsent);
        EXPECT_EQ(s.onsetEpoch,       kAbsent);
        EXPECT_EQ(s.onsetLatencyMin,  kAbsent);
        EXPECT_EQ(s.finalWakeEpoch,   kAbsent);
        EXPECT_EQ(s.wasoMin,          kAbsent);
        EXPECT_EQ(s.awakenings,       kAbsent);
        EXPECT_EQ(s.efficiencyPct,    kAbsent);
        EXPECT_EQ(s.stillInBedMin,    kAbsent);
        EXPECT_EQ(s.movementIndexPct, kAbsent);
        EXPECT_EQ(s.awakeningsListed, 0u);
    }
}

TEST(NightSummary, AbsentIsNotZeroForANightWithNoSleep)
{
    // A session that ran but never contained ten consecutive sleep minutes is
    // a real outcome -- someone who did not sleep. Reporting zero minutes reads
    // as a measurement; absent reads as what it is.
    const auto v  = verdicts({ {300, Verdict::Wake} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), 0);

    EXPECT_FALSE(s.hasSleep);
    EXPECT_EQ(s.timeInBedMin, 300) << "time in bed was still measured";
    EXPECT_EQ(s.totalSleepMin, kAbsent);
    EXPECT_EQ(s.onsetEpoch, kAbsent);
}

TEST(NightSummary, HeartRateIsReportedEvenForANightThatFailedTheGate)
{
    // HR is a measurement of the sensor, not a claim about sleep. A night that
    // failed still usefully shows whether the optical path produced anything.
    auto v  = verdicts({ {400, Verdict::Sleep} });
    auto in = matching(v);
    for (size_t i = 0; i < in.size(); ++i) {
        in[i].hrMeanX10 = static_cast<int16_t>(600 - (i < 200 ? i : 400 - i));
    }

    const auto s = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                          failingGate(WornVerdict::NotWorn), 0);

    EXPECT_FALSE(s.hasSleep);
    EXPECT_EQ(s.hrMinX10, 400);
    EXPECT_EQ(s.hrMinEpoch, 200);
    EXPECT_EQ(s.hrEpochs, 400u);
}

TEST(NightSummary, InterruptionFlagsSurviveIntoTheSummary)
{
    const uint16_t flags = Engine::Interruption::kCharging |
                           Engine::Interruption::kResumed;
    const auto v  = verdicts({ {300, Verdict::Sleep} });
    const auto in = matching(v);
    const auto s  = NightAnalyser::analyse(in.data(), v.data(), v.size(),
                                           passingGate(), flags);

    EXPECT_EQ(s.interruption, flags);
}

} // namespace
