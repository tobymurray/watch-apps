/**
 ******************************************************************************
 * @file    ActivityWriter.cpp
 * @brief   Serializes activity data to a FIT file (native SDK::Fit encoder).
 ******************************************************************************
 */

#include "ActivityWriter.hpp"

#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"

#include <cassert>
#include <cstring>

#define LOG_MODULE_PRX      "ActivityWriter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "SDK/UnaLogger/Logger.h"

namespace fit = SDK::Fit;
using Field = fit::FitWriter::Field;
using DevFieldDef = fit::FitWriter::DevField;

namespace {

// WIRE FORMAT. None of these are in SDK/Fit/FitProfile.hpp, so every number
// came from the FIT profile itself via Tools/fit-profile/lookup.py, checked
// against the Garmin FIT SDK profile (21.214.0Release) and python-fitparse,
// which agree. They cannot be revised without invalidating every file already
// written.
//
// A FIELD NUMBER IS NOT SHARED BETWEEN lap AND session, and every wrong number
// below lands on a real field that looks plausible:
//
//   meant                     right  wrong  what actually lives there
//   session.time_in_hr_zone      65     57  avg_temperature, sint8, degrees C
//   session.total_work           48     41  avg_stroke_count, uint32, strokes/lap
//   session.avg_power            20     19  max_cadence, uint8, rpm
//   lap.avg_power                19     20  max_power -- the average as the maximum
//
// Spin measures no temperature, no strokes and no cadence, so nothing would
// look wrong from here; the damage is entirely in the reader.
// ActivityWriter_test.cpp asserts the session wrote nothing into 57, 41, 19 or
// 21, and that no lap carried a work field.
constexpr uint8_t kLapTimeInHrZoneNum     = 57;
constexpr uint8_t kSessionTimeInHrZoneNum = 65;
constexpr uint8_t kSessionTotalWorkNum    = 48;
constexpr uint8_t kSessionAvgPowerNum     = 20;

/// time_in_hr_zone is stored in milliseconds; the app holds seconds.
constexpr uint32_t kTimeInHrZoneScale = 1000;

/// total_work is defined in JOULES, and the wearer enters kilojoules.
constexpr uint32_t kJoulesPerKilojoule = 1000;

/// A heart rate for the file, rounded rather than truncated and held below the
/// uint8 invalid value of 255.
uint8_t beats(float bpm)
{
    if (bpm <= 0.0f) { return 0; }
    return (bpm >= 254.0f) ? 254u : static_cast<uint8_t>(bpm + 0.5f);
}

/// Work over ACTIVE seconds, not elapsed: a ride paused for ten minutes did no
/// work in them. Rounds rather than truncating, which would lose up to a watt
/// in the same direction every ride -- a bias, not noise.
uint16_t averageWatts(uint32_t joules, std::time_t activeSeconds)
{
    if (joules == 0 || activeSeconds <= 0) {
        return 0;
    }
    const uint64_t seconds = static_cast<uint64_t>(activeSeconds);
    const uint64_t watts   = (static_cast<uint64_t>(joules) + seconds / 2) / seconds;
    constexpr uint64_t kMaxWatts = 65534;   // 65535 is the uint16 invalid value
    return static_cast<uint16_t>(watts > kMaxWatts ? kMaxWatts : watts);
}

}  // namespace

ActivityWriter::ActivityWriter(const SDK::Kernel& kernel, const char* pathToDir)
    : mKernel(kernel), mPath(pathToDir), mMarker(kernel.fs, pathToDir)
{
    assert(pathToDir != nullptr);
}

void ActivityWriter::start(const AppInfo& info)
{
    // A ride with no zones writes no zone array, rather than one full of zeros
    // a reader would take as "nowhere near a zone all ride".
    mZoneBuckets = (info.zoneCount > 0 && info.zoneCount <= kMaxZones)
                       ? static_cast<uint8_t>(info.zoneCount + 1)
                       : 0;

    mLapCounter   = 0;
    mLastFlushUtc = info.timestamp;

    if (!createAndOpenFile(info.timestamp)) {
        return;
    }

    mFit = std::make_unique<fit::FitWriter>(*mFile);
    const bool begun = mFit->begin(/*profileVersion=*/0);
    if (!begun) {
        LOG_ERROR("Failed to write FIT header\n");
    }

    // file_id
    mFit->defineMessage(L_FILE_ID, fit::mesgNum(fit::MesgNum::FileId),
        {fit::field::FileId::Type, fit::field::FileId::Manufacturer,
         fit::field::FileId::Product, fit::field::FileId::SerialNumber,
         fit::field::FileId::TimeCreated});
    mFit->data(L_FILE_ID)
        .u8(static_cast<uint8_t>(fit::File::Activity))
        .u16(static_cast<uint16_t>(fit::Manufacturer::Development))
        .u16(0)
        .u32(0)
        .u32(unixToFitTimestamp(info.timestamp))
        .write();

    // developer_data_id
    mFit->defineMessage(L_DEV_ID, fit::mesgNum(fit::MesgNum::DeveloperDataId),
        {fit::field::DeveloperDataId::ApplicationId,
         fit::field::DeveloperDataId::DeveloperDataIndex});
    {
        uint8_t appId[16] = {};
        std::strncpy(reinterpret_cast<char*>(appId), info.appID.c_str(), sizeof(appId));
        mFit->data(L_DEV_ID).bytes(appId, sizeof(appId)).u8(0).write();
    }

    // Developer fields are self-describing, so they survive any profile.
    writeFieldDescription(DF_BATTERY_LEVEL, "batteryLevel", "%", fit::BaseType::UInt8);
    writeFieldDescription(DF_BATTERY_VOLTAGE, "battVoltage", "mV", fit::BaseType::UInt16);
    writeFieldDescription(DF_HR_SOURCE, "hr_source", nullptr, fit::BaseType::UInt8);
    writeFieldDescription(DF_HR_OPTICAL, "hr_optical", "bpm", fit::BaseType::UInt8);
    writeFieldDescription(DF_HR_EXTERNAL, "hr_external", "bpm", fit::BaseType::UInt8);
    // WIRE FORMAT: there is no lap.resting_calories to promote this to. `lap`
    // has total_calories (11) and total_fat_calories (12) and nothing else in
    // the family; session.metabolic_calories (196) does exist and is native
    // below. Re-check with Tools/fit-profile/lookup.py lap.
    writeFieldDescription(DF_LAP_RESTING_CAL, "resting_calories", "kcal", fit::BaseType::UInt16);

    // event
    mFit->defineMessage(L_EVENT, fit::mesgNum(fit::MesgNum::Event),
        {fit::field::Event::Timestamp, fit::field::Event::EventField,
         fit::field::Event::EventType});

    defineRecordMessages();

    // The zone array is as long as this ride has zones.
    const Field lapZones{kLapTimeInHrZoneNum, fit::BaseType::UInt32, mZoneBuckets};

    if (mZoneBuckets > 0) {
        mFit->defineMessage(L_LAP, fit::mesgNum(fit::MesgNum::Lap),
            {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
             fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
             fit::field::Lap::MessageIndex, fit::field::Lap::AvgHeartRate,
             fit::field::Lap::MaxHeartRate, fit::field::Lap::TotalCalories,
             lapZones},
            {{DF_LAP_RESTING_CAL, 2, 0}});
    } else {
        mFit->defineMessage(L_LAP, fit::mesgNum(fit::MesgNum::Lap),
            {fit::field::Lap::Timestamp, fit::field::Lap::StartTime,
             fit::field::Lap::TotalElapsedTime, fit::field::Lap::TotalTimerTime,
             fit::field::Lap::MessageIndex, fit::field::Lap::AvgHeartRate,
             fit::field::Lap::MaxHeartRate, fit::field::Lap::TotalCalories},
            {{DF_LAP_RESTING_CAL, 2, 0}});
    }
    // L_SESSION is NOT defined here. Whether the session carries a work figure
    // is not known until the ride ends, and an absent field has to be absent
    // from the definition -- see defineSessionMessage().
    mFit->defineMessage(L_ACTIVITY, fit::mesgNum(fit::MesgNum::Activity),
        {fit::field::Activity::Timestamp, fit::field::Activity::TotalTimerTime,
         fit::field::Activity::LocalTimestamp, fit::field::Activity::NumSessions});

    addMessageEvent(info.timestamp, fit::EventType::Start);

    // Header + all definitions are on disk: flush and drop the recovery marker.
    // getPosition() here is a clean record boundary, so a crash after this point
    // is recoverable up to at least the header/definitions. Only write the
    // marker when begin() succeeded AND the initial flush is durable: a failed
    // begin()/flush leaves a near-empty/broken or non-durable .fit that the
    // recovery marker must not point next boot's recover() at.
    if (begun && mFile->flush()) {
        mMarker.write(mFile->getPath(), static_cast<uint32_t>(mFile->getPosition()));
    }
}

void ActivityWriter::defineRecordMessages()
{
    const DevFieldDef hr3[] = {
        {DF_HR_SOURCE, 1, 0}, {DF_HR_OPTICAL, 1, 0}, {DF_HR_EXTERNAL, 1, 0},
    };
    const DevFieldDef batt5[] = {
        {DF_BATTERY_LEVEL, 1, 0}, {DF_BATTERY_VOLTAGE, 2, 0},
        {DF_HR_SOURCE, 1, 0}, {DF_HR_OPTICAL, 1, 0}, {DF_HR_EXTERNAL, 1, 0},
    };

    // Plain record (HR only) + 3 HR developer fields.
    mFit->defineMessage(L_RECORD, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
        {hr3[0], hr3[1], hr3[2]});

    // + battery (5 developer fields).
    mFit->defineMessage(L_RECORD_B, fit::mesgNum(fit::MesgNum::Record),
        {fit::field::Record::Timestamp, fit::field::Record::HeartRate},
        {batt5[0], batt5[1], batt5[2], batt5[3], batt5[4]});
}

void ActivityWriter::defineSessionMessage(bool withWork)
{
    const Field zones{kSessionTimeInHrZoneNum, fit::BaseType::UInt32, mZoneBuckets};
    const Field work{kSessionTotalWorkNum, fit::BaseType::UInt32};
    const Field power{kSessionAvgPowerNum, fit::BaseType::UInt16};

    // Four spellings, because two fields are optional and defineMessage() takes
    // an initializer_list, which cannot be built at runtime or spliced. The
    // preprocessor can splice tokens, so the twelve mandatory fields are named
    // once rather than copied out four times.
#define SPIN_SESSION_FIELDS                                                        \
    fit::field::Session::Timestamp, fit::field::Session::StartTime,                \
    fit::field::Session::TotalElapsedTime, fit::field::Session::TotalTimerTime,    \
    fit::field::Session::MessageIndex, fit::field::Session::NumLaps,               \
    fit::field::Session::Sport, fit::field::Session::SubSport,                     \
    fit::field::Session::AvgHeartRate, fit::field::Session::MaxHeartRate,          \
    fit::field::Session::TotalCalories, fit::field::Session::MetabolicCalories

    const uint16_t mesg = fit::mesgNum(fit::MesgNum::Session);
    if (mZoneBuckets > 0 && withWork) {
        mFit->defineMessage(L_SESSION, mesg, {SPIN_SESSION_FIELDS, zones, work, power});
    } else if (mZoneBuckets > 0) {
        mFit->defineMessage(L_SESSION, mesg, {SPIN_SESSION_FIELDS, zones});
    } else if (withWork) {
        mFit->defineMessage(L_SESSION, mesg, {SPIN_SESSION_FIELDS, work, power});
    } else {
        mFit->defineMessage(L_SESSION, mesg, {SPIN_SESSION_FIELDS});
    }

#undef SPIN_SESSION_FIELDS
}

void ActivityWriter::writeFieldDescription(uint8_t devFieldNum, const char* name,
                                           const char* units, fit::BaseType baseType)
{
    const uint8_t nameLen  = name ? static_cast<uint8_t>(std::strlen(name) + 1) : 1;
    const uint8_t unitsLen = units ? static_cast<uint8_t>(std::strlen(units) + 1) : 1;

    // Redefined per call, to size the name/units strings exactly.
    mFit->defineMessage(L_FIELD_DESC, fit::mesgNum(fit::MesgNum::FieldDescription),
        {fit::field::FieldDescription::DeveloperDataIndex,
         fit::field::FieldDescription::FieldDefinitionNumber,
         fit::field::FieldDescription::FitBaseTypeId,
         {fit::field::FieldDescription::kFieldNameNum, fit::BaseType::String, nameLen},
         {fit::field::FieldDescription::kUnitsNum, fit::BaseType::String, unitsLen}});
    mFit->data(L_FIELD_DESC)
        .u8(0)
        .u8(devFieldNum)
        .u8(fit::baseTypeId(baseType))
        .str(name ? name : "", nameLen)
        .str(units ? units : "", unitsLen)
        .write();
}

void ActivityWriter::pause(std::time_t timestamp)
{
    if (mFit) {
        addMessageEvent(timestamp, fit::EventType::Stop);
    }
}

void ActivityWriter::resume(std::time_t timestamp)
{
    if (mFit) {
        addMessageEvent(timestamp, fit::EventType::Start);
    }
}

void ActivityWriter::addRecord(const RecordData& record)
{
    if (!mFit) {
        return;
    }

    const bool batt  = record.has(RecordData::Field::BATTERY);
    const uint8_t local = batt ? L_RECORD_B : L_RECORD;

    fit::FitWriter::Data d = mFit->data(local);

    d.u32(unixToFitTimestamp(record.timestamp));
    d.u8(record.has(RecordData::Field::HEART_RATE)
             ? static_cast<uint8_t>(record.heartRate)
             : static_cast<uint8_t>(fit::baseTypeInvalid(fit::BaseType::UInt8)));

    // Developer fields, in definition order.
    if (batt) {
        d.u8(record.batteryLevel).u16(record.batteryVoltage);
    }
    d.u8(record.hrSource).u8(record.hrOpticalBpm).u8(record.hrExternalBpm);

    d.write();

    if (record.timestamp - mLastFlushUtc >= skFlushIntervalSec) {
        // Advance the marker only when the flush landed, so recover() is never
        // pointed past non-durable data.
        if (mFile->flush()) {
            mMarker.update(static_cast<uint32_t>(mFile->getPosition()));
            mLastFlushUtc = record.timestamp;
        }
    }
}

void ActivityWriter::addLap(const LapData& lap)
{
    if (!mFit) {
        return;
    }

    fit::FitWriter::Data d = mFit->data(L_LAP);
    d.u32(unixToFitTimestamp(lap.timestamp))
     .u32(unixToFitTimestamp(lap.timeStart))
     .u32(static_cast<uint32_t>(lap.elapsed * 1000))
     .u32(static_cast<uint32_t>(lap.duration * 1000))
     .u16(0)  // message_index
     .u8(beats(lap.hrAvg))
     .u8(beats(lap.hrMax))
     .u16(static_cast<uint16_t>(lap.calories + 0.5f));
    // Still a native field: time_in_hr_zone is the last one in the definition.
    writeZoneSeconds(d, lap.zoneSeconds);
    // Developer fields follow every native one, in definition order.
    d.u16(static_cast<uint16_t>(lap.restingCalories + 0.5f));
    d.write();

    mLapCounter++;

    if (mFile->flush()) {
        mMarker.update(static_cast<uint32_t>(mFile->getPosition()));
        mLastFlushUtc = lap.timestamp;
    }
}

void ActivityWriter::writeZoneSeconds(fit::FitWriter::Data& d,
                                      const std::time_t (&zones)[kZoneBuckets]) const
{
    // Only as many buckets as this ride declared: definition and data must
    // agree on the length or the message is malformed.
    for (size_t i = 0; i < mZoneBuckets; ++i) {
        d.u32(static_cast<uint32_t>(zones[i]) * kTimeInHrZoneScale);
    }
}

bool ActivityWriter::stop(const TrackData& track)
{
    if (!mFit) {
        return false;
    }

    bool ok = mFit->ok();

    // Absent, not zero: `total_work = 0` is a measurement claiming the wearer
    // produced nothing, which a platform downstream would average into a season.
    // Out of the DEFINITION rather than written as an invalid sentinel, because
    // a sentinel is absent only to a decoder that honours it.
    const uint32_t workJoules =
        static_cast<uint32_t>(track.workKilojoules) * kJoulesPerKilojoule;
    const uint16_t watts = averageWatts(workJoules, track.duration);
    // Both or neither: a total_work with no avg_power leaves a reader dividing
    // by a duration it may define differently.
    const bool withWork = track.workKilojoules > 0 && watts > 0;

    defineSessionMessage(withWork);

    fit::FitWriter::Data session = mFit->data(L_SESSION);
    session
        .u32(unixToFitTimestamp(track.timestamp))
        .u32(unixToFitTimestamp(track.timeStart))
        .u32(static_cast<uint32_t>(track.elapsed * 1000))
        .u32(static_cast<uint32_t>(track.duration * 1000))
        .u16(0)  // message_index
        .u16(mLapCounter)
        // Two fields, not one: sport=cycling is what a consumer aggregates
        // under, sub_sport=indoor_cycling is what stops it asking where the
        // ride went.
        .u8(static_cast<uint8_t>(fit::Sport::Cycling))
        .u8(static_cast<uint8_t>(fit::SubSport::IndoorCycling))
        .u8(beats(track.hrAvg))
        .u8(beats(track.hrMax))
        .u16(static_cast<uint16_t>(track.calories + 0.5f))
        .u16(static_cast<uint16_t>(track.metabolicCalories + 0.5f));
    writeZoneSeconds(session, track.zoneSeconds);
    // In definition order, so these come after the zone array.
    if (withWork) {
        session.u32(workJoules).u16(watts);
    }
    ok = session.write() && ok;

    ok = mFit->data(L_ACTIVITY)
        .u32(unixToFitTimestamp(track.timestamp))
        .u32(static_cast<uint32_t>(track.duration * 1000))
        .u32(unixToFitTimestamp(epochToLocal(track.timestamp)))
        .u16(1)
        .write() && ok;

    const bool finishOk = mFit->finish();
    if (!finishOk) {
        LOG_ERROR("Failed to finalize FIT file\n");
    }
    ok = finishOk && mFit->ok() && ok;
    mFit.reset();

    if (mFile) {
        ok = mFile->flush() && ok;
        ok = mFile->close() && ok;
    } else {
        ok = false;
    }

    // Durably on disk, so the next boot must not treat this as interrupted.
    if (ok) {
        mMarker.remove();
    }

    // Only when the FIT is durable: with ok == false the marker is kept for
    // next-boot recovery, and a sidecar would misrepresent the activity. The
    // summary is best-effort, so its failure never flips the return value.
    if (ok && !saveSummary(track)) {
        LOG_ERROR("Activity summary (.json) save failed; FIT is durable and registered\n");
    }
    return ok;
}

void ActivityWriter::discard()
{
    mFit.reset();
    mMarker.remove();
    if (!mFile) {
        return;
    }
    if (mFile->isOpen()) {
        mFile->close();
    }
    mFile->remove();
    mFile.reset();
}

void ActivityWriter::addMessageEvent(std::time_t t, fit::EventType type)
{
    mFit->data(L_EVENT)
        .u32(unixToFitTimestamp(t))
        .u8(static_cast<uint8_t>(fit::Event::Timer))
        .u8(static_cast<uint8_t>(type))
        .write();
}

bool ActivityWriter::recoverInterrupted()
{
    // The kernel's activity registry tracks .fit files only, so a recovered
    // activity needs no sibling .json.
    const auto result = mMarker.recover();
    if (result.recovered) {
        LOG_INFO("Recovery: finalized interrupted activity [%s]\n", result.path.c_str());
    }
    return result.recovered;
}

bool ActivityWriter::createAndOpenFile(std::time_t utc)
{
    char buff[256]{};
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif

    int len = snprintf(buff, sizeof(buff), "%s/%04u%02u/", mPath,
                       localTime.tm_year + 1900, localTime.tm_mon + 1);
    if (len <= 0 || !mKernel.fs.mkdir(buff)) {
        LOG_ERROR("Failed to create dir [%s]\n", buff);
        return false;
    }

    snprintf(&buff[len], sizeof(buff) - len, "activity_%04u%02u%02uT%02u%02u%02u.fit",
        localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
        localTime.tm_hour, localTime.tm_min, localTime.tm_sec);

    mFile = mKernel.fs.file(buff);
    if (!mFile || !mFile->open(true, true)) {
        LOG_ERROR("Failed to create file [%s]\n", buff);
        mFile.reset();
        return false;
    }

    return true;
}

bool ActivityWriter::saveSummary(const TrackData& track)
{
    if (!mFile) {
        return false;
    }
    char buff[256]{};
    size_t nameLen = strlen(mFile->getPath());
    snprintf(buff, sizeof(buff), "%.*s%s", static_cast<int>(nameLen - 3), mFile->getPath(), "json");

    mFile->setPath(buff);

    if (!mFile->open(true, true)) {
        LOG_ERROR("Failed to open activity summary [%s]\n", buff);
        mFile.reset();
        return false;
    }

    SDK::JsonStreamWriter writer(mFile.get());
    writer.startMap();
    writer.add("time_start", static_cast<uint32_t>(track.timeStart));
    writer.add("duration", static_cast<uint32_t>(track.duration));
    writer.add("hr_avg", track.hrAvg);
    writer.add("activity_type", "cycling");
    writer.endMap();

    const bool ok = mFile->flush();
    return mFile->close() && ok;
}

std::time_t ActivityWriter::tm2epoch(const struct tm* tm)
{
    int y = tm->tm_year + 1900;
    int m = tm->tm_mon + 1;
    int d = tm->tm_mday;
    if (m <= 2) { y -= 1; m += 12; }
    int64_t  era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = (uint32_t)(y - era * 400);
    uint32_t doy = (153 * (m - 3) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    int64_t secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
    return (std::time_t)secs;
}

std::time_t ActivityWriter::epochToLocal(std::time_t utc)
{
    std::tm localTime{};
#if WIN32
    localtime_s(&localTime, &utc);
#else
    localtime_r(&utc, &localTime);
#endif
    return tm2epoch(&localTime);
}

uint32_t ActivityWriter::unixToFitTimestamp(std::time_t unixTimestamp)
{
    const std::time_t FIT_EPOCH_OFFSET = 631065600;
    return static_cast<uint32_t>(unixTimestamp - FIT_EPOCH_OFFSET);
}
