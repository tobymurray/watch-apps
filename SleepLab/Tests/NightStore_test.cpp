/**
 * Host tests for a night on disk.
 *
 * The properties under test are the ones that decide whether a night survives
 * being interrupted -- which on this platform is not an edge case, it is what
 * happens every time somebody plugs the watch in.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "KernelTestDoubles.hpp"
#include "NightStore.hpp"

namespace {

using SDK::TestSupport::KernelFixture;
using SleepLab::NightStore;
using SleepLab::ResumeState;

/// A fixed evening, so the generated filenames are stable.
constexpr int64_t kStart = 1755037800;   // 2025-08-12, evening

Engine::Epoch epoch(uint32_t uptimeMs, uint32_t count = 40, uint8_t wornPct = 100)
{
    Engine::Epoch e;
    e.uptimeMs = uptimeMs;
    e.wallUtc  = kStart + uptimeMs / 1000;
    e.spanMs   = Engine::kEpochMs;
    e.count    = count;
    e.samples  = 700;
    e.wornPct  = wornPct;
    return e;
}

Engine::NightSummary goodNight()
{
    Engine::NightSummary s;
    s.worn          = Engine::WornVerdict::Worn;
    s.hasSleep      = true;
    s.epochs        = 480;
    s.timeInBedMin  = 480;
    s.totalSleepMin = 430;
    s.wasoMin       = 30;
    s.awakenings    = 3;
    s.efficiencyPct = 89;
    s.stillInBedMin = 410;
    s.onsetEpoch    = 15;
    s.finalWakeEpoch = 470;
    s.onsetLatencyMin = 15;
    s.hrMinX10      = 512;
    s.hrMeanX10     = 560;
    s.hrMinEpoch    = 240;
    s.hrEpochs      = 470;
    return s;
}

std::vector<std::string> lines(const std::string &blob)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < blob.size()) {
        const size_t nl = blob.find('\n', start);
        if (nl == std::string::npos) {
            if (start < blob.size()) { out.push_back(blob.substr(start)); }
            break;
        }
        out.push_back(blob.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

// -- Writing ---------------------------------------------------------------------

TEST(NightStore, ANightGetsAHeaderAndOneRowPerEpoch)
{
    KernelFixture fx;
    NightStore store(fx.kernel);

    ASSERT_TRUE(store.beginNight(kStart, 1000));
    ASSERT_TRUE(store.isOpen());

    for (int i = 1; i <= 5; i++) {
        ASSERT_TRUE(store.appendEpoch(epoch(1000 + i * 30000), 0));
    }
    EXPECT_EQ(store.epochsWritten(), 5u);

    const auto ls = lines(fx.fileSystem.readFile(store.path()));
    // Four comment lines, one column header, five rows.
    ASSERT_EQ(ls.size(), 10u);
    EXPECT_EQ(ls[0].rfind("# SleepLab epoch log", 0), 0u);
    EXPECT_EQ(ls[4].rfind("uptime_ms,wall_utc,", 0), 0u);
    EXPECT_EQ(ls[5].rfind("31000,", 0), 0u);
    EXPECT_EQ(ls[9].rfind("151000,", 0), 0u);
}

TEST(NightStore, EveryEpochIsFlushedAndNoHandleIsLeftOpen)
{
    // A row still in the FAT cache when the USB cable goes in is a row that
    // never happened, and a leaked handle is a FatFs lock slot gone for good.
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));

    for (int i = 1; i <= 20; i++) {
        store.appendEpoch(epoch(1000 + i * 30000), 0);
    }

    EXPECT_EQ(fx.fileSystem.openHandles[store.path()], 0u);
    EXPECT_EQ(fx.fileSystem.openHandles[SleepLab::kStatePath], 0u);
    EXPECT_GE(fx.fileSystem.flushCounts[store.path()], 20u);
}

TEST(NightStore, TheReservedHrvColumnsExistInEveryRowFromTheStart)
{
    // There is no HRV producer today, and there may be one later. Writing the
    // columns now means a future producer is a recorder change, not a schema
    // bump and not a reason to discard every night already recorded.
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    ASSERT_TRUE(store.appendEpoch(epoch(31000), 0));

    const auto ls = lines(fx.fileSystem.readFile(store.path()));
    EXPECT_NE(ls[4].find("rmssd_x10,sdnn_x10,rr_count"), std::string::npos);
    // ...and the row ends with the three sentinels.
    EXPECT_NE(ls[5].find("-1,-1,0"), std::string::npos);
}

// -- Resume ------------------------------------------------------------------------

TEST(NightStore, AStateFileIsWrittenAfterEveryEpochAndNamesTheNight)
{
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    ASSERT_TRUE(store.appendEpoch(epoch(31000), 0));
    ASSERT_TRUE(store.appendEpoch(epoch(61000), 0));

    const std::string state = fx.fileSystem.readFile(SleepLab::kStatePath);
    EXPECT_EQ(state.rfind("STATE ", 0), 0u);
    EXPECT_NE(state.find(store.path()), std::string::npos);
    // Rewritten whole, not appended: a half-written second line is worse than
    // a stale first one, because a parser can match the wrong half.
    EXPECT_EQ(std::count(state.begin(), state.end(), '\n'), 1);
}

TEST(NightStore, ARelaunchInsideOneBootIsAnAppRestart)
{
    KernelFixture fx;
    {
        NightStore store(fx.kernel);
        ASSERT_TRUE(store.beginNight(kStart, 1000));
        store.appendEpoch(epoch(120000), 0);
    }

    NightStore relaunched(fx.kernel);
    // Uptime has climbed: the device did not reboot, the app was killed.
    const ResumeState r = relaunched.readState(400000, kStart + 400);

    EXPECT_TRUE(r.present);
    EXPECT_TRUE(r.appRestarted);
    EXPECT_FALSE(r.deviceRebooted);
    EXPECT_EQ(r.epochs, 1u);
    EXPECT_TRUE(r.flags & Engine::Interruption::kResumed);
}

TEST(NightStore, UptimeGoingBackwardsIsADeviceReboot)
{
    // Uptime is the only clock that can tell these apart: it survives an app
    // restart and resets only when the device does.
    KernelFixture fx;
    {
        NightStore store(fx.kernel);
        ASSERT_TRUE(store.beginNight(kStart, 5000000));
        store.appendEpoch(epoch(5030000), 0);
    }

    NightStore relaunched(fx.kernel);
    const ResumeState r = relaunched.readState(9000, kStart + 900);

    EXPECT_TRUE(r.deviceRebooted);
    EXPECT_FALSE(r.appRestarted);
    EXPECT_TRUE(r.flags & Engine::Interruption::kResumed);
}

TEST(NightStore, AWallClockThatMovedFurtherThanUptimeIsAClockJump)
{
    KernelFixture fx;
    {
        NightStore store(fx.kernel);
        ASSERT_TRUE(store.beginNight(kStart, 1000));
        store.appendEpoch(epoch(31000), 0);
    }

    NightStore relaunched(fx.kernel);
    // Sixty seconds of uptime, one hour of wall clock: a timezone change.
    const ResumeState r = relaunched.readState(91000, kStart + 31 + 3600);

    EXPECT_TRUE(r.flags & Engine::Interruption::kClockJump);
}

TEST(NightStore, ANormalRelaunchIsNotAClockJump)
{
    // The false positive that would mark every resumed night twice over.
    KernelFixture fx;
    {
        NightStore store(fx.kernel);
        ASSERT_TRUE(store.beginNight(kStart, 1000));
        store.appendEpoch(epoch(31000), 0);
    }

    NightStore relaunched(fx.kernel);
    const ResumeState r = relaunched.readState(91000, kStart + 91);

    EXPECT_FALSE(r.flags & Engine::Interruption::kClockJump);
}

TEST(NightStore, ResumeContinuesTheSameFileRatherThanOpeningASecond)
{
    // Two half nights in the history are worse than one flagged one, and both
    // halves might be short enough to be discarded entirely.
    KernelFixture fx;
    std::string path;
    {
        NightStore store(fx.kernel);
        ASSERT_TRUE(store.beginNight(kStart, 1000));
        path = store.path();
        for (int i = 1; i <= 3; i++) {
            store.appendEpoch(epoch(1000 + i * 30000), 0);
        }
    }

    NightStore relaunched(fx.kernel);
    const ResumeState r = relaunched.readState(200000, kStart + 200);
    ASSERT_TRUE(relaunched.resumeNight(r));

    EXPECT_EQ(std::string(relaunched.path()), path);
    EXPECT_EQ(relaunched.epochsWritten(), 3u);

    relaunched.appendEpoch(epoch(230000), r.flags);
    // Seven rows: five from before (4 comments + header + 3) plus one.
    EXPECT_EQ(lines(fx.fileSystem.readFile(path)).size(), 9u);
}

TEST(NightStore, AStateNamingAMissingFileIsRefusedRatherThanResumedInto)
{
    KernelFixture fx;
    fx.fileSystem.seedFile(SleepLab::kStatePath,
                           "STATE Nights/gone.csv 100 1000 1755037800 0 1755037800\n");

    NightStore store(fx.kernel);
    const ResumeState r = store.readState(200000, kStart);
    EXPECT_TRUE(r.present);
    EXPECT_FALSE(store.resumeNight(r)) << "deleted over USB, most likely";
}

TEST(NightStore, AnUnreadableStateFileStartsFreshRatherThanCrashing)
{
    KernelFixture fx;
    fx.fileSystem.seedFile(SleepLab::kStatePath, "garbage that is not a state\n");

    NightStore store(fx.kernel);
    EXPECT_FALSE(store.readState(1000, kStart).present);
}

TEST(NightStore, AStateFileFromAnOlderBuildStartsFreshRatherThanResumingWrong)
{
    // The state line gained its start-time field when the index turned out to
    // be recording the wrong clock. An old file parses five of six fields, and
    // a resume built on it would file the night under whatever start happened
    // to be left in memory. Refusing it costs at most the night in progress.
    KernelFixture fx;
    fx.fileSystem.seedFile(SleepLab::kStatePath,
                           "STATE Nights/old.csv 100 1000 1755037800 0\n");

    NightStore store(fx.kernel);
    EXPECT_FALSE(store.readState(200000, kStart).present);
}

// -- Finishing ---------------------------------------------------------------------

TEST(NightStore, FinishingWritesASummaryAnIndexRowAndClearsTheState)
{
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    store.appendEpoch(epoch(31000), 0);

    const std::string csv = store.path();
    ASSERT_TRUE(store.finishNight(goodNight(), "method-v1", true, "continuous"));

    std::string json = csv;
    json.replace(json.size() - 4, 4, ".json");
    const std::string body = fx.fileSystem.readFile(json);
    EXPECT_NE(body.find("\"total_sleep_min\""), std::string::npos);
    EXPECT_NE(body.find("430"), std::string::npos);

    EXPECT_FALSE(fx.fileSystem.exist(SleepLab::kStatePath))
        << "the state must be cleared last, and it must be cleared";
    EXPECT_FALSE(store.isOpen());

    const auto idx = lines(fx.fileSystem.readFile(SleepLab::kIndexPath));
    ASSERT_GE(idx.size(), 5u);
    EXPECT_EQ(idx[3].rfind("start_utc,", 0), 0u);
}

TEST(NightStore, TheSummaryCarriesItsOwnMethodAndItsOwnCaveats)
{
    // A file separated from this repo has to be able to say how it was made,
    // and what it is not.
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    store.appendEpoch(epoch(31000), 0);
    std::string json = store.path();
    json.replace(json.size() - 4, 4, ".json");

    ASSERT_TRUE(store.finishNight(goodNight(), "band-method-v1", false,
                                  "continuous"));

    const std::string body = fx.fileSystem.readFile(json);
    EXPECT_NE(body.find("cole-kripke-1992"), std::string::npos);
    EXPECT_NE(body.find("band-method-v1"), std::string::npos);
    EXPECT_NE(body.find("no polysomnography"), std::string::npos);
    EXPECT_NE(body.find("over-reports sleep"), std::string::npos);
    EXPECT_NE(body.find("\"restfulness_used_hr\":false"), std::string::npos);
}

TEST(NightStore, ANightWithNoSleepWritesNullsRatherThanZeroes)
{
    // Zero minutes of sleep is a claim about the night. The whole point of the
    // worn gate is that for some nights no such claim is available.
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    store.appendEpoch(epoch(31000), 0);
    std::string json = store.path();
    json.replace(json.size() - 4, 4, ".json");

    Engine::NightSummary s;
    s.worn = Engine::WornVerdict::NotWorn;
    s.epochs = 400;
    ASSERT_TRUE(store.finishNight(s, "m", false, "off"));

    const std::string body = fx.fileSystem.readFile(json);
    EXPECT_NE(body.find("\"reported\":false"), std::string::npos);
    EXPECT_NE(body.find("\"total_sleep_min\":null"), std::string::npos);
    EXPECT_NE(body.find("\"efficiency_pct\":null"), std::string::npos);
    EXPECT_NE(body.find("not-worn"), std::string::npos);
}

TEST(NightStore, ADiscardedNightKeepsItsDataAndReachesNoIndex)
{
    // The epoch CSV is real data somebody may want; what matters is that a
    // 40-minute "night" never reaches the history or the baseline.
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    store.appendEpoch(epoch(31000), 0);
    const std::string csv = store.path();

    store.discardNight();

    EXPECT_TRUE(fx.fileSystem.exist(csv.c_str()));
    EXPECT_FALSE(fx.fileSystem.exist(SleepLab::kIndexPath));
    EXPECT_FALSE(fx.fileSystem.exist(SleepLab::kStatePath));
    EXPECT_FALSE(store.isOpen());
}

// -- History and the baseline ----------------------------------------------------

/// Record @p n nights into the index.
void recordNights(KernelFixture &fx, int n, Engine::WornVerdict worn,
                  int32_t hrMinX10 = 500)
{
    for (int i = 0; i < n; i++) {
        NightStore store(fx.kernel);
        const int64_t start = kStart + static_cast<int64_t>(i) * 86400;
        ASSERT_TRUE(store.beginNight(start, 1000));
        store.appendEpoch(epoch(31000), 0);

        Engine::NightSummary s = goodNight();
        s.worn      = worn;
        s.hasSleep  = (worn == Engine::WornVerdict::Worn);
        s.hrMinX10  = hrMinX10;
        store.finishNight(s, "m", true, "continuous");
    }
}

TEST(NightStore, HistoryComesBackOldestFirst)
{
    KernelFixture fx;
    recordNights(fx, 5, Engine::WornVerdict::Worn);

    NightStore store(fx.kernel);
    NightStore::IndexRow rows[NightStore::kMaxHistory];
    const size_t n = store.readHistory(rows, NightStore::kMaxHistory);

    ASSERT_EQ(n, 5u);
    for (size_t i = 1; i < n; i++) {
        EXPECT_LT(rows[i - 1].startUtc, rows[i].startUtc);
    }
}

TEST(NightStore, HistoryKeepsOnlyTheWindowAndKeepsTheNewestOfIt)
{
    KernelFixture fx;
    recordNights(fx, static_cast<int>(NightStore::kMaxHistory) + 6,
                 Engine::WornVerdict::Worn);

    NightStore store(fx.kernel);
    NightStore::IndexRow rows[NightStore::kMaxHistory];
    const size_t n = store.readHistory(rows, NightStore::kMaxHistory);

    EXPECT_EQ(n, NightStore::kMaxHistory);
    // The oldest six have fallen out, so the first row kept is night 6.
    EXPECT_EQ(rows[0].startUtc, kStart + 6 * 86400);
}

TEST(NightStore, TheBaselineIsRebuiltFromTheIndexAndFromNothingElse)
{
    KernelFixture fx;
    recordNights(fx, 8, Engine::WornVerdict::Worn, 512);

    NightStore store(fx.kernel);
    Engine::BaselineStore baseline;
    store.loadBaseline(baseline);

    EXPECT_EQ(baseline.nights(), 8u);
    const auto d = baseline.hrMin(530);
    ASSERT_TRUE(d.available);
    EXPECT_EQ(d.baseline, 512);
    EXPECT_EQ(d.delta, 18);
}

TEST(NightStore, ANightstandNeverReachesTheBaseline)
{
    // THE test in this file. A watch on furniture scores a flawless night, and
    // one in the baseline would poison it for four weeks and make every real
    // night afterwards look bad.
    KernelFixture fx;
    recordNights(fx, 6, Engine::WornVerdict::NotWorn);

    NightStore store(fx.kernel);
    Engine::BaselineStore baseline;
    store.loadBaseline(baseline);

    EXPECT_EQ(baseline.nights(), 0u);
    EXPECT_FALSE(baseline.hrMin(500).available);
}

TEST(NightStore, AnUncertainNightIsExcludedToo)
{
    // Uncertain suppresses the numbers exactly as NotWorn does, so it must not
    // contribute them to a baseline either.
    KernelFixture fx;
    recordNights(fx, 6, Engine::WornVerdict::Uncertain);

    NightStore store(fx.kernel);
    Engine::BaselineStore baseline;
    store.loadBaseline(baseline);
    EXPECT_EQ(baseline.nights(), 0u);
}

TEST(NightStore, AnAbsentIndexIsEmptyHistoryRatherThanAFailure)
{
    KernelFixture fx;
    NightStore store(fx.kernel);
    NightStore::IndexRow rows[NightStore::kMaxHistory];
    EXPECT_EQ(store.readHistory(rows, NightStore::kMaxHistory), 0u);

    Engine::BaselineStore baseline;
    store.loadBaseline(baseline);
    EXPECT_EQ(baseline.nights(), 0u);
}

// ---------------------------------------------------------------------------
// What the state file is allowed to say
// ---------------------------------------------------------------------------

/// `night_state.txt` is read with `%63s` and the path it names is used verbatim:
/// to test for existence, to append every epoch to, and -- with ".csv" replaced
/// by ".json" -- to write the summary to. Nothing checks that it is a path this
/// app could have written.
///
/// Two consequences. A path that escapes the app's own directory would have a
/// night appended to it. And a path of the full 63 characters overflows the
/// 64-byte buffer the summary path is built in, because ".json" is one byte
/// longer than the ".csv" it replaces.
///
/// The file is the app's own and lives in the app's sandbox, so this is not an
/// attack surface so much as a corruption surface -- which is the one that
/// actually happens, because the file is rewritten 1 900 times a night and the
/// power can go at any of them.
TEST(NightStore, AStateFileNamingSomethingOutsideTheNightsDirectoryIsRefused)
{
    KernelFixture fx;
    fx.fileSystem.seedFile("../SharedData/victim.csv", "not a night\n");
    fx.fileSystem.seedFile(
        "night_state.txt",
        "STATE ../SharedData/victim.csv 10 1000 1755037800 0 1755037800\n");

    NightStore store(fx.kernel);
    const ResumeState st = store.readState(2000, kStart + 2);
    EXPECT_FALSE(st.present)
        << "the state file named " << st.path
        << ", which is not a path this app writes";
    EXPECT_FALSE(store.resumeNight(st));

    // And nothing was appended to it.
    EXPECT_EQ(fx.fileSystem.readFile("../SharedData/victim.csv"),
              "not a night\n");
}

TEST(NightStore, AStateFileNamingAnOverlongPathIsRefused)
{
    KernelFixture fx;
    // 63 characters exactly: the most `%63s` will read, and one more than the
    // summary path can hold once ".csv" becomes ".json".
    std::string longPath = "Nights/";
    longPath.append(63 - 7 - 4, 'x');
    longPath += ".csv";
    ASSERT_EQ(longPath.size(), 63u);

    fx.fileSystem.seedFile(longPath, "header\n");
    fx.fileSystem.seedFile("night_state.txt",
                           "STATE " + longPath +
                               " 10 1000 1755037800 0 1755037800\n");

    NightStore store(fx.kernel);
    const ResumeState st = store.readState(2000, kStart + 2);
    if (st.present && store.resumeNight(st)) {
        // If it is accepted, finishing it must not write past the end of the
        // buffer the summary path is built in. There is no portable way to
        // assert that from inside the process, so the requirement is that such
        // a path is refused before it gets there.
        FAIL() << "a 63-character night path was accepted; the summary path is "
                  "built in a 64-byte buffer and .json is longer than .csv";
    }
}

// ---------------------------------------------------------------------------
// Writes that fail
// ---------------------------------------------------------------------------

/// The documented ordering is: summary JSON, then index row, then clear the
/// state -- "so a crash anywhere above resumes the night rather than losing it".
/// That guarantee is about a crash, and it holds. A refused write is not a
/// crash: control reaches the clear, so a volume with no room lost the night
/// with nothing anywhere that said so.
///
/// Keeping the state file is *not* the remedy, and this test is here to pin that
/// too: a relaunch would resume into a night that has already been summarised,
/// and 07:00 is inside a 21:00-11:00 window, so the session would stay open all
/// morning appending breakfast to last night's CSV. So the night closes either
/// way and the caller is told, which is what `finishNight`'s return value is
/// for -- and what nothing was reading.
TEST(NightStore, ANightWhoseSummaryAndIndexBothFailReportsIt)
{
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));
    ASSERT_TRUE(store.appendEpoch(epoch(30000), 0));
    ASSERT_TRUE(fx.fileSystem.exist("night_state.txt"));
    const std::string csv = store.path();

    // No room for anything more.
    fx.fileSystem.failWritesAfterBytes = fx.fileSystem.bytesWritten;

    EXPECT_FALSE(store.finishNight(goodNight(), "band", true, "off"))
        << "the summary and the index row both failed and finishNight said the "
           "night was filed";

    // The night is closed, not left open to be spliced into tomorrow.
    EXPECT_FALSE(fx.fileSystem.exist("night_state.txt"));
    EXPECT_FALSE(store.isOpen());
    // And the record is still on the volume to be copied off.
    EXPECT_TRUE(fx.fileSystem.exist(csv.c_str()));
}

/// FatFs's `f_close` keeps the FIL -- and its lock-table entry -- when the sync
/// underneath it fails. A recorder that opens and closes twice per epoch has to
/// treat a failed close as a failed write, or it reports a night as recorded
/// while the rows sit in a cache that was never committed.
TEST(NightStore, AFailedCloseIsAFailedWrite)
{
    KernelFixture fx;
    NightStore store(fx.kernel);
    ASSERT_TRUE(store.beginNight(kStart, 1000));

    fx.fileSystem.closeGate = [](const std::string &path) {
        return path.find(".csv") == std::string::npos;
    };

    EXPECT_FALSE(store.appendEpoch(epoch(30000), 0))
        << "the epoch's close failed and appendEpoch reported success";
}

/// A decade of nights is ~400 index rows and the reader takes only the last
/// 4 096 bytes, which lands mid-row essentially always. A truncated leading field
/// parses cleanly as a smaller number, so the guard is to skip to the first
/// newline -- and the guard is what this exercises, by writing more rows than the
/// tail window can hold.
TEST(NightStore, AnIndexBiggerThanTheTailWindowIsReadFromAWholeRow)
{
    KernelFixture fx;

    std::string index =
        "start_utc,tib_min,tst_min,eff_pct,hr_min_x10,hr_min_at_pct,worn,"
        "interruption\n";
    // Ten years of nights, each with a distinguishable start time so a
    // half-parsed row would be obvious.
    constexpr int kNights = 3650;
    for (int i = 0; i < kNights; ++i) {
        char row[128];
        std::snprintf(row, sizeof(row), "%lld,480,430,89,512,50,0,0\n",
                      static_cast<long long>(kStart) + i * 86400LL);
        index += row;
    }
    ASSERT_GT(index.size(), 4096u * 8u) << "the fixture is not big enough to "
                                            "make the tail read land mid-row";
    fx.fileSystem.seedFile("Nights/index.csv", index);

    NightStore store(fx.kernel);
    NightStore::IndexRow rows[NightStore::kMaxHistory];
    const size_t n = store.readHistory(rows, NightStore::kMaxHistory);

    ASSERT_EQ(n, NightStore::kMaxHistory)
        << "the tail read recovered " << n << " rows of a decade of nights";

    // Oldest first, consecutive days, ending on the last night written. A row
    // parsed from a truncated timestamp would break the sequence.
    for (size_t i = 0; i < n; ++i) {
        const int64_t expect =
            kStart + static_cast<int64_t>(kNights - n + i) * 86400LL;
        EXPECT_EQ(rows[i].startUtc, expect)
            << "row " << i << " of the tail read";
        EXPECT_EQ(rows[i].timeInBedMin, 480);
        EXPECT_EQ(rows[i].efficiencyPct, 89);
    }
}

} // namespace
