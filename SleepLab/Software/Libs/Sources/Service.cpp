/**
 ******************************************************************************
 * @file    Service.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The recorder. Rationale is in Service.hpp.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstring>
#include <ctime>

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp"

#define LOG_MODULE_PRX      "SleepLab"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

using Engine::kAbsent;

namespace {

/// Requested accelerometer period, ms. 40 ms nominal (25 Hz) sits at the low
/// end of what published count derivations assume and well inside the
/// 0.25-3 Hz band the counts are filtered to. It is a *request*: the delivered
/// rate is not the same number, which is why EpochCounter is rate-independent
/// by construction and why the probe measures it.
constexpr float kAccelPeriodMs = 40.0f;

/// Requested accelerometer batch latency, ms. Without batching, 25 Hz is 25
/// IPC wakes a second for eight hours.
/// TODO: set from the wakes/minute and battery columns of the first probe
/// nights (ledger rows S2, S10). 5 s is a deliberate guess.
constexpr uint32_t kAccelLatencyMs = 5000;

/// Requested period for the event sensors, ms. These publish on a state change
/// rather than on a clock, so this is a floor on re-reporting, not a rate.
constexpr float kEventPeriodMs = 1000.0f;

/// Requested period for heart rate, ms.
constexpr float kHrPeriodMs = 1000.0f;

/// Requested period for the battery channels, ms. State of charge moves in
/// whole percent over tens of minutes.
constexpr float kBatteryPeriodMs = 30000.0f;

/// Below this many accelerometer samples, a recording epoch is treated as a
/// data gap rather than as a quiet epoch.
///
/// A near-empty epoch integrates to near-zero, which reads as perfect
/// stillness -- so an undetected delivery outage would be the soundest sleep of
/// the night. The scorer has its own, looser guard on the scoring epoch; this
/// one exists to raise the night's `kDataGap` flag, which the report shows.
/// TODO: set from the delivered-rate column of the first probe nights.
constexpr uint16_t kMinSamplesPerRecordingEpoch = 60;

/// Local minutes past midnight, or -1 if the wall clock is unreadable.
///
/// Read for *labelling only*. No duration anywhere in this app comes from two
/// wall-clock readings: the clock can jump on a timezone change, a host sync or
/// DST, and a jump would silently rewrite an interval.
int16_t localMinutes(std::time_t utc)
{
    if (utc <= 0) {
        return -1;
    }
    std::tm local {};
#if defined(_WIN32) || defined(_WIN64)
    if (localtime_s(&local, &utc) != 0) { return -1; }
#else
    if (localtime_r(&utc, &local) == nullptr) { return -1; }
#endif
    return static_cast<int16_t>(local.tm_hour * 60 + local.tm_min);
}

} // namespace


Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mSettings()
    , mStore(kernel)
    , mRaw(kernel)
    , mCounter()
    , mSegmenter()
    , mBaseline()
    , mAccel(SDK::Sensor::Type::ACCELEROMETER, kAccelPeriodMs, kAccelLatencyMs)
    , mTouch(SDK::Sensor::Type::TOUCH_DETECT, kEventPeriodMs)
    , mMotion(SDK::Sensor::Type::MOTION_DETECT, kEventPeriodMs)
    , mActivity(SDK::Sensor::Type::ACTIVITY_RECOGNITION, kEventPeriodMs)
    , mHr(SDK::Sensor::Type::HEART_RATE, kHrPeriodMs)
    , mHrEx(SDK::Sensor::Type::HEART_RATE_EX, kHrPeriodMs)
    , mSteps(SDK::Sensor::Type::STEP_COUNTER, kEventPeriodMs)
    , mBattLevel(SDK::Sensor::Type::BATTERY_LEVEL, kBatteryPeriodMs)
    , mBattCharge(SDK::Sensor::Type::BATTERY_CHARGING, kEventPeriodMs)
{
}

Service::~Service()
{
    disconnectSensors();
}


// -- Lifecycle ------------------------------------------------------------------

void Service::connectSensors()
{
    mAccel.connect();
    mTouch.connect();
    mMotion.connect();
    mActivity.connect();
    mSteps.connect();
    mBattLevel.connect();
    mBattCharge.connect();

    if (mSettings.hrMode != SleepLab::HrMode::Off) {
        mHr.connect();
        mHrEx.connect();
        mHrDutyOn = true;
    }
}

void Service::disconnectSensors()
{
    mAccel.disconnect();
    mTouch.disconnect();
    mMotion.disconnect();
    mActivity.disconnect();
    mHr.disconnect();
    mHrEx.disconnect();
    mSteps.disconnect();
    mBattLevel.disconnect();
    mBattCharge.disconnect();
}

uint32_t Service::pumpHrDuty(uint32_t now)
{
    if (mSettings.hrMode != SleepLab::HrMode::Duty) {
        return 0;
    }

    // Signed difference: uptime is 32-bit and wraps at ~49.7 days, and an
    // unsigned compare across the wrap would either stall the cycle for weeks
    // or fire it continuously.
    if (static_cast<int32_t>(now - mHrDutyNextAt) < 0) {
        return static_cast<uint32_t>(mHrDutyNextAt - now);
    }

    mHrDutyOn = !mHrDutyOn;
    if (mHrDutyOn) {
        mHr.connect();
        mHrEx.connect();
        mHrDutyNextAt = now + static_cast<uint32_t>(mSettings.hrDutyOnSec) * 1000u;
    } else {
        mHr.disconnect();
        mHrEx.disconnect();
        const uint32_t offSec =
            static_cast<uint32_t>(mSettings.hrDutyPerSec - mSettings.hrDutyOnSec);
        mHrDutyNextAt = now + offSec * 1000u;
    }
    return static_cast<uint32_t>(mHrDutyNextAt - now);
}


// -- Sample path ------------------------------------------------------------------

void Service::onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch)
{
    const uint16_t n = batch.size();

    if (mAccel.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::Accelerometer p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            float x = 0.0f, y = 0.0f, z = 0.0f;
            p.getXYZ(x, y, z);
            // The sensor's own timestamp, not a clock read here: batches arrive
            // late and in bursts, and the loop's clock would attribute a whole
            // batch to the instant it was delivered.
            const uint32_t ts = p.getTimestamp();
            mCounter.add(ts, x, y, z);
            if (mRaw.isRecording()) {
                mRaw.add(ts, x, y, z);
            }
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
            if (mTouchLastValid && worn != mTouchLastWorn &&
                mAcc.touchEdges < 255) {
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
            using M = SDK::SensorDataParser::MotionDetect::Motion;
            if (p.getID() == M::MOTION)     { mAcc.motion++; }
            if (p.getID() == M::SIG_MOTION) { mAcc.sigMotion++; }
        }
        return;
    }

    if (mActivity.matchesDriver(handle)) {
        // Subscribed but not folded into an epoch field. ACTIVITY_RECOGNITION
        // is cheap corroboration for out-of-bed and the accelerometer already
        // carries it: a wearer the kernel calls WALKING has counts far past
        // the segmenter's activity floor. Kept connected so the probe's
        // contention question (ledger row S8) is answered under the same load
        // the real app puts on the sensor layer.
        return;
    }

    if (mHr.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::HeartRate p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            const float bpm = p.getBpm();
            if (bpm <= 0.0f) {
                continue;
            }
            const uint16_t x10 = static_cast<uint16_t>(bpm * 10.0f);
            if (mAcc.hrCount == 0 || x10 < mAcc.hrMinX10) {
                mAcc.hrMinX10 = x10;
            }
            mAcc.hrSum += x10;
            mAcc.hrCount++;
        }
        return;
    }

    if (mHrEx.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::HeartRateEx p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            // Provenance recorded rather than assumed. An epoch whose HR came
            // from a chest strap is a different measurement from one whose HR
            // came from the wrist: the strap is electrical, the wrist optical,
            // and only the wrist degrades with a loose band.
            using S = SDK::SensorDataParser::HeartRateEx::Source;
            if (p.getSource() == S::OPTICAL)  { mAcc.hrOptical  = true; }
            if (p.getSource() == S::EXTERNAL) { mAcc.hrExternal = true; }
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

    if (mBattLevel.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryLevel p(batch[i]);
            if (p.isDataValid()) {
                mBattPctX10 = static_cast<int32_t>(p.getCharge() * 10.0f);
            }
        }
        return;
    }

    if (mBattCharge.matchesDriver(handle)) {
        for (uint16_t i = 0; i < n; i++) {
            const SDK::SensorDataParser::BatteryCharging p(batch[i]);
            if (!p.isDataValid()) {
                continue;
            }
            if (p.isCharging() || p.isUsbConnected()) {
                // One epoch of this marks the whole night interrupted.
                // Plugging in terminates every running app, so a night that
                // saw the charger has a hole in it whose length is not even
                // knowable from in here.
                mCharging = true;
                mFlags |= Engine::Interruption::kCharging;
            } else {
                mCharging = false;
            }
        }
        return;
    }
}


// -- Epoch pipeline ----------------------------------------------------------------

void Service::closeRecordingEpoch(uint32_t now, uint32_t spanMs)
{
    Engine::Epoch e;
    e.uptimeMs = now;
    const std::time_t wall = std::time(nullptr);
    e.wallUtc  = (wall > 0) ? static_cast<int64_t>(wall) : -1;
    e.spanMs   = spanMs;

    mCounter.closeEpoch(e.count, e.peak, e.samples);

    e.motionEvents = mAcc.motion;
    e.sigMotion    = mAcc.sigMotion;

    if (mStepTotal >= 0) {
        // STEP_COUNTER is monotonic since boot, so a *decrease* means the
        // device rebooted under us: the delta is unknown, not negative.
        if (mStepAtEpoch >= 0 && mStepTotal >= mStepAtEpoch) {
            e.stepDelta = static_cast<int32_t>(mStepTotal - mStepAtEpoch);
        }
        mStepAtEpoch = mStepTotal;
    }

    if (mAcc.hrCount > 0) {
        e.hrMeanX10 = static_cast<int16_t>(mAcc.hrSum / mAcc.hrCount);
        e.hrMinX10  = static_cast<int16_t>(mAcc.hrMinX10);
        e.hrSamples = mAcc.hrCount;
        if (mAcc.hrOptical && mAcc.hrExternal) {
            e.hrSource = Engine::HrSource::Mixed;
        } else if (mAcc.hrExternal) {
            e.hrSource = Engine::HrSource::External;
        } else if (mAcc.hrOptical) {
            e.hrSource = Engine::HrSource::Optical;
        }
    }

    if (mAcc.touchN > 0) {
        e.wornPct = static_cast<uint8_t>(mAcc.touchWornN * 100u / mAcc.touchN);
    }
    e.wornEdges  = mAcc.touchEdges;
    e.battPctX10 = static_cast<int16_t>(mBattPctX10);
    e.charging   = mCharging;

    // The reserved HRV fields stay absent. There is no producer: HEART_BEAT
    // emits nothing, so there are no RR intervals. They are written every row
    // anyway so the column exists in every night already on disk.

    // A near-empty epoch integrates to near-zero, which reads as perfect
    // stillness -- so an undetected outage would be the best-looking part of
    // the night. Flagged here; the scorer's own guard is separate and looser.
    if (e.samples < kMinSamplesPerRecordingEpoch) {
        mFlags |= Engine::Interruption::kDataGap;
    }

    mAcc.reset();

    if (mStore.isOpen()) {
        mStore.appendEpoch(e, mFlags);
    } else {
        // Idle: keep the epoch in the ring, so a night that opens on sustained
        // stillness can be backdated to the minutes it actually began in.
        mPreRoll[mPreRollNext] = e;
        mPreRollNext = (mPreRollNext + 1) % kPreRollEpochs;
        if (mPreRollCount < kPreRollEpochs) {
            mPreRollCount++;
        }
    }

    // Fold into the scoring epoch in progress. Counts are SUMMED, not
    // averaged: a count is an integral, so the integral over a minute is the
    // sum of the integrals over its halves, and averaging would halve every
    // count and quietly rescale the whole night.
    mPendingScore.count   += e.count;
    mPendingScore.samples =
        static_cast<uint16_t>(mPendingScore.samples + e.samples);
    // Worn is a mean of the halves; on the first half this is just the value.
    mPendingScore.wornPct = static_cast<uint8_t>(
        (mPendingScore.wornPct * mPendingHalves + e.wornPct) /
        (mPendingHalves + 1));
    if (e.hrMeanX10 != static_cast<int16_t>(kAbsent)) {
        mPendingScore.hrMeanX10 =
            (mPendingScore.hrMeanX10 == static_cast<int16_t>(kAbsent))
                ? e.hrMeanX10
                : static_cast<int16_t>((mPendingScore.hrMeanX10 + e.hrMeanX10) / 2);
    }

    mPendingHalves++;
    if (mPendingHalves >= Engine::kEpochsPerScoringEpoch) {
        closeScoringEpoch();
    }
}

void Service::closeScoringEpoch()
{
    const Engine::ScoringInput scored = mPendingScore;
    mPendingScore  = Engine::ScoringInput{};
    mPendingHalves = 0;

    const std::time_t wall = std::time(nullptr);
    const int16_t localMin = localMinutes(wall);

    // The most recent recording epoch carries the step delta the segmenter
    // wants; a scoring epoch is two of them, and either half walking is
    // walking.
    int32_t stepDelta = kAbsent;
    if (mPreRollCount > 0 || mStore.isOpen()) {
        stepDelta = (mStepAtEpoch >= 0) ? 0 : kAbsent;
    }

    const bool worn = scored.wornPct >= Engine::SleepWakeScorer::kMinWornPct;
    const Engine::NightSegmenter::Update u =
        mSegmenter.update(localMin, worn, scored.count, stepDelta);

    if (u.clockJumped) {
        mFlags |= Engine::Interruption::kClockJump;
    }

    switch (u.event) {
        case Engine::NightSegmenter::Event::Opened:
            openNight(u.backdateEpochs);
            break;
        case Engine::NightSegmenter::Event::Closed:
            // The epoch that closed the night is part of it.
            if (mScoringCount < Engine::kMaxScoringEpochs) {
                mScoring[mScoringCount++] = scored;
            }
            closeNight(false);
            return;
        case Engine::NightSegmenter::Event::Discarded:
            closeNight(true);
            return;
        default:
            break;
    }

    if (mStore.isOpen()) {
        if (mScoringCount < Engine::kMaxScoringEpochs) {
            mScoring[mScoringCount++] = scored;
        } else if ((mFlags & Engine::Interruption::kTruncated) == 0) {
            // A "night" that ran 16 hours is a data-quality problem, and the
            // segmenter should have caught it. Flagged rather than silently
            // dropped, and only once.
            mFlags |= Engine::Interruption::kTruncated;
            LOG_WARNING("night exceeded %u scoring epochs; truncating\n",
                        static_cast<unsigned>(Engine::kMaxScoringEpochs));
        }
        checkAlarm();
    }

    // Once a scoring epoch has landed, the phase line on any open screen is
    // stale. Cheap: publishReport() returns immediately with no GUI attached,
    // which is the case for all but a minute of every eight hours.
    publishReport();
}

void Service::flushPreRoll(uint16_t scoringEpochs)
{
    // Two recording epochs per scoring epoch, bounded by what the ring holds.
    size_t want = static_cast<size_t>(scoringEpochs) *
                  Engine::kEpochsPerScoringEpoch;
    if (want > mPreRollCount) {
        want = mPreRollCount;
    }

    const size_t first = (mPreRollNext + kPreRollEpochs - want) % kPreRollEpochs;
    for (size_t i = 0; i < want; ++i) {
        mStore.appendEpoch(mPreRoll[(first + i) % kPreRollEpochs], mFlags);
    }
    LOG_INFO("backdated %u epochs into the night\n",
             static_cast<unsigned>(want));
}


// -- Night lifecycle ------------------------------------------------------------------

void Service::openNight(uint16_t backdateScoringEpochs)
{
    const std::time_t wall = std::time(nullptr);
    mNightStartUtc = (wall > 0) ? static_cast<int64_t>(wall) : -1;

    // Backdated: the session began where the still run began, not where it was
    // proved. Adjusting the recorded start by the same amount keeps the file
    // name and the summary agreeing about when the night was.
    if (mNightStartUtc > 0) {
        mNightStartUtc -= static_cast<int64_t>(backdateScoringEpochs) * 60;
    }

    mScoringCount  = 0;
    mResumedEpochs = 0;
    mAlarmFired    = false;
    // Deliberately NOT cleared: charging and clock jumps seen while idle in the
    // minutes now being backdated into the night are part of the night.
    // mFlags stands.

    if (!mStore.beginNight(mNightStartUtc, mKernel.sys.getTimeMs())) {
        LOG_WARNING("could not open a night file; recording to RAM only\n");
    } else {
        flushPreRoll(backdateScoringEpochs);
    }

    // The backdated scoring epochs are in the CSV but not in the scoring
    // array: the ring holds Epochs, and re-pairing them here would duplicate
    // the pairing logic. They are counted instead, so time in bed is right.
    mResumedEpochs = backdateScoringEpochs;

    if (mSettings.rawRecording) {
        mRaw.start(mNightStartUtc, mSettings.rawMaxMb, mSettings.rawMaxMin);
    }

    mPreRollCount = 0;
    LOG_INFO("night opened, backdated %u min\n",
             static_cast<unsigned>(backdateScoringEpochs));
    publishReport();
}

void Service::closeNight(bool discard)
{
    mRaw.stop();

    if (discard) {
        mStore.discardNight();
        mScoringCount = 0;
        mFlags        = 0;
        publishReport();
        return;
    }

    const size_t n = mScoringCount;

    // Score, gate, analyse -- in that order, because the gate's verdict is what
    // decides whether the analyser is allowed to fill in a single sleep field.
    Engine::SleepWakeScorer::score(mScoring, n, mVerdicts);

    const bool hrSampled = (mSettings.hrMode != SleepLab::HrMode::Off);
    const Engine::WornGate::Result gate =
        Engine::WornGate::evaluate(mScoring, n, hrSampled);

    Engine::NightSummary s =
        Engine::NightAnalyser::analyse(mScoring, mVerdicts, n, gate, mFlags);

    // Time in bed has to include the epochs recorded before this launch or
    // before the backdate -- they are in the CSV and they were part of the
    // night, they are simply not in the scoring array.
    if (s.hasSleep && mResumedEpochs > 0) {
        s.timeInBedMin += static_cast<int32_t>(mResumedEpochs);
        s.efficiencyPct = (s.timeInBedMin > 0)
                              ? (s.totalSleepMin * 100 / s.timeInBedMin)
                              : kAbsent;
    }

    mLastBandUsedHr = Engine::RestfulnessBand::compute(mScoring, mVerdicts, n,
                                                       s.hrMinX10, mBand);

    // Times of day, derived here because the service is the only half that
    // holds both clocks. The GUI must never do wall-clock arithmetic.
    mLastAsleepAtMin = -1;
    mLastWokeAtMin   = -1;
    if (s.hasSleep && mNightStartUtc > 0) {
        const int64_t onsetUtc =
            mNightStartUtc + static_cast<int64_t>(s.onsetEpoch) * 60;
        const int64_t wakeUtc =
            mNightStartUtc + static_cast<int64_t>(s.finalWakeEpoch) * 60;
        mLastAsleepAtMin = localMinutes(static_cast<std::time_t>(onsetUtc));
        mLastWokeAtMin   = localMinutes(static_cast<std::time_t>(wakeUtc));
    }

    mStore.finishNight(s, Engine::RestfulnessBand::kMethod, mLastBandUsedHr,
                       SleepLab::toString(mSettings.hrMode));

    // Rebuilt from the index, which now includes tonight. One source of truth:
    // there is no separate baseline file to fall out of step with it.
    mStore.loadBaseline(mBaseline);

    mLastSummary  = s;
    mHaveReport   = true;
    mScoringCount = 0;
    mFlags        = 0;

    LOG_INFO("night closed: worn=%u sleep=%ld min eff=%ld%%\n",
             static_cast<unsigned>(s.worn),
             static_cast<long>(s.totalSleepMin),
             static_cast<long>(s.efficiencyPct));

    publishReport();
    publishHistory();
}


// -- Alarm ---------------------------------------------------------------------------

void Service::checkAlarm()
{
    if (!mSettings.alarmEnabled || mAlarmFired || !mStore.isOpen()) {
        return;
    }

    const int16_t nowMin = localMinutes(std::time(nullptr));
    if (nowMin < 0) {
        return;   // no clock, no deadline
    }

    const int16_t windowStart = static_cast<int16_t>(
        (mSettings.alarmDeadlineMin - mSettings.alarmWindowMin +
         Engine::kMinutesPerDay) % Engine::kMinutesPerDay);

    // The deadline first: it fires whatever the scorer thinks, which is the
    // whole point of having one.
    if (nowMin == mSettings.alarmDeadlineMin) {
        LOG_INFO("alarm: deadline\n");
        playAlarm();
        return;
    }

    if (!Engine::inWindow(nowMin, windowStart, mSettings.alarmDeadlineMin)) {
        return;
    }

    // Inside the smart window, fire on the first epoch that is not sleep.
    //
    // The verdict used is the **raw Cole-Kripke one, without Webster
    // rescoring**, and there is no way around that: rescoring is a whole-night
    // pass and there is no whole night yet. It also has to be an epoch with its
    // full look-ahead available, so the freshest one that can be judged is two
    // scoring epochs old -- a two-minute lag inside a thirty-minute window.
    if (mScoringCount <= static_cast<size_t>(Engine::SleepWakeScorer::kLookAhead)) {
        return;
    }
    const size_t at = mScoringCount - 1 -
                      static_cast<size_t>(Engine::SleepWakeScorer::kLookAhead);
    const Engine::Verdict v =
        Engine::SleepWakeScorer::rawVerdict(mScoring, mScoringCount, at);

    if (v == Engine::Verdict::Wake) {
        LOG_INFO("alarm: wake-ish epoch inside the window\n");
        playAlarm();
    }
}

void Service::playAlarm()
{
    mAlarmFired = true;

    // Backlight, vibro and buzzer, the same three `Alarm`'s service raises --
    // and from a service with no GUI attached, which is the case at 06:30.
    //
    // As of the 1.4 kernel, mute does not silence app-requested alerts: muting
    // covers alerts the watch raises at the user, not feedback an app produces
    // in a session it owns (PR #267, una-kernel#260). Unverified on this unit
    // -- ledger row T1 -- and an alarm that fails silently is worse than no
    // alarm, which is why the README says to test it on a weekend first.
    if (auto msg = SDK::make_msg<SDK::Message::RequestBacklightSet>(mKernel)) {
        msg->autoOffTimeoutMs = 8000;
        msg->brightness       = 100;
        msg.send();
    }

    if (auto msg = SDK::make_msg<SDK::Message::RequestVibroPlay>(mKernel)) {
        for (int i = 0; i < 5; i += 2) {
            msg->notes[i].effect =
                SDK::Message::RequestVibroPlay::Effect::ALERT_750MS_100;
        }
        msg->notes[1].pause = 250;
        msg->notes[3].pause = 250;
        msg->notesCount = 5;
        msg.send();
    }

    if (auto msg = SDK::make_msg<SDK::Message::RequestBuzzerPlay>(mKernel)) {
        for (int i = 0; i < 5; i++) {
            msg->notes[i].volume = (i % 2 == 0) ? 100 : 0;
            msg->notes[i].time   = (i % 2 == 0) ? 750 : 250;
        }
        msg->notesCount = 5;
        msg.send();
    }
}


// -- GUI --------------------------------------------------------------------------------

void Service::buildStrip(CustomMessage::SleepReportData &msg) const
{
    for (uint16_t i = 0; i < CustomMessage::kStripBuckets; i++) {
        msg.strip[i] = CustomMessage::Strip::pack(
            CustomMessage::Strip::kVerdictNone, 0);
    }

    const size_t n = mScoringCount > 0 ? mScoringCount : mLastSummary.epochs;
    if (n == 0) {
        msg.stripUsed = 0;
        return;
    }

    const uint16_t used = (n < CustomMessage::kStripBuckets)
                              ? static_cast<uint16_t>(n)
                              : CustomMessage::kStripBuckets;
    msg.stripUsed = used;

    for (uint16_t b = 0; b < used; b++) {
        const size_t from = n * b / used;
        size_t       to   = n * (b + 1) / used;
        if (to <= from) {
            to = from + 1;
        }

        // The bucket takes the *worst* verdict in its range and the *least
        // settled* band. Deliberately not the majority: a five-minute bucket
        // containing one minute awake should show that the wearer woke, and a
        // majority vote would hide every short awakening in the night -- which
        // is precisely the thing a restless night is made of.
        uint8_t worst = static_cast<uint8_t>(Engine::Verdict::Sleep);
        uint8_t band  = static_cast<uint8_t>(Engine::Restfulness::Deepest);
        bool    any   = false;

        for (size_t i = from; i < to && i < Engine::kMaxScoringEpochs; ++i) {
            const uint8_t v = static_cast<uint8_t>(mVerdicts[i]);
            const uint8_t r = static_cast<uint8_t>(mBand[i]);
            if (v > worst) { worst = v; }
            if (r != static_cast<uint8_t>(Engine::Restfulness::Unknown) &&
                r < band) {
                band = r;
            }
            any = true;
        }

        msg.strip[b] = any
            ? CustomMessage::Strip::pack(worst, band)
            : CustomMessage::Strip::pack(CustomMessage::Strip::kVerdictNone, 0);
    }
}

void Service::publishReport()
{
    // Nothing is published while no GUI is attached. That matters more than it
    // sounds for an app that runs for the device's whole life: the screen is
    // open for perhaps a minute of every eight hours, and publishing into a
    // void the rest of the time is pure battery cost.
    if (!mGuiStarted) {
        return;
    }

    auto msg = SDK::make_msg<CustomMessage::SleepReport>(mKernel);
    if (!msg) {
        return;
    }

    if (mStore.isOpen()) {
        msg->data.phase = static_cast<uint8_t>(CustomMessage::Phase::Recording);
    } else if (mHaveReport) {
        msg->data.phase = static_cast<uint8_t>(CustomMessage::Phase::Reported);
    } else if (mSegmenter.state() == Engine::NightSegmenter::State::Idle &&
               Engine::inWindow(localMinutes(std::time(nullptr)),
                                mSettings.segmenter.windowStartMin,
                                mSettings.segmenter.windowEndMin)) {
        msg->data.phase = static_cast<uint8_t>(CustomMessage::Phase::Watching);
    } else {
        msg->data.phase = static_cast<uint8_t>(CustomMessage::Phase::Idle);
    }

    const Engine::NightSummary &s = mLastSummary;
    msg->data.worn         = static_cast<uint8_t>(s.worn);
    msg->data.hasSleep     = s.hasSleep;
    msg->data.interruption = s.interruption;
    msg->data.asleepAtMin  = mLastAsleepAtMin;
    msg->data.wokeAtMin    = mLastWokeAtMin;

    msg->data.timeInBedMin    = s.timeInBedMin;
    msg->data.totalSleepMin   = s.totalSleepMin;
    msg->data.stillInBedMin   = s.stillInBedMin;
    msg->data.wasoMin         = s.wasoMin;
    msg->data.awakenings      = s.awakenings;
    msg->data.efficiencyPct   = s.efficiencyPct;
    msg->data.onsetLatencyMin = s.onsetLatencyMin;
    msg->data.hrMinX10        = s.hrMinX10;
    msg->data.hrMeanX10       = s.hrMeanX10;
    msg->data.bandUsedHr      = mLastBandUsedHr;
    msg->data.epochs          = static_cast<uint16_t>(s.epochs);

    // Deltas against the wearer's own nights, and only once enough exist. A
    // delta shown with a caveat is a number that gets remembered without the
    // caveat, so below the threshold nothing is sent but the shortfall.
    const auto hrDelta  = mBaseline.hrMin(s.hrMinX10);
    const auto effDelta = mBaseline.efficiency(s.efficiencyPct);
    msg->data.hrDeltaAvailable  = hrDelta.available;
    msg->data.hrDeltaX10        = hrDelta.delta;
    msg->data.effDeltaAvailable = effDelta.available;
    msg->data.effDeltaPct       = effDelta.delta;
    msg->data.baselineNights    = static_cast<uint16_t>(hrDelta.nights);
    msg->data.nightsNeeded      = static_cast<uint16_t>(hrDelta.nightsNeeded);

    buildStrip(msg->data);
    msg.send();
}

void Service::publishHistory()
{
    if (!mGuiStarted) {
        return;
    }

    SleepLab::NightStore::IndexRow rows[SleepLab::NightStore::kMaxHistory];
    const size_t n = mStore.readHistory(rows, SleepLab::NightStore::kMaxHistory);

    // Always sent, even when empty. An empty history and a dropped burst look
    // identical to the GUI otherwise, and only one of them means "you have not
    // recorded a night yet".
    if (n == 0) {
        auto msg = SDK::make_msg<CustomMessage::SleepHistory>(mKernel);
        if (msg) {
            msg->total = 0;
            msg.send();
        }
        return;
    }

    // Newest first, which is the order the list draws them in.
    for (size_t sent = 0; sent < n; sent += CustomMessage::kHistoryRowsPerMsg) {
        auto msg = SDK::make_msg<CustomMessage::SleepHistory>(mKernel);
        if (!msg) {
            return;
        }
        const size_t take =
            (n - sent < CustomMessage::kHistoryRowsPerMsg)
                ? (n - sent)
                : CustomMessage::kHistoryRowsPerMsg;

        msg->firstIndex = static_cast<uint8_t>(sent);
        msg->count      = static_cast<uint8_t>(take);
        msg->total      = static_cast<uint8_t>(n);

        for (size_t i = 0; i < take; i++) {
            const SleepLab::NightStore::IndexRow &r = rows[n - 1 - (sent + i)];
            auto &out = msg->rows[i];
            out.startUtcDays  = static_cast<int32_t>(r.startUtc / 86400);
            out.totalSleepMin = static_cast<int16_t>(r.totalSleepMin);
            out.efficiencyPct = static_cast<int16_t>(r.efficiencyPct);
            out.hrMinX10      = static_cast<int16_t>(r.hrMinX10);
            out.worn          = r.worn;
            out.interrupted   = (r.interruption != 0) ? 1 : 0;
        }
        msg.send();
    }
}


// -- The loop ------------------------------------------------------------------------

void Service::run()
{
    const uint32_t start = mKernel.sys.getTimeMs();
    const std::time_t wall = std::time(nullptr);
    const int64_t nowUtc = (wall > 0) ? static_cast<int64_t>(wall) : -1;

    const SleepLab::SettingsStatus cfg =
        SleepLab::loadSettings(mKernel, mSettings);
    LOG_INFO("%s; bed %d-%d hr=%s alarm=%d raw=%d\n",
             SleepLab::toString(cfg),
             static_cast<int>(mSettings.segmenter.windowStartMin),
             static_cast<int>(mSettings.segmenter.windowEndMin),
             SleepLab::toString(mSettings.hrMode),
             mSettings.alarmEnabled, mSettings.rawRecording);

    mSegmenter = Engine::NightSegmenter(mSettings.segmenter);
    mStore.loadBaseline(mBaseline);

    // A night in progress when the app was killed -- almost always the USB
    // cable. Resumed rather than replaced: two half nights in the history are
    // worse than one flagged one, and both halves might be short enough to be
    // discarded entirely.
    const SleepLab::ResumeState resume = mStore.readState(start, nowUtc);
    if (resume.present && mStore.resumeNight(resume)) {
        mFlags         = resume.flags;
        mResumedEpochs = resume.epochs / Engine::kEpochsPerScoringEpoch;
        mNightStartUtc = resume.wallUtc;
        mSegmenter.resumeOpen(static_cast<uint16_t>(mResumedEpochs));
        if (mSettings.rawRecording) {
            mRaw.start(mNightStartUtc, mSettings.rawMaxMb, mSettings.rawMaxMin);
        }
    }

    connectSensors();

    mEpochOpenedAt = start;
    mNextEpochAt   = start + Engine::kEpochMs;
    mHrDutyNextAt  = start + static_cast<uint32_t>(mSettings.hrDutyOnSec) * 1000u;

    while (true) {
        const uint32_t now = mKernel.sys.getTimeMs();

        // Signed difference, so the ~49.7-day uptime wrap cannot turn "due in
        // 400 ms" into "due in 49 days".
        int32_t toEpoch = static_cast<int32_t>(mNextEpochAt - now);
        if (toEpoch <= 0) {
            closeRecordingEpoch(now, now - mEpochOpenedAt);
            mEpochOpenedAt = now;
            // Advance the grid by whole epochs rather than re-basing on `now`,
            // so a late epoch does not push every subsequent one late with it.
            // If the loop overslept by more than a whole epoch the catch-up
            // would spin, so it skips forward -- and the skip is itself the
            // finding, visible as a jump in uptime_ms in the CSV.
            do {
                mNextEpochAt += Engine::kEpochMs;
            } while (static_cast<int32_t>(mNextEpochAt - now) <= 0);
            toEpoch = static_cast<int32_t>(mNextEpochAt - now);
        }

        uint32_t sleepMs = static_cast<uint32_t>(toEpoch);

        const uint32_t toDuty = pumpHrDuty(now);
        if (toDuty > 0 && toDuty < sleepMs) {
            sleepMs = toDuty;
        }

        SDK::MessageBase *msg = nullptr;
        if (mKernel.comm.getMessage(msg, sleepMs)) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP: {
                    // Almost always the USB cable. Close the night rather than
                    // leaving it open: the state file is what lets the relaunch
                    // resume, and the raw capture has a buffer to flush.
                    LOG_INFO("stopping\n");
                    mRaw.stop();
                    disconnectSensors();
                    mKernel.comm.releaseMessage(msg);
                    return;
                }

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    mGuiStarted = true;
                    publishReport();
                    publishHistory();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    // Deliberately no exit, unlike a typical utility service.
                    // Autostart exists so the recording continues whether
                    // anyone is looking or not, and closing the screen at 22:45
                    // is the normal case rather than a shutdown.
                    mGuiStarted = false;
                    break;

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto *event = static_cast<SDK::Message::Sensor::EventData *>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count,
                                                 event->stride);
                    onSensorData(static_cast<uint16_t>(event->handle), batch);
                    break;
                }

                case CustomMessage::SLEEP_REQUEST:
                    publishReport();
                    publishHistory();
                    break;

                default:
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }
    }
}
