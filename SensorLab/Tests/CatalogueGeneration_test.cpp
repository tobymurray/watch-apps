/**
 ******************************************************************************
 * @file    CatalogueGeneration_test.cpp
 * @brief   The generated type table, and the claim ids nothing may rename.
 ******************************************************************************
 *
 * `SensorTypeTable.generated.hpp` is committed rather than generated at build
 * time, because the TouchGFX simulator's Makefile has no place to run a
 * generator and requiring python3 for every build would be worse than a
 * checked-in artifact. The cost is that the file can fall behind the SDK it was
 * generated from, and the whole reason it is generated is that
 * `Docs/SensorsLayer.md` already did exactly that.
 *
 * So there is a ctest -- `sensorlab-catalogue-current` in `CMakeLists.txt` --
 * that re-runs `Tools/gen_catalogue.py --check` against the SDK the tests were
 * configured with. A stale table is a red test rather than a silently wrong
 * answer.
 *
 * This file covers the half that a re-run cannot: that the *shape* of the table
 * is what the rest of the app assumes, and that no claim_id has been renamed.
 * A renamed claim_id silently breaks every firmware comparison spanning the
 * rename, which is the one failure in this project that would not look like a
 * failure.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "Catalogue/Catalogue.hpp"

using namespace SensorLab::Catalogue;

// ---------------------------------------------------------------------------
// The table's shape
// ---------------------------------------------------------------------------

TEST(TypeTable, DeclaresThirtySevenTypesAndTwentyNineParsers)
{
    // If either of these changes, the SDK's sensor surface changed and this is
    // the first thing that should say so.
    EXPECT_EQ(kTypeCount, 37u);
    EXPECT_EQ(kParserCount, 29u);
}

TEST(TypeTable, FiveTypesShipNoParser)
{
    // `MAGNETIC_FIELD`, `HEART_BEAT`, `GESTURE_RECOGNITION`, `PPG`, `ECG`. For
    // these, a measured frame layout is the only description that exists
    // anywhere -- publishing one is a genuine contribution to the SDK's
    // documentation.
    EXPECT_EQ(kTypesWithoutParser, 5u);

    size_t counted = 0;
    for (size_t i = 0; i < kTypeCount; i++) {
        if (kTypes[i].parser == kNoParser) { counted++; }
    }
    EXPECT_EQ(counted, kTypesWithoutParser);
}

TEST(TypeTable, SixTypesAreMissingFromTheSdksOwnDocumentation)
{
    // Finding number one, and the reason this table is generated rather than
    // typed: `HEART_RATE_EX` (0x43), `STEP_COUNTER_DAILY` (0x52),
    // `RUNNING_CADENCE` (0x53), `FLOOR_COUNTER_DAILY` (0x61), `ACTIVITY_TIME`
    // (0xE0) and `GRADE` (0x150).
    EXPECT_EQ(kTypesMissingFromDoc, 6u);

    std::set<uint32_t> missing;
    for (size_t i = 0; i < kTypeCount; i++) {
        if (kTypes[i].missingFromDoc) { missing.insert(kTypes[i].value); }
    }
    EXPECT_EQ(missing,
              (std::set<uint32_t>{ 0x43, 0x52, 0x53, 0x61, 0xE0, 0x150 }));
}

TEST(TypeTable, OnlyHeartRateExAcceptsAWiderFrameThanItDeclares)
{
    // 28 of 29 parsers test the field count for exact equality, so a single
    // appended field silently invalidates every sample. `HeartRateEx` uses `>=`
    // deliberately, so a future kernel can extend the frame without breaking
    // apps. **That asymmetry is a conformance finding in its own right** -- see
    // Docs/FINDINGS.md.
    size_t atLeast = 0;
    for (size_t i = 0; i < kParserCount; i++) {
        if (kParsers[i].validity == Validity::AtLeast) {
            atLeast++;
            EXPECT_STREQ(kParsers[i].cls, "HeartRateEx");
        }
    }
    EXPECT_EQ(atLeast, 1u);
}

TEST(TypeTable, GpsLocationIsTheOneParserThatReadsAFieldBeforeCheckingTheCount)
{
    // `isDataValid()` evaluates `mData.u[Field::COORDS_VALID] <= 1` before
    // `mData.getFieldCount() == Field::COUNT`. `&&` short-circuits left to
    // right, so on a one-field frame that is an out-of-bounds read -- and
    // `DataView`'s bounds assert is compiled out at -Os with NDEBUG.
    //
    // The profiler is the first thing on this platform that will ever meet a
    // frame that does not match its parser, which is exactly the input the
    // parsers were not written for.
    size_t offenders = 0;
    for (size_t i = 0; i < kParserCount; i++) {
        if (kParsers[i].readsBeforeCount) {
            offenders++;
            EXPECT_STREQ(kParsers[i].cls, "GpsLocation");
        }
    }
    EXPECT_EQ(offenders, 1u);
}

TEST(TypeTable, EightParsersRangeCheckAValueAsWellAsTheWidth)
{
    // The implementation prompt said three. It is eight, and the difference
    // matters: for `WristMotion` and `StepDetector` the check is *equality to
    // one*, so a frame carrying zero reads as invalid -- which for an event
    // sensor whose only field is a flag makes `isDataValid()` and "the event
    // happened" the same predicate. Both parsers' `get...()` returns
    // `isDataValid()` despite doc comments promising a count.
    std::set<std::string> ranged;
    for (size_t i = 0; i < kParserCount; i++) {
        if (kParsers[i].rangeChecked) { ranged.insert(kParsers[i].cls); }
    }
    EXPECT_EQ(ranged, (std::set<std::string>{
                          "ActivityRecognition", "BatteryCharging",
                          "BatteryLevel", "GpsLocation", "MotionDetect",
                          "StepDetector", "Touch", "WristMotion" }));
}

TEST(TypeTable, HeartRateExsSourceIsTheOnlyFloatEncodedEnumOnThePlatform)
{
    // Every other enum field is read through `.u`. `HeartRateEx::getSource()`
    // reads `mData.f[SOURCE]` and casts the *float* to a uint8_t, which only
    // works if the kernel writes 1.0f rather than the integer 1. Ledger row S8's
    // 30 169 optical readings against zero unattributed is the measurement that
    // says it does -- so the parser is right and the type comment ("Which source
    // was chosen (Source)") is what misleads.
    const size_t hrex = typeIndexOf(0x43);
    ASSERT_LT(hrex, kTypeCount);
    ASSERT_NE(kTypes[hrex].parser, kNoParser);
    const ParserSpec &p = kParsers[kTypes[hrex].parser];
    ASSERT_EQ(p.fieldCount, 7u);
    EXPECT_STREQ(p.fields[2].name, "SOURCE");
    EXPECT_EQ(p.fields[2].kind, FieldKind::Float);

    // ...whereas MOTION_DETECT's is a u32, like every other enum.
    const size_t motion = typeIndexOf(0xB0);
    ASSERT_LT(motion, kTypeCount);
    const ParserSpec &m = kParsers[kTypes[motion].parser];
    EXPECT_EQ(m.fields[0].kind, FieldKind::U32);
}

TEST(TypeTable, ThreeParsersServeTwoTypesEach)
{
    // The daily variants share their since-boot counterpart's frame, and
    // `Activity` serves both halves of the 0xE0/0xE1 split. Anything else would
    // mean the generator's hand-maintained map has drifted.
    size_t shared = 0;
    for (size_t p = 0; p < kParserCount; p++) {
        size_t users = 0;
        for (size_t t = 0; t < kTypeCount; t++) {
            if (kTypes[t].parser == p) { users++; }
        }
        EXPECT_LE(users, 2u) << kParsers[p].cls;
        if (users == 2) { shared++; }
    }
    EXPECT_EQ(shared, 3u);
}

TEST(TypeTable, EveryTypeValueIsDistinct)
{
    std::set<uint32_t> seen;
    for (size_t i = 0; i < kTypeCount; i++) {
        EXPECT_TRUE(seen.insert(kTypes[i].value).second)
            << "duplicate type value 0x" << std::hex << kTypes[i].value;
    }
}

TEST(TypeTable, TypeIndexOfFindsEveryTypeAndRejectsOthers)
{
    for (size_t i = 0; i < kTypeCount; i++) {
        EXPECT_EQ(typeIndexOf(kTypes[i].value), i);
    }
    EXPECT_EQ(typeIndexOf(0x9999), kTypeCount);
    // The two aliases the generator skipped are not separate types: `ACTIVITY`
    // is `ACTIVITY_TIME_DAILY` and `HEART_RATE_METRICS` is
    // `HEART_RATE_METRICS_DAILY`. Their values resolve to the canonical name.
    ASSERT_LT(typeIndexOf(0xE1), kTypeCount);
    EXPECT_STREQ(kTypes[typeIndexOf(0xE1)].name, "ACTIVITY_TIME_DAILY");
    ASSERT_LT(typeIndexOf(0x42), kTypeCount);
    EXPECT_STREQ(kTypes[typeIndexOf(0x42)].name, "HEART_RATE_METRICS_DAILY");
}

// ---------------------------------------------------------------------------
// The claim index space
// ---------------------------------------------------------------------------

TEST(Catalogue, EveryClaimIndexDecomposesBackToItself)
{
    for (size_t t = 0; t < kTypeCount; t++) {
        for (size_t m = 0; m < kPerTypeMetrics; m++) {
            const auto metric = static_cast<Metric>(m);
            const size_t idx  = claimIndex(t, metric);
            const ClaimRef r  = describe(idx);
            ASSERT_EQ(r.kind, ClaimRef::Kind::PerType) << idx;
            EXPECT_EQ(r.typeIdx, t);
            EXPECT_EQ(static_cast<size_t>(r.metric), m);
            EXPECT_EQ(r.layer, metricLayer(metric));
        }
        for (uint8_t f = 0; f < fieldSlots(t); f++) {
            for (size_t m = 0; m < kFieldMetrics; m++) {
                const size_t idx = claimIndex(t, f, static_cast<FieldMetric>(m));
                const ClaimRef r = describe(idx);
                ASSERT_EQ(r.kind, ClaimRef::Kind::PerField) << idx;
                EXPECT_EQ(r.typeIdx, t);
                EXPECT_EQ(r.fieldIdx, f);
                EXPECT_EQ(static_cast<size_t>(r.fieldMetric), m);
                EXPECT_EQ(r.layer, Layer::Value);
            }
        }
    }
    for (size_t b = 0; b < kBespokeCount; b++) {
        const ClaimRef r = describe(bespokeClaimIndex(b));
        ASSERT_EQ(r.kind, ClaimRef::Kind::Bespoke);
        EXPECT_EQ(r.bespokeIdx, b);
        EXPECT_EQ(r.layer, kBespoke[b].layer);
    }
}

TEST(Catalogue, NoTwoClaimsShareAnId)
{
    // A collision would make two different measurements the same row in every
    // diff for ever.
    std::set<std::string> ids;
    char buf[kClaimIdMax];
    for (size_t i = 0; i < claimCount(); i++) {
        ASSERT_GT(formatClaimId(buf, sizeof(buf), i), 0u) << "index " << i;
        EXPECT_TRUE(ids.insert(buf).second) << "duplicate claim_id " << buf;
    }
    EXPECT_EQ(ids.size(), claimCount());
}

TEST(Catalogue, ClaimIdsAreScopeThenLayerThenMetric)
{
    char buf[kClaimIdMax];

    formatClaimId(buf, sizeof(buf), claimIndex(0, Metric::DtMs));
    EXPECT_STREQ(buf, "0x10.timing.dt_ms");

    formatClaimId(buf, sizeof(buf), claimIndex(0, 0, FieldMetric::Lsb));
    EXPECT_STREQ(buf, "0x10.value.f0_lsb");

    // A platform-scoped claim, which belongs to no sensor.
    bool found = false;
    for (size_t b = 0; b < kBespokeCount; b++) {
        if (kBespoke[b].scope == kPlatformScope) {
            formatClaimId(buf, sizeof(buf), bespokeClaimIndex(b));
            EXPECT_EQ(std::string(buf).compare(0, 9, "platform."), 0) << buf;
            found = true;
        }
    }
    EXPECT_TRUE(found) << "at least one claim is not about a single sensor";
}

TEST(Catalogue, ClaimIdsUseTheValueNotTheEnumeratorName)
{
    // A type renamed upstream keeps its value, and the value is what the kernel
    // dispatches on. Keying on the name would break every historical comparison
    // the day UNA tidied an enumerator.
    char buf[kClaimIdMax];
    const size_t grade = typeIndexOf(0x150);
    ASSERT_LT(grade, kTypeCount);
    formatClaimId(buf, sizeof(buf), claimIndex(grade, Metric::DefaultResolves));
    EXPECT_STREQ(buf, "0x150.existence.default_resolves");
}

TEST(Catalogue, EveryClaimHasAMethodIdNamingTheProbeThatWouldSettleIt)
{
    // A row that cannot say how it would be measured does not get written.
    char buf[kMethodIdMax];
    for (size_t i = 0; i < claimCount(); i++) {
        ASSERT_GT(formatMethodId(buf, sizeof(buf), i), 0u) << "index " << i;
        // "P<n>." followed by something.
        EXPECT_EQ(buf[0], 'P');
        EXPECT_NE(std::string(buf).find('.'), std::string::npos) << buf;
        EXPECT_EQ(std::string(buf).find(".?"), std::string::npos)
            << "unnamed method at index " << i << ": " << buf;
    }
}

TEST(Catalogue, EveryMetricHasAUnitOrIsDeliberatelyDimensionless)
{
    // Not a tautology: the point is that the unit lives in the catalogue rather
    // than in the measurement, so a number whose unit came from the run -- and
    // which therefore nobody could check -- is impossible by construction.
    for (size_t m = 0; m < kPerTypeMetrics; m++) {
        EXPECT_NE(metricUnit(static_cast<Metric>(m)), nullptr);
    }
    EXPECT_STREQ(metricUnit(Metric::DtMs), "ms");
    EXPECT_STREQ(metricUnit(Metric::DeliveredHz), "Hz");
    EXPECT_STREQ(metricUnit(Metric::DefaultResolves), "");
}

TEST(Catalogue, TheTimingDistributionsMinimumIsTenThousandSamples)
{
    // The prompt's figure, and the reason for it: at the ~48 Hz the
    // accelerometer actually delivers, ten thousand samples is about three and a
    // half minutes -- long enough that a p95 is a property of the sensor rather
    // than of the minute it was sampled in.
    EXPECT_EQ(metricMinimumN(Metric::DtMs), 10000u);
    EXPECT_TRUE(metricIsDistribution(Metric::DtMs));
    EXPECT_TRUE(metricIsDistribution(Metric::BatchJitterMs));
    EXPECT_TRUE(metricIsDistribution(Metric::SamplesPerBatch));
    // An existence question needs one attempt and no more.
    EXPECT_EQ(metricMinimumN(Metric::DefaultResolves), 1u);
}

TEST(Catalogue, TheBespokeListNamesEveryUnbuiltTierSoCompletenessIsHonest)
{
    // Layers 7 and 8 are not built in this version. Their claims exist anyway,
    // UNVERIFIED, in the completeness denominator -- which is what stops a
    // Tier 1 profile reading as a finished one.
    size_t physical = 0, consistency = 0;
    for (size_t b = 0; b < kBespokeCount; b++) {
        if (kBespoke[b].layer == Layer::Physical)    { physical++; }
        if (kBespoke[b].layer == Layer::Consistency) { consistency++; }
    }
    EXPECT_GT(physical, 30u);
    EXPECT_GT(consistency, 20u);
}

TEST(Catalogue, TheFourExistenceOnlyProtocolsCarryTheirReason)
{
    // `SPO2`, `HEART_BEAT`, `PPG` and `ECG`. INAPPLICABLE with a stated reason
    // rather than left UNVERIFIED, so a firmware update turns the row back on by
    // itself rather than leaving it written off.
    size_t withReason = 0;
    for (size_t b = 0; b < kBespokeCount; b++) {
        if (kBespoke[b].inapplicableReason != nullptr) {
            withReason++;
            EXPECT_GT(std::string(kBespoke[b].inapplicableReason).size(), 20u)
                << "a reason has to be a sentence somebody can act on";
        }
    }
    EXPECT_EQ(withReason, 4u);
}

TEST(Catalogue, FieldSlotsCoverEveryParsedFieldPlusRoomForTheUndocumentedOnes)
{
    EXPECT_EQ(kParsedFieldTotal, 76u);
    // 76 parsed fields plus eight reserved slots for each of the five types with
    // no parser, whose real width is not knowable until layer 2 measures it.
    EXPECT_EQ(kFieldSlotTotal, kParsedFieldTotal + kTypesWithoutParser * kAssumedFields);

    // The widest parsed frame is `HeartRateEx` at seven, so eight reserved slots
    // records a discovered frame one field wider than anything currently shipped.
    uint8_t widest = 0;
    for (size_t i = 0; i < kParserCount; i++) {
        if (kParsers[i].fieldCount > widest) { widest = kParsers[i].fieldCount; }
    }
    EXPECT_EQ(widest, 7u);
    EXPECT_GT(kAssumedFields, widest);
}

TEST(Catalogue, TheOnlyExpectationTheAppCarriesIsAFieldCountFromAHeader)
{
    // Datasheet figures are looked up, not remembered: they live in
    // Docs/EXPECTED.md on the host, where the citation sits next to the number.
    // A datasheet figure compiled into an app is a figure nobody can check.
    size_t withExpectation = 0;
    for (size_t i = 0; i < claimCount(); i++) {
        const Expectation e = expectationFor(i);
        if (!e.hasValue) { continue; }
        withExpectation++;
        const ClaimRef r = describe(i);
        ASSERT_EQ(r.kind, ClaimRef::Kind::PerType);
        EXPECT_EQ(r.metric, Metric::FieldCount);
        // ...and it cites a file, never a paraphrase.
        ASSERT_NE(e.source, nullptr);
        EXPECT_NE(std::string(e.source).find(".hpp"), std::string::npos)
            << e.source;
    }
    // One per type that ships a parser.
    EXPECT_EQ(withExpectation, kTypeCount - kTypesWithoutParser);
}
