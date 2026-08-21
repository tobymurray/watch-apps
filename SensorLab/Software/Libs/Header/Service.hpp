/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   SensorLab's service: the instrument, and everything that owns state.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * What this app is
 *
 * An instrument, not a product. Nobody wears it to learn something about
 * themselves. It exists so that four questions about the UNA Watch's sensor
 * layer have written answers: what the limitations are, whether the platform
 * conforms to its own spec, what changed with a firmware update, and how much of
 * any of it is actually known.
 *
 * Its product is not a screen. It is `profile-<firmware>.json` and the run logs
 * beside it, and the report `Tools/profile_report.py` renders from them.
 *
 * ---------------------------------------------------------------------------
 * Why it does not autostart
 *
 * `APP_AUTOSTART Off`, and that is a design decision rather than an omission.
 * Two autostart apps that both claim the accelerometer contend (ledger row S8);
 * SleepLab is the one that has to run every night; and a profiler that silently
 * competed with it would corrupt the thing it exists to measure. **The app does
 * not run unless asked, so that it never becomes the reason another app's
 * measurements are wrong.**
 *
 * A `Utility` app cannot be service-only -- the merger requires a GUI ELF for
 * every type except `Glance` (row P3) -- so there is a screen, and the screen
 * turns out to be the second most valuable thing here after the profile. The
 * Sleep Probe's post-mortem is right that its unique value was *a screen you
 * read before bed rather than a log you read after*: two minutes of hardware
 * time and a thirteen-character block on a 240x240 panel settled four ledger
 * rows. This app's roster is that screen, for all thirty-seven types.
 *
 * ---------------------------------------------------------------------------
 * Three phases, and the loop is the same in all of them
 *
 *   **Existence** (layer 1). Thirty-seven `RequestDefault` / `RequestList` /
 *   `RequestGetDesc` / `RequestConnect` round trips. Seconds. Produces the first
 *   publishable artifact -- an existence and structure table for every sensor
 *   type on this firmware -- and it is the tier that supersedes the Sleep
 *   Probe's log.
 *
 *   **Soak** (layers 2-5). Subscribe what resolved, count what arrives, write
 *   one interval row per sensor per minute, accumulate distributions. Long,
 *   unattended, and the run that must not be interrupted by the cable.
 *
 *   **Idle**. A profile is written and nothing is subscribed. The resting state,
 *   and the one the app opens in.
 *
 * The loop computes the wait to its next due work and passes it to
 * `getMessage()`, the way `Timer` and `Alarm` do. It never polls: this app's
 * sample path is the hottest in the repository -- 308 accelerometer batches a
 * minute at 9.6 samples each for *one* sensor (row S17), with up to thirty-seven
 * possibly subscribed -- so every avoidable wake is multiplied by that.
 *
 * ---------------------------------------------------------------------------
 * Fixed buffers, and the arithmetic
 *
 * Nothing allocates after start. The service holds, by value:
 *
 *   ClaimStore                                       ~67 KB
 *   StreamStats  x 37   (two 128-bin + one 64-bin histogram each)  ~48 KB
 *   FieldStats   x 116  (a distinct-value set plus scalars each)   ~10 KB
 *                                                                 -------
 *                                                                 ~125 KB
 *
 * against the service's 500 KB. Sized statically, so a build that no longer fits
 * fails to link rather than failing at 03:00.
 *
 * ---------------------------------------------------------------------------
 * Resuming, explicitly
 *
 * A soak that is terminated by the cable leaves `state.json` naming an open run.
 * On start the service reads it and does one of exactly two things, and says
 * which in the log:
 *
 *   - uptime climbed since the state was written -> an app restart inside one
 *     boot. The run is closed as `TruncatedByUsb` and a new one opened. Not
 *     resumed: the sensor subscriptions went with the process, so the gap is
 *     unbounded and a distribution spanning it would be a distribution of two
 *     different experiments.
 *   - uptime went backwards -> the device rebooted. Closed as
 *     `TruncatedByReboot`, which is a different finding: every since-boot
 *     counter reset too.
 *
 * Never silently. A run whose manifest says `in_progress` for ever is a run
 * nobody can interpret.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_SERVICE_HPP
#define SENSORLAB_SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Catalogue/Catalogue.hpp"
#include "Commands.hpp"
#include "Evidence/ClaimStore.hpp"
#include "Probes/SensorBus.hpp"
#include "Profile/Manifest.hpp"
#include "Profile/ProfileWriter.hpp"
#include "Profile/RunLog.hpp"
#include "Settings.hpp"
#include "Stats/FieldStats.hpp"
#include "Stats/StreamStats.hpp"

/// This build's version, stamped into every manifest. Passed in by CMake so the
/// packed `.uapp` and the profile cannot disagree about which app produced it.
#ifndef SENSORLAB_VERSION
#define SENSORLAB_VERSION "0.0.0-dev"
#endif

// `Service` is deliberately in the **global** namespace while everything else in
// this app is under `SensorLab::`. The SDK's own service entry point,
// `Libs/Source/AppSystem/EntryPoint/Service/main.cpp`, does
// `new (storage) Service(kernel)` against an unqualified name, so a namespaced
// class does not link. SleepLab and the Sleep Probe do the same thing for the
// same reason.

/**
 * @brief The whole instrument.
 *
 * Single-threaded and allocation-free after start. Takes its kernel by
 * reference rather than through `SDK::KernelProviderService::GetInstance()`,
 * which is what lets `Tests/RunHarness.hpp` script the message queue and drive
 * whole runs through this exact class in milliseconds.
 */
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service();

    void run();

private:
    // -- Per-type live state --------------------------------------------------

    /**
     * @brief What this run knows about one sensor type.
     *
     * One of these per type, always -- not per *subscribed* type. Thirty-seven
     * of them is 48 KB of histograms, which is affordable, and the alternative
     * is a handle-to-slot map on the sample path.
     */
    struct Live
    {
        /// From layer 1, at full 32-bit width. Zero when nothing resolved.
        uint32_t handle    = 0;
        bool     resolved  = false;
        bool     connected = false;
        bool     asked     = false;

        SensorLab::Stats::StreamStats stream;

        /// Index into the service's flat `FieldStats` array, and how many of
        /// them belong to this type. Flat rather than nested so the array is one
        /// static allocation whose size is a catalogue constant.
        uint16_t fieldBase  = 0;
        uint8_t  fieldSlots = 0;
    };

    // -- Lifecycle ------------------------------------------------------------

    /// Read settings, ask the kernel what firmware it is, decide whether a
    /// previous run has to be closed. Nothing is measured here.
    void start();

    /// Open a run: allocate a run id, write its manifest and the log's `R` row.
    bool openRun(CustomMessage::Phase phase);

    /// Close the current run with a stated outcome, write its manifest again,
    /// and write the profile.
    void closeRun(SensorLab::Profile::RunEnd end);

    /// Deal with whatever the last launch left behind. See the header.
    void recoverPreviousRun();

    // -- Layer 1 --------------------------------------------------------------

    /// The thirty-seven-type existence sweep, and the claims it produces.
    void runExistenceSweep();

    /// Record what one type's identity means, as claim rows.
    void recordIdentity(size_t typeIdx, const SensorLab::Probes::Identity &id);

    // -- Soak -----------------------------------------------------------------

    void startSoak();
    void stopSoak(SensorLab::Profile::RunEnd end);

    /// Subscribe everything the settings ask for that resolved a driver.
    void subscribeAll();
    void unsubscribeAll();

    /// Write one interval's rows and promote whatever the accumulated data now
    /// supports. Called on the interval boundary; @p spanMs is the real uptime
    /// span covered, not the nominal interval.
    void closeInterval(uint32_t now, uint32_t spanMs);

    /// Turn the accumulated statistics for one type into claim rows. Separate
    /// from `closeInterval` so that the promotion rules are exercised by the
    /// harness independently of the file writing.
    void promoteStreamClaims(size_t typeIdx, uint32_t now, int64_t wall);
    void promoteFieldClaims(size_t typeIdx, uint32_t now, int64_t wall);

    // -- Sample path ----------------------------------------------------------

    /// A batch arrived. The hottest function in the app.
    void onSensorData(uint32_t handle, uint32_t count, uint32_t stride,
                      const SDK::Sensor::Data *base);

    /// Type index for a handle, or `kTypeCount`. Linear over 37 entries, on the
    /// batch path rather than the sample path -- 308 batches a minute per
    /// stream, so this runs a few thousand times a minute at worst, and a map
    /// would cost an allocation the platform forbids.
    size_t typeForHandle(uint32_t handle) const;

    // -- GUI ------------------------------------------------------------------

    /// Send the status message, then the roster as an indexed burst.
    ///
    /// Publishes nothing at all with no GUI attached. That matters for an
    /// instrument whose screen is open for a minute of every twelve hours:
    /// publishing into a void the rest of the time would be pure battery cost,
    /// charged to the measurement.
    void publish();
    void publishStatus();
    void publishRoster();

    void handleCommand(CustomMessage::Command command);

    // -- Collaborators --------------------------------------------------------

    SDK::Kernel            &mKernel;
    SensorLab::Settings                mSettings;
    SensorLab::SettingsStatus          mSettingsStatus =
        SensorLab::SettingsStatus::Absent;
    SensorLab::Probes::SensorBus       mBus;
    SensorLab::Evidence::ClaimStore    mClaims;
    SensorLab::Profile::RunLog         mLog;
    SensorLab::Profile::ProfileWriter  mProfile;
    SensorLab::Profile::RunManifest    mManifest {};
    SensorLab::Profile::RunState       mState {};

    Live mLive[SensorLab::Catalogue::kTypeCount];

    /// One per field slot in the catalogue, flat. `Live::fieldBase` indexes it.
    SensorLab::Stats::FieldStats mFields[SensorLab::Catalogue::kFieldSlotTotal];

    CustomMessage::Phase mPhase = CustomMessage::Phase::Starting;

    /// Whether a GUI is attached. The service never exits when it detaches
    /// during a soak: an instrument that stopped measuring when the screen
    /// closed would only ever measure watched sensors.
    bool mGuiStarted = false;

    /// Publish rate-limiting. The GUI's custom-message queue is ten deep and
    /// drops the oldest when full, and one publish is four messages -- see
    /// `kPublishMinGapMs` in Service.cpp. A request that arrives inside the gap
    /// sets `mPublishPending` rather than being dropped.
    uint32_t mLastPublishAt  = 0;
    bool     mHavePublished  = false;
    bool     mPublishPending = false;

    /// Uptime this launch began at, and the current interval's boundaries.
    uint32_t mStartedAt      = 0;
    uint32_t mNextIntervalAt = 0;
    uint32_t mIntervalOpened = 0;

    /// Uptime the soak must stop at, or 0 for no deadline.
    uint32_t mSoakDeadline = 0;

    uint32_t mSamplesSeen = 0;
    uint32_t mBatchesSeen = 0;

    /// Sticky, for the status screen and the manifest. On the cable is not a
    /// detail: plugging in terminates every running app, so a soak on the
    /// charger records nothing at all.
    int8_t mCharging = -1;
    int8_t mUsb      = -1;
};

#endif // SENSORLAB_SERVICE_HPP
