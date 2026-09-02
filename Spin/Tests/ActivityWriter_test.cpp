/**
 ******************************************************************************
 * @file    ActivityWriter_test.cpp
 * @brief   What Spin actually writes, decoded back out of the .fit.
 *
 * Encodes a whole ride with the real writer and decodes it with an independent
 * reader, because none of these claims is observable from the watch.
 ******************************************************************************
 */

#include "ActivityWriter.hpp"
#include "KernelTestDoubles.hpp"
#include "SDK/Fit/FitProfile.hpp"
#include "fit/FitReader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace fit = SDK::Fit;
using testfit::FitReader;

namespace {

/// FIT global message numbers, spelled out where the profile header does not
/// name them, so an assertion reads as the message it is about.
constexpr uint16_t kMesgFileId  = 0;
constexpr uint16_t kMesgSession = 18;
constexpr uint16_t kMesgLap     = 19;
constexpr uint16_t kMesgRecord  = 20;
constexpr uint16_t kMesgEvent   = 21;

/// Field definition numbers used in the assertions below.
constexpr uint8_t kFieldSport         = 5;   // session.sport
constexpr uint8_t kFieldSubSport      = 6;   // session.sub_sport
constexpr uint8_t kFieldNumLaps       = 26;  // session.num_laps
// lap is 15/16, session 16/17, so lap.max_heart_rate and
// session.avg_heart_rate share the number 16: the session's constants read
// on a lap return max where avg was meant -- a wrong number, not a missing
// one.
constexpr uint8_t kSessionAvgHeartRate = 16;
constexpr uint8_t kSessionMaxHeartRate = 17;
constexpr uint8_t kLapAvgHeartRate     = 15;
constexpr uint8_t kLapMaxHeartRate     = 16;
// These two do agree across the two messages.
constexpr uint8_t kFieldTotalTimer    = 8;   // session/lap total_timer_time
constexpr uint8_t kFieldTotalElapsed  = 7;   // session/lap total_elapsed_time
constexpr uint8_t kFieldFileType      = 0;   // file_id.type
constexpr uint8_t kFieldRecordHr      = 3;   // record.heart_rate
constexpr uint8_t kFieldEventType     = 1;   // event.event_type

/// Developer field numbers, from ActivityWriter::DevField.
constexpr uint8_t kDevHrSource      = 4;
constexpr uint8_t kDevHrOptical     = 5;
constexpr uint8_t kDevHrExternal    = 6;
constexpr uint8_t kDevLapRestingCal = 7;

constexpr uint8_t kFieldTotalCalories      = 11;   // session/lap total_calories
constexpr uint8_t kFieldMetabolicCalories  = 196;  // session.metabolic_calories

// The neighbours each session field would land on if the lap's number were
// used by mistake. Spin measures no temperature, strokes or cadence, so none
// would look wrong from the writing end. See ActivityWriter.cpp.
constexpr uint8_t kFieldLapTimeInHrZone     = 57;
constexpr uint8_t kFieldSessionTimeInHrZone = 65;
constexpr uint8_t kFieldSessionAvgTemperature = 57;

constexpr uint8_t kFieldSessionTotalWork = 48;
constexpr uint8_t kFieldSessionAvgPower  = 20;
constexpr uint8_t kFieldSessionAvgStrokeCount = 41;   // where lap.total_work would land
constexpr uint8_t kFieldSessionMaxCadence     = 19;   // where lap.avg_power would land
constexpr uint8_t kFieldSessionMaxPower       = 21;   // the neighbour above avg_power

/// The file holds joules; the wearer enters kilojoules.
constexpr uint32_t kJoulesPerKilojoule = 1000;

/// Stored value is milliseconds; the app holds seconds.
constexpr uint32_t kTimeInHrZoneScale = 1000;

/// Decodes the flat uint32 array a time_in_hr_zone field carries.
std::vector<uint32_t> zoneMillis(const FitReader::Message &m, uint8_t fieldNum)
{
    const auto it = m.fields.find(fieldNum);
    if (it == m.fields.end()) {
        return {};
    }
    const std::vector<uint8_t> &raw = it->second.raw;
    std::vector<uint32_t> out;
    for (size_t i = 0; i + 4 <= raw.size(); i += 4) {
        out.push_back(uint32_t(raw[i]) | (uint32_t(raw[i + 1]) << 8) |
                      (uint32_t(raw[i + 2]) << 16) | (uint32_t(raw[i + 3]) << 24));
    }
    return out;
}

constexpr std::time_t kStartUtc = 1782475200;  // 2026-06-26 12:00 UTC

std::string findPath(const SDK::TestSupport::InMemoryFileSystem& fs, const char* suffix)
{
    const std::string want(suffix);
    for (const auto& kv : fs.files) {
        const std::string& path = kv.first;
        if (kv.second.exists && path.size() > want.size() &&
            path.compare(path.size() - want.size(), want.size(), want) == 0) {
            return path;
        }
    }
    return {};
}

std::vector<uint8_t> readFit(const SDK::TestSupport::InMemoryFileSystem& fs)
{
    const std::string path = findPath(fs, ".fit");
    if (path.empty()) {
        return {};
    }
    const std::string s = fs.readFile(path);
    return std::vector<uint8_t>(s.begin(), s.end());
}

/// One complete ride, in the order Service.cpp writes one, so the file under
/// test is the file the watch would produce.
struct Ride {
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter writer{fx.kernel, "Activity"};
    bool stopped = false;

    void run(uint32_t seconds, const std::vector<uint8_t>& bpm, uint8_t hrSource,
             uint16_t workKilojoules = 0)
    {
        ActivityWriter::AppInfo info;
        info.timestamp  = kStartUtc;
        info.appVersion = 0x00000400;
        info.devID      = "UNA";
        info.appID      = "spin";
        writer.start(info);

        float sum = 0.0f;
        float max = 0.0f;
        for (uint32_t i = 0; i < seconds; ++i) {
            ActivityWriter::RecordData r;
            r.timestamp = kStartUtc + i;
            const uint8_t beat = bpm.empty() ? 0 : bpm[i % bpm.size()];
            if (beat > 0) {
                r.set(ActivityWriter::RecordData::Field::HEART_RATE);
                r.heartRate     = static_cast<float>(beat);
                r.hrSource      = hrSource;
                r.hrExternalBpm = (hrSource == 2) ? beat : 0;
                r.hrOpticalBpm  = (hrSource == 1) ? beat : 0;
                sum += static_cast<float>(beat);
                max = std::max(max, static_cast<float>(beat));
            }
            writer.addRecord(r);
        }

        const float avg = bpm.empty() ? 0.0f : sum / static_cast<float>(seconds);
        const std::time_t end = kStartUtc + seconds;

        writer.pause(end);

        ActivityWriter::LapData lap;
        lap.timestamp = end;
        lap.timeStart = kStartUtc;
        lap.duration  = seconds;
        lap.elapsed   = seconds;
        lap.hrAvg     = avg;
        lap.hrMax     = max;
        writer.addLap(lap);

        ActivityWriter::TrackData track;
        track.timestamp = end;
        track.timeStart = kStartUtc;
        track.duration  = seconds;
        track.elapsed   = seconds;
        track.hrAvg     = avg;
        track.hrMax     = max;
        track.workKilojoules = workKilojoules;
        stopped = writer.stop(track);
    }
};

}  // namespace

// -- The claim the whole app exists to make ----------------------------------

TEST(SpinActivityWriter, SessionSaysIndoorCycling)
{
    Ride ride;
    ride.run(120, {130, 132, 135}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);

    // Both, or the file reads as an outdoor ride that recorded no distance.
    EXPECT_EQ(sessions[0]->fields.at(kFieldSport).u(),
              static_cast<uint64_t>(fit::Sport::Cycling));
    EXPECT_EQ(sessions[0]->fields.at(kFieldSubSport).u(),
              static_cast<uint64_t>(fit::SubSport::IndoorCycling));

    // The literal wire values: comparing only against the enum they came from
    // would pass just as happily if the enum were wrong.
    EXPECT_EQ(sessions[0]->fields.at(kFieldSport).u(), 2u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldSubSport).u(), 6u);
}

TEST(SpinActivityWriter, IsAnActivityFileWithAValidCrc)
{
    Ride ride;
    ride.run(60, {120}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.crcValid());
    EXPECT_EQ(reader.headerSize(), 14);

    const auto ids = reader.withGlobal(kMesgFileId);
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0]->fields.at(kFieldFileType).u(),
              static_cast<uint64_t>(fit::File::Activity));
}

TEST(SpinActivityWriter, CarriesExactlyOneLapCoveringTheRide)
{
    // Spin has no lap button, but a session with no lap is a session many FIT
    // consumers quietly drop. One lap, spanning the whole ride, is the floor.
    Ride ride;
    ride.run(300, {140}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto laps = reader.withGlobal(kMesgLap);
    ASSERT_EQ(laps.size(), 1u);

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldNumLaps).u(), 1u);

    // Both are in milliseconds, and both cover the ride rather than a slice.
    EXPECT_EQ(laps[0]->fields.at(kFieldTotalTimer).u(), 300u * 1000u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalTimer).u(), 300u * 1000u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalElapsed).u(), 300u * 1000u);
}

TEST(SpinActivityWriter, AutoLapProducesOneLapPerSplitAndCountsThemInTheSession)
{
    // N whole laps plus the remainder. Each lap's total_timer_time is its own
    // split, not a running total.
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    w.start(info);

    constexpr uint32_t kSplit = 300;   // 5 minutes
    constexpr uint32_t kWhole = 3;     // three full laps
    constexpr uint32_t kTail  = 120;   // and a short one at the end

    std::time_t lapStart = kStartUtc;
    for (uint32_t lap = 0; lap < kWhole; ++lap) {
        ActivityWriter::LapData l;
        l.timestamp = lapStart + kSplit;
        l.timeStart = lapStart;
        l.duration  = kSplit;
        l.elapsed   = kSplit;
        l.hrAvg     = 130.0f;
        l.hrMax     = 150.0f;
        w.addLap(l);
        lapStart += kSplit;
    }

    ActivityWriter::LapData tail;
    tail.timestamp = lapStart + kTail;
    tail.timeStart = lapStart;
    tail.duration  = kTail;
    tail.elapsed   = kTail;
    tail.hrAvg     = 120.0f;
    tail.hrMax     = 140.0f;
    w.addLap(tail);

    const uint32_t total = kWhole * kSplit + kTail;
    ActivityWriter::TrackData track;
    track.timestamp = kStartUtc + total;
    track.timeStart = kStartUtc;
    track.duration  = total;
    track.elapsed   = total;
    track.hrAvg     = 128.0f;
    track.hrMax     = 150.0f;
    ASSERT_TRUE(w.stop(track));

    FitReader reader(readFit(fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.crcValid());

    const auto laps = reader.withGlobal(kMesgLap);
    ASSERT_EQ(laps.size(), kWhole + 1u);
    for (uint32_t i = 0; i < kWhole; ++i) {
        EXPECT_EQ(laps[i]->fields.at(kFieldTotalTimer).u(), kSplit * 1000u)
            << "lap " << i << " is not one split long";
    }
    EXPECT_EQ(laps[kWhole]->fields.at(kFieldTotalTimer).u(), kTail * 1000u);

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldNumLaps).u(), kWhole + 1u);
    // The laps have to add up to the session, or a consumer summing them gets
    // a different ride from the one the session claims.
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalTimer).u(), total * 1000u);
}

TEST(SpinActivityWriter, RecordsHeartRateAndWhereItCameFrom)
{
    Ride ride;
    ride.run(10, {150}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto records = reader.withGlobal(kMesgRecord);
    ASSERT_EQ(records.size(), 10u);

    for (const auto* r : records) {
        EXPECT_EQ(r->fields.at(kFieldRecordHr).u(), 150u);
        // Not recoverable from the arbitrated number afterwards.
        EXPECT_EQ(r->devFields.at(kDevHrSource).u(), 2u);
        EXPECT_EQ(r->devFields.at(kDevHrExternal).u(), 150u);
        EXPECT_EQ(r->devFields.at(kDevHrOptical).u(), 0u);
    }
}

TEST(SpinActivityWriter, SummarisesHeartRateOnTheSessionAndTheLap)
{
    Ride ride;
    ride.run(4, {100, 200, 100, 200}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto sessions = reader.withGlobal(kMesgSession);
    const auto laps     = reader.withGlobal(kMesgLap);
    ASSERT_EQ(sessions.size(), 1u);
    ASSERT_EQ(laps.size(), 1u);

    EXPECT_EQ(sessions[0]->fields.at(kSessionAvgHeartRate).u(), 150u);
    EXPECT_EQ(sessions[0]->fields.at(kSessionMaxHeartRate).u(), 200u);
    EXPECT_EQ(laps[0]->fields.at(kLapAvgHeartRate).u(), 150u);
    EXPECT_EQ(laps[0]->fields.at(kLapMaxHeartRate).u(), 200u);
}

TEST(SpinActivityWriter, BracketsTheRideWithTimerEvents)
{
    Ride ride;
    ride.run(30, {120}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto events = reader.withGlobal(kMesgEvent);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0]->fields.at(kFieldEventType).u(),
              static_cast<uint64_t>(fit::EventType::Start));
    EXPECT_EQ(events[1]->fields.at(kFieldEventType).u(),
              static_cast<uint64_t>(fit::EventType::Stop));
}

TEST(SpinActivityWriter, PauseAndResumeAreTimerEventsNotGapsInTheRecords)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    w.start(info);

    ActivityWriter::RecordData r;
    r.timestamp = kStartUtc;
    w.addRecord(r);
    w.pause(kStartUtc + 10);
    w.resume(kStartUtc + 40);
    r.timestamp = kStartUtc + 41;
    w.addRecord(r);
    w.pause(kStartUtc + 50);

    ActivityWriter::LapData lap;
    lap.timestamp = kStartUtc + 50;
    lap.timeStart = kStartUtc;
    lap.duration  = 20;
    lap.elapsed   = 50;
    w.addLap(lap);

    ActivityWriter::TrackData track;
    track.timestamp = kStartUtc + 50;
    track.timeStart = kStartUtc;
    track.duration  = 20;   // active
    track.elapsed   = 50;   // wall clock
    ASSERT_TRUE(w.stop(track));

    FitReader reader(readFit(fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    // start, stop, start, stop -- a decoder reconstructs the paused span from
    // these, which is why the two totals below are allowed to differ.
    const auto events = reader.withGlobal(kMesgEvent);
    ASSERT_EQ(events.size(), 4u);
    EXPECT_EQ(events[0]->fields.at(kFieldEventType).u(), 0u);  // start
    EXPECT_EQ(events[1]->fields.at(kFieldEventType).u(), 1u);  // stop
    EXPECT_EQ(events[2]->fields.at(kFieldEventType).u(), 0u);  // start
    EXPECT_EQ(events[3]->fields.at(kFieldEventType).u(), 1u);  // stop

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalTimer).u(), 20u * 1000u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalElapsed).u(), 50u * 1000u);
}

TEST(SpinActivityWriter, RecordsTimeInEachHeartRateZone)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    constexpr uint8_t kZones = 5;

    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    info.zoneCount = kZones;
    w.start(info);

    ActivityWriter::RecordData r;
    r.timestamp = kStartUtc;
    w.addRecord(r);

    // A real ride warms up, works and eases off, so every bucket has something
    // in it; only kZones + 1 of them are written.
    std::time_t zones[ActivityWriter::kZoneBuckets] = {};
    const std::time_t used[] = {90, 240, 600, 900, 300, 60};
    for (size_t i = 0; i < sizeof(used) / sizeof(used[0]); ++i) {
        zones[i] = used[i];
    }
    std::time_t total = 0;
    for (std::time_t z : zones) {
        total += z;
    }

    ActivityWriter::LapData lap;
    lap.timestamp = kStartUtc + total;
    lap.timeStart = kStartUtc;
    lap.duration  = total;
    lap.elapsed   = total;
    for (size_t i = 0; i < ActivityWriter::kZoneBuckets; ++i) {
        lap.zoneSeconds[i] = zones[i];
    }
    w.addLap(lap);

    ActivityWriter::TrackData track;
    track.timestamp = kStartUtc + total;
    track.timeStart = kStartUtc;
    track.duration  = total;
    track.elapsed   = total;
    for (size_t i = 0; i < ActivityWriter::kZoneBuckets; ++i) {
        track.zoneSeconds[i] = zones[i];
    }
    ASSERT_TRUE(w.stop(track));

    FitReader reader(readFit(fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.crcValid());

    const auto sessions = reader.withGlobal(kMesgSession);
    const auto laps     = reader.withGlobal(kMesgLap);
    ASSERT_EQ(sessions.size(), 1u);
    ASSERT_EQ(laps.size(), 1u);

    // Each message uses its own field number, and they differ.
    const std::pair<const FitReader::Message *, uint8_t> targets[] = {
        {sessions[0], kFieldSessionTimeInHrZone},
        {laps[0], kFieldLapTimeInHrZone},
    };

    for (const auto &target : targets) {
        const std::vector<uint32_t> got = zoneMillis(*target.first, target.second);
        // One bucket per zone plus the one below zone 1 -- not the storage
        // array's length. A five-zone ride writes six, an eight-zone ride nine.
        ASSERT_EQ(got.size(), static_cast<size_t>(kZones) + 1u)
            << "field " << unsigned(target.second) << " is missing or the wrong length";
        for (size_t i = 0; i < static_cast<size_t>(kZones) + 1u; ++i) {
            EXPECT_EQ(got[i], static_cast<uint32_t>(zones[i]) * kTimeInHrZoneScale)
                << "bucket " << i;
        }
        uint32_t sum = 0;
        for (uint32_t v : got) {
            sum += v;
        }
        EXPECT_EQ(sum, static_cast<uint32_t>(total) * kTimeInHrZoneScale)
            << "the buckets should account for every active second";
    }

    // The assertion that would have caught the bug the profile lookup found:
    // 57 on the session is avg_temperature.
    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionAvgTemperature), 0u)
        << "the session wrote something into avg_temperature";
}

TEST(SpinActivityWriter, TheZoneArrayIsAsLongAsTheRideHasZones)
{
    // So a reader sees exactly the zones that existed, rather than a fixed
    // array padded with zeros.
    for (uint8_t zoneCount : {0, 3, 5, 8}) {
        SDK::TestSupport::KernelFixture fx;
        ActivityWriter w(fx.kernel, "Activity");

        ActivityWriter::AppInfo info;
        info.timestamp = kStartUtc;
        info.devID     = "UNA";
        info.appID     = "spin";
        info.zoneCount = zoneCount;
        w.start(info);

        ActivityWriter::RecordData r;
        r.timestamp = kStartUtc;
        w.addRecord(r);

        ActivityWriter::LapData lap;
        lap.timestamp = kStartUtc + 60;
        lap.timeStart = kStartUtc;
        lap.duration  = 60;
        lap.elapsed   = 60;
        for (size_t i = 0; i <= zoneCount; ++i) {
            lap.zoneSeconds[i] = 10;
        }
        w.addLap(lap);

        ActivityWriter::TrackData t;
        t.timestamp = kStartUtc + 60;
        t.timeStart = kStartUtc;
        t.duration  = 60;
        t.elapsed   = 60;
        for (size_t i = 0; i <= zoneCount; ++i) {
            t.zoneSeconds[i] = 10;
        }
        ASSERT_TRUE(w.stop(t)) << "zoneCount " << unsigned(zoneCount);

        FitReader reader(readFit(fx.fileSystem));
        ASSERT_TRUE(reader.ok()) << "zoneCount " << unsigned(zoneCount);
        EXPECT_TRUE(reader.crcValid()) << "zoneCount " << unsigned(zoneCount);

        const auto sessions = reader.withGlobal(kMesgSession);
        ASSERT_EQ(sessions.size(), 1u);
        const std::vector<uint32_t> got =
            zoneMillis(*sessions[0], kFieldSessionTimeInHrZone);

        if (zoneCount == 0) {
            // No zones set means no zone field at all, not an array of zeros.
            EXPECT_TRUE(got.empty()) << "a ride with no zones wrote a zone array";
        } else {
            EXPECT_EQ(got.size(), static_cast<size_t>(zoneCount) + 1u)
                << "zoneCount " << unsigned(zoneCount);
        }
    }
}

TEST(SpinActivityWriter, RecordsActiveAndRestingCalories)
{
    Ride ride;
    ride.run(60, {150}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto sessions = reader.withGlobal(kMesgSession);
    const auto laps     = reader.withGlobal(kMesgLap);
    ASSERT_EQ(sessions.size(), 1u);
    ASSERT_EQ(laps.size(), 1u);

    // The harness leaves calories at zero, so this asserts the fields are
    // present rather than any particular figure.
    EXPECT_EQ(sessions[0]->fields.count(kFieldTotalCalories), 1u);
    EXPECT_EQ(sessions[0]->fields.count(kFieldMetabolicCalories), 1u);
    EXPECT_EQ(laps[0]->fields.count(kFieldTotalCalories), 1u);
    EXPECT_EQ(laps[0]->devFields.count(kDevLapRestingCal), 1u);
}

TEST(SpinActivityWriter, CaloriesRoundRatherThanTruncate)
{
    // kcal is a uint16 in the file and a float in the app, so the .5 has to go
    // somewhere. 401.6 kcal is 402, not 401.
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");
    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    w.start(info);

    ActivityWriter::RecordData r;
    r.timestamp = kStartUtc;
    w.addRecord(r);

    ActivityWriter::LapData lap;
    lap.timestamp = kStartUtc + 10;
    lap.timeStart = kStartUtc;
    lap.duration  = 10;
    lap.elapsed   = 10;
    lap.calories  = 401.6f;
    lap.restingCalories = 12.4f;
    w.addLap(lap);

    ActivityWriter::TrackData t;
    t.timestamp = kStartUtc + 10;
    t.timeStart = kStartUtc;
    t.duration  = 10;
    t.elapsed   = 10;
    t.calories  = 401.6f;
    t.metabolicCalories = 12.4f;
    ASSERT_TRUE(w.stop(t));

    FitReader reader(readFit(fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    const auto sessions = reader.withGlobal(kMesgSession);
    const auto laps     = reader.withGlobal(kMesgLap);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalCalories).u(), 402u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldMetabolicCalories).u(), 12u);
    EXPECT_EQ(laps[0]->fields.at(kFieldTotalCalories).u(), 402u);
    EXPECT_EQ(laps[0]->devFields.at(kDevLapRestingCal).u(), 12u);
}

// -- The kilojoules the wearer read off the bike -----------------------------

TEST(SpinActivityWriter, SessionCarriesTheEnteredWorkAndThePowerItImplies)
{
    // The file holds joules, so 430 kJ is 430000 -- the factor of a thousand
    // between what the wearer types and what the field is defined in.
    Ride ride;
    ride.run(2700, {138}, /*external=*/2, /*workKilojoules=*/430);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);

    EXPECT_EQ(sessions[0]->fields.at(kFieldSessionTotalWork).u(), 430u * kJoulesPerKilojoule);
    // 430000 J / 2700 s = 159.26 W.
    EXPECT_EQ(sessions[0]->fields.at(kFieldSessionAvgPower).u(), 159u);
}

TEST(SpinActivityWriter, ARideWithNoWorkFigureOmitsBothFieldsRatherThanWritingZeros)
{
    // `total_work = 0` is a MEASUREMENT saying the wearer produced nothing,
    // which a platform downstream would average into a season.
    Ride ride;
    ride.run(2700, {138}, /*external=*/2);   // nobody was asked
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);

    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionTotalWork), 0u)
        << "an absent work figure was written as a value";
    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionAvgPower), 0u)
        << "an absent power figure was written as a value";

    // ...and the rest of the ride is untouched by the question not being
    // answered: this is the same file the app wrote before the screen existed.
    EXPECT_EQ(sessions[0]->fields.at(kFieldSport).u(),
              static_cast<uint64_t>(fit::Sport::Cycling));
    EXPECT_EQ(sessions[0]->fields.at(kFieldTotalTimer).u(), 2700u * 1000u);
    EXPECT_EQ(reader.withGlobal(kMesgLap).size(), 1u);
    EXPECT_TRUE(reader.crcValid());
}

TEST(SpinActivityWriter, TheWorkFieldsDoNotLandOnTheirNeighbours)
{
    // lap.total_work is 41 and lap.avg_power is 19; on the session those are
    // avg_stroke_count and max_cadence.
    Ride ride;
    ride.run(1800, {140}, /*external=*/2, /*workKilojoules=*/600);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);

    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionAvgStrokeCount), 0u)
        << "the session wrote something into avg_stroke_count (lap.total_work's number)";
    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionMaxCadence), 0u)
        << "the session wrote something into max_cadence (lap.avg_power's number)";
    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionMaxPower), 0u)
        << "the session wrote the average into max_power";

    // One number covers the whole ride and cannot be honestly divided between
    // splits.
    const auto laps = reader.withGlobal(kMesgLap);
    ASSERT_FALSE(laps.empty());
    for (const auto* lap : laps) {
        EXPECT_EQ(lap->fields.count(41), 0u) << "a lap carried total_work";
        EXPECT_EQ(lap->fields.count(19), 0u) << "a lap carried avg_power";
    }
}

TEST(SpinActivityWriter, AveragePowerRoundsRatherThanTruncating)
{
    // 100 kJ over 199 s is 502.51 W. Truncating loses the half watt in the same
    // direction every ride: a bias, and a trend is why this exists.
    Ride ride;
    ride.run(199, {150}, /*external=*/2, /*workKilojoules=*/100);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldSessionAvgPower).u(), 503u);
}

TEST(SpinActivityWriter, WorkAndTimeInZoneCoexistOnTheSameSession)
{
    // The session definition has four shapes; this exercises the one with both
    // optional groups present.
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    constexpr uint8_t kZones = 5;
    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    info.zoneCount = kZones;
    w.start(info);

    ActivityWriter::RecordData r;
    r.timestamp = kStartUtc;
    w.addRecord(r);

    ActivityWriter::TrackData t;
    t.timestamp = kStartUtc + 600;
    t.timeStart = kStartUtc;
    t.duration  = 600;
    t.elapsed   = 600;
    t.workKilojoules = 120;
    for (size_t i = 0; i <= kZones; ++i) {
        t.zoneSeconds[i] = 100;
    }
    ASSERT_TRUE(w.stop(t));

    FitReader reader(readFit(fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.crcValid());
    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);

    EXPECT_EQ(zoneMillis(*sessions[0], kFieldSessionTimeInHrZone).size(),
              static_cast<size_t>(kZones) + 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldSessionTotalWork).u(), 120u * kJoulesPerKilojoule);
    EXPECT_EQ(sessions[0]->fields.at(kFieldSessionAvgPower).u(), 200u);   // 120000/600
    EXPECT_EQ(sessions[0]->fields.count(kFieldSessionAvgTemperature), 0u);
}

TEST(SpinActivityWriter, WritesAJsonSidecarThatAlsoSaysCycling)
{
    // Auxiliary to the .fit and best-effort by contract, but it is what the
    // watch's own activity list reads, so it must not disagree about the sport.
    Ride ride;
    ride.run(90, {125}, /*external=*/2);
    ASSERT_TRUE(ride.stopped);

    const std::string path = findPath(ride.fx.fileSystem, ".json");
    ASSERT_FALSE(path.empty());

    const std::string json = ride.fx.fileSystem.readFile(path);
    EXPECT_NE(json.find("\"activity_type\""), std::string::npos);
    EXPECT_NE(json.find("cycling"), std::string::npos);
    EXPECT_EQ(json.find("workout"), std::string::npos);
    EXPECT_NE(json.find("\"duration\""), std::string::npos);
}

TEST(SpinActivityWriter, DiscardLeavesNoFileBehind)
{
    SDK::TestSupport::KernelFixture fx;
    ActivityWriter w(fx.kernel, "Activity");

    ActivityWriter::AppInfo info;
    info.timestamp = kStartUtc;
    info.devID     = "UNA";
    info.appID     = "spin";
    w.start(info);

    ActivityWriter::RecordData r;
    r.timestamp = kStartUtc;
    w.addRecord(r);

    w.discard();

    EXPECT_TRUE(findPath(fx.fileSystem, ".fit").empty());
    // The recovery marker has to go too, or the next boot finalizes a ride the
    // wearer threw away.
    EXPECT_FALSE(fx.fileSystem.exist("Activity/.recording"));
}

TEST(SpinActivityWriter, ARideWithNoHeartRateIsStillAValidIndoorRide)
{
    // No strap, no wrist reading it trusted: the ride still has to file as an
    // indoor ride rather than fail to write.
    Ride ride;
    ride.run(60, {}, /*none=*/0);
    ASSERT_TRUE(ride.stopped);

    FitReader reader(readFit(ride.fx.fileSystem));
    ASSERT_TRUE(reader.ok());
    EXPECT_TRUE(reader.crcValid());

    const auto sessions = reader.withGlobal(kMesgSession);
    ASSERT_EQ(sessions.size(), 1u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldSport).u(), 2u);
    EXPECT_EQ(sessions[0]->fields.at(kFieldSubSport).u(), 6u);

    // heart_rate is present but set to the uint8 invalid sentinel, which is
    // how FIT says "no value here" -- not 0, which would be a heart rate.
    const auto records = reader.withGlobal(kMesgRecord);
    ASSERT_FALSE(records.empty());
    EXPECT_EQ(records[0]->fields.at(kFieldRecordHr).u(), 0xFFu);
}
