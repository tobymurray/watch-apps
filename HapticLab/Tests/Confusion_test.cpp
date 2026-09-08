#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Analysis/Confusion.hpp"

using namespace Haptic;
using namespace Haptic::Analysis;

namespace {

/// Every triple of `n` patterns, each member presented `each` times and
/// answered correctly. Every triple rather than a ring of neighbours, because a
/// pair that never co-occurs has no rate at all and a set built from those is
/// limited by the offering rather than by the wrist.
Tally allTriples(uint8_t n, uint16_t each)
{
    Tally tally;
    tally.patternCount = n;
    for (uint8_t a = 0; a < n; a++) {
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < n; b++) {
            for (uint8_t c = static_cast<uint8_t>(b + 1); c < n; c++) {
                const uint8_t offered[3] = { a, b, c };
                for (uint8_t slot = 0; slot < 3; slot++) {
                    for (uint16_t i = 0; i < each; i++) {
                        tally.addResponse(offered[slot], offered[slot], offered);
                    }
                }
            }
        }
    }
    return tally;
}

/// Adds `count` trials in which `presented` was taken for `mistaken`, offered
/// against a third pattern so the triple is a real one.
void confuse(Tally &tally, uint8_t presented, uint8_t mistaken, uint16_t count)
{
    const uint8_t third =
        static_cast<uint8_t>(presented == 0 || mistaken == 0
                                 ? (presented == 1 || mistaken == 1 ? 2 : 1)
                                 : 0);
    uint8_t offered[3] = { presented, mistaken, third };
    std::sort(offered, offered + 3);
    for (uint16_t i = 0; i < count; i++) {
        tally.addResponse(presented, mistaken, offered);
    }
}

} // namespace

/// Wilson rather than the normal approximation, because the interesting cells
/// are the ones with no confusions in them and the normal approximation gives
/// those a width of zero.
TEST(Wilson, ZeroConfusionsStillHasAnUpperBound)
{
    const Interval interval = wilson(0, 20);
    EXPECT_TRUE(interval.reportable);
    EXPECT_DOUBLE_EQ(interval.point, 0.0);
    EXPECT_DOUBLE_EQ(interval.low, 0.0);
    EXPECT_GT(interval.high, 0.1) << "twenty trials do not prove a rate below 10 percent";
    EXPECT_LT(interval.high, 0.25);
}

TEST(Wilson, TheIntervalNarrowsAsTheTrialsPileUp)
{
    const Interval few  = wilson(2, 20);
    const Interval many = wilson(20, 200);
    EXPECT_NEAR(few.point, many.point, 1e-12);
    EXPECT_LT(many.high - many.low, few.high - few.low);
}

TEST(Wilson, TheIntervalNeverLeavesTheUnitRange)
{
    for (uint16_t n = 1; n <= 200; n++) {
        for (uint16_t k = 0; k <= n; k++) {
            const Interval interval = wilson(k, n);
            EXPECT_GE(interval.low, 0.0) << k << "/" << n;
            EXPECT_LE(interval.high, 1.0) << k << "/" << n;
            EXPECT_LE(interval.low, interval.high) << k << "/" << n;
        }
    }
}

/// A rate with too few trials behind it is not reported at all. Saying how many
/// more are needed is the honest form of the answer.
TEST(Wilson, ARateWithTooFewTrialsRefusesToBeReported)
{
    for (uint16_t n = 0; n < kMinTrialsPerPair; n++) {
        EXPECT_FALSE(wilson(0, n).reportable) << n;
    }
    EXPECT_TRUE(wilson(0, kMinTrialsPerPair).reportable);
}

TEST(Tally, APairIsCountedOnlyOverTheTrialsWhereBothWereOffered)
{
    Tally tally;
    tally.patternCount = 4;

    const uint8_t offered[3] = { 0, 1, 2 };
    tally.addResponse(0, 0, offered);
    tally.addResponse(0, 1, offered);

    EXPECT_EQ(tally.pairFor(0, 1).trials, 2);
    EXPECT_EQ(tally.pairFor(0, 1).confusions, 1);
    EXPECT_EQ(tally.pairFor(0, 2).trials, 2);
    EXPECT_EQ(tally.pairFor(0, 2).confusions, 0);
    // Pattern 3 was never on the screen, so it has no rate at all.
    EXPECT_EQ(tally.pairFor(0, 3).trials, 0);
    EXPECT_FALSE(pairConfusion(tally, 0, 3).reportable);
}

/// A presentation nobody felt is not evidence about any pair. Counting it as a
/// pair trial with no confusion would make every pattern in the triple look
/// more distinguishable for having gone unnoticed.
TEST(Tally, AMissIsNotEvidenceThatAPairIsDistinguishable)
{
    Tally tally;
    tally.patternCount = 3;
    const uint8_t offered[3] = { 0, 1, 2 };

    for (int i = 0; i < 20; i++) {
        tally.addMiss(0, offered);
    }
    EXPECT_EQ(tally.misses[0], 20);
    EXPECT_EQ(tally.presentations[0], 20);
    EXPECT_EQ(tally.pairFor(0, 1).trials, 0) << "a miss was counted as a pair trial";
    EXPECT_FALSE(pairConfusion(tally, 0, 1).reportable);
}

TEST(Tally, AccuracyCountsAMissAgainstThePresentationRatherThanIgnoringIt)
{
    Tally tally;
    tally.patternCount = 3;
    const uint8_t offered[3] = { 0, 1, 2 };
    for (int i = 0; i < 15; i++) {
        tally.addResponse(0, 0, offered);
    }
    for (int i = 0; i < 5; i++) {
        tally.addMiss(0, offered);
    }
    EXPECT_EQ(tally.presentations[0], 20);
    EXPECT_NEAR(accuracy(tally, 0).point, 0.75, 1e-12);
}

TEST(InformationTransfer, APerfectMatrixTransfersTheWholeStimulusEntropy)
{
    const Tally tally = allTriples(8, 3);
    EXPECT_NEAR(informationTransferBits(tally), 3.0, 1e-9);
    EXPECT_NEAR(stimulusEntropyBits(tally), 3.0, 1e-9);
}

TEST(InformationTransfer, AMatrixWithNoRelationshipTransfersNothing)
{
    Tally tally;
    tally.patternCount = 4;
    for (uint8_t presented = 0; presented < 4; presented++) {
        for (uint8_t responded = 0; responded < 4; responded++) {
            const uint8_t offered[3] = { presented, responded,
                                         static_cast<uint8_t>((presented + 1) % 4) };
            for (int i = 0; i < 10; i++) {
                tally.addResponse(presented, responded, offered);
            }
        }
    }
    EXPECT_NEAR(informationTransferBits(tally), 0.0, 1e-9);
}

TEST(InformationTransfer, ConfusionCostsBits)
{
    const Tally clean   = allTriples(8, 4);
    Tally muddled = clean;
    confuse(muddled, 0, 1, 40);
    EXPECT_LT(informationTransferBits(muddled), informationTransferBits(clean));
    EXPECT_GT(informationTransferBits(muddled), 0.0);
}

TEST(InformationTransfer, AnEmptyTallyTransfersNothingRatherThanDividingByZero)
{
    Tally tally;
    tally.patternCount = 6;
    EXPECT_DOUBLE_EQ(informationTransferBits(tally), 0.0);
    EXPECT_DOUBLE_EQ(stimulusEntropyBits(tally), 0.0);
}

/// The answer, in its honest form. A set of eight with no confusions is
/// distinguishable; the same set with one pair muddled loses that pair.
TEST(DistinguishableSet, ACleanSetKeepsEveryoneAndAMuddledPairCostsAMember)
{
    const Tally clean = allTriples(8, 4);
    const DistinguishableSet all = largestDistinguishableSet(clean, 0.10);
    EXPECT_EQ(all.size, 8);
    EXPECT_EQ(all.undersampledPairs, 0);

    Tally muddled = clean;
    // Pattern 0 and 1 taken for each other often enough that the interval's
    // upper bound clears the threshold.
    confuse(muddled, 0, 1, 40);
    const DistinguishableSet fewer = largestDistinguishableSet(muddled, 0.10);
    EXPECT_LT(fewer.size, 8);
    const bool bothIn = (fewer.members & 1u) && (fewer.members & 2u);
    EXPECT_FALSE(bothIn) << "a confused pair stayed in the set";
}

/// The upper bound of the interval decides membership, not the point estimate:
/// one confusion in twelve is not proof of a rate below ten percent.
TEST(DistinguishableSet, AThinlySampledPairIsNotCalledDistinguishable)
{
    Tally thin;
    thin.patternCount = 3;
    const uint8_t offered[3] = { 0, 1, 2 };
    for (int i = 0; i < 12; i++) {
        thin.addResponse(0, 0, offered);
        thin.addResponse(1, 1, offered);
        thin.addResponse(2, 2, offered);
    }
    // Twelve trials a pair, none confused: reportable, but the interval is far
    // too wide to claim a rate under ten percent.
    ASSERT_TRUE(pairConfusion(thin, 0, 1).reportable);
    EXPECT_GT(pairConfusion(thin, 0, 1).high, 0.10);
    EXPECT_EQ(largestDistinguishableSet(thin, 0.10).size, 1)
        << "a thinly sampled pair was called distinguishable";

    // With enough trials the same zero-confusion pair earns its place.
    Tally thick = thin;
    for (int i = 0; i < 200; i++) {
        thick.addResponse(0, 0, offered);
        thick.addResponse(1, 1, offered);
        thick.addResponse(2, 2, offered);
    }
    EXPECT_EQ(largestDistinguishableSet(thick, 0.10).size, 3);
}

TEST(DistinguishableSet, APairWithNoTrialsAtAllIsCountedAsUndersampled)
{
    Tally tally;
    tally.patternCount = 4;
    const uint8_t offered[3] = { 0, 1, 2 };
    for (int i = 0; i < 4; i++) {
        tally.addResponse(0, 0, offered);
    }
    const DistinguishableSet set = largestDistinguishableSet(tally, 0.10);
    EXPECT_GT(set.undersampledPairs, 0);
    EXPECT_EQ(set.size, 1) << "a set was claimed from pairs nobody measured";
}

TEST(DistinguishableSet, TheWorstPairInASetIsWhatAVocabularyEntryCarries)
{
    Tally tally = allTriples(4, 20);
    confuse(tally, 0, 1, 6);

    const DistinguishableSet set = largestDistinguishableSet(tally, 0.50);
    ASSERT_GE(set.size, 2);
    const Interval worst = worstPairIn(tally, set.members, 0);
    EXPECT_TRUE(worst.reportable);
    EXPECT_GT(worst.high, 0.0);
    EXPECT_GE(worst.high, pairConfusion(tally, 0, 2).high);
}

TEST(Shortfall, TheReportSaysHowManyMoreTrialsAPairNeeds)
{
    Tally tally;
    tally.patternCount = 3;
    const uint8_t offered[3] = { 0, 1, 2 };
    for (int i = 0; i < 5; i++) {
        tally.addResponse(0, 0, offered);
    }
    EXPECT_EQ(trialsStillNeeded(tally, 0, 1), kMinTrialsPerPair - 5);
    EXPECT_EQ(trialsStillNeeded(tally, 1, 2), kMinTrialsPerPair);

    for (int i = 0; i < 20; i++) {
        tally.addResponse(0, 0, offered);
    }
    EXPECT_EQ(trialsStillNeeded(tally, 0, 1), 0);
}
