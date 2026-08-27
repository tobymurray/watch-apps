/**
 ******************************************************************************
 * @file    ProbePlan.cpp
 * @brief   The experiment itself, and the cursor that walks it.
 ******************************************************************************
 */

#include "ProbePlan.hpp"

namespace Probe
{

namespace
{

/// Auto-off long enough that Suite 1 never has to race it.
///
/// Ten minutes. The whole ladder is under two, so if the light goes out during
/// Suite 1 the cause is a kernel policy clamp and not this number, which is
/// itself a finding (Q6), and one that would be invisible if the value here were
/// merely "a bit longer than the step".
constexpr uint32_t kLadderHoldMs = 600000u;

/// What `sendMessage` is given, so `getResult()` means something.
///
/// This is NOT the auto-off. It is the completion-semaphore timeout, and it is
/// the entire instrument for Q1: with 0 here the call is fire-and-forget and the
/// result stays PENDING no matter what the kernel did, which is exactly why the
/// two shipped callers can tell us nothing. 250 ms is the same bound FwDump
/// puts on REQUEST_SYSTEM_INFO: long enough for a kernel that answers, short
/// enough that one that does not costs a blink rather than a stall.
constexpr uint32_t kSendTimeoutMs = 250u;

/// Let the light settle before sweeping registers at it.
constexpr uint32_t kSettleMs = 1500u;

/// Long enough to get a phone camera or a light meter onto a static frame
/// without hurrying, at every rung of the ladder.
constexpr uint32_t kMeterMs = 6000u;

/// Shorthand builders. Written out rather than inferred so the table below
/// reads as the experiment rather than as C++.
constexpr Step note(const char* label)
{
    return Step{Action::Note, Sender::Service, 0, 0, 0, 0, label, true};
}

constexpr Step set(uint8_t brightness, uint32_t autoOffMs, const char* label,
                   Sender sender = Sender::Service)
{
    return Step{Action::SetBacklight, sender,   brightness, autoOffMs,
                kSendTimeoutMs,       0,        label,      true};
}

constexpr Step sweep(const char* label)
{
    return Step{Action::Sweep, Sender::Service, 0, 0, 0, 0, label, true};
}

constexpr Step hold(uint32_t ms, const char* label)
{
    return Step{Action::Hold, Sender::Service, 0, 0, 0, ms, label, true};
}

constexpr Step observe(uint32_t ms, const char* label)
{
    return Step{Action::Observe, Sender::Service, 0, 0, 0, ms, label, false};
}

constexpr Step probeIids(const char* label)
{
    return Step{Action::ProbeIids, Sender::Service, 0, 0, 0, 0, label, true};
}

/**
 * The experiment.
 *
 * Three suites, run back to back, about four minutes end to end, which has to
 * fit inside "however long the watch is off the cable", because USB stops every
 * running app.
 *
 * ## Suite 1: the brightness ladder (Q2, Q9, Q11)
 *
 * The one that decides the investigation. Each rung sets a brightness, lets it
 * settle, sweeps the peripheral registers, and then holds a static frame long
 * enough to photograph or meter.
 *
 * The point is NOT to rediscover that `brightness` is inert; that is already
 * known from the device. The point is to record it at the *register* level. A
 * photograph showing no visible difference between 25 and 100 is weak evidence
 * (eyes are bad at this, and a memory LCD's appearance depends on ambient light
 * more than on its front-light). Two sweeps that are byte-identical across the
 * whole GPIO and RCC space are strong evidence, and they say something a
 * photograph cannot: that nothing downstream even *tried*.
 *
 * Descending 100 to 1 rather than ascending, so the first rung after dark is the
 * unambiguous one. If 100 does not visibly light the screen, the probe is broken
 * and there is no point reading the rest.
 *
 * The dark sweep is taken first and again at the end. The second one is not
 * ceremony: if it differs from the first, something drifted during the run and
 * every diff in between is suspect.
 *
 * ## Suite 2: what the timeout actually does (Q3, Q4, Q6)
 *
 * All human-observed, because no app can read the light back. Each step counts
 * milliseconds on screen so a phone video timestamps itself and the frame where
 * the light dies can be read off directly.
 *
 * `t = 0` is the interesting one. `IBacklight`'s header and the message's own
 * comment both say 0 disables auto-off; the simulator's mock starts a
 * zero-length timer and blanks within about 50 ms, i.e. the exact opposite. One
 * of those is wrong about the device and this step is what says which.
 *
 * ## Suite 3: context (Q1, Q7)
 *
 * The IID walk, and one request sent from the GUI process rather than the
 * service, since the message type sits directly below a block commented
 * "Display control (GUI only)" and both shipped callers happen to be services.
 */
constexpr Step kPlan[] = {
    //; Suite 1 -------------------------------------------------------------
    note("SUITE 1. Brightness ladder: does anything change per level"),

    set(0, kLadderHoldMs, "dark baseline"),
    hold(kSettleMs, "settle"),
    sweep("dark"),
    hold(kMeterMs, "meter dark"),

    set(100, kLadderHoldMs, "b100"),
    hold(kSettleMs, "settle"),
    sweep("lit_b100"),
    hold(kMeterMs, "meter b100"),

    set(75, kLadderHoldMs, "b75"),
    hold(kSettleMs, "settle"),
    sweep("lit_b075"),
    hold(kMeterMs, "meter b75"),

    set(50, kLadderHoldMs, "b50"),
    hold(kSettleMs, "settle"),
    sweep("lit_b050"),
    hold(kMeterMs, "meter b50"),

    set(25, kLadderHoldMs, "b25"),
    hold(kSettleMs, "settle"),
    sweep("lit_b025"),
    hold(kMeterMs, "meter b25"),

    set(10, kLadderHoldMs, "b10"),
    hold(kSettleMs, "settle"),
    sweep("lit_b010"),
    hold(kMeterMs, "meter b10"),

    set(1, kLadderHoldMs, "b1"),
    hold(kSettleMs, "settle"),
    sweep("lit_b001"),
    hold(kMeterMs, "meter b1"),

    // Back to dark, and swept again. A dark sweep that no longer matches the
    // first one invalidates every diff above it.
    set(0, kLadderHoldMs, "dark again"),
    hold(kSettleMs, "settle"),
    sweep("dark_after"),

    // Suite 2 -------------------------------------------------------------
    note("SUITE 2. Auto-off semantics: read the blank off the counter"),

    set(100, 100u, "t=100ms"),
    observe(3000u, "t=100ms"),

    set(100, 1000u, "t=1s"),
    observe(5000u, "t=1s"),

    set(100, 5000u, "t=5s: what both shipped apps use"),
    observe(10000u, "t=5s"),

    // The clamp test. If a kernel policy caps on-time below a minute, this is
    // where it shows, and the counter says at what.
    set(100, 60000u, "t=60s"),
    observe(70000u, "t=60s"),

    // The contested one. Header says hold indefinitely; the simulator blanks
    // almost at once. Thirty seconds settles which, and "still lit at 30s" is a
    // perfectly good answer to record.
    set(100, 0u, "t=0: header says no auto-off, simulator blanks at once"),
    observe(30000u, "t=0"),

    set(100, 0xFFFFFFFFu, "t=MAX: clamped, or taken at face value"),
    observe(30000u, "t=MAX"),

    // Q4, first half: does a second request cancel the first one's timer, or do
    // both timers stay armed? If the light dies at about 1 s despite the 60 s
    // request that followed it, the first timer was never cancelled.
    set(100, 1000u, "cancel test: arm 1s"),
    set(100, 60000u, "cancel test: then 60s; does the 1s timer still fire"),
    observe(20000u, "cancel test"),

    // Q4, second half: does brightness = 0 beat a timer already running?
    set(100, 60000u, "off test: arm 60s"),
    hold(2000u, "let it light"),
    set(0, 0u, "off test: brightness = 0 now"),
    observe(8000u, "off test"),

    // Suite 3 -------------------------------------------------------------
    note("SUITE 3. Context: the IID gap, and a GUI-sent request"),

    probeIids("unallocated IIDs 0x00050000..0x000A0000"),

    // Q1's context half. Same request, sent from the GUI process.
    set(100, 5000u, "GUI-sent request", Sender::Gui),
    observe(8000u, "GUI-sent"),

    // Leave the watch as it was found.
    set(0, 0u, "done: backlight off"),
};

} // namespace

const Step* plan() { return kPlan; }

size_t planSize() { return sizeof(kPlan) / sizeof(kPlan[0]); }

const char* actionName(Action action)
{
    switch (action) {
        case Action::Note:         return "NOTE";
        case Action::SetBacklight: return "SET";
        case Action::Sweep:        return "SWEEP";
        case Action::Hold:         return "HOLD";
        case Action::Observe:      return "OBSERVE";
        case Action::ProbeIids:    return "IIDS";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// ProbeRunner
// ---------------------------------------------------------------------------

ProbeRunner::ProbeRunner(const Step* steps, size_t count, ProbeExecutor& executor)
    : mSteps(steps)
    , mCount(count)
    , mExecutor(executor)
{
}

const Step& ProbeRunner::current() const
{
    // Clamped rather than asserted: this is read by the screen, which repaints
    // on its own schedule and can arrive here a poll after the plan ended.
    const size_t at = (mIndex < mCount) ? mIndex : (mCount ? mCount - 1 : 0);
    return mSteps[at];
}

uint32_t ProbeRunner::stepElapsedMs(uint32_t nowMs) const
{
    if (!mRunning) {
        return 0;
    }
    return nowMs - mStepStartedMs;
}

bool ProbeRunner::quietNow() const
{
    if (!mRunning || mIndex >= mCount) {
        return true;
    }
    return mSteps[mIndex].quiet;
}

uint32_t ProbeRunner::currentDurationMs() const
{
    if (mIndex >= mCount) {
        return 0;
    }

    // Every action honours durationMs, not just the two waits. It could have
    // switched on the action and returned zero for the rest: the plan only
    // ever sets a duration on Hold and Observe, so the two are equivalent today.
    // They are not equivalent for the obvious next edit, "sweep and then hold on
    // that state": with a switch, that step's duration would be silently
    // ignored, and mActed below would be the only thing standing between it and
    // rewriting its sweep file on every poll. One rule that always applies beats
    // two that agree by coincidence.
    return mSteps[mIndex].durationMs;
}

void ProbeRunner::start(uint32_t nowMs)
{
    if (mRunning) {
        return; // A second press is a no-op, not a restart.
    }
    if (mCount == 0) {
        mFinished = true;
        mExecutor.planFinished();
        return;
    }

    mRunning  = true;
    mFinished = false;
    mIndex    = 0;
    beginStep(nowMs);
}

void ProbeRunner::beginStep(uint32_t nowMs)
{
    mStepStartedMs = nowMs;
    mActed         = false;
    mExecutor.stepBegan(mIndex, mSteps[mIndex]);
}

void ProbeRunner::poll(uint32_t nowMs)
{
    if (!mRunning) {
        return;
    }

    // Perform this step's action once, on the first poll after it began. Waits
    // have no action; they finish by elapsing.
    if (!mActed) {
        mActed = true;
        switch (mSteps[mIndex].action) {
            case Action::Note:         mExecutor.note(mIndex, mSteps[mIndex]); break;
            case Action::SetBacklight: mExecutor.setBacklight(mIndex, mSteps[mIndex]); break;
            case Action::Sweep:        mExecutor.sweep(mIndex, mSteps[mIndex]); break;
            case Action::ProbeIids:    mExecutor.probeIids(mIndex, mSteps[mIndex]); break;
            case Action::Hold:
            case Action::Observe:      break;
        }
    }

    // A step whose action took real time (a sweep writes a few hundred lines to
    // storage) has already outlasted a zero duration, so this is not a spin: at
    // most one step's action runs per poll, and the cursor advances past the
    // instantaneous ones on the poll after they ran.
    if ((nowMs - mStepStartedMs) < currentDurationMs()) {
        return;
    }

    ++mIndex;
    if (mIndex >= mCount) {
        mRunning  = false;
        mFinished = true;
        mExecutor.planFinished();
        return;
    }

    beginStep(nowMs);
}

} // namespace Probe
