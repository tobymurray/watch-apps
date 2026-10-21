#include "Service.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "CardStore.hpp"
#include "LastNight.hpp"
#include "LocalTime.hpp"
#include "Retention.hpp"
#include "SleepPeriods.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/MessageTypes.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp"

#define LOG_MODULE_PRX   "SlumberSvc"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

// SleepLab's request rates. Measured: the firmware delivers 50 Hz in batches
// about every 190 ms whatever this asks for, on every SleepLab night and on
// Slumber's first day; `acc_samples` other than ~1500 per row would refute it.
constexpr float    kAccelPeriodMs   = 40.0f;
constexpr uint32_t kAccelLatencyMs  = 5000;
constexpr float    kEventPeriodMs   = 1000.0f;
constexpr float    kHrPeriodMs      = 1000.0f;
constexpr float    kSlowPeriodMs    = 30000.0f;

/// A wall clock that moved this much more or less than uptime between two rows
/// was set, synced or changed zone.
constexpr int64_t kClockJumpS = 60;

constexpr char kMarksPath[]   = "Marks.csv";
constexpr char kMarksHeader[] =
    "# Slumber marks: what the wearer said they were doing from this moment.\n"
    "utc,utc_off_min,uptime_ms,local,code,label\n";

/// UTC seconds, or 0 when the watch has no time yet.
uint32_t nowUtc()
{
    const std::time_t now = std::time(nullptr);
    return now > 0 ? static_cast<uint32_t>(now) : 0u;
}

/// Local minus UTC in minutes, or -1 with no clock.
int32_t utcOffsetMin(int64_t utc, std::tm &local)
{
    if (utc <= 0) {
        return -1;
    }
    const std::time_t t = static_cast<std::time_t>(utc);
    if (localtime_r(&t, &local) == nullptr) {
        return -1;
    }
    return Slumber::LocalTime::offsetMinutes(
        utc, local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
        static_cast<unsigned>(local.tm_mday), static_cast<unsigned>(local.tm_hour),
        static_cast<unsigned>(local.tm_min), static_cast<unsigned>(local.tm_sec));
}

uint16_t bump(uint16_t v)
{
    return v < 0xFFFFu ? static_cast<uint16_t>(v + 1) : v;
}

SleepPeriods::Epoch toEpoch(const Slumber::EpochRow &r)
{
    return Slumber::DayRow::toEpoch(r.utc, r.count, r.steps, r.hrN, r.hrTrustX10, r.hrMeanX10);
}

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mLog(kernel)
    , mDays(kernel.fs)
    , mHistory(kernel.fs)
    , mNights(kernel.fs)
    , mAccel(SDK::Sensor::Type::ACCELEROMETER, kAccelPeriodMs, kAccelLatencyMs)
    , mHr(SDK::Sensor::Type::HEART_RATE, kHrPeriodMs)
    , mSteps(SDK::Sensor::Type::STEP_COUNTER, kEventPeriodMs)
    , mMotion(SDK::Sensor::Type::MOTION_DETECT, kEventPeriodMs)
    , mTemp(SDK::Sensor::Type::AMBIENT_TEMPERATURE, kSlowPeriodMs)
    , mBattLevel(SDK::Sensor::Type::BATTERY_LEVEL, kSlowPeriodMs)
    , mBattMetrics(SDK::Sensor::Type::BATTERY_METRICS, kSlowPeriodMs)
    , mBattCharge(SDK::Sensor::Type::BATTERY_CHARGING, kEventPeriodMs)
{
}

Service::~Service()
{
    disconnectSensors();
}

void Service::connectSensors()
{
    mAccel.connect();
    mHr.connect();
    mSteps.connect();
    mMotion.connect();
    mTemp.connect();
    mBattLevel.connect();
    mBattMetrics.connect();
    mBattCharge.connect();
}

void Service::disconnectSensors()
{
    mAccel.disconnect();
    mHr.disconnect();
    mSteps.disconnect();
    mMotion.disconnect();
    mTemp.disconnect();
    mBattLevel.disconnect();
    mBattMetrics.disconnect();
    mBattCharge.disconnect();
}

void Service::logSensors()
{
    struct Entry {
        char                     letter;
        SDK::Sensor::Connection &conn;
    };
    Entry entries[] = {
        {'A', mAccel},     {'H', mHr},         {'S', mSteps},       {'M', mMotion},
        {'T', mTemp},      {'L', mBattLevel},  {'E', mBattMetrics}, {'C', mBattCharge},
    };
    char block[sizeof(entries) / sizeof(entries[0]) + 1] = {};
    size_t at = 0;
    for (Entry &e : entries) {
        block[at++] = e.conn.isValid() ? e.letter : static_cast<char>(e.letter - 'A' + 'a');
    }
    mLog.line("sensors", "%s (AHSMTLEC, upper=resolved)", block);
}

void Service::onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch)
{
    const uint16_t n = batch.size();

    if (mAccel.matchesDriver(handle)) {
        mAcc.accBatches = bump(mAcc.accBatches);
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::Accelerometer p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            float x = 0.0f, y = 0.0f, z = 0.0f;
            p.getXYZ(x, y, z);
            const uint32_t ts = p.getTimestamp();
            if (mAccLastTsValid) {
                const uint32_t gap = ts - mAccLastTs;
                if (gap < 0x7FFFFFFFu && static_cast<int32_t>(gap) > mAcc.accMaxGapMs) {
                    mAcc.accMaxGapMs = static_cast<int32_t>(gap);
                }
            }
            mAccLastTs      = ts;
            mAccLastTsValid = true;
            mCount.add(ts, x, y, z);
            mTilt.add(ts, x, y, z);
        }
        return;
    }

    if (mHr.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::HeartRate p(batch[i]);
            if (!p.isDataValid() || p.getBpm() <= 0.0f) {
                continue;
            }
            const uint16_t x10 = static_cast<uint16_t>(p.getBpm() * 10.0f);
            if (mAcc.hrN == 0 || x10 < mAcc.hrMinX10) {
                mAcc.hrMinX10 = x10;
            }
            if (x10 > mAcc.hrMaxX10) {
                mAcc.hrMaxX10 = x10;
            }
            mAcc.hrSumX10 += x10;
            mAcc.hrN = bump(mAcc.hrN);
            const float trust = p.getTrustLevel();
            if (trust >= 0.0f) {
                mAcc.trustSum += trust;
                mAcc.trustN = bump(mAcc.trustN);
            }
        }
        return;
    }

    if (mSteps.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::StepCounter p(batch[i]);
            if (p.isDataValid()) {
                mStepTotal = static_cast<int64_t>(p.getStepCount());
            }
        }
        return;
    }

    if (mMotion.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::MotionDetect p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            using M = SDK::SensorDataParser::MotionDetect::Motion;
            if (p.getID() == M::MOTION) {
                mAcc.motion = bump(mAcc.motion);
            }
            if (p.getID() == M::SIG_MOTION) {
                mAcc.sigMotion = bump(mAcc.sigMotion);
            }
        }
        return;
    }

    if (mTemp.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::Temperature p(batch[i]);
            if (p.isDataValid()) {
                mAcc.tempSum += p.getTemperature();
                mAcc.tempN = bump(mAcc.tempN);
            }
        }
        return;
    }

    if (mBattLevel.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryLevel p(batch[i]);
            if (p.isDataValid()) {
                mBattPctX10 = static_cast<int32_t>(p.getCharge() * 10.0f);
            }
        }
        return;
    }

    if (mBattMetrics.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryMetrics p(batch[i]);
            if (p.isDataValid()) {
                mBattMv       = static_cast<int32_t>(p.getVoltage() * 1000.0f);
                mBattMaX10    = static_cast<int32_t>(p.getCurrent() * 10.0f);
                mBattAvgMaX10 = static_cast<int32_t>(p.getAverageCurrent() * 10.0f);
                mBattMah      = static_cast<int32_t>(p.getCapacity());
            }
        }
        return;
    }

    if (mBattCharge.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryCharging p(batch[i]);
            if (p.isDataValid()) {
                mCharging = (p.isCharging() || p.isUsbConnected()) ? 1 : 0;
            }
        }
        return;
    }
}

void Service::closeEpoch(uint32_t now, uint32_t lateMs, uint32_t slotsLost)
{
    Slumber::EpochRow r;
    r.uptimeMs  = now;
    r.spanMs    = now - mEpochOpenedAt;
    r.lateMs    = lateMs;
    r.slotsLost = slotsLost;

    const uint32_t utc = nowUtc();
    std::tm        local{};
    if (utc > 0) {
        r.utc       = utc;
        r.utcOffMin = utcOffsetMin(utc, local);
    }

    const Slumber::ActivityCount::Out c = mCount.close();
    r.accSamples   = c.samples;
    r.accBatches   = mAcc.accBatches;
    r.accMaxGapMs  = mAcc.accMaxGapMs;
    r.count        = c.count;
    r.peak         = c.peak;
    for (int a = 0; a < 3; ++a) {
        r.countAxis[a] = c.axis[a];
    }

    const Slumber::Tilt::Out t = mTilt.close();
    if (t.any) {
        for (int a = 0; a < 3; ++a) {
            r.gravX1000[a] = t.meanX1000[a];
        }
    }
    r.turnSumDegX10 = t.turnSumDegX10;
    r.turnMaxDegX10 = t.turnMaxDegX10;
    r.tiltBlocks    = t.blocks;

    // STEP_COUNTER counts from boot, so a fall means a reboot: unknown, not negative.
    if (mStepTotal >= 0) {
        if (mStepAtEpoch >= 0 && mStepTotal >= mStepAtEpoch) {
            r.steps = static_cast<int32_t>(mStepTotal - mStepAtEpoch);
        }
        mStepAtEpoch = mStepTotal;
    }
    r.motion    = mAcc.motion;
    r.sigMotion = mAcc.sigMotion;

    if (mAcc.hrN > 0) {
        r.hrMeanX10 = static_cast<int32_t>(mAcc.hrSumX10 / mAcc.hrN);
        r.hrMinX10  = mAcc.hrMinX10;
        r.hrMaxX10  = mAcc.hrMaxX10;
    }
    r.hrN = mHr.isConnected() ? static_cast<int32_t>(mAcc.hrN) : -1;
    if (mAcc.trustN > 0) {
        r.hrTrustX10 = static_cast<int32_t>(mAcc.trustSum / mAcc.trustN * 10.0f);
    }

    if (mAcc.tempN > 0) {
        r.tempX10 = static_cast<int32_t>(mAcc.tempSum / mAcc.tempN * 10.0f);
    }
    r.tempN = mAcc.tempN;

    r.battPctX10   = mBattPctX10;
    r.battMv       = mBattMv;
    r.battMaX10    = mBattMaX10;
    r.battAvgMaX10 = mBattAvgMaX10;
    r.battMah      = mBattMah;
    r.charging     = mCharging;

    r.wakes      = mAcc.wakes;
    r.msgs       = mAcc.msgs;
    r.mark       = mAcc.mark;

    if (mLastRowUtc > 0 && r.utc > 0) {
        const int64_t wall = r.utc - mLastRowUtc;
        const int64_t up   = static_cast<int64_t>(now - mLastRowUptime) / 1000;
        if (wall - up > kClockJumpS || up - wall > kClockJumpS) {
            mLog.line("clock", "wall_s=%ld up_s=%ld utc=%ld", static_cast<long>(wall),
                      static_cast<long>(up), static_cast<long>(r.utc));
        }
    }
    mLastRowUtc    = r.utc;
    mLastRowUptime = now;

    char stem[9] = "nodate";
    if (r.utc > 0 && r.utcOffMin != -1) {
        Slumber::LocalTime::dayStem(r.utc, r.utcOffMin, stem);
    }
    r.writeFails = mWriteFails;
    if (!mDays.add(r, stem)) {
        ++mWriteFails;
        if (mWriteFails == 1 || mWriteFails % 100 == 0) {
            mLog.line("write-fail", "n=%lu stem=%s", static_cast<unsigned long>(mWriteFails), stem);
        }
    }
    if (r.utc > 0 && r.utcOffMin != -1) {
        restoreHistory(static_cast<uint32_t>(r.utc));
        if (!mHistory.add(toEpoch(r), stem)) {
            ++mWriteFails;
        }
    }
    if (mDays.rowsInFile() == 1) {
        mLog.line("day", "stem=%s off_min=%ld", stem, static_cast<long>(r.utcOffMin));
        if (r.utc > 0 && r.utcOffMin != -1) {
            const uint32_t from    = mKernel.sys.getTimeMs();
            const unsigned removed = Slumber::Retention::prune(mKernel.fs, r.utc, r.utcOffMin);
            mLog.line("prune", "files=%u ms=%lu", removed,
                      static_cast<unsigned long>(mKernel.sys.getTimeMs() - from));
        }
    }
    if (slotsLost > 0) {
        mLog.line("late", "late_ms=%lu lost=%lu", static_cast<unsigned long>(lateMs),
                  static_cast<unsigned long>(slotsLost));
    }

    mLastAccOk = r.accSamples > 0;
    mLastHrOk  = r.hrN > 0;
    mAcc       = Accum{};
    ++mEpochs;

    if (mEpochs % kCardEveryEpochs == 0) {
        publishCard();
    }
    publishStatus();
}

void Service::onMark(const CustomMessage::Mark &mark)
{
    char label[CustomMessage::kMarkLabelCap + 1] = {};
    std::memcpy(label, mark.label, CustomMessage::kMarkLabelCap);
    for (char &ch : label) {
        if (ch == ',' || ch == '\n' || ch == '\r') {
            ch = ' ';
        }
    }

    const uint32_t utc = nowUtc();
    std::tm        local{};
    const int32_t  off = utcOffsetMin(utc, local);
    char when[40] = "-";
    if (off != -1) {
        std::snprintf(when, sizeof(when), "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900,
                      local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    }

    char line[96];
    const int n = std::snprintf(line, sizeof(line), "%lu,%ld,%lu,%s,%u,%s\n",
                                static_cast<unsigned long>(utc), static_cast<long>(off),
                                static_cast<unsigned long>(mKernel.sys.getTimeMs()), when,
                                static_cast<unsigned>(mark.code), label);
    const bool ok = n > 0 && static_cast<size_t>(n) < sizeof(line) &&
                    Slumber::appendFile(mKernel.fs, kMarksPath, kMarksHeader, line,
                                        static_cast<size_t>(n));
    if (!ok) {
        ++mWriteFails;
    }
    mLog.line("mark", "code=%u ok=%d %s", static_cast<unsigned>(mark.code), ok ? 1 : 0, label);

    mLastMark   = mark.code;
    mLastMarkMs = mKernel.sys.getTimeMs();
    mAcc.mark   = mark.code;
    publishStatus();
}

void Service::publishCard()
{
    SlumberCard::Card card;
    card.seq             = ++mCardSeq;
    card.writtenUtc      = nowUtc();
    card.heartbeatUtc    = card.writtenUtc;
    card.serviceStartUtc = mStartUtc;
    card.launchNo        = mLaunchNo;
    fillNight(card, card.writtenUtc);
    publishNow(card.writtenUtc);

    mCardOk = SlumberCard::publish(mKernel.fs, card);
    if (!mCardOk) {
        mLog.line("card-fail", "seq=%lu", static_cast<unsigned long>(card.seq));
    }
}

void Service::publishNow(uint32_t utc)
{
    std::tm       local{};
    const int32_t off = utcOffsetMin(utc, local);
    if (utc == 0 || off == -1) {
        return;
    }
    Slumber::SleepNow::State s;
    s.writtenUtc = utc;
    s.heartbeatS = SlumberCard::kHeartbeatS;
    s.offsetMin  = off;
    if (mHistory.size() > 0) {
        s.asleepSinceUtc =
            LastNight::asleepSince(mPeriods, mPeriodN, mHistory.data()[mHistory.size() - 1].utc);
    }
    s.lastNight   = mLastNight;
    s.usual       = mNights.recent().usualBefore(UINT32_MAX);
    s.usualBefore = mNights.recent().usualBefore(mLastNight != nullptr ? mLastNight->startUtc : UINT32_MAX);
    s.tonightDay  = Schedule::tonight(utc, off);
    s.tonight     = mNights.schedule().expect(s.tonightDay);
    s.week        = mNights.schedule().week();
    if (mLastNight != nullptr && mLastNight->worn == SleepPeriods::Worn::Worn) {
        s.lastHeart  = NightSummary::heart(mHistory.data(), mHistory.size(), *mLastNight);
        s.usualHeart = mNights.schedule().heartBefore(Rhythm::noonDay(mLastNight->startUtc, off));
    }
    if (!Slumber::SleepNow::publish(mKernel.fs, s)) {
        ++mWriteFails;
        mLog.line("write-fail", "n=%lu path=%s", static_cast<unsigned long>(mWriteFails),
                  Slumber::SleepNow::kPath);
    }
}

void Service::restoreHistory(uint32_t utc)
{
    std::tm       local{};
    const int32_t off = utcOffsetMin(utc, local);
    if (mHistoryRestored || off == -1) {
        return;
    }
    const uint32_t from    = mKernel.sys.getTimeMs();
    const unsigned rebuilt = mHistory.restore(utc, off);
    mHistoryRestored       = true;
    mLog.line("history", "epochs=%lu rebuilt_days=%u ms=%lu",
              static_cast<unsigned long>(mHistory.size()), rebuilt,
              static_cast<unsigned long>(mKernel.sys.getTimeMs() - from));
}

void Service::logNights(size_t n, int32_t offsetMin)
{
    for (size_t i = 0; i < n; ++i) {
        const SleepPeriods::Period &p = mPeriods[i];
        if (!mNights.due(p)) {
            continue;
        }
        const NightSummary::Summary s = NightSummary::summarise(mHistory.data(), mHistory.size(), p);
        if (!mNights.add(p, s, offsetMin, mHistory.data(), mHistory.size())) {
            ++mWriteFails;
            mLog.line("write-fail", "n=%lu path=%s", static_cast<unsigned long>(mWriteFails),
                      Slumber::NightLog::kPath);
        }
    }
}

void Service::fillNight(SlumberCard::Card &card, uint32_t utc)
{
    card.state = SlumberCard::State::Recording;
    mPeriodN   = 0;
    mLastNight = nullptr;
    std::tm       local{};
    const int32_t off = utcOffsetMin(utc, local);
    if (off == -1 || mHistory.size() == 0) {
        return;
    }
    const size_t n = SleepPeriods::find(mHistory.data(), mHistory.size(), mPeriods,
                                        sizeof(mPeriods) / sizeof(mPeriods[0]), utc);
    mPeriodN = n;
    logNights(n, off);
    const LastNight::Choice c = LastNight::choose(mPeriods, n, utc);
    if (c.kind == LastNight::Kind::None) {
        return;
    }
    mLastNight = c.period;
    const SleepPeriods::Period &p = *c.period;
    using SlumberCard::State;
    card.state         = c.kind == LastNight::Kind::NotWorn ? State::NotWorn
                         : p.settled                        ? State::Report
                                                            : State::Provisional;
    card.restMin       = p.restMin;
    card.spanMin       = static_cast<uint16_t>((p.endUtc - p.startUtc) / 60u);
    int32_t sleptOff   = off;
    mNights.loggedOffset(p.startUtc, sleptOff);
    card.onsetMinOfDay = Slumber::LocalTime::minuteOfDay(p.startUtc, sleptOff);
    card.wakeMinOfDay  = Slumber::LocalTime::minuteOfDay(p.endUtc, sleptOff);
    card.validUntilUtc = p.endUtc + LastNight::kRecentS;

    if (p.startUtc != mReportedStartUtc || card.state != mReportedState) {
        mLog.line("night", "state=%u start=%lu end=%lu rest_min=%u worn_pct=%u hr_pct=%u",
                  static_cast<unsigned>(card.state), static_cast<unsigned long>(p.startUtc),
                  static_cast<unsigned long>(p.endUtc), static_cast<unsigned>(p.restMin),
                  static_cast<unsigned>(p.wornPct), static_cast<unsigned>(p.hrCoverPct));
        mReportedStartUtc = p.startUtc;
        mReportedState    = card.state;
    }
}

uint32_t Service::upMinutes() const
{
    return (mKernel.sys.getTimeMs() - mStartMs) / 60000u;
}

void Service::publishStatus()
{
    if (!mGuiAttached) {
        return;
    }
    if (auto st = SDK::make_msg<CustomMessage::Status>(mKernel)) {
        st->serviceUpMin = upMinutes();
        st->rowsToday    = mDays.rowsInFile();
        st->writeFails   = mWriteFails;
        st->lastMark     = mLastMark;
        st->markAgoMin   = mLastMark == CustomMessage::kNoMark
                               ? CustomMessage::kNoMarkAge
                               : (mKernel.sys.getTimeMs() - mLastMarkMs) / 60000u;
        st->accOk = mLastAccOk ? 1 : 0;
        st->hrOk  = mLastHrOk ? 1 : 0;
        st.send();
    }
}

void Service::run()
{
    mStartMs  = mKernel.sys.getTimeMs();
    mStartUtc = nowUtc();
    mLaunchNo = mLog.bumpLaunchCount();
    std::tm local{};
    mLog.line("start", "launch=%lu utc=%lu off_min=%ld", static_cast<unsigned long>(mLaunchNo),
              static_cast<unsigned long>(mStartUtc),
              static_cast<long>(utcOffsetMin(mStartUtc, local)));

    // Carries the sequence on from the last process, so the glance never sees
    // it go backwards across a restart.
    SlumberCard::Card previous;
    const SlumberCard::Read prior = SlumberCard::load(mKernel.fs, previous);
    if (prior == SlumberCard::Read::Ok || prior == SlumberCard::Read::FromBackup) {
        mCardSeq = previous.seq;
    }
    if (mStartUtc > 0) {
        restoreHistory(mStartUtc);
    }
    publishCard();

    connectSensors();
    logSensors();

    mEpochOpenedAt = mStartMs;
    mNextEpochAt   = mStartMs + kEpochMs;

    while (true) {
        const uint32_t now     = mKernel.sys.getTimeMs();
        int32_t        toEpoch = static_cast<int32_t>(mNextEpochAt - now);
        if (toEpoch <= 0) {
            const uint32_t lateMs = static_cast<uint32_t>(-toEpoch);
            // Advance by whole slots so one late wake does not shift every row
            // after it, and count the slots a long stall stepped over.
            uint32_t slots = 0;
            do {
                mNextEpochAt += kEpochMs;
                ++slots;
            } while (static_cast<int32_t>(mNextEpochAt - now) <= 0);
            closeEpoch(now, lateMs, slots - 1);
            mEpochOpenedAt = now;
            toEpoch        = static_cast<int32_t>(mNextEpochAt - now);
        }

        mAcc.wakes = bump(mAcc.wakes);
        SDK::MessageBase *msg = nullptr;
        if (!mKernel.comm.getMessage(msg, static_cast<uint32_t>(toEpoch))) {
            continue;
        }
        mAcc.msgs = bump(mAcc.msgs);

        switch (msg->getType()) {
            case SDK::MessageType::COMMAND_APP_STOP: {
                const bool historyFlushed = mHistory.flush();
                const bool flushed        = mDays.flush() && historyFlushed;
                mLog.line("stop", "up_min=%lu rows=%lu fails=%lu flushed=%d",
                          static_cast<unsigned long>(upMinutes()),
                          static_cast<unsigned long>(mEpochs),
                          static_cast<unsigned long>(mWriteFails), flushed ? 1 : 0);
                disconnectSensors();
                mKernel.comm.releaseMessage(msg);
                return;
            }

            case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                auto *event = static_cast<SDK::Message::Sensor::EventData *>(msg);
                SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                onSensorData(static_cast<uint16_t>(event->handle), batch);
                break;
            }

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
            case CustomMessage::STATUS_REQUEST:
                if (!mGuiAttached) {
                    mLog.line("gui-open");
                }
                mGuiAttached = true;
                publishStatus();
                break;

            case CustomMessage::MARK:
                mGuiAttached = true;
                onMark(*static_cast<CustomMessage::Mark *>(msg));
                break;

            case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                // A typical utility service exits here; autostart exists so
                // this one does not.
                mLog.line("gui-close");
                mGuiAttached = false;
                break;

            default:
                msg->setResult(SDK::MessageResult::FAIL);
                break;
        }

        mKernel.comm.releaseMessage(msg);
    }
}
