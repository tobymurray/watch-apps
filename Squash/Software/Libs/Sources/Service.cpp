#include "Service.hpp"

#include <ctime>

#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/Messages/AccessoryMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Timer/Timer.hpp"

#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

/// Called by the Rust crate's panic handler. Without it a panic would stop the
/// Service silently and the recording would be lost with no reason on disk.
extern "C" void squash_engine_host_panic(const uint8_t *msg, uint32_t len)
{
    LOG_ERROR("engine panic: %.*s\n", static_cast<int>(len),
              reinterpret_cast<const char *>(msg));
}

Service::Service(SDK::Kernel &kernel)
        : mKernel(kernel)
        , mGuiStarted(false)
        , mGuiSender(kernel)
        , mDiag(kernel)
        , mImuSink(mKernel, "Imu")
        , mMarkerSink(mKernel, "Imu", "_events")
        , mHrSink(mKernel, "Imu", "_hr")
        , mSensorHr(SDK::Sensor::Type::HEART_RATE, skSamplePeriod, skSampleLatency)
        , mSensorHrEx(SDK::Sensor::Type::HEART_RATE_EX, skSamplePeriod, skSampleLatency)
        , mSensorWristMotion(SDK::Sensor::Type::WRIST_MOTION)
        , mSensorFusion(SDK::Sensor::Type::FUSION_RAW, 1000.0f / skFusionSampleRateHz, 100)
        , mTimeTracker(kernel.sys)
        , mWristTiltDetector()
{
    WristTiltDetector::Config config{};
    config.sampleRateHz = skFusionSampleRateHz;
    mWristTiltDetector.setConfig(config);
    mWristTiltDetector.setListener(this);
}

Service::~Service()
{
    disconnect();
}

void Service::run()
{
    LOG_INFO("Started\n");
    // Which build is running, on the volume. INSTALLING.md reads this line to
    // tell a new install from an old one that kept booting, which is the
    // failure that document exists for and which no other artefact reveals.
    mDiag.line("launch", "version %s", BUILD_VERSION);

    // A layout disagreement between this binary and the Rust archive would
    // corrupt every feature silently, so it is a refusal rather than a warning.
    const uint32_t theirs = squash_engine_abi_fingerprint();
    const uint32_t ours   = squash_engine_abi::fingerprint();
    if (theirs != ours) {
        LOG_ERROR("engine ABI mismatch: crate %08lx, service %08lx\n",
                  static_cast<unsigned long>(theirs), static_cast<unsigned long>(ours));
        mDiag.line("abi", "mismatch crate %08lx service %08lx -- refused to start",
                   static_cast<unsigned long>(theirs), static_cast<unsigned long>(ours));
        return;
    }
    mDiag.line("abi", "engine %08lx ok", static_cast<unsigned long>(ours));

    mTimeTracker.init();

    // Read once here as well as at every session start, so the pre-session
    // screen can say whether starting will actually record.
    loadConfig();

    SDK::Timer guiInitTimeout(TIMER_SECONDS(skGuiInitTimeoutS));
    guiInitTimeout.start();

    std::time_t processedUtc = 0;

    while (true) {
        SDK::MessageBase *msg;
        if (mKernel.comm.getMessage(msg, 500)) {
            switch (msg->getType()) {

                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("Force exit from the application\n");
                    disconnect();
                    if (mState != Session::State::INACTIVE) {
                        // Not a discard: the wearer did not ask to throw this
                        // away, the system took the app.
                        stopSession(false);
                    }
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

                case CustomMessage::SESSION_START:
                    handleEvent(*static_cast<CustomMessage::SessionStart*>(msg));
                    break;

                case CustomMessage::SESSION_STOP:
                    handleEvent(*static_cast<CustomMessage::SessionStop*>(msg));
                    break;

                case CustomMessage::SESSION_PAUSE:
                    handleEvent(*static_cast<CustomMessage::SessionPause*>(msg));
                    break;

                case CustomMessage::SESSION_RESUME:
                    handleEvent(*static_cast<CustomMessage::SessionResume*>(msg));
                    break;

                case CustomMessage::MARK:
                    handleEvent(*static_cast<CustomMessage::Mark*>(msg));
                    break;

                case CustomMessage::SET_LABEL:
                    handleEvent(*static_cast<CustomMessage::SetLabel*>(msg));
                    break;

                case SDK::MessageType::EVENT_SENSOR_LAYER_DATA: {
                    auto event = static_cast<SDK::Message::Sensor::EventData*>(msg);
                    SDK::Sensor::DataBatch batch(event->data, event->count, event->stride);
                    handleSensorsData(event->handle, batch);
                } break;

                case SDK::MessageType::EVENT_ACCESSORY_STATUS: {
                    auto* evt = static_cast<SDK::Message::Accessory::EventStatus*>(msg);
                    LOG_INFO("Accessory status: state %u\n", evt->state);
                    mGuiSender.accessoryStatus(evt->state);
                } break;

                default:
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        if (mGuiStarted) {
            // Keyed on the UTC second rather than the number of loops: a tick
            // the Service was too busy to serve is a real second that went past,
            // and the recorded-seconds figure is the one the caps are read
            // against.
            const std::time_t utc = mTimeTracker.getExpectedUTC();
            if (processedUtc != utc) {
                processedUtc = utc;

                if (mState == Session::State::ACTIVE) {
                    ++mActiveS;
                    if (mLabel != Session::Label::NONE) {
                        ++mLabelS;
                    }
                }
                sendStatus();
            }
        } else if (guiInitTimeout.expired()) {
            LOG_INFO("No GUI, exiting service\n");
            return;
        }
    }
}

void Service::connectSensors()
{
    if (!mIsSensorsConnected) {
        LOG_DEBUG("Connect to sensors...\n");
        mSensorHr.connect();
        mSensorHrEx.connect();
        mSensorWristMotion.connect();
        mSensorFusion.connect();
        mIsSensorsConnected = true;
    }
}

void Service::disconnect()
{
    if (mIsSensorsConnected) {
        LOG_DEBUG("Disconnect sensors...\n");
        mSensorFusion.disconnect();
        mSensorWristMotion.disconnect();
        mSensorHrEx.disconnect();
        mSensorHr.disconnect();

        // The external HR accessory is released by the kernel when the app
        // stops (AccessoryManager::onAppStopped), so nothing to do here.

        mIsSensorsConnected = false;
    }
}

void Service::onStartGUI()
{
    mGuiStarted = true;
    setCapabilities();
    requestAccessoryPrepare();
    connectSensors();
    // The pre-session screen needs the armed state before the first tick.
    loadConfig();
    mGuiSender.state(mState, false);
    sendStatus();
}

void Service::onStopGUI()
{
    mGuiStarted = false;
    if (mState != Session::State::INACTIVE) {
        stopSession(false);
    }
    disconnect();
}

void Service::handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data)
{
    if (mSensorHr.matchesDriver(handle)) {
        // BPM comes from HEART_RATE, never from HEART_RATE_EX. The SDK is
        // explicit that EX is opt-in provenance and that display must not be
        // gated on it (Docs/ExternalSensors.md), and the two disagree about
        // what a valid frame is: EX wants seven fields where this wants two, so
        // reading BPM from EX shows nothing at all whenever EX is not producing.
        SDK::SensorDataParser::HeartRate parser(data[0]);
        if (parser.isDataValid()) {
            mHrBpm   = parser.getBpm();
            mHrTrust = static_cast<uint8_t>(parser.getTrustLevel());
            mHrUtc   = mTimeTracker.getExpectedUTC();

            // One row per arbitrated reading, carrying whatever provenance EX
            // last said. A row is written even when EX has said nothing, with
            // the source and per-source columns zero, because a reading with
            // unknown provenance is still a reading.
            if (mHrLog.isRecording()) {
                HrCsvLog::Sample hr{};
                hr.bpm         = mHrBpm;
                hr.opticalBpm  = mHrOptical;
                hr.externalBpm = mHrExternal;
                hr.trust       = mHrTrust;
                hr.source      = static_cast<HrCsvLog::Source>(mHrSource);
                // The IMU tick, so the three files share one origin and a
                // labelled transition lines up with the readings around it.
                if (!mHrLog.onSample(mLastImuTs, hr)) {
                    LOG_INFO("Heart-rate log ended: reason %u, %u samples\n",
                             static_cast<unsigned>(mHrLog.stopReason()),
                             static_cast<unsigned>(mHrLog.sampleCount()));
                }
            }
        }
    } else if (mSensorHrEx.matchesDriver(handle)) {
        // Provenance only. Nothing here reaches the screen's bpm.
        SDK::SensorDataParser::HeartRateEx parser(data[0]);
        if (parser.isDataValid()) {
            mHrOptical  = parser.getOpticalBpm();
            mHrExternal = parser.getExternalBpm();
            mHrSource   = static_cast<uint8_t>(parser.getSource());
        }
    } else if (mSensorWristMotion.matchesDriver(handle)) {
        SDK::SensorDataParser::WristMotion parser(data[0]);
        if (parser.isDataValid()) {
            backlightOn();
        }
    } else if (mSensorFusion.matchesDriver(handle)) {
        static constexpr uint16_t kBatchSize = 10u;
        TiltImuSample batch[kBatchSize];
        uint16_t batchLen = 0;

        for (uint16_t i = 0; i < data.size(); i++) {
            SDK::SensorDataParser::FusionRaw parser(data[i]);
            if (!parser.isDataValid()) {
                continue;
            }
            SDK::SensorDataParser::FusionRaw::Data sample{};
            parser.getData(sample);
            const uint32_t ts = parser.getTimestamp();

            onFusionSample(ts, sample.accel.x, sample.accel.y, sample.accel.z,
                           sample.gyro.x, sample.gyro.y, sample.gyro.z);

            batch[batchLen].ayLsb = sample.accel.y;
            batch[batchLen].gxLsb = sample.gyro.x;
            batch[batchLen].timestampMs = ts;
            ++batchLen;

            if (batchLen == kBatchSize) {
                mWristTiltDetector.addBatch(batch, kBatchSize);
                batchLen = 0;
            }
        }

        if (batchLen > 0u) {
            mWristTiltDetector.addBatch(batch, batchLen);
        }
    }
}

void Service::onFusionSample(uint32_t ts, int16_t ax, int16_t ay, int16_t az,
                             int16_t gx, int16_t gy, int16_t gz)
{
    if (!mImuArmed && !mImuRecorder.isRecording()) {
        return;
    }

    if (mImuArmed) {
        mImuArmed = false;
        squash_engine_reset();
        if (!mImuRecorder.begin(mImuSink, ts, mLimits)) {
            LOG_ERROR("Failed to start research recording\n");
        }
        // Same tick as the sample recorder, so a marker row and a sample row
        // with the same t_ms are the same instant.
        if (mMarkerSink.isOpen() && !mMarkerLog.begin(mMarkerSink, ts)) {
            LOG_ERROR("Failed to start the marker log\n");
        }
        if (mHrSink.isOpen() && !mHrLog.begin(mHrSink, ts)) {
            LOG_ERROR("Failed to start the heart-rate log\n");
        }
        // Whatever the wearer had selected before starting is true from the
        // first sample, so it is written rather than inferred from its absence.
        if (mLabel != Session::Label::NONE) {
            writeMarker(mLabel);
        }
    }

    mLastImuTs = ts;

    if (!mImuRecorder.isRecording()) {
        return;
    }

    // The screen's features and the ones `phase-a` prints off this file are the
    // same numbers because they are the same code; see SquashEngine.hpp.
    if (squash_engine_push(ts, ax, ay, az, gx, gy, gz)) {
        squash_engine_last_epoch(&mEpoch);
    }

    ImuCsvRecorder::Sample raw{};
    raw.ax = ax;
    raw.ay = ay;
    raw.az = az;
    raw.gx = gx;
    raw.gy = gy;
    raw.gz = gz;
    if (!mImuRecorder.onSample(ts, raw)) {
        LOG_INFO("Research recording ended: reason %u, %u samples\n",
                 static_cast<unsigned>(mImuRecorder.stopReason()),
                 static_cast<unsigned>(mImuRecorder.sampleCount()));
        // A cap is not a failure, but it is the moment the recording and the
        // session stop being the same length, so the wearer is told.
        buzz(2);
    }
}

void Service::loadConfig()
{
    mConfig = std::make_unique<SDK::AppConfig>(mKernel, SquashConfig::kConfigFile,
                                               SquashConfig::kFields,
                                               SquashConfig::kFieldCount);
    if (!mConfig) {
        mRecordImu = false;
        return;
    }

    mRecordImu = mConfig->getBool(SquashConfig::field(SquashConfig::kRecordImu));

    const int32_t minutes = mConfig->getInt(SquashConfig::field(SquashConfig::kMaxMinutes));
    const int32_t megabytes = mConfig->getInt(SquashConfig::field(SquashConfig::kMaxMegabytes));
    mLimits.maxDurationMs = static_cast<uint32_t>(minutes) * 60u * 1000u;
    mLimits.maxBytes      = static_cast<uint32_t>(megabytes) * 1024u * 1024u;

    LOG_INFO("Config: record %u, caps %ld min / %ld MB\n",
             static_cast<unsigned>(mRecordImu),
             static_cast<long>(minutes), static_cast<long>(megabytes));
    mDiag.line("config", "record %u caps %ld min %ld MB",
               static_cast<unsigned>(mRecordImu),
               static_cast<long>(minutes), static_cast<long>(megabytes));
}

void Service::startSession(std::time_t utc)
{
    loadConfig();

    mActiveS = 0;
    mLabelS  = 0;
    mHrUtc   = 0;
    mEpoch   = squash_epoch{};
    mLastImuTs = 0;

    if (mRecordImu) {
        mImuArmed = mImuSink.create(utc);
        if (!mImuArmed) {
            LOG_ERROR("Failed to open the research recording\n");
        } else {
            // The samples are the recording; the sidecars are what is known
            // about it. Losing one is bad -- the labels are the point, and the
            // heart rate has nowhere else to go now there is no .fit -- but
            // neither is a reason to refuse to record.
            if (!mMarkerSink.create(utc)) {
                LOG_ERROR("Research recording started without a marker log\n");
            }
            if (!mHrSink.create(utc)) {
                LOG_ERROR("Research recording started without a heart-rate log\n");
            }
        }
    }

    mState = Session::State::ACTIVE;
    mGuiSender.state(mState, false);
    backlightOn();
    buzz(1);
}

void Service::stopSession(bool discard)
{
    bool intact = true;

    if (mImuArmed || mImuRecorder.isRecording() || mImuSink.isOpen()) {
        // Close the accumulator first so the last partial second is counted.
        if (squash_engine_flush()) {
            squash_engine_last_epoch(&mEpoch);
        }
        intact = mImuRecorder.end();
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

        mDiag.line("imu", "%s intact=%u stop=%u samples=%lu bytes=%lu markers=%u beats=%u",
                   discard ? "discarded" : "saved",
                   static_cast<unsigned>(intact),
                   static_cast<unsigned>(mImuRecorder.stopReason()),
                   static_cast<unsigned long>(samples),
                   static_cast<unsigned long>(bytes),
                   static_cast<unsigned>(markers),
                   static_cast<unsigned>(beats));
        LOG_INFO("Recording %s: intact=%u stop=%u samples=%lu bytes=%lu markers=%u beats=%u\n",
                 discard ? "kept on discard" : "saved",
                 static_cast<unsigned>(intact),
                 static_cast<unsigned>(mImuRecorder.stopReason()),
                 static_cast<unsigned long>(samples),
                 static_cast<unsigned long>(bytes),
                 static_cast<unsigned>(markers),
                 static_cast<unsigned>(beats));
    }

    mState = Session::State::INACTIVE;
    mGuiSender.state(mState, intact);
    sendStatus();
    backlightOn();
}

void Service::pauseSession(bool pause)
{
    if (pause && mState != Session::State::ACTIVE) {
        return;
    }
    if (!pause && mState != Session::State::PAUSED) {
        return;
    }

    mState = pause ? Session::State::PAUSED : Session::State::ACTIVE;
    mGuiSender.state(mState, false);
    backlightOn();
}

bool Service::writeMarker(Session::Label label)
{
    if (!mMarkerLog.isRecording()) {
        return false;
    }
    if (!mMarkerLog.mark(mLastImuTs, static_cast<ImuMarkerLog::Kind>(label))) {
        LOG_INFO("Marker log ended: reason %u, %u markers\n",
                 static_cast<unsigned>(mMarkerLog.stopReason()),
                 static_cast<unsigned>(mMarkerLog.markerCount()));
        return false;
    }
    return true;
}

void Service::sendStatus()
{
    Session::Status s{};
    s.elapsedS  = mActiveS;
    s.recS      = mImuRecorder.recordedMs() / 1000u;
    s.recCapS   = mLimits.maxDurationMs / 1000u;
    s.recKb     = mImuRecorder.bytesWritten() / 1024u;
    s.recCapKb  = mLimits.maxBytes / 1024u;
    s.gyroMag   = mEpoch.gyro_mag;
    s.accelVarK = mEpoch.accel_var_k;
    s.markers   = mMarkerLog.markerCount();
    // Aged here rather than in the renderer, which owns no clock: past the
    // window the reading is sent as absent, which the screen already draws as
    // "-- BPM". No new screen state, and no stale number can reach the glass.
    const bool hrCurrent = Freshness::isCurrent(mHrUtc, mTimeTracker.getExpectedUTC(),
                                                Freshness::kHeartRateStaleAfterS);
    s.hrBpm     = (hrCurrent && mHrBpm > 0.0f) ? static_cast<uint16_t>(mHrBpm + 0.5f) : 0u;
    s.labelS    = (mLabelS > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(mLabelS);
    s.satAccelPct = mEpoch.sat_accel_pct;
    s.satGyroPct  = mEpoch.sat_gyro_pct;
    s.hrTrust   = hrCurrent ? mHrTrust : 0u;
    s.hrSource  = hrCurrent ? mHrSource : 0u;
    s.recording = mImuRecorder.isRecording() ? 1u : 0u;
    s.recStop   = static_cast<uint8_t>(mImuRecorder.stopReason());
    s.armed     = mRecordImu ? 1u : 0u;
    s.label     = static_cast<uint8_t>(mLabel);
    mGuiSender.status(s);
}

void Service::handleEvent(const CustomMessage::SessionStart& /*event*/)
{
    if (mState != Session::State::INACTIVE) {
        return;
    }
    startSession(std::time(nullptr));
}

void Service::handleEvent(const CustomMessage::SessionStop& event)
{
    if (mState == Session::State::INACTIVE) {
        return;
    }
    stopSession(event.discard != 0u);
}

void Service::handleEvent(const CustomMessage::SessionPause& /*event*/)
{
    pauseSession(true);
}

void Service::handleEvent(const CustomMessage::SessionResume& /*event*/)
{
    pauseSession(false);
}

void Service::handleEvent(const CustomMessage::Mark& /*event*/)
{
    if (writeMarker(Session::Label::NONE)) {
        // The press has no other feedback: the counter on the screen is a
        // second behind, so the buzz is what says it registered.
        buzz(1);
    }
}

void Service::handleEvent(const CustomMessage::SetLabel& event)
{
    // A value this build does not know must not reach the file; the file is
    // what a later analysis trusts.
    if (event.label >= static_cast<uint8_t>(Session::Label::COUNT)) {
        LOG_WARNING("Ignored an unknown label %u\n", static_cast<unsigned>(event.label));
        return;
    }

    const auto label = static_cast<Session::Label>(event.label);
    if (label == mLabel) {
        return;
    }

    mLabel  = label;
    mLabelS = 0;
    if (writeMarker(mLabel)) {
        buzz(1);
    }
    sendStatus();
}

void Service::setCapabilities()
{
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::RequestSetCapabilities>();
    if (msg) {
        msg->enPhoneNotification = false;
        msg->enUsbChargingScreen = false;
        // Off because enMusicControl makes the system eat HOLD_1S, and this app
        // needs every press of its four buttons.
        msg->enMusicControl = false;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Service::requestAccessoryPrepare()
{
    // Pre-acquire an external HR strap while the wearer is still on the
    // pre-session screen. A no-op kernel-side unless external HR is enabled in
    // the watch's own settings.
    auto *msg = mKernel.comm.allocateMessage<SDK::Message::Accessory::RequestPrepare>();
    if (msg) {
        msg->kinds = SDK::Accessory::Kind::HRM;
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

void Service::buzz(uint8_t count)
{
    if (count == 0) {
        return;
    }

    // A series of N effects needs 2*N-1 notes (effects and the silences between
    // them), so what fits is (skMaxNotes + 1) / 2.
    const uint8_t maxCount = (SDK::Message::RequestVibroPlay::skMaxNotes + 1u) / 2u;
    if (count > maxCount) {
        count = maxCount;
    }

    auto* msg = mKernel.comm.allocateMessage<SDK::Message::RequestVibroPlay>();
    if (msg) {
        uint8_t n = 0;
        for (uint8_t i = 0; i < count; ++i) {
            msg->notes[n].effect = SDK::Message::RequestVibroPlay::Effect::SHARP_CLICK_100;
            msg->notes[n].pause  = 0;
            ++n;
            if (i < count - 1u) {
                msg->notes[n].effect = SDK::Message::RequestVibroPlay::Effect::NO_EFFECT;
                msg->notes[n].pause  = skBuzzGapMs;
                ++n;
            }
        }
        msg->notesCount = n;
        mKernel.comm.sendMessage(msg);
        mKernel.comm.releaseMessage(msg);
    }
}

void Service::onWristTilt(uint32_t /*timestampMs*/)
{
    backlightOn();
}
