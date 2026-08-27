/**
 ******************************************************************************
 * @file    ProbePlan.hpp
 * @brief   The experiment, as a table, and the runner that walks it.
 ******************************************************************************
 *
 * The whole point of this app is to produce a record somebody can argue with
 * later. That means the experiment has to be a *table* rather than a sequence of
 * calls buried in the service: a table can be printed into the results file
 * verbatim, diffed between runs, and walked on a host with no watch attached.
 *
 * So `Step` is data, `kPlan` is the experiment, and `ProbeRunner` is a cursor
 * over it with a clock. Nothing in the runner knows what a backlight is; it
 * calls `ProbeExecutor`, which the service implements and the tests fake. That
 * split is what makes the timing behaviour testable at all: the interesting
 * bugs here are "the step advanced before the light settled" and "the hold was
 * shorter than the auto-off it was meant to outlast", and neither needs a watch
 * to catch.
 *
 * ## Why the steps are shaped the way they are
 *
 * `Hold` and `Observe` are the same wait with different intent, and they are
 * deliberately not merged:
 *
 *   - `Hold` is a *quiet* wait. The screen does not repaint. It exists so the
 *     light can be photographed or metered against a static frame, and so a
 *     register sweep is taken with nothing else going on.
 *   - `Observe` is a *counting* wait. The screen repaints a millisecond counter
 *     roughly ten times a second, so a phone video of the watch timestamps
 *     itself and the moment the light goes out can be read off the frame.
 *
 * The distinction matters because repainting is not neutral. A repaint is a
 * `REQUEST_DISPLAY_UPDATE` to the same kernel that owns the backlight, and a
 * kernel that treats display activity as user activity would extend an auto-off
 * timer it was meant to be measuring. Which of the two a step used is therefore
 * recorded in the results file next to its finding, because it is a plausible
 * confound and a reader has to be able to see it. See `Step::quiet`.
 *
 * ## What the plan cannot do
 *
 * It cannot see the light. There is no `isOn()` an app can reach (that is Q5,
 * and Phase A settled it: `IBacklight` is not obtainable), and the message
 * carries no state back. So every timing answer in Suite 2 is read off a video
 * or a meter by a person, and the app's only job there is to make the moment
 * unambiguous. Nothing here should be written as though the app knows whether
 * the backlight is lit: it never does.
 *
 ******************************************************************************
 */

#ifndef PROBE_PLAN_HPP
#define PROBE_PLAN_HPP

#include <cstddef>
#include <cstdint>

namespace Probe
{

/// What a step does. One action each, so the results file can name it.
enum class Action : uint8_t {
    Note         = 0, ///< Write a heading into the results file. No side effect.
    SetBacklight = 1, ///< Send RequestBacklightSet and record the result code.
    Sweep        = 2, ///< Labelled register sweep, appended to the results file.
    Hold         = 3, ///< Quiet wait: no repaints, for metering and photography.
    Observe      = 4, ///< Counting wait: repaints a ms counter for video correlation.
    ProbeIids    = 5, ///< queryInterface across the unallocated IID range.
};

/// Who sends the backlight request.
///
/// Q1's context half: both shipped callers (`GpsLab`, `Squash`) send it from a
/// Service, and the message type sits directly below a block commented
/// "Display control (GUI only)". Whether that comment reaches this message is
/// unverified and cheap to test, so the plan tests it rather than assuming.
enum class Sender : uint8_t {
    Service = 0,
    Gui     = 1,
};

/**
 * @brief One row of the experiment.
 *
 * Deliberately flat and POD: it is printed into the results file field by field,
 * so anything that needed a constructor to be correct would be a field the file
 * could disagree with.
 */
struct Step {
    Action  action;
    Sender  sender;      ///< SetBacklight only.
    uint8_t brightness;  ///< SetBacklight only. 0-100 as the message documents it.

    /// SetBacklight only: `autoOffTimeoutMs` on the wire.
    uint32_t timeoutMs;

    /// SetBacklight only: the `timeoutMs` handed to `sendMessage`, NOT the
    /// auto-off above. Non-zero arms the completion semaphore and makes
    /// `getResult()` meaningful, which is the entire instrument for Q1: both
    /// shipped callers pass 0 here and so learn nothing. Kept separate from
    /// `timeoutMs` because conflating the two is the single easiest mistake to
    /// make with this message.
    uint32_t sendTimeoutMs;

    /// Hold and Observe only.
    uint32_t durationMs;

    /// Sweep and Note: the label. Also the human name of any other step.
    const char* label;

    /// True when the screen must not repaint during this step. Set on Hold,
    /// clear on Observe. Recorded in the results file because a repaint is a
    /// message to the kernel that owns the light. See the file comment.
    bool quiet;
};

/// The experiment. Definition and rationale live in ProbePlan.cpp.
const Step* plan();

/// How many steps `plan()` has.
size_t planSize();

/// Human name for an action, as written into the results file.
const char* actionName(Action action);

/**
 * @brief What the runner calls to make a step happen.
 *
 * The service implements it against the kernel; the host tests implement it
 * against a vector. Nothing in `ProbeRunner` touches an SDK type, which is why
 * the runner's timing can be tested without a watch.
 */
class ProbeExecutor
{
public:
    virtual ~ProbeExecutor() = default;

    /// Send the request. `index` is the step's position, so the executor can
    /// label the record without the runner having to pass the whole step.
    virtual void setBacklight(size_t index, const Step& step) = 0;

    /// Take a labelled register sweep.
    virtual void sweep(size_t index, const Step& step) = 0;

    /// Walk the unallocated IID range, logging pointers and calling nothing.
    virtual void probeIids(size_t index, const Step& step) = 0;

    /// Write a heading.
    virtual void note(size_t index, const Step& step) = 0;

    /// Called once as each step begins, before the action, so the executor can
    /// record when it started. Waits have no action of their own and this is the
    /// only callback they produce.
    virtual void stepBegan(size_t index, const Step& step) = 0;

    /// Called once when the whole plan has finished.
    virtual void planFinished() = 0;
};

/**
 * @brief Walks a plan against a clock.
 *
 * Single-stepping and non-blocking: `poll()` performs at most the work that has
 * come due and returns. The service calls it between kernel message waits, for
 * the reason FwDump's dumper does the same: a long synchronous loop on an app
 * thread trips the liveness watchdog and reboots the watch.
 *
 * The clock is passed in rather than read, so the tests can drive it.
 */
class ProbeRunner
{
public:
    ProbeRunner(const Step* steps, size_t count, ProbeExecutor& executor);

    /// Begin at step 0. Ignored if already running.
    void start(uint32_t nowMs);

    /// Perform whatever is due. Safe to call when idle or finished.
    void poll(uint32_t nowMs);

    bool   running() const { return mRunning; }
    bool   finished() const { return mFinished; }
    size_t index() const { return mIndex; }
    size_t count() const { return mCount; }

    /// The step in progress. Only meaningful while `running()`.
    const Step& current() const;

    /// How long the current step has been in progress. This is what the screen
    /// draws during an Observe step, and it is the number a video is read
    /// against, so it is milliseconds since the step began and nothing else.
    uint32_t stepElapsedMs(uint32_t nowMs) const;

    /// Whether the screen should hold still right now. True while a quiet step
    /// runs, and true when nothing is running at all.
    bool quietNow() const;

private:
    const Step*    mSteps;
    size_t         mCount;
    ProbeExecutor& mExecutor;

    bool   mRunning  = false;
    bool   mFinished = false;
    size_t mIndex    = 0;

    uint32_t mStepStartedMs = 0;

    /// Whether the current step's action has already been performed. Waits do
    /// their work by elapsing, so this is what stops an instantaneous action
    /// from being repeated on every poll of the same step.
    bool mActed = false;

    void beginStep(uint32_t nowMs);

    /// How long the current step must last before the cursor moves on. Zero for
    /// everything except Hold and Observe.
    uint32_t currentDurationMs() const;
};

} // namespace Probe

#endif // PROBE_PLAN_HPP
