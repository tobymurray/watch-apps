/**
 * Host tests for Cole-Kripke scoring and Webster rescoring.
 *
 * The scorer and the rescoring rules are tested separately as well as
 * together, because a rescoring bug that happens to cancel a scoring bug
 * passes an end-to-end test and fails on the first real night.
 */

#include <gtest/gtest.h>

#include <vector>

#include "Engine/SleepWakeScorer.hpp"
#include "NightFixture.hpp"

namespace {

using Engine::ScoringInput;
using Engine::SleepWakeScorer;
using Engine::Verdict;
using Fixture::kActive;
using Fixture::kQuiet;

std::vector<Verdict> scoreAll(const std::vector<ScoringInput> &in)
{
    std::vector<Verdict> out(in.size(), Verdict::Unscorable);
    SleepWakeScorer::score(in.data(), in.size(), out.data());
    return out;
}

size_t countOf(const std::vector<Verdict> &v, Verdict want)
{
    size_t n = 0;
    for (Verdict x : v) {
        if (x == want) { ++n; }
    }
    return n;
}

// -- The raw discriminant ------------------------------------------------------

TEST(ColeKripke, AQuietNightScoresSleepAndABusyOneScoresWake)
{
    std::vector<ScoringInput> quiet, busy;
    Fixture::run(quiet, 60, kQuiet);
    Fixture::run(busy,  60, kActive);

    // Sampled away from the ends, where the window reads past the array as
    // zero activity and biases towards sleep by construction.
    EXPECT_EQ(SleepWakeScorer::rawVerdict(quiet.data(), quiet.size(), 30),
              Verdict::Sleep);
    EXPECT_EQ(SleepWakeScorer::rawVerdict(busy.data(), busy.size(), 30),
              Verdict::Wake);
}

TEST(ColeKripke, TheDiscriminantIsTheDocumentedWeightedSum)
{
    // Recomputed here from the published constants rather than from the
    // implementation, so a typo'd weight fails rather than agreeing with
    // itself.
    std::vector<ScoringInput> in;
    const uint32_t kCounts[] = { 10, 200, 50, 900, 30, 400, 70, 20, 60 };
    for (uint32_t c : kCounts) {
        in.push_back(Fixture::epoch(c));
    }

    const size_t at = 4; // has full history and look-ahead
    float expected = 0.0f;
    for (int k = -SleepWakeScorer::kLookBack; k <= SleepWakeScorer::kLookAhead; ++k) {
        const float a = static_cast<float>(kCounts[at + k]) *
                        SleepWakeScorer::kCountScale;
        expected += SleepWakeScorer::kWeights[k + SleepWakeScorer::kLookBack] * a;
    }
    expected *= SleepWakeScorer::kP;

    EXPECT_NEAR(SleepWakeScorer::discriminant(in.data(), in.size(), at),
                expected, 1e-6f);
}

TEST(ColeKripke, TheWindowReadsPastTheEndsAsZeroRatherThanClamping)
{
    // Clamping would repeat the first epoch four times, letting one movement
    // at lights-out dominate the whole opening window. Zero biases the first
    // four and last two minutes towards sleep, which is documented and
    // accepted -- but it must be zero, not the neighbour.
    std::vector<ScoringInput> in;
    in.push_back(Fixture::epoch(10000));   // one huge epoch
    Fixture::run(in, 10, kQuiet);

    // At index 0 the window covers A-4..A+2; only A0 exists as the big one.
    const float d0 = SleepWakeScorer::discriminant(in.data(), in.size(), 0);
    const float expected = SleepWakeScorer::kP *
        SleepWakeScorer::kWeights[SleepWakeScorer::kLookBack] *
        (10000.0f * SleepWakeScorer::kCountScale) +
        SleepWakeScorer::kP *
        (SleepWakeScorer::kWeights[SleepWakeScorer::kLookBack + 1] +
         SleepWakeScorer::kWeights[SleepWakeScorer::kLookBack + 2]) *
        (static_cast<float>(kQuiet) * SleepWakeScorer::kCountScale);

    EXPECT_NEAR(d0, expected, 1e-4f);
}

// -- Unscorable ----------------------------------------------------------------

TEST(ColeKripke, AnEpochBuiltFromAlmostNoSamplesIsUnscorableNotSleep)
{
    // The failure this guards: a delivery outage integrates to near-zero,
    // which reads as perfect stillness and would score as the soundest sleep
    // of the night. A hole in the data must never be the best-looking part of
    // the record.
    std::vector<ScoringInput> in;
    Fixture::run(in, 20, kQuiet);
    in[10] = Fixture::epoch(0, 550, /*samples=*/3, /*wornPct=*/100);

    const auto v = scoreAll(in);
    EXPECT_EQ(v[10], Verdict::Unscorable);
    EXPECT_EQ(v[9],  Verdict::Sleep);
}

TEST(ColeKripke, AnEpochNotWornIsUnscorableEvenWhenPerfectlyStill)
{
    std::vector<ScoringInput> in;
    Fixture::run(in, 20, kQuiet);
    in[10] = Fixture::epoch(0, 550, Fixture::kGoodSamples, /*wornPct=*/0);

    EXPECT_EQ(scoreAll(in)[10], Verdict::Unscorable);
}

// -- Webster rescoring ----------------------------------------------------------

TEST(Webster, SleepImmediatelyAfterALongWakeIsRescoredAsWake)
{
    // Rule 3: after 15+ minutes of wake, the first 4 minutes of sleep are
    // rescored. The raw scorer over-calls sleep here because the window's
    // look-back is only four epochs deep, and this is what corrects it.
    std::vector<ScoringInput> in;
    Fixture::run(in, 30, kActive);   // a long wake
    Fixture::run(in, 60, kQuiet);    // then sleep

    const auto v = scoreAll(in);

    // The first four sleep epochs after the wake run must be wake.
    for (size_t i = 30; i < 34; ++i) {
        EXPECT_EQ(v[i], Verdict::Wake) << "epoch " << i << " should be rescored";
    }
    EXPECT_EQ(v[40], Verdict::Sleep) << "and the rest of the night is still sleep";
}

TEST(Webster, AShortWakeRescoresLessThanALongOne)
{
    auto rescoredAfter = [](size_t wakeMinutes) {
        std::vector<ScoringInput> in;
        Fixture::run(in, wakeMinutes, kActive);
        Fixture::run(in, 60, kQuiet);
        const auto v = scoreAll(in);

        size_t n = 0;
        for (size_t i = wakeMinutes; i < in.size() && v[i] == Verdict::Wake; ++i) {
            ++n;
        }
        return n;
    };

    // The rules are cumulative thresholds: 4 min -> 1, 10 min -> 3, 15 -> 4.
    // Sampled either side of each threshold rather than at it, because the raw
    // scorer's own transition sits near the boundary and would make an
    // exact-value assertion test two things at once.
    EXPECT_LT(rescoredAfter(5),  rescoredAfter(20));
    EXPECT_LE(rescoredAfter(12), rescoredAfter(20));
}

TEST(Webster, RescoringDoesNotCascadeIntoTheWholeNight)
{
    // The rules are applied against the *original* wake runs. If the wake this
    // pass writes could itself lengthen a run, a night with frequent brief
    // awakenings would unravel into continuous wake -- which is the single
    // most destructive way to get this wrong, because it turns a normal night
    // into a catastrophic-looking one.
    std::vector<ScoringInput> in;
    for (int block = 0; block < 8; ++block) {
        Fixture::run(in, 20, kQuiet);
        Fixture::run(in, 5,  kActive);
    }
    Fixture::run(in, 40, kQuiet);

    const auto v = scoreAll(in);
    const size_t sleep = countOf(v, Verdict::Sleep);

    EXPECT_GT(sleep, in.size() / 2)
        << "a night of 20-minute sleep blocks must still be mostly sleep";
}

TEST(Webster, AShortSleepIslandSurroundedByWakeIsRescored)
{
    // Rule 4: a sleep block of <=6 min with >=10 min of wake on both sides.
    std::vector<ScoringInput> in;
    Fixture::run(in, 25, kActive);
    Fixture::run(in, 4,  kQuiet);    // the island
    Fixture::run(in, 25, kActive);

    const auto v = scoreAll(in);
    for (size_t i = 25; i < 29; ++i) {
        EXPECT_NE(v[i], Verdict::Sleep)
            << "epoch " << i << ": four still minutes between two long wakes"
               " is not a sleep bout";
    }
}

TEST(Webster, ALongSleepBlockSurroundedByWakeSurvives)
{
    // The complement of the previous test, and the one that would catch an
    // over-eager rule: 30 minutes of sleep is a sleep bout however much wake
    // brackets it.
    std::vector<ScoringInput> in;
    Fixture::run(in, 30, kActive);
    Fixture::run(in, 30, kQuiet);
    Fixture::run(in, 30, kActive);

    const auto v = scoreAll(in);
    size_t sleep = 0;
    for (size_t i = 30; i < 60; ++i) {
        if (v[i] == Verdict::Sleep) { ++sleep; }
    }
    EXPECT_GT(sleep, 20u);
}

/// Build a verdict array from (count, verdict) runs.
std::vector<Verdict> pattern(std::initializer_list<std::pair<size_t, Verdict>> runs)
{
    std::vector<Verdict> v;
    for (const auto &r : runs) {
        v.insert(v.end(), r.first, r.second);
    }
    return v;
}

TEST(Webster, ShortBoutsAreJudgedAgainstTheOriginalNightNotACascade)
{
    // Driven straight into rescoreShortBouts rather than through the scorer.
    // The property under test is what the pass does to a *given* pattern of
    // verdicts; reaching that pattern through the scorer would mean building an
    // activity fixture that produces it, which tests the scorer's calibration
    // at the same time and breaks for the wrong reason when a constant moves.
    //
    // The pattern: 25 wake | 6 sleep | 11 wake | 10 sleep | 25 wake.
    //
    //   Bout A (6 min) has >=10 min wake on both sides, so rule 4 removes it.
    //   Bout B (10 min) has only 11 min of wake before it in the ORIGINAL, so
    //   rule 5 -- which needs 20 on both sides -- must leave it alone.
    //
    // A cascading implementation would see 25+6+11 = 42 minutes of wake before
    // B once A had been rewritten, and eat B too.
    auto v = pattern({ {25, Verdict::Wake}, {6,  Verdict::Sleep},
                       {11, Verdict::Wake}, {10, Verdict::Sleep},
                       {25, Verdict::Wake} });

    SleepWakeScorer::rescoreShortBouts(v.data(), v.size());

    for (size_t i = 25; i < 31; ++i) {
        EXPECT_EQ(v[i], Verdict::Wake) << "bout A, epoch " << i
                                       << ": six minutes between two long wakes";
    }
    for (size_t i = 42; i < 52; ++i) {
        EXPECT_EQ(v[i], Verdict::Sleep)
            << "bout B, epoch " << i
            << ": condemned by wake that bout A was rewritten into";
    }
}

TEST(Webster, TheAfterWakeRulesDoNotCascadeEither)
{
    // Same hazard on rules 1-3: if the wake this pass writes could itself
    // lengthen a run, each rescored minute would justify rescoring the next and
    // a night of brief awakenings would unravel into continuous wake.
    //
    // 20 wake | 6 sleep | 20 wake | 6 sleep ... Each sleep block loses its
    // first 4 minutes to rule 3 and keeps its last 2. A cascade would take all.
    auto v = pattern({ {20, Verdict::Wake}, {6, Verdict::Sleep},
                       {20, Verdict::Wake}, {6, Verdict::Sleep},
                       {20, Verdict::Wake}, {6, Verdict::Sleep} });

    SleepWakeScorer::rescoreAfterWake(v.data(), v.size());

    for (size_t start : { size_t(20), size_t(46), size_t(72) }) {
        EXPECT_EQ(v[start + 4], Verdict::Sleep)
            << "block at " << start << " lost more than the rule allows";
        EXPECT_EQ(v[start + 5], Verdict::Sleep);
        EXPECT_EQ(v[start + 0], Verdict::Wake);
        EXPECT_EQ(v[start + 3], Verdict::Wake);
    }
}

TEST(Webster, UnscorableEpochsEndAWakeRunWithoutExtendingIt)
{
    // Counting an outage as wake would manufacture the very precondition the
    // after-wake rules trigger on, and eat the first minutes of real sleep on
    // the far side of it.
    auto v = pattern({ {3,  Verdict::Wake}, {30, Verdict::Unscorable},
                       {20, Verdict::Sleep} });

    SleepWakeScorer::rescoreAfterWake(v.data(), v.size());

    EXPECT_EQ(v[33], Verdict::Sleep)
        << "a 30-minute outage is not 30 minutes of wake";
}

// -- The HRV channel ------------------------------------------------------------

TEST(HrvChannel, SupplyingHrvChangesNothingToday)
{
    // This test is meant to FAIL the day someone wires HRV into the scorer.
    // That day is the day clause A2 of Docs/FEASIBILITY-LEDGER.md -- "no stage
    // hypnogram" -- has to be revisited in writing, with the literature in
    // hand, rather than quietly relabelled. Deleting this test to make a change
    // pass is the mistake it exists to prevent.
    std::vector<ScoringInput> plain;
    Fixture::run(plain, 60, kQuiet);
    Fixture::run(plain, 30, kActive);
    Fixture::run(plain, 60, kQuiet);

    std::vector<ScoringInput> withHrv = plain;
    for (size_t i = 0; i < withHrv.size(); ++i) {
        withHrv[i].rmssdX10 = static_cast<int32_t>(200 + (i % 40) * 10);
    }

    EXPECT_EQ(scoreAll(plain), scoreAll(withHrv))
        << "HRV is plumbed and deliberately unused: there is no firmware "
           "producer for it. If this now fails, update the ledger before "
           "updating the test.";
}

// -- Bounds -----------------------------------------------------------------------

TEST(ColeKripke, ANightLongerThanTheEngineWillScoreIsClampedNotOverrun)
{
    std::vector<ScoringInput> in;
    Fixture::run(in, Engine::kMaxScoringEpochs + 200, kQuiet);
    std::vector<Verdict> out(in.size(), Verdict::Unscorable);

    EXPECT_EQ(SleepWakeScorer::score(in.data(), in.size(), out.data()),
              Engine::kMaxScoringEpochs);
}

TEST(ColeKripke, NullInputsAreRefusedRatherThanDereferenced)
{
    std::vector<Verdict> out(10, Verdict::Unscorable);
    EXPECT_EQ(SleepWakeScorer::score(nullptr, 10, out.data()), 0u);

    std::vector<ScoringInput> in(10);
    EXPECT_EQ(SleepWakeScorer::score(in.data(), 10, nullptr), 0u);
}

} // namespace

// ---------------------------------------------------------------------------
// Webster rules 1-3, on a pattern designed to make the bookkeeping matter
// ---------------------------------------------------------------------------

/// Webster's rules 1-3 are stated against the *original* scoring: a wake run of
/// at least 4, 10 or 15 minutes rescores the first 1, 3 or 4 minutes of the
/// sleep that follows it. The pass therefore has to keep measuring the original
/// wake runs at their original lengths.
///
/// It does not. After rescoring a sleep block that ran out before the rule did,
/// it advances the cursor by the full rule length rather than by what it
/// actually rewrote -- stepping over the first minutes of the next wake run and
/// measuring that run short. A run of 11 measured as 8 drops from rule 2 (three
/// minutes rescored) to rule 1 (one), and the two minutes lost are scored as
/// sleep. The direction is the same direction as actigraphy's own bias, which is
/// what makes it hard to see in an output.
TEST(SleepWakeScorer, RescoringAdvancesOnlyPastWhatItRewrote)
{
    // 15 wake | 1 sleep | 11 wake | 30 sleep.
    //
    // Rule 3 fires on the 15-run and wants 4 minutes of sleep; only 1 is there.
    // The 11-minute wake run then fires rule 2, which wants 3 minutes of the
    // long sleep block.
    std::vector<Engine::Verdict> v;
    auto push = [&v](size_t n, Engine::Verdict x) {
        for (size_t i = 0; i < n; ++i) { v.push_back(x); }
    };
    push(15, Engine::Verdict::Wake);
    push(1,  Engine::Verdict::Sleep);
    push(11, Engine::Verdict::Wake);
    push(30, Engine::Verdict::Sleep);

    const size_t longBlock = 27;   // where the 30-minute sleep block starts
    Engine::SleepWakeScorer::rescoreAfterWake(v.data(), v.size());

    size_t rescored = 0;
    for (size_t i = longBlock; i < v.size(); ++i) {
        if (v[i] == Engine::Verdict::Wake) { ++rescored; }
    }
    EXPECT_EQ(rescored, 3u)
        << "an 11-minute wake run rescored " << rescored
        << " minutes of the sleep after it; rule 2 asks for 3";
}

/// The same bookkeeping, seen from the other side: the epochs the pass steps
/// over must not lose their own identity. Nothing between the two wake runs
/// should end up as sleep.
TEST(SleepWakeScorer, RescoringDoesNotStepOverWakeEpochs)
{
    std::vector<Engine::Verdict> v;
    auto push = [&v](size_t n, Engine::Verdict x) {
        for (size_t i = 0; i < n; ++i) { v.push_back(x); }
    };
    push(15, Engine::Verdict::Wake);
    push(2,  Engine::Verdict::Sleep);
    push(12, Engine::Verdict::Wake);
    push(30, Engine::Verdict::Sleep);

    Engine::SleepWakeScorer::rescoreAfterWake(v.data(), v.size());

    // Everything up to the long sleep block is now wake: the two sleep minutes
    // were rescored and the twelve after them were already wake.
    for (size_t i = 0; i < 29; ++i) {
        EXPECT_EQ(v[i], Engine::Verdict::Wake) << "at " << i;
    }
    size_t rescored = 0;
    for (size_t i = 29; i < v.size(); ++i) {
        if (v[i] == Engine::Verdict::Wake) { ++rescored; }
    }
    EXPECT_EQ(rescored, 3u)
        << "a 12-minute wake run rescored " << rescored
        << " minutes; rule 2 asks for 3";
}
