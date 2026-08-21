/**
 ******************************************************************************
 * @file    Evidence_test.cpp
 * @brief   The rules that keep the document defensible, as tests.
 ******************************************************************************
 *
 * Section 9 of the implementation prompt is a list of non-negotiables. Most of
 * them are properties of one function -- `ClaimStore::record()` -- and this file
 * is that list turned into assertions, so that violating one is a red test
 * rather than a review comment somebody might not make.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <cstring>

#include "Catalogue/Catalogue.hpp"
#include "Evidence/ClaimStore.hpp"

using namespace SensorLab;
using namespace SensorLab::Evidence;

namespace
{

Measurement value(float v, uint32_t n, Verdict verdict = Verdict::Confirmed,
                  Source source = Source::Measured)
{
    Measurement m {};
    m.hasValue = true;
    m.value    = v;
    m.n        = n;
    m.verdict  = verdict;
    m.source   = source;
    return m;
}

/// A claim with a low minimum, for testing everything that is not the
/// minimum-n rule. `existence.default_resolves` needs n = 1.
size_t cheapClaim(size_t typeIdx = 0)
{
    return Catalogue::claimIndex(typeIdx, Catalogue::Metric::DefaultResolves);
}

/// A claim with a high minimum: `timing.dt_ms` needs ten thousand samples.
size_t expensiveClaim(size_t typeIdx = 0)
{
    return Catalogue::claimIndex(typeIdx, Catalogue::Metric::DtMs);
}

} // namespace

// ---------------------------------------------------------------------------
// No claim without a run
// ---------------------------------------------------------------------------

TEST(ClaimStore, ARowWithNoRunIdCannotBeWritten)
{
    ClaimStore store;
    // setRunId has not been called, so no run is open.
    EXPECT_EQ(store.record(cheapClaim(), value(1.0f, 1), 1000, 42),
              Verdict::Unverified);
    EXPECT_EQ(store.at(cheapClaim()).verdict, Verdict::Unverified);
    EXPECT_EQ(store.at(cheapClaim()).runId, 0u);
}

TEST(ClaimStore, EveryStoredRowCarriesItsRunAndBothClocks)
{
    ClaimStore store;
    store.setRunId(7);
    store.record(cheapClaim(), value(1.0f, 1), 123456, 1755553500);

    const Claim &c = store.at(cheapClaim());
    EXPECT_EQ(c.runId, 7u);
    EXPECT_EQ(c.uptimeMs, 123456u);
    EXPECT_EQ(c.wallUtc, 1755553500);
}

// ---------------------------------------------------------------------------
// No distribution below its minimum n
// ---------------------------------------------------------------------------

TEST(ClaimStore, AValueBelowItsMinimumNStaysUnverifiedAndSaysWhy)
{
    ClaimStore store;
    store.setRunId(1);

    // `timing.dt_ms` requires ten thousand samples. Promoting a p95 off 148 of
    // them would be a lie with a pedigree.
    const Verdict v = store.record(expensiveClaim(), value(20.4f, 148), 1000, 1);
    EXPECT_EQ(v, Verdict::Unverified);

    const Claim &c = store.at(expensiveClaim());
    EXPECT_EQ(c.verdict, Verdict::Unverified);
    EXPECT_EQ(c.note, Note::BelowMinimumN);
    // The value is still kept, so the screen can show progress and the report
    // can say "148 of 10 000" -- displaying how much longer is needed is a
    // feature. It is the *verdict* that is not promoted, because the verdict is
    // the only thing a reader acts on.
    EXPECT_TRUE(c.hasValue());
    EXPECT_FLOAT_EQ(c.value, 20.4f);
    EXPECT_EQ(c.n, 148u);
}

TEST(ClaimStore, TheSameValueAtTheMinimumIsPromoted)
{
    ClaimStore store;
    store.setRunId(1);
    EXPECT_EQ(store.record(expensiveClaim(), value(20.4f, 10000), 1000, 1),
              Verdict::Confirmed);
    EXPECT_EQ(store.at(expensiveClaim()).note, Note::None);
}

TEST(ClaimStore, AnInapplicableVerdictIsNotSubjectToTheMinimum)
{
    ClaimStore store;
    store.setRunId(1);
    Measurement m {};
    m.n       = 0;
    m.verdict = Verdict::Inapplicable;
    m.note    = Note::EventSensorNoDt;
    store.record(expensiveClaim(), m, 1000, 1);
    EXPECT_EQ(store.at(expensiveClaim()).verdict, Verdict::Inapplicable);
}

TEST(ClaimStore, ARefutationIsNotSubjectToTheMinimumEither)
{
    // A single observation can refute. "This type resolves no driver" needs one
    // attempt, and so does "this frame has four fields where the parser expects
    // two" -- SleepLab's row S4 was settled in one two-minute hardware run.
    ClaimStore store;
    store.setRunId(1);
    EXPECT_EQ(store.record(expensiveClaim(), value(0.0f, 1, Verdict::Refuted),
                           1000, 1),
              Verdict::Refuted);
}

// ---------------------------------------------------------------------------
// Reading cannot confirm
// ---------------------------------------------------------------------------

TEST(ClaimStore, ASpecReadCannotProduceAConfirmedRow)
{
    ClaimStore store;
    store.setRunId(1);

    // Reading settles what the *spec* says; only measurement settles what the
    // device does.
    const Verdict v = store.record(
        cheapClaim(), value(1.0f, 1, Verdict::Confirmed, Source::SpecRead),
        1000, 1);
    EXPECT_EQ(v, Verdict::Unverified);
}

TEST(ClaimStore, ASpecReadCanStillRefute)
{
    // Three rows in SleepLab's ledger were refuted by reading code. Reading is
    // decisive in exactly one direction.
    ClaimStore store;
    store.setRunId(1);
    EXPECT_EQ(store.record(cheapClaim(),
                           value(0.0f, 1, Verdict::Refuted, Source::SpecRead),
                           1000, 1),
              Verdict::Refuted);
}

TEST(ClaimStore, ADerivedValueTopsOutAtLikely)
{
    ClaimStore store;
    store.setRunId(1);
    EXPECT_EQ(store.record(cheapClaim(),
                           value(1.0f, 1, Verdict::Confirmed, Source::Derived),
                           1000, 1),
              Verdict::Likely);
    EXPECT_TRUE(store.at(cheapClaim()).inferred());
}

TEST(ClaimStore, AMeasuredValueIsNotMarkedInferred)
{
    ClaimStore store;
    store.setRunId(1);
    store.record(cheapClaim(), value(1.0f, 1), 1000, 1);
    EXPECT_FALSE(store.at(cheapClaim()).inferred());
}

// ---------------------------------------------------------------------------
// A negative result is a result
// ---------------------------------------------------------------------------

TEST(ClaimStore, ADriverThatDoesNotResolveIsAConfirmedRowNotAnAbsentOne)
{
    // Ledger row S4: `SPO2` does not resolve a driver. That closed a design
    // question permanently, and it could only do so because it was recorded.
    ClaimStore store;
    store.setRunId(1);
    EXPECT_EQ(store.record(cheapClaim(), value(0.0f, 1), 1000, 1),
              Verdict::Confirmed);

    const Claim &c = store.at(cheapClaim());
    EXPECT_TRUE(c.hasValue());
    EXPECT_FLOAT_EQ(c.value, 0.0f);
    EXPECT_TRUE(c.answered());
}

// ---------------------------------------------------------------------------
// INAPPLICABLE is not UNVERIFIED
// ---------------------------------------------------------------------------

TEST(ClaimStore, InapplicableIsExcludedFromTheDenominatorAndUnverifiedIsNot)
{
    ClaimStore store;
    store.setRunId(1);

    const Completeness before = store.completenessForType(0);
    ASSERT_GT(before.applicable, 0u);
    EXPECT_EQ(before.answered, 0u);
    EXPECT_EQ(before.percent(), 0u);

    store.markInapplicable(expensiveClaim(), Note::EventSensorNoDt, 1000, 1);

    const Completeness after = store.completenessForType(0);
    // The denominator shrank by one and the numerator did not move: a dt
    // distribution for an event sensor is not a gap somebody could go and fill.
    EXPECT_EQ(after.applicable, before.applicable - 1);
    EXPECT_EQ(after.answered, 0u);
    EXPECT_EQ(after.inapplicable, 1u);
}

TEST(ClaimStore, AMeasurementDoesNotSilentlyOverwriteAnInapplicableClaim)
{
    ClaimStore store;
    store.setRunId(1);
    store.markInapplicable(expensiveClaim(), Note::NoProducer, 1000, 1);

    // A probe running over a claim that was switched off for a stated reason is
    // a mistake, not an update.
    EXPECT_EQ(store.record(expensiveClaim(), value(20.0f, 20000), 2000, 2),
              Verdict::Inapplicable);
    EXPECT_EQ(store.at(expensiveClaim()).note, Note::NoProducer);
}

TEST(ClaimStore, ClearingInapplicableIsWhatMakesAFirmwareUpdateTurnAProbeBackOn)
{
    // The `SPO2` rows are INAPPLICABLE because no driver resolves. Layer 1
    // clears them the moment one does -- without this, a driver appearing would
    // be invisible in a document that had already written the sensor off.
    ClaimStore store;
    store.setRunId(1);
    store.markInapplicable(expensiveClaim(), Note::NoProducer, 1000, 1);
    ASSERT_EQ(store.at(expensiveClaim()).verdict, Verdict::Inapplicable);

    store.clearInapplicable(expensiveClaim());
    EXPECT_EQ(store.at(expensiveClaim()).verdict, Verdict::Unverified);
    EXPECT_EQ(store.record(expensiveClaim(), value(20.0f, 20000), 2000, 2),
              Verdict::Confirmed);
}

TEST(ClaimStore, ClearingANonInapplicableClaimDoesNotDiscardIt)
{
    ClaimStore store;
    store.setRunId(1);
    store.record(cheapClaim(), value(1.0f, 1), 1000, 1);
    store.clearInapplicable(cheapClaim());
    EXPECT_EQ(store.at(cheapClaim()).verdict, Verdict::Confirmed);
}

// ---------------------------------------------------------------------------
// Conformance
// ---------------------------------------------------------------------------

TEST(ClaimStore, AFieldCountMatchingTheParsersIsMATCHES)
{
    // Type index 0 is ACCELEROMETER, whose parser declares three fields.
    ClaimStore store;
    store.setRunId(1);
    const size_t idx = Catalogue::claimIndex(0, Catalogue::Metric::FieldCount);

    store.record(idx, value(3.0f, 1), 1000, 1);
    EXPECT_EQ(store.at(idx).conformance, Conformance::Matches);
}

TEST(ClaimStore, AFieldCountDifferingFromTheParsersIsDIFFERS)
{
    ClaimStore store;
    store.setRunId(1);
    const size_t idx = Catalogue::claimIndex(0, Catalogue::Metric::FieldCount);

    // A frame one field wider than the parser expects. For 28 of the 29 parsers
    // this silently invalidates every sample, which is why exact comparison
    // rather than a tolerance is correct here.
    store.record(idx, value(4.0f, 1), 1000, 1);
    EXPECT_EQ(store.at(idx).conformance, Conformance::Differs);
}

TEST(ClaimStore, AClaimTheSpecSaysNothingAboutIsNO_CLAIM)
{
    ClaimStore store;
    store.setRunId(1);
    store.record(expensiveClaim(), value(20.4f, 20000), 1000, 1);
    // No spec anywhere states an inter-sample dt for this platform. That is not
    // a failing -- it is what makes the measurement worth publishing.
    EXPECT_EQ(store.at(expensiveClaim()).conformance, Conformance::NoClaim);
}

TEST(ClaimStore, ATypeWithNoParserGetsNO_CLAIMOnItsFieldCount)
{
    // `MAGNETIC_FIELD`, `HEART_BEAT`, `GESTURE_RECOGNITION`, `PPG` and `ECG`
    // ship no parser, so the SDK makes no claim about the frame at all and a
    // measured layout is the only description that exists anywhere.
    const size_t magIdx = Catalogue::typeIndexOf(0x30);
    ASSERT_LT(magIdx, Catalogue::kTypeCount);
    ASSERT_EQ(Catalogue::kTypes[magIdx].parser, Catalogue::kNoParser);

    ClaimStore store;
    store.setRunId(1);
    const size_t idx = Catalogue::claimIndex(magIdx, Catalogue::Metric::FieldCount);
    store.record(idx, value(3.0f, 1), 1000, 1);
    EXPECT_EQ(store.at(idx).conformance, Conformance::NoClaim);
}

// ---------------------------------------------------------------------------
// Spread
// ---------------------------------------------------------------------------

TEST(ClaimStore, ASpreadIsStoredAndRetrievableByClaim)
{
    ClaimStore store;
    store.setRunId(1);

    Measurement m = value(20.4f, 20000);
    m.hasSpread   = true;
    m.spread.p05  = 19.0f;
    m.spread.p50  = 20.4f;
    m.spread.p95  = 22.1f;
    store.record(expensiveClaim(), m, 1000, 1);

    const Spread *s = store.spreadFor(expensiveClaim());
    ASSERT_NE(s, nullptr);
    EXPECT_FLOAT_EQ(s->p05, 19.0f);
    EXPECT_FLOAT_EQ(s->p95, 22.1f);
    EXPECT_EQ(store.spreadsDropped(), 0u);
}

TEST(ClaimStore, RerecordingAClaimReusesItsSpreadSlot)
{
    // Otherwise a soak that promotes a distribution every minute would exhaust
    // the side table in a couple of hours and silently start dropping
    // quantiles.
    ClaimStore store;
    store.setRunId(1);

    Measurement m = value(20.0f, 20000);
    m.hasSpread = true;
    for (int i = 0; i < 100; i++) {
        m.spread.p50 = 20.0f + static_cast<float>(i);
        store.record(expensiveClaim(), m, 1000, 1);
    }
    EXPECT_EQ(store.spreadsUsed(), 1u);
    EXPECT_EQ(store.spreadsDropped(), 0u);
    ASSERT_NE(store.spreadFor(expensiveClaim()), nullptr);
    EXPECT_FLOAT_EQ(store.spreadFor(expensiveClaim())->p50, 119.0f);
}

TEST(ClaimStore, ADroppedSpreadIsCountedRatherThanSilentlyLost)
{
    ClaimStore store;
    store.setRunId(1);

    Measurement m {};
    m.hasValue  = true;
    m.value     = 1.0f;
    m.n         = 1;
    m.hasSpread = true;
    m.verdict   = Verdict::Confirmed;

    // Fill the side table with distinct claims, then keep going past it. Every
    // per-field claim is distinct, and there are 928 of them against a table of
    // 192, so this overflows comfortably.
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        for (uint8_t f = 0; f < Catalogue::fieldSlots(t); f++) {
            for (size_t fm = 0; fm < Catalogue::kFieldMetrics; fm++) {
                store.record(
                    Catalogue::claimIndex(t, f,
                                          static_cast<Catalogue::FieldMetric>(fm)),
                    m, 1000, 1);
            }
        }
    }
    EXPECT_EQ(store.spreadsUsed(), kMaxSpreads);
    EXPECT_GT(store.spreadsDropped(), 0u);
}

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

TEST(ClaimStore, ADescriptorFillingItsFieldIsTruncatedRatherThanRunOn)
{
    // `RequestGetDesc::desc` is `char[32]` with no guarantee of a terminator.
    ClaimStore store;
    const char *thirtyTwo = "01234567890123456789012345678901";
    ASSERT_EQ(std::strlen(thirtyTwo), 32u);
    store.setDescriptor(0, thirtyTwo);
    EXPECT_STREQ(store.descriptor(0), thirtyTwo);
    EXPECT_EQ(std::strlen(store.descriptor(0)), 32u);
}

TEST(ClaimStore, ADescriptorForAnUnknownTypeIsIgnoredRatherThanWrittenOutOfRange)
{
    ClaimStore store;
    store.setDescriptor(Catalogue::kTypeCount + 5, "nope");
    EXPECT_STREQ(store.descriptor(Catalogue::kTypeCount + 5), "");
}

// ---------------------------------------------------------------------------
// Completeness
// ---------------------------------------------------------------------------

TEST(Completeness, AFreshStoreIsZeroPercentCompleteAndNothingIsConfirmed)
{
    ClaimStore store;
    const Completeness c = store.completenessOverall();
    // The denominator was fixed before any measurement was taken, which is what
    // makes the fraction honest from minute one. A profiler that only counted
    // what it attempted would always look finished.
    EXPECT_EQ(c.applicable, Catalogue::claimCount());
    EXPECT_EQ(c.answered, 0u);
    EXPECT_EQ(c.confirmed, 0u);
    EXPECT_EQ(c.percent(), 0u);
}

TEST(Completeness, PerLayerAndPerTypeSumToTheWhole)
{
    ClaimStore store;

    uint32_t byLayer = 0;
    for (size_t i = 1; i <= Catalogue::kLayerCount; i++) {
        byLayer += store.completenessForLayer(
                            static_cast<Catalogue::Layer>(i)).applicable;
    }
    EXPECT_EQ(byLayer, Catalogue::claimCount());

    uint32_t byType = 0;
    for (size_t t = 0; t < Catalogue::kTypeCount; t++) {
        byType += store.completenessForType(t).applicable;
    }
    // Platform-scoped claims belong to no type, so the per-type sum is short by
    // exactly those. Asserted rather than glossed, because a report that
    // presented per-type completeness as the whole picture would be hiding them.
    EXPECT_LT(byType, Catalogue::claimCount());
    EXPECT_EQ(Catalogue::claimCount() - byType,
              store.completenessOverall().applicable - byType);
}

TEST(Completeness, AnAllInapplicableSensorReadsAsCompleteRatherThanZero)
{
    // A sensor with nothing left to measure is complete. That is different from
    // a sensor nothing has been measured on, and conflating them would make a
    // type with no producer look like an outstanding task for ever.
    ClaimStore store;
    store.setRunId(1);

    const size_t total = Catalogue::claimCount();
    for (size_t i = 0; i < total; i++) {
        if (Catalogue::describe(i).typeIdx == 0) {
            store.markInapplicable(i, Note::NoProducer, 1000, 1);
        }
    }
    const Completeness c = store.completenessForType(0);
    EXPECT_EQ(c.applicable, 0u);
    EXPECT_EQ(c.percent(), 100u);
    EXPECT_GT(c.inapplicable, 0u);
}
