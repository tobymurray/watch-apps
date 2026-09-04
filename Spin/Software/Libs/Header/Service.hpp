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

#include "EventLog.hpp"
#include "SharedLog.hpp"
#include "SpinEngine.hpp"

#include "ActivityWriter.hpp"
#include "AppConfigFields.hpp"
#include "HrHold.hpp"
#include "SecondsAccrual.hpp"
#include "ZoneLadder.hpp"
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

    static constexpr size_t skMaxZones    = SpinConfig::kMaxZones;
    static constexpr size_t skZoneBuckets = skMaxZones + 1;

    /// A bare filename, so it lands in this app's own directory where USB can
    /// read it. Diagnostic only; see Docs/RECOVERY-FIELD-TEST.md.
    static constexpr const char *skEventLogFile = "recovery.log";

    /// Ticks a second apart are only logged while PAUSED, where the window is;
    /// riding gets one line this often instead, which is enough to see the
    /// shape of the ride without filling the card.
    static constexpr std::time_t skActiveLogPeriod = 30;

    /// Names ../SharedData/spin_sessions.json and appears inside it.
    static constexpr const char *skLogApp   = "Spin";
    /// What the entries are, for a reader merging several apps' logs. The same
    /// pair of words the FIT session carries as sport and sub_sport.
    static constexpr const char *skLogSport = "indoor_cycling";

    // Three places size a zone-bucket array and every one of them is indexed by
    // hrZone, which runs to mZoneCount. Track::Data::zoneSeconds was left at 6
    // when the dial grew from five zones to eight, so an eight-zone ride wrote
    // past it into the calorie accumulators. Now they cannot drift apart
    // silently again.
    static_assert(Track::kZoneBuckets == skZoneBuckets,
                  "Track::Data::zoneSeconds is not the size the Service indexes");
    static_assert(ActivityWriter::kZoneBuckets == skZoneBuckets,
                  "ActivityWriter's zone arrays are not the size the Service fills");
    static_assert(SPIN_MAX_ZONE_BUCKETS == skZoneBuckets,
                  "the engine's zone arrays are not the size the Service fills");
    static_assert(SPIN_MAX_ZONES == skMaxZones,
                  "the engine's zone ladder is not the size the Service fills");

    // -- Infrastructure -------------------------------------------------------

    SDK::Kernel&          mKernel;
    bool                  mGuiStarted = false;
    CustomMessage::Sender mGuiSender;

    ActivityWriter mActivityWriter;

    /// The record of the series, as opposed to the record of the ride. Written
    /// once, after the .fit is closed; see EffortKit/README.md for the schema.
    Spin::SharedLog mSharedLog;

    /// Why a recovery window did what it did, in a file, because LOG_* needs a
    /// debug UART adapter this watch is not usually attached to.
    Spin::EventLog mEventLog;
    std::time_t        mLastActiveLogUtc = 0;

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
    /// Offer the post-ride kilojoule screen. Forwarded to the GUI, which owns
    /// the screen; the Service only has to know whether to promise it.
    bool        mAskForKilojoules = true;

    // -- Sensors --------------------------------------------------------------

    SDK::Sensor::Connection mSensorHr;
    SDK::Sensor::Connection mSensorWristMotion;
    bool                    mIsSensorsConnected = false;

    // -- Metrics --------------------------------------------------------------

    SDK::Metric::MonotonicTime<SDK::Interface::ISystem> mTimeTracker;
    SDK::Metric::MonotonicCounter<std::time_t>          mTimeCounter;
    SDK::Metric::VariableCounter                        mHrCounter;

    float   mWeightKg = skDefaultWeightKg;      ///< From the watch's own profile.

    /// The zone floors in use: mZoneFloor[i] is the lowest heart rate in zone
    /// i+1, and zone i+1 runs up to mZoneFloor[i+1]. The top zone is
    /// open-ended, so N zones need N floors. Filled from this app's config when
    /// it declares a count, and from the watch's own settings otherwise.
    uint8_t mZoneFloor[skMaxZones] = {};
    uint8_t mZoneCount = 0;

    /// The watch's own zone floors, kept separately so a bad config can fall
    /// back to them without another trip to the kernel.
    uint8_t mSystemZoneFloor[skMaxZones] = {};
    uint8_t mSystemZoneCount = 0;

    /// The top of the watch's own ladder, which is its maximum heart rate
    /// rather than a zone floor. Kept so a configured zone count with no floors
    /// of its own can be spread across the same range.
    uint8_t mSystemMaxHr = 0;

    /// Bridges a one-second dip in the arbiter's confidence, so the screen does
    /// not blank a reading the sensor still has. Display side only -- the FIT
    /// record keeps the strict gate. See HrHold.hpp.
    HrHold mHrHold;

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

    /// Turns a tick into the span of active time it is responsible for, so
    /// zone seconds and calories total the ride rather than exceeding it by
    /// one. See SecondsAccrual.hpp.
    SecondsAccrual mAccrual;
    std::time_t mLapZoneSeconds[skZoneBuckets] = {};

    /// The measurements this ride produced. The newest are kept, because the
    /// pause at the end of a ride is the one that happens every ride and so is
    /// the one comparable across them.
    SpinRecovery mRecoveries[SPIN_MAX_RECOVERIES] = {};
    uint8_t mRecoveryCount     = 0;
    uint8_t mRecoveriesDropped = 0;

    // -- Lifecycle ------------------------------------------------------------

    void connectSensors();
    void disconnect();
    void onStartGUI();
    void onStopGUI();

    void handleSensorsData(uint16_t handle, SDK::Sensor::DataBatch& data);

    // -- Ride control ---------------------------------------------------------

    void loadConfig();
    void loadSystemSettings();
    void applyZoneConfig();
    void startTrack(std::time_t utc);
    void processTrack();
    /// A lap the wearer asked for, as opposed to one auto-lap produced.
    void lapTrack();
    void saveLap();
    /// Keep a completed measurement, dropping the oldest if there is no room.
    void keepRecovery(const SpinRecovery& measurement);
    /// One line per second while paused, one every skActiveLogPeriod while
    /// riding, so a window can be reconstructed from the file afterwards.
    void logSecond(std::time_t utc, bool trusted);
    /// Fold the finished ride into ../SharedData; @p saved is whether the .fit
    /// landed, and nothing is written when it did not.
    void recordSession(bool saved, uint16_t workKilojoules);
    void updateHrDerivedMetrics();
    uint8_t hrZoneFor(float hr) const;
    uint8_t hrZoneFractionFor(float hr, uint8_t zone) const;
    static float zoneMet(uint8_t zone);
    /// @param workKilojoules  the bike console's figure as the wearer entered
    ///        it, or 0 for "nobody said" -- which is a normal ride, not a
    ///        failure, and reaches the file as absent fields rather than zeros.
    void stopTrack(bool discard, uint16_t workKilojoules);
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
