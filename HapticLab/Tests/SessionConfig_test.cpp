#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "SessionConfig.hpp"

using namespace Haptic;

namespace {

SessionConfig parse(const std::string &json, bool &ok)
{
    SessionConfig config;
    ok = parseConfig(json.c_str(), json.size(), config);
    return config;
}

} // namespace

TEST(SessionConfig, TheDefaultsAreTheOnesTheMenuDraws)
{
    SessionConfig config;
    EXPECT_EQ(config.set, SetId::RhythmAndFamily);
    EXPECT_EQ(config.contact, Contact::Wrist);
    EXPECT_EQ(config.masking, Masking::Unmasked);
    // Four passes, because that is what puts twelve trials behind every pair
    // and the analysis refuses a cell with fewer.
    EXPECT_EQ(config.testPasses, kDefaultTestPasses);
    EXPECT_FALSE(config.fromFile);
    EXPECT_STREQ(config.wrist, "unrecorded");
    // A sentinel rather than a plausible room temperature: nobody said.
    EXPECT_EQ(config.ambientTempC, -32768);
}

TEST(SessionConfig, AWholeDocumentIsRead)
{
    bool ok = false;
    const SessionConfig config = parse(R"({
        "schema": 1,
        "seed": 987654321,
        "set": "with-amplitude",
        "test_passes": 2,
        "contact": "table",
        "masking": "masked",
        "wrist": "left",
        "watch_position": "one finger above the ulnar head",
        "strap_tightness": "snug, one finger under",
        "arm_posture": "arm down at the side",
        "ambient_temp_c": 21,
        "sweep_repeats": 3,
        "sweep_first_effect": 1,
        "sweep_last_effect": 60
    })", ok);

    ASSERT_TRUE(ok);
    EXPECT_TRUE(config.fromFile);
    EXPECT_EQ(config.seed, 987654321u);
    EXPECT_EQ(config.set, SetId::WithAmplitude);
    EXPECT_EQ(config.testPasses, 2);
    EXPECT_EQ(config.contact, Contact::Table);
    EXPECT_EQ(config.masking, Masking::Masked);
    EXPECT_STREQ(config.wrist, "left");
    EXPECT_STREQ(config.watchPosition, "one finger above the ulnar head");
    EXPECT_EQ(config.ambientTempC, 21);
    EXPECT_EQ(config.sweepRepeats, 3);
    EXPECT_EQ(config.sweepLastEffect, 60);
}

/// The no-contact control is the cheapest and most decisive control in the
/// design, so the word that selects it has to be read exactly.
TEST(SessionConfig, OnlyTheWordTableSelectsTheNoContactControl)
{
    bool ok = false;
    EXPECT_EQ(parse(R"({"contact":"table"})", ok).contact, Contact::Table);
    EXPECT_EQ(parse(R"({"contact":"wrist"})", ok).contact, Contact::Wrist);
    // Anything else falls back to the wrist, which the menu then draws, so a
    // typo is visible before the run rather than after it.
    EXPECT_EQ(parse(R"({"contact":"tabel"})", ok).contact, Contact::Wrist);
    EXPECT_EQ(parse(R"({"contact":"TABLE"})", ok).contact, Contact::Wrist);
}

TEST(SessionConfig, AFieldTheDocumentDoesNotMentionKeepsItsDefault)
{
    bool ok = false;
    const SessionConfig config = parse(R"({"seed": 5})", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(config.seed, 5u);
    EXPECT_EQ(config.set, SetId::RhythmAndFamily);
    EXPECT_EQ(config.testPasses, kDefaultTestPasses);
    EXPECT_STREQ(config.armPosture, "unrecorded");
}

TEST(SessionConfig, BadJsonLeavesTheConfigUntouched)
{
    SessionConfig config;
    config.seed = 4242;
    const std::string bad = "{\"seed\": ";
    EXPECT_FALSE(parseConfig(bad.c_str(), bad.size(), config));
    EXPECT_EQ(config.seed, 4242u) << "a half-parsed document overwrote the config";
    EXPECT_FALSE(config.fromFile);
}

/// An operator note lands in a CSV field, so a comma in it would break every
/// column after it.
TEST(SessionConfig, ACommaInAnOperatorNoteIsReplacedRatherThanEscaped)
{
    bool ok = false;
    const SessionConfig config =
        parse(R"({"strap_tightness":"snug, one finger under, no gap"})", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(std::string(config.strapTightness).find(','), std::string::npos);
    EXPECT_STREQ(config.strapTightness, "snug  one finger under  no gap");
}

TEST(SessionConfig, AnOverlongNoteIsTruncatedRatherThanOverflowing)
{
    bool ok = false;
    const std::string longNote(200, 'x');
    const SessionConfig config = parse(R"({"watch_position":")" + longNote + R"("})", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(std::strlen(config.watchPosition), kNoteLength);
}

/// A zero pass count or a zero repeat count would produce a run with nothing
/// in it, which is worse than a default.
TEST(SessionConfig, ZeroPassesAndZeroRepeatsAreRaisedToOne)
{
    bool ok = false;
    const SessionConfig config = parse(R"({"test_passes": 0, "sweep_repeats": 0})", ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(config.testPasses, 1);
    EXPECT_EQ(config.sweepRepeats, 1);
}
