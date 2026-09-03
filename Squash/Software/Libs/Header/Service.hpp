#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Metrics/MonotonicTime.hpp"
#include "SDK/Metrics/MonotonicCounter.hpp"
#include "SDK/Metrics/VariableCounter.hpp"
#include "SDK/Metrics/ThrottledSample.hpp"

#include "SettingsSerializer.hpp"
#include "ActivitySummarySerializer.hpp"
#include "ActivityWriter.hpp"
#include "Commands.hpp"
#include "WristTiltDetector.hpp"
#include "ImuCsvRecorder.hpp"
#include "ImuFileSink.hpp"
#include "HrCsvLog.hpp"
#include "SquashEngine.hpp"
#include "SquashLog.hpp"
#include "ImuMarkerLog.hpp"
#include "AppConfigFields.hpp"
#include <array>
#include <memory>

class Service : public WristTiltDetector::IListener
{
public:
    Service(SDK::Kernel &kernel);

    virtual ~Service();

    void run();

private:
    // -- Constants ------------------------------------------------------------

    static constexpr uint32_t skBacklightTimeout     = 5000;
    static constexpr uint32_t skSamplePeriod         = 1000;
    static constexpr uint32_t skSampleLatency        = 1000;

    static constexpr uint32_t skBatteryLogPeriodMs   = 5 * 60 * 1000;
    static constexpr float    skFusionSampleRateHz   = 100.0f;
    static constexpr float    skDefaultWeightKg      = 75.0f; ///< Fallback when the system profile weight is absent or zero.

    // -- Infrastructure -------------------------------------------------------

    SDK::Kernel&          mKernel;
    bool                  mGuiStarted;
    CustomMessage::Sender mGuiSender;

    // -- Settings & persistence -----------------------------------------------

    Settings                  mSettings;
    bool                      mIsImperial = false;
    bool                      mTimeFormat12h = false;
    SettingsSerializer        mSettingsSerializer;
    ActivitySummary           mSummary;
    ActivitySummarySerializer mActivitySummarySerializer;
    ActivityWriter            mActivityWriter;

    // Research mode, off unless the wearer turned it on from their phone. The
    // values file is re-read at the start of every session, so a change takes
    // effect on the next session rather than the next reinstall; the cost is
    // one file read per session.
    std::unique_ptr<SDK::AppConfig> mConfig;
    bool                      mRecordImu = false;
    ImuFileSink               mImuSink;
    ImuCsvRecorder            mImuRecorder;
    /// Sink is open and waiting for the first IMU sample to start the clock.
    /// Cleared once begun, so a run stopped by a cap is never restarted.
    bool                      mImuArmed = false;

    // Markers share the recording's clock, so they are begun from the same
    // sensor tick as the sample recorder and stamped from the last sample seen.
    ImuFileSink               mMarkerSink;
    ImuMarkerLog              mMarkerLog;

    // The heart rate that went with the recording, on the recording's clock,
    // because the settling time of this signal cannot be measured from the FIT
    // file's own timebase.
    ImuFileSink               mHrSink;
    HrCsvLog                  mHrLog;
    /// Sensor tick of the most recent IMU sample, which is the only clock a
    /// marker or a heart-rate reading can be placed on: neither event carries a
    /// sensor timestamp, and the two clocks are unrelated. At 100 Hz this is at
    /// most 10 ms stale.
    uint32_t                  mLastImuTs = 0;

    // The engine runs on the same sensor tick as everything else, offset so a
    // session starts at zero. Its state is static inside the Rust archive, so
    // there is nothing to hold here but the offset.
    SquashProfileStore        mProfileStore;
    /// Everything worth knowing about a session, on the volume rather than down
    /// a UART nobody has attached on court.
    SquashLog                 mLog;
    uint32_t                  mEngineStartTs = 0;
    bool                      mEngineStarted = false;

    // -- Sensors --------------------------------------------------------------

    SDK::Sensor::Connection mSensorHr;
    SDK::Sensor::Connection mSensorBatteryLevel;
    SDK::Sensor::Connection mSensorBatteryMetrics;
    SDK::Sensor::Connection mSensorWristMotion;
    SDK::Sensor::Connection mSensorFusion;
    bool                    mIsSensorsConnected = false;

    // -- Metrics --------------------------------------------------------------

    SDK::Metric::MonotonicTime<SDK::Interface::ISystem> mTimeTracker;
    SDK::Metric::MonotonicCounter<std::time_t>          mTimeCounter;
    SDK::Metric::MonotonicCounter<float>                mDistanceCounter;
    SDK::Metric::VariableCounter                        mSpeedCounter;
    SDK::Metric::VariableCounter                        mHrCounter;

    // Battery SoC and voltage are sampled independently;
    // a FIT record is written only when both are due.
    SDK::Metric::ThrottledSample<float, SDK::Interface::ISystem> mBatterySoc;     ///< State of charge, percent
    SDK::Metric::ThrottledSample<float, SDK::Interface::ISystem> mBatteryVoltage; ///< Voltage, volts

    float mWeightKg = skDefaultWeightKg;  ///< From system profile; falls back to skDefaultWeightKg.
    std::array<uint8_t, CustomMessage::kHrThresholdsCount> mHrThresholds = {};
    uint8_t mHrThresholdCount = 0;
    uint8_t mHrSource = 0;      ///< Latest HR source (HeartRateEx::Source) for the icon + FIT hr_source.
    /// Last source written to the diagnostic log, so a change is logged once
    /// rather than every second. 0xFF means nothing logged yet.
    uint8_t mHrSourceLogged = 0xFFu;
    uint8_t mHrOpticalBpm = 0;  ///< Latest raw optical (PPG) bpm, for the FIT hr_optical series.
    uint8_t mHrExternalBpm = 0; ///< Latest raw external (strap) bpm, for the FIT hr_external series.

    // -- Track state ----------------------------------------------------------

    Track::State mTrackState          = Track::State::INACTIVE;
    bool         mSessionNotEmpty     = false;
    bool         mLapNotEmpty         = false;
    Track::Data  mTrackData{};

    // -- Wrist tilt -----------------------------------------------------------

    WristTiltDetector mWristTiltDetector;

    // -- Lifecycle ------------------------------------------------------------

    void connectSensors();
    void disconnect();
    void onStartGUI();
    void onStopGUI();

    // -- Sensor data dispatch -------------------------------------------------

    void handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data);

    // -- Event handlers -------------------------------------------------------

    void handleEvent(const CustomMessage::TrackStart& event);
    void handleEvent(const CustomMessage::TrackStop& event);
    void handleEvent(const CustomMessage::SettingsSave& event);
    void handleEvent(const CustomMessage::TrackPause& event);
    void handleEvent(const CustomMessage::TrackResume& event);
    void handleEvent(const CustomMessage::ManualLap& event);

    /**
     * @brief (Re)read the values file by building a fresh SDK::AppConfig.
     *
     * SDK::AppConfig reads its file once, in its constructor, and exposes no
     * reload, so picking up a change made on the phone means a new instance.
     */
    void loadConfig();

    // -- Track control --------------------------------------------------------

    void sendInitialInfoToGui();
    void startTrack(std::time_t utc);
    void processTrack();
    void updateHrDerivedMetrics();
    void saveLap();
    void stopTrack(bool discard);
    void pauseTrack(bool pause);
    void buildPartialSummary();
    ActivityWriter::RecordData prepareRecordData();
    uint8_t getHrZone(float hr) const;
    float getZoneMet(uint8_t zone) const;

    // -- Notifications --------------------------------------------------------

    void setCapabilities();
    void requestAccessoryPrepare();   // opt in to external HR (pre-warm at GUI start)
    void requestAccessoryRelease();
    void notifyNewActivity();
    void backlightOn(uint32_t timeoutMs = skBacklightTimeout);
    void playBuzzerPattern(uint16_t beepMs, uint8_t count = 1, uint16_t silenceMs = 100);
    void playVibroPattern(SDK::Message::RequestVibroPlay::Effect effect, uint8_t count = 1, uint16_t silenceMs = 100);

    // -- WristTilt callback ---------------------------------------------------

    virtual void onWristTilt(uint32_t timestampMs) override;
};

#endif // SERVICE_HPP
