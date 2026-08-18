/**
 ******************************************************************************
 * @file    ProbeLog.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The probe's on-disk record: one line per minute, appended forever.
 ******************************************************************************
 *
 * NORMATIVE FORMAT. `Tools/probe_report.py` parses exactly what is described
 * here, so this comment and that script are the two halves of one contract.
 *
 * The file is line-oriented CSV with a `kind` first column, because a single
 * flat table cannot say both "a launch began here" and "this is what the last
 * minute delivered". Three kinds:
 *
 *   H  header      one per file, written only when the file is created
 *   R  run start   one per app launch -- see "launch boundaries" below
 *   M  minute      the measurement
 *
 * Every numeric field is an integer. Nothing here formats a float: the MCU's
 * newlib may not link %f at all, and a diagnostic that silently prints empty
 * strings for its own measurements is worse than one that scales by ten. A
 * field named `_x10` or `_x100` is the value times that factor, truncated.
 *
 * A missing measurement is `-1`, never `0`. Zero delivered heart-rate samples
 * in a minute is a finding; a heart-rate sensor that was never subscribed is
 * not, and the two must not read the same.
 *
 * ---------------------------------------------------------------------------
 * Launch boundaries, and why the log needs them
 *
 * MapManager's log reads as one confused process until you know that plugging
 * in USB terminates every running app and autostart relaunches it on unplug
 * (see `MapManager/README.md`). Nothing marks where one launch ended and the
 * next began, so the boundaries had to be recovered afterwards by hand.
 *
 * This log marks them. Every launch writes an `R` row carrying both clocks, so
 * a reader can tell the three cases apart without inference:
 *
 *   - uptime climbs across the R row     -> app restart within one boot
 *                                           (USB session, or a service crash)
 *   - uptime jumps backwards             -> device reboot
 *   - wall clock moves but uptime does not agree
 *                                        -> the wall clock was changed under us
 *
 * That last case is why every row carries both. Uptime is monotonic but says
 * nothing about "23:00"; wall clock knows what 23:00 means but can jump on a
 * timezone change, a host sync or DST. A duration is only ever derived from
 * uptime; a time of day is only ever read from the wall clock.
 *
 ******************************************************************************
 */

#ifndef PROBELOG_HPP
#define PROBELOG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace Probe
{

/// Bumped when a column is added, removed or reinterpreted. The reader refuses
/// a file whose schema it does not know rather than mapping columns by
/// position and quietly reporting the wrong sensor's counts.
constexpr uint32_t kLogSchema = 1;

/// Relative to the app's sandbox -- `Apps/SleepProbe/` on the USB volume.
constexpr char kLogPath[] = "probe_log.csv";

/**
 * @brief One minute of delivery counts, from every sensor at once.
 *
 * Deliberately flat and POD: the service accumulates into one of these across
 * a minute and hands it over whole. No allocation, no vectors -- this lives on
 * the service's stack for the life of the app.
 *
 * "Delivered" throughout means *what the app actually received*, never what it
 * asked for. The requested period is not honoured naively -- the per-listener
 * sample-rate gate thins on a boundary at half the expected period, an exact
 * ratio falls on the thinner side, and the thinning is quantised into bands
 * rather than being proportional to rate (pinned by
 * `Tests/Host/simulator/SampleRateAdapter_test.cpp` on the SDK's
 * `feat/sample-rate-adapter-rule`). Measuring delivery rather than assuming it
 * is the entire point of this row.
 */
struct MinuteRow
{
    // -- Clocks ---------------------------------------------------------------

    uint32_t uptimeMs   = 0;   ///< kernel.sys.getTimeMs() at the moment of write.
    int64_t  wallUtc    = 0;   ///< time(nullptr); -1 if the clock is unreadable.
    int16_t  localMin   = -1;  ///< Local minutes past midnight, 0..1439; -1 unknown.

    /// Uptime span this row actually covers. Not assumed to be 60000: a row is
    /// written when the loop next wakes at or after its deadline, and a busy
    /// or delayed loop overshoots. Every rate in the report is computed
    /// against this, not against a nominal minute.
    uint32_t spanMs     = 0;

    // -- Accelerometer --------------------------------------------------------

    int32_t  accN       = -1;  ///< Samples delivered.
    /// Span between the first and last *sensor-reported* timestamps in the
    /// minute, in ms. With accN this gives a delivered rate derived from the
    /// sensor's own clock rather than from the loop's -- the two disagreeing
    /// is itself a finding.
    int32_t  accTsSpanMs = -1;
    /// Largest gap between consecutive sensor timestamps, in ms. A whole night
    /// can average a healthy rate and still have stopped for twenty minutes at
    /// 02:00; the mean will not show that and this will.
    int32_t  accMaxGapMs = -1;
    int32_t  accBatches  = -1; ///< EVENT_SENSOR_LAYER_DATA messages carrying accel.

    // -- Worn detection -------------------------------------------------------

    int32_t  touchN     = -1;  ///< TOUCH_DETECT samples delivered.
    int32_t  touchWornN = -1;  ///< ...of which reported worn.
    /// Worn/not-worn transitions within the minute. The flicker rate on a
    /// loosely-strapped sleeping wrist is a Tier 0 question, and a worn
    /// *fraction* alone cannot answer it: 50 % worn is one clean removal or
    /// thirty flickers, and only one of those breaks the worn gate.
    int32_t  touchEdges = -1;

    // -- Kernel-side movement gates ------------------------------------------

    int32_t  motionN    = -1;  ///< MOTION_DETECT events delivered.
    int32_t  motionNo   = -1;  ///< ...NO_MOTION.
    int32_t  motionMot  = -1;  ///< ...MOTION.
    int32_t  motionSig  = -1;  ///< ...SIG_MOTION.

    int32_t  arN        = -1;  ///< ACTIVITY_RECOGNITION events delivered.
    int32_t  arStill    = -1;  ///< ...STILL.
    int32_t  arWalk     = -1;  ///< ...WALKING.
    int32_t  arRun      = -1;  ///< ...RUNNING.

    // -- Cardio ---------------------------------------------------------------

    int32_t  hrN        = -1;  ///< HEART_RATE samples delivered.
    int32_t  hrMeanX10  = -1;  ///< Mean bpm x10 over the minute.
    int32_t  hrMin      = -1;  ///< Minimum bpm seen.
    int32_t  hrMax      = -1;  ///< Maximum bpm seen.
    int32_t  hrTrustX10 = -1;  ///< Mean trust x10.

    int32_t  hrExN      = -1;  ///< HEART_RATE_EX samples delivered.
    int32_t  hrExOptN   = -1;  ///< ...arbitrated to the wrist optical source.
    int32_t  hrExExtN   = -1;  ///< ...arbitrated to an external strap.
    int32_t  hrExUnkN   = -1;  ///< ...no valid source.

    /// HEART_BEAT (0x40) events. UNA's answer to PR #167 was that this emits
    /// nothing at all -- HR detection is a frequency-domain algorithm, not
    /// per-beat detection -- so the expected value is 0 and a non-zero value
    /// reopens the HRV question entirely. Firmware moved to 1.4 on 2026-08-17,
    /// which is why this column exists rather than being assumed.
    int32_t  beatN      = -1;
    int32_t  ppgN       = -1;  ///< PPG (0xF0) samples delivered.
    int32_t  ppgTsSpanMs = -1; ///< As accTsSpanMs; with ppgN gives the PPG rate.

    /// SPO2 (0xF1). The type and parser exist; whether the firmware produces
    /// anything is unverified, and one sample all night settles it.
    int32_t  spo2N      = -1;
    int32_t  spo2LastX10 = -1; ///< Last saturation seen, x10.

    // -- Steps ----------------------------------------------------------------

    int64_t  stepTotal  = -1;  ///< STEP_COUNTER, monotonic since boot.
    int32_t  stepDelta  = -1;  ///< Change across this minute; -1 if unknown.

    // -- Power ----------------------------------------------------------------

    int32_t  battPctX10 = -1;  ///< BATTERY_LEVEL x10.
    int8_t   charging   = -1;  ///< 1 charging, 0 not, -1 unknown.
    int8_t   usb        = -1;  ///< 1 cable present, 0 not, -1 unknown.
    int32_t  battMv     = -1;  ///< Millivolts.
    int32_t  battMaX10  = -1;  ///< Instantaneous current x10, signed.
    int32_t  battAvgMaX10 = -1;///< Averaged current x10, signed. The number the
                               ///< overnight cost is computed from.
    int32_t  battMah    = -1;  ///< Remaining capacity, mAh.

    // -- The loop itself ------------------------------------------------------

    /// Times getMessage() returned within the minute. A service that is
    /// supposed to sleep to its next deadline and instead wakes 40 000 times is
    /// a battery fault visible nowhere else.
    int32_t  wakes      = -1;
    int32_t  msgs       = -1;  ///< Messages actually dequeued.
};

/**
 * @brief Append-only writer for the log described above.
 *
 * Opens, writes, flushes and closes around every row rather than holding a
 * handle across the night. That is the FwDump discipline and it is the right
 * trade here for the same reason: the writer is interrupted by a USB
 * connection that terminates the process without warning, and the cost is one
 * open per minute against losing the night.
 */
class Log
{
public:
    explicit Log(const SDK::Kernel &kernel, const char *path = kLogPath);

    /**
     * @brief Write the header if the file is new, then a run-start row.
     *
     * @param uptimeMs  Uptime at launch.
     * @param wallUtc   time(nullptr) at launch, or -1 if unreadable.
     * @param hrMode    The HR mode this launch is running, verbatim from the
     *                  config, so a night's rows carry the setting that
     *                  produced them instead of relying on a note somewhere.
     * @retval true     The rows reached storage.
     */
    bool begin(uint32_t uptimeMs, int64_t wallUtc, const char *hrMode);

    /// @retval true The row reached storage.
    bool write(const MinuteRow &row);

    /// Rows this process failed to write. Reported on screen-less hardware via
    /// the log, and the only way to notice storage filling up.
    uint32_t failures() const { return mFailures; }

    /// Bytes this process has appended. Cross-checked against the file size to
    /// measure sustained append throughput on the user volume.
    uint64_t bytesWritten() const { return mBytes; }

private:
    /// Both record kinds are formatted into this and appended in one write.
    /// Sized from the widest possible row: every column at its longest
    /// negative-integer form plus separators, rounded up.
    static constexpr size_t kLineMax = 512;

    bool append(const char *text, size_t len);

    const SDK::Kernel &mKernel;
    const char        *mPath;
    uint32_t           mFailures = 0;
    uint64_t           mBytes    = 0;
};

} // namespace Probe

#endif // PROBELOG_HPP
