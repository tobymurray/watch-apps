
#include "Service.hpp"

#include <ctime>
#include <cmath>
#include <memory>
#include <cstring>

#include "Settings.hpp"
#include "ActivitySummary.hpp"
#include "Track.hpp"
#include "SDK/Tools/FirmwareVersion.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/Messages/AccessoryMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Utils/Utils.hpp"
#include "SDK/Timer/Timer.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace {
constexpr std::time_t kOneSecond = 1;

} // namespace

Service::Service(SDK::Kernel &kernel)
        : mKernel(kernel)
        , mGuiStarted(false)
        , mGuiSender(kernel)
        , mSettings{}
        , mSettingsSerializer(mKernel, "settings.json")
        , mSummary{}
        , mActivitySummarySerializer(mKernel, "Activity/summary.json")
        , mActivityWriter(mKernel, "Activity")
        , mImuSink(mKernel, "Imu")
        , mMarkerSink(mKernel, "Imu", "_events")
        , mHrSink(mKernel, "Imu", "_hr")
        , mProfileStore(mKernel, "profile.json")
        , mLog(mKernel)
        , mSensorHr(SDK::Sensor::Type::HEART_RATE_EX, skSamplePeriod, skSampleLatency)
        , mSensorBatteryLevel(SDK::Sensor::Type::BATTERY_LEVEL)
        , mSensorBatteryMetrics(SDK::Sensor::Type::BATTERY_METRICS, skSamplePeriod, skSampleLatency)
        , mSensorWristMotion(SDK::Sensor::Type::WRIST_MOTION)
        , mSensorFusion(SDK::Sensor::Type::FUSION_RAW, 1000.0f / skFusionSampleRateHz, 100)
        , mTimeTracker(kernel.sys)
        , mBatterySoc(kernel.sys)
        , mBatteryVoltage(kernel.sys)
        , mWristTiltDetector()

{
    mTimeCounter.init();
    mDistanceCounter.init();
    mSpeedCounter.init(0.5f, 300.0f);
    mHrCounter.init(20.0f, 300.0f);

    WristTiltDetector::Config config{};
    config.sampleRateHz = skFusionSampleRateHz;
    mWristTiltDetector.setConfig(config);

    mWristTiltDetector.setListener(this);
    mHrThresholds.fill(0);
}

Service::~Service()
{
    disconnect();   // Cleanup resources
}

void Service::run()
{
    LOG_INFO("Started\n");

    // Initialize time
    mTimeTracker.init();

    // Checked rather than trusted: SquashSessionRecord is written twice, in
    // two languages, and nothing else notices when one copy moves.
    const uint32_t abi   = squash_engine_abi_fingerprint();
    const bool     abiOk = abi == kAbiFingerprint;
    if (!abiOk) {
        LOG_ERROR("Engine ABI is %u, expected %u; every session field would be misread\n",
                  static_cast<unsigned>(abi), static_cast<unsigned>(kAbiFingerprint));
    }
    mLog.line("launch", "v%s abi=%u expect=%u ok=%u calibration=%u",
              BUILD_VERSION, static_cast<unsigned>(abi),
              static_cast<unsigned>(kAbiFingerprint), abiOk ? 1u : 0u,
              static_cast<unsigned>(squash_engine_calibration()));

    // A profile that is absent, damaged or from a later schema is a warm-up,
    // never a reason not to start.
    const SquashProfileStore::Load profileLoad = mProfileStore.load();
    LOG_INFO("Profile: load %u, %u sessions\n",
             static_cast<unsigned>(profileLoad),
             static_cast<unsigned>(squash_profile_sessions()));
    mLog.line("profile", "load=%u sessions=%u",
              static_cast<unsigned>(profileLoad),
              static_cast<unsigned>(squash_profile_sessions()));

    // Get settings
    if (!mSettingsSerializer.load(mSettings)) {
        LOG_WARNING("Failed to load settings\n");
    }

    // Get summary
    if (!mActivitySummarySerializer.load(mSummary)) {
        LOG_WARNING("Failed to load activity summary\n");
    }

    // Recover any activity a previous boot left unfinished (power loss /
    // crash mid-recording), before any new track can start.
    if (mActivityWriter.recoverInterrupted()) {
        LOG_INFO("Recovered an interrupted activity\n");
        notifyNewActivity();
    }

    SDK::Timer guiInitTimeout(TIMER_SECONDS(5));
    guiInitTimeout.start();

    std::time_t processedUtc = 0;

    while (true) {
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, 500)) {
            // Command handling
            switch (msg->getType()) {

                // Kernel messages
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    disconnect();   // Cleanup resources
                    if (mTrackState != Track::State::INACTIVE) {
                        stopTrack(false);
                    }
                    // We must release message because this is the last event.
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

                // Custom messages
                case CustomMessage::SETTINGS_SAVE:  {
                    LOG_DEBUG("SETTINGS_SAVE\n");
                    handleEvent(*static_cast<CustomMessage::SettingsSave*>(msg));
                } break;

                case CustomMessage::TRACK_START:  {
                    LOG_DEBUG("TRACK_START\n");
                    handleEvent(*static_cast<CustomMessage::TrackStart*>(msg));
                } break;

                case CustomMessage::TRACK_STOP:  {
                    LOG_DEBUG("TRACK_STOP\n");
                    handleEvent(*static_cast<CustomMessage::TrackStop*>(msg));
                } break;

                case CustomMessage::TRACK_PAUSE:  {
                    LOG_DEBUG("TRACK_PAUSE\n");
                    handleEvent(*static_cast<CustomMessage::TrackPause*>(msg));
                } break;

                case CustomMessage::TRACK_RESUME:  {
                    LOG_DEBUG("TRACK_RESUME\n");
                    handleEvent(*static_cast<CustomMessage::TrackResume*>(msg));
                } break;

                case CustomMessage::MANUAL_LAP:  {
                    LOG_DEBUG("MANUAL_LAP\n");
                    handleEvent(*static_cast<CustomMessage::ManualLap*>(msg));
                } break;

                // Sensors messages
                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                    handleSensorsData(event->handle, batch);
                } break;

                // External accessory link status (WP-S4) -> forward to GUI for
                // the pre-activity HR indicator.
                case SDK::MessageType::EVENT_ACCESSORY_STATUS: {
                    auto* evt = static_cast<SDK::Message::Accessory::EventStatus*>(msg);
                    LOG_INFO("Accessory status: state %u\n", evt->state);
                    mGuiSender.accessoryStatus(evt->state, evt->name);
                } break;

                default:
                    break;
            }
            // Release message after processing
            mKernel.comm.releaseMessage(msg);
        }


        // Periodic process
        if (mGuiStarted) {
            // Update time every second
            std::time_t utc = mTimeTracker.getExpectedUTC();

            if (processedUtc != utc) {
                processedUtc = utc;

                // Send to GUI real "local time" to display
                std::tm tmNow = mTimeTracker.getLocalTime(std::time(nullptr));
                mGuiSender.time(tmNow);

                mGuiSender.battery(static_cast<uint8_t>(mBatterySoc.get()));

                if (mTrackState != Track::State::INACTIVE) {
                    mTimeCounter.add(utc);
                    processTrack();
                }
            }
        } else {
            // Just wait some time to see if GUI starts
            if (guiInitTimeout.expired()) {
                LOG_INFO("No activities, exiting service\n");
                return; // Exit app
            }
        }
    }
}

void Service::connectSensors()
{
    if (!mIsSensorsConnected) {
        LOG_DEBUG("Connect to sensors...\n");

        mSensorBatteryLevel.connect();
        mSensorBatteryMetrics.connect();
        mSensorHr.connect();
        mSensorFusion.connect();

        // External HR strap is pre-acquired at the pre-activity screen via the
        // accessoryKinds capability (WP-S4), not here — so it is already
        // connecting before the workout starts. Its readings arrive through the
        // normal HEART_RATE sensor (the kernel arbitrates external vs optical).

        mIsSensorsConnected = true;
    }
}

void Service::disconnect()
{
    if (mIsSensorsConnected) {
        LOG_DEBUG("Disconnect from sensors...\n");

        mSensorFusion.disconnect();
        mSensorHr.disconnect();
        mSensorBatteryLevel.disconnect();
        mSensorBatteryMetrics.disconnect();

        // External HR accessory is released automatically by the kernel when the
        // app stops (AccessoryManager::onAppStopped), so no explicit release here.

        mIsSensorsConnected = false;
    }
}


void Service::handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data)
{
    if (mSensorHr.matchesDriver(handle)) {
        SDK::SensorDataParser::HeartRateEx parser(data[0]);
        if (parser.isDataValid()) {
            mHrCounter.add(parser.getBpm());           // arbitrated (kernel's choice)
            mTrackData.hrTrustLevel = parser.getTrustLevel();
            mHrSource     = static_cast<uint8_t>(parser.getSource());
            mHrOpticalBpm = static_cast<uint8_t>(parser.getOpticalBpm());
            mHrExternalBpm= static_cast<uint8_t>(parser.getExternalBpm());
            LOG_DEBUG("HR %.1f trust %.1f src %u (opt %u ext %u)\n",
                      parser.getBpm(), parser.getTrustLevel(), mHrSource,
                      mHrOpticalBpm, mHrExternalBpm);

            if (mEngineStarted) {
                squash_engine_on_hr(mLastImuTs - mEngineStartTs,
                                    parser.getBpm(),
                                    static_cast<uint8_t>(parser.getTrustLevel()),
                                    static_cast<uint8_t>(parser.getSource()));
            }

            if (mHrLog.isRecording()) {
                HrCsvLog::Sample hr{};
                hr.bpm         = parser.getBpm();
                hr.opticalBpm  = parser.getOpticalBpm();
                hr.externalBpm = parser.getExternalBpm();
                hr.trust       = static_cast<uint8_t>(parser.getTrustLevel());
                hr.source      = static_cast<HrCsvLog::Source>(parser.getSource());
                // Stamped from the last IMU sample, which is the only clock the
                // recording is on. A heart-rate event carries no sensor
                // timestamp, and the batch latency is 100 ms, so a reading sits
                // within one batch of when it arrived -- three orders below the
                // settling times this file exists to measure.
                if (!mHrLog.onSample(mLastImuTs, hr)) {
                    LOG_INFO("Heart-rate log ended: reason %u, %u samples\n",
                             static_cast<unsigned>(mHrLog.stopReason()),
                             static_cast<unsigned>(mHrLog.sampleCount()));
                }
            }
        }
    } else if (mSensorBatteryLevel.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryLevel parser(data[0]);
        if (parser.isDataValid()) {
            mBatterySoc.set(parser.getCharge());
            LOG_DEBUG("Battery %.1f %%\n", mBatterySoc.get());
        }
    } else if (mSensorBatteryMetrics.matchesDriver(handle)) {
        SDK::SensorDataParser::BatteryMetrics parser(data[0]);
        if (parser.isDataValid()) {
            mBatteryVoltage.set(parser.getVoltage());
            LOG_DEBUG("Battery voltage %.1f V\n", mBatteryVoltage.get());
        }
    } else if (mSensorWristMotion.matchesDriver(handle)) {
        SDK::SensorDataParser::WristMotion parser(data[0]);
        if (parser.isDataValid()) {
            LOG_DEBUG("Wrist Motion detected\n");
            backlightOn();
        }
    } else if (mSensorFusion.matchesDriver(handle)) {
        static constexpr uint16_t kBatchSize = 10u;
        TiltImuSample batch[kBatchSize];
        uint16_t batchLen = 0;

        for (uint16_t i = 0; i < data.size(); i++) {
            SDK::SensorDataParser::FusionRaw parser(data[i]);
            if (parser.isDataValid()) {
                SDK::SensorDataParser::FusionRaw::Data sample{};
                parser.getData(sample);

                // The sensor's own timestamp is the clock: a batch carries ~10
                // samples, so a loop-local "now" would collapse them onto one
                // instant.
                const uint32_t ts = parser.getTimestamp();
                mLastImuTs = ts;

                // The engine sees every sample whether or not anything is being
                // recorded, so the epoch path runs on hardware in an ordinary
                // session rather than only when research mode is on.
                if (mTrackState == Track::State::ACTIVE) {
                    if (!mEngineStarted) {
                        mEngineStarted = true;
                        mEngineStartTs = ts;
                    }
                    const int16_t axes[6] = { sample.accel.x, sample.accel.y, sample.accel.z,
                                              sample.gyro.x,  sample.gyro.y,  sample.gyro.z };
                    squash_engine_on_imu(ts - mEngineStartTs, axes);
                }

                // Research recording gets all six axes in raw LSB, unlike the
                // tilt detector below which only needs ay/gx.
                if (mImuArmed || mImuRecorder.isRecording()) {
                    if (mImuArmed) {
                        mImuArmed = false;
                        if (!mImuRecorder.begin(mImuSink, ts)) {
                            LOG_ERROR("Failed to start research recording\n");
                        }
                        // Same tick as the sample recorder, so a marker row and
                        // a sample row with the same t_ms are the same instant.
                        if (mMarkerSink.isOpen() && !mMarkerLog.begin(mMarkerSink, ts)) {
                            LOG_ERROR("Failed to start the marker log\n");
                        }
                        if (mHrSink.isOpen() && !mHrLog.begin(mHrSink, ts)) {
                            LOG_ERROR("Failed to start the heart-rate log\n");
                        }
                    }
                    if (mImuRecorder.isRecording()) {
                        ImuCsvRecorder::Sample raw{};
                        raw.ax = sample.accel.x;
                        raw.ay = sample.accel.y;
                        raw.az = sample.accel.z;
                        raw.gx = sample.gyro.x;
                        raw.gy = sample.gyro.y;
                        raw.gz = sample.gyro.z;
                        if (!mImuRecorder.onSample(ts, raw)) {
                            LOG_INFO("Research recording ended: reason %u, %u samples\n",
                                     static_cast<unsigned>(mImuRecorder.stopReason()),
                                     static_cast<unsigned>(mImuRecorder.sampleCount()));
                        }
                    }
                }

                batch[batchLen].ayLsb = sample.accel.y;
                batch[batchLen].gxLsb = sample.gyro.x;
                batch[batchLen].timestampMs = parser.getTimestamp();
                //LOG_DEBUG("AY: %d, GX: %d\n", sample.accel.y, sample.gyro.x);
                ++batchLen;

                if (batchLen == kBatchSize) {
                    mWristTiltDetector.addBatch(batch, kBatchSize);
                    batchLen = 0;
                }
            }
        }

        if (batchLen > 0u) {
            mWristTiltDetector.addBatch(batch, batchLen);
        }
    }
}


void Service::onStartGUI()
{
    mGuiStarted = true;

    setCapabilities();
    requestAccessoryPrepare();   // pre-warm external HR while on the pre-activity screen

    mSensorWristMotion.connect();

    sendInitialInfoToGui();
}

void Service::onStopGUI()
{
    mGuiStarted = false;

    requestAccessoryRelease();
    mSensorWristMotion.disconnect();
}

void Service::handleEvent(const CustomMessage::TrackStart& /*event*/)
{
    // We can synchronize the time because we haven't started the track yet,
    // before a new track starts.
    mTimeTracker.init();

    startTrack(mTimeTracker.getExpectedUTC());
}

void Service::handleEvent(const CustomMessage::TrackStop& event)
{
    stopTrack(event.discard);
}

void Service::handleEvent(const CustomMessage::SettingsSave& event)
{
    bool updCaps = mSettings.phoneNotifEn != event.settings.phoneNotifEn;
    mSettings = event.settings;
    mSettingsSerializer.save(event.settings);

    if (updCaps) {
        setCapabilities();
    }
}

void Service::handleEvent(const CustomMessage::TrackPause& /*event*/)
{
    pauseTrack(true);
}

void Service::handleEvent(const CustomMessage::TrackResume& /*event*/)
{
    pauseTrack(false);
}

void Service::handleEvent(const CustomMessage::ManualLap& /*event*/)
{
    // R2 on the track screen. A lap and a research marker are the same gesture
    // -- "note this instant" -- recorded at whatever layers are running, so one
    // press does both rather than competing for a button the watch has not got.
    saveLap();
    mGuiSender.lapEnd(mTrackData.lapNum);

    if (mMarkerLog.isRecording() && !mMarkerLog.mark(mLastImuTs)) {
        LOG_INFO("Marker log ended: reason %u, %u markers\n",
                 static_cast<unsigned>(mMarkerLog.stopReason()),
                 static_cast<unsigned>(mMarkerLog.markerCount()));
    }
}

void Service::setCapabilities()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg) {
        msg->enPhoneNotification = mSettings.phoneNotifEn;
        msg->enUsbChargingScreen = false;
        msg->enMusicControl = true;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Service::requestAccessoryPrepare()
{
    // Pre-acquire an external HR strap at the pre-activity screen. Sent at
    // onStartGUI so the kernel starts scanning/connecting while the user gets
    // ready. No-op kernel-side unless external HR is enabled in Settings.
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::Accessory::RequestPrepare>();
    if (msg) {
        msg->kinds = SDK::Accessory::Kind::HRM;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Service::requestAccessoryRelease()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::Accessory::RequestRelease>();
    if (msg) {
        msg->kinds = 0;   // release everything we acquired
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}


void Service::notifyNewActivity()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::CommandAppNewActivity>();
    if (msg) {
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
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

void Service::playBuzzerPattern(uint16_t beepMs, uint8_t count, uint16_t silenceMs)
{
    if (count == 0) {
        return;
    }

    // A series of N beeps needs 2*N-1 notes (beeps + silences between them).
    // Cap to what fits in skMaxNotes: max count = (skMaxNotes + 1) / 2 = 5.
    const uint8_t maxCount = (SDK::Message::RequestBuzzerPlay::skMaxNotes + 1u) / 2u;
    if (count > maxCount) {
        count = maxCount;
    }

    auto* msg = mKernel.comm.allocateMessage<SDK::Message::RequestBuzzerPlay>();
    if (msg) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count; ++i) {
            msg->notes[n].volume = 100;
            msg->notes[n].time = beepMs;
            ++n;
            if (i < count - 1u) {
                msg->notes[n].volume = 0;
                msg->notes[n].time = silenceMs;
                ++n;
            }
        }
        msg->notesCount = n;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Service::playVibroPattern(SDK::Message::RequestVibroPlay::Effect effect, uint8_t count, uint16_t silenceMs)
{
    if (count == 0) {
        return;
    }

    // A series of N effects needs 2*N-1 notes (effects + silences between them).
    // Cap to what fits in skMaxNotes: max count = (skMaxNotes + 1) / 2 = 4.
    const uint8_t maxCount = (SDK::Message::RequestVibroPlay::skMaxNotes + 1u) / 2u;
    if (count > maxCount) {
        count = maxCount;
    }

    auto* msg = mKernel.comm.allocateMessage<SDK::Message::RequestVibroPlay>();
    if (msg) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count; ++i) {
            msg->notes[n].effect = static_cast<uint8_t>(effect);
            msg->notes[n].pause = 0;
            ++n;
            if (i < count - 1u) {
                msg->notes[n].effect = static_cast<uint8_t>(SDK::Message::RequestVibroPlay::Effect::NO_EFFECT);
                msg->notes[n].pause = silenceMs;
                ++n;
            }
        }
        msg->notesCount = n;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}


ActivityWriter::RecordData Service::prepareRecordData()
{
    ActivityWriter::RecordData fitRecord{};

    fitRecord.timestamp    = mTimeCounter.getCurrent();

    bool hasHeartRate = (mHrCounter.getCurrent() > 20 && mTrackData.hrTrustLevel >= 1 && mTrackData.hrTrustLevel <= 3);
    fitRecord.set(ActivityWriter::RecordData::Field::HEART_RATE, hasHeartRate);
    fitRecord.heartRate    = mHrCounter.getCurrent();
    // Tag each record with where the HR came from (none when no valid HR), and
    // log the raw per-source readings (0 when that source has none this tick).
    fitRecord.hrSource     = hasHeartRate ? mHrSource : 0;
    fitRecord.hrOpticalBpm = mHrOpticalBpm;
    fitRecord.hrExternalBpm= mHrExternalBpm;

    // Both samples must be checked every call; evaluate separately to avoid short-circuit.
    const bool socReady     = mBatterySoc.isDue();
    const bool voltReady    = mBatteryVoltage.isDue();
    const bool batteryReady = socReady && voltReady;
    if (batteryReady) {
        mBatterySoc.consume();
        mBatteryVoltage.consume();
    }
    fitRecord.set(ActivityWriter::RecordData::Field::BATTERY, batteryReady);
    fitRecord.batteryLevel   = static_cast<uint8_t>(mBatterySoc.get());
    fitRecord.batteryVoltage = static_cast<uint16_t>(mBatteryVoltage.get() * 1000);

    return fitRecord;
}

void Service::sendInitialInfoToGui()
{
    // Settings
    uint8_t hrThresholds[CustomMessage::kHrThresholdsCount];
    memcpy(hrThresholds, CustomMessage::kHrThresholdsDefault, sizeof(hrThresholds));

    if (auto msg = SDK::make_msg<SDK::Message::RequestSystemSettings>(mKernel)) {
        if (msg.send(100) && msg.ok()) {
            mIsImperial = msg->imperialUnits;
            mTimeFormat12h = msg->timeFormat;

            if (msg->weightKg > 0.0f) {
                mWeightKg = msg->weightKg;
            }

            if (msg->heartRateCount > CustomMessage::kHrThresholdsCount) {
                msg->heartRateCount = CustomMessage::kHrThresholdsCount;
            }

            if (msg->heartRateCount > 0) {
                // Copy received elements
                uint8_t i = 0;
                for (; i < msg->heartRateCount; ++i) {
                    hrThresholds[i] = msg->heartRateTh[i];
                }

                // Complete the array elements to the full number
                for (; i < CustomMessage::kHrThresholdsCount; ++i) {
                    if (i > 0) {
                        hrThresholds[i] = hrThresholds[i - 1] + 20;
                    } else {
                        hrThresholds[i] = CustomMessage::kHrThresholdsDefault[0];
                    }
                }
            }
        }
    }

    mGuiSender.settingsUpd(mSettings, mIsImperial, mTimeFormat12h, hrThresholds, CustomMessage::kHrThresholdsCount);
    memcpy(mHrThresholds.data(), hrThresholds, sizeof(hrThresholds));
    mHrThresholdCount = CustomMessage::kHrThresholdsCount;
    mGuiSender.summary(&mSummary);
    mGuiSender.battery(static_cast<uint8_t>(mBatterySoc.get()));
}

void Service::loadConfig()
{
    // Destroy the previous instance before building the next one: SDK::AppConfig
    // is documented as one instance per app, and two alive at once would
    // briefly share the same temporary file.
    mConfig.reset();
    mConfig.reset(new (std::nothrow) SDK::AppConfig(
        mKernel, SquashConfig::kConfigFile,
        SquashConfig::kFields, SquashConfig::kFieldCount));

    if (!mConfig) {
        // Out of memory, which nothing here can fix. The declared default is
        // off, and off is the safe direction for a flag whose only effect is
        // to start filling flash.
        LOG_ERROR("Could not read configuration; not recording raw IMU\n");
        mRecordImu = false;
        return;
    }

    mRecordImu = mConfig->getBool(SquashConfig::field(SquashConfig::kRecordImu));

    LOG_INFO("Config: record raw IMU %u\n", static_cast<unsigned>(mRecordImu));
}

void Service::startTrack(std::time_t utc)
{
    // Reset data
    mTrackData = {};

    mTimeCounter.reset();
    mTimeCounter.add(utc);

    mDistanceCounter.reset();
    mSpeedCounter.reset();
    mHrCounter.reset();
    mHrSource = 0;  // don't carry a prior track's HR source/readings into the new session
    mHrOpticalBpm = 0;
    mHrExternalBpm = 0;
    mBatterySoc.reset(skBatteryLogPeriodMs);
    mBatteryVoltage.reset(skBatteryLogPeriodMs);
    mSessionNotEmpty = false;
    mLapNotEmpty = false;

    mSummary = ActivitySummary{};
    mSummary.laps.reserve(10);

    mWristTiltDetector.reset();

    connectSensors();

    // Research recording: open the file now so a storage failure is known and
    // logged at start, but leave the recorder's clock unstarted until the first
    // sample arrives, so t=0 is a real sample and not this call.
    //
    // The values file is re-read here rather than once at boot, so a change
    // made on the phone takes effect on the next session instead of the next
    // restart. Costs one file read per session.
    loadConfig();
    if (mRecordImu) {
        mImuArmed = mImuSink.create(utc);
        if (!mImuArmed) {
            LOG_ERROR("Research recording enabled but the file could not be opened\n");
        } else {
            // The samples are the recording; the sidecars are labels on it and
            // the heart rate beside it. Losing either costs that, not the
            // session, so neither failure disarms the recorder.
            if (!mMarkerSink.create(utc)) {
                LOG_ERROR("Research recording started without a marker log\n");
            }
            if (!mHrSink.create(utc)) {
                LOG_ERROR("Research recording started without a heart-rate log\n");
            }
        }
    }
    mLastImuTs = 0;
    mEngineStarted = false;
    squash_engine_begin();
    mLog.line("start", "utc=%lld record_imu=%u armed=%u",
              static_cast<long long>(utc), mRecordImu ? 1u : 0u, mImuArmed ? 1u : 0u);

    ActivityWriter::AppInfo info{};
    info.timestamp = utc;
    info.appVersion = SDK::ParseVersion(BUILD_VERSION).u32;
    info.devID = DEV_ID;
    info.appID = APP_ID;
    mActivityWriter.start(info);

    mTrackState = Track::State::ACTIVE;

    mGuiSender.trackState(mTrackState);
}

void Service::processTrack()
{
    LOG_DEBUG("Time: %u / %u\n", static_cast<uint32_t>(mTimeCounter.getValueActive()), static_cast<uint32_t>(mTimeCounter.getValueTotal()));

    // Time, s
    mTrackData.totalTime = mTimeCounter.getValueActive();
    mTrackData.lapTime = mTimeCounter.getLapValueActive();

    // HR
    mTrackData.hr = mHrCounter.getCurrent();
    mTrackData.hrSource = mHrSource;  // for the in-activity source-driven HR icon
    mTrackData.avgHR = mHrCounter.getAverage();
    mTrackData.maxHR = mHrCounter.getMaximum();
    mTrackData.avgLapHR = mHrCounter.getLapAverage();
    mTrackData.maxLapHR = mHrCounter.getLapMaximum();
    updateHrDerivedMetrics();

    // Update GUI
    mGuiSender.trackData(mTrackData);


    if (mTrackState == Track::State::ACTIVE) {
        // Save record to the FIT file
        ActivityWriter::RecordData fitRecord = prepareRecordData();
        mActivityWriter.addRecord(fitRecord);

        mSessionNotEmpty = true;    // Session has at least one record
        mLapNotEmpty = true;        // Lap has at least one record
    }

}

void Service::updateHrDerivedMetrics()
{
    mTrackData.hrZone = getHrZone(mTrackData.hr);
    if (mTrackState != Track::State::ACTIVE) {
        return;
    }

    // Basal metabolic rate accrues every active second regardless of HR.
    // Surfaced as session "metabolic_calories" / lap "resting_calories" in the FIT file.
    static constexpr float kRestingMet = 1.0f;
    const float restingKcal = kRestingMet * mWeightKg * (1.0f / 3600.0f);
    mTrackData.restingCaloriesTotal += restingKcal;
    mTrackData.restingCaloriesLap   += restingKcal;

    // Active calories: zone-MET when HR is in a zone; BMR fallback otherwise
    // (HR below zone 1 or no valid HR sample yet).
    const float met = (mTrackData.hrZone == 0) ? kRestingMet : getZoneMet(mTrackData.hrZone);
    const float calories = met * mWeightKg * (1.0f / 3600.0f);
    mTrackData.totalCalories += calories;
    mTrackData.lapCalories   += calories;

    if (mTrackData.hrZone >= 1) {
        mTrackData.zoneTimeSec[mTrackData.hrZone - 1] += kOneSecond;
    }
}

void Service::saveLap()
{
    // Accumulate lap into summary
    mSummary.laps.push_back({
        mTimeCounter.getLapValueActive(),
        mHrCounter.getLapAverage(),
        mHrCounter.getLapMaximum(),
    });

    // Save lap to the FIT file
    ActivityWriter::LapData fitLap{};

    fitLap.timestamp = mTimeCounter.getCurrent();
    fitLap.timeStart = mTimeCounter.getCurrent() - mTimeCounter.getLapValueTotal();
    fitLap.duration  = mTimeCounter.getLapValueActive();
    fitLap.elapsed   = mTimeCounter.getLapValueTotal();

    fitLap.hrAvg            = mHrCounter.getLapAverage();
    fitLap.hrMax            = mHrCounter.getLapMaximum();
    fitLap.calories         = mTrackData.lapCalories;
    fitLap.restingCalories  = mTrackData.restingCaloriesLap;

    mActivityWriter.addLap(fitLap);
    mTrackData.lapNum++;

    LOG_INFO("Lap_%u saved. UTC: %u\n", mTrackData.lapNum, static_cast<uint32_t>(mTimeCounter.getCurrent()));
    LOG_INFO("Time: %u / %u s\n", static_cast<uint32_t>(mTimeCounter.getLapValueActive()), static_cast<uint32_t>(mTimeCounter.getLapValueTotal()));
    LOG_INFO("Heart rate: %.0f / %.0f bpm\n", mHrCounter.getLapAverage(), mHrCounter.getLapMaximum());
    LOG_INFO("Lap calories: %.1f kcal\n", mTrackData.lapCalories);

    // Reset lap counters
    mTimeCounter.resetLap();
    mDistanceCounter.resetLap();
    mSpeedCounter.resetLap();
    mHrCounter.resetLap();

    // Clear track data
    mTrackData.lapTime = 0;
    mTrackData.avgLapHR = 0.0f;
    mTrackData.maxLapHR = 0.0f;
    mTrackData.lapCalories = 0.0f;
    mTrackData.restingCaloriesLap = 0.0f;

    mLapNotEmpty = false;
}

void Service::buildPartialSummary()
{
    mSummary.utc       = mTimeCounter.getCurrent();
    mSummary.time      = mTimeCounter.getValueActive();
    mSummary.hrMax     = mHrCounter.getMaximum();
    mSummary.hrAvg     = mHrCounter.getAverage();
    mSummary.calories         = mTrackData.totalCalories;
    mSummary.restingCalories  = mTrackData.restingCaloriesTotal;
    mSummary.activeCalories   = std::fmax(0.0f,
        mTrackData.totalCalories - mTrackData.restingCaloriesTotal);

    for (size_t i = 0; i < std::size(mSummary.zoneTimeSec); ++i) {
        mSummary.zoneTimeSec[i] = mTrackData.zoneTimeSec[i];
    }
}

void Service::stopTrack(bool discard)
{
    if (mTrackState == Track::State::INACTIVE) {
        return;
    }

    if (!discard && mSessionNotEmpty) {

        if (mTrackState != Track::State::PAUSED) {
            mActivityWriter.pause(mTimeCounter.getCurrent());
        }

        if (mLapNotEmpty) {
            saveLap();
        }

        mBatterySoc.request();
        mBatteryVoltage.request();
        ActivityWriter::RecordData fitRecord = prepareRecordData();
        mActivityWriter.addRecord(fitRecord);

        buildPartialSummary();

        // Save summary
        if (!mActivitySummarySerializer.save(mSummary)) {
            LOG_ERROR("Can't save activity summary\n");
        }
        mGuiSender.summary(&mSummary);

        // Save FIT file
        ActivityWriter::TrackData fitTrack{};

        fitTrack.timestamp = mTimeCounter.getCurrent();
        fitTrack.timeStart = mTimeCounter.getCurrent() - mTimeCounter.getValueTotal();
        fitTrack.duration  = mTimeCounter.getValueActive();
        fitTrack.elapsed   = mTimeCounter.getValueTotal();

        fitTrack.hrAvg              = mHrCounter.getAverage();
        fitTrack.hrMax              = mHrCounter.getMaximum();
        fitTrack.calories           = mTrackData.totalCalories;
        fitTrack.metabolicCalories  = mTrackData.restingCaloriesTotal;

        if (mActivityWriter.stop(fitTrack)) {
            notifyNewActivity();
        } else {
            LOG_ERROR("activity save failed\n");
            // Do NOT notify: the .fit is left unfinished, so the crash-recovery
            // marker (if any) stays for the next boot to finalize.
        }
    } else {
        mActivityWriter.discard();
    }

    // Closed out on both paths, including discard: the CSV is research data,
    // not part of the activity, so a discarded session's samples are still
    // worth keeping. end() is safe when nothing was ever started.
    SquashLog::Session logRow{};
    if (mImuArmed || mImuRecorder.isRecording() || mImuSink.isOpen()) {
        const bool intact = mImuRecorder.end();
        const uint32_t samples = mImuRecorder.sampleCount();
        const uint32_t bytes   = mImuRecorder.bytesWritten();
        const uint16_t markers = mMarkerLog.markerCount();
        const uint16_t beats   = mHrLog.sampleCount();
        mImuSink.close();
        mImuArmed = false;
        mMarkerLog.end();
        mMarkerSink.close();
        mHrLog.end();
        mHrSink.close();

        logRow.imuSamples      = samples;
        logRow.imuBytes         = bytes;
        logRow.markers          = markers;
        logRow.hrRows           = beats;
        logRow.imuStop          = static_cast<uint8_t>(mImuRecorder.stopReason());
        logRow.recordingIntact  = intact ? 1u : 0u;
        mLog.line("imu", "intact=%u stop=%u samples=%u bytes=%u markers=%u beats=%u",
                  intact ? 1u : 0u, static_cast<unsigned>(mImuRecorder.stopReason()),
                  static_cast<unsigned>(samples), static_cast<unsigned>(bytes),
                  static_cast<unsigned>(markers), static_cast<unsigned>(beats));

        if (intact) {
            LOG_INFO("Research recording saved: %u samples, %u bytes, %u markers, %u beats\n",
                     static_cast<unsigned>(samples), static_cast<unsigned>(bytes),
                     static_cast<unsigned>(markers), static_cast<unsigned>(beats));
        } else {
            LOG_ERROR("Research recording is torn and should not be trusted\n");
        }
    }

    // The engine is closed out on both paths so its static state does not carry
    // into the next session, but the profile only hears about a session the
    // wearer kept: discarding one is them saying it did not happen, and a
    // baseline is a record of sessions that did.
    SquashSessionRecord record{};
    squash_engine_finish(static_cast<uint32_t>(mTimeCounter.getCurrent()),
                         static_cast<uint32_t>(mTimeCounter.getValueActive()),
                         &record);
    mEngineStarted = false;

    if (!discard && mSessionNotEmpty) {
        squash_profile_record(&record);
        const bool saved = mProfileStore.save();
        if (!saved) {
            LOG_WARNING("Profile not updated; the previous one is kept\n");
        }
        LOG_INFO("Session: %us active, HR mean %.1f max %.1f over %us, segmented %u\n",
                 static_cast<unsigned>(record.activeS), record.hrMean, record.hrMax,
                 static_cast<unsigned>(record.hrCoveredS),
                 static_cast<unsigned>(record.segmented));

        logRow.record          = record;
        logRow.profileSessions = squash_profile_sessions();
        logRow.calibration     = squash_engine_calibration();
        logRow.profileSaved    = saved ? 1u : 0u;
        mLog.session(logRow);
    } else {
        mLog.line("session", "discarded=%u empty=%u active=%u",
                  discard ? 1u : 0u, mSessionNotEmpty ? 0u : 1u,
                  static_cast<unsigned>(record.activeS));
    }

    mTrackState = Track::State::INACTIVE;
    LOG_INFO("Track stopped. UTC: %u\n", static_cast<uint32_t>(mTimeCounter.getCurrent()));
    LOG_INFO("Time: %u / %u s\n", static_cast<uint32_t>(mTimeCounter.getValueActive()), static_cast<uint32_t>(mTimeCounter.getValueTotal()));
    LOG_INFO("Heart rate: %.0f / %.0f bpm\n", mHrCounter.getAverage(), mHrCounter.getMaximum());
    LOG_INFO("Calories: %.1f kcal\n", mTrackData.totalCalories);

    mGuiSender.trackState(mTrackState);

    disconnect();
}

void Service::pauseTrack(bool pause)
{
    if (mTrackState == Track::State::INACTIVE) {
        return;
    }

    if (pause && mTrackState == Track::State::ACTIVE) {
        mTimeCounter.pause();
        mDistanceCounter.pause();
        mSpeedCounter.pause();
        mHrCounter.pause();

        mActivityWriter.pause(mTimeCounter.getCurrent());

        mTrackState = Track::State::PAUSED;
        LOG_INFO("Track paused. UTC: %u\n", static_cast<uint32_t>(mTimeCounter.getCurrent()));
        mGuiSender.trackState(mTrackState);

        buildPartialSummary();
        mGuiSender.summary(&mSummary);
    } else if (!pause && mTrackState == Track::State::PAUSED) {
        mTimeCounter.resume();
        mDistanceCounter.resume();
        mSpeedCounter.resume();
        mHrCounter.resume();

        mActivityWriter.resume(mTimeCounter.getCurrent());

        mTrackState = Track::State::ACTIVE;
        LOG_INFO("Track resumed. UTC: %u\n", static_cast<uint32_t>(mTimeCounter.getCurrent()));
        mGuiSender.trackState(mTrackState);
    }
}

uint8_t Service::getHrZone(float hr) const
{
    if (mHrThresholdCount == 0 || hr <= 0.0f) {
        return 0;
    }

    uint8_t zone = 0;
    const uint8_t thresholdCount = (mHrThresholdCount > mHrThresholds.size()) ? mHrThresholds.size() : mHrThresholdCount;
    for (uint8_t i = 0; i < thresholdCount; ++i) {
        if (hr > mHrThresholds[i]) {
            zone = i + 1;
        }
    }
    return (zone > 5) ? 5 : zone;
}

float Service::getZoneMet(uint8_t zone) const
{
    static constexpr float kMetByZone[5] = {2.5f, 4.5f, 7.0f, 10.0f, 12.5f};
    if (zone < 1 || zone > 5) {
        return 0.0f;
    }
    return kMetByZone[zone - 1];
}

void Service::onWristTilt(uint32_t timestampMs)
{
    LOG_DEBUG("Wrist Tilt detected\n");
    backlightOn();
}
