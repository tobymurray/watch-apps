#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"

#include "Recorder.hpp"
#include "SessionConfig.hpp"

using namespace Haptic;
using SDK::TestSupport::KernelFixture;

namespace {

struct Fixture {
    KernelFixture   kernel;
    Pattern         patterns[kMaxPatterns] {};
    uint8_t         patternCount = 0;
    Plan            plan {};
    Timing          timing {};
    EffectDurations durations = EffectDurations::nominal();
    RunHeader       run {};
    SessionMeta     meta;
    Recorder        recorder { kernel.kernel };

    Fixture()
    {
        patternCount = buildSet(SetId::RhythmAndFamily, patterns);
        buildPlan(SetId::RhythmAndFamily, 1, 4242, plan);
        timing = timingFor(patterns, patternCount, durations);

        std::snprintf(run.runId, sizeof(run.runId), "20260908T101500");
        run.seed    = 4242;
        run.contact = Contact::Wrist;
        run.masking = Masking::Unmasked;
        run.set     = SetId::RhythmAndFamily;
        run.setHash = setHash(patterns, patternCount);

        meta.appVersion = "0.1.0";
        meta.firmware   = "1.4.0";
        meta.kernelAbi  = 3;
        meta.startTick  = 1000;
        meta.startWall  = 1757325300;
    }

    bool open() { return recorder.open(run, plan, patterns, patternCount, timing, durations, meta); }

    std::string file(const char *leaf) const
    {
        return kernel.fileSystem.readFile(std::string("Runs/") + run.runId + "/" + leaf);
    }

    static size_t lines(const std::string &content)
    {
        return static_cast<size_t>(std::count(content.begin(), content.end(), '\n'));
    }

    TrialRecord trial(uint16_t index, bool correct) const
    {
        TrialRecord row {};
        row.trialInRun   = index;
        row.trialInBlock = index;
        row.blockKind    = BlockKind::Test;
        row.presented    = static_cast<uint8_t>(index % patternCount);
        row.offered[0]   = row.presented;
        row.offered[1]   = static_cast<uint8_t>((row.presented + 1) % patternCount);
        row.offered[2]   = static_cast<uint8_t>((row.presented + 2) % patternCount);
        row.response     = correct ? Response::Slot0 : Response::Slot1;
        row.correct      = correct;
        row.noteCount    = patterns[row.presented].noteCount;
        for (uint8_t i = 0; i < row.noteCount; i++) {
            row.notes[i] = patterns[row.presented].notes[i];
        }
        return row;
    }
};

} // namespace

TEST(Recorder, OpeningWritesEveryFileSHeaderAndTheCandidateSet)
{
    Fixture f;
    ASSERT_TRUE(f.open());

    for (const char *leaf : { "trials.csv", "events.csv", "commands.csv", "probe.csv" }) {
        const std::string content = f.file(leaf);
        EXPECT_FALSE(content.empty()) << leaf << " was not created";
        EXPECT_EQ(content.find("schema,"), 0u) << leaf << " does not start with its header";
    }

    // The set as actually played, one row per pattern plus the header.
    EXPECT_EQ(Fixture::lines(f.file("patterns.csv")), f.patternCount + 1u);
    for (uint8_t i = 0; i < f.patternCount; i++) {
        EXPECT_NE(f.file("patterns.csv").find(f.patterns[i].id), std::string::npos)
            << f.patterns[i].id << " is missing from patterns.csv";
    }

    const std::string session = f.file("session.json");
    EXPECT_NE(session.find("\"end_reason\": \"running\""), std::string::npos);
    EXPECT_NE(session.find("20260908T101500"), std::string::npos);
}

/// A session that dies at trial 50 has to leave 49 usable trials, which means
/// the append cannot be buffered.
TEST(Recorder, EveryRowIsOnDiskBeforeTheNextOneIsWritten)
{
    Fixture f;
    ASSERT_TRUE(f.open());

    for (uint16_t i = 0; i < 50; i++) {
        f.recorder.appendTrial(f.trial(i, i % 3 != 0), f.patterns);
        EXPECT_EQ(Fixture::lines(f.file("trials.csv")), i + 2u) << "after trial " << i;
    }
    EXPECT_GE(f.kernel.fileSystem.flushCounts["Runs/20260908T101500/trials.csv"], 50u)
        << "rows were buffered rather than flushed";
}

/// A write-mode open positions the handle at offset zero rather than at the
/// end, so an append without the seek keeps only the newest row.
TEST(Recorder, AppendingKeepsTheHeaderFirstAndEveryEarlierRow)
{
    Fixture f;
    ASSERT_TRUE(f.open());
    for (uint16_t i = 0; i < 30; i++) {
        f.recorder.appendTrial(f.trial(i, true), f.patterns);
    }

    const std::string content = f.file("trials.csv");
    EXPECT_EQ(content.find("schema,"), 0u) << "the header is no longer first";
    EXPECT_EQ(Fixture::lines(content), 31u);
    // The first row's trial index is still there, which it would not be if the
    // handle had rewound.
    EXPECT_NE(content.find(",0,0,"), std::string::npos);
}

/// A FatFs lock slot held by a handle nobody closed is a file the next writer
/// cannot open, so no row may leak one.
TEST(Recorder, NoHandleIsLeftOpenBetweenRows)
{
    Fixture f;
    ASSERT_TRUE(f.open());
    for (uint16_t i = 0; i < 10; i++) {
        f.recorder.appendTrial(f.trial(i, true), f.patterns);
        EventRecord event {};
        event.trialIndex = i;
        f.recorder.appendEvent(event);
    }
    f.recorder.close("completed", 2000, 1757325900);

    for (const auto &entry : f.kernel.fileSystem.openHandles) {
        EXPECT_EQ(entry.second, 0u) << entry.first << " still has an open handle";
    }
}

TEST(Recorder, ClosingRewritesTheHeaderWholeWithTheEndReasonAndTheClocks)
{
    Fixture f;
    ASSERT_TRUE(f.open());
    f.recorder.appendTrial(f.trial(0, true), f.patterns);
    f.recorder.close("abandoned", 987654, 1757329999);

    const std::string session = f.file("session.json");
    EXPECT_NE(session.find("\"end_reason\": \"abandoned\""), std::string::npos);
    EXPECT_NE(session.find("\"end_tick\": 987654"), std::string::npos);
    EXPECT_NE(session.find("\"end_wall_clock\": 1757329999"), std::string::npos);
    EXPECT_EQ(session.find("\"end_reason\": \"running\""), std::string::npos)
        << "the running header was left behind";
    // Still one JSON object rather than two appended ones.
    EXPECT_EQ(std::count(session.begin(), session.end(), '{'), 1);
}

/// Throwing away a session does not make the rows less real, so a run that
/// ended early is a run with an end reason rather than a file nobody closed.
TEST(Recorder, AnAbandonedRunKeepsTheRowsItAlreadyHad)
{
    Fixture f;
    ASSERT_TRUE(f.open());
    for (uint16_t i = 0; i < 7; i++) {
        f.recorder.appendTrial(f.trial(i, true), f.patterns);
    }
    f.recorder.close("stopped-by-kernel", 5000, 1757326000);

    EXPECT_EQ(Fixture::lines(f.file("trials.csv")), 8u);
    EXPECT_NE(f.file("session.json").find("stopped-by-kernel"), std::string::npos);
}

TEST(Recorder, WritingAfterTheCloseChangesNothing)
{
    Fixture f;
    ASSERT_TRUE(f.open());
    f.recorder.appendTrial(f.trial(0, true), f.patterns);
    f.recorder.close("completed", 1, 1);

    const std::string before = f.file("trials.csv");
    f.recorder.appendTrial(f.trial(1, true), f.patterns);
    EXPECT_EQ(f.file("trials.csv"), before);
}

/// The SDK exposes no free-space query, so the cap is self-defence: a row is
/// written only if its worst case still fits, and the cap that was hit is in
/// the header.
TEST(Recorder, TheCapRefusesARowRatherThanTruncatingOne)
{
    Fixture f;
    ASSERT_TRUE(f.open());

    // Enough rows that trials.csv would pass its cap several times over.
    const uint16_t rows = static_cast<uint16_t>(kByteCapPerFile / 100u);
    for (uint16_t i = 0; i < rows; i++) {
        f.recorder.appendTrial(f.trial(i, true), f.patterns);
    }

    EXPECT_TRUE(f.recorder.capHit());
    EXPECT_STREQ(f.recorder.capsHit(), "trials.csv");
    EXPECT_LE(f.file("trials.csv").size(), kByteCapPerFile);
    // Every line that did land is whole.
    const std::string content = f.file("trials.csv");
    EXPECT_EQ(content.back(), '\n') << "a row was cut in half";

    f.recorder.close("cap-reached", 2, 2);
    EXPECT_NE(f.file("session.json").find("\"caps_hit\": \"trials.csv\""), std::string::npos);
}

TEST(Recorder, AFilesystemThatRefusesTheRunDirectoryIsReportedRatherThanIgnored)
{
    Fixture f;
    // Every write-mode open of a .csv fails, which is what a full or unwritable
    // filesystem looks like from here.
    f.kernel.fileSystem.failWriteOpenSuffix = ".csv";
    EXPECT_FALSE(f.open());
    EXPECT_FALSE(f.recorder.ok());
}

TEST(Recorder, TheRunIdIsTheWallClockInAFormThatSortsAndPairs)
{
    char runId[20] = {};
    // 2026-09-08T10:15:00Z
    Recorder::makeRunId(runId, 1788862500u);
    EXPECT_STREQ(runId, "20260908T101500");

    char later[20] = {};
    Recorder::makeRunId(later, 1788862501u);
    EXPECT_STREQ(later, "20260908T101501");
    EXPECT_LT(std::string(runId), std::string(later)) << "run ids do not sort by time";
}

TEST(Recorder, TheDurationCapIsMeasuredFromTheRunSStartAndSurvivesTheUptimeWrap)
{
    Fixture f;
    f.meta.startTick = 0xFFFFF000u;
    ASSERT_TRUE(f.open());

    EXPECT_FALSE(f.recorder.overDurationCap(0xFFFFF000u + 1000u));
    EXPECT_TRUE(f.recorder.overDurationCap(0xFFFFF000u + kRunDurationCapMs));
}

TEST(Recorder, EveryKindOfRowLandsInItsOwnFile)
{
    Fixture f;
    ASSERT_TRUE(f.open());

    f.recorder.appendTrial(f.trial(0, true), f.patterns);
    EventRecord event {};
    event.buttonId = 2;
    f.recorder.appendEvent(event);
    CommandRecord command {};
    command.patternIndex = 1;
    command.noteCount    = 1;
    command.notes[0].effect = 1;
    f.recorder.appendCommand(command, f.patterns, f.patternCount);
    ProbeRecord probe {};
    probe.noteCount = 1;
    probe.notes[0].effect = 127;
    std::snprintf(probe.label, sizeof(probe.label), "V2-effect-127/0");
    f.recorder.appendProbe(probe);

    EXPECT_EQ(Fixture::lines(f.file("trials.csv")), 2u);
    EXPECT_EQ(Fixture::lines(f.file("events.csv")), 2u);
    EXPECT_EQ(Fixture::lines(f.file("commands.csv")), 2u);
    EXPECT_EQ(Fixture::lines(f.file("probe.csv")), 2u);
    EXPECT_NE(f.file("probe.csv").find("V2-effect-127/0"), std::string::npos);
}
