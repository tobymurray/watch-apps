/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   The half of Spin that owns the clock, the heart rate and the file.
 *
 * A GUI process cannot reach a sensor — SDK/Kernel/Kernel.hpp defines what a
 * GUI process is handed, and SDK::Sensor::Connection is not in it — so every
 * number on the screen originates here and travels to the Rust renderer as a
 * CustomMessage snapshot. See Commands.hpp.
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Metrics/MonotonicTime.hpp"
#include "SDK/Metrics/MonotonicCounter.hpp"
#include "SDK/Metrics/VariableCounter.hpp"

#include "SDK/AppConfig/AppConfig.hpp"

#include "ActivityWriter.hpp"
#include "AppConfigFields.hpp"
#include "Commands.hpp"

#include <memory>

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    virtual ~Service();

    void run();

private:
    // -- Constants ------------------------------------------------------------

    static constexpr uint32_t skBacklightTimeout = 5000;
    static constexpr uint32_t skSamplePeriod     = 1000;
    static constexpr uint32_t skSampleLatency    = 1000;

    /// The kernel's HR arbiter reports a 0..3 confidence. 1..3 is a reading it
    /// stands behind; 0 means "this is not a heart rate". Only 1..3 is recorded,
    /// so a FIT file never carries a beat the watch itself did not believe.
    static constexpr float skHrTrustMin = 1.0f;
    static constexpr float skHrTrustMax = 3.0f;

    /// VariableCounter validity window: readings outside it are ignored rather
    /// than averaged in. Same bounds the SDK's own activity apps use.
    static constexpr float skHrMinValid = 20.0f;
    static constexpr float skHrMaxValid = 300.0f;

    static constexpr std::time_t skSecondsPerMinute = 60;

    /// Fallback when the watch's profile carries no weight. The calorie model
    /// is proportional to it, so a wrong weight scales the estimate rather than
    /// breaking it -- but it is why the figure is an estimate.
    static constexpr float skDefaultWeightKg = 75.0f;

    /// Zone buckets: [0] below zone 1, [1..5] the zones. Matches
    /// ActivityWriter::kZoneBuckets and Track::Data::zoneSeconds.
    static constexpr size_t skZoneBuckets = 6;

    /// The kernel reports up to six zone thresholds.
    static constexpr size_t skMaxHrThresholds = 6;

    // -- Infrastructure -------------------------------------------------------

    SDK::Kernel&          mKernel;
    bool                  mGuiStarted = false;
    CustomMessage::Sender mGuiSender;

    ActivityWriter mActivityWriter;

    // -- Configuration --------------------------------------------------------
    // Values the wearer set on their phone, read through SDK::AppConfig from
    // the file it writes into this app's own directory. Re-read at the start of
    // every ride, so a change takes effect on the next ride rather than the
    // next reinstall; the cost is one file read per ride.

    std::unique_ptr<SDK::AppConfig> mConfig;

    std::time_t mAutoLapSeconds = 0;  ///< 0 = one lap for the whole ride
    std::time_t mTargetSeconds  = 0;  ///< 0 = no target
    bool        mKeepScreenLit  = false;
    bool        mEnergyInKilojoules = false;  ///< display unit only

    // -- Sensors --------------------------------------------------------------

    SDK::Sensor::Connection mSensorHr;
    SDK::Sensor::Connection mSensorWristMotion;
    bool                    mIsSensorsConnected = false;

    // -- Metrics --------------------------------------------------------------

    SDK::Metric::MonotonicTime<SDK::Interface::ISystem> mTimeTracker;
    SDK::Metric::MonotonicCounter<std::time_t>          mTimeCounter;
    SDK::Metric::VariableCounter                        mHrCounter;

    float   mWeightKg = skDefaultWeightKg;      ///< From the watch's own profile.
    uint8_t mHrThresholds[skMaxHrThresholds] = {};
    uint8_t mHrThresholdCount = 0;

    uint8_t mHrSource      = 0;  ///< Latest HeartRateEx::Source, for the icon + FIT hr_source.
    uint8_t mHrOpticalBpm  = 0;  ///< Latest raw optical (PPG) bpm, for the FIT hr_optical series.
    uint8_t mHrExternalBpm = 0;  ///< Latest raw external (strap) bpm, for the FIT hr_external series.

    /// Last SDK::Accessory::State seen. Held so a GUI that starts (or resumes)
    /// after the strap connected still gets told about it.
    uint8_t mAccessoryState = 0;
    char    mAccessoryName[24] = {};

    // -- Ride state -----------------------------------------------------------

    Track::State mTrackState      = Track::State::INACTIVE;
    bool         mSessionNotEmpty = false;
    Track::Data  mTrackData{};

    /// The same three quantities as Track::Data's, but accumulated since the
    /// last lap rather than since the ride began. Held here rather than in
    /// Track::Data because the GUI has no use for a per-lap figure and would
    /// only be shown one it could not explain.
    float       mLapCalories        = 0.0f;
    float       mLapRestingCalories = 0.0f;
    std::time_t mLapZoneSeconds[skZoneBuckets] = {};

    // -- Lifecycle ------------------------------------------------------------

    void connectSensors();
    void disconnect();
    void onStartGUI();
    void onStopGUI();

    void handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data);

    // -- Ride control ---------------------------------------------------------

    void loadConfig();
    void loadSystemSettings();
    void startTrack(std::time_t utc);
    void processTrack();
    void saveLap();
    void updateHrDerivedMetrics();
    uint8_t hrZoneFor(float hr) const;
    static float zoneMet(uint8_t zone);
    void stopTrack(bool discard);
    void pauseTrack(bool pause);
    ActivityWriter::RecordData prepareRecordData() const;

    // -- Kernel requests ------------------------------------------------------

    void setCapabilities();
    void requestAccessoryPrepare();
    void requestAccessoryRelease();
    void notifyNewActivity();
    void notifyLapEnd();
    void notifyTargetReached();
    void backlightOn(uint32_t timeoutMs = skBacklightTimeout);
    /// Hold the backlight until something turns it off. autoOffTimeoutMs == 0
    /// disables the auto-off, so this is one message rather than a re-arming
    /// timer -- see SDK::Message::RequestBacklightSet.
    void backlightHold(bool on);
    void playBuzzerPattern(uint16_t beepMs, uint8_t count = 1, uint16_t silenceMs = 100);
    void playVibroPattern(SDK::Message::RequestVibroPlay::Effect effect,
                          uint8_t count = 1, uint16_t silenceMs = 100);
};

#endif // SERVICE_HPP
