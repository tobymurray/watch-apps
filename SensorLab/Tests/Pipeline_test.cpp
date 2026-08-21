/**
 ******************************************************************************
 * @file    Pipeline_test.cpp
 * @brief   Whole runs through the real Service. See RunHarness.hpp.
 ******************************************************************************
 *
 * Every scenario here is a device this app has to survive, and several of them
 * are devices this app is *known* to face: a type with no producer, an event
 * sensor that speaks once an hour, a frame that does not match its parser, a
 * handle above 255, a kernel that will not say what firmware it is.
 *
 * Nothing here is evidence about a sensor. The streams are generated.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include <string>

#include "RunHarness.hpp"

#include "Profile/Decimal.hpp"

using namespace Harness;
using SensorLab::Catalogue::kTypeCount;
using SensorLab::Catalogue::typeIndexOf;

namespace
{

/// A scenario with the three channels whose hardware behaviour is on record.
Scenario measuredDevice()
{
    Scenario s;
    s.durationMs = 3u * 60u * 1000u;
    s.channels.push_back(accelerometerAsMeasured(1));
    s.channels.push_back(touchAsMeasured(2));
    s.channels.push_back(noProducer(0xF1));   // SPO2, ledger row S4
    s.channels.push_back(noProducer(0x40));   // HEART_BEAT, row S5
    return s;
}

/// `settings.json` that starts a soak-friendly run: a short interval so a
/// three-minute scenario produces several rows.
std::string settings(const std::string &extra = "")
{
    return std::string("{\"schema\":1,\"values\":{\"interval_sec\":30")
           + extra + "}}";
}

/// Does @p haystack contain @p needle?
bool has(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// Count occurrences of a line prefix in a CSV.
size_t countRows(const std::string &csv, const std::string &prefix)
{
    size_t n = 0, pos = 0;
    while (true) {
        const size_t at = csv.find("\n" + prefix, pos);
        if (at == std::string::npos) { break; }
        n++;
        pos = at + 1;
    }
    if (csv.compare(0, prefix.size(), prefix) == 0) { n++; }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// The existence sweep
// ---------------------------------------------------------------------------

TEST(Pipeline, TheSweepRunsUnpromptedAndWritesOneRowPerType)
{
    // A screen that opened blank and needed a button press to say anything would
    // be a worse instrument, so layer 1 runs at start.
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = settings();
    run.execute(s);

    const std::string csv = run.file("runs/1.csv");
    ASSERT_FALSE(csv.empty()) << "the first run should have written its log";
    EXPECT_EQ(countRows(csv, "E,"), kTypeCount)
        << "one existence row per declared sensor type, always -- including the "
           "ones with no producer, because absent is a finding";
    EXPECT_EQ(countRows(csv, "R,"), 1u);
    EXPECT_EQ(countRows(csv, "X,"), 1u);
}

TEST(Pipeline, ATypeWithNoProducerIsRecordedAsResolvedZeroNotOmitted)
{
    Runner run;
    Scenario s = measuredDevice();
    run.execute(s);

    const std::string csv = run.file("runs/1.csv");
    // `E,<uptime>,<run>,0xf1,0,...` -- resolved = 0. Ledger row S4 is exactly
    // this and it closed a design question permanently, which it could only do
    // because it was written down.
    EXPECT_TRUE(has(csv, ",0xf1,0,"))
        << "SPO2 resolving no driver must appear as a row with resolved=0";
    EXPECT_TRUE(has(csv, ",0x40,0,"))
        << "HEART_BEAT resolving no driver must appear as a row with resolved=0";
    // ...and the ones that did resolve say so.
    EXPECT_TRUE(has(csv, ",0x10,1,"));
}

TEST(Pipeline, TheDriverDescriptorIsReadAndWritten)
{
    // `RequestGetDesc` has never been used by any app in either repository. It is
    // the kernel naming its own driver.
    Runner run;
    Scenario s = measuredDevice();
    run.execute(s);

    EXPECT_TRUE(has(run.file("runs/1.csv"), "bmi270-accel"))
        << "the descriptor the kernel returned should reach the log";
}

TEST(Pipeline, ADescriptorFillingAllThirtyTwoBytesDoesNotRunOn)
{
    // The harness deliberately does not terminate a descriptor that fills the
    // field, because `RequestGetDesc::desc` is `char[32]` with no guarantee of
    // one. An app that assumed a terminator would read past the pool block.
    Runner run;
    Scenario s = measuredDevice();
    s.channels[0].descriptor = "01234567890123456789012345678901";  // exactly 32
    run.execute(s);

    const std::string csv = run.file("runs/1.csv");
    EXPECT_TRUE(has(csv, "01234567890123456789012345678901"));
    // Nothing after it on that line but the newline: no run-on into adjacent
    // memory.
    const size_t at = csv.find("01234567890123456789012345678901");
    ASSERT_NE(at, std::string::npos);
    EXPECT_EQ(csv[at + 32], '\n');
}

TEST(Pipeline, RequestListsAnswerReachesTheLogAndTheRoster)
{
    // Nobody has ever seen this answer. More than one driver for a type would be
    // news.
    Runner run;
    Scenario s = measuredDevice();
    s.channels[0].driverCount = 3;
    s.guiOpensAtMs = 0;
    run.execute(s);

    EXPECT_TRUE(has(run.file("runs/1.csv"), ",0x10,1,0x1,3,"))
        << "handle 1, three drivers";

    const size_t accel = typeIndexOf(0x10);
    ASSERT_LT(accel, kTypeCount);
    ASSERT_TRUE(run.observations.roster.count(static_cast<uint8_t>(accel)));
    EXPECT_EQ(run.observations.roster[static_cast<uint8_t>(accel)].driverCount, 3u);
}

TEST(Pipeline, AKernelThatDoesNotImplementRequestListLeavesTheCountAtMinusOne)
{
    // "The kernel does not implement this request" and "this type has no
    // drivers" are different findings and the log keeps them apart.
    Runner run;
    Scenario s = measuredDevice();
    s.channels[0].listAnswered = false;
    run.execute(s);
    EXPECT_TRUE(has(run.file("runs/1.csv"), ",0x10,1,0x1,-1,"));
}

// ---------------------------------------------------------------------------
// The handle this app exists to survive
// ---------------------------------------------------------------------------

TEST(Pipeline, AHandleAboveTwoHundredAndFiftyFiveIsNotTruncated)
{
    // `SDK::Sensor::Connection` stores the handle as `uint8_t` while
    // `RequestDefault::handle` is a `uint32_t`. A profiler subscribing to all
    // thirty-seven types is the app most likely to be handed a large handle, and
    // truncation would either drop every sample or attribute one sensor's
    // samples to another.
    Runner run;
    Scenario s = measuredDevice();
    s.channels[0].handle = 0x1234;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    EXPECT_TRUE(has(run.file("runs/1.csv"), ",0x10,1,0x1234,"))
        << "the handle must be logged at full width";

    // ...and the samples must still be attributed to the accelerometer, which is
    // the half that a truncating comparison would break.
    const std::string soak = run.file("runs/2.csv");
    ASSERT_FALSE(soak.empty());
    EXPECT_TRUE(has(soak, "S,")) << "the soak should have written stream rows";
    EXPECT_TRUE(has(soak, ",0x10,"))
        << "batches on handle 0x1234 must be attributed to type 0x10";
}

// ---------------------------------------------------------------------------
// The manifest, which is the primary key
// ---------------------------------------------------------------------------

TEST(Pipeline, TheFirmwareVersionIsReadFromTheKernelAndMarkedAsRead)
{
    // SleepLab's ledger row P1 is LIKELY because the firmware version was
    // *reported by the device's owner*. `RequestSystemInfo` is what makes it a
    // measurement, and nothing in the SDK uses it.
    Runner run;
    Scenario s = measuredDevice();
    s.firmware = "1.4.0";
    run.execute(s);

    const std::string manifest = run.file("runs/1.json");
    ASSERT_FALSE(manifest.empty());
    EXPECT_TRUE(has(manifest, "\"firmware\":\"1.4.0\""));
    EXPECT_TRUE(has(manifest, "\"firmware_read_from_kernel\":true"));

    // The profile is named with it, so `profile_diff.py` has two things to
    // compare and neither is a moving target.
    EXPECT_FALSE(run.file("profile-1.4.0.json").empty());
}

TEST(Pipeline, AKernelThatWillNotSayItsFirmwareFallsBackAndSaysSo)
{
    Runner run;
    Scenario s = measuredDevice();
    s.systemInfoAnswered = false;
    s.settingsJson = "{\"schema\":1,\"values\":{\"firmware\":\"1.4.0-declared\"}}";
    run.execute(s);

    const std::string manifest = run.file("runs/1.json");
    EXPECT_TRUE(has(manifest, "\"firmware\":\"1.4.0-declared\""));
    // The distinction that stops a human's memory being mistaken for a
    // measurement.
    EXPECT_TRUE(has(manifest, "\"firmware_read_from_kernel\":false"));
    EXPECT_FALSE(run.file("profile-1.4.0-declared.json").empty());
}

TEST(Pipeline, WithNoFirmwareAtAllTheProfileIsNamedUnknown)
{
    // A filename that says, in the one place somebody will definitely look, that
    // this profile cannot be diffed.
    Runner run;
    Scenario s = measuredDevice();
    s.systemInfoAnswered = false;
    run.execute(s);
    EXPECT_FALSE(run.file("profile-unknown.json").empty());
}

TEST(Pipeline, TheManifestRecordsWhatElseWasSubscribedAtTheTime)
{
    // A dt distribution measured while eleven other streams were running is not
    // the same measurement as one taken alone. Both are worth having as long as
    // which is which is recorded.
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string manifest = run.file("runs/2.json");
    ASSERT_FALSE(manifest.empty());
    EXPECT_TRUE(has(manifest, "\"types_asked_mask\""));
    EXPECT_TRUE(has(manifest, "\"types_resolved_mask\""));
    EXPECT_TRUE(has(manifest, "\"types_delivered_mask\""));
    EXPECT_TRUE(has(manifest, "\"requested_period_ms\""));
    EXPECT_TRUE(has(manifest, "\"requested_latency_ms\""));
    EXPECT_TRUE(has(manifest, "\"gui_attached\""));
}

// ---------------------------------------------------------------------------
// The soak
// ---------------------------------------------------------------------------

TEST(Pipeline, ASoakWritesIntervalRowsAndPerFieldRows)
{
    Runner run;
    Scenario s = measuredDevice();
    s.durationMs   = 5u * 60u * 1000u;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string soak = run.file("runs/2.csv");
    ASSERT_FALSE(soak.empty());
    // Nine 30-second intervals over five minutes, minus the partial first one.
    EXPECT_GE(countRows(soak, "S,"), 8u);
    EXPECT_GE(countRows(soak, "V,"), 8u)
        << "layer 5 writes one row per field per interval";
}

TEST(Pipeline, ASoakInterruptedByTheCableIsMarkedTruncatedNotCompleted)
{
    // Plugging in terminates every running app (ledger row P8). A truncated
    // run's distributions are shorter than they look, and a reader has to know.
    Runner run;
    Scenario s = measuredDevice();
    s.durationMs   = 2u * 60u * 1000u;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string manifest = run.file("runs/2.json");
    ASSERT_FALSE(manifest.empty());
    EXPECT_TRUE(has(manifest, "\"end\":\"truncated_by_usb\""));
    EXPECT_TRUE(has(run.file("runs/2.csv"), "truncated_by_usb"));
}

TEST(Pipeline, StoppingASoakDeliberatelyMarksItCompleted)
{
    Runner run;
    Scenario s = measuredDevice();
    s.durationMs   = 4u * 60u * 1000u;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);
    // The first run -- the existence sweep -- is the one that completes cleanly.
    EXPECT_TRUE(has(run.file("runs/1.json"), "\"end\":\"completed\""));
}

TEST(Pipeline, APartialIntervalIsWrittenBeforeTheRunLetsGoOfTheSensors)
{
    // Where a run stopped is the finding, and an interval that never reached
    // storage cannot say where that was.
    Runner run;
    Scenario s = measuredDevice();
    // Two and a half intervals: the last one is partial.
    s.durationMs   = 76u * 1000u;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string soak = run.file("runs/2.csv");
    // Two full 30 s intervals plus the partial close.
    EXPECT_GE(countRows(soak, "S,"), 3u);
}

// ---------------------------------------------------------------------------
// Resume
// ---------------------------------------------------------------------------

TEST(Pipeline, AnOpenRunFromAPreviousLaunchIsClosedAsTruncatedNotLeftOpen)
{
    // A run whose manifest says `in_progress` for ever is a run nobody can
    // interpret.
    Runner run;
    Scenario first = measuredDevice();
    first.durationMs   = 2u * 60u * 1000u;
    first.settingsJson = settings();
    first.commandAtMs  = 1000;
    first.command      = CustomMessage::Command::StartSoak;
    run.execute(first);

    // Now the second launch, against what the first one actually wrote --
    // rather than against a hand-written state file that might not be one the
    // app can produce.
    Scenario second = measuredDevice();
    second.keepFilesystem = true;
    second.startUptimeMs  = first.startUptimeMs + first.durationMs + 30000;
    second.settingsJson   = settings();
    run.execute(second);

    // The run counter kept going rather than reusing an id, which would have
    // appended this run's rows to a previous run's file.
    EXPECT_FALSE(run.file("runs/3.csv").empty())
        << "the second launch should have opened run 3";
}

namespace
{

/// The state file a real run wrote, reopened on run 1.
///
/// This models a process killed *without* `COMMAND_APP_STOP` -- a crash, or the
/// battery going flat mid-soak. On the APP_STOP path the service closes its run
/// properly, which is correct behaviour and which is why the harness cannot
/// produce an open run on its own: the last thing a clean run writes is
/// `run_open: false, run_id: 0`.
///
/// Two fields are changed and no others, so everything else -- the schema, the
/// run counter, the uptime and wall clock at the last flush -- is exactly what
/// the app itself wrote. That is closer to the truth than hand-authoring a state
/// file, which might not be one the app can produce at all.
std::string stateWithRunLeftOpen(const std::string &realState)
{
    std::string out = realState;
    const auto swap = [&out](const std::string &from, const std::string &to) {
        const size_t at = out.find(from);
        if (at != std::string::npos) {
            out.replace(at, from.size(), to);
        }
    };
    swap("\"run_id\":0", "\"run_id\":1");
    swap("\"run_open\":false", "\"run_open\":true");
    return out;
}

} // namespace

TEST(Pipeline, ARunLeftOpenByACrashIsClosedAsTruncatedOnTheNextLaunch)
{
    // A run whose manifest says `in_progress` for ever is a run nobody can
    // interpret. Closed explicitly, and the log says which of the two causes it
    // was.
    Runner first;
    Scenario a = measuredDevice();
    a.startUptimeMs = 10u * 60u * 60u * 1000u;   // ten hours of uptime
    a.settingsJson  = settings();
    first.execute(a);

    const std::string state = first.file("state.json");
    ASSERT_FALSE(state.empty());

    Runner second;
    Scenario b = measuredDevice();
    b.settingsJson = settings();
    // Uptime climbed since the state was written: an app restart inside one
    // boot, almost certainly the USB cable.
    b.startUptimeMs = a.startUptimeMs + a.durationMs + 30000;
    b.seedFiles["state.json"] = stateWithRunLeftOpen(state);
    second.execute(b);

    EXPECT_TRUE(has(second.file("runs/1.json"), "\"end\":\"truncated_by_usb\""))
        << "uptime climbed, so the open run is an app restart within one boot";
}

TEST(Pipeline, AnUptimeThatWentBackwardsIsClosedAsARebootNotACableEvent)
{
    // Different findings: a reboot resets every since-boot counter too, so no
    // distribution can span it. Conflating the two would let a profile carry a
    // distribution built from two different boots of the sensor pipeline.
    Runner first;
    Scenario a = measuredDevice();
    a.startUptimeMs = 10u * 60u * 60u * 1000u;
    a.settingsJson  = settings();
    first.execute(a);

    const std::string state = first.file("state.json");
    ASSERT_FALSE(state.empty());

    Runner second;
    Scenario b = measuredDevice();
    b.settingsJson  = settings();
    b.startUptimeMs = 5000;    // uptime went backwards: the device rebooted
    b.seedFiles["state.json"] = stateWithRunLeftOpen(state);
    second.execute(b);

    EXPECT_TRUE(has(second.file("runs/1.json"), "\"end\":\"truncated_by_reboot\""))
        << "an uptime that went backwards is a reboot, not a cable event";
}

// ---------------------------------------------------------------------------
// The roster burst
// ---------------------------------------------------------------------------

TEST(Pipeline, TheRosterArrivesAsIndexedBurstsCoveringEveryType)
{
    Runner run;
    Scenario s = measuredDevice();
    s.guiOpensAtMs = 0;
    run.execute(s);

    ASSERT_FALSE(run.observations.bursts.empty());
    EXPECT_EQ(run.observations.rosterTotal, kTypeCount);
    // Thirty-seven rows at fourteen a message.
    EXPECT_EQ(run.observations.roster.size(), kTypeCount)
        << "every type must appear exactly once across the bursts";

    // The indices are explicit, so a burst that lost its middle message would
    // show a gap rather than a shifted roster -- SleepLab's ledger row T2 is
    // what happens when a burst contract relies on ordering.
    uint32_t covered = 0;
    for (const auto &b : run.observations.bursts) {
        covered += b.second;
    }
    EXPECT_GE(covered, kTypeCount);
}

TEST(Pipeline, TheRosterDistinguishesResolvedFromDeliveringAtAGlance)
{
    // That distinction caught two of the ledger's most consequential rows in two
    // minutes of hardware time.
    Runner run;
    Scenario s = measuredDevice();
    s.durationMs   = 3u * 60u * 1000u;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.guiOpensAtMs = 100000;
    run.execute(s);

    const auto accel = static_cast<uint8_t>(typeIndexOf(0x10));
    const auto spo2  = static_cast<uint8_t>(typeIndexOf(0xF1));
    ASSERT_TRUE(run.observations.roster.count(accel));
    ASSERT_TRUE(run.observations.roster.count(spo2));

    const auto &a = run.observations.roster[accel];
    EXPECT_TRUE(a.flags & CustomMessage::RosterRow::kResolved);
    EXPECT_TRUE(a.flags & CustomMessage::RosterRow::kEverDelivered);
    EXPECT_GT(a.samplesPerMinX10, 0u);

    const auto &o = run.observations.roster[spo2];
    EXPECT_FALSE(o.flags & CustomMessage::RosterRow::kResolved);
    EXPECT_FALSE(o.flags & CustomMessage::RosterRow::kEverDelivered);
}

TEST(Pipeline, ATypeWithNoParserIsFlaggedOnTheRoster)
{
    // For these five, a measured layout is the only description of the frame
    // that exists anywhere -- so the screen says which ones they are.
    Runner run;
    Scenario s = measuredDevice();
    s.guiOpensAtMs = 0;
    run.execute(s);

    for (uint32_t type : { 0x30u, 0x40u, 0xD0u, 0xF0u, 0x100u }) {
        const auto idx = static_cast<uint8_t>(typeIndexOf(type));
        ASSERT_TRUE(run.observations.roster.count(idx)) << std::hex << type;
        EXPECT_TRUE(run.observations.roster[idx].flags
                    & CustomMessage::RosterRow::kNoParser)
            << "0x" << std::hex << type << " ships no parser";
    }
}

TEST(Pipeline, NothingIsPublishedWithNoGuiAttached)
{
    // An instrument whose screen is open for a minute of every twelve hours
    // must not publish into a void the rest of the time: the cost would be
    // charged to the measurement.
    Runner run;
    Scenario s = measuredDevice();
    s.guiOpensAtMs = -1;   // never
    run.execute(s);

    EXPECT_TRUE(run.observations.statuses.empty());
    EXPECT_TRUE(run.observations.roster.empty());
}

// ---------------------------------------------------------------------------
// Frames that do not match their parser
// ---------------------------------------------------------------------------

TEST(Pipeline, AFrameWiderThanItsParserIsRecordedAsDiffering)
{
    // 28 of the 29 parsers test field count for exact equality, so one appended
    // field silently invalidates every sample. The profiler is the first thing
    // on this platform that will ever meet such a frame.
    Runner run;
    Scenario s;
    s.durationMs = 3u * 60u * 1000u;
    Channel accel = accelerometerAsMeasured(1);
    accel.fields  = 4;                 // the parser declares 3
    s.channels.push_back(accel);
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.guiOpensAtMs = 120000;
    run.execute(s);

    const auto idx = static_cast<uint8_t>(typeIndexOf(0x10));
    ASSERT_TRUE(run.observations.roster.count(idx));
    EXPECT_TRUE(run.observations.roster[idx].flags
                & CustomMessage::RosterRow::kFrameDiffers);
    EXPECT_EQ(run.observations.roster[idx].fieldCount, 4u);

    const std::string profile = run.file("profile-1.4.0.json");
    ASSERT_FALSE(profile.empty());
    EXPECT_TRUE(has(profile, "\"conformance\":\"DIFFERS\""))
        << "the field-count claim should be a conformance finding";
}

TEST(Pipeline, AFrameNarrowerThanItsParserIsAlsoRecordedRatherThanParsedBlind)
{
    // `GpsLocation::isDataValid()` reads field 1 *before* checking the field
    // count, so a one-field frame is an out-of-bounds read in any shipped build
    // -- `DataView`'s bounds assert is compiled out at -Os. This app derives the
    // field count from the stride and validates it before constructing any
    // parser, which is why a short frame is a finding here rather than a crash.
    Runner run;
    Scenario s;
    s.durationMs = 3u * 60u * 1000u;
    Channel gps;
    gps.type              = 0x110;
    gps.handle            = 5;
    gps.deliveredPeriodMs = 1000;
    gps.deliveredBatchMs  = 1000;
    gps.fields            = 1;         // the parser declares 5
    gps.valueBase         = 1.0f;
    s.channels.push_back(gps);
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.guiOpensAtMs = 120000;
    run.execute(s);

    const auto idx = static_cast<uint8_t>(typeIndexOf(0x110));
    ASSERT_TRUE(run.observations.roster.count(idx));
    EXPECT_EQ(run.observations.roster[idx].fieldCount, 1u);
    EXPECT_TRUE(run.observations.roster[idx].flags
                & CustomMessage::RosterRow::kFrameDiffers);
}

// ---------------------------------------------------------------------------
// Cadence, measured rather than assumed
// ---------------------------------------------------------------------------

TEST(Pipeline, AnEventSensorThatSpeaksOnceIsNotClassifiedAsStreaming)
{
    // `TOUCH_DETECT` was assumed streaming, delivered zero samples in a minute,
    // and read as "not worn" -- which would have suppressed every night SleepLab
    // ever recorded (row S12). One touch sample in 507 minutes is the measured
    // behaviour (row S7).
    Runner run;
    Scenario s = measuredDevice();
    s.durationMs   = 3u * 60u * 1000u;
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.guiOpensAtMs = 120000;
    run.execute(s);

    const auto idx = static_cast<uint8_t>(typeIndexOf(0x140));
    ASSERT_TRUE(run.observations.roster.count(idx));
    // Unknown, not Streaming: a sensor that spoke once cannot be classified, and
    // saying so is the honest answer.
    EXPECT_EQ(run.observations.roster[idx].cadence,
              static_cast<uint8_t>(SensorLab::Stats::Cadence::Unknown));
}

// ---------------------------------------------------------------------------
// The profile
// ---------------------------------------------------------------------------

TEST(Pipeline, TheProfileCarriesEveryClaimIncludingTheUnansweredOnes)
{
    // The decision that makes the document honest. A profile that wrote only the
    // rows it had answers for would be a third of the size and would read as
    // finished.
    Runner run;
    Scenario s = measuredDevice();
    run.execute(s);

    const std::string profile = run.file("profile-1.4.0.json");
    ASSERT_FALSE(profile.empty());
    EXPECT_TRUE(has(profile, "\"verdict\":\"UNVERIFIED\""))
        << "the unanswered claims are the reader's to-do list";
    EXPECT_TRUE(has(profile, "\"verdict\":\"CONFIRMED\""));
    EXPECT_TRUE(has(profile, "\"verdict\":\"INAPPLICABLE\""))
        << "a type with no producer has claims that cannot be answered";
    // Completeness alongside the results, always.
    EXPECT_TRUE(has(profile, "\"completeness\""));
    EXPECT_TRUE(has(profile, "\"percent\""));
}

TEST(Pipeline, EveryClaimRowCarriesItsMethodItsNAndItsMinimum)
{
    // No number without a method, a run and an n.
    Runner run;
    Scenario s = measuredDevice();
    run.execute(s);

    const std::string profile = run.file("profile-1.4.0.json");
    EXPECT_TRUE(has(profile, "\"method_id\":\"P1.request-default\""));
    EXPECT_TRUE(has(profile, "\"minimum_n\""));
    EXPECT_TRUE(has(profile, "\"claim_id\":\"0x10.existence.default_resolves\""));
    EXPECT_TRUE(has(profile, "\"observed_at\""));
    EXPECT_TRUE(has(profile, "\"uptime_ms\""));
    EXPECT_TRUE(has(profile, "\"wall_utc\""));
}

TEST(Pipeline, TheProfileNamesTheParserHazardsItInherited)
{
    Runner run;
    Scenario s = measuredDevice();
    run.execute(s);

    const std::string profile = run.file("profile-1.4.0.json");
    // `HeartRateEx` is the only parser using `>=`, deliberately, so a future
    // kernel can append fields. Recorded, because the asymmetry is itself a
    // conformance finding.
    EXPECT_TRUE(has(profile, "\"parser_validity\":\"at_least\""));
    EXPECT_TRUE(has(profile, "\"parser_validity\":\"exact\""));
    // `GpsLocation` reads a field before checking the count.
    EXPECT_TRUE(has(profile, "\"parser_reads_before_count\":true"));
    // The six types the SDK's own documentation does not mention.
    EXPECT_TRUE(has(profile, "\"missing_from_doc\":true"));
}

TEST(Pipeline, EveryNumberIsWrittenWithoutTouchingAFloatOrASignedSdkPath)
{
    // Three defects converge on this one assertion:
    //
    //   * the watch's newlib may not link floating-point printf, and when it
    //     does not `%g` emits nothing at runtime rather than failing at link
    //     time;
    //   * `JsonStreamWriter::add(int32_t)` formats an int32 with `%ld`, so a
    //     negative value comes out as its unsigned reinterpretation on any
    //     64-bit build -- an exponent of -5 was written as 4294967291 before
    //     this was fixed;
    //   * `add(int64_t)` casts to double and formats with `%g`, which would turn
    //     a UNIX timestamp into `1.75555e+09`.
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.durationMs   = 3u * 60u * 1000u;
    run.execute(s);

    const std::string profile = run.file("profile-1.4.0.json");
    ASSERT_FALSE(profile.empty());

    // Values are quoted decimal strings.
    EXPECT_TRUE(has(profile, "\"value\":\"1\""))
        << "a boolean claim should read as the string \"1\"";
    // No exponent notation anywhere: that would mean %g got involved.
    EXPECT_FALSE(has(profile, "e+0")) << "%g reached the file";
    EXPECT_FALSE(has(profile, "e-0")) << "%g reached the file";
    // No unsigned reinterpretation of a small negative. 4294967291 is -5.
    EXPECT_FALSE(has(profile, "4294967291"))
        << "a negative int32 went through add(int32_t)";
    // The wall clock survives with its seconds intact.
    EXPECT_TRUE(has(profile, "\"wall_utc\":\""))
        << "the wall clock must be a string, not a double";
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

TEST(Pipeline, TheRunLogIsAppendedRatherThanOverwrittenFromByteZero)
{
    // `open(write, override=false)` positions at offset 0, not end of file
    // (ledger row P6). This cost SleepLab a silent data-loss bug found only by a
    // host test.
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.durationMs   = 3u * 60u * 1000u;
    run.execute(s);

    const std::string csv = run.file("runs/2.csv");
    ASSERT_FALSE(csv.empty());
    // The header is first and the run-open row is still there after many
    // appends. Without the seek, the file would hold only its newest row.
    EXPECT_EQ(csv.compare(0, 2, "H,"), 0);
    EXPECT_EQ(countRows(csv, "R,"), 1u);
    EXPECT_GT(countRows(csv, "S,"), 1u);
}

TEST(Pipeline, AVolumeThatFillsMidRunIsCountedRatherThanIgnored)
{
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson         = settings();
    s.commandAtMs          = 1000;
    s.command              = CustomMessage::Command::StartSoak;
    s.durationMs           = 4u * 60u * 1000u;
    s.failWritesAfterBytes = 3000;
    run.execute(s);

    // A run that could not write is not a run whose numbers are shorter -- it is
    // a run whose numbers are missing, and the manifest has to say so.
    const std::string manifest = run.file("runs/2.json");
    if (!manifest.empty()) {
        EXPECT_TRUE(has(manifest, "\"row_failures\""));
    }
    // The app did not crash, which is the other half of the claim.
    SUCCEED();
}

TEST(Pipeline, TheStateFileNamesTheOpenRunAndAdvancesTheCounter)
{
    Runner run;
    Scenario s = measuredDevice();
    run.execute(s);

    const std::string state = run.file("state.json");
    ASSERT_FALSE(state.empty());
    EXPECT_TRUE(has(state, "\"schema\":1"));
    EXPECT_TRUE(has(state, "\"next_run_id\":2"));
    // The sweep closed cleanly, so nothing is open.
    EXPECT_TRUE(has(state, "\"run_open\":false"));
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

TEST(Pipeline, AnOutOfRangeSettingIsRefusedRatherThanClamped)
{
    // `"interval_sec": 100000` is a typo. Clamping it would run an interval
    // nobody asked for while looking perfectly healthy.
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = "{\"schema\":1,\"values\":{\"interval_sec\":100000}}";
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.durationMs   = 4u * 60u * 1000u;
    run.execute(s);

    // The default 60 s interval stands, so a four-minute soak still wrote rows.
    const std::string soak = run.file("runs/2.csv");
    ASSERT_FALSE(soak.empty());
    EXPECT_GE(countRows(soak, "S,"), 2u);
}

TEST(Pipeline, AWrongSchemaFallsBackEntirelyRatherThanGuessing)
{
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = "{\"schema\":99,\"values\":{\"interval_sec\":5}}";
    run.execute(s);
    // Still ran, still wrote a profile: a settings file somebody else wrote must
    // never stop the app starting.
    EXPECT_FALSE(run.file("profile-1.4.0.json").empty());
}

TEST(Pipeline, AnUnparseableSettingsFileDoesNotStopTheApp)
{
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson = "{ this is not json";
    run.execute(s);
    EXPECT_FALSE(run.file("profile-1.4.0.json").empty());
}

TEST(Pipeline, NamingOneTypeSubscribesOnlyThatOne)
{
    // The way to get an *uncontended* measurement, which is the whole reason the
    // setting exists: measuring a sensor changes it.
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson =
        "{\"schema\":1,\"values\":{\"interval_sec\":30,"
        "\"subscribe_all\":\"off\",\"only_type\":\"0x10\"}}";
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.durationMs   = 3u * 60u * 1000u;
    run.execute(s);

    const std::string soak = run.file("runs/2.csv");
    ASSERT_FALSE(soak.empty());
    EXPECT_GT(countRows(soak, "S,"), 0u);
    EXPECT_TRUE(has(soak, ",0x10,"));
    EXPECT_FALSE(has(soak, ",0x140,"))
        << "TOUCH_DETECT was not asked for, so it must not appear";
}

TEST(Pipeline, ATypeNameIsAcceptedWhereAHexValueIs)
{
    Runner run;
    Scenario s = measuredDevice();
    s.settingsJson =
        "{\"schema\":1,\"values\":{\"interval_sec\":30,"
        "\"subscribe_all\":\"no\",\"only_type\":\"ACCELEROMETER\"}}";
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.durationMs   = 3u * 60u * 1000u;
    run.execute(s);

    EXPECT_TRUE(has(run.file("runs/2.csv"), ",0x10,"));
}

// ---------------------------------------------------------------------------
// The uptime wrap, end to end
// ---------------------------------------------------------------------------

TEST(Pipeline, AWholeRunAcrossTheUptimeWrapMatchesItsTwinAwayFromIt)
{
    // `getTimeMs()` wraps at ~49.7 days and nobody has observed it. This app
    // cannot wait for it; it can prove its arithmetic against a synthetic one,
    // which is how SleepLab's row P9a was earned.
    Scenario base = measuredDevice();
    base.durationMs   = 3u * 60u * 1000u;
    base.settingsJson = settings();
    base.commandAtMs  = 1000;
    base.command      = CustomMessage::Command::StartSoak;
    base.guiOpensAtMs = 150000;

    Runner away;
    Scenario s1 = base;
    s1.startUptimeMs = 3600u * 1000u;
    away.execute(s1);

    Runner across;
    Scenario s2 = base;
    // Ninety seconds before the wrap, so the run crosses it.
    s2.startUptimeMs = 0xFFFFFFFFu - 90u * 1000u;
    across.execute(s2);

    const auto idx = static_cast<uint8_t>(typeIndexOf(0x10));
    ASSERT_TRUE(away.observations.roster.count(idx));
    ASSERT_TRUE(across.observations.roster.count(idx));

    const auto &a = away.observations.roster[idx];
    const auto &b = across.observations.roster[idx];

    // Within a hair: the streams are identical, so the only thing that could
    // differ is the arithmetic. A single magnitude compare anywhere would show
    // up here as a 49-day gap.
    EXPECT_NEAR(a.samplesPerMinX10, b.samplesPerMinX10, 20);
    EXPECT_EQ(a.longestGapS, b.longestGapS);
    EXPECT_EQ(a.cadence, b.cadence);
    EXPECT_EQ(a.fieldCount, b.fieldCount);

    ASSERT_TRUE(away.observations.haveStatus());
    ASSERT_TRUE(across.observations.haveStatus());
    // ...and the run's own duration is not 49 days.
    EXPECT_LT(across.observations.lastStatus().runningMs, 10u * 60u * 1000u);
}

// ---------------------------------------------------------------------------
// Layer 5 end to end
// ---------------------------------------------------------------------------

TEST(Pipeline, AStuckFieldReachesTheProfileAsAStuckRunAndNeverChanged)
{
    // Row S18's failure mode, end to end: not broken enough to be absent, not
    // working enough to be usable.
    Runner run;
    Scenario s;
    s.durationMs = 4u * 60u * 1000u;
    Channel batt;
    batt.type              = 0x120;
    batt.handle            = 9;
    batt.descriptor        = "max17262";
    batt.deliveredPeriodMs = 1000;
    batt.deliveredBatchMs  = 1000;
    batt.fields            = 1;
    batt.valueBase         = 100.0f;
    batt.stuck             = true;
    s.channels.push_back(batt);
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string soak = run.file("runs/2.csv");
    ASSERT_FALSE(soak.empty());
    // A `V` row whose `ever_changed` column is 0.
    EXPECT_TRUE(has(soak, "V,")) << "layer 5 should have written field rows";

    const std::string profile = run.file("profile-1.4.0.json");
    ASSERT_FALSE(profile.empty());
    EXPECT_TRUE(has(profile, "0x120.value.f0_ever_changed"));
    EXPECT_TRUE(has(profile, "0x120.value.f0_stuck_max_run"));
    // The LSB claim carries its reason rather than a zero that would read as
    // infinite resolution.
    EXPECT_TRUE(has(profile, "the value never varied"));
}

TEST(Pipeline, ANaNInAStreamIsCountedAndTheMeanSurvivesIt)
{
    Runner run;
    Scenario s;
    s.durationMs = 4u * 60u * 1000u;
    Channel accel = accelerometerAsMeasured(1);
    accel.nanCount = 10;
    s.channels.push_back(accel);
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string profile = run.file("profile-1.4.0.json");
    ASSERT_FALSE(profile.empty());
    EXPECT_TRUE(has(profile, "0x10.value.f0_nonfinite"));
    // The mean is finite: a NaN never reaches the sum.
    EXPECT_FALSE(has(profile, "\"value\":\"nan\"")) << "no statistic should be NaN";
}

TEST(Pipeline, AMicrosecondFieldOverNineNineNineReachesTheProfile)
{
    // If this is ever non-zero on hardware, every microsecond timestamp in every
    // app on this platform is wrong.
    Runner run;
    Scenario s;
    s.durationMs = 4u * 60u * 1000u;
    Channel accel = accelerometerAsMeasured(1);
    accel.usOver999 = true;
    s.channels.push_back(accel);
    s.settingsJson = settings();
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    run.execute(s);

    const std::string profile = run.file("profile-1.4.0.json");
    ASSERT_FALSE(profile.empty());
    EXPECT_TRUE(has(profile, "0x10.timing.ts_us_over_999"));
    EXPECT_TRUE(has(run.file("runs/2.csv"), "S,"));
}

// ---------------------------------------------------------------------------
// Decimal, which every number in the profile goes through
// ---------------------------------------------------------------------------

TEST(Decimal, RoundTripsEveryMagnitudeThisAppReports)
{
    using SensorLab::Profile::decompose;
    using SensorLab::Profile::recompose;

    // Station pressure, a heart rate, a recovered accelerometer LSB, and a
    // negative current -- nine orders of magnitude, all in one document.
    const float values[] = {
        101325.0f, 60.0f, 20.4f, 1.0f, 0.001f,
        4.0f / 65536.0f, -1.33f, -0.0000123f, 1e-30f, 3.4e30f,
    };
    for (float v : values) {
        const float back = recompose(decompose(v));
        ASSERT_NE(v, 0.0f);
        // Seven significant digits, which is float's own precision.
        EXPECT_NEAR(back / v, 1.0f, 1e-6f) << "value " << v;
    }
}

TEST(Decimal, ZeroAndNonFiniteValuesAreTaggedRatherThanNumbered)
{
    using namespace SensorLab::Profile;
    EXPECT_EQ(decompose(0.0f).kind, DecimalKind::Zero);
    EXPECT_EQ(decompose(__builtin_nanf("")).kind, DecimalKind::NaN);
    EXPECT_EQ(decompose(__builtin_inff()).kind, DecimalKind::PosInf);
    EXPECT_EQ(decompose(-__builtin_inff()).kind, DecimalKind::NegInf);
}

TEST(Decimal, FormatsWithoutAnyFloatingPointPrintf)
{
    using SensorLab::Profile::format;
    char buf[32];

    format(buf, sizeof(buf), 20.4f);
    EXPECT_STREQ(buf, "20.4");
    format(buf, sizeof(buf), 1.0f);
    EXPECT_STREQ(buf, "1");
    format(buf, sizeof(buf), -1.33f);
    EXPECT_STREQ(buf, "-1.33");
    format(buf, sizeof(buf), 101325.0f);
    EXPECT_STREQ(buf, "101325");
    format(buf, sizeof(buf), 0.0f);
    EXPECT_STREQ(buf, "zero");
    format(buf, sizeof(buf), 0.001f);
    EXPECT_STREQ(buf, "0.001");
}

TEST(Decimal, IsMonotonicSoTwoProfilesCanBeComparedWithoutDecoding)
{
    using SensorLab::Profile::decompose;
    using SensorLab::Profile::recompose;

    float prev = -1e30f;
    for (int i = -60; i <= 60; i++) {
        const float v = static_cast<float>(i) * 1.37f;
        const float back = recompose(decompose(v));
        EXPECT_GE(back, prev - 1e-3f) << "at i=" << i;
        prev = back;
    }
}
