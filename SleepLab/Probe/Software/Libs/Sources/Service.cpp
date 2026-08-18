/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Tier 0 feasibility probe. Rationale is in Service.hpp.
 ******************************************************************************
 */

#include "Service.hpp"

#include <ctime>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserSpo2.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"

#define LOG_MODULE_PRX      "Probe"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

/// Requested periods for the cheap event-driven sensors, milliseconds.
///
/// These are all *event* sensors -- they publish on a state change, not on a
/// clock -- so the period is a floor on how often the kernel will re-report,
/// not a sample rate. One second is fast enough that a worn/not-worn flicker
/// is visible and slow enough that a still night costs nothing.
constexpr float kEventPeriodMs = 1000.0f;

/// Requested period for heart rate, milliseconds. The kernel's own optical
/// pipeline runs on its own schedule; this asks for one arbitrated reading a
/// second and the log records what actually arrives.
constexpr float kHrPeriodMs = 1000.0f;

/// Requested period for the battery channels, milliseconds. Battery state of
/// charge moves in whole percent over tens of minutes, but BATTERY_METRICS
/// carries instantaneous and averaged current, which is the number the whole
/// power question turns on -- so it is sampled every 10 s and averaged into
/// the row rather than read once a minute and hoped to be representative.
constexpr float kBatteryPeriodMs = 10000.0f;

/// Requested period for PPG, milliseconds. UNA describe the waveform as 20 Hz
/// single channel, so 50 ms is the rate it already runs at; asking for less
/// would only exercise the thinning gate.
constexpr float kPpgPeriodMs = 50.0f;

/// Batch latency for the high-rate streams, milliseconds. Without batching,
/// 25 Hz accelerometer is 25 IPC wakes a second for eight hours.
constexpr uint32_t kEventLatencyMs = 0;

/// Local minutes past midnight, or -1 if the wall clock is unreadable.
///
/// Wall clock is read for *labelling only*. No duration anywhere in this app
/// is derived from two wall-clock readings -- it can jump on a timezone
/// change, a host sync or DST, and a jump would silently rewrite an interval.
int16_t localMinutes(std::time_t utc)
{
    if (utc <= 0) {
        return -1;
    }

    std::tm local {};
#if defined(_WIN32) || defined(_WIN64)
    if (localtime_s(&local, &utc) != 0) {
        return -1;
    }
#else
    if (localtime_r(&utc, &local) == nullptr) {
        return -1;
    }
#endif
    return static_cast<int16_t>(local.tm_hour * 60 + local.tm_min);
}

/// x10 of a float, truncated, saturating rather than wrapping.
int32_t x10(float v)
{
    const float scaled = v * 10.0f;
    if (scaled > 2000000000.0f) {
        return 2000000000;
    }
    if (scaled < -2000000000.0f) {
        return -2000000000;
    }
    return static_cast<int32_t>(scaled);
}

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mConfig()
    , mLog(kernel)
    // Periods are set properly in connectSensors(), once the config is read.
    , mAccel(SDK::Sensor::Type::ACCELEROMETER)
    , mTouch(SDK::Sensor::Type::TOUCH_DETECT, kEventPeriodMs, kEventLatencyMs)
    , mMotion(SDK::Sensor::Type::MOTION_DETECT, kEventPeriodMs, kEventLatencyMs)
    , mActivity(SDK::Sensor::Type::ACTIVITY_RECOGNITION, kEventPeriodMs, kEventLatencyMs)
    , mHr(SDK::Sensor::Type::HEART_RATE, kHrPeriodMs, kEventLatencyMs)
    , mHrEx(SDK::Sensor::Type::HEART_RATE_EX, kHrPeriodMs, kEventLatencyMs)
    , mBeat(SDK::Sensor::Type::HEART_BEAT, kEventPeriodMs, kEventLatencyMs)
    , mPpg(SDK::Sensor::Type::PPG, kPpgPeriodMs, kEventLatencyMs)
    , mSpo2(SDK::Sensor::Type::SPO2, kEventPeriodMs, kEventLatencyMs)
    , mSteps(SDK::Sensor::Type::STEP_COUNTER, kEventPeriodMs, kEventLatencyMs)
    , mBattLevel(SDK::Sensor::Type::BATTERY_LEVEL, kBatteryPeriodMs, kEventLatencyMs)
    , mBattCharge(SDK::Sensor::Type::BATTERY_CHARGING, kEventPeriodMs, kEventLatencyMs)
    , mBattMetrics(SDK::Sensor::Type::BATTERY_METRICS, kBatteryPeriodMs, kEventLatencyMs)
{
}

Service::~Service()
{
    disconnectSensors();
}


// -- Lifecycle ----------------------------------------------------------------

void Service::connectSensors()
{
    // The accelerometer is the only stream whose period the config sets, and
    // the only one worth batching: everything else is either an event sensor
    // or already slow.
    mHas.accel = mAccel.connect(static_cast<float>(mConfig.accelPeriodMs),
                                mConfig.accelLatencyMs);

    mHas.touch       = mTouch.connect();
    mHas.motion      = mMotion.connect();
    mHas.activity    = mActivity.connect();

    // HEART_BEAT is subscribed whatever the HR mode says. It is the highest
    // value question here -- UNA answered "no events at all" on the 1.3 line
    // and a single event on 1.4 would reopen overnight HRV -- and a stream
    // that emits nothing costs nothing to listen to.
    mHas.beat = mBeat.connect();

    if (mConfig.hrMode != Probe::HrMode::Off) {
        mHas.hr   = mHr.connect();
        mHas.hrEx = mHrEx.connect();
        // Duty mode starts in the on phase so the first row carries data.
        mHrDutyOn = true;
    }

    if (mConfig.ppgEnabled) {
        mHas.ppg = mPpg.connect();
    }
    if (mConfig.spo2Enabled) {
        mHas.spo2 = mSpo2.connect();
    }

    mHas.steps       = mSteps.connect();
    mHas.battLevel   = mBattLevel.connect();
    mHas.battCharge  = mBattCharge.connect();
    mHas.battMetrics = mBattMetrics.connect();

    // Logged as a block, because "which of these resolved" is the first thing
    // to check when a column is empty all night, and it is invisible from the
    // CSV alone.
    LOG_INFO("subscribed: acc=%d touch=%d motion=%d ar=%d hr=%d hrex=%d "
             "beat=%d ppg=%d spo2=%d steps=%d bl=%d bc=%d bm=%d\n",
             mHas.accel, mHas.touch, mHas.motion, mHas.activity,
             mHas.hr, mHas.hrEx, mHas.beat, mHas.ppg, mHas.spo2,
             mHas.steps, mHas.battLevel, mHas.battCharge, mHas.battMetrics);
}

void Service::disconnectSensors()
{
    mAccel.disconnect();
    mTouch.disconnect();
    mMotion.disconnect();
    mActivity.disconnect();
    mHr.disconnect();
    mHrEx.disconnect();
    mBeat.disconnect();
    mPpg.disconnect();
    mSpo2.disconnect();
    mSteps.disconnect();
    mBattLevel.disconnect();
    mBattCharge.disconnect();
    mBattMetrics.disconnect();
}

uint32_t Service::pumpHrDuty(uint32_t now)
{
    if (mConfig.hrMode != Probe::HrMode::Duty) {
        return 0;
    }

    // Signed difference throughout: getTimeMs() is a 32-bit uptime that wraps
    // at ~49.7 days, and an unsigned compare across the wrap would either
    // stall the cycle for weeks or fire it continuously.
    if (static_cast<int32_t>(now - mHrDutyNextAt) < 0) {
        return static_cast<uint32_t>(mHrDutyNextAt - now);
    }

    mHrDutyOn = !mHrDutyOn;
    if (mHrDutyOn) {
        mHas.hr   = mHr.connect();
        mHas.hrEx = mHrEx.connect();
        mHrDutyNextAt = now + static_cast<uint32_t>(mConfig.hrDutyOnSec) * 1000u;
    } else {
        mHr.disconnect();
        mHrEx.disconnect();
        const uint32_t offSec =
            static_cast<uint32_t>(mConfig.hrDutyPerSec - mConfig.hrDutyOnSec);
        mHrDutyNextAt = now + offSec * 1000u;
    }

    return static_cast<uint32_t>(mHrDutyNextAt - now);
}


// -- Sample path --------------------------------------------------------------

void Service::onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch)
{
    const uint16_t n = batch.size();

    if (mAccel.matchesDriver(handle)) {
        mAcc.accBatches++;
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::Accelerometer p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            const uint32_t ts = p.getTimestamp();
            if (!mAcc.accSeen) {
                mAcc.accSeen    = true;
                mAcc.accFirstTs = ts;
            } else {
                // Sensor timestamps, not loop time: the question is what the
                // sensor pipeline delivered, and the loop's own clock cannot
                // tell a late batch from a missing one.
                const uint32_t gap = ts - mAcc.accLastTs;
                if (gap > mAcc.accMaxGap) {
                    mAcc.accMaxGap = gap;
                }
            }
            mAcc.accLastTs = ts;
            mAcc.accN++;
        }
        return;
    }

    if (mTouch.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::Touch p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            const bool worn = p.isTouched();
            mAcc.touchN++;
            if (worn) {
                mAcc.touchWornN++;
            }
            // Edges are counted across row boundaries, not within them: a
            // flicker at 03:59:59 is still a flicker.
            if (mTouchLastValid && worn != mTouchLastWorn) {
                mAcc.touchEdges++;
            }
            mTouchLastWorn  = worn;
            mTouchLastValid = true;
        }
        return;
    }

    if (mMotion.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::MotionDetect p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            mAcc.motionN++;
            switch (p.getID()) {
                case SDK::SensorDataParser::MotionDetect::Motion::NO_MOTION:
                    mAcc.motionNo++;  break;
                case SDK::SensorDataParser::MotionDetect::Motion::MOTION:
                    mAcc.motionMot++; break;
                case SDK::SensorDataParser::MotionDetect::Motion::SIG_MOTION:
                    mAcc.motionSig++; break;
                default: break;
            }
        }
        return;
    }

    if (mActivity.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::ActivityRecognition p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            mAcc.arN++;
            switch (p.getID()) {
                case SDK::SensorDataParser::ActivityRecognition::Activity::STILL:
                    mAcc.arStill++; break;
                case SDK::SensorDataParser::ActivityRecognition::Activity::WALKING:
                    mAcc.arWalk++;  break;
                case SDK::SensorDataParser::ActivityRecognition::Activity::RUNNING:
                    mAcc.arRun++;   break;
                default: break;
            }
        }
        return;
    }

    if (mHr.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::HeartRate p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            const float bpm = p.getBpm();
            if (mAcc.hrN == 0 || bpm < mAcc.hrMin) {
                mAcc.hrMin = bpm;
            }
            if (mAcc.hrN == 0 || bpm > mAcc.hrMax) {
                mAcc.hrMax = bpm;
            }
            mAcc.hrN++;
            mAcc.hrSum      += bpm;
            mAcc.hrTrustSum += p.getTrustLevel();
        }
        return;
    }

    if (mHrEx.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::HeartRateEx p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            mAcc.hrExN++;
            switch (p.getSource()) {
                case SDK::SensorDataParser::HeartRateEx::Source::OPTICAL:
                    mAcc.hrExOpt++; break;
                case SDK::SensorDataParser::HeartRateEx::Source::EXTERNAL:
                    mAcc.hrExExt++; break;
                default:
                    mAcc.hrExUnk++; break;
            }
        }
        return;
    }

    if (mBeat.matchesDriver(handle)) {
        // No parser: HEART_BEAT's payload contract is unestablished precisely
        // because it has never been seen to fire. The count is the finding.
        mAcc.beatN += n;
        LOG_WARNING("HEART_BEAT delivered %u events -- this contradicts PR #167, "
                    "re-run BeatProbe and revisit the HRV clause\n",
                    static_cast<unsigned>(n));
        return;
    }

    if (mPpg.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const uint32_t ts = batch[i].getTimestamp();
            if (!mAcc.ppgSeen) {
                mAcc.ppgSeen    = true;
                mAcc.ppgFirstTs = ts;
            }
            mAcc.ppgLastTs = ts;
            mAcc.ppgN++;
        }
        return;
    }

    if (mSpo2.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::Spo2 p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            mAcc.spo2N++;
            mAcc.spo2Last = p.getSaturation();
        }
        return;
    }

    if (mSteps.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::StepCounter p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            mStepTotal = static_cast<int64_t>(p.getStepCount());
        }
        return;
    }

    if (mBattLevel.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryLevel p(batch[i]);
            if (p.isDataValid()) {
                mBattPctX10 = x10(p.getCharge());
            }
        }
        return;
    }

    if (mBattCharge.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryCharging p(batch[i]);
            if (p.isDataValid()) {
                mCharging = p.isCharging() ? 1 : 0;
                mUsb      = p.isUsbConnected() ? 1 : 0;
            }
        }
        return;
    }

    if (mBattMetrics.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryMetrics p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            mBattMv       = static_cast<int32_t>(p.getVoltage() * 1000.0f);
            mBattMaX10    = x10(p.getCurrent());
            mBattAvgMaX10 = x10(p.getAverageCurrent());
            mBattMah      = static_cast<int32_t>(p.getCapacity());
        }
        return;
    }
}


// -- Row emission -------------------------------------------------------------

void Service::emitRow(uint32_t now, uint32_t spanMs)
{
    const std::time_t wall = std::time(nullptr);

    Probe::MinuteRow r;
    r.uptimeMs = now;
    r.wallUtc  = (wall > 0) ? static_cast<int64_t>(wall) : -1;
    r.localMin = localMinutes(wall);
    r.spanMs   = spanMs;

    if (mHas.accel) {
        r.accN        = static_cast<int32_t>(mAcc.accN);
        r.accBatches  = static_cast<int32_t>(mAcc.accBatches);
        // A span needs two samples to exist. One sample is a count, not a rate.
        r.accTsSpanMs = (mAcc.accN >= 2)
                            ? static_cast<int32_t>(mAcc.accLastTs - mAcc.accFirstTs)
                            : -1;
        r.accMaxGapMs = (mAcc.accN >= 2) ? static_cast<int32_t>(mAcc.accMaxGap) : -1;
    }

    if (mHas.touch) {
        r.touchN     = static_cast<int32_t>(mAcc.touchN);
        r.touchWornN = static_cast<int32_t>(mAcc.touchWornN);
        r.touchEdges = static_cast<int32_t>(mAcc.touchEdges);
    }

    if (mHas.motion) {
        r.motionN   = static_cast<int32_t>(mAcc.motionN);
        r.motionNo  = static_cast<int32_t>(mAcc.motionNo);
        r.motionMot = static_cast<int32_t>(mAcc.motionMot);
        r.motionSig = static_cast<int32_t>(mAcc.motionSig);
    }

    if (mHas.activity) {
        r.arN     = static_cast<int32_t>(mAcc.arN);
        r.arStill = static_cast<int32_t>(mAcc.arStill);
        r.arWalk  = static_cast<int32_t>(mAcc.arWalk);
        r.arRun   = static_cast<int32_t>(mAcc.arRun);
    }

    if (mHas.hr) {
        r.hrN = static_cast<int32_t>(mAcc.hrN);
        if (mAcc.hrN > 0) {
            const float inv = 1.0f / static_cast<float>(mAcc.hrN);
            r.hrMeanX10  = x10(mAcc.hrSum * inv);
            r.hrMin      = static_cast<int32_t>(mAcc.hrMin);
            r.hrMax      = static_cast<int32_t>(mAcc.hrMax);
            r.hrTrustX10 = x10(mAcc.hrTrustSum * inv);
        }
    }

    if (mHas.hrEx) {
        r.hrExN    = static_cast<int32_t>(mAcc.hrExN);
        r.hrExOptN = static_cast<int32_t>(mAcc.hrExOpt);
        r.hrExExtN = static_cast<int32_t>(mAcc.hrExExt);
        r.hrExUnkN = static_cast<int32_t>(mAcc.hrExUnk);
    }

    if (mHas.beat) {
        r.beatN = static_cast<int32_t>(mAcc.beatN);
    }

    if (mHas.ppg) {
        r.ppgN = static_cast<int32_t>(mAcc.ppgN);
        r.ppgTsSpanMs = (mAcc.ppgN >= 2)
                            ? static_cast<int32_t>(mAcc.ppgLastTs - mAcc.ppgFirstTs)
                            : -1;
    }

    if (mHas.spo2) {
        r.spo2N = static_cast<int32_t>(mAcc.spo2N);
        if (mAcc.spo2N > 0) {
            r.spo2LastX10 = x10(mAcc.spo2Last);
        }
    }

    if (mHas.steps && mStepTotal >= 0) {
        r.stepTotal = mStepTotal;
        // STEP_COUNTER is monotonic since boot, so a *decrease* means the
        // device rebooted under us and the delta is meaningless, not negative.
        if (mStepAtRowStart >= 0 && mStepTotal >= mStepAtRowStart) {
            r.stepDelta = static_cast<int32_t>(mStepTotal - mStepAtRowStart);
        }
        mStepAtRowStart = mStepTotal;
    }

    r.battPctX10   = mBattPctX10;
    r.charging     = mCharging;
    r.usb          = mUsb;
    r.battMv       = mBattMv;
    r.battMaX10    = mBattMaX10;
    r.battAvgMaX10 = mBattAvgMaX10;
    r.battMah      = mBattMah;

    r.wakes = static_cast<int32_t>(mAcc.wakes);
    r.msgs  = static_cast<int32_t>(mAcc.msgs);

    // Kept for the status screen, which has to show the row that just closed
    // rather than the one now accumulating.
    mLastAccN      = r.accN;
    mLastHrN       = r.hrN;
    mLastTouchWorn = r.touchWornN;
    mLastTouchN    = r.touchN;
    mTotalBeatN   += mAcc.beatN;
    mTotalSpo2N   += mAcc.spo2N;

    if (mLog.write(r)) {
        mRowsWritten++;
    }
    mAcc.reset();

    publishStatus();
}


// -- GUI ----------------------------------------------------------------------

void Service::publishStatus()
{
    if (!mGuiStarted) {
        return;
    }

    uint16_t subs = 0;
    if (mHas.accel)       { subs |= CustomMessage::Sub::kAccel; }
    if (mHas.touch)       { subs |= CustomMessage::Sub::kTouch; }
    if (mHas.motion)      { subs |= CustomMessage::Sub::kMotion; }
    if (mHas.activity)    { subs |= CustomMessage::Sub::kActivity; }
    if (mHas.hr)          { subs |= CustomMessage::Sub::kHr; }
    if (mHas.hrEx)        { subs |= CustomMessage::Sub::kHrEx; }
    if (mHas.beat)        { subs |= CustomMessage::Sub::kBeat; }
    if (mHas.ppg)         { subs |= CustomMessage::Sub::kPpg; }
    if (mHas.spo2)        { subs |= CustomMessage::Sub::kSpo2; }
    if (mHas.steps)       { subs |= CustomMessage::Sub::kSteps; }
    if (mHas.battLevel)   { subs |= CustomMessage::Sub::kBattLevel; }
    if (mHas.battCharge)  { subs |= CustomMessage::Sub::kBattCharge; }
    if (mHas.battMetrics) { subs |= CustomMessage::Sub::kBattMetrics; }

    auto msg = SDK::make_msg<CustomMessage::ProbeStatus>(mKernel);
    if (!msg) {
        return;
    }

    msg->rowsWritten   = mRowsWritten;
    msg->rowFailures   = mLog.failures();
    msg->bytesWritten  = static_cast<uint32_t>(mLog.bytesWritten());
    msg->runningMs     = mKernel.sys.getTimeMs() - mStartedAt;
    msg->subscribed    = subs;
    msg->hrMode        = static_cast<uint16_t>(mConfig.hrMode);
    msg->lastAccN      = mLastAccN;
    msg->lastHrN       = mLastHrN;
    msg->lastTouchWorn = mLastTouchWorn;
    msg->lastTouchN    = mLastTouchN;
    msg->totalBeatN    = mTotalBeatN;
    msg->totalSpo2N    = mTotalSpo2N;
    msg->battPctX10    = mBattPctX10;
    msg->charging      = mCharging;
    msg->usb           = mUsb;

    msg.send();
}


// -- The loop -----------------------------------------------------------------

void Service::run()
{
    const uint32_t start = mKernel.sys.getTimeMs();

    const Probe::Status cfgStatus = Probe::load(mKernel, mConfig);
    LOG_INFO("config %s: hr=%s accel=%ums/%ums ppg=%d spo2=%d\n",
             Probe::toString(cfgStatus), Probe::toString(mConfig.hrMode),
             static_cast<unsigned>(mConfig.accelPeriodMs),
             static_cast<unsigned>(mConfig.accelLatencyMs),
             mConfig.ppgEnabled, mConfig.spo2Enabled);

    const std::time_t wall = std::time(nullptr);
    mLog.begin(start, (wall > 0) ? static_cast<int64_t>(wall) : -1,
               Probe::toString(mConfig.hrMode));

    connectSensors();

    mStartedAt    = start;
    mRowOpenedAt  = start;
    mNextRowAt    = start + kRowPeriodMs;
    mHrDutyNextAt = start + static_cast<uint32_t>(mConfig.hrDutyOnSec) * 1000u;

    while (true) {
        const uint32_t now = mKernel.sys.getTimeMs();

        // Signed difference, so the ~49.7-day uptime wrap cannot turn "due in
        // 400 ms" into "due in 49 days".
        int32_t toRow = static_cast<int32_t>(mNextRowAt - now);
        if (toRow <= 0) {
            emitRow(now, now - mRowOpenedAt);
            mRowOpenedAt = now;
            // Advance the grid by exactly one period rather than re-basing on
            // `now`. If the loop overslept by more than a whole period the
            // catch-up would spin, so skip forward instead -- and the skipped
            // rows are themselves the finding, visible as a jump in uptime_ms.
            do {
                mNextRowAt += kRowPeriodMs;
            } while (static_cast<int32_t>(mNextRowAt - now) <= 0);
            toRow = static_cast<int32_t>(mNextRowAt - now);
        }

        uint32_t sleepMs = static_cast<uint32_t>(toRow);

        const uint32_t toDuty = pumpHrDuty(now);
        if (toDuty > 0 && toDuty < sleepMs) {
            sleepMs = toDuty;
        }

        SDK::MessageBase *msg = nullptr;
        if (mKernel.comm.getMessage(msg, sleepMs)) {
            mAcc.wakes++;
            mAcc.msgs++;

            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP: {
                    // Almost always the USB cable. Write the partial row
                    // before going: where a night stops is the finding, and a
                    // row that never reached storage cannot say where that was.
                    const uint32_t stopAt = mKernel.sys.getTimeMs();
                    emitRow(stopAt, stopAt - mRowOpenedAt);
                    LOG_INFO("stopping: %lu rows failed, %llu bytes written\n",
                             static_cast<unsigned long>(mLog.failures()),
                             static_cast<unsigned long long>(mLog.bytesWritten()));
                    disconnectSensors();
                    mKernel.comm.releaseMessage(msg);
                    return;
                }

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto *event = static_cast<SDK::Message::Sensor::EventData *>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count,
                                                 event->stride);
                    onSensorData(static_cast<uint16_t>(event->handle), batch);
                    break;
                }

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    mGuiStarted = true;
                    publishStatus();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    // Deliberately no exit here, unlike a typical utility
                    // service. Autostart exists so the recording continues
                    // whether anyone is looking or not, and closing the screen
                    // at 22:45 is the normal case rather than a shutdown.
                    mGuiStarted = false;
                    break;

                case CustomMessage::PROBE_REQUEST:
                    // The screen just opened and does not want to wait up to a
                    // minute for the next row to produce one.
                    publishStatus();
                    break;

                default:
                    break;
            }

            mKernel.comm.releaseMessage(msg);
        } else {
            mAcc.wakes++;
        }
    }
}
