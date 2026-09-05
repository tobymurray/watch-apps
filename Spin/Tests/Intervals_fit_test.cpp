/**
 ******************************************************************************
 * @file    Intervals_fit_test.cpp
 * @brief   That the laps in the .fit land on the session's own boundaries.
 ******************************************************************************
 *
 * Drives the real cursor over the real parser through the C ABI the Service
 * uses, writes a lap per step with the real ActivityWriter, and reads the
 * result back with the SDK's independent test FIT reader. Decoding a file
 * rather than reading the writer is the point: a lap that is one message short
 * of what was prescribed looks fine from the writing end.
 *
 * WHAT THIS DOES NOT COVER: Service.cpp itself, which is compiled by the app
 * build and by nothing else, so the clock that decides *when* a boundary comes
 * is only tested by riding it. This covers everything from the config string to
 * the bytes on storage.
 *
 ******************************************************************************
 */

#include "ActivityWriter.hpp"
#include "KernelTestDoubles.hpp"
#include "SpinEngine.hpp"
#include "fit/FitReader.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using testfit::FitReader;

extern "C" {

/// The Service's own, which the archive expects any host to provide. A panic
/// reaching here means a parser bug the Rust suite should have caught.
void spin_engine_host_panic(const uint8_t *msg, uint32_t len)
{
    FAIL() << "The engine panicked: " << std::string(reinterpret_cast<const char *>(msg), len);
}

} // extern "C"

namespace {

constexpr uint16_t kMesgSession     = 18;
constexpr uint16_t kMesgLap         = 19;
/// The two messages that define a *workout* file. An activity file carrying
/// them is a guess about what decoders tolerate, and this app does not make it.
constexpr uint16_t kMesgWorkout     = 26;
constexpr uint16_t kMesgWorkoutStep = 27;

constexpr uint8_t kFieldTotalTimer = 8;   // session/lap total_timer_time
constexpr uint8_t kFieldNumLaps    = 26;  // session.num_laps

constexpr std::time_t kStartUtc = 1782475200;  // 2026-06-26 12:00 UTC

/// The session ridden on 2026-09-04; see Tests/pulled/.
constexpr char kRealSession[] =
    "5m@2,6x(20s@5,40s@2),6m@2,3x(1m@5,1m@2),7m@2,2x(4m@4,3m@2)";

std::vector<uint8_t> readFit(const SDK::TestSupport::InMemoryFileSystem &fs)
{
    for (const auto &kv : fs.files) {
        const std::string &path = kv.first;
        if (kv.second.exists && path.size() > 4 &&
            path.compare(path.size() - 4, 4, ".fit") == 0) {
            const std::string s = fs.readFile(path);
            return std::vector<uint8_t>(s.begin(), s.end());
        }
    }
    return {};
}

/// Every step the session prescribes, in the order the cursor hands them over.
std::vector<uint16_t> stepSeconds(const char *text)
{
    std::vector<uint16_t> out;
    if (spin_intervals_load(reinterpret_cast<const uint8_t *>(text),
                            static_cast<uint32_t>(std::strlen(text))) == 0) {
        return out;
    }
    for (;;) {
        uint16_t seconds = 0;
        uint8_t effort = 0, rep = 0, reps = 0;
        if (!spin_intervals_step(&seconds, &effort, &rep, &reps)) {
            break;
        }
        out.push_back(seconds);
        if (!spin_intervals_advance()) {
            break;
        }
    }
    return out;
}

} // namespace

TEST(SpinIntervalsFit, EveryStepIsOneLapOfItsOwnLength)
{
    const std::vector<uint16_t> steps = stepSeconds(kRealSession);
    ASSERT_EQ(steps.size(), 25u) << "the prescription changed";

    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    w.start(info);

    // What the Service does at a boundary: close a lap of exactly the step that
    // ended, then move the cursor on.
    std::time_t at = kStartUtc;
    for (uint16_t seconds : steps) {
        ActivityWriter::LapData l;
        l.timestamp = at + seconds;
        l.timeStart = at;
        l.duration  = seconds;
        l.elapsed   = seconds;
        l.hrAvg     = 130.0f;
        l.hrMax     = 150.0f;
        w.addLap(l);
        at += seconds;
    }

    const uint32_t total = static_cast<uint32_t>(at - kStartUtc);
    ActivityWriter::TrackData track;
    track.timestamp = at;
    track.timeStart = kStartUtc;
    track.duration  = total;
    track.elapsed   = total;
    track.hrAvg     = 130.0f;
    track.hrMax     = 178.0f;
    ASSERT_TRUE(w.stop(track));

    FitReader reader(readFit(fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.crcValid());

    const auto laps = reader.withGlobal(kMesgLap);
    ASSERT_EQ(laps.size(), steps.size());
    for (size_t i = 0; i < steps.size(); ++i) {
        EXPECT_EQ(laps[i]->fields.at(kFieldTotalTimer).u(), steps[i] * 1000u)
            << "lap " << i << " does not match the step that prescribed it";
    }

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldNumLaps).u(), steps.size());
    // The laps have to add up to the session, or a consumer summing them gets a
    // different ride from the one the session claims.
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalTimer).u(), total * 1000u);
}

TEST(SpinIntervalsFit, TheFileRecordsWhatHappenedAndNotWhatWasAskedFor)
{
    const std::vector<uint16_t> steps = stepSeconds(kRealSession);
    ASSERT_FALSE(steps.empty());

    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");
    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    w.start(info);

    ActivityWriter::LapData l;
    l.timestamp = kStartUtc + steps[0];
    l.timeStart = kStartUtc;
    l.duration  = steps[0];
    l.elapsed   = steps[0];
    w.addLap(l);

    ActivityWriter::TrackData track;
    track.timestamp = kStartUtc + steps[0];
    track.timeStart = kStartUtc;
    track.duration  = steps[0];
    track.elapsed   = steps[0];
    ASSERT_TRUE(w.stop(track));

    const std::vector<uint8_t> bytes = readFit(fx.fileSystem);
    FitReader reader(bytes);
    ASSERT_TRUE(reader.ok());

    // workout and workout_step define a different FIT file type, and whether a
    // decoder tolerates them inside an activity file is untested. The
    // prescription goes to ../SharedData/spin_sessions.json instead.
    EXPECT_TRUE(reader.withGlobal(kMesgWorkout).empty());
    EXPECT_TRUE(reader.withGlobal(kMesgWorkoutStep).empty());

    // And the string itself is nowhere in the file.
    const std::string text(bytes.begin(), bytes.end());
    EXPECT_EQ(text.find("6x(20s@5"), std::string::npos);
}

TEST(SpinIntervalsFit, ARideWithNoSessionPrescribesNoLaps)
{
    // "0s" is off, and so is anything the grammar refuses: both leave the ride
    // to autoLapMinutes and the lap button, exactly as before this existed.
    EXPECT_TRUE(stepSeconds("0s").empty());
    EXPECT_TRUE(stepSeconds("").empty());
    EXPECT_TRUE(stepSeconds("10M").empty());
}
