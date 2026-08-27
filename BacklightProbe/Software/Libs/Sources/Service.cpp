/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   Running the plan, and writing down what happened.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstring>

#include "SDK/Interfaces/IKernel.hpp"
#include "SDK/Interfaces/IKIP.hpp"
#include "SDK/Messages/MessageGuard.hpp"

#include "IidProbe.hpp"
#include "RegisterSweep.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

/// The SDK's own entry points declare this the same way, and it is the only
/// route from an app to `queryInterface`: `SDK::Kernel` binds the five
/// interfaces it knows about and does not expose the provider they came from.
///
/// That is worth stating plainly, because it is what makes the IID walk possible
/// at all: an app is not confined to the five identifiers the facade resolves,
/// it just has to reach past the facade to ask for anything else.
extern const SDK::Interface::IKernel* gIKernel;

namespace
{

/// The record. Sits next to the sweep_*.txt files in the app's own folder.
constexpr char kResultsPath[] = "backlight_probe.txt";

/// Presence of this file, at any size and with any content, enables the
/// unconfirmed timer bases in the sweep.
///
/// A marker file rather than a parsed config: there is exactly one thing to
/// decide, the decision has real consequences (an address that does not decode
/// takes the app down), and a flag that can be got wrong by a typo inside a
/// JSON file is worse than one that cannot be got wrong at all. Absent means
/// off, which is the safe default and the one the first run should use.
constexpr char kTimersMarkerPath[] = "sweep_timers.enable";

} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mRunner(Probe::plan(), Probe::planSize(), *this)
{
    for (size_t i = 0; i < Probe::planSize(); ++i) {
        if (Probe::plan()[i].action == Probe::Action::Observe) {
            ++mObserveSteps;
        }
    }
}

void Service::readConfig()
{
    mIncludeTimers = mKernel.fs.exist(kTimersMarkerPath);
    LOG_INFO("timer bases %s\n", mIncludeTimers ? "ENABLED (unconfirmed; may fault)" : "off");
}

void Service::openResults()
{
    mResultsFile = mKernel.fs.file(kResultsPath);
    if (!mResultsFile || !mResultsFile->open(true, true)) {
        // Not fatal. The run still happens and still logs, and a run whose
        // record only reached a UART capture is worth more than no run.
        LOG_INFO("could not open %s: the log is the only record this time\n", kResultsPath);
        mResultsFile.reset();
        return;
    }

    mLog = std::make_unique<Probe::ProbeLog>(*mResultsFile);
    mLog->header(mKernel.sys.getTimeMs(), RegisterSweep::available(),
                 RegisterSweep::confirmedBlockCount()
                     + (mIncludeTimers ? RegisterSweep::timerBlockCount() : 0),
                 mIncludeTimers);
}

void Service::run()
{
    LOG_INFO("started\n");

    readConfig();
    openResults();

    if (!RegisterSweep::available()) {
        // Said loudly and early. On this build the sweep reads nothing, so a
        // completed run proves the plan machinery works and proves exactly
        // nothing about the watch.
        LOG_INFO("SIMULATOR/host: no peripheral registers; no sweep will be taken, "
                 "and nothing this run produces says anything about real hardware\n");
    }

    while (true) {
        SDK::MessageBase* msg;
        if (mKernel.comm.getMessage(msg, nextWaitMs())) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("force exit from the application\n");
                    if (mResultsFile) {
                        // Everything is already flushed; this only releases the
                        // handle. A run cut short leaves a truncated record,
                        // which is the honest artefact of a run cut short.
                        mResultsFile->close();
                    }
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    LOG_INFO("GUI is now running\n");
                    mGuiStarted = true;
                    publish();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    LOG_INFO("GUI has stopped\n");
                    mGuiStarted = false;
                    // Does NOT end the run. Suite 1 deliberately spends time
                    // with the screen dark, and losing the experiment to that
                    // would defeat the app.
                    break;

                default:
                    handleCommand(msg);
                    break;
            }
            mKernel.comm.releaseMessage(msg);
        }

        poll();
    }
}

void Service::poll()
{
    const uint32_t nowMs = mKernel.sys.getTimeMs();

    mRunner.poll(nowMs);

    const CustomMessage::ProbeState state = mState;
    if (state != mLastPublishedState) {
        publish();
        return;
    }

    if (mGuiStarted && (nowMs - mLastPublishAtMs) >= kPublishPeriodMs) {
        publish();
    }
}

uint32_t Service::nextWaitMs() const
{
    return mRunner.running() ? kBusyWaitMs : kIdleWaitMs;
}

// ---------------------------------------------------------------------------
// Probe::ProbeExecutor
// ---------------------------------------------------------------------------

void Service::stepBegan(size_t index, const Probe::Step& step)
{
    const uint32_t nowMs = mKernel.sys.getTimeMs();

    LOG_INFO("step %02u %s %s\n", static_cast<unsigned>(index), Probe::actionName(step.action),
             step.label ? step.label : "");

    if (mLog) {
        mLog->stepBegan(index, step, nowMs);
    }

    // Published immediately rather than on the next throttle tick: the screen's
    // millisecond counter is timed from the step's start, and a counter that
    // begins a tenth of a second late is a counter a video cannot be read
    // against.
    publish();
}

void Service::setBacklight(size_t index, const Probe::Step& step)
{
    if (step.sender == Probe::Sender::Gui) {
        if (!mGuiStarted) {
            LOG_INFO("step %02u wanted a GUI-sent request but no GUI is running\n",
                     static_cast<unsigned>(index));
            if (mLog) {
                Backlight::Outcome missed;
                missed.brightness    = step.brightness;
                missed.autoOffMs     = step.timeoutMs;
                missed.sendTimeoutMs = step.sendTimeoutMs;
                mLog->backlight(index, step, missed);
            }
            return;
        }

        // Asked for, not waited for. The reply lands in handleGuiSendResult
        // whenever it arrives; the plan's next step is an Observe that gives it
        // room. See Commands.hpp.
        ++mGuiSendSeq;
        mGuiSendBrightness = step.brightness;
        mGuiSendAutoOffMs  = step.timeoutMs;
        mGuiSendTimeoutMs  = step.sendTimeoutMs;
        mGuiSendStep       = index;
        publish();
        return;
    }

    const Backlight::Outcome outcome =
        Backlight::request(mKernel, step.brightness, step.timeoutMs, step.sendTimeoutMs);

    mLastResult = outcome.result;

    LOG_INFO("step %02u SET b=%u auto_off=%lu -> sent=%c result=%s in %lu ms\n",
             static_cast<unsigned>(index), static_cast<unsigned>(outcome.brightness),
             static_cast<unsigned long>(outcome.autoOffMs), outcome.sent ? 'Y' : 'N',
             Backlight::resultName(outcome.result), static_cast<unsigned long>(outcome.elapsedMs));

    if (mLog) {
        mLog->backlight(index, step, outcome);
    }
}

void Service::sweep(size_t index, const Probe::Step& step)
{
    const bool ok = RegisterSweep::write(mKernel, step.label, mIncludeTimers);
    if (mLog) {
        mLog->sweep(index, step, ok);
    }
}

void Service::probeIids(size_t index, const Probe::Step& step)
{
    (void)step;

    if (gIKernel == nullptr) {
        LOG_INFO("no kernel provider: IID walk skipped\n");
        return;
    }

    // const_cast because queryInterface is non-const on IKIP and gIKernel is a
    // pointer to const. The call reads a lookup table; nothing about it mutates
    // the kernel, and the constness here is an artefact of how the SDK's entry
    // points happen to declare the global rather than a guarantee about the
    // interface.
    auto& kip = const_cast<SDK::Interface::IKIP&>(gIKernel->kip);

    const IidProbe::Result result = IidProbe::run(kip);
    if (mLog) {
        mLog->iids(index, result);
    }
}

void Service::note(size_t index, const Probe::Step& step)
{
    if (mLog) {
        mLog->note(index, step);
    }
}

void Service::planFinished()
{
    mState = CustomMessage::ProbeState::Done;

    LOG_INFO("plan finished\n");

    if (mLog) {
        mLog->footer(mKernel.sys.getTimeMs(), Probe::planSize(), mObserveSteps);
    }
    if (mResultsFile) {
        mResultsFile->flush();
        // Deliberately left open. The app stays alive so the screen can keep
        // saying DONE, and closing here would only save a handle while risking
        // a later write finding no file.
    }

    publish();
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void Service::handleCommand(SDK::MessageBase* msg)
{
    if (msg->getType() != CustomMessage::PROBE_COMMAND) {
        return; // Not one of ours.
    }

    const auto* command = static_cast<CustomMessage::ProbeCommand*>(msg);
    switch (static_cast<CustomMessage::Command>(command->command)) {
        case CustomMessage::Command::Start:
            handleStart();
            break;

        case CustomMessage::Command::Resend:
            publish();
            break;

        case CustomMessage::Command::GuiSendResult:
            handleGuiSendResult(*command);
            break;
    }
}

void Service::handleStart()
{
    if (mRunner.running()) {
        LOG_INFO("start ignored: already running\n");
        return;
    }
    if (mState == CustomMessage::ProbeState::Done) {
        LOG_INFO("start ignored: already finished; relaunch to run again\n");
        return;
    }

    mStartedAtMs = mKernel.sys.getTimeMs();
    mState       = CustomMessage::ProbeState::Running;
    LOG_INFO("start: %u steps, %u of them OBSERVE\n", static_cast<unsigned>(Probe::planSize()),
             static_cast<unsigned>(mObserveSteps));

    mRunner.start(mStartedAtMs);
    publish();
}

void Service::handleGuiSendResult(const CustomMessage::ProbeCommand& reply)
{
    if (reply.seq != mGuiSendSeq || mGuiSendSeq == 0) {
        // A reply to a request that is no longer current. Dropped rather than
        // attributed to the wrong step, which is the whole reason the sequence
        // number exists.
        LOG_INFO("stale GUI send result seq=%lu (current %lu), ignored\n",
                 static_cast<unsigned long>(reply.seq), static_cast<unsigned long>(mGuiSendSeq));
        return;
    }

    Backlight::Outcome outcome;
    outcome.brightness       = mGuiSendBrightness;
    outcome.autoOffMs        = mGuiSendAutoOffMs;
    outcome.sendTimeoutMs    = mGuiSendTimeoutMs;
    outcome.sent             = reply.sent;
    outcome.allocationFailed = reply.allocFailed;
    outcome.result           = static_cast<SDK::MessageResult>(reply.result);
    outcome.completed        = reply.completed;
    outcome.elapsedMs        = reply.elapsedMs;

    mLastResult = outcome.result;

    LOG_INFO("GUI-sent request -> sent=%c result=%s in %lu ms\n", outcome.sent ? 'Y' : 'N',
             Backlight::resultName(outcome.result), static_cast<unsigned long>(outcome.elapsedMs));

    if (mLog) {
        mLog->backlight(mGuiSendStep, Probe::plan()[mGuiSendStep], outcome);
    }

    mGuiSendSeq = 0; // Answered.
    publish();
}

void Service::publish()
{
    mLastPublishAtMs    = mKernel.sys.getTimeMs();
    mLastPublishedState = mState;

    if (!mGuiStarted) {
        return; // Nothing to tell.
    }

    auto guard = SDK::make_msg<CustomMessage::ProbeStatus>(mKernel);
    if (!guard) {
        return; // The next publish is 100 ms away; a dropped frame is nothing.
    }

    const uint32_t nowMs   = mKernel.sys.getTimeMs();
    const Probe::Step& step = mRunner.current();

    guard->stepElapsedMs  = mRunner.stepElapsedMs(nowMs);
    guard->stepDurationMs = (step.action == Probe::Action::Hold
                             || step.action == Probe::Action::Observe)
                                ? step.durationMs
                                : 0;
    guard->startedAtMs    = mStartedAtMs;

    guard->guiSendSeq       = mGuiSendSeq;
    guard->guiAutoOffMs     = mGuiSendAutoOffMs;
    guard->guiSendTimeoutMs = mGuiSendTimeoutMs;
    guard->guiBrightness    = mGuiSendBrightness;

    guard->stepIndex    = static_cast<uint16_t>(mRunner.index());
    guard->stepCount    = static_cast<uint16_t>(mRunner.count());
    guard->observeSteps = static_cast<uint16_t>(mObserveSteps);

    guard->state      = static_cast<uint8_t>(mState);
    guard->action     = static_cast<uint8_t>(step.action);
    guard->lastResult = static_cast<uint8_t>(mLastResult);

    guard->quiet              = mRunner.quietNow();
    guard->registersAvailable = RegisterSweep::available();
    guard->logIntact          = mLog ? mLog->intact() : false;

    const char* label = step.label ? step.label : "";
    std::strncpy(guard->label, label, CustomMessage::kLabelMax - 1);
    guard->label[CustomMessage::kLabelMax - 1] = '\0';

    guard.send();
}
