/**
 * Tests for the plan runner, and for the plan itself.
 *
 * Two different kinds of thing live here, and the second is the more valuable.
 *
 * The runner tests are ordinary: does the cursor advance when it should, does an
 * action happen exactly once, does a wait actually wait. Worth having because
 * the failure mode (a step advancing before the light settled) produces a
 * sweep that looks perfectly well-formed and means nothing.
 *
 * The **plan** tests are assertions about the experiment. A register sweep
 * whose label collides with another one silently overwrites its file; a request
 * sent with a zero send timeout silently reports PENDING forever; a ladder whose
 * auto-off is shorter than the ladder is a ladder that goes dark halfway through
 * for a reason nobody would suspect. None of those break a build, none of them
 * throw, and every one of them quietly destroys the run. They are properties of
 * the experiment rather than of the code, and pinning them is what stops a later
 * edit to the table turning it into an experiment that cannot answer the
 * question it was written for.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "ProbePlan.hpp"

namespace {

using namespace Probe;

/// Records what the runner asked for, in order.
class RecordingExecutor : public ProbeExecutor
{
public:
    struct Call {
        std::string what;
        size_t      index;
    };

    std::vector<Call> calls;
    std::vector<size_t> begins;
    int finishedCount = 0;

    void setBacklight(size_t index, const Step&) override { calls.push_back({"set", index}); }
    void sweep(size_t index, const Step&) override { calls.push_back({"sweep", index}); }
    void probeIids(size_t index, const Step&) override { calls.push_back({"iids", index}); }
    void note(size_t index, const Step&) override { calls.push_back({"note", index}); }
    void stepBegan(size_t index, const Step&) override { begins.push_back(index); }
    void planFinished() override { ++finishedCount; }
};

Step makeHold(uint32_t ms, const char* label)
{
    return Step{Action::Hold, Sender::Service, 0, 0, 0, ms, label, true};
}

Step makeSweep(const char* label)
{
    return Step{Action::Sweep, Sender::Service, 0, 0, 0, 0, label, true};
}

Step makeSet(uint8_t brightness)
{
    return Step{Action::SetBacklight, Sender::Service, brightness, 1000, 250, 0, "set", true};
}

Step makeObserve(uint32_t ms)
{
    return Step{Action::Observe, Sender::Service, 0, 0, 0, ms, "obs", false};
}

// ---------------------------------------------------------------------------
// The runner
// ---------------------------------------------------------------------------

TEST(ProbeRunner, DoesNothingBeforeStart)
{
    const Step steps[] = {makeSweep("a")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 1, exec);

    runner.poll(0);
    runner.poll(1000);

    EXPECT_TRUE(exec.calls.empty());
    EXPECT_FALSE(runner.running());
    EXPECT_FALSE(runner.finished());
}

TEST(ProbeRunner, PerformsAnInstantActionOnceAndAdvancesInTheSamePoll)
{
    const Step steps[] = {makeSweep("a"), makeSweep("b")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 2, exec);

    runner.start(0);
    runner.poll(0);

    ASSERT_EQ(exec.calls.size(), 1u);
    EXPECT_EQ(exec.calls[0].what, "sweep");
    EXPECT_EQ(exec.calls[0].index, 0u);
    EXPECT_EQ(runner.index(), 1u);

    runner.poll(10);
    ASSERT_EQ(exec.calls.size(), 2u);
    EXPECT_EQ(exec.calls[1].index, 1u);
}

TEST(ProbeRunner, ConsecutiveInstantStepsLandOnePollApart)
{
    // The cancel test in Suite 2 depends on this: "arm 1s, then immediately
    // request 60s" is only a test of timer cancellation if the two requests are
    // milliseconds apart rather than a second.
    const Step steps[] = {makeSet(100), makeSet(100), makeObserve(5000)};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 3, exec);

    runner.start(0);
    runner.poll(0);
    runner.poll(10);

    ASSERT_EQ(exec.calls.size(), 2u);
    EXPECT_EQ(exec.calls[0].what, "set");
    EXPECT_EQ(exec.calls[1].what, "set");
}

TEST(ProbeRunner, AHoldDoesNotAdvanceUntilItsDurationHasElapsed)
{
    const Step steps[] = {makeHold(1000, "settle"), makeSweep("after")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 2, exec);

    runner.start(0);
    runner.poll(0);
    runner.poll(500);
    runner.poll(999);
    EXPECT_TRUE(exec.calls.empty()) << "the sweep ran before the light had settled";
    EXPECT_EQ(runner.index(), 0u);

    runner.poll(1000);
    EXPECT_EQ(runner.index(), 1u);

    runner.poll(1010);
    ASSERT_EQ(exec.calls.size(), 1u);
    EXPECT_EQ(exec.calls[0].what, "sweep");
}

TEST(ProbeRunner, ReportsElapsedWithinTheCurrentStepOnly)
{
    const Step steps[] = {makeHold(1000, "a"), makeHold(1000, "b")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 2, exec);

    runner.start(100);
    EXPECT_EQ(runner.stepElapsedMs(600), 500u);

    runner.poll(1100); // First step done; second begins at 1100.
    EXPECT_EQ(runner.index(), 1u);
    EXPECT_EQ(runner.stepElapsedMs(1300), 200u)
        << "elapsed must restart with the step, or a video cannot be read against it";
}

TEST(ProbeRunner, QuietTracksTheCurrentStep)
{
    const Step steps[] = {makeHold(100, "quiet"), makeObserve(100)};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 2, exec);

    EXPECT_TRUE(runner.quietNow()) << "not running: the screen must not be repainting";

    runner.start(0);
    EXPECT_TRUE(runner.quietNow());

    runner.poll(100);
    EXPECT_EQ(runner.index(), 1u);
    EXPECT_FALSE(runner.quietNow()) << "an OBSERVE step must let the counter move";
}

TEST(ProbeRunner, FinishesExactlyOnceAndStaysFinished)
{
    const Step steps[] = {makeSweep("a")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 1, exec);

    runner.start(0);
    runner.poll(0);

    EXPECT_EQ(exec.finishedCount, 1);
    EXPECT_TRUE(runner.finished());
    EXPECT_FALSE(runner.running());

    runner.poll(10);
    runner.poll(20);
    EXPECT_EQ(exec.finishedCount, 1);
    EXPECT_EQ(exec.calls.size(), 1u) << "a finished plan must not keep acting";
}

TEST(ProbeRunner, ASecondStartWhileRunningIsIgnored)
{
    const Step steps[] = {makeHold(1000, "a"), makeSweep("b")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 2, exec);

    runner.start(0);
    runner.poll(500);
    runner.start(500); // A second press mid-run.
    EXPECT_EQ(runner.index(), 0u);
    EXPECT_EQ(runner.stepElapsedMs(600), 600u) << "a second start must not restart the step clock";
}

TEST(ProbeRunner, AnActionOnAStepThatAlsoWaitsHappensExactlyOnce)
{
    // No step in the current plan has both an action and a duration, so this
    // guard is unexercised by the plan as it stands: removing it left every
    // other test green. It is pinned anyway, because the obvious future edit is
    // "sweep, then hold on that state", and without the guard that step would
    // rewrite its sweep file on every poll for the length of the hold.
    Step sweepAndWait = makeSweep("s");
    sweepAndWait.durationMs = 1000;

    const Step steps[] = {sweepAndWait};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 1, exec);

    runner.start(0);
    for (uint32_t t = 0; t <= 1000; t += 10) {
        runner.poll(t);
    }

    EXPECT_EQ(exec.calls.size(), 1u) << "the action repeated for the length of the wait";
}

TEST(ProbeRunner, AnEmptyPlanFinishesImmediately)
{
    RecordingExecutor exec;
    ProbeRunner runner(nullptr, 0, exec);

    runner.start(0);
    EXPECT_TRUE(runner.finished());
    EXPECT_EQ(exec.finishedCount, 1);
}

TEST(ProbeRunner, AnnouncesEveryStepExactlyOnce)
{
    const Step steps[] = {makeSet(100), makeHold(50, "h"), makeSweep("s")};
    RecordingExecutor exec;
    ProbeRunner runner(steps, 3, exec);

    runner.start(0);
    for (uint32_t t = 0; t <= 200; t += 10) {
        runner.poll(t);
    }

    ASSERT_EQ(exec.begins.size(), 3u);
    EXPECT_EQ(exec.begins[0], 0u);
    EXPECT_EQ(exec.begins[1], 1u);
    EXPECT_EQ(exec.begins[2], 2u);
}

// ---------------------------------------------------------------------------
// The experiment
// ---------------------------------------------------------------------------

TEST(Plan, SweepLabelsAreUnique)
{
    // Each sweep writes sweep_<label>.txt. Two steps sharing a label means the
    // second silently overwrites the first, and the run comes back with a
    // missing state that nothing in the results file reports as missing --
    // the dark/lit diff would then be comparing a file against itself.
    std::set<std::string> seen;
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action != Action::Sweep) {
            continue;
        }
        ASSERT_NE(step.label, nullptr) << "sweep at step " << i << " has no label";
        EXPECT_TRUE(seen.insert(step.label).second)
            << "duplicate sweep label \"" << step.label << "\" at step " << i
            << ": it would overwrite the earlier sweep's file";
    }
    EXPECT_GE(seen.size(), 3u) << "too few sweeps to diff anything";
}

TEST(Plan, SweepLabelsAreSafeAsFilenames)
{
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action != Action::Sweep) {
            continue;
        }
        const std::string label = step.label;
        EXPECT_FALSE(label.empty());
        EXPECT_LT(label.size(), 32u) << "label at step " << i << " will not fit the path buffer";
        for (char c : label) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                            || (c >= '0' && c <= '9') || c == '_' || c == '-';
            EXPECT_TRUE(ok) << "label \"" << label << "\" has a character a path cannot carry: "
                            << c;
        }
    }
}

TEST(Plan, EveryRequestArmsTheCompletionSemaphore)
{
    // A zero send timeout leaves getResult() PENDING no matter what the kernel
    // did, which is exactly why the two shipped callers can say nothing about
    // whether this message is handled. A step that regressed to zero would
    // still run, still log, and still be worthless.
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action != Action::SetBacklight) {
            continue;
        }
        EXPECT_GT(step.sendTimeoutMs, 0u)
            << "step " << i << " (" << step.label << ") sends fire-and-forget; its result "
            << "would be PENDING whatever happened";
    }
}

TEST(Plan, HoldsAreQuietAndObservesAreNot)
{
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action == Action::Hold) {
            EXPECT_TRUE(step.quiet) << "step " << i << " meters against a repainting screen";
        }
        if (step.action == Action::Observe) {
            EXPECT_FALSE(step.quiet)
                << "step " << i << " needs a moving counter for the video to be readable";
        }
    }
}

TEST(Plan, TheLadderCannotBlankPartWayThroughOnOurOwnTimer)
{
    // Suite 1 sets a long auto-off and then spends over a minute settling,
    // sweeping and metering. If that auto-off were shorter than the remaining
    // ladder, the light would go out mid-run and every sweep after it would
    // record a dark state under a lit label, which is not a wrong number, it
    // is a wrong conclusion.
    //
    // Applies only to metering requests: a request the plan then OBSERVEs is a
    // Suite 2 step, where outlasting the auto-off is the whole measurement. The
    // two are told apart by what follows, not by the size of the timeout --
    // scoping this by "timeout is big" instead caught the 60 s clamp test and
    // called a correct experiment a bug.
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action != Action::SetBacklight || step.brightness == 0) {
            continue;
        }

        uint32_t waited = 0;
        bool metering = true;
        for (size_t j = i + 1; j < planSize(); ++j) {
            if (plan()[j].action == Action::SetBacklight) {
                break; // Superseded before it could expire.
            }
            if (plan()[j].action == Action::Observe) {
                metering = false; // Suite 2: the expiry is the point.
                break;
            }
            if (plan()[j].action == Action::Hold) {
                waited += plan()[j].durationMs;
            }
        }

        if (!metering) {
            continue;
        }
        EXPECT_GT(step.timeoutMs, waited)
            << "step " << i << " (" << step.label << ") holds the light for " << step.timeoutMs
            << " ms but the plan then meters for " << waited << " ms";
    }
}

TEST(Plan, EverySweepIsPrecededByTimeForTheLightToSettle)
{
    // A sweep taken in the same millisecond as the request that caused it reads
    // the registers before the kernel has acted on the message. The result is a
    // perfectly well-formed file of the PREVIOUS state, filed under the new
    // state's label: the most damaging thing this app could produce, because
    // nothing about it looks wrong.
    //
    // Found by mutation: setting the settle hold to zero left every other test
    // green.
    for (size_t i = 0; i < planSize(); ++i) {
        if (plan()[i].action != Action::Sweep) {
            continue;
        }

        // Walk back to the request this sweep is measuring, accumulating wait.
        uint32_t settled = 0;
        bool sawRequest = false;
        for (size_t j = i; j-- > 0;) {
            if (plan()[j].action == Action::SetBacklight) {
                sawRequest = true;
                break;
            }
            if (plan()[j].action == Action::Hold || plan()[j].action == Action::Observe) {
                settled += plan()[j].durationMs;
            }
        }

        ASSERT_TRUE(sawRequest) << "sweep at step " << i << " measures no particular state";
        EXPECT_GE(settled, 500u)
            << "sweep at step " << i << " (" << plan()[i].label << ") runs only " << settled
            << " ms after the request that set the state it claims to record";
    }
}

TEST(Plan, LadderCoversAWideEnoughRangeToSeeAnything)
{
    // If two non-zero brightness values ever produce different registers, it
    // will be between the extremes. A ladder that only sampled 90 and 100 could
    // find nothing and prove nothing.
    uint8_t lowest = 255;
    uint8_t highest = 0;
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action != Action::SetBacklight || step.brightness == 0) {
            continue;
        }
        lowest  = step.brightness < lowest ? step.brightness : lowest;
        highest = step.brightness > highest ? step.brightness : highest;
    }
    EXPECT_EQ(highest, 100u) << "the ladder must include full brightness as its baseline";
    EXPECT_LE(lowest, 10u) << "the ladder must reach near the bottom of the documented range";
}

TEST(Plan, ThePlanEndsWithTheBacklightOff)
{
    // The watch is left as it was found. A plan whose last request was a ten
    // minute hold would leave the light on across the walk back to the cable.
    const Step* last = nullptr;
    for (size_t i = 0; i < planSize(); ++i) {
        if (plan()[i].action == Action::SetBacklight) {
            last = &plan()[i];
        }
    }
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(last->brightness, 0u) << "the run leaves the backlight on";
}

TEST(Plan, TheZeroTimeoutCaseIsActuallyTested)
{
    // The contested one: the header says 0 disables auto-off, the simulator's
    // mock blanks within about 50 ms. If this step ever fell out of the plan,
    // the run would come back looking complete and would have skipped the single
    // most contradictory claim in the SDK.
    bool found = false;
    for (size_t i = 0; i < planSize(); ++i) {
        const Step& step = plan()[i];
        if (step.action == Action::SetBacklight && step.brightness > 0 && step.timeoutMs == 0) {
            found = true;
            // And it has to be watched for long enough to be worth anything.
            ASSERT_LT(i + 1, planSize());
            EXPECT_EQ(plan()[i + 1].action, Action::Observe);
            EXPECT_GE(plan()[i + 1].durationMs, 10000u)
                << "a few seconds of watching cannot distinguish 'holds forever' from 'holds a while'";
        }
    }
    EXPECT_TRUE(found) << "nothing in the plan tests autoOffTimeoutMs = 0";
}

TEST(Plan, SomethingIsSentFromTheGuiProcess)
{
    bool found = false;
    for (size_t i = 0; i < planSize(); ++i) {
        if (plan()[i].action == Action::SetBacklight && plan()[i].sender == Sender::Gui) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "Q1's context half is untested: every request comes from the service";
}

TEST(Plan, EveryStepHasALabel)
{
    for (size_t i = 0; i < planSize(); ++i) {
        EXPECT_NE(plan()[i].label, nullptr) << "step " << i << " has nothing to show on screen";
        EXPECT_GT(std::strlen(plan()[i].label), 0u) << "step " << i << " has an empty label";
    }
}

TEST(Plan, ActionNamesAreAllDistinctAndNonEmpty)
{
    // These go into the results file as a column. Two actions sharing a name
    // would make that column unreadable.
    const Action all[] = {Action::Note,    Action::SetBacklight, Action::Sweep,
                          Action::Hold,    Action::Observe,      Action::ProbeIids};
    std::set<std::string> seen;
    for (Action a : all) {
        const std::string name = actionName(a);
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "?");
        EXPECT_TRUE(seen.insert(name).second) << "duplicate action name " << name;
    }
}

} // namespace
