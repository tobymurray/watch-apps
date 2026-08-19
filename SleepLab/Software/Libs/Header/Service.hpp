/**
 ******************************************************************************
 * @file    Service.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The recorder: sensors in, epochs to disk, a night out.
 ******************************************************************************
 *
 * Autostart, and it never exits. Not when the GUI closes -- the whole point of
 * autostart is that recording continues unobserved, and the report is written
 * as a file whether anyone opens the app or not.
 *
 * ---------------------------------------------------------------------------
 * The loop sleeps to its next due work
 *
 * `getMessage()` is called with a timeout sized to the nearest of: the next
 * epoch boundary, the next heart-rate duty transition, and the alarm deadline.
 * This service runs for the device's whole life, so a polling loop is a
 * permanent battery tax -- the same reasoning `Alarm`, `Timer` and
 * `MapManager` all record, and `MapManager` measured: with nothing to wake it
 * early, a service does exactly what its wait tells it to.
 *
 * ---------------------------------------------------------------------------
 * Two epoch lengths
 *
 * Recording epochs are 30 s and are what reaches the CSV. Scoring epochs are
 * 60 s -- pairs of recording epochs, summed -- and are what the engine sees,
 * because that is the epoch length Cole-Kripke's coefficients were derived for
 * and coefficients do not transfer across epoch lengths.
 *
 * ---------------------------------------------------------------------------
 * Backdating, and the pre-roll ring
 *
 * A night opens once stillness has been *sustained*, which is by definition
 * some minutes after it began. The segmenter reports how far to backdate, and
 * those minutes have to come from somewhere -- so a ring of the most recent
 * recording epochs is kept even while idle, and flushed into the night's CSV
 * the moment it opens. Without it, every night would lose its first quarter
 * hour and every onset latency would be wrong by the same amount.
 *
 * ---------------------------------------------------------------------------
 * Restart survival
 *
 * Plugging in USB terminates every running app; autostart relaunches on
 * unplug. On start the service looks for `night_state.txt`, and a night in
 * progress is resumed into rather than replaced. What cannot be recovered is
 * the scoring array -- the epochs written before the restart are on disk but
 * not in RAM -- so a resumed night is scored from the epochs recorded *since*
 * the restart, its length is taken from the state file, and it is marked
 * interrupted. Re-reading the CSV back into the scoring array would be
 * possible and is deliberately not done: it would put a parser for this app's
 * own output on the recording path, and a night that resumed is already
 * flagged as one whose numbers are not clean.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstdint>

#include "SDK/Glance/GlanceControl.hpp"
#include "SDK/HomeWidget/HomeWidget.hpp"
#include "SDK/Kernel/Kernel.hpp"
#include "SDK/SensorLayer/SensorConnection.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"

#include "Engine/BaselineStore.hpp"
#include "Engine/Epoch.hpp"
#include "Engine/EpochCounter.hpp"
#include "Engine/NightSegmenter.hpp"
#include "Engine/NightSummary.hpp"
#include "Engine/RestfulnessBand.hpp"
#include "Engine/SleepWakeScorer.hpp"

#include "Commands.hpp"
#include "Diag.hpp"
#include "NightStore.hpp"
#include "RawRecorder.hpp"
#include "Settings.hpp"
#include "WallClock.hpp"

/**
 * @brief The recorder.
 *
 * Placement-new'd into static storage by the SDK's service entry point, so its
 * ~20 KB of arrays live in the app's 500 KB rather than on a 10 KB stack. No
 * allocation happens after `run()` starts.
 */
class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service();

    void run();

private:
    // -- Per-recording-epoch accumulators -------------------------------------

    /**
     * @brief Everything counted between two recording epochs.
     *
     * Held by value and reset wholesale. The sample path allocates nothing --
     * a platform requirement, and the difference between a service that
     * survives eight hours and one that fragments its heap into failure.
     */
    struct Accum
    {
        uint16_t motion    = 0;
        uint16_t sigMotion = 0;

        uint32_t hrSum     = 0;   ///< bpm x10, summed.
        uint16_t hrMinX10  = 0;
        uint16_t hrCount   = 0;
        bool     hrOptical  = false;
        bool     hrExternal = false;

        uint16_t touchN     = 0;
        uint16_t touchWornN = 0;
        uint8_t  touchEdges = 0;

        // -- Delivery, for the diagnostics columns ---------------------------
        uint16_t accBatches   = 0;
        uint16_t accMaxGapMs  = 0;
        float    hrTrustSum   = 0.0f;
        uint16_t hrTrustN     = 0;
        uint16_t hrexOptical  = 0;
        uint16_t hrexExternal = 0;
        uint16_t hrexUnknown  = 0;
        uint16_t wakes        = 0;
        uint16_t msgs         = 0;

        void reset() { *this = Accum{}; }
    };

    // -- Lifecycle ------------------------------------------------------------

    void connectSensors();
    /// Write the resolved-driver block to the diagnostic log, in the probe's own
    /// single-letter form. This is the one line that would have caught
    /// TOUCH_DETECT and SPO2 without spending a night on either.
    void logSensors();
    void disconnectSensors();
    /// Drive the heart-rate duty cycle. Returns ms to the next transition, or 0.
    uint32_t pumpHrDuty(uint32_t now);

    // -- Sample path ----------------------------------------------------------

    void onSensorData(uint16_t handle, SDK::Sensor::DataBatch &batch);

    // -- Epoch pipeline -------------------------------------------------------

    /// Close a 30 s recording epoch: build it, store it, fold it into the
    /// scoring epoch in progress.
    void closeRecordingEpoch(uint32_t now, uint32_t spanMs);

    /// Close a 60 s scoring epoch: push it to the night, drive the segmenter,
    /// and check the alarm.
    void closeScoringEpoch();

    /**
     * @brief Write the pre-roll ring into a night that has just opened, and
     *        pair it into the scoring array.
     *
     * @return Scoring epochs the ring was able to give back, which is normally
     *         all of them. The caller counts the shortfall, if any.
     */
    uint16_t flushPreRoll(uint16_t epochs);

    /// Fold one recording epoch into a scoring epoch under construction.
    ///
    /// One function rather than two, because the live path and the pre-roll
    /// replay must agree exactly: a night whose first quarter hour was summed by
    /// slightly different arithmetic from the rest of it would have a seam in
    /// it that nothing downstream could see.
    static void fold(const Engine::Epoch &e, Engine::ScoringInput &into,
                     uint8_t &halves, int32_t &steps);

    // -- Night lifecycle ------------------------------------------------------

    void openNight(uint16_t backdateScoringEpochs);
    void closeNight(bool discard);
    /// Raise the write-failure flag, and say so once on the volume.
    void noteWriteFailure(const char *where);

    // -- Alarm ----------------------------------------------------------------

    /// Fire if the smart window is open and the wearer looks awake, or if the
    /// hard deadline has arrived.
    void checkAlarm();
    /// @param why      Which path fired: "deadline" or "smart-window".
    /// @param atEpoch  The epoch whose verdict fired it, or -1 for the deadline.
    ///
    /// Both are recorded on the volume. An alarm is the one output whose silent
    /// failure is worse than its absence, and it used to leave no trace at all --
    /// so "it did not go off" and "it went off and you slept through it" were the
    /// same observation in the morning.
    void playAlarm(const char *why, int32_t atEpoch);

    // -- Glance and home widget -------------------------------------------------
    //
    // Both exist for the same reason: a sleep report is glanced at once, half
    // awake, and neither surface needs the app to be opened. `Utility` apps are
    // marked glance-capable regardless of type -- `una-app.cmake` passes
    // `-glance_capable` unconditionally -- so this costs no change of app type
    // and no loss of autostart.

    bool glanceConfig();
    void glanceCreate();
    void glanceRefresh();

    /// Claim, update or release the home-screen widget.
    ///
    /// Shown only in the morning: from a night closing until the next bedtime
    /// window opens. A widget still showing last Tuesday's efficiency in
    /// Thursday's afternoon is clutter, not information.
    void pumpWidget();

    // -- GUI ------------------------------------------------------------------

    void publishReport();
    void publishHistory();
    /// Fill the strip by downsampling the night onto kStripBuckets.
    void buildStrip(CustomMessage::SleepReportData &msg) const;

    // -- Collaborators --------------------------------------------------------

    SDK::Kernel        &mKernel;
    SleepLab::Settings  mSettings;
    SleepLab::NightStore mStore;
    SleepLab::RawRecorder mRaw;
    SleepLab::Diag        mDiag;

    /// Counters for the recording epoch in progress.
    Accum mAcc;

    Engine::EpochCounter   mCounter;
    Engine::NightSegmenter mSegmenter;
    Engine::BaselineStore  mBaseline;

    SDK::Glance::Form        mGlance;
    SDK::Glance::ControlText mGlanceTitle;
    SDK::Glance::ControlText mGlanceValue;
    SDK::Glance::ControlText mGlanceSub;
    bool                     mGlanceActive = false;

    SDK::HomeWidget mWidget;
    bool            mWidgetActive = false;
    /// Last text pushed to the widget, so an unchanged morning pushes once
    /// rather than once per epoch.
    char            mWidgetText[16] = {};

    SDK::Sensor::Connection mAccel;
    SDK::Sensor::Connection mTouch;
    SDK::Sensor::Connection mMotion;
    SDK::Sensor::Connection mActivity;
    SDK::Sensor::Connection mHr;
    SDK::Sensor::Connection mHrEx;
    SDK::Sensor::Connection mSteps;
    SDK::Sensor::Connection mBattLevel;
    SDK::Sensor::Connection mBattCharge;
    /// Voltage, current and remaining capacity. Subscribed only when the
    /// diagnostics setting is on -- it is the one extra channel the merge adds,
    /// and it is the only one that can answer a power question, because the
    /// percent gauge cannot (ledger row S18).
    SDK::Sensor::Connection mBattMetrics;

    // -- The night in RAM -----------------------------------------------------
    //
    // ~17 KB. It has to be held: Cole-Kripke needs look-ahead and Webster
    // needs whole-night passes, so a night cannot be scored as it goes.

    Engine::ScoringInput mScoring[Engine::kMaxScoringEpochs];
    Engine::Verdict      mVerdicts[Engine::kMaxScoringEpochs];
    Engine::Restfulness  mBand[Engine::kMaxScoringEpochs];
    size_t               mScoringCount = 0;

    /// The wall clock at each scoring epoch's *end*, or -1 where it was
    /// unreadable. Taken from the recording epoch that closed it.
    ///
    /// Recorded rather than computed. A time of day derived as "session start
    /// plus index times sixty" is only right while the array index and the
    /// session minute are the same number, and three ordinary things break that:
    /// epochs recorded before a restart, minutes that passed while the app was
    /// not running, and a loop that woke late and skipped a grid slot. Each of
    /// those puts a step in the map, and a step in the map moves every event
    /// after it earlier by the length of the step -- silently, in the one figure
    /// a person reads off the screen.
    ///
    /// 7.7 KB against ~20 KB of night already held in RAM, to make the answer
    /// structurally right instead of right until the next thing that skips a
    /// minute.
    int64_t              mScoringWallEnd[Engine::kMaxScoringEpochs] = {};

    /// Recording epochs kept while idle, so a backdated open can recover the
    /// minutes that were already part of the night. Two per scoring epoch.
    static constexpr size_t kPreRollEpochs = 2 * 30;
    Engine::Epoch mPreRoll[kPreRollEpochs];
    size_t        mPreRollNext  = 0;
    size_t        mPreRollCount = 0;

    // -- Scoring epoch in progress --------------------------------------------

    Engine::ScoringInput mPendingScore {};
    uint8_t              mPendingHalves = 0;
    /// Steps across the scoring epoch in progress, summed from its halves.
    /// The segmenter reads this; steps are the least ambiguous out-of-bed
    /// signal there is, because a sleeping wrist does not accumulate them.
    int32_t              mPendingSteps  = Engine::kAbsent;

    // -- Sticky sensor state --------------------------------------------------

    bool     mTouchLastWorn  = false;
    bool     mTouchLastValid = false;
    /// Whether TOUCH_DETECT has ever delivered a single sample.
    ///
    /// Distinct from mTouchLastValid only in that it is never reset. A worn
    /// sensor that said nothing all night is a broken sensor, and telling
    /// somebody their watch was not worn would send them to put on a watch
    /// they are already wearing.
    bool     mTouchEverReported = false;
    int64_t  mStepTotal      = Engine::kAbsent;
    int64_t  mStepAtEpoch    = Engine::kAbsent;
    int32_t  mBattPctX10     = Engine::kAbsent;
    bool     mCharging       = false;

    /// Latest BATTERY_METRICS reading, carried forward like the percent is: the
    /// channel publishes on its own period, not on the epoch grid.
    int32_t  mBattMv         = Engine::kAbsent;
    int32_t  mBattMaX10      = Engine::kAbsent;
    int32_t  mBattAvgMaX10   = Engine::kAbsent;
    int32_t  mBattMah        = Engine::kAbsent;

    /// Timestamp of the last accelerometer sample, for the worst-gap column. The
    /// counter tracks this internally too, and deliberately does not expose it --
    /// its copy is filter state and this one is a delivery measurement.
    uint32_t mAccLastTs      = 0;
    bool     mAccLastTsValid = false;

    /// Whether the previous recording epoch fell inside the bedtime window, so
    /// entering it can restart `watching.csv` and the file holds one window rather
    /// than every evening since the app was installed.
    bool     mWasInWindow    = false;

    // -- Night state ----------------------------------------------------------

    uint16_t mFlags        = 0;   ///< Engine::Interruption bits for this night.

    /// Whether a night is in progress, which is a question about the *session* and
    /// not about the file.
    ///
    /// `mStore.isOpen()` was standing in for this, and it is a different question:
    /// a night whose CSV could not be created has no open file and is still a
    /// night. Gating the scoring array on the file meant an unwritable volume
    /// produced no record and no report either -- the log said "recording to RAM
    /// only" and nothing was recorded at all.
    bool     mSessionOpen  = false;
    int64_t  mNightStartUtc = -1;
    /// Recording epochs the loop lost: it woke later than a whole epoch and
    /// advanced the grid past the slots it had missed. Real minutes of the night
    /// with no record, so they count towards time in bed and mark the night as
    /// having a gap -- what they must not do is shorten it. Counted in halves
    /// because that is the grid they were lost from.
    uint32_t mHalvesLost = 0;

    /// Scoring epochs that are part of the night but not in `mScoring`.
    ///
    /// Two causes, both meaning the same thing to the summary: epochs
    /// backdated into a night when it opened (they are in the CSV, but the
    /// pre-roll ring holds `Epoch`s rather than paired scoring inputs), and
    /// epochs recorded before a restart (on disk, not in RAM). Added to the
    /// scored count so time in bed is the night's real length.
    uint32_t mEpochsNotInArray = 0;

    // -- Alarm ----------------------------------------------------------------

    bool mAlarmFired = false;

    // -- Clocks ---------------------------------------------------------------

    /// The wall clock at the last recording epoch's end, so the scoring epoch it
    /// closes can be stamped with a clock that was read rather than derived.
    int64_t  mLastEpochWallUtc = -1;

    uint32_t mNextEpochAt  = 0;
    uint32_t mEpochOpenedAt = 0;
    bool     mHrDutyOn     = false;
    uint32_t mHrDutyNextAt = 0;

    // -- GUI ------------------------------------------------------------------

    bool mGuiStarted = false;

    /// The last completed night, kept so the morning report survives the
    /// service moving on. Cleared only when a new night closes.
    Engine::NightSummary mLastSummary {};
    /// Verdicts actually computed for the last night, which is `mLastSummary`'s
    /// epoch count only for a night that did not resume -- and is what bounds
    /// the strip, because there are no verdicts past it.
    size_t               mLastScoredCount = 0;
    bool                 mHaveReport   = false;
    bool                 mLastBandUsedHr = false;
    int16_t              mLastAsleepAtMin = -1;
    int16_t              mLastWokeAtMin   = -1;
};

#endif // SERVICE_HPP
