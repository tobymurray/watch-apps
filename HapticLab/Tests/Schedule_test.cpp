#include <gtest/gtest.h>

#include <map>
#include <set>
#include <vector>

#include "Schedule.hpp"

using namespace Haptic;

namespace {

Plan planFor(SetId set, uint8_t passes, uint32_t seed)
{
    Plan plan {};
    buildPlan(set, passes, seed, plan);
    return plan;
}

} // namespace

TEST(Rng, TheSameSeedGivesTheSameStreamAndDifferentSeedsDoNot)
{
    Rng a(1234), b(1234), c(1235);
    for (int i = 0; i < 64; i++) {
        EXPECT_EQ(a.next(), b.next());
    }
    Rng d(1234);
    bool differs = false;
    for (int i = 0; i < 64; i++) {
        differs = differs || d.next() != c.next();
    }
    EXPECT_TRUE(differs);
}

/// The rejection is what stops the low values being favoured, which would bias
/// which triple wins a tie and therefore which pairs get covered.
TEST(Rng, BelowIsUniformEnoughThatNoBucketIsHalfEmpty)
{
    Rng rng(99);
    std::map<uint32_t, int> counts;
    for (int i = 0; i < 30000; i++) {
        counts[rng.below(7)]++;
    }
    ASSERT_EQ(counts.size(), 7u);
    for (const auto &bucket : counts) {
        EXPECT_GT(bucket.second, 3500) << "bucket " << bucket.first;
        EXPECT_LT(bucket.second, 5100) << "bucket " << bucket.first;
    }
}

TEST(Triples, EveryTripleHasThreeDistinctMembersInAscendingOrder)
{
    Rng rng(7);
    Triple triples[kMaxTriples] {};
    const uint16_t count = planTriples(12, 2, rng, triples, kMaxTriples);
    ASSERT_GT(count, 0);
    for (uint16_t t = 0; t < count; t++) {
        EXPECT_LT(triples[t].member[0], triples[t].member[1]);
        EXPECT_LT(triples[t].member[1], triples[t].member[2]);
        EXPECT_LT(triples[t].member[2], 12);
    }
}

/// The answer is built from the pairwise confusion matrix, so a pair offered
/// fewer times than the rest is a cell that refuses to report. The planner runs
/// to the floor rather than to a fixed count, which makes the shortfall zero by
/// construction; the excess is bounded rather than zero, and this is where the
/// bound is measured instead of assumed.
TEST(Triples, NoPairIsEverOfferedFewerTimesThanAskedFor)
{
    /// Measured over this sweep rather than argued: no seed here leaves a pair
    /// more than this many offerings above what was asked for.
    constexpr int kWorstExcess = 3;

    for (uint8_t n : { 12, 16 }) {
        for (uint8_t passes : { 1, 2, 3, 4 }) {
            for (uint32_t seed = 1; seed <= 40; seed++) {
                Rng rng(seed);
                Triple triples[kMaxTriples] {};
                const uint16_t count = planTriples(n, passes, rng, triples, kMaxTriples);
                ASSERT_LE(count, kMaxTriples);

                std::map<std::pair<uint8_t, uint8_t>, int> pairs;
                for (uint8_t a = 0; a < n; a++) {
                    for (uint8_t b = a + 1; b < n; b++) {
                        pairs[{ a, b }] = 0;
                    }
                }
                for (uint16_t t = 0; t < count; t++) {
                    const Triple &triple = triples[t];
                    pairs[{ triple.member[0], triple.member[1] }]++;
                    pairs[{ triple.member[0], triple.member[2] }]++;
                    pairs[{ triple.member[1], triple.member[2] }]++;
                }

                int lowest = INT32_MAX, highest = 0;
                for (const auto &entry : pairs) {
                    lowest  = std::min(lowest, entry.second);
                    highest = std::max(highest, entry.second);
                }
                EXPECT_GE(lowest, passes) << "n=" << int(n) << " passes=" << int(passes)
                                          << " seed=" << seed << ": a pair fell short";
                EXPECT_LE(highest, passes + kWorstExcess)
                    << "n=" << int(n) << " passes=" << int(passes) << " seed=" << seed
                    << ": a pair was offered far more often than the rest";
            }
        }
    }
}

TEST(Plan, EveryTrialPresentsAMemberOfItsOwnTriple)
{
    const Plan plan = planFor(SetId::RhythmAndFamily, 1, 31337);
    ASSERT_GT(plan.trialCount, 0);
    for (uint16_t i = 0; i < plan.trialCount; i++) {
        const Trial  &trial  = plan.trials[i];
        const Triple &triple = plan.triples[trial.triple];
        ASSERT_LT(trial.slot, 3);
        EXPECT_EQ(presentedPattern(plan, i), triple.member[trial.slot]);
    }
}

/// Adaptation to one pattern would otherwise be measured as identification of
/// it.
TEST(Plan, NoPatternIsPresentedTwiceInARow)
{
    for (uint32_t seed : { 1u, 2u, 999u, 0x5A5A5A5Au, 0xFFFFFFFFu, 7u }) {
        const Plan plan = planFor(SetId::WithAmplitude, 2, seed);
        for (uint16_t i = 1; i < plan.trialCount; i++) {
            EXPECT_NE(presentedPattern(plan, i), presentedPattern(plan, i - 1))
                << "seed " << seed << " trial " << i;
        }
    }
}

TEST(Plan, TheBlocksTileTheTrialsExactlyOnce)
{
    const Plan plan = planFor(SetId::RhythmAndFamily, 2, 77);
    ASSERT_GT(plan.blockCount, 1);

    uint16_t seen = 0;
    for (uint8_t b = 0; b < plan.blockCount; b++) {
        EXPECT_EQ(plan.blocks[b].firstTrial, seen) << "block " << int(b) << " does not follow on";
        seen += plan.blocks[b].trialCount;
        for (uint16_t i = plan.blocks[b].firstTrial;
             i < plan.blocks[b].firstTrial + plan.blocks[b].trialCount; i++) {
            EXPECT_EQ(plan.trials[i].block, b);
        }
    }
    EXPECT_EQ(seen, plan.trialCount);
}

TEST(Plan, TheFirstBlockIsTrainingAndTheRestAreTests)
{
    const Plan plan = planFor(SetId::RhythmAndFamily, 1, 5);
    ASSERT_GE(plan.blockCount, 2);
    EXPECT_EQ(plan.blocks[0].kind, BlockKind::Training);
    for (uint8_t b = 1; b < plan.blockCount; b++) {
        EXPECT_EQ(plan.blocks[b].kind, BlockKind::Test) << "block " << int(b);
    }
}

/// Absolute identification of a *learned* set is what is being measured, so
/// every pattern has to be presented with feedback before the test blocks.
TEST(Plan, TrainingPresentsEveryPatternTheSameNumberOfTimes)
{
    const Plan plan = planFor(SetId::WithAmplitude, 1, 11);
    const Block &training = plan.blocks[0];

    std::map<uint8_t, int> seen;
    for (uint8_t p = 0; p < plan.patternCount; p++) {
        seen[p] = 0;
    }
    for (uint16_t i = training.firstTrial; i < training.firstTrial + training.trialCount; i++) {
        seen[presentedPattern(plan, i)]++;
    }
    for (const auto &entry : seen) {
        EXPECT_EQ(entry.second, kTrainingPresentationsEach) << "pattern " << int(entry.first);
    }
}

/// Within a triple every member is the answer once, so no pattern is
/// over-represented as the correct response.
TEST(Plan, EveryTripleIsAskedAboutOncePerMemberInTheTestBlocks)
{
    const Plan plan = planFor(SetId::RhythmAndFamily, 1, 808);

    std::map<uint16_t, std::multiset<uint8_t>> slots;
    for (uint8_t b = 1; b < plan.blockCount; b++) {
        for (uint16_t i = plan.blocks[b].firstTrial;
             i < plan.blocks[b].firstTrial + plan.blocks[b].trialCount; i++) {
            slots[plan.trials[i].triple].insert(plan.trials[i].slot);
        }
    }
    for (const auto &entry : slots) {
        EXPECT_EQ(entry.second, (std::multiset<uint8_t> { 0, 1, 2 }))
            << "triple " << entry.first << " was not asked about once per member";
    }
}

TEST(Plan, TheWholePlanIsAPureFunctionOfTheSetThePassesAndTheSeed)
{
    const Plan a = planFor(SetId::WithAmplitude, 2, 4242);
    const Plan b = planFor(SetId::WithAmplitude, 2, 4242);
    const Plan c = planFor(SetId::WithAmplitude, 2, 4243);

    ASSERT_EQ(a.trialCount, b.trialCount);
    for (uint16_t i = 0; i < a.trialCount; i++) {
        EXPECT_EQ(presentedPattern(a, i), presentedPattern(b, i)) << "trial " << i;
    }

    bool differs = a.trialCount != c.trialCount;
    for (uint16_t i = 0; i < a.trialCount && i < c.trialCount && !differs; i++) {
        differs = presentedPattern(a, i) != presentedPattern(c, i);
    }
    EXPECT_TRUE(differs) << "a different seed produced the same order";
}

TEST(Plan, PairBalanceReportsWhatThePlanActuallyDid)
{
    const Plan plan = planFor(SetId::RhythmAndFamily, 2, 1);
    uint16_t lowest = 0, highest = 0;
    pairBalance(plan, lowest, highest);
    EXPECT_GE(lowest, 2 * kTrialsPerTripleUse) << "a pair fell below what two passes asks for";
    // The excess is bounded rather than zero; `NoPairIsEverOfferedFewerTimes`
    // is where the bound is measured.
    EXPECT_LE(highest - lowest, 3 * kTrialsPerTripleUse);

    uint16_t recomputedLowest = UINT16_MAX;
    for (uint8_t a = 0; a < plan.patternCount; a++) {
        for (uint8_t b = a + 1; b < plan.patternCount; b++) {
            recomputedLowest = std::min(recomputedLowest, plan.pairTrialsFor(a, b));
            EXPECT_EQ(plan.pairTrialsFor(a, b), plan.pairTrialsFor(b, a));
        }
    }
    EXPECT_EQ(lowest, recomputedLowest);
}

/// The floor the analysis refuses below is twelve trials a pair, and the
/// default pass count exists to clear it. A run that cannot answer is worse
/// than a shorter one.
TEST(Plan, AtTheDefaultPassCountEveryPairHasEnoughTrialsToBeReported)
{
    constexpr uint16_t kAnalysisFloor = 12;
    for (SetId set : { SetId::RhythmAndFamily, SetId::WithAmplitude }) {
        const Plan plan = planFor(set, kDefaultTestPasses, 4242);
        for (uint8_t a = 0; a < plan.patternCount; a++) {
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < plan.patternCount; b++) {
                EXPECT_GE(plan.pairTrialsFor(a, b), kAnalysisFloor)
                    << setName(set) << " pair " << int(a) << "/" << int(b);
            }
        }
    }
}

TEST(Plan, NothingOverflowsTheFixedCapacities)
{
    const Plan plan = planFor(SetId::WithAmplitude, kDefaultTestPasses, 3);
    EXPECT_LE(plan.tripleCount, kMaxTriples);
    EXPECT_LE(plan.trialCount, kMaxTrials);
    EXPECT_LE(plan.blockCount, kMaxBlocks);
}
