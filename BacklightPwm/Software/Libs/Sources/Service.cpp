/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   Climbing the ladder, one burst per poll.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstring>

#include "SDK/Messages/MessageGuard.hpp"

#include "BacklightRequest.hpp"

#define LOG_MODULE_PRX      "Service"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{
/// The kernel's millisecond tick, as a plain function pointer for the clock
/// calibration to sample. A lambda would need captures; this needs the kernel,
/// so it is parked here at file scope for the one call that uses it.
SDK::Kernel* gKernelForMillis = nullptr;

uint32_t millisThunk()
{
    return gKernelForMillis ? gKernelForMillis->sys.getTimeMs() : 0u;
}

/// How long to wait for the kernel to acknowledge a backlight request. Bounded
/// so a kernel that does not answer costs a blink rather than a stall.
constexpr uint32_t kSendTimeoutMs = 250;
} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mPwm(mPin, mClock)
{
    gKernelForMillis = &kernel;
    mPwm.setPeriodUs(Pwm::kPeriodUs);
}

void Service::run()
{
    LOG_INFO("started\n");

    if (!Pwm::available()) {
        // The host and simulator builds have no GPIOF and no cycle counter. The
        // app still runs, so the screen and the ladder logic can be exercised,
        // but it drives nothing and says so everywhere it can.
        LOG_INFO("no registers on this build: the ladder will run and drive nothing\n");
        mState    = CustomMessage::PwmState::Refused;
        mDriving  = false;
    }

    while (true) {
        SDK::MessageBase* msg;
        if (mKernel.comm.getMessage(msg, nextWaitMs())) {
            switch (msg->getType()) {
                case SDK::MessageType::COMMAND_APP_STOP:
                    LOG_INFO("force exit from the application\n");
                    // The pin goes back before anything else. This is the path
                    // USB insertion takes, and it takes it without warning.
                    handOverPin();
                    mKernel.comm.releaseMessage(msg);
                    return;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
                    mGuiStarted = true;
                    publish();
                    break;

                case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
                    mGuiStarted = false;
                    // The run continues with the screen blanked, deliberately:
                    // the kernel blanking the display is one of the events that
                    // might make it reassert the pin, and watching that happen is
                    // the point.
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

uint32_t Service::nextWaitMs() const
{
    // Zero while driving: the queue is checked and the next burst starts at once,
    // so the gap between bursts is a queue poll rather than a sleep. Any longer
    // and the seams between bursts would be visible in the light.
    return (mState == CustomMessage::PwmState::Running) ? 0u : kIdleWaitMs;
}

void Service::poll()
{
    if (mState != CustomMessage::PwmState::Running) {
        return;
    }

    const uint32_t nowMs = mKernel.sys.getTimeMs();
    const Pwm::Rung& rung = Pwm::ladder()[mRung];

    if (mDriving) {
        const Pwm::BurstStats stats = mPwm.runBurst(Pwm::kBurstUs);
        if (stats.totalUs > 0u) {
            mAchievedDuty = stats.achievedPercent();
        }
    }

    if ((nowMs - mRungStartedMs) >= rung.holdMs) {
        if ((mRung + 1u) < Pwm::ladderSize()) {
            beginRung(mRung + 1u);
        } else {
            LOG_INFO("ladder complete: %lu periods, %lu edges\n",
                     static_cast<unsigned long>(mPwm.totals().periods),
                     static_cast<unsigned long>(mPwm.totals().edges));
            handOverPin();
            mState = CustomMessage::PwmState::Done;
            publish();
            return;
        }
    }

    if (mState != mLastPublishedState) {
        publish();
        return;
    }
    if (mGuiStarted && (nowMs - mLastPublishAtMs) >= kPublishPeriodMs) {
        publish();
    }
}

void Service::beginRung(size_t index)
{
    mRung          = index;
    mRungStartedMs = mKernel.sys.getTimeMs();
    mAchievedDuty  = 0;

    const Pwm::Rung& rung = Pwm::ladder()[index];
    mPwm.setDuty(rung.duty);

    LOG_INFO("rung %u/%u duty=%u%% for %lu ms (%s)\n", static_cast<unsigned>(index + 1u),
             static_cast<unsigned>(Pwm::ladderSize()), static_cast<unsigned>(rung.duty),
             static_cast<unsigned long>(rung.holdMs), rung.label);

    publish();
}

void Service::askKernelToHoldLight()
{
    const Backlight::Outcome outcome =
        Backlight::request(mKernel, 100, Pwm::kKernelHoldMs, kSendTimeoutMs);

    LOG_INFO("asked kernel to hold the light: sent=%c result=%s\n", outcome.sent ? 'Y' : 'N',
             Backlight::resultName(outcome.result));
}

void Service::handleStart()
{
    if (mState == CustomMessage::PwmState::Running) {
        LOG_INFO("start ignored: already running\n");
        return;
    }

    if (!Pwm::available()) {
        LOG_INFO("start refused: no registers on this build\n");
        mState = CustomMessage::PwmState::Refused;
        publish();
        return;
    }

    // Calibrate before anything is driven. A PWM timed off a counter that is not
    // running would produce a waveform nobody could vouch for, and it would look
    // like a result rather than like a failure.
    if (!mClock.calibrate(&millisThunk)) {
        LOG_INFO("start refused: cycle counter would not calibrate\n");
        mState   = CustomMessage::PwmState::Refused;
        mDriving = false;
        publish();
        return;
    }

    // Put the kernel's own state machine into "on" first, so it is not trying to
    // turn the light off underneath us for the whole run.
    askKernelToHoldLight();

    mDriving = true;
    mState   = CustomMessage::PwmState::Running;
    beginRung(0);
}

void Service::handOverPin()
{
    if (!mDriving) {
        return;
    }
    mDriving = false;

    mPwm.off();
    mClock.release();

    // Resync the kernel's view with the pin's actual state. Without this the
    // kernel still believes the light is on and would not turn it off until its
    // ten minute timer expired.
    const Backlight::Outcome outcome = Backlight::request(mKernel, 0, 0, kSendTimeoutMs);
    LOG_INFO("pin handed back: off sent=%c result=%s\n", outcome.sent ? 'Y' : 'N',
             Backlight::resultName(outcome.result));
}

void Service::handleCommand(SDK::MessageBase* msg)
{
    if (msg->getType() != CustomMessage::PWM_COMMAND) {
        return;
    }

    const auto* command = static_cast<CustomMessage::PwmCommand*>(msg);
    switch (static_cast<CustomMessage::Command>(command->command)) {
        case CustomMessage::Command::Start:
            handleStart();
            break;

        case CustomMessage::Command::Stop:
            LOG_INFO("stop requested\n");
            handOverPin();
            mState = CustomMessage::PwmState::Done;
            publish();
            break;

        case CustomMessage::Command::Resend:
            publish();
            break;
    }
}

void Service::publish()
{
    mLastPublishAtMs    = mKernel.sys.getTimeMs();
    mLastPublishedState = mState;

    if (!mGuiStarted) {
        return;
    }

    auto guard = SDK::make_msg<CustomMessage::PwmStatus>(mKernel);
    if (!guard) {
        return;
    }

    const Pwm::Rung& rung = Pwm::ladder()[mRung];

    guard->elapsedMs   = mKernel.sys.getTimeMs() - mRungStartedMs;
    guard->holdMs      = rung.holdMs;
    guard->edges       = mPwm.totals().edges;
    guard->periods     = mPwm.totals().periods;
    guard->cyclesPerUs = mClock.cyclesPerUs();

    guard->rungIndex = static_cast<uint16_t>(mRung);
    guard->rungCount = static_cast<uint16_t>(Pwm::ladderSize());

    guard->requestedDuty = rung.duty;
    guard->achievedDuty  = mAchievedDuty;
    guard->state         = static_cast<uint8_t>(mState);
    guard->driving       = mDriving;

    const char* label = rung.label ? rung.label : "";
    std::strncpy(guard->label, label, CustomMessage::kLabelMax - 1);
    guard->label[CustomMessage::kLabelMax - 1] = '\0';

    guard.send();
}
