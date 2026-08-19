/**
 * A whole night through the real `Service`, at a desk. See NightHarness.hpp
 * for what the harness is and what it deliberately is not evidence about.
 *
 * Every test here is a claim about what the *app* does with a stream of a known
 * shape -- not a claim about sleep. Where a test asserts a number of minutes,
 * that number is derived from the scenario by construction and stated in the
 * test, so a failure says which of the two is wrong.
 */
#include <gtest/gtest.h>

#include <cstdlib>

#include "Diag.hpp"
#include "NightStore.hpp"
#include "NightHarness.hpp"

namespace {

using namespace Harness;

/// The default 21:00-11:00 window, a Tuesday evening at 21:45 UTC.
/// Local == UTC because the suite runs with TZ=UTC (set in CMake), which is the
/// only way a time-of-day assertion can mean anything.
constexpr int64_t kStart = 1755553500;   // 2026-08-18 21:45:00 UTC

// The amplitudes below are chosen against a *measured* count scale rather than
// guessed, because every threshold in this app lives inside one decade of it.
// Feeding EpochCounter a sinusoid of amplitude A and taking the count per 60 s
// scoring epoch gives, at the ~48 Hz the hardware actually delivers:
//
//     A (g)     0.3 Hz    1.0 Hz
//     0.0005        10        14
//     0.0010        20        30
//     0.0020        42        60
//     0.0100       216       300
//     0.0500      1088      1508
//     0.3000      6528      9058
//
// against which the app's thresholds are: micro-movement floor 8, band
// "settled" 20, movement floor 40, stillness-to-open 60, band "restless" 120,
// activity-to-close 250, and Cole-Kripke's own sleep/wake boundary at about
// 273 counts sustained. So a "still sleeper" and "somebody moving about" are
// about 0.001 g and 0.05 g respectively -- three milli-g apart, which is worth
// knowing before a night is spent on it.

/// Respiration on a still, worn wrist: ~20 counts a minute. Below the stillness
/// ceiling that opens a night and below the movement floor, and above the
/// micro-movement floor the worn gate needs to see to believe a wrist is alive.
constexpr float kBreathingG  = 0.0010f;
constexpr float kBreathingHz = 0.30f;

/// Somebody awake and shifting about: ~1500 counts a minute, six times the
/// activity floor that closes a night and five times Cole-Kripke's boundary.
constexpr float kAwakeG  = 0.05f;
constexpr float kAwakeHz = 1.0f;

Phase still(int minutes, int hr = 55)
{
    Phase p;
    p.minutes    = minutes;
    p.amplitudeG = kBreathingG;
    p.freqHz     = kBreathingHz;
    p.worn       = true;
    p.hrBpm      = hr;
    return p;
}

Phase awake(int minutes, int hr = 68, int steps = 0)
{
    Phase p;
    p.minutes     = minutes;
    p.amplitudeG  = kAwakeG;
    p.freqHz      = kAwakeHz;
    p.worn        = true;
    p.hrBpm       = hr;
    p.stepsPerMin = steps;
    return p;
}

/// A plain night: five minutes awake, seven hours still, then up and walking.
Scenario plainNight()
{
    Scenario s;
    s.startUtc = kStart;
    s.phases   = { awake(5), still(7 * 60), awake(20, 72, 40) };
    return s;
}

TEST(Pipeline, APlainNightIsRecordedScoredAndSummarised)
{
    const Scenario s = plainNight();
    const Observations obs = Rig::instance().run(s);

    const std::string csvPath = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csvPath.empty()) << "no epoch CSV was written at all";

    const std::vector<EpochRow> rows =
        parseEpochs(Rig::instance().fs.readFile(csvPath));
    EXPECT_GT(rows.size(), 100u) << "the night barely recorded anything";

    // The index is what the history and the baseline are rebuilt from.
    const std::string index = Rig::instance().fs.readFile("Nights/index.csv");
    EXPECT_NE(index.find(','), std::string::npos) << "no index row";

    // And the state file is gone, because the night closed cleanly.
    EXPECT_FALSE(Rig::instance().fs.exist("night_state.txt"));

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr) << "the service never published a closed night";
    EXPECT_TRUE(rep->hasSleep) << "a worn, still night produced no sleep numbers";
}

// ---------------------------------------------------------------------------
// The night's own clock
//
// These are the expensive bugs. A crash is cheap -- you see it and fix it. A
// silent offset in every reported sleep time survives review, looks plausible
// in the morning, and contaminates the diary calibration that every other
// number in the app is waiting on.
// ---------------------------------------------------------------------------

/// Local minutes past midnight of a UTC instant. TZ=UTC, so local == UTC; the
/// helper exists so the arithmetic in the assertions reads as a time of day.
int16_t localOf(int64_t utc)
{
    const std::time_t t = static_cast<std::time_t>(utc);
    std::tm g {};
    gmtime_r(&t, &g);
    return static_cast<int16_t>(g.tm_hour * 60 + g.tm_min);
}

TEST(NightClock, ReportedWakeTimeIsWhenTheSleeperStoppedBeingStill)
{
    // Awake for 5 minutes, still for 300, then up and walking. By construction
    // the last still minute is run-minute 304 and the wearer is up from 305, so
    // the honest final wake is 21:45 + 305 = 02:50.
    Scenario s;
    s.startUtc = kStart;
    s.phases   = { awake(5), still(300), awake(30, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(rep->hasSleep);

    const int16_t expected = localOf(kStart + 305 * 60);
    // Two minutes of slack: the epoch grid and the ten-minute onset run mean
    // the last *scored* sleep minute can legitimately be a minute either side.
    EXPECT_NEAR(rep->wokeAtMin, expected, 2)
        << "reported wake " << rep->wokeAtMin << ", the sleeper stopped at "
        << expected << " -- a difference of "
        << (expected - rep->wokeAtMin) << " minutes in every night";
}

TEST(NightClock, ReportedSleepOnsetIsWhenTheSleeperSettled)
{
    // Same night. Stillness begins at run-minute 5 = 21:50, and the onset
    // definition needs ten consecutive sleep minutes, so onset is 21:50 and
    // onset latency is measured from the session start, which is also 21:50.
    Scenario s;
    s.startUtc = kStart;
    s.phases   = { awake(5), still(300), awake(30, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(rep->hasSleep);

    // Onset latency is session start to onset. Both are 21:50 here, so a
    // latency near zero is right -- and it must be right *because* the opening
    // minutes were scored, not because they were skipped. The paired assertion
    // below is what tells those two apart.
    EXPECT_LE(rep->onsetLatencyMin, 2);
    EXPECT_NEAR(rep->asleepAtMin, localOf(kStart + 5 * 60), 2);
}

TEST(NightClock, TimeInBedIsTheEpochsTheSummaryWasBuiltFrom)
{
    // For a night that never restarted, "how many scoring epochs the night
    // contained" and "how many minutes the wearer was in bed" are the same
    // number counted twice. A summary whose own two fields disagree cannot be
    // reconciled by anyone reading the file, and every index the JSON quotes --
    // onset_epoch, final_wake_epoch, hr min_epoch -- is on one of the two axes
    // without saying which.
    const Observations obs = Rig::instance().run(plainNight());
    (void)obs;

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    std::string jsonPath = csv;
    jsonPath.replace(jsonPath.size() - 4, 4, ".json");
    const std::string json = Rig::instance().fs.readFile(jsonPath);
    ASSERT_FALSE(json.empty());

    EXPECT_EQ(jsonField(json, "epochs"), jsonField(json, "time_in_bed_min"))
        << "provenance.epochs and sleep.time_in_bed_min disagree; the epoch "
           "indices in this file are on neither axis unambiguously";
}

TEST(NightClock, TheMinutesBackdatedIntoANightAreScoredNotOnlyRecorded)
{
    // A night with no awakenings at all. Every minute of it was either scored
    // as sleep or is a minute the app decided not to look at -- and the second
    // kind is the finding: the quarter hour the segmenter backdates is written
    // to the CSV, counted in time in bed, and never passed to the scorer, so it
    // is missing from total sleep time and drags efficiency down with it.
    const Observations obs = Rig::instance().run(plainNight());
    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(rep->hasSleep);

    // The ten-minute onset run is the only sleep the definition legitimately
    // declines to count, plus a minute for the grid.
    const int32_t unaccounted = rep->timeInBedMin - rep->totalSleepMin -
                                rep->wasoMin;
    EXPECT_LE(unaccounted, 11)
        << "time in bed " << rep->timeInBedMin << ", sleep "
        << rep->totalSleepMin << ", WASO " << rep->wasoMin << ": "
        << unaccounted << " minutes of this night were recorded and never "
           "scored";

    // And the consequence a person would actually see.
    EXPECT_GE(rep->efficiencyPct, 97)
        << "a night with no awakenings reported " << rep->efficiencyPct
        << "% efficiency";
}

// ---------------------------------------------------------------------------
// The honesty contract, on the surface a person actually reads
// ---------------------------------------------------------------------------

/// A watch on a nightstand that TOUCH_DETECT wrongly believes is on a wrist:
/// perfectly still, no pulse. This is the case WornGate exists for.
Scenario nightstandNight()
{
    Scenario s;
    s.startUtc = kStart;
    Phase table;
    table.minutes    = 7 * 60;
    table.amplitudeG = 0.0f;   // a rigid object
    table.worn       = true;   // ...that the sensor reports as worn
    table.hrBpm      = 0;      // optical HR against a hard surface
    Phase up = awake(30, 72, 40);
    s.phases = { table, up };
    return s;
}

TEST(HonestyContract, ANightThatFailedTheWornGateDrawsNoStrip)
{
    const Observations obs = Rig::instance().run(nightstandNight());
    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr) << "the nightstand night never closed";

    ASSERT_NE(rep->worn, static_cast<uint8_t>(Engine::WornVerdict::Worn))
        << "a still, pulseless night passed the worn gate";
    ASSERT_FALSE(rep->hasSleep);

    // The numbers are suppressed. The picture must be too: the strip is a
    // per-epoch sleep/wake verdict and restfulness level for every minute of a
    // night the app has just said it cannot report on, drawn under a caption
    // that tells the reader it came from their movement and heart rate.
    EXPECT_EQ(rep->stripUsed, 0)
        << "the strip published " << rep->stripUsed
        << " buckets of sleep/wake and restfulness for a night whose numbers "
           "were suppressed";
}

TEST(HonestyContract, ANightStillRunningDrawsNoStripFromTheLastOne)
{
    // Two nights, the second observed while it is still running. `mVerdicts`
    // and `mBand` are only filled when a night *closes*, so a strip built
    // mid-night is drawn from whatever the previous night left in them -- and
    // on the very first night of a fresh install, from zeroed memory, which
    // decodes as "asleep, most settled" for every minute so far.
    Rig::instance().run(plainNight());

    Scenario s = plainNight();
    s.keepFilesystem = true;
    s.startUptimeMs  = 3600u * 1000u + 500u * 60000u;
    s.guiOpensAtMin  = 120;      // somebody checks at 23:45
    s.guiOpensAtEnd  = false;
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *live = nullptr;
    for (const auto &r : obs.reports) {
        if (r.phase == static_cast<uint8_t>(CustomMessage::Phase::Recording)) {
            live = &r;
        }
    }
    ASSERT_NE(live, nullptr) << "no report was published while recording";

    EXPECT_EQ(live->stripUsed, 0)
        << "a night in progress published " << live->stripUsed
        << " buckets of verdicts that were never computed for it";
}

TEST(HonestyContract, ADaytimeChargeDoesNotMarkTheFollowingNightInterrupted)
{
    // The charger was on the desk in the evening, forty minutes before the
    // wearer settled -- outside the pre-roll ring, so outside the night by any
    // reading. The interruption flags are the first line of the morning report,
    // and a flag that cries wolf is a flag nobody reads on the night it matters.
    Scenario s;
    s.startUtc = kStart;
    Phase charging = awake(40);
    charging.charging = true;
    s.phases = { charging, awake(5), still(6 * 60), awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    EXPECT_EQ(rep->interruption & Engine::Interruption::kCharging, 0u)
        << "a charge that ended forty minutes before the night began was "
           "reported as an interruption of it";
}

// ---------------------------------------------------------------------------
// Restart survival
// ---------------------------------------------------------------------------

TEST(Resume, AResumedNightsTimesAreOnTheOriginalSessionsClock)
{
    // Run the first two and a half hours for real, stop the way the USB cable
    // does, then run the rest against the state file the first half actually
    // wrote. Nothing here is hand-authored: if the app cannot produce the state
    // file, the test cannot use one.
    Scenario first;
    first.startUtc      = kStart;
    first.phases        = { awake(5), still(400), awake(30, 72, 40) };
    first.stopAtMin     = 150;             // plugged in at 00:15
    first.guiOpensAtEnd = false;
    Rig::instance().run(first);
    ASSERT_TRUE(Rig::instance().fs.exist("night_state.txt"))
        << "the first half left nothing to resume";

    Scenario second = first;
    second.keepFilesystem = true;
    second.stopAtMin      = -1;
    second.guiOpensAtEnd  = true;
    // Twenty minutes on the charger: uptime climbs (the app restarted inside one
    // boot) and the wall clock moves with it. The sleeper carries on from where
    // the first launch stopped rather than starting the evening again.
    second.startUptimeMs  = first.startUptimeMs + 170u * 60000u;
    second.startUtc       = kStart + 170 * 60;
    second.phaseOffsetMin = 170;
    const Observations obs = Rig::instance().run(second);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr) << "the resumed night never closed";
    ASSERT_TRUE(rep->interruption & Engine::Interruption::kResumed);

    // The wearer got up at run-minute 405 of the *original* session, which the
    // second launch reaches at its own minute 235. Either way the wall clock
    // says 04:30.
    const int16_t expected = localOf(kStart + 405 * 60);
    ASSERT_TRUE(rep->hasSleep)
        << "a resumed night that ran seven hours reported no sleep at all";
    EXPECT_NEAR(rep->wokeAtMin, expected, 3)
        << "reported wake " << rep->wokeAtMin << " against " << expected
        << ": a resumed night's times are anchored to the last flush before "
           "the restart rather than to when the night opened";
}

// ---------------------------------------------------------------------------
// Storage that fails in the middle of the night
// ---------------------------------------------------------------------------

TEST(Storage, AVolumeThatFillsMidNightIsSaidSoInTheMorning)
{
    // `NightStore::appendEpoch` returns a bool and nothing checks it. So a
    // volume that fills at 03:00 stops recording, the night carries on
    // counting minutes in RAM, and the morning summary describes a night whose
    // record on disk stops a third of the way through -- with nothing anywhere
    // saying which third.
    Scenario s = plainNight();
    const Observations warm = Rig::instance().run(s);
    (void)warm;
    const std::string full = theNightCsv(Rig::instance().fs);
    const size_t wholeNight = Rig::instance().fs.readFile(full).size();

    // Fail every write once about a third of the night is on disk.
    s.failWritesAfterBytes = wholeNight / 3;
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    EXPECT_NE(rep->interruption, 0u)
        << "every write after the volume filled was refused and the night "
           "reported itself clean";
}

TEST(Storage, ANightThatCouldNotBeFiledSaysSoOnTheScreen)
{
    // Room for the epochs, none for the summary or the index row. The night is
    // real, its numbers are real, and the two files that would let anyone else
    // read them are missing -- so the one surface that still works has to say so.
    // Nothing was reading `finishNight`'s return value.
    Scenario s = plainNight();
    const Observations warm = Rig::instance().run(s);
    (void)warm;
    const std::string full = theNightCsv(Rig::instance().fs);
    const size_t wholeNight = Rig::instance().fs.readFile(full).size();

    s.failWritesAfterBytes = wholeNight + 200;
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    EXPECT_TRUE(rep->interruption & Engine::Interruption::kWriteFailed)
        << "the summary and the index row both failed and the report said the "
           "night was clean";

    // And the night is closed rather than left open to be spliced into the
    // wearer's morning.
    EXPECT_FALSE(Rig::instance().fs.exist("night_state.txt"));
}

TEST(Storage, AWholeNightLeaksNoFileHandle)
{
    // FatFs holds a finite lock table and its `f_close` keeps the FIL valid
    // when a sync fails. A recorder that opens and closes a file twice per
    // 30 s epoch for eight hours -- 1 900 times -- has to leave none of them
    // open, and the InMemoryFileSystem counts them.
    Rig::instance().run(plainNight());
    for (const auto &kv : Rig::instance().fs.openHandles) {
        EXPECT_EQ(kv.second, 0u) << kv.first << " left "
                                 << kv.second << " handles open";
    }
}

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

TEST(Loop, AnOversleptLoopDoesNotShortenTheNightItLost)
{
    // The epoch grid advances by whole epochs and *skips forward* when the loop
    // wakes late: one recording epoch absorbs the whole overshoot, with a
    // span_ms far past 30 000 and a healthy sample count -- so the thin-epoch
    // guard never fires. Every duration in the night is then short by the
    // overshoot, because time in bed is an epoch count and the epochs are gone.
    //
    // Two runs of the same night, one stalled for five minutes. The same amount
    // of real time elapsed in both, so both must report the same time in bed.
    Scenario clean = plainNight();
    const Observations a = Rig::instance().run(clean);
    const CustomMessage::SleepReportData *unstalled = a.lastReportedNight();
    ASSERT_NE(unstalled, nullptr);

    Scenario stalled = plainNight();
    stalled.oversleepAtMin = 200;
    stalled.oversleepMs    = 5u * 60u * 1000u;
    const Observations b = Rig::instance().run(stalled);
    const CustomMessage::SleepReportData *rep = b.lastReportedNight();
    ASSERT_NE(rep, nullptr);

    // The stall really happened: one CSV row covers more than an epoch.
    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    bool sawLongEpoch = false;
    for (const EpochRow &r : parseEpochs(Rig::instance().fs.readFile(csv))) {
        if (r.spanMs > static_cast<long>(Engine::kEpochMs) * 3 / 2) {
            sawLongEpoch = true;
        }
    }
    ASSERT_TRUE(sawLongEpoch) << "the scenario did not actually stall the loop";

    EXPECT_NEAR(rep->timeInBedMin, unstalled->timeInBedMin, 2)
        << "the same night reported " << rep->timeInBedMin
        << " minutes in bed when the loop lost five of them and "
        << unstalled->timeInBedMin << " when it did not";

    EXPECT_NE(rep->interruption, 0u)
        << "the loop lost five minutes of the night and the summary reported "
           "it clean";
}

// ---------------------------------------------------------------------------
// The surfaces that exist so the app need not be opened
// ---------------------------------------------------------------------------

TEST(Surfaces, TheMorningWidgetIsClaimedWhenTheNightCloses)
{
    // The home widget's whole purpose is a report you do not have to open the
    // app for. `pumpWidget()` is called from the GUI handlers and from
    // `openNight()` -- and not from `closeNight()`, so on a morning where
    // nobody opens the app the widget is never claimed at all.
    Scenario s = plainNight();
    s.guiOpensAtEnd = false;     // nobody picks the watch up
    const Observations obs = Rig::instance().run(s);

    bool started = false;
    for (const Ask &a : obs.widget) {
        if (a.type == SDK::MessageType::REQUEST_WIDGET_START) { started = true; }
    }
    EXPECT_TRUE(started)
        << "the night closed and the home widget was never claimed";
}

TEST(Surfaces, AnOpenGlanceIsSentItsContentAtAll)
{
    // The carousel's tick is the only thing that sends glance content, and it
    // sends it only when the form reports itself invalid. `glanceRefresh()` sets
    // the three texts -- which invalidates them -- and then calls `setValid()`, so
    // by the time a tick arrives the form says it has nothing to send. The tick
    // handler never calls `setValid()` either, which is where all five of the
    // SDK's own Glance examples put it.
    //
    // Net effect: the glance is never sent anything. Not stale content -- none.
    Scenario s = plainNight();
    s.glanceOpensAtMin = 100;
    s.guiOpensAtEnd    = false;
    const Observations obs = Rig::instance().run(s);

    size_t updates = 0;
    for (const Ask &a : obs.glance) {
        if (a.type == SDK::MessageType::REQUEST_GLANCE_UPDATE) { ++updates; }
    }
    EXPECT_GT(updates, 0u)
        << "the glance was opened and ticked for hours and was never sent a "
           "single update";
}

TEST(Surfaces, AnOpenGlanceStopsSayingRecordingWhenTheNightEnds)
{
    // And once it can send at all: a glance opened during the night has to stop
    // saying "recording" when the night ends. `closeNight()` called neither
    // `pumpWidget()` nor `glanceRefresh()`.
    Scenario s = plainNight();
    s.glanceOpensAtMin = 100;
    s.guiOpensAtEnd    = false;
    const Observations obs = Rig::instance().run(s);

    // The night closes in the last phase, twenty minutes before the run ends.
    const uint32_t closeAbout = Rig::instance().system.nowMs - 21u * 60000u;
    size_t updatesAfter = 0;
    for (const Ask &a : obs.glance) {
        if (a.type == SDK::MessageType::REQUEST_GLANCE_UPDATE &&
            a.uptimeMs > closeAbout) {
            ++updatesAfter;
        }
    }
    EXPECT_GT(updatesAfter, 0u)
        << "the night closed and the open glance was never refreshed, so it "
           "still says the watch is recording";
}

// ---------------------------------------------------------------------------
// Nights designed to be awkward rather than typical
//
// Each of these is a shape the app has to survive rather than a shape it has to
// score well. What is asserted is that it does not crash, does not wedge, does
// not open a night it should not, and does not report a number it cannot know.
// ---------------------------------------------------------------------------

TEST(HostileNights, ASleeperWhoNeverSettlesGetsNoNight)
{
    // Restless from lights-out to morning: never fifteen consecutive still
    // minutes, so no session should ever open and there should be nothing to
    // report. A night that opened here would be a spurious history row and, worse,
    // a spurious baseline sample.
    Scenario s;
    s.startUtc = kStart;
    s.phases   = { awake(9 * 60) };
    const Observations obs = Rig::instance().run(s);

    EXPECT_TRUE(theNightCsv(Rig::instance().fs).empty())
        << "a night opened for a wearer who never settled";
    EXPECT_FALSE(Rig::instance().fs.exist("Nights/index.csv"));
    ASSERT_TRUE(obs.haveReport());
    EXPECT_FALSE(obs.lastReport().hasSleep);
}

TEST(HostileNights, ASleeperWhoSettlesInstantlyKeepsTheirFirstQuarterHour)
{
    // Still from the very first epoch of the run: the wearer was already settled
    // when the service started. The pre-roll ring fills from that first epoch, so
    // by the time the segmenter has its fifteen still minutes the ring holds
    // exactly those thirty recording epochs -- and the night must be backdated to
    // minute zero rather than starting a quarter of an hour late.
    Scenario s;
    s.startUtc = kStart;
    s.phases   = { still(7 * 60), awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(rep->hasSleep);

    // The night began when the run did, and the first sleep observed is right at
    // the top of it.
    EXPECT_NEAR(rep->asleepAtMin, localOf(kStart), 2)
        << "the night was backdated to " << rep->asleepAtMin
        << " rather than to " << localOf(kStart);
    EXPECT_EQ(rep->interruption & Engine::Interruption::kDataGap, 0u)
        << "the ring held the whole backdate and the night was flagged anyway";
    // 420 still minutes plus the minute that closed it.
    EXPECT_NEAR(rep->timeInBedMin, 421, 3);
}

TEST(HostileNights, ADeliveryOutageIsUnscorableRatherThanPerfectStillness)
{
    // The accelerometer stops for forty minutes in the middle of the night and
    // the app does not restart. A near-empty epoch integrates to near-zero, which
    // reads as the soundest sleep of the night -- so the outage has to become
    // Unscorable and be flagged, not become sleep.
    Scenario s = plainNight();
    s.accelGapFromMin = 200;
    s.accelGapToMin   = 240;
    const Observations obs = Rig::instance().run(s);

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    const std::vector<EpochRow> rows =
        parseEpochs(Rig::instance().fs.readFile(csv));
    size_t empty = 0;
    for (const EpochRow &r : rows) {
        if (r.samples == 0) { ++empty; }
    }
    EXPECT_GT(empty, 20u) << "the outage did not reach the recorder";

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    EXPECT_NE(rep->interruption & Engine::Interruption::kDataGap, 0u)
        << "forty minutes with no samples and the night reported itself clean";

    // The night is still a night: the outage must not swallow it.
    EXPECT_TRUE(rep->hasSleep);
    // And the outage minutes must not have been counted as sleep. Time in bed
    // covers them; total sleep must be short of it by at least the outage.
    EXPECT_LE(rep->totalSleepMin, rep->timeInBedMin - 30)
        << "the outage was scored as sleep";
}

TEST(HostileNights, DeliveryThatDegradesRatherThanStoppingIsVisibleInTheRecord)
{
    // A tenth of the samples, all night: 4.8 Hz where the hardware delivers about
    // 48. Neither guard notices -- `kMinSamplesPerRecordingEpoch` is 60 and
    // `kMinSamplesPerEpoch` is 120, both set against a *nominal* 25 Hz, so they
    // fire only below about 4 % of the delivered rate.
    //
    // And the counts do not survive it intact: measured against a 0.5 Hz sinusoid,
    // a scoring epoch counts 286 at 48 Hz and 212 at 4.8 Hz, a 26 % shrink -- so
    // every threshold in the app quietly means something else. See
    // `EpochCounter.TheCountIsOnlyRateIndependentInTheUpperHalfOfTheRange`.
    //
    // Nothing here can be fixed by a threshold nobody has measured, so what is
    // required is that the record *says* what it was built from: the epoch rows
    // carry their own sample counts and spans, and the summary carries the night's
    // delivered rate, so a morning can answer this without a second night.
    Scenario s = plainNight();
    s.accelThinning = 10;
    const Observations obs = Rig::instance().run(s);
    (void)obs;

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    const std::vector<EpochRow> rows =
        parseEpochs(Rig::instance().fs.readFile(csv));
    ASSERT_GT(rows.size(), 100u);

    // Every row can be turned back into a delivered rate, which is the whole
    // point of recording the span rather than assuming it.
    for (const EpochRow &r : rows) {
        if (r.samples == 0) { continue; }
        ASSERT_GT(r.spanMs, 0L) << "an epoch row carries no span";
        const double hz = 1000.0 * static_cast<double>(r.samples) /
                          static_cast<double>(r.spanMs);
        EXPECT_LT(hz, 8.0) << "the thinning did not reach the recorder";
    }

    // And the summary says so without anyone having to parse the CSV.
    std::string jsonPath = csv;
    jsonPath.replace(jsonPath.size() - 4, 4, ".json");
    const std::string json = Rig::instance().fs.readFile(jsonPath);
    ASSERT_FALSE(json.empty());
    EXPECT_NE(json.find("acc_hz_x10"), std::string::npos)
        << "the summary does not record the rate the night was built from, so an "
           "interrupted or degraded night cannot say what produced it";
}

TEST(HostileNights, ANightAtTheSixteenHourBoundIsCutAndSaysSo)
{
    // A watch left on a still surface, inside a bedtime window wide enough that
    // leaving it cannot be what ends the session. The engine scores at most sixteen
    // hours, and the segmenter has to be what ends it -- not the array bound, and
    // certainly not a uint16 going round.
    //
    // The window is 21:00-20:59, so its one-minute hole is 23 hours away from a
    // 21:45 start and the sixteen-hour bound is reached first.
    Scenario s;
    s.startUtc = kStart;
    s.settingsJson =
        "{\"schema\":1,\"values\":{\"bedtime\":\"21:00\",\"wake_by\":\"20:59\"}}";
    Phase forever;
    forever.minutes    = 20 * 60;
    forever.amplitudeG = kBreathingG;
    forever.freqHz     = kBreathingHz;
    forever.worn       = true;
    forever.hrBpm      = 55;
    s.phases = { forever };
    Rig::instance().run(s);

    // Read the index rather than the last report: a watch on a still surface opens
    // a *second* session the minute the first one closes, so the live report is
    // about that one.
    const std::string index = Rig::instance().fs.readFile("Nights/index.csv");
    ASSERT_NE(index.find(','), std::string::npos)
        << "a twenty-hour session never ended";

    long long startUtc = 0;
    long tib = 0, tst = 0, eff = 0, hrmin = 0, hrat = 0;
    unsigned worn = 0, interruption = 0;
    bool parsed = false;
    for (const std::string &l : lines(index)) {
        if (l.empty() || l[0] == '#' || l[0] == 's') { continue; }
        parsed = std::sscanf(l.c_str(), "%lld,%ld,%ld,%ld,%ld,%ld,%u,%u",
                             &startUtc, &tib, &tst, &eff, &hrmin, &hrat,
                             &worn, &interruption) == 8;
        break;
    }
    ASSERT_TRUE(parsed) << "no index row for the long night";
    EXPECT_LE(tib, static_cast<long>(Engine::kMaxScoringEpochs) + 2)
        << "the night ran " << tib << " minutes, past what the engine scores";
    EXPECT_GE(tib, static_cast<long>(Engine::kMaxScoringEpochs) - 4)
        << "the night ended at " << tib
        << " minutes, well short of the bound -- something else closed it and "
           "the bound is untested";
}

TEST(HostileNights, SensorTimestampsThatJumpBackwardsDoNotFabricateMovement)
{
    // The accelerometer's own clock is reset under the app. EpochCounter takes
    // every dt from those timestamps and differences them unsigned, so a jump
    // backwards presents as an enormous forward gap -- above kMaxGapMs, so the
    // filters re-seed and contribute nothing rather than integrating a fabricated
    // rectangle across it.
    Scenario s = plainNight();
    s.accelTimestampJumpAtMin      = 200;
    s.accelTimestampJumpBackMs     = 3600u * 1000u;
    const Observations obs = Rig::instance().run(s);

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    for (const EpochRow &r : parseEpochs(Rig::instance().fs.readFile(csv))) {
        // Nothing anywhere in the night may look like violent movement.
        EXPECT_LT(r.count, 5000L)
            << "an epoch counted " << r.count
            << " from a sensor clock that went backwards";
    }
    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr) << "the night did not survive a sensor clock reset";
}

TEST(HostileNights, AWallClockThatJumpsMidNightMarksTheNightRatherThanMovingIt)
{
    // A host sync or a timezone change moves the wall clock an hour forward while
    // the night is running. Uptime does not move, so no duration may change -- and
    // the night has to say the two halves of it are not on the same scale.
    Scenario s = plainNight();
    s.clockJumpAtMin = 200;
    s.clockJumpSec   = 3600;
    const Observations withJump = Rig::instance().run(s);

    Scenario clean = plainNight();
    const Observations without = Rig::instance().run(clean);

    const CustomMessage::SleepReportData *a = withJump.lastReportedNight();
    const CustomMessage::SleepReportData *b = without.lastReportedNight();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    EXPECT_NE(a->interruption & Engine::Interruption::kClockJump, 0u)
        << "the wall clock moved an hour and the night did not say so";
    // Durations come from uptime and from an epoch count, so they must be
    // untouched by the clock.
    EXPECT_NEAR(a->timeInBedMin, b->timeInBedMin, 2)
        << "a wall-clock jump changed how long the night thinks it was";
    EXPECT_NEAR(a->totalSleepMin, b->totalSleepMin, 2);
}

TEST(HostileNights, TheChargerGoingInDuringANightIsSaidLoudly)
{
    // Plugging in terminates every running app, so a night that saw the charger
    // has a hole in it whose length is not knowable from inside. It must be the
    // first thing the morning says.
    Scenario s;
    s.startUtc = kStart;
    Phase charged = still(60);
    charged.charging = true;
    s.phases = { awake(5), still(3 * 60), charged, still(2 * 60),
                 awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    EXPECT_NE(rep->interruption & Engine::Interruption::kCharging, 0u)
        << "the charger was connected for an hour of the night and the report "
           "did not say so";
}

TEST(HostileNights, AWornSensorThatSaysNothingAllNightIsUncertainNotUnworn)
{
    // TOUCH_DETECT resolved on hardware and delivered zero samples in a minute
    // (ledger row S12). A sensor that never speaks leaves no evidence either way,
    // and telling somebody their watch was not worn would send them to put on a
    // watch they are already wearing.
    Scenario s = plainNight();
    s.touchReportsInitialState = false;
    const Observations obs = Rig::instance().run(s);

    // Two honest outcomes, and which one happens is worth pinning rather than
    // accepting either: with no worn evidence at all the segmenter never sees a
    // still *worn* epoch, so no night opens -- and the diagnostic log is then the
    // only thing that distinguishes "nobody went to bed" from "the worn sensor is
    // mute", which is exactly what it exists for.
    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
    ASSERT_FALSE(log.empty());

    if (rep == nullptr) {
        EXPECT_TRUE(theNightCsv(Rig::instance().fs).empty())
            << "no night was reported and yet one was recorded";
        // The log has to say the sensor resolved, so a reader can tell this from a
        // sensor that was never subscribed.
        EXPECT_NE(log.find("ATMRHXSLC"), std::string::npos)
            << "the log cannot distinguish a mute worn sensor from an absent "
               "one:\n" << log;
        return;
    }

    // If a night did open, the verdict must be Uncertain rather than NotWorn:
    // telling somebody their watch was not worn would send them to put on a watch
    // they are already wearing.
    EXPECT_NE(rep->worn, static_cast<uint8_t>(Engine::WornVerdict::NotWorn))
        << "a sensor that never spoke was reported as the watch not being worn";
    EXPECT_FALSE(rep->hasSleep);
}

TEST(HostileNights, AFlickeringWornSensorIsRecordedAsFlickering)
{
    // The load-bearing sensor claim in the whole app (ledger row S7): a loosely
    // strapped sleeping wrist that makes TOUCH_DETECT chatter. Whatever the
    // verdict, the *edges* have to reach the file, because a worn fraction cannot
    // tell a flicker from a removal and only one of those means tighten the strap.
    Scenario s;
    s.startUtc = kStart;
    s.phases.push_back(awake(5));
    for (int i = 0; i < 60; ++i) {
        Phase on  = still(6);
        Phase off = still(1);
        off.worn  = false;
        s.phases.push_back(on);
        s.phases.push_back(off);
    }
    s.phases.push_back(awake(20, 72, 40));
    const Observations obs = Rig::instance().run(s);
    (void)obs;

    const std::string csv = theNightCsv(Rig::instance().fs);
    if (csv.empty()) {
        // A sensor flickering this hard never gives fifteen consecutive still worn
        // epochs, so no night opens. That is the suppression ledger row S7 warns
        // about, reached honestly -- and the *reason* has to be on the volume, or
        // it is indistinguishable from a wearer who did not go to bed.
        const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
        ASSERT_FALSE(log.empty());
        EXPECT_NE(log.find(" launch "), std::string::npos) << log;
        EXPECT_EQ(log.find(" open "), std::string::npos)
            << "no CSV was written and the log says a night opened:\n" << log;
        return;
    }
    long edges = 0;
    for (const EpochRow &r : parseEpochs(Rig::instance().fs.readFile(csv))) {
        edges += r.wornEdges;
    }
    EXPECT_GT(edges, 0L)
        << "the worn sensor changed state dozens of times and the epoch log "
           "recorded no edges at all";
}

// ---------------------------------------------------------------------------
// The history's dates
// ---------------------------------------------------------------------------

/// A night's identity is the local evening it began: that is what names its file,
/// and `NightStore.hpp` says so normatively -- "a UTC stem would name half the
/// year's nights with the wrong date".
///
/// The history list is on a different calendar. `publishHistory` sends
/// `startUtc / 86400`, whole UTC days, and `MainView::formatDay` renders it with
/// `gmtime` under a comment asserting "the value is already a whole local day,
/// computed service-side from the night's own start". Nothing computes that. West
/// of UTC a 23:00 bedtime is the next UTC day, so every night in the Americas is
/// listed under tomorrow's date while its own file is named for today -- and a
/// history whose dates disagree with the filenames is a history nobody can match
/// to a diary, which is the one thing the calibration needs it for.
TEST(History, ANightIsListedUnderTheLocalDayItsFileIsNamedFor)
{
    // Only meaningful away from UTC, and the suite runs at UTC by default.
    ASSERT_EQ(setenv("TZ", "America/New_York", 1), 0);
    tzset();

    // 2025-08-18 23:30 local = 2025-08-19 03:30 UTC. The file is named for the
    // 18th; the UTC day is the 19th.
    const int64_t bedtime = 1755574200;   // 23:30 EDT on the 18th

    Scenario s;
    s.startUtc = bedtime;
    s.phases   = { awake(5), still(6 * 60), awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());

    ASSERT_FALSE(obs.historyRows.empty()) << "no history row was published";
    const int32_t days = obs.historyRows.back().startUtcDays;

    // The day the list will render, and the day the file is named for.
    char listed[16];
    const std::time_t t = static_cast<std::time_t>(days) * 86400;
    std::tm g {};
    ASSERT_NE(gmtime_r(&t, &g), nullptr);
    std::snprintf(listed, sizeof(listed), "%04d%02d%02d",
                  g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);

    // "Nights/YYYYMMDDTHHMMSS.csv"
    const std::string stem = csv.substr(std::string("Nights/").size(), 8);

    EXPECT_EQ(std::string(listed), stem)
        << "the history lists this night under " << listed
        << " and its file is named " << stem;

    ASSERT_EQ(setenv("TZ", "UTC", 1), 0);
    tzset();
}

// ---------------------------------------------------------------------------
// The uptime wrap
// ---------------------------------------------------------------------------

/// `getTimeMs()` is device uptime: 32-bit, wrapping at ~49.7 days (ledger row P9,
/// CONFIRMED). Every comparison against it has to be a signed or unsigned
/// *difference*, never a magnitude compare -- and there are four: the epoch grid's
/// advance, the heart-rate duty cycle, the resume classification, and the sample
/// path's own dt.
///
/// A magnitude compare would not fail once at 49.7 days and recover. The epoch
/// grid would decide the next epoch was due in 49 days and the service would sleep
/// through the night; the duty cycle would stall for weeks or spin; the resume
/// classification would call every relaunch a device reboot. So it is constructed
/// here rather than reasoned about: a night that straddles the wrap has to be an
/// ordinary night.
TEST(UptimeWrap, ANightStraddlingTheWrapIsAnOrdinaryNight)
{
    // Start four minutes before 2^32 ms, so the wrap lands inside the opening
    // stillness -- which is where the grid, the duty cycle and the pre-roll ring
    // are all live at once.
    Scenario s;
    s.startUtc      = kStart;
    s.startUptimeMs = 0xFFFFFFFFu - 4u * 60u * 1000u;
    s.phases        = { awake(2), still(6 * 60), awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr) << "the service did not survive the uptime wrap";
    ASSERT_TRUE(rep->hasSleep)
        << "a night across the uptime wrap produced no sleep numbers";

    // The same night, well away from the wrap, must give the same answer.
    Scenario ref = s;
    ref.startUptimeMs = 3600u * 1000u;
    const Observations refObs = Rig::instance().run(ref);
    const CustomMessage::SleepReportData *refRep = refObs.lastReportedNight();
    ASSERT_NE(refRep, nullptr);

    EXPECT_NEAR(rep->timeInBedMin, refRep->timeInBedMin, 2)
        << "across the wrap: " << rep->timeInBedMin << " minutes in bed, away "
           "from it " << refRep->timeInBedMin;
    EXPECT_NEAR(rep->totalSleepMin, refRep->totalSleepMin, 2);
    EXPECT_EQ(rep->wokeAtMin, refRep->wokeAtMin);

    // And no epoch may have absorbed 49 days.
    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    for (const EpochRow &r : parseEpochs(Rig::instance().fs.readFile(csv))) {
        EXPECT_LT(r.spanMs, static_cast<long>(Engine::kEpochMs) * 3)
            << "an epoch spanned " << r.spanMs << " ms across the wrap";
    }
}

TEST(UptimeWrap, TheHeartRateDutyCycleKeepsCyclingAcrossTheWrap)
{
    // The duty cycle is the one clock comparison with nothing else watching it: a
    // stalled cycle leaves heart rate off for the rest of the night, and the night
    // then degrades to Uncertain -- which looks exactly like a night where heart
    // rate was configured off on purpose.
    Scenario s;
    s.startUtc      = kStart;
    s.startUptimeMs = 0xFFFFFFFFu - 6u * 60u * 1000u;
    s.settingsJson  = "{\"schema\":1,\"values\":{\"hr\":\"duty\","
                      "\"hr_duty_on_sec\":\"60\",\"hr_duty_per_sec\":\"300\"}}";
    s.phases        = { awake(3), still(5 * 60), awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty());
    const std::vector<EpochRow> rows =
        parseEpochs(Rig::instance().fs.readFile(csv));
    ASSERT_GT(rows.size(), 100u);

    // Heart rate has to come and go across the whole night rather than stopping.
    size_t withHr = 0, withoutHr = 0, lateWithHr = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const bool has = rows[i].hrSamples > 0;
        if (has) { ++withHr; } else { ++withoutHr; }
        if (has && i > rows.size() / 2) { ++lateWithHr; }
    }
    EXPECT_GT(withHr, 0u)    << "the duty cycle never turned heart rate on";
    EXPECT_GT(withoutHr, 0u) << "the duty cycle never turned heart rate off";
    EXPECT_GT(lateWithHr, 0u)
        << "heart rate stopped in the first half of the night and never came "
           "back: the duty cycle stalled";
}

TEST(UptimeWrap, AResumeAcrossTheWrapIsARestartNotAReboot)
{
    // The resume classification is the only thing that can tell an app relaunch
    // from a device reboot, and it does it with a signed uptime difference for
    // exactly this reason. An unsigned magnitude compare would call every relaunch
    // across the wrap a reboot -- which discards the gap measurement, so a resumed
    // night's times would land early again.
    Scenario first;
    first.startUtc      = kStart;
    first.startUptimeMs = 0xFFFFFFFFu - 90u * 60u * 1000u;
    first.phases        = { awake(5), still(400), awake(30, 72, 40) };
    first.stopAtMin     = 60;
    first.guiOpensAtEnd = false;
    Rig::instance().run(first);
    ASSERT_TRUE(Rig::instance().fs.exist("night_state.txt"));

    // Relaunched twenty minutes later, on the far side of the wrap.
    Scenario second = first;
    second.keepFilesystem = true;
    second.stopAtMin      = -1;
    second.guiOpensAtEnd  = true;
    second.startUptimeMs  = first.startUptimeMs + 80u * 60u * 1000u;
    second.startUtc       = kStart + 80 * 60;
    second.phaseOffsetMin = 80;
    const Observations obs = Rig::instance().run(second);

    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr) << "the resumed night never closed";
    EXPECT_NE(rep->interruption & Engine::Interruption::kResumed, 0u);

    // The wearer got up at run-minute 405 of the original session: 04:30.
    EXPECT_NEAR(rep->wokeAtMin, localOf(kStart + 405 * 60), 3)
        << "reported wake " << rep->wokeAtMin
        << ": the relaunch across the wrap was misclassified, so the twenty "
           "minutes off were not counted";
}

// ---------------------------------------------------------------------------
// Can a night be diagnosed from what it left behind?
// ---------------------------------------------------------------------------

TEST(Diagnosis, ANightThatRecordedNothingStillExplainsItself)
{
    // The failure the economics of this app cannot afford: an eight-hour night
    // that leaves no epoch CSV at all. Before the diagnostic log there was
    // nothing on the volume to read -- no file, no history row, no clue -- and the
    // only way to answer "why" was to spend another night.
    //
    // Here the volume refuses every write from the first byte, so the night file
    // is never created. What has to survive is the *reason*.
    Scenario s = plainNight();
    s.failWritesAfterBytes = 0;
    const Observations obs = Rig::instance().run(s);

    // First: the service returned. A wedged loop is the one failure that costs a
    // night *and* the next one, and there is nothing to read afterwards either.
    // Reaching this line is the assertion.

    // Nothing was filed.
    EXPECT_FALSE(Rig::instance().fs.exist("Nights/index.csv"));
    const std::string csv = theNightCsv(Rig::instance().fs);
    if (!csv.empty()) {
        // A zero-length night file is what a refused write leaves: the entry was
        // created and nothing went into it. That is itself a diagnosis, and a
        // better one than no file at all, so it is pinned rather than prevented.
        EXPECT_TRUE(Rig::instance().fs.readFile(csv).empty())
            << csv << " has content, so the writes were not actually refused";
    }

    // And the honest report: no numbers, and a reason.
    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    if (rep != nullptr) {
        EXPECT_NE(rep->interruption & Engine::Interruption::kWriteFailed, 0u)
            << "nothing could be written and the report did not say so";
    }
}

TEST(Diagnosis, TheLogSaysWhichSensorDriversResolved)
{
    // The single most valuable line in the file. On hardware, a two-minute run of
    // the probe's screen turned over two ledger rows -- TOUCH_DETECT resolved and
    // said nothing (S12), SPO2 did not resolve at all (S4) -- and a lower-case
    // letter is what said so. SleepLab had no equivalent: a night recorded against
    // a sensor that never resolved looked exactly like a night recorded against one
    // that did.
    Scenario s = plainNight();
    Rig::instance().run(s);

    const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
    ASSERT_FALSE(log.empty()) << "no diagnostic log was written at all";

    EXPECT_NE(log.find(" sensors "), std::string::npos)
        << "the log does not say which drivers resolved:\n" << log;
    // All nine resolve in the harness, so the block is all upper case.
    EXPECT_NE(log.find("ATMRHXSLC"), std::string::npos)
        << "the resolved-driver block is not in the log:\n" << log;
}

TEST(Diagnosis, TheLogSaysWhatTheNightRanUnderAndHowItEnded)
{
    // Second most valuable: a night is never diagnosed against the settings
    // somebody meant to write. And the close line has to carry enough to tell a
    // suppressed night from a clean one without opening anything else.
    Scenario s = plainNight();
    s.settingsJson = "{\"schema\":1,\"values\":{\"bedtime\":\"22:30\","
                     "\"wake_by\":\"09:00\",\"hr\":\"off\"}}";
    Rig::instance().run(s);

    const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
    ASSERT_FALSE(log.empty());

    EXPECT_NE(log.find("settings.json loaded"), std::string::npos)
        << "the log does not say whether the settings were read:\n" << log;
    EXPECT_NE(log.find("bed=1350-540"), std::string::npos)
        << "the log does not say what window was in force:\n" << log;
    EXPECT_NE(log.find("hr=off"), std::string::npos) << log;
    // Against the constant, not a literal. A test that hardcodes the version is a
    // test that fails on the next bump for a reason unrelated to what it checks --
    // which this one did.
    EXPECT_NE(log.find(std::string("v") + SleepLab::kAppVersion),
              std::string::npos)
        << "the log does not say which build wrote it (expected v"
        << SleepLab::kAppVersion << "):\n" << log;

    EXPECT_NE(log.find(" open "), std::string::npos) << log;
    EXPECT_NE(log.find(" close "), std::string::npos)
        << "the night closed and the log does not say how:\n" << log;
    EXPECT_NE(log.find("acc_hz_x10="), std::string::npos)
        << "the close line does not carry the delivered rate:\n" << log;
}

TEST(Diagnosis, TheLogSaysWhereARefusedWriteStoppedTheRecord)
{
    // The difference between losing the last hour of a night and losing the first
    // seven is the difference between a usable night and a wasted one, and only
    // the log can say which: the report says INCOMPLETE and the CSV says nothing
    // about where it was cut off, because a truncated file looks like a short one.
    //
    // The failure is scoped to the epoch CSV -- its closes fail, which FatFs treats
    // as a failed sync -- rather than filling the whole volume. That is the case
    // where the log can still be written, and it is the case worth testing.
    //
    // A volume that is *genuinely* full cannot be reported by a file on that
    // volume, and this deliberately does not pretend otherwise. See
    // `Docs/POST-MORTEM.md`, which names that as an accepted blind spot and what
    // covers it instead.
    Scenario s = plainNight();
    s.failCloseContaining = ".csv";
    Rig::instance().run(s);

    const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
    ASSERT_FALSE(log.empty()) << "no diagnostic log at all";
    EXPECT_NE(log.find(" fail "), std::string::npos)
        << "every epoch write was refused and the log said nothing:\n" << log;
    EXPECT_NE(log.find("epoch write refused"), std::string::npos) << log;
    // And it says how far the night got before it stopped.
    EXPECT_NE(log.find("after "), std::string::npos) << log;
}

TEST(Diagnosis, TheLogDoesNotGrowWithoutBound)
{
    // An append-only file in a service that runs for the device's whole life is a
    // way to fill the volume the night then cannot be written to.
    Scenario s = plainNight();
    for (int i = 0; i < 6; ++i) {
        s.keepFilesystem = (i > 0);
        s.startUptimeMs  = 3600u * 1000u +
                           static_cast<uint32_t>(i) * 600u * 60000u;
        s.startUtc       = kStart + static_cast<int64_t>(i) * 86400;
        Rig::instance().run(s);
    }
    const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
    ASSERT_FALSE(log.empty());
    EXPECT_LT(log.size(), SleepLab::kDiagMaxBytes + 1024u)
        << "the log is " << log.size() << " bytes after six launches";
    // ~20 lines a launch, so six launches is well under the cap and nothing
    // should have been rotated away yet.
    EXPECT_NE(log.find("launch"), std::string::npos);
}

TEST(Diagnosis, TheAlarmLeavesATrace)
{
    // The one output whose silent failure is worse than its absence, and it used
    // to leave nothing at all: "it did not go off" and "it went off and you slept
    // through it" were the same observation in the morning, and so were "it fired
    // at the deadline" and "it fired forty minutes early on a wake epoch".
    //
    // A night that is restless from 06:00, with the alarm set for 07:00 and a
    // thirty-minute smart window: the smart path should take it.
    Scenario s;
    s.startUtc = kStart;
    s.settingsJson = "{\"schema\":1,\"values\":{\"alarm\":\"on\","
                     "\"alarm_at\":\"07:00\",\"alarm_window_min\":\"30\"}}";
    // 21:45 start: still until 06:35, then awake inside the smart window.
    s.phases = { awake(5), still(8 * 60 + 45), awake(40, 72, 0),
                 awake(20, 72, 40) };
    const Observations obs = Rig::instance().run(s);

    ASSERT_FALSE(obs.alarms.empty())
        << "the alarm was enabled, the window was open and the wearer was moving, "
           "and nothing was raised";

    const std::string log = Rig::instance().fs.readFile("Debug/sleeplab.log");
    ASSERT_FALSE(log.empty());
    EXPECT_NE(log.find(" alarm "), std::string::npos)
        << "the alarm fired and the volume has no record of it:\n" << log;
    EXPECT_NE(log.find("fired why="), std::string::npos) << log;

    // And the time it fired is in there, which is the thing a wearer disputes.
    EXPECT_NE(log.find("local_min="), std::string::npos) << log;
}

TEST(Diagnosis, AnInterruptedNightsCsvSaysWhatProducedIt)
{
    // The summary JSON carries the build, the settings and the constants -- and is
    // written only when the night *closes*. So a night the USB cable ended left a
    // CSV that could not say which build wrote it or what window it ran under, and
    // that is the night most likely to need explaining.
    Scenario s = plainNight();
    s.settingsJson = "{\"schema\":1,\"values\":{\"bedtime\":\"22:00\","
                     "\"wake_by\":\"08:30\",\"hr\":\"off\"}}";
    s.stopAtMin     = 200;      // plugged in mid-night
    s.guiOpensAtEnd = false;
    Rig::instance().run(s);

    const std::string csv = theNightCsv(Rig::instance().fs);
    ASSERT_FALSE(csv.empty()) << "nothing was recorded at all";

    // No summary, because the night never closed. That is the case under test.
    std::string jsonPath = csv;
    jsonPath.replace(jsonPath.size() - 4, 4, ".json");
    ASSERT_FALSE(Rig::instance().fs.exist(jsonPath.c_str()))
        << "the night closed, so this is not the interrupted case";

    const std::string body = Rig::instance().fs.readFile(csv);
    EXPECT_NE(body.find("SleepLab v"), std::string::npos)
        << "the CSV does not say which build wrote it";
    EXPECT_NE(body.find("bed=1320-510"), std::string::npos)
        << "the CSV does not say what window it ran under";
    EXPECT_NE(body.find("hr=off"), std::string::npos) << "no heart-rate mode";
    EXPECT_NE(body.find("epoch_s=30"), std::string::npos)
        << "the CSV does not say what an epoch is, so its rows cannot be paired";
    // And it is a comment, so every existing reader skips it.
    EXPECT_EQ(body.find("SleepLab v") > 0 &&
                  body[body.rfind('\n', body.find("SleepLab v")) + 1] == '#',
              true);
}

// ---------------------------------------------------------------------------
// The merge: one night answers both questions
//
// The Tier 0 probe recorded per-sensor delivery and battery telemetry; SleepLab
// recorded the scored night. Neither recorded the other, and the two cannot be
// installed together -- so on 2026-08-19 a clean 8h26m night with a hand diary and
// a Garmin reference settled six sensor rows and could not be scored at all
// (ledger row S13). These pin that a single night now does both.
// ---------------------------------------------------------------------------

/// Pull one column out of the epoch CSV by name, so a test names what it wants
/// rather than counting commas -- which is the mistake the schema comment in
/// `NightStore.hpp` warns readers about.
std::vector<long> column(const std::string &csv, const std::string &name)
{
    std::vector<std::string> ls = lines(csv);
    size_t hdr = ls.size(), idx = 0;
    for (size_t i = 0; i < ls.size(); ++i) {
        if (ls[i].rfind("uptime_ms,wall_utc,", 0) == 0) { hdr = i; break; }
    }
    if (hdr == ls.size()) { return {}; }

    std::vector<std::string> cols;
    { std::string cur; for (char c : ls[hdr]) {
        if (c == ',') { cols.push_back(cur); cur.clear(); } else { cur += c; } }
      cols.push_back(cur); }
    bool found = false;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (cols[i] == name) { idx = i; found = true; break; }
    }
    if (!found) { return {}; }

    std::vector<long> out;
    for (size_t i = hdr + 1; i < ls.size(); ++i) {
        if (ls[i].empty() || ls[i][0] == '#') { continue; }
        std::vector<std::string> f;
        std::string cur;
        for (char c : ls[i]) {
            if (c == ',') { f.push_back(cur); cur.clear(); } else { cur += c; } }
        f.push_back(cur);
        if (idx < f.size()) { out.push_back(std::strtol(f[idx].c_str(), nullptr, 10)); }
    }
    return out;
}

TEST(Merge, OneNightCarriesTheDeliveryColumnsAndTheSleepOnes)
{
    const Observations obs = Rig::instance().run(plainNight());
    const CustomMessage::SleepReportData *rep = obs.lastReportedNight();
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(rep->hasSleep) << "the merge cost the night its sleep numbers";

    const std::string csv =
        Rig::instance().fs.readFile(theNightCsv(Rig::instance().fs));
    ASSERT_FALSE(csv.empty());

    // Every column the probe used to own, present and populated.
    //
    // `touch_n` is deliberately not in this list. It is the one column whose zero
    // is the finding rather than a fault: TOUCH_DETECT is an event sensor, and in
    // this scenario it reported its state once at minute zero -- before the night
    // opened, so before any row was written -- and then had nothing to say for
    // seven hours. Measured on hardware, one sample in 507 minutes (ledger row
    // S7). `TouchDeliveryIsSeenWhenTheSensorActuallySpeaks` covers the other case.
    for (const char *name : { "acc_batches", "hr_trust_x10",
                              "hrex_opt", "batt_mah", "wakes", "msgs" }) {
        const std::vector<long> v = column(csv, name);
        ASSERT_FALSE(v.empty()) << name << " is not a column in the epoch log";
        bool anyReal = false;
        for (long x : v) { if (x > 0) { anyReal = true; break; } }
        EXPECT_TRUE(anyReal) << name << " exists and every row is absent or zero";
    }
    // And the column exists even when nothing filled it, which is the point of a
    // fixed layout.
    EXPECT_FALSE(column(csv, "touch_n").empty());
}

TEST(Merge, TouchDeliveryIsSeenWhenTheSensorActuallySpeaks)
{
    // The other half of the event-sensor story: when the worn state does change
    // mid-night, `touch_n` records that the sensor spoke. Without this column,
    // "reported worn" and "said nothing and the last state was carried forward"
    // are the same row -- which is what made ledger row S12 cost a screen read
    // rather than a log read.
    Scenario s;
    s.startUtc = kStart;
    Phase off = still(4);
    off.worn  = false;
    s.phases  = { awake(5), still(3 * 60), off, still(3 * 60),
                  awake(20, 72, 40) };
    Rig::instance().run(s);

    const std::string csv =
        Rig::instance().fs.readFile(theNightCsv(Rig::instance().fs));
    ASSERT_FALSE(csv.empty());

    const std::vector<long> touchN = column(csv, "touch_n");
    ASSERT_FALSE(touchN.empty());
    long spoke = 0;
    for (long x : touchN) { if (x > 0) { ++spoke; } }
    EXPECT_GT(spoke, 0)
        << "the worn state changed twice mid-night and no row recorded the "
           "sensor speaking";
    EXPECT_LT(spoke, static_cast<long>(touchN.size()) / 4)
        << "the sensor spoke in a quarter of all rows; it publishes on change";
}

TEST(Merge, TheBatteryColumnsThePercentGaugeCannotReplace)
{
    // Ledger row S18: across a whole night the percent gauge read 100.0 % at both
    // ends while remaining capacity fell 10 mAh. So `batt_mah` has to move, and it
    // has to move monotonically down, or a power question is unanswerable from a
    // SleepLab night -- which is what S16 first wrongly claimed it was not.
    Rig::instance().run(plainNight());
    const std::string csv =
        Rig::instance().fs.readFile(theNightCsv(Rig::instance().fs));

    const std::vector<long> mah = column(csv, "batt_mah");
    ASSERT_GT(mah.size(), 100u);
    EXPECT_GT(mah.front(), mah.back())
        << "remaining capacity did not fall across the night: "
        << mah.front() << " -> " << mah.back();

    // And the current is recorded, which is the column that gives a rate rather
    // than a difference.
    const std::vector<long> ma = column(csv, "batt_avg_ma_x10");
    ASSERT_FALSE(ma.empty());
    bool anyNonAbsent = false;
    for (long x : ma) { if (x != Engine::kAbsent) { anyNonAbsent = true; break; } }
    EXPECT_TRUE(anyNonAbsent) << "batt_avg_ma_x10 is absent in every row";
}

TEST(Merge, TouchDeliveryIsRecordedSeparatelyFromWhatTouchSaid)
{
    // The column that would have made ledger row S12 obvious in one night instead
    // of needing a screen read: `worn_pct` says what the sensor reported and
    // `touch_n` says whether it reported at all. On hardware the answer was one
    // sample in 507 minutes, and `worn_pct` looked identical throughout.
    Scenario s = plainNight();
    s.touchReportsInitialState = true;
    Rig::instance().run(s);

    const std::string csv =
        Rig::instance().fs.readFile(theNightCsv(Rig::instance().fs));
    const std::vector<long> touchN = column(csv, "touch_n");
    const std::vector<long> wornPct = column(csv, "worn_pct");
    ASSERT_EQ(touchN.size(), wornPct.size());
    ASSERT_GT(touchN.size(), 100u);

    // The harness's touch sensor speaks on change only, so most rows are silent
    // while the worn fraction stays high -- exactly the hardware shape.
    size_t silent = 0, worn = 0;
    for (size_t i = 0; i < touchN.size(); ++i) {
        if (touchN[i] == 0)    { ++silent; }
        if (wornPct[i] >= 100) { ++worn; }
    }
    EXPECT_GT(silent, touchN.size() / 2)
        << "the touch sensor spoke in most rows; it is an event sensor";
    EXPECT_GT(worn, touchN.size() / 2)
        << "the worn fraction did not carry forward through the silence";
}

TEST(Merge, DiagnosticsOffLeavesTheColumnsAbsentRatherThanZero)
{
    // The schema is fixed and the setting controls gathering, not layout -- so a
    // reader never has to branch, and an ungathered column is absent, which is
    // what -1 already means in this file. Zero would be a measurement.
    Scenario s = plainNight();
    s.settingsJson = "{\"schema\":1,\"values\":{\"diagnostics\":\"off\"}}";
    Rig::instance().run(s);

    const std::string csv =
        Rig::instance().fs.readFile(theNightCsv(Rig::instance().fs));
    ASSERT_FALSE(csv.empty());

    // The columns still exist.
    const std::vector<long> mah = column(csv, "batt_mah");
    ASSERT_FALSE(mah.empty()) << "the column layout changed with the setting";
    for (long x : mah) {
        EXPECT_EQ(x, Engine::kAbsent) << "batt_mah was gathered with diagnostics off";
    }
    // And the sleep columns are untouched by the setting.
    const std::vector<long> count = column(csv, "count");
    bool anyCount = false;
    for (long x : count) { if (x > 0) { anyCount = true; break; } }
    EXPECT_TRUE(anyCount) << "turning diagnostics off cost the night its counts";
}

// ---------------------------------------------------------------------------
// The idle record: a night that does not open is a measurement, not a waste
// ---------------------------------------------------------------------------

TEST(Watching, ANightThatNeverOpensStillMeasuresTheNoiseFloor)
{
    // The failure this exists for. `stillnessCountMax` is a guess at about 2 mg of
    // band-limited movement, which is the same order as a wrist IMU's own in-band
    // noise -- so if the noise is above it, no night ever opens and from the
    // outside that is indistinguishable from a wearer who did not go to bed.
    //
    // Modelled by a wearer who is worn and perfectly still but whose counts sit
    // just above the stillness ceiling all night: exactly the shape a noise floor
    // above the threshold produces.
    Scenario s;
    s.startUtc = kStart;
    Phase noisy;
    noisy.minutes    = 6 * 60;
    // ~0.0035 g at 0.3 Hz is ~75 counts a scoring epoch, above the 60 ceiling and
    // far below the 250 that would read as activity.
    noisy.amplitudeG = 0.0035f;
    noisy.freqHz     = kBreathingHz;
    noisy.worn       = true;
    noisy.hrBpm      = 55;
    s.phases = { noisy };
    Rig::instance().run(s);

    // No night, which is the premise.
    ASSERT_TRUE(theNightCsv(Rig::instance().fs).empty())
        << "a night opened; this scenario is meant not to open one";
    EXPECT_FALSE(Rig::instance().fs.exist("Nights/index.csv"));

    // But the counts are on the volume.
    const std::string watching =
        Rig::instance().fs.readFile(SleepLab::kWatchingPath);
    ASSERT_FALSE(watching.empty())
        << "no night opened and nothing recorded what the wrist was doing";

    const std::vector<long> counts = column(watching, "count");
    ASSERT_GT(counts.size(), 300u)
        << "only " << counts.size() << " idle rows for a six-hour window";

    // And they are a distribution a threshold can be set from, rather than zeros.
    long above = 0;
    for (long c : counts) { if (c > 0) { ++above; } }
    EXPECT_GT(above, static_cast<long>(counts.size()) / 2)
        << "the idle rows carry no counts, so they measure nothing";

    // Same format as a night, which is what lets night_report.py read it.
    EXPECT_NE(watching.find("uptime_ms,wall_utc,"), std::string::npos);
    EXPECT_NE(watching.find("acc_batches"), std::string::npos)
        << "the idle record is not on schema 2, so it cannot be compared with a "
           "night's counts";
}

TEST(Watching, TheMinutesBeforeANightOpensAreKeptToo)
{
    // A night that *does* open leaves the run-up on the volume as well, which is
    // what answers "why did it open at 23:40 rather than 23:20".
    Rig::instance().run(plainNight());

    ASSERT_FALSE(theNightCsv(Rig::instance().fs).empty());
    const std::string watching =
        Rig::instance().fs.readFile(SleepLab::kWatchingPath);
    ASSERT_FALSE(watching.empty())
        << "the night opened and the minutes before it were discarded";
    EXPECT_GT(column(watching, "count").size(), 4u);
}

TEST(Watching, NothingIsRecordedOutsideTheBedtimeWindow)
{
    // Outside the window the segmenter would not have opened a night whatever the
    // counts were, so rows there answer nothing and would be most of a day of them.
    Scenario s;
    s.startUtc = 1755529200;   // 15:00 UTC, well outside 21:00-11:00
    s.phases   = { still(90) };
    Rig::instance().run(s);

    EXPECT_FALSE(Rig::instance().fs.exist(SleepLab::kWatchingPath))
        << "idle rows were written at three in the afternoon";
}

TEST(Watching, EnteringTheWindowStartsTheFileAgain)
{
    // One window's worth, not every evening since the app was installed. The file
    // is restarted on the transition into the window rather than capped alone,
    // because a cap that fires mid-window loses the beginning of it.
    Scenario s;
    s.startUtc = 1755550800;   // 21:00 UTC exactly: the window opens here
    Phase before;              // 20:40-21:00 is outside it
    before.minutes    = 20;
    before.amplitudeG = kBreathingG;
    before.freqHz     = kBreathingHz;
    before.worn       = true;
    before.hrBpm      = 55;
    s.startUtc = 1755549600;   // 20:40 UTC
    s.phases   = { before, still(60) };
    Rig::instance().run(s);

    const std::string watching =
        Rig::instance().fs.readFile(SleepLab::kWatchingPath);
    ASSERT_FALSE(watching.empty());

    // Exactly one header, so the file was started once rather than appended to a
    // previous evening's.
    size_t headers = 0, at = 0;
    while ((at = watching.find("uptime_ms,wall_utc,", at)) != std::string::npos) {
        ++headers;
        at += 1;
    }
    EXPECT_EQ(headers, 1u) << "the idle record has " << headers << " headers";

    // And nothing from before the window opened: the first row's wall clock is at
    // or after 21:00.
    const std::vector<long> rows = column(watching, "wall_utc");
    ASSERT_FALSE(rows.empty());
    EXPECT_GE(rows.front(), 1755550800L)
        << "the file carries rows from before the window opened";
}

} // namespace
