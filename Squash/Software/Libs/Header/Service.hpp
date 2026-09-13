#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Metrics/MonotonicTime.hpp"

#include "Commands.hpp"
#include "Session.hpp"
#include "SquashEngine.hpp"
#include "WristTiltDetector.hpp"
#include "ImuCsvRecorder.hpp"
#include "ImuFileSink.hpp"
#include "ImuMarkerLog.hpp"
#include "AppConfigFields.hpp"

#include <memory>

/// The recorder's half: the clock, the sensors and the files.
///
/// It writes no activity and no FIT. What it produces is a recording and its
/// two sidecars, which is what a squash metric would later be built from --
/// see `Squash/README.md`.
class Service : public WristTiltDetector::IListener
{
public:
    Service(SDK::Kernel &kernel);

    virtual ~Service();

    void run();

private:
    // -- Constants ------------------------------------------------------------

    static constexpr uint32_t skBacklightTimeout   = 5000;
    static constexpr uint32_t skSamplePeriod       = 1000;
    static constexpr uint32_t skSampleLatency      = 1000;
    static constexpr float    skFusionSampleRateHz = 100.0f;

    /// How long the Service waits for a GUI before deciding there is nothing to
    /// do. Inherited from the SDK's own example services.
    static constexpr uint32_t skGuiInitTimeoutS = 5;

    /// Silence between the clicks of a multi-click buzz.
    static constexpr uint32_t skBuzzGapMs = 100;

    // -- Infrastructure -------------------------------------------------------

    SDK::Kernel&          mKernel;
    bool                  mGuiStarted;
    CustomMessage::Sender mGuiSender;

    // -- Configuration --------------------------------------------------------

    // Re-read at the start of every session, so a change made on the phone takes
    // effect on the next session rather than the next reinstall.
    std::unique_ptr<SDK::AppConfig> mConfig;
    bool                 mRecordImu = false;
    ImuCsvRecorder::Limits mLimits{};

    // -- Recording ------------------------------------------------------------

    ImuFileSink    mImuSink;
    ImuCsvRecorder mImuRecorder;
    /// Sink is open and waiting for the first IMU sample to start the clock.
    /// Cleared once begun, so a run stopped by a cap is never restarted.
    bool           mImuArmed = false;

    // Markers share the recording's clock, so they are begun from the same
    // sensor tick as the sample recorder and stamped from the last sample seen.
    ImuFileSink  mMarkerSink;
    ImuMarkerLog mMarkerLog;
    /// Sensor tick of the most recent IMU sample, which is the only clock a
    /// marker can be placed on: a key event carries no sensor timestamp, and
    /// the two clocks are unrelated. At 100 Hz this is at most 10 ms stale.
    uint32_t     mLastImuTs = 0;

    /// The most recently completed epoch, from the Rust accumulator.
    squash_epoch mEpoch{};

    // -- Sensors --------------------------------------------------------------

    SDK::Sensor::Connection mSensorHr;
    SDK::Sensor::Connection mSensorWristMotion;
    SDK::Sensor::Connection mSensorFusion;
    bool                    mIsSensorsConnected = false;

    // -- Session --------------------------------------------------------------

    SDK::Metric::MonotonicTime<SDK::Interface::ISystem> mTimeTracker;

    Session::State mState     = Session::State::INACTIVE;
    uint32_t       mActiveS   = 0;   ///< seconds the session has been running
    Session::Label mLabel     = Session::Label::NONE;
    uint32_t       mLabelS    = 0;   ///< seconds held in mLabel

    float   mHrBpm    = 0.0f;
    uint8_t mHrTrust  = 0;
    uint8_t mHrSource = 0;

    // -- Wrist tilt -----------------------------------------------------------

    WristTiltDetector mWristTiltDetector;

    // -- Lifecycle ------------------------------------------------------------

    void connectSensors();
    void disconnect();
    void onStartGUI();
    void onStopGUI();

    // -- Sensor data dispatch -------------------------------------------------

    void handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data);
    void onFusionSample(uint32_t ts, int16_t ax, int16_t ay, int16_t az,
                        int16_t gx, int16_t gy, int16_t gz);

    // -- Event handlers -------------------------------------------------------

    void handleEvent(const CustomMessage::SessionStart& event);
    void handleEvent(const CustomMessage::SessionStop& event);
    void handleEvent(const CustomMessage::SessionPause& event);
    void handleEvent(const CustomMessage::SessionResume& event);
    void handleEvent(const CustomMessage::Mark& event);
    void handleEvent(const CustomMessage::SetLabel& event);

    /**
     * @brief (Re)read the values file by building a fresh SDK::AppConfig.
     *
     * SDK::AppConfig reads its file once, in its constructor, and exposes no
     * reload, so picking up a change made on the phone means a new instance.
     */
    void loadConfig();

    // -- Session control ------------------------------------------------------

    void startSession(std::time_t utc);
    void stopSession(bool discard);
    void pauseSession(bool pause);
    void sendStatus();

    /// Write a marker carrying @p label, and return whether one landed.
    bool writeMarker(Session::Label label);

    // -- Notifications --------------------------------------------------------

    void setCapabilities();
    void requestAccessoryPrepare();
    void backlightOn(uint32_t timeoutMs = skBacklightTimeout);
    void buzz(uint8_t count = 1);

    // -- WristTilt callback ---------------------------------------------------

    virtual void onWristTilt(uint32_t timestampMs) override;
};

#endif // SERVICE_HPP
