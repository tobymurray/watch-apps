#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Csv.hpp"

using namespace Haptic;

namespace {

size_t commas(const std::string &line)
{
    return static_cast<size_t>(std::count(line.begin(), line.end(), ','));
}

std::vector<std::string> fields(const std::string &line)
{
    std::vector<std::string> out;
    size_t                   start = 0;
    for (size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == ',' || line[i] == '\n') {
            out.push_back(line.substr(start, i - start));
            start = i + 1;
            if (i < line.size() && line[i] == '\n') {
                break;
            }
        }
    }
    return out;
}

RunHeader header()
{
    RunHeader run {};
    std::snprintf(run.runId, sizeof(run.runId), "20260908T101500");
    run.seed    = 4242;
    run.contact = Contact::Wrist;
    run.masking = Masking::Masked;
    run.set     = SetId::WithAmplitude;
    run.setHash = 0xDEADBEEF;
    return run;
}

} // namespace

/// A row with a different number of fields from its header is a file no reader
/// can parse, and the one place that would show up is an analysis six months
/// later.
TEST(Csv, EveryRowHasExactlyAsManyFieldsAsItsHeader)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::WithAmplitude, patterns);
    const RunHeader run = header();
    const EffectDurations durations = EffectDurations::nominal();

    char line[kMaxRowLength];

    TrialRecord trial {};
    trial.presented = 3;
    trial.offered[0] = 1;
    trial.offered[1] = 3;
    trial.offered[2] = 7;
    trial.noteCount = patterns[3].noteCount;
    for (uint8_t i = 0; i < trial.noteCount; i++) {
        trial.notes[i] = patterns[3].notes[i];
    }
    trial.response       = Response::Slot1;
    trial.responseButton = kButtonSlot1;
    trial.reactionMs     = -17;
    ASSERT_GT(formatTrialRow(line, sizeof(line), run, trial, patterns), 0);
    EXPECT_EQ(commas(line), commas(kTrialsHeader)) << line;

    EventRecord event {};
    event.trialIndex = kNoTrial;
    ASSERT_GT(formatEventRow(line, sizeof(line), run, event), 0);
    EXPECT_EQ(commas(line), commas(kEventsHeader)) << line;

    CommandRecord command {};
    command.trialIndex   = 12;
    command.patternIndex = 2;
    command.noteCount    = patterns[2].noteCount;
    for (uint8_t i = 0; i < command.noteCount; i++) {
        command.notes[i] = patterns[2].notes[i];
    }
    ASSERT_GT(formatCommandRow(line, sizeof(line), run, command, patterns, count), 0);
    EXPECT_EQ(commas(line), commas(kCommandsHeader)) << line;

    for (uint8_t i = 0; i < count; i++) {
        ASSERT_GT(formatPatternRow(line, sizeof(line), run, i, patterns[i], durations), 0);
        EXPECT_EQ(commas(line), commas(kPatternsHeader)) << line;
    }

    ProbeRecord probe {};
    probe.noteCount = 3;
    probe.notes[0].effect  = 1;
    probe.notes[1].pauseMs = 320;
    probe.notes[2].effect  = 1;
    probe.declaredNotesCount = 12;
    std::snprintf(probe.label, sizeof(probe.label), "V5-count-12/0");
    ASSERT_GT(formatProbeRow(line, sizeof(line), run, probe), 0);
    EXPECT_EQ(commas(line), commas(kProbeHeader)) << line;
}

TEST(Csv, EveryRowIsOneLineAndEndsWithOne)
{
    Pattern patterns[kMaxPatterns] {};
    buildSet(SetId::RhythmAndFamily, patterns);
    const RunHeader run = header();

    char        line[kMaxRowLength];
    TrialRecord trial {};
    trial.noteCount = 5;
    for (uint8_t i = 0; i < 5; i++) {
        trial.notes[i] = patterns[8].notes[i];
    }
    const int length = formatTrialRow(line, sizeof(line), run, trial, patterns);
    ASSERT_GT(length, 0);
    ASSERT_LT(static_cast<size_t>(length), sizeof(line)) << "the row was truncated";

    const std::string row(line);
    EXPECT_EQ(row.back(), '\n');
    EXPECT_EQ(std::count(row.begin(), row.end(), '\n'), 1);
}

/// A negative reaction time is real: the wearer can answer before the frame
/// that opened the window reached the panel. The SDK's JSON writer turns a
/// negative into its unsigned reinterpretation on an LP64 host, which is one
/// of the reasons these rows are written by hand.
TEST(Csv, ANegativeReactionTimeStaysNegative)
{
    Pattern patterns[kMaxPatterns] {};
    buildSet(SetId::RhythmAndFamily, patterns);
    const RunHeader run = header();

    char        line[kMaxRowLength];
    TrialRecord trial {};
    trial.noteCount  = 1;
    trial.notes[0].effect = 1;
    trial.reactionMs = -250;
    ASSERT_GT(formatTrialRow(line, sizeof(line), run, trial, patterns), 0);

    const std::vector<std::string> parsed = fields(line);
    const std::vector<std::string> names  = fields(std::string(kTrialsHeader) + "\n");
    ASSERT_EQ(parsed.size(), names.size());

    const auto column = [&](const char *name) {
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == name) {
                return parsed[i];
            }
        }
        return std::string("<missing>");
    };
    EXPECT_EQ(column("reaction_ms"), "-250");
}

TEST(Csv, NoResponseIsWrittenAsMinusOneRatherThanAsAButtonNumber)
{
    Pattern patterns[kMaxPatterns] {};
    buildSet(SetId::RhythmAndFamily, patterns);
    const RunHeader run = header();

    char        line[kMaxRowLength];
    TrialRecord trial {};
    trial.noteCount       = 1;
    trial.notes[0].effect = 1;
    trial.response        = Response::None;
    trial.responseButton  = kNoButton;
    ASSERT_GT(formatTrialRow(line, sizeof(line), run, trial, patterns), 0);

    const std::vector<std::string> parsed = fields(line);
    const std::vector<std::string> names  = fields(std::string(kTrialsHeader) + "\n");
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == "response_button") {
            EXPECT_EQ(parsed[i], "-1");
        }
        if (names[i] == "response") {
            EXPECT_EQ(parsed[i], "none");
        }
    }
}

TEST(Csv, TheNoteListSaysWhichNotesWereEffectsAndWhichWerePauses)
{
    char  out[96];
    Note  notes[4] {};
    notes[0].effect  = 1;
    notes[1].pauseMs = 80;
    notes[2].effect  = 24;
    notes[3].pauseMs = 1270;
    ASSERT_GT(formatNotes(out, sizeof(out), notes, 4), 0);
    EXPECT_STREQ(out, "e1;p80;e24;p1270");

    ASSERT_GE(formatNotes(out, sizeof(out), notes, 0), 0);
    EXPECT_STREQ(out, "");
}

/// A note list with a comma in it would break every column after it.
TEST(Csv, NoFieldEverContainsASeparator)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::WithAmplitude, patterns);
    const RunHeader run = header();
    const EffectDurations durations = EffectDurations::nominal();

    char line[kMaxRowLength];
    for (uint8_t i = 0; i < count; i++) {
        ASSERT_GT(formatPatternRow(line, sizeof(line), run, i, patterns[i], durations), 0);
        const std::vector<std::string> parsed = fields(line);
        for (const std::string &field : parsed) {
            EXPECT_EQ(field.find('"'), std::string::npos) << field;
            EXPECT_EQ(field.find('\n'), std::string::npos) << field;
        }
    }
}

TEST(Csv, TheSchemaVersionIsTheFirstFieldOfEveryRowAndEveryHeader)
{
    for (const char *header : { kTrialsHeader, kEventsHeader, kCommandsHeader, kPatternsHeader,
                                kProbeHeader, kEffectsHeader }) {
        EXPECT_EQ(fields(std::string(header) + "\n").front(), "schema") << header;
    }

    Pattern patterns[kMaxPatterns] {};
    buildSet(SetId::RhythmAndFamily, patterns);
    char        line[kMaxRowLength];
    TrialRecord trial {};
    trial.noteCount       = 1;
    trial.notes[0].effect = 1;
    ASSERT_GT(formatTrialRow(line, sizeof(line), header(), trial, patterns), 0);
    EXPECT_EQ(fields(line).front(), std::to_string(kSchemaVersion));
}

/// A row has to be readable without joining back to `session.json`, which is
/// what the repeated run-level fields are for.
TEST(Csv, EveryRowRepeatsTheRunLevelFieldsItWouldOtherwiseNeedAJoinFor)
{
    Pattern patterns[kMaxPatterns] {};
    buildSet(SetId::WithAmplitude, patterns);
    const RunHeader run = header();

    char        line[kMaxRowLength];
    TrialRecord trial {};
    trial.noteCount       = 1;
    trial.notes[0].effect = 1;
    ASSERT_GT(formatTrialRow(line, sizeof(line), run, trial, patterns), 0);

    const std::string row(line);
    EXPECT_NE(row.find(run.runId), std::string::npos);
    EXPECT_NE(row.find("4242"), std::string::npos) << "the seed is missing";
    EXPECT_NE(row.find("wrist"), std::string::npos);
    EXPECT_NE(row.find("masked"), std::string::npos);
    EXPECT_NE(row.find("with-amplitude"), std::string::npos);
    EXPECT_NE(row.find("DEADBEEF"), std::string::npos) << "the set hash is missing";
}

/// Operator notes at their cap are the longest a real header gets, and the
/// combination that would have overflowed the buffer the first run used.
TEST(SessionJson, TheHeaderStillFitsWithEveryOperatorNoteAtItsCap)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::WithAmplitude, patterns);
    Plan plan {};
    buildPlan(SetId::WithAmplitude, kDefaultTestPasses, 0xFFFFFFFFu, plan);
    const Timing timing = timingFor(patterns, count, EffectDurations::nominal());

    const std::string longest(kNoteLength, 'M');
    SessionMeta meta;
    meta.appVersion     = "0.1.0-99-abcdef1-dirty";
    meta.firmware       = "1.4.0";
    meta.kernelAbi      = 3;
    meta.wrist          = longest.c_str();
    meta.watchPosition  = longest.c_str();
    meta.strapTightness = longest.c_str();
    meta.armPosture     = longest.c_str();
    meta.ambientTempC   = -32768;
    meta.startTick      = 0xFFFFFFFFu;
    meta.startWall      = 0xFFFFFFFFu;
    meta.endTick        = 0xFFFFFFFFu;
    meta.endWall        = 0xFFFFFFFFu;
    meta.endReason      = "stopped-by-kernel";
    meta.capsHit        = "commands.csv";

    RunHeader run = header();
    run.seed = 0xFFFFFFFFu;

    char      body[kSessionJsonBytes];
    const int length = formatSessionJson(body, sizeof(body), run, plan, timing,
                                         EffectDurations::nominal(), meta, 0xFFFFFFFFu,
                                         0xFFFFFFFFu);
    ASSERT_GT(length, 0);
    EXPECT_LT(static_cast<size_t>(length), kSessionJsonBytes)
        << "the worst-case header does not fit the buffer the Recorder uses";
}

TEST(SessionJson, TheHeaderIsValidJsonAndCarriesTheEndReason)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::RhythmAndFamily, patterns);
    Plan plan {};
    buildPlan(SetId::RhythmAndFamily, 1, 4242, plan);
    const Timing timing = timingFor(patterns, count, EffectDurations::nominal());

    SessionMeta meta;
    meta.appVersion = "0.1.0";
    meta.firmware   = "1.4.0";
    meta.kernelAbi  = 3;
    meta.startTick  = 1234;
    meta.startWall  = 1757325300;
    meta.endTick    = 9999;
    meta.endWall    = 1757325999;
    meta.endReason  = "completed";

    char      body[kSessionJsonBytes];
    const int length = formatSessionJson(body, sizeof(body), header(), plan, timing,
                                         EffectDurations::nominal(), meta, 1024, 60000);
    ASSERT_GT(length, 0);
    ASSERT_LT(static_cast<size_t>(length), kSessionJsonBytes) << "the header was truncated";

    const std::string json(body);
    EXPECT_EQ(std::count(json.begin(), json.end(), '{'), 1);
    EXPECT_EQ(std::count(json.begin(), json.end(), '}'), 1);
    EXPECT_NE(json.find("\"end_reason\": \"completed\""), std::string::npos);
    EXPECT_NE(json.find("\"seed\": 4242"), std::string::npos);
    EXPECT_NE(json.find("\"candidate_set_hash\": \"DEADBEEF\""), std::string::npos);
    EXPECT_NE(json.find("\"effect_durations_measured\": false"), std::string::npos);
    EXPECT_NE(json.find("\"pair_trials_lowest\""), std::string::npos);
    // No *number* is fractional, because the watch's newlib may not link `%f`
    // and when it does not it emits nothing at all rather than failing to
    // link. Version strings are strings, so the check strips quoted spans
    // first and then insists nothing with a decimal point is left.
    std::string unquoted;
    bool        inString = false;
    for (char c : json) {
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (!inString) {
            unquoted += c;
        }
    }
    EXPECT_EQ(unquoted.find('.'), std::string::npos) << "a fractional number crept in";
}
