#ifndef HAPTICLAB_PROTOCOL_HPP
#define HAPTICLAB_PROTOCOL_HPP

#include <cstdint>

#include "Patterns.hpp"
#include "Records.hpp"
#include "Schedule.hpp"

/// The protocol, as a state machine that owns no clock, no screen and no file.
/// It is handed ticks and button events and says what should happen; the GUI
/// half does it. Why the protocol is this shape: HapticLab/README.md.
namespace Haptic {

enum class Stage : uint8_t {
    Idle = 0,      ///< Waiting to be started.
    Calibration,   ///< Proving the wearer knows which button is which.
    Learn,         ///< Names a pattern and replays it on demand.
    TrialWait,     ///< The inter-trial interval.
    TrialPlaying,  ///< The presentation window, the same length every trial.
    TrialAnswer,   ///< The response window.
    TrialFeedback, ///< Training only.
    Rest,          ///< Between blocks.
    Done,
    Aborted,
    Count,
};

/// `EventButton::Id` as this app uses them. The mapping never changes within a
/// run, and every trial records it anyway: a mapping that was supposed to be
/// fixed and was not is a bug only the data would show.
constexpr uint8_t kButtonSlot0  = 0; ///< SW1, top left.
constexpr uint8_t kButtonSlot1  = 1; ///< SW2, top right.
constexpr uint8_t kButtonSlot2  = 2; ///< SW3, bottom left.
constexpr uint8_t kButtonNotFelt = 3; ///< SW4, bottom right.
constexpr uint8_t kButtonCount   = 4;
constexpr uint8_t kNoButton      = 0xFF;
constexpr uint16_t kNoTrial      = 0xFFFF;

/// `EventButton::Event::CLICK`, which is the only event code this app acts on:
/// a hold is unavailable while music control is on, and `LONG_PRESS` is not
/// forwarded by the SDK's TouchGFX port at all.
constexpr uint8_t kEventClick = 2;

struct Timing {
    /// The same for every pattern in the set, so how long the screen says
    /// PLAYING leaks nothing about which pattern was played. Derived from the
    /// longest modelled pattern rather than chosen.
    uint32_t presentationMs;
    uint32_t interTrialMs;
    uint32_t feedbackMs;
    uint32_t responseTimeoutMs;

    /// Whether these are timings at all. A default-constructed `Timing` is all
    /// zeros, and a session run on one does not fail: every stage elapses on
    /// the next tick, so each trial plays and is dismissed inside a single
    /// frame and records a response of `none`. Run `20260915T025630` did
    /// exactly that, stepping through trials at a metronomic 398 ms, which is
    /// four stages at the GUI's 10 fps, until the wearer abandoned it. A
    /// session that cannot be timed must refuse to start rather than produce a
    /// full set of rows that look like a wearer who felt nothing.
    bool usable() const
    {
        return presentationMs > 0 && interTrialMs > 0 && feedbackMs > 0 && responseTimeoutMs > 0;
    }
};

/// Margin on top of the longest modelled pattern, so a pattern that runs a
/// little longer than modelled still finishes inside the window.
constexpr uint32_t kPresentationMarginMs = 400;

/// Long enough that the wrist is not still ringing from the last presentation.
constexpr uint32_t kInterTrialMs = 3000;

constexpr uint32_t kFeedbackMs         = 1500;
constexpr uint32_t kResponseTimeoutMs  = 8000;

inline Timing timingFor(const Pattern patterns[], uint8_t count, const EffectDurations &durations)
{
    Timing timing {};
    timing.presentationMs    = longestModelledMs(patterns, count, durations) + kPresentationMarginMs;
    timing.interTrialMs      = kInterTrialMs;
    timing.feedbackMs        = kFeedbackMs;
    timing.responseTimeoutMs = kResponseTimeoutMs;
    return timing;
}

/// What the caller should do after handing the session a tick or an event.
struct Step {
    bool playPattern = false;
    bool recordTrial = false;
    bool redraw      = false;
    bool finished    = false;
    /// Index into the candidate set; meaningful when `playPattern`.
    uint8_t pattern = 0xFF;
    /// The trial this play belongs to, or `kNoTrial` for a learning replay.
    uint16_t    trial = kNoTrial;
    TrialRecord record {};
};

inline const char *stageName(Stage stage)
{
    switch (stage) {
    case Stage::Idle:          return "idle";
    case Stage::Calibration:   return "calibration";
    case Stage::Learn:         return "learn";
    case Stage::TrialWait:     return "wait";
    case Stage::TrialPlaying:  return "playing";
    case Stage::TrialAnswer:   return "answer";
    case Stage::TrialFeedback: return "feedback";
    case Stage::Rest:          return "rest";
    case Stage::Done:          return "done";
    case Stage::Aborted:       return "aborted";
    case Stage::Count:         break;
    }
    return "unknown";
}

class Session
{
public:
    Session(const Plan &plan, const Pattern patterns[], const Timing &timing)
        : mPlan(plan)
        , mPatterns(patterns)
        , mTiming(timing)
    {}

    Stage    stage() const { return mStage; }
    uint16_t trialIndex() const { return mTrial; }
    uint8_t  block() const { return mBlock; }
    uint8_t  learnIndex() const { return mLearnIndex; }
    uint8_t  calibrationMask() const { return mCalibrationMask; }
    bool     calibrated() const { return mCalibrationMask == (1u << kButtonCount) - 1u; }
    uint8_t  lastCalibrated() const { return mLastCalibrated; }

    /// Which trial of the current block is next, counting from one, for the
    /// rest screen. Never shown on a trial screen: progress on the screen the
    /// wearer is answering on is one more thing to read instead of answering.
    uint16_t trialInBlock() const
    {
        return mBlock < mPlan.blockCount
                   ? static_cast<uint16_t>(mTrial - mPlan.blocks[mBlock].firstTrial)
                   : 0;
    }

    uint16_t blockTrialCount() const
    {
        return mBlock < mPlan.blockCount ? mPlan.blocks[mBlock].trialCount : 0;
    }

    BlockKind blockKind() const
    {
        return mBlock < mPlan.blockCount ? mPlan.blocks[mBlock].kind : BlockKind::Test;
    }

    /// The three on the screen, in the order they are drawn, which does not
    /// depend on which of them was presented.
    const Triple &offered() const { return mPlan.triples[mPlan.trials[mTrial].triple]; }

    /// What came back from the last presentation, which the feedback screen
    /// draws and nothing else reads.
    bool     lastWasCorrect() const { return mLastCorrect; }
    bool     lastWasMiss() const { return mLastMiss; }
    Response lastResponse() const { return mLastResponse; }
    uint8_t  lastPresented() const { return mLastPresented; }

    Step begin(uint32_t nowMs)
    {
        Step step;
        if (mStage != Stage::Idle) {
            return step;
        }
        if (!mTiming.usable()) {
            mStage      = Stage::Aborted;
            step.redraw = true;
            return step;
        }
        mStage      = Stage::Calibration;
        mStageStart = nowMs;
        step.redraw = true;
        return step;
    }

    Step abort(uint32_t nowMs)
    {
        Step step;
        mStage        = Stage::Aborted;
        mStageStart   = nowMs;
        step.redraw   = true;
        step.finished = true;
        return step;
    }

    /// Records when the frame that opened the response window actually reached
    /// the panel, which is what the wearer reacted to.
    void noteFramePushed(uint32_t nowMs)
    {
        if (mStage == Stage::TrialAnswer && mWindowDrawn == 0) {
            mWindowDrawn = nowMs != 0 ? nowMs : 1;
        }
    }

    /// The two clocks only the caller can know: when it asked the kernel to
    /// play, and when the kernel said it had accepted. Only meaningful for a
    /// trial presentation; a learning replay has no trial to attach them to.
    void notePlayClocks(uint32_t tickBeforeSend, uint32_t tickAtAccepted, uint32_t wallClock)
    {
        mTickBeforeSend = tickBeforeSend;
        mTickAtAccepted = tickAtAccepted;
        mWallAtSend     = wallClock;
        mModelledEnd    = tickBeforeSend + modelledDurationMs(presentedPatternRef(), mDurations);
    }

    void setDurations(const EffectDurations &durations) { mDurations = durations; }

    /// The timing has to be set after construction, because it is derived from
    /// the pattern set and the measured durations and neither is known when the
    /// GUI's members are built. Taking a copy at construction is what put a
    /// whole session on zeros; `begin` is what stops that being silent.
    void setTiming(const Timing &timing) { mTiming = timing; }

    const Timing &timing() const { return mTiming; }

    Step onTick(uint32_t nowMs)
    {
        Step step;
        switch (mStage) {
        case Stage::TrialWait:
            if (sinceTick(mStageStart, nowMs) >= static_cast<int32_t>(mTiming.interTrialMs)) {
                enterPlaying(nowMs, step);
            }
            break;

        case Stage::TrialPlaying:
            if (sinceTick(mPlayAsked, nowMs) >= static_cast<int32_t>(mTiming.presentationMs)) {
                mStage       = Stage::TrialAnswer;
                mStageStart  = nowMs;
                mWindowDue   = mPlayAsked + mTiming.presentationMs;
                mWindowDrawn = 0;
                step.redraw  = true;
            }
            break;

        case Stage::TrialAnswer:
            if (sinceTick(mWindowDue, nowMs) >= static_cast<int32_t>(mTiming.responseTimeoutMs)) {
                takeResponse(Response::None, kNoButton, 0, nowMs, step);
            }
            break;

        case Stage::TrialFeedback:
            if (sinceTick(mStageStart, nowMs) >= static_cast<int32_t>(mTiming.feedbackMs)) {
                advanceTrial(nowMs, step);
            }
            break;

        case Stage::Idle:
        case Stage::Calibration:
        case Stage::Learn:
        case Stage::Rest:
        case Stage::Done:
        case Stage::Aborted:
        case Stage::Count:
            break;
        }
        return step;
    }

    /// Returns what to do, and reports through `disposition` why an event was
    /// not taken as a response, so `events.csv` can hold every press.
    Step onButton(uint8_t id, uint8_t event, uint32_t buttonTimestamp, uint32_t nowMs,
                  EventDisposition &disposition)
    {
        Step step;
        disposition = EventDisposition::Unmapped;

        if (event != kEventClick) {
            disposition = EventDisposition::NotAClick;
            return step;
        }

        switch (mStage) {
        case Stage::Idle:
            if (id == kButtonSlot1) {
                disposition = EventDisposition::TakenAsResponse;
                return begin(nowMs);
            }
            break;

        case Stage::Calibration: {
            const uint8_t before = mCalibrationMask;
            if (id < kButtonCount) {
                mCalibrationMask = static_cast<uint8_t>(mCalibrationMask | (1u << id));
                mLastCalibrated  = id;
                disposition      = EventDisposition::TakenAsResponse;
                step.redraw      = true;
            }
            // The press that completes the set does not also start the run:
            // the wearer has to see all four lit and then ask to go on.
            const bool completedNow = before != mCalibrationMask && calibrated();
            if (calibrated() && !completedNow && id == kButtonSlot1) {
                enterLearn(nowMs, step);
            }
            break;
        }

        case Stage::Learn:
            disposition = EventDisposition::TakenAsResponse;
            if (id == kButtonSlot1) {
                step.playPattern = true;
                step.pattern     = mLearnIndex;
                step.trial       = kNoTrial;
                step.redraw      = true;
            } else if (id == kButtonSlot2) {
                mLearnIndex = static_cast<uint8_t>((mLearnIndex + 1) % mPlan.patternCount);
                step.redraw = true;
            } else if (id == kButtonSlot0) {
                mLearnIndex = static_cast<uint8_t>(
                    (mLearnIndex + mPlan.patternCount - 1) % mPlan.patternCount);
                step.redraw = true;
            } else if (id == kButtonNotFelt) {
                startBlock(0, nowMs, step);
            }
            break;

        case Stage::TrialWait:
            disposition = EventDisposition::BetweenTrials;
            break;

        case Stage::TrialPlaying:
            // Evidence the wearer decided before the pattern finished, which is
            // a real result about that pattern and is invisible if only
            // accepted responses are kept.
            disposition = EventDisposition::DuringPlayback;
            break;

        case Stage::TrialAnswer: {
            const Response response = responseFor(id);
            if (response == Response::None) {
                break;
            }
            disposition = EventDisposition::TakenAsResponse;
            takeResponse(response, id, buttonTimestamp, nowMs, step);
            break;
        }

        case Stage::TrialFeedback:
            disposition = EventDisposition::AfterResponse;
            break;

        case Stage::Rest:
            disposition = EventDisposition::TakenAsResponse;
            if (id == kButtonSlot1) {
                startBlock(mBlock, nowMs, step);
            } else if (id == kButtonNotFelt) {
                return abort(nowMs);
            }
            break;

        case Stage::Done:
        case Stage::Aborted:
        case Stage::Count:
            break;
        }
        return step;
    }

private:
    const Pattern &presentedPatternRef() const
    {
        return mPatterns[presentedPattern(mPlan, mTrial)];
    }

    static Response responseFor(uint8_t id)
    {
        switch (id) {
        case kButtonSlot0:   return Response::Slot0;
        case kButtonSlot1:   return Response::Slot1;
        case kButtonSlot2:   return Response::Slot2;
        case kButtonNotFelt: return Response::NotFelt;
        default:             return Response::None;
        }
    }

    void enterLearn(uint32_t nowMs, Step &step)
    {
        mStage      = Stage::Learn;
        mStageStart = nowMs;
        mLearnIndex = 0;
        step.redraw = true;
    }

    void startBlock(uint8_t block, uint32_t nowMs, Step &step)
    {
        if (block >= mPlan.blockCount) {
            mStage        = Stage::Done;
            mStageStart   = nowMs;
            step.redraw   = true;
            step.finished = true;
            return;
        }
        mBlock      = block;
        mTrial      = mPlan.blocks[block].firstTrial;
        mStage      = Stage::TrialWait;
        mStageStart = nowMs;
        step.redraw = true;
    }

    void enterPlaying(uint32_t nowMs, Step &step)
    {
        mStage           = Stage::TrialPlaying;
        mStageStart      = nowMs;
        mPlayAsked       = nowMs;
        mTickBeforeSend  = nowMs;
        mTickAtAccepted  = 0;
        mModelledEnd     = 0;
        mWindowDue       = 0;
        mWindowDrawn     = 0;
        step.playPattern = true;
        step.pattern     = presentedPattern(mPlan, mTrial);
        step.trial       = mTrial;
        step.redraw      = true;
    }

    void takeResponse(Response response, uint8_t button, uint32_t buttonTimestamp, uint32_t nowMs,
                      Step &step)
    {
        const Triple &triple    = offered();
        const uint8_t presented = presentedPattern(mPlan, mTrial);
        const Pattern &pattern  = mPatterns[presented];

        TrialRecord &row = step.record;
        row              = TrialRecord {};
        row.trialInRun   = mTrial;
        row.trialInBlock = trialInBlock();
        row.block        = mBlock;
        row.blockKind    = blockKind();
        row.presented    = presented;
        for (uint8_t i = 0; i < 3; i++) {
            row.offered[i]       = triple.member[i];
            row.buttonForSlot[i] = static_cast<uint8_t>(kButtonSlot0 + i);
        }
        row.missButton     = kButtonNotFelt;
        row.response       = response;
        row.responseButton = button;
        row.miss           = response == Response::NotFelt;

        const int8_t presentedSlot = slotOf(row.offered, presented);
        row.correct = response != Response::None && response != Response::NotFelt &&
                      presentedSlot >= 0 &&
                      static_cast<uint8_t>(response) ==
                          static_cast<uint8_t>(Response::Slot0) + static_cast<uint8_t>(presentedSlot);

        row.tickBeforeSend      = mTickBeforeSend;
        row.tickAtAccepted      = mTickAtAccepted;
        row.modelledEndTick     = mModelledEnd;
        row.windowOpenDueTick   = mWindowDue;
        row.windowOpenDrawnTick = mWindowDrawn;
        row.responseTimestamp   = buttonTimestamp;
        row.wallClockAtSend     = mWallAtSend;
        // From the drawn frame when there is one: the wearer cannot answer a
        // window they have not seen open.
        const uint32_t reference = mWindowDrawn != 0 ? mWindowDrawn : mWindowDue;
        row.reactionMs = response == Response::None ? 0 : sinceTick(reference, nowMs);

        row.family             = pattern.family;
        row.pulses             = pattern.pulses;
        row.rhythm             = pattern.rhythm;
        row.amplitude          = pattern.amplitude;
        row.length             = pattern.length;
        row.noteCount          = pattern.noteCount;
        for (uint8_t i = 0; i < pattern.noteCount && i < kMaxNotes; i++) {
            row.notes[i] = pattern.notes[i];
        }
        row.modelledDurationMs = modelledDurationMs(pattern, mDurations);

        mLastCorrect      = row.correct;
        mLastMiss         = row.miss;
        mLastResponse     = response;
        mLastPresented    = presented;
        step.recordTrial  = true;
        step.redraw       = true;

        if (blockKind() == BlockKind::Training) {
            mStage      = Stage::TrialFeedback;
            mStageStart = nowMs;
        } else {
            advanceTrial(nowMs, step);
        }
    }

    void advanceTrial(uint32_t nowMs, Step &step)
    {
        const Block &block = mPlan.blocks[mBlock];
        const uint16_t last = static_cast<uint16_t>(block.firstTrial + block.trialCount);
        if (static_cast<uint16_t>(mTrial + 1) < last) {
            mTrial++;
            mStage      = Stage::TrialWait;
            mStageStart = nowMs;
            step.redraw = true;
            return;
        }

        if (static_cast<uint8_t>(mBlock + 1) < mPlan.blockCount) {
            mBlock++;
            mTrial      = mPlan.blocks[mBlock].firstTrial;
            mStage      = Stage::Rest;
            mStageStart = nowMs;
            step.redraw = true;
            return;
        }

        mStage        = Stage::Done;
        mStageStart   = nowMs;
        step.redraw   = true;
        step.finished = true;
    }

    const Plan     &mPlan;
    const Pattern  *mPatterns;
    Timing          mTiming;
    EffectDurations mDurations = EffectDurations::nominal();

    Stage    mStage      = Stage::Idle;
    uint32_t mStageStart = 0;
    uint16_t mTrial      = 0;
    uint8_t  mBlock      = 0;
    uint8_t  mLearnIndex = 0;

    uint8_t mCalibrationMask = 0;
    uint8_t mLastCalibrated  = kNoButton;

    uint32_t mPlayAsked      = 0;
    uint32_t mTickBeforeSend = 0;
    uint32_t mTickAtAccepted = 0;
    uint32_t mModelledEnd    = 0;
    uint32_t mWallAtSend     = 0;
    uint32_t mWindowDue      = 0;
    uint32_t mWindowDrawn    = 0;

    bool     mLastCorrect   = false;
    bool     mLastMiss      = false;
    Response mLastResponse  = Response::None;
    uint8_t  mLastPresented = 0;
};

} // namespace Haptic

#endif // HAPTICLAB_PROTOCOL_HPP
