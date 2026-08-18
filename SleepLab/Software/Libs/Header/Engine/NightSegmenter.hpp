/**
 ******************************************************************************
 * @file    NightSegmenter.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   When does a night start, and when has it ended?
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O.
 *
 * A recorder that runs for the device's whole life needs to know which stretch
 * of it was a night. The answer is a small state machine over three things:
 * the time of day, whether the watch is worn, and whether the wearer is still.
 *
 * ---------------------------------------------------------------------------
 * Windows cross midnight, and that is the interesting case
 *
 * A bedtime window of 21:00-11:00 is not `start <= t < end` -- that predicate
 * is false for every minute of it. Every comparison here goes through
 * `inWindow()`, which handles both orientations, and the tests for it were
 * written before the rest of this class.
 *
 * ---------------------------------------------------------------------------
 * Opening and closing are deliberately asymmetric
 *
 * Opening is conservative: inside the window, worn, and *sustained* stillness.
 * A false open costs a spurious night in the history and, worse, a spurious
 * baseline sample.
 *
 * Closing is eager on the signals that unambiguously mean "up" -- steps, or
 * sustained activity -- but only after a minimum duration, so a trip to the
 * bathroom at 01:00 does not end the night and start a second one. Leaving the
 * window closes it unconditionally: a session still open at 11:00 is a bug or
 * a forgotten watch, and either way it has stopped being a night.
 *
 * ---------------------------------------------------------------------------
 * Time of day is the one thing here that reads the wall clock
 *
 * It has to: "bedtime" is a time of day and uptime does not have one. But the
 * wall clock can jump, so a jump is reported rather than absorbed -- see
 * `Update::clockJumped`. Every *duration* this class works in is counted in
 * epochs, which are uptime-derived, so a clock jump changes which window the
 * session thinks it is in and never changes how long it thinks it has run.
 *
 ******************************************************************************
 */

#ifndef ENGINE_NIGHTSEGMENTER_HPP
#define ENGINE_NIGHTSEGMENTER_HPP

#include <cstdint>

namespace Engine
{

/// Minutes in a day, for window arithmetic.
constexpr int16_t kMinutesPerDay = 24 * 60;

/**
 * @brief Where a night begins and ends.
 *
 * Durations are in *scoring epochs* (one minute each), not in wall-clock
 * minutes, so nothing here can be broken by the clock moving.
 */
struct SegmenterConfig
{
    /// Earliest a night may open, local minutes past midnight. Default 21:00.
    int16_t windowStartMin = 21 * 60;
    /// Latest a night may stay open, local minutes past midnight. Default
    /// 11:00 -- the window crosses midnight, which is the normal case.
    int16_t windowEndMin   = 11 * 60;

    /// Consecutive still, worn epochs required to open a night.
    ///
    /// Fifteen minutes. Long enough that sitting still watching something does
    /// not open a night, short enough that it costs at most fifteen minutes of
    /// a real one -- and those minutes are recovered anyway, because the
    /// session is backdated to the start of the run that opened it.
    uint16_t stillnessToOpenMin = 15;

    /// Consecutive active epochs required to close a night.
    ///
    /// Ten minutes. A turn-over is one epoch; getting up is ten.
    uint16_t activityToCloseMin = 10;

    /// Steps within one epoch that close a night outright, once the minimum
    /// duration has passed.
    ///
    /// Steps are the least ambiguous out-of-bed signal there is -- a sleeping
    /// wrist does not accumulate them -- so this is a far lower bar than
    /// sustained activity. 20 in a minute is walking, not a twitch the step
    /// counter miscounted.
    uint16_t stepsToCloseInEpoch = 20;

    /// A night shorter than this is discarded rather than reported.
    ///
    /// Ninety minutes. Below it there is nothing worth summarising and a
    /// baseline built from such nights would be noise. It is also the guard
    /// that stops a bathroom trip splitting one night into two.
    uint16_t minSessionMin = 90;

    /// Counts per scoring epoch at or below which an epoch counts as still,
    /// for the purpose of opening a night.
    ///
    /// TODO: set from a diary-validated recording -- the value should sit above
    /// a settled sleeper's epochs and below someone lying awake reading. 60 is
    /// a guess against EpochCounter's scale, deliberately looser than
    /// NightAnalyser::kMovementFloor because opening a night should tolerate
    /// the shuffling that precedes sleep.
    uint32_t stillnessCountMax = 60;

    /// Counts per scoring epoch above which an epoch counts as active, for the
    /// purpose of closing a night. Deliberately well above
    /// `stillnessCountMax`, so the two do not chatter around one boundary.
    /// TODO: same recording.
    uint32_t activityCountMin = 250;

    /// Longest a session may run, in scoring epochs. Sixteen hours.
    ///
    /// Not a threshold about sleep -- it is the backstop that makes the other
    /// rules' failure survivable. Leaving the window is what ends a long session,
    /// and that needs a readable wall clock, which an open session deliberately
    /// does not require: ending a night because the clock became unreadable would
    /// lose real data over something the app does not control. So a session that
    /// outlives its clock had nothing to close it at all -- it ran until
    /// `mSessionEpochs` went round a uint16 at 45 days, after which it was below
    /// `minSessionMin` and could no longer close on activity either, while the CSV
    /// grew without bound.
    ///
    /// Sixteen hours is `kMaxScoringEpochs`, which is where the engine stops
    /// scoring, so a session can no longer outlive the array that holds it. A
    /// night this long is a data-quality problem either way; the point is that it
    /// is reported as one rather than never reported at all.
    uint16_t maxSessionMin = 16 * 60;
};

/**
 * @brief Whether @p t is inside the window [start, end), which may wrap.
 *
 * Free function and tested directly: it is the one piece of arithmetic here
 * that is easy to get wrong and impossible to notice being wrong, because a
 * window that never opens looks exactly like a user who never went to bed.
 */
bool inWindow(int16_t t, int16_t startMin, int16_t endMin);

/**
 * @brief Tracks one session at a time.
 */
class NightSegmenter
{
public:
    enum class State : uint8_t {
        Idle = 0,   ///< No session. Watching for one to open.
        Open = 1,   ///< A session is running.
    };

    /// What happened on this update.
    enum class Event : uint8_t {
        None    = 0,
        Opened  = 1, ///< A session just opened. See Update::backdateEpochs.
        Closed  = 2, ///< A session just closed and is long enough to report.
        /// A session just closed and was **too short to report**. The caller
        /// should discard it rather than summarise it -- a 40-minute "night"
        /// in the history would corrupt the baseline as surely as a wrong one.
        Discarded = 3,
    };

    struct Update
    {
        Event   event  = Event::None;
        State   state  = State::Idle;
        /// On Opened: how many epochs back the session actually began.
        ///
        /// The session opens once stillness has been *sustained*, which by
        /// definition is after it started. Backdating recovers those epochs
        /// rather than throwing away the quarter hour in which the wearer was
        /// in fact already settling.
        uint16_t backdateEpochs = 0;
        /// On Closed/Discarded: the session's length in epochs, backdating
        /// included.
        uint16_t sessionEpochs  = 0;
        /// The wall clock moved by more than this update's own step. Reported,
        /// never absorbed: it marks the night interrupted and nothing here
        /// tries to correct for it.
        bool     clockJumped    = false;
    };

    explicit NightSegmenter(const SegmenterConfig &cfg = SegmenterConfig{})
        : mCfg(cfg) {}

    /**
     * @brief Feed one scoring epoch.
     *
     * @param localMin   Local minutes past midnight, 0..1439, or -1 if the
     *                   wall clock is unreadable. With no clock a session can
     *                   never *open* -- "bedtime" has no meaning without one --
     *                   but an open session keeps running, because it is
     *                   already known to be a night and ending it would lose
     *                   real data over a clock the app does not control.
     * @param worn       Whether the epoch met the worn floor.
     * @param count      The epoch's activity count.
     * @param stepDelta  Steps in the epoch, or Engine::kAbsent if unknown.
     */
    Update update(int16_t localMin, bool worn, uint32_t count, int32_t stepDelta);

    /**
     * @brief Close an open session immediately, whatever its state.
     *
     * For shutdown: the kernel is stopping the app -- almost always because
     * the USB cable went in -- and a session left open is a night that never
     * gets written. Returns Closed or Discarded exactly as `update()` would.
     */
    Update finish();

    /**
     * @brief Re-enter an open session after a service restart.
     *
     * The recorder persists enough to know a night was in progress; this puts
     * the segmenter back into the state that matches, so a resumed night
     * continues rather than opening a second one alongside it.
     *
     * @param epochsSoFar Epochs the session already contains.
     */
    void resumeOpen(uint16_t epochsSoFar);

    State state()  const { return mState; }
    /// Epochs in the session currently open, backdating included.
    uint16_t sessionEpochs() const { return mSessionEpochs; }

    const SegmenterConfig &config() const { return mCfg; }

private:
    /// @param clockJumped Carried into the returned Update. A jump that also
    ///        closes the session must still be reported -- the clock moving
    ///        past the end of the window is precisely that case.
    Update closeNow(bool clockJumped);

    SegmenterConfig mCfg;
    State    mState         = State::Idle;
    uint16_t mStillRun      = 0;  ///< Consecutive still worn epochs, while Idle.
    uint16_t mActiveRun     = 0;  ///< Consecutive active epochs, while Open.
    uint16_t mSessionEpochs = 0;
    int16_t  mLastLocalMin  = -1;
};

} // namespace Engine

#endif // ENGINE_NIGHTSEGMENTER_HPP
