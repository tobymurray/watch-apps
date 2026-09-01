#include "Service.hpp"

#include <cstring>
#include <ctime>

#include "SDK/Tools/FirmwareVersion.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/Messages/AccessoryMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Timer/Timer.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {

/// How long to wait for the GUI before concluding nobody opened the app. An
/// Activity service is started alongside its GUI, so if none arrives there is
/// no ride to record and staying resident would only cost battery.
constexpr uint32_t kGuiInitTimeoutSec = 5;

} // namespace

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mGuiSender(kernel)
    , mActivityWriter(mKernel, "Activity")
    , mSensorHr(SDK::Sensor::Type::HEART_RATE_EX, skSamplePeriod, skSampleLatency)
    , mSensorWristMotion(SDK::Sensor::Type::WRIST_MOTION)
    , mTimeTracker(kernel.sys)
{
    mTimeCounter.init();
    mHrCounter.init(skHrMinValid, skHrMaxValid);
}

Service::~Service()
{
    disconnect();
}

void Service::run()
{
    LOG_INFO("Started\n");

    mTimeTracker.init();

    // Read here and not in the constructor: reading can log, and the simulator
    // constructs the app's objects before its logger is usable, so the first
    // log line out of a constructor takes the process down. The SDK documents
    // that trap for SDK::AppConfig specifically.
    loadConfig();
    loadSystemSettings();

    // Recover an activity a previous boot left unfinished (power loss or crash
    // mid-recording) before any new ride can start — the recovery marker names
    // exactly one torn .fit, so a second start would overwrite the evidence.
    if (mActivityWriter.recoverInterrupted()) {
        LOG_INFO("Recovered an interrupted ride\n");
        notifyNewActivity();
    }

    SDK::Timer guiInitTimeout(TIMER_SECONDS(kGuiInitTimeoutSec));
    guiInitTimeout.start();

    std::time_t processedUtc = 0;

    while (true) {
        SDK::MessageBase *msg = nullptr;
        if (mKernel.comm.getMessage(msg, 500)) {
            switch (msg->getType()) {

                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    // Save rather than discard: the wearer did not ask to throw
                    // the ride away, and a finished .fit is recoverable evidence
                    // where a discarded one is nothing.
                    if (mTrackState != Track::State::INACTIVE) {
                        stopTrack(false);
                    }
                    disconnect();
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    onStartGUI();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    onStopGUI();
                    break;

                case CustomMessage::TRACK_START:
                    LOG_DEBUG("TRACK_START\n");
                    // Re-synchronise before the clock starts, never during: a
                    // step correction mid-ride would move seconds that have
                    // already been written into the FIT file.
                    mTimeTracker.init();
                    startTrack(mTimeTracker.getExpectedUTC());
                    break;

                case CustomMessage::TRACK_STOP:
                    LOG_DEBUG("TRACK_STOP\n");
                    stopTrack(static_cast<CustomMessage::TrackStop*>(msg)->discard);
                    break;

                case CustomMessage::TRACK_PAUSE:
                    LOG_DEBUG("TRACK_PAUSE\n");
                    pauseTrack(true);
                    break;

                case CustomMessage::TRACK_RESUME:
                    LOG_DEBUG("TRACK_RESUME\n");
                    pauseTrack(false);
                    break;

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto *event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                    handleSensorsData(event->handle, batch);
                } break;

                // The strap's BLE link state. Remembered as well as forwarded,
                // because the GUI can start after the strap has already
                // connected and the kernel does not repeat itself.
                case SDK::MessageType::EVENT_ACCESSORY_STATUS: {
                    auto *evt = static_cast<SDK::Message::Accessory::EventStatus*>(msg);
                    LOG_INFO("Accessory status: state %u\n", evt->state);
                    mAccessoryState = evt->state;
                    std::strncpy(mAccessoryName, evt->name, sizeof(mAccessoryName) - 1);
                    mGuiSender.accessoryStatus(mAccessoryState, mAccessoryName);
                } break;

                default:
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        if (mGuiStarted) {
            const std::time_t utc = mTimeTracker.getExpectedUTC();
            if (processedUtc != utc) {
                processedUtc = utc;
                if (mTrackState != Track::State::INACTIVE) {
                    mTimeCounter.add(utc);
                    processTrack();
                }
            }
        } else if (guiInitTimeout.expired()) {
            LOG_INFO("No GUI appeared, exiting service\n");
            return;
        }
    }
}

void Service::connectSensors()
{
    if (mIsSensorsConnected) {
        return;
    }
    LOG_DEBUG("Connect to sensors...\n");

    // One sensor, because a stationary bike moves the watch nowhere: there is
    // no position to fix, no speed to integrate and no altitude to filter. An
    // external strap arrives through this same connection — the kernel
    // arbitrates strap against wrist optical and reports which it chose.
    mSensorHr.connect();

    mIsSensorsConnected = true;
}

void Service::disconnect()
{
    if (!mIsSensorsConnected) {
        return;
    }
    LOG_DEBUG("Disconnect from sensors...\n");

    mSensorHr.disconnect();

    // The external HR accessory is released by the kernel when the app stops
    // (AccessoryManager::onAppStopped), so there is nothing to release here.

    mIsSensorsConnected = false;
}

void Service::handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data)
{
    if (mSensorHr.matchesDriver(handle)) {
        SDK::SensorDataParser::HeartRateEx parser(data[0]);
        if (parser.isDataValid()) {
            mHrCounter.add(parser.getBpm());           // arbitrated (kernel's choice)
            mTrackData.hrTrustLevel = parser.getTrustLevel();
            mHrSource      = static_cast<uint8_t>(parser.getSource());
            mHrOpticalBpm  = static_cast<uint8_t>(parser.getOpticalBpm());
            mHrExternalBpm = static_cast<uint8_t>(parser.getExternalBpm());
            LOG_DEBUG("HR %.1f trust %.1f src %u (opt %u ext %u)\n",
                      parser.getBpm(), parser.getTrustLevel(), mHrSource,
                      mHrOpticalBpm, mHrExternalBpm);
        }
    } else if (mSensorWristMotion.matchesDriver(handle)) {
        SDK::SensorDataParser::WristMotion parser(data[0]);
        if (parser.isDataValid()) {
            LOG_DEBUG("Wrist motion detected\n");
            backlightOn();
        }
    }
}

void Service::onStartGUI()
{
    mGuiStarted = true;

    setCapabilities();
    requestAccessoryPrepare();   // start acquiring the strap while the wearer gets on the bike

    mSensorWristMotion.connect();

    // The GUI has no state of its own to fall back on, so hand it the current
    // one immediately. On a fresh start this is INACTIVE and an UNAVAILABLE
    // strap, which is exactly what the pre-ride screen should draw.
    mGuiSender.trackState(mTrackState);
    mGuiSender.accessoryStatus(mAccessoryState, mAccessoryName);
    mGuiSender.rideConfig(static_cast<uint16_t>(mTargetSeconds / skSecondsPerMinute),
                          mEnergyInKilojoules);
}

void Service::onStopGUI()
{
    mGuiStarted = false;

    requestAccessoryRelease();
    mSensorWristMotion.disconnect();
}

void Service::loadSystemSettings()
{
    // The wearer's weight and heart-rate zone thresholds are the watch's own
    // settings, not this app's: they are the same for every activity app on the
    // watch, so asking for them here is right and declaring them as config
    // fields would be a second place to get them wrong.
    auto msg = SDK::make_msg<SDK::Message::RequestSystemSettings>(mKernel);
    if (!msg || !msg.send(100) || !msg.ok()) {
        LOG_WARNING("No system settings; using %0.f kg and no zones\n",
                    static_cast<double>(skDefaultWeightKg));
        return;
    }

    if (msg->weightKg > 0.0f) {
        mWeightKg = msg->weightKg;
    }

    uint8_t count = msg->heartRateCount;
    if (count > skMaxHrThresholds) {
        count = skMaxHrThresholds;
    }
    for (uint8_t i = 0; i < count; ++i) {
        mHrThresholds[i] = msg->heartRateTh[i];
    }
    mHrThresholdCount = count;

    LOG_INFO("System: %.1f kg, %u heart-rate zone thresholds\n",
             static_cast<double>(mWeightKg), static_cast<unsigned>(mHrThresholdCount));
}

uint8_t Service::hrZoneFor(float hr) const
{
    // Zone N is "above the Nth threshold". No thresholds means no zones -- not
    // zone 1 -- because a wearer who has never set them has no zones to be in,
    // and inventing a default set would put a number on the screen that means
    // nothing about them.
    if (mHrThresholdCount == 0 || hr <= 0.0f) {
        return 0;
    }

    uint8_t zone = 0;
    for (uint8_t i = 0; i < mHrThresholdCount; ++i) {
        if (hr > static_cast<float>(mHrThresholds[i])) {
            zone = static_cast<uint8_t>(i + 1);
        }
    }
    // The kernel can report six thresholds; the display and the FIT buckets
    // have five zones, so anything above the fifth is still zone 5.
    return (zone > 5) ? 5 : zone;
}

float Service::zoneMet(uint8_t zone)
{
    // Metabolic equivalents per zone, the same ladder the Squash app uses.
    // A coarse model: it maps a heart rate to an effort, and effort times body
    // mass times time is the estimate. It is not calorimetry and the README
    // says so.
    static constexpr float kMetByZone[5] = {2.5f, 4.5f, 7.0f, 10.0f, 12.5f};
    if (zone < 1 || zone > 5) {
        return 0.0f;
    }
    return kMetByZone[zone - 1];
}

void Service::updateHrDerivedMetrics()
{
    mTrackData.hrZone   = hrZoneFor(mTrackData.hr);
    mTrackData.hasZones = mHrThresholdCount > 0;

    if (mTrackState != Track::State::ACTIVE) {
        return;
    }

    // Basal metabolic rate accrues every active second whatever the heart is
    // doing: a rider coasting is still a body running. Reported separately from
    // the active figure so the two can be told apart afterwards.
    static constexpr float kRestingMet = 1.0f;
    static constexpr float kPerSecond  = 1.0f / 3600.0f;

    const float restingKcal = kRestingMet * mWeightKg * kPerSecond;
    mTrackData.restingCalories += restingKcal;
    mLapRestingCalories        += restingKcal;

    // Below zone 1, or with no zones set at all, the resting rate is the only
    // honest answer -- a MET plucked for "some effort" would be a guess on top
    // of a guess.
    const float met = (mTrackData.hrZone == 0) ? kRestingMet : zoneMet(mTrackData.hrZone);
    const float activeKcal = met * mWeightKg * kPerSecond;
    mTrackData.calories += activeKcal;
    mLapCalories        += activeKcal;

    // Every second lands in exactly one bucket, including the below-zone-1 one,
    // so the buckets sum to the ride's active time and a consumer can check it.
    mTrackData.zoneSeconds[mTrackData.hrZone] += 1;
    mLapZoneSeconds[mTrackData.hrZone]        += 1;
}

void Service::loadConfig()
{
    // Destroy the previous instance before building the next one: SDK::AppConfig
    // is documented as one instance per app, and two alive at once would
    // briefly share the same temporary file.
    mConfig.reset();
    mConfig.reset(new (std::nothrow) SDK::AppConfig(
        mKernel, SpinConfig::kConfigFile,
        SpinConfig::kFields, SpinConfig::kFieldCount));

    if (!mConfig) {
        // Out of memory, which nothing here can fix. The declared defaults are
        // all "off", so the app is exactly what it is without a config file.
        LOG_ERROR("Could not read configuration; using defaults\n");
        mAutoLapSeconds     = 0;
        mTargetSeconds      = 0;
        mKeepScreenLit      = false;
        mEnergyInKilojoules = false;
        return;
    }

    // getInt() clamps to the bounds in the field table, so these cannot be
    // negative and cannot overflow the multiplication below.
    mAutoLapSeconds = mConfig->getInt(SpinConfig::field(SpinConfig::kAutoLapMinutes)) *
                      skSecondsPerMinute;
    mTargetSeconds  = mConfig->getInt(SpinConfig::field(SpinConfig::kTargetMinutes)) *
                      skSecondsPerMinute;
    mKeepScreenLit      = mConfig->getBool(SpinConfig::field(SpinConfig::kKeepScreenLit));
    mEnergyInKilojoules = mConfig->getBool(SpinConfig::field(SpinConfig::kEnergyInKilojoules));

    LOG_INFO("Config: auto lap %us, target %us, keep screen lit %u\n",
             static_cast<unsigned>(mAutoLapSeconds),
             static_cast<unsigned>(mTargetSeconds),
             static_cast<unsigned>(mKeepScreenLit));
}

void Service::startTrack(std::time_t utc)
{
    if (mTrackState != Track::State::INACTIVE) {
        return;
    }

    LOG_INFO("Ride started. UTC: %u\n", static_cast<uint32_t>(utc));

    // Re-read rather than trust what was loaded at boot: the phone can rewrite
    // the file while the app sits on the pre-ride screen, and the ride about to
    // start is the one the wearer just configured.
    loadConfig();
    mGuiSender.rideConfig(static_cast<uint16_t>(mTargetSeconds / skSecondsPerMinute),
                          mEnergyInKilojoules);

    mTimeCounter.reset();
    mHrCounter.reset();
    mTrackData = Track::Data{};
    mLapCalories        = 0.0f;
    mLapRestingCalories = 0.0f;
    for (size_t i = 0; i < skZoneBuckets; ++i) {
        mLapZoneSeconds[i] = 0;
    }
    mSessionNotEmpty = false;

    connectSensors();

    ActivityWriter::AppInfo info{};
    info.timestamp  = utc;
    info.appVersion = SDK::ParseVersion(BUILD_VERSION).u32;
    info.devID      = DEV_ID;
    info.appID      = APP_ID;
    mActivityWriter.start(info);

    if (mKeepScreenLit) {
        backlightHold(true);
    }

    mTrackState = Track::State::ACTIVE;
    mGuiSender.trackState(mTrackState);
}

void Service::processTrack()
{
    mTrackData.totalTime = mTimeCounter.getValueActive();

    mTrackData.hr       = mHrCounter.getCurrent();
    mTrackData.hrSource = mHrSource;
    mTrackData.avgHR    = mHrCounter.getAverage();
    mTrackData.maxHR    = mHrCounter.getMaximum();

    updateHrDerivedMetrics();

    if (mTrackState == Track::State::ACTIVE) {
        mActivityWriter.addRecord(prepareRecordData());
        mSessionNotEmpty = true;

        // The target first: crossing it and closing a lap on the same second is
        // possible, and two alerts in a row is one buzz the wrist reads as
        // both. Checked before the lap so the target keeps the distinct pattern.
        if (mTargetSeconds > 0 && !mTrackData.targetReached &&
            mTrackData.totalTime >= mTargetSeconds) {
            mTrackData.targetReached = true;
            LOG_INFO("Target reached at %u s\n",
                     static_cast<uint32_t>(mTrackData.totalTime));
            notifyTargetReached();
        }

        // Auto lap. Measured on active time, not wall clock, so a ride paused
        // for two minutes does not come back to an immediate lap it did not
        // pedal for.
        if (mAutoLapSeconds > 0 && mTimeCounter.getLapValueActive() >= mAutoLapSeconds) {
            saveLap();
            notifyLapEnd();
        }
    }

    mGuiSender.trackData(mTrackData);
}

void Service::saveLap()
{
    ActivityWriter::LapData fitLap{};

    fitLap.timestamp = mTimeCounter.getCurrent();
    fitLap.timeStart = mTimeCounter.getCurrent() - mTimeCounter.getLapValueTotal();
    fitLap.duration  = mTimeCounter.getLapValueActive();
    fitLap.elapsed   = mTimeCounter.getLapValueTotal();
    fitLap.hrAvg           = mHrCounter.getLapAverage();
    fitLap.hrMax           = mHrCounter.getLapMaximum();
    fitLap.calories        = mLapCalories;
    fitLap.restingCalories = mLapRestingCalories;
    for (size_t i = 0; i < skZoneBuckets; ++i) {
        fitLap.zoneSeconds[i] = mLapZoneSeconds[i];
    }

    mActivityWriter.addLap(fitLap);
    mTrackData.lapNum++;

    LOG_INFO("Lap %u saved: %u / %u s, HR %.0f / %.0f bpm\n",
             static_cast<unsigned>(mTrackData.lapNum),
             static_cast<uint32_t>(mTimeCounter.getLapValueActive()),
             static_cast<uint32_t>(mTimeCounter.getLapValueTotal()),
             mHrCounter.getLapAverage(), mHrCounter.getLapMaximum());

    // Both counters restart their lap window together, or the next lap's
    // average heart rate would be taken over a different span than its time.
    mTimeCounter.resetLap();
    mHrCounter.resetLap();
    mLapCalories        = 0.0f;
    mLapRestingCalories = 0.0f;
    for (size_t i = 0; i < skZoneBuckets; ++i) {
        mLapZoneSeconds[i] = 0;
    }
}

ActivityWriter::RecordData Service::prepareRecordData() const
{
    ActivityWriter::RecordData fitRecord{};

    fitRecord.timestamp = mTimeCounter.getCurrent();

    const bool hasHeartRate = mHrCounter.getCurrent() > skHrMinValid &&
                              mTrackData.hrTrustLevel >= skHrTrustMin &&
                              mTrackData.hrTrustLevel <= skHrTrustMax;

    fitRecord.set(ActivityWriter::RecordData::Field::HEART_RATE, hasHeartRate);
    fitRecord.heartRate = mHrCounter.getCurrent();

    // Tag each record with where the beat came from, and keep both raw per-source
    // readings alongside the arbitrated one. Which source was believed is not
    // recoverable from the arbitrated number afterwards, and for a ride whose
    // whole point is the strap it is the first thing worth checking.
    fitRecord.hrSource      = hasHeartRate ? mHrSource : 0;
    fitRecord.hrOpticalBpm  = mHrOpticalBpm;
    fitRecord.hrExternalBpm = mHrExternalBpm;

    return fitRecord;
}

void Service::pauseTrack(bool pause)
{
    if (mTrackState == Track::State::INACTIVE) {
        return;
    }

    if (pause && mTrackState == Track::State::ACTIVE) {
        mTimeCounter.pause();
        mHrCounter.pause();
        mActivityWriter.pause(mTimeCounter.getCurrent());
        mTrackState = Track::State::PAUSED;
        LOG_INFO("Ride paused. UTC: %u\n", static_cast<uint32_t>(mTimeCounter.getCurrent()));
    } else if (!pause && mTrackState == Track::State::PAUSED) {
        mTimeCounter.resume();
        mHrCounter.resume();
        mActivityWriter.resume(mTimeCounter.getCurrent());
        mTrackState = Track::State::ACTIVE;
        LOG_INFO("Ride resumed. UTC: %u\n", static_cast<uint32_t>(mTimeCounter.getCurrent()));
    } else {
        return;
    }

    mGuiSender.trackState(mTrackState);
}

void Service::stopTrack(bool discard)
{
    if (mTrackState == Track::State::INACTIVE) {
        return;
    }

    bool saved = false;

    if (!discard && mSessionNotEmpty) {
        // A FIT activity ends with the timer stopped. Only emit that event when
        // the ride was still running: a pause has already written one, and two
        // stops in a row is a malformed event sequence.
        if (mTrackState != Track::State::PAUSED) {
            mActivityWriter.pause(mTimeCounter.getCurrent());
        }

        // Close the final lap. With auto-lap off that is the whole ride, which
        // is the point: the FIT profile expects at least one lap per session
        // and many consumers quietly drop a session without one, so a ride with
        // no lap button is still a ride with a lap. With auto-lap on it is
        // whatever is left since the last split, however short.
        saveLap();

        ActivityWriter::TrackData fitTrack{};
        fitTrack.timestamp = mTimeCounter.getCurrent();
        fitTrack.timeStart = mTimeCounter.getCurrent() - mTimeCounter.getValueTotal();
        fitTrack.duration  = mTimeCounter.getValueActive();
        fitTrack.elapsed   = mTimeCounter.getValueTotal();
        fitTrack.hrAvg             = mHrCounter.getAverage();
        fitTrack.hrMax             = mHrCounter.getMaximum();
        fitTrack.calories          = mTrackData.calories;
        fitTrack.metabolicCalories = mTrackData.restingCalories;
        for (size_t i = 0; i < skZoneBuckets; ++i) {
            fitTrack.zoneSeconds[i] = mTrackData.zoneSeconds[i];
        }

        saved = mActivityWriter.stop(fitTrack);
        if (saved) {
            notifyNewActivity();
        } else {
            LOG_ERROR("Ride save failed\n");
            // Deliberately no notification: the .fit is unfinished, so the
            // recovery marker stays for the next boot to finalize.
        }
    } else {
        mActivityWriter.discard();
    }

    mTrackState = Track::State::INACTIVE;

    // Unconditionally, not only when mKeepScreenLit: the setting can have been
    // turned off between the ride starting and ending, and a backlight left on
    // with no auto-off would stay on until the battery ran out.
    backlightHold(false);

    LOG_INFO("Ride stopped. UTC: %u\n", static_cast<uint32_t>(mTimeCounter.getCurrent()));
    LOG_INFO("Time: %u / %u s\n",
             static_cast<uint32_t>(mTimeCounter.getValueActive()),
             static_cast<uint32_t>(mTimeCounter.getValueTotal()));
    LOG_INFO("Heart rate: %.0f / %.0f bpm\n",
             mHrCounter.getAverage(), mHrCounter.getMaximum());
    LOG_INFO("Energy: %.0f kcal active, %.0f kcal resting\n",
             static_cast<double>(mTrackData.calories),
             static_cast<double>(mTrackData.restingCalories));

    mGuiSender.trackState(mTrackState);
    mGuiSender.rideSaved(mTimeCounter.getValueActive(), mHrCounter.getAverage(),
                         mTrackData.calories, saved);

    disconnect();
}

void Service::setCapabilities()
{
    auto msg = SDK::make_msg<SDK::Message::RequestSetCapabilities>(mKernel);
    if (msg) {
        // Notifications stay off for the length of a ride. There is no settings
        // screen to turn them back on, and a phone alert over the ride timer is
        // the one interruption an indoor session does not need.
        msg->enPhoneNotification = false;
        msg->enUsbChargingScreen = false;
        msg->enMusicControl      = true;
        msg.send();
    }
}

void Service::requestAccessoryPrepare()
{
    // Ask for the strap as soon as the GUI is up, not when the ride starts, so
    // the BLE link has the pre-ride screen's worth of time to come up. No-op
    // kernel-side unless external HR is enabled in the watch's own settings.
    auto msg = SDK::make_msg<SDK::Message::Accessory::RequestPrepare>(mKernel);
    if (msg) {
        msg->kinds = SDK::Accessory::Kind::HRM;
        msg.send();
    }
}

void Service::requestAccessoryRelease()
{
    auto msg = SDK::make_msg<SDK::Message::Accessory::RequestRelease>(mKernel);
    if (msg) {
        msg->kinds = 0;   // release everything we acquired
        msg.send();
    }
}

void Service::notifyNewActivity()
{
    auto msg = SDK::make_msg<SDK::Message::CommandAppNewActivity>(mKernel);
    if (msg) {
        msg.send();
    }
}

void Service::notifyLapEnd()
{
    // Two short beeps. Deliberately not the target's three: a lap is a marker
    // you can ignore and the target is the thing you were riding for, so they
    // have to be tellable apart without looking down.
    backlightOn();
    playBuzzerPattern(120, 2);
    playVibroPattern(SDK::Message::RequestVibroPlay::Effect::ALERT_750MS_100);
}

void Service::notifyTargetReached()
{
    backlightOn();
    playBuzzerPattern(200, 3);
    playVibroPattern(SDK::Message::RequestVibroPlay::Effect::ALERT_750MS_100, 2);
}

void Service::backlightOn(uint32_t timeoutMs)
{
    auto bl = SDK::make_msg<SDK::Message::RequestBacklightSet>(mKernel);
    if (bl) {
        bl->brightness       = 100;
        bl->autoOffTimeoutMs = timeoutMs;
        bl.send();
    }
}

void Service::backlightHold(bool on)
{
    auto bl = SDK::make_msg<SDK::Message::RequestBacklightSet>(mKernel);
    if (bl) {
        // autoOffTimeoutMs == 0 disables the auto-off entirely, so holding the
        // light needs one message rather than a timer re-arming itself for the
        // length of the ride. Releasing hands it back to the ordinary
        // wrist-tilt behaviour by turning it off with the usual timeout.
        bl->brightness       = on ? 100 : 0;
        bl->autoOffTimeoutMs = on ? 0 : skBacklightTimeout;
        bl.send();
    }
}

void Service::playBuzzerPattern(uint16_t beepMs, uint8_t count, uint16_t silenceMs)
{
    if (count == 0) {
        return;
    }

    // A series of N beeps needs 2*N-1 notes (beeps plus the silences between
    // them). Cap to what fits: max count = (skMaxNotes + 1) / 2.
    const uint8_t maxCount = (SDK::Message::RequestBuzzerPlay::skMaxNotes + 1u) / 2u;
    if (count > maxCount) {
        count = maxCount;
    }

    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestBuzzerPlay>();
    if (msg) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count; ++i) {
            msg->notes[n].volume = 100;
            msg->notes[n].time   = beepMs;
            ++n;
            if (i < count - 1u) {
                msg->notes[n].volume = 0;
                msg->notes[n].time   = silenceMs;
                ++n;
            }
        }
        msg->notesCount = n;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Service::playVibroPattern(SDK::Message::RequestVibroPlay::Effect effect,
                               uint8_t count, uint16_t silenceMs)
{
    if (count == 0) {
        return;
    }

    const uint8_t maxCount = (SDK::Message::RequestVibroPlay::skMaxNotes + 1u) / 2u;
    if (count > maxCount) {
        count = maxCount;
    }

    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestVibroPlay>();
    if (msg) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count; ++i) {
            // A pause is a note with no effect and a duration; an effect is a
            // note with no duration. Setting both would be a note that is
            // neither.
            msg->notes[n].effect = static_cast<uint8_t>(effect);
            msg->notes[n].pause  = 0;
            ++n;
            if (i < count - 1u) {
                msg->notes[n].effect =
                    static_cast<uint8_t>(SDK::Message::RequestVibroPlay::Effect::NO_EFFECT);
                msg->notes[n].pause = silenceMs;
                ++n;
            }
        }
        msg->notesCount = n;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}
