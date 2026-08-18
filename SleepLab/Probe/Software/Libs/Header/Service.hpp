/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Tier 0 feasibility probe: subscribe everything, count, sleep.
 ******************************************************************************
 *
 * Nothing on this platform has ever run all night. Every app in `watch-apps`
 * and in the SDK's `Examples/` is either foreground-interactive or a
 * background task measured in minutes. Whether a Service keeps its sensor
 * subscriptions, its file handles and its battery across eight hours of
 * screen-off operation is *unmeasured* -- and a sleep app that discovers in
 * month two that its sensors stopped at 02:00 has wasted month one.
 *
 * So this exists first, and it is deliberately the smallest thing that can
 * answer the questions:
 *
 *   1. Does sensor delivery survive a whole night uninterrupted, and if not,
 *      where does it stop and what does the log look like there?
 *   2. What does continuous optical HR cost overnight, in percent and in mA?
 *   3. What is the *delivered* rate per sensor, over hours rather than
 *      minutes?
 *   4. Does SPO2 (0xF1) produce a single sample?
 *   5. Does HEART_BEAT (0x40) still emit nothing on 1.4 firmware? UNA
 *      answered "no events at all" against the 1.3 line (PR #167); firmware
 *      moved to 1.4 on 2026-08-17 and a higher-rate PPG mode is on their
 *      roadmap, so the answer has an expiry date. A non-zero count here
 *      reopens overnight HRV, which reopens sleep staging.
 *   6. Does TOUCH_DETECT hold "worn" for a loosely-strapped sleeping wrist,
 *      and how often does it flicker?
 *   7. Does anything else on the device contend for the HR sensor?
 *   8. Free space and sustained append throughput on the user volume.
 *
 * It answers them by subscribing to every candidate sensor, counting what
 * actually arrives, and appending one line per minute. It computes nothing,
 * decides nothing and displays nothing. Analysis is `Tools/probe_report.py`
 * on the host, where it can be re-run against the same night as the questions
 * change.
 *
 * ---------------------------------------------------------------------------
 * Why it has no GUI
 *
 * A `.uapp` with no TouchGFX ELF is a supported shape -- the SDK's own
 * `GlanceHR` is built that way, and `una-app.cmake` makes the GUI half
 * conditional on TOUCHGFX_PATH. Leaving it out keeps the binary small and
 * removes an entire class of thing that can go wrong overnight. The cost is
 * that the probe cannot be stopped from the watch: it is autostart, so it
 * starts itself, and the way to stop it is to delete `Apps/SleepProbe/` over
 * USB. That is the correct trade for a diagnostic whose whole job is to still
 * be running at 04:00.
 *
 * ---------------------------------------------------------------------------
 * The USB rule
 *
 * Plugging in terminates every running app; autostart relaunches on unplug
 * (`MapManager/README.md` records the kernel log proving it). So a probe night
 * must be run unplugged, and the `R` rows in the log mark every launch
 * boundary the cable caused. BATTERY_CHARGING is subscribed anyway, because
 * "the watch was on the charger" has to be visible in the data rather than
 * remembered.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Commands.hpp"
#include "ProbeConfig.hpp"
#include "ProbeLog.hpp"

/// One row per minute. A minute is short enough to localise where delivery
/// stopped and long enough that 480 rows a night is a ~50 KB file.
constexpr uint32_t kRowPeriodMs = 60u * 1000u;

/**
 * @brief The probe's whole implementation.
 *
 * Single-threaded, allocation-free after start (the config reader's buffer is
 * the one allocation and it is released before the loop begins), and it sleeps
 * to its next due row rather than polling. That last part is not tidiness: an
 * autostart service that spins costs the device battery for as long as it is
 * installed, and this probe's entire purpose is measuring battery.
 */
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service();

    void run();

private:
    // -- Per-minute accumulators ---------------------------------------------

    /**
     * @brief Everything counted between two rows.
     *
     * Reset wholesale at each row boundary. Held by value, so the sample path
     * allocates nothing -- one of the platform's hard requirements, and the
     * difference between a service that survives eight hours and one that
     * fragments its heap into failure.
     */
    struct Accum
    {
        // Accelerometer
        uint32_t accN        = 0;
        uint32_t accBatches  = 0;
        uint32_t accFirstTs  = 0;
        uint32_t accLastTs   = 0;
        uint32_t accMaxGap   = 0;
        bool     accSeen     = false;

        // Worn
        uint32_t touchN      = 0;
        uint32_t touchWornN  = 0;
        uint32_t touchEdges  = 0;

        // Movement gates
        uint32_t motionN = 0, motionNo = 0, motionMot = 0, motionSig = 0;
        uint32_t arN = 0, arStill = 0, arWalk = 0, arRun = 0;

        // Cardio
        uint32_t hrN        = 0;
        float    hrSum      = 0.0f;
        float    hrTrustSum = 0.0f;
        float    hrMin      = 0.0f;
        float    hrMax      = 0.0f;

        uint32_t hrExN = 0, hrExOpt = 0, hrExExt = 0, hrExUnk = 0;

        uint32_t beatN      = 0;
        uint32_t ppgN       = 0;
        uint32_t ppgFirstTs = 0;
        uint32_t ppgLastTs  = 0;
        bool     ppgSeen    = false;

        uint32_t spo2N = 0;
        float    spo2Last = 0.0f;

        // Loop
        uint32_t wakes = 0;
        uint32_t msgs  = 0;

        void reset() { *this = Accum{}; }
    };

    // -- Lifecycle ------------------------------------------------------------

    /// Resolve and connect every sensor the config asks for, recording which
    /// ones actually resolved. A type with no firmware producer fails to
    /// resolve here rather than at first sample, and that distinction is what
    /// lets the log write -1 (never subscribed) instead of 0 (subscribed,
    /// delivered nothing) -- the difference between "the SDK has no driver"
    /// and "the firmware produced nothing all night".
    void connectSensors();
    void disconnectSensors();

    /// Drive the HR duty cycle, if configured. Returns the ms until the next
    /// duty transition, or 0 if there is none to wait for.
    uint32_t pumpHrDuty(uint32_t now);

    // -- Sample path ----------------------------------------------------------

    void onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch);

    // -- Row emission ---------------------------------------------------------

    /// Fill a row from the accumulators and the sticky state, write it, and
    /// reset. @p spanMs is the real uptime span the row covers, not 60000.
    void emitRow(uint32_t now, uint32_t spanMs);

    // -- GUI ------------------------------------------------------------------

    /// Send the status screen a fresh snapshot.
    ///
    /// Called on GUI start and after each row, and never otherwise: with no
    /// GUI attached this returns immediately without allocating a message.
    /// That matters more than it sounds for an app that runs for the device's
    /// whole life -- the screen is open for perhaps a minute of every eight
    /// hours, and publishing into a void the rest of the time would be pure
    /// battery cost.
    void publishStatus();

    // -- Collaborators --------------------------------------------------------

    SDK::Kernel   &mKernel;
    Probe::Config  mConfig;
    Probe::Log     mLog;

    // Connections. Declared in the order they are connected, which is the
    // order they appear in the log's columns.
    SDK::Sensor::Connection mAccel;
    SDK::Sensor::Connection mTouch;
    SDK::Sensor::Connection mMotion;
    SDK::Sensor::Connection mActivity;
    SDK::Sensor::Connection mHr;
    SDK::Sensor::Connection mHrEx;
    SDK::Sensor::Connection mBeat;
    SDK::Sensor::Connection mPpg;
    SDK::Sensor::Connection mSpo2;
    SDK::Sensor::Connection mSteps;
    SDK::Sensor::Connection mBattLevel;
    SDK::Sensor::Connection mBattCharge;
    SDK::Sensor::Connection mBattMetrics;

    /// Whether each connection resolved a driver. See connectSensors().
    struct Subscribed {
        bool accel = false, touch = false, motion = false, activity = false;
        bool hr = false, hrEx = false, beat = false, ppg = false, spo2 = false;
        bool steps = false, battLevel = false, battCharge = false, battMetrics = false;
    } mHas;

    Accum mAcc;

    /// Whether a GUI is attached. The service never exits when it detaches:
    /// the whole point of autostart is that the recording continues unobserved.
    bool  mGuiStarted = false;

    /// Uptime this launch began at, for the status screen's "running for".
    uint32_t mStartedAt = 0;

    /// Rows appended this launch, and the last row's headline counts. Kept
    /// separately from the accumulators because those are reset at every row
    /// boundary and the screen has to show the row that just closed.
    uint32_t mRowsWritten   = 0;
    int32_t  mLastAccN      = -1;
    int32_t  mLastHrN       = -1;
    int32_t  mLastTouchWorn = -1;
    int32_t  mLastTouchN    = -1;

    /// Cumulative across the launch: both are "did this ever happen at all"
    /// questions, and a per-row count would lose the answer at the next row.
    uint32_t mTotalBeatN = 0;
    uint32_t mTotalSpo2N = 0;

    // -- Sticky state ---------------------------------------------------------
    //
    // Values that describe a level rather than a count, so the last one seen
    // is the answer for the row even if it arrived three rows ago. Battery
    // level updates perhaps once a minute; step count only when you move.

    bool     mTouchLastWorn    = false;
    bool     mTouchLastValid   = false;

    int64_t  mStepTotal        = -1;
    int64_t  mStepAtRowStart   = -1;

    int32_t  mBattPctX10       = -1;
    int8_t   mCharging         = -1;
    int8_t   mUsb              = -1;
    int32_t  mBattMv           = -1;
    int32_t  mBattMaX10        = -1;
    int32_t  mBattAvgMaX10     = -1;
    int32_t  mBattMah          = -1;

    // -- Clocks ---------------------------------------------------------------

    /// Uptime the next row is due at. Advanced by exactly kRowPeriodMs each
    /// time, never re-based on "now", so a row that lands late does not push
    /// every subsequent row late with it -- the file stays on a minute grid
    /// and the drift shows up in span_ms where it can be read.
    uint32_t mNextRowAt = 0;

    /// Uptime the current row's window opened at, for its real span.
    uint32_t mRowOpenedAt = 0;

    // -- HR duty state --------------------------------------------------------

    bool     mHrDutyOn     = false;
    uint32_t mHrDutyNextAt = 0;
};
#endif // SERVICE_HPP
