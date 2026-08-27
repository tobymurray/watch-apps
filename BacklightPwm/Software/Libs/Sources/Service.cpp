/**
 ******************************************************************************
 * @file    Service.cpp
 * @brief   Climbing the ladder, one burst per poll.
 ******************************************************************************
 */

#include "Service.hpp"

#include <cstring>

#include <cstdio>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"
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

void sleepThunk(uint32_t ms)
{
    if (gKernelForMillis) {
        gKernelForMillis->sys.delay(ms);
    }
}

/// Hands ISystem::delay to the PWM engine so it can sleep through off phases
/// rather than spinning them.
class KernelSleeper : public Pwm::ISleeper
{
public:
    void sleepMs(uint32_t ms) override { sleepThunk(ms); }
};

KernelSleeper gSleeper;

/// How long to wait for the kernel to acknowledge a backlight request. Bounded
/// so a kernel that does not answer costs a blink rather than a stall.
constexpr uint32_t kSendTimeoutMs = 250;

/// Breadcrumb, rewritten at every rung. See Service::writeBreadcrumb.
constexpr char kProgressPath[] = "progress.txt";
} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mPwm(mPin, mClock, &gSleeper)
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
    if (mState != CustomMessage::PwmState::Running) {
        return kIdleWaitMs;
    }

    // A held rung has nothing to modulate, so block here instead of returning
    // straight back into a burst that will do nothing. Short enough that the pin
    // is still re-asserted about fifty times a second, which is what overrides
    // the kernel if it writes the pin during a held rung.
    if (mLastBurstHeld) {
        return kHeldWaitMs;
    }

    // Modulating. Zero is documented as NON-blocking, which is what made the
    // first version a pure spin; what gives time back now is the sleep inside the
    // off phase and the yield below it, not this.
    return 0u;
}

void Service::poll()
{
    if (mState != CustomMessage::PwmState::Running) {
        return;
    }

    const uint32_t nowMs = mKernel.sys.getTimeMs();
    const Pwm::Rung& rung = Pwm::ladder()[mRung];

    if (mDriving) {
        // A hard ceiling on how long this app may hold the pin, independent of
        // the ladder's own arithmetic. Nothing should reach it; if anything ever
        // does, the light goes back to the kernel rather than staying on because
        // a rung's timing went wrong.
        if ((nowMs - mDriveStartedMs) > kMaxDriveMs) {
            LOG_INFO("drive cap of %lu ms reached, handing the pin back\n",
                     static_cast<unsigned long>(kMaxDriveMs));
            handOverPin();
            mState = CustomMessage::PwmState::Done;
            publish();
            return;
        }

        const Pwm::BurstStats stats = mPwm.runBurst(Pwm::kBurstUs);
        mRungOnUs      += stats.onUs;
        mLastBurstHeld  = stats.held;

        // Hand the CPU back, every burst, without exception. This is the line
        // whose absence rebooted the watch: a burst is tens of milliseconds of
        // busy waiting, and without a yield between them nothing else on the
        // system ever runs.
        mKernel.sys.yield();

        // Duty measured against the rung's wall clock rather than against the
        // time spent inside bursts, so the gaps between bursts are counted. They
        // are real off-time and the light is genuinely dimmer for them; a figure
        // that ignored them would flatter the technique.
        if (stats.held) {
            // Nothing was modulated, so there is no measured duty to report and
            // the requested one is exactly what the pin is doing.
            mAchievedDuty = mPwm.duty();
        } else {
            const uint32_t rungMs = nowMs - mRungStartedMs;
            if (rungMs > 0u) {
                // onUs / (rungMs * 1000) as a percentage, i.e. onUs / (rungMs * 10).
                const uint32_t pct = mRungOnUs / (rungMs * 10u);
                mAchievedDuty = static_cast<uint8_t>(pct > 100u ? 100u : pct);
            }
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

void Service::writeBreadcrumb(size_t index)
{
    const Pwm::Rung& rung = Pwm::ladder()[index];

    char line[160];
    const int n = std::snprintf(line, sizeof(line),
                                "rung=%u/%u duty=%u label=%s uptime_ms=%lu cycles_per_us=%lu\n"
                                "If this is the last line, the watch died here.\n",
                                static_cast<unsigned>(index + 1u),
                                static_cast<unsigned>(Pwm::ladderSize()),
                                static_cast<unsigned>(rung.duty), rung.label,
                                static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                                static_cast<unsigned long>(mClock.cyclesPerUs()));
    if (n <= 0) {
        return;
    }

    // Truncating rewrite rather than an append: only the last rung matters, and a
    // single short write is far less likely to be caught half-done by a reboot
    // than a growing file would be.
    std::unique_ptr<SDK::Interface::IFile> f = mKernel.fs.file(kProgressPath);
    if (!f || !f->open(true, true)) {
        return;
    }
    size_t bw = 0;
    f->write(line, static_cast<size_t>(n), bw);
    f->flush();
    f->close();
}

void Service::beginRung(size_t index)
{
    mRung          = index;
    mRungStartedMs = mKernel.sys.getTimeMs();
    mAchievedDuty  = 0;
    mRungOnUs      = 0;
    mLastBurstHeld = false;

    const Pwm::Rung& rung = Pwm::ladder()[index];
    mPwm.setDuty(rung.duty);

    LOG_INFO("rung %u/%u duty=%u%% for %lu ms (%s)\n", static_cast<unsigned>(index + 1u),
             static_cast<unsigned>(Pwm::ladderSize()), static_cast<unsigned>(rung.duty),
             static_cast<unsigned long>(rung.holdMs), rung.label);

    writeBreadcrumb(index);
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

    // Say so on screen before blocking for a tenth of a second. When this froze
    // on READY there was no way to tell from the watch whether the button had
    // even been seen.
    mState = CustomMessage::PwmState::Calibrating;
    publish();

    // Calibrate before anything is driven. A PWM timed off a counter that is not
    // running would produce a waveform nobody could vouch for, and it would look
    // like a result rather than like a failure.
    if (!mClock.calibrate(&millisThunk, &sleepThunk)) {
        LOG_INFO("start refused: cycle counter would not calibrate\n");
        mState   = CustomMessage::PwmState::Refused;
        mDriving = false;
        publish();
        return;
    }

    // Put the kernel's own state machine into "on" first, so it is not trying to
    // turn the light off underneath us for the whole run.
    askKernelToHoldLight();

    mDriving        = true;
    mDriveStartedMs = mKernel.sys.getTimeMs();
    mState          = CustomMessage::PwmState::Running;
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
