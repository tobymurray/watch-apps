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



/// How long to wait for the kernel to acknowledge a backlight request. Bounded
/// so a kernel that does not answer costs a blink rather than a stall.
constexpr uint32_t kSendTimeoutMs = 250;

/// Breadcrumb, rewritten at every rung. See Service::writeBreadcrumb.
constexpr char kProgressPath[] = "progress.txt";

/// The full record. See PwmLog.hpp.
constexpr char kResultsPath[] = "backlight_pwm.txt";

/// Presence of this file selects the discrimination ladder: pairs of adjacent
/// duties, alternating, to find where two levels stop being distinguishable.
/// Absent, the standard six-level ladder runs.
///
/// A marker file rather than a button, because the choice has to survive the app
/// being launched by someone who is not looking at the screen, and because there
/// is exactly one bit to communicate.
constexpr char kFlipMarkerPath[] = "flip.enable";

/// Presence of this file drives the light with a timer and a DMA channel instead
/// of a busy-waiting thread. Same pin, same waveform, no CPU.
///
/// Absent by default because it is the only part of this app that writes
/// configuration rather than just BSRR, and because the software engine is what
/// the earlier results were measured with.
constexpr char kDmaMarkerPath[] = "dma.enable";
} // namespace

Service::Service(SDK::Kernel& kernel)
    : mKernel(kernel)
    , mPwm(mPin, mClock)
{
    gKernelForMillis = &kernel;
    mPwm.setPeriodUs(Pwm::kPeriodUs);
}

const Pwm::Rung* Service::ladder() const
{
    return mDiscriminate ? Pwm::flipLadder() : Pwm::ladder();
}

size_t Service::ladderCount() const
{
    return mDiscriminate ? Pwm::flipLadderSize() : Pwm::ladderSize();
}

void Service::driveDuty(uint8_t duty)
{
    if (mUseDma) {
        mDma.setDuty(duty);
    } else {
        mPwm.setDuty(duty);
    }
}

void Service::applyFlip(uint32_t nowMs, const Pwm::Rung& rung)
{
    if (rung.dutyB == 0u || rung.dutyB == rung.duty) {
        return; // Not a discrimination pair.
    }

    // Which half we are in, derived from the rung's own clock rather than a
    // counter, so a burst that runs long cannot drift the alternation.
    const bool second = (((nowMs - mRungStartedMs) / Pwm::kFlipMs) % 2u) != 0u;
    if (second == mFlipSecondHalf) {
        return;
    }

    mFlipSecondHalf = second;
    driveDuty(second ? rung.dutyB : rung.duty);
}

void Service::openResults()
{
    mResultsFile = mKernel.fs.file(kResultsPath);
    if (!mResultsFile || !mResultsFile->open(true, true)) {
        // Not fatal: the run still happens and still logs. A run whose record
        // only reached a UART capture is worth more than no run.
        LOG_INFO("could not open %s\n", kResultsPath);
        mResultsFile.reset();
        return;
    }
    mLog = std::make_unique<Pwm::PwmLog>(*mResultsFile);
}

void Service::run()
{
    LOG_INFO("started\n");

    // Which ladder this run walks. Read once, before anything else, so the
    // record and the screen agree with what actually ran.
    mDiscriminate = mKernel.fs.exist(kFlipMarkerPath);
    mUseDma       = mKernel.fs.exist(kDmaMarkerPath);
    LOG_INFO("engine: %s\n", mUseDma ? "timer + DMA (no CPU)" : "software PWM (busy wait)");
    LOG_INFO("ladder: %s (%u rungs)\n",
             mDiscriminate ? "discrimination pairs" : "standard six levels",
             static_cast<unsigned>(ladderCount()));

    openResults();

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

    // The hardware engine needs nothing from this thread between re-arms, so the
    // service simply sleeps on the queue. This is the difference the whole
    // exercise is about: with the software engine the same rung would be spinning.
    if (mUseDma) {
        return kHeldWaitMs;
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
    const Pwm::Rung& rung = ladder()[mRung];

    // Before the burst, so the whole burst runs at one duty.
    applyFlip(nowMs, rung);

    if (mDriving && mUseDma) {
        // The entire cost of running the hardware engine: check whether the
        // block-repeat counter has run out and re-arm it if so, roughly once
        // every ten seconds. No burst, no spin, no yield. The waveform continues
        // regardless of what this thread does.
        mDma.poll();

        // The duty the hardware was told to produce. Not a measurement, unlike
        // the software engine's, because nothing here counts microseconds: the
        // DMA does not report back. Reported as commanded and labelled as such
        // rather than dressed up as an achievement.
        mAchievedDuty = mDma.duty();

        // Safe to publish periodically here, which it is not on the software
        // engine: a publish wakes the GUI, and on the software engine that
        // preempts a busy loop mid-pulse and shows up as a flash. Nothing is
        // spinning here, so the screen can stay current.
        if (mGuiStarted && (nowMs - mLastPublishAtMs) >= kIdlePublishPeriodMs) {
            publish();
        }
    }

    if (mDriving && !mUseDma) {
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

        const Pwm::BurstStats stats = mPwm.runBurst(Pwm::kPeriodsPerBurst);
        mLastBurstHeld = stats.held;

        if (stats.held) {
            // A held burst sets the pin and returns, so it reports no periods
            // and no microseconds. Bill its wall-clock time instead, or any
            // stretch at full brightness counts as zero on-time and drags the
            // rung's achieved duty down with it.
            if (mLastPollMs != 0u && mPwm.duty() == 100u) {
                mRungOnUs += (nowMs - mLastPollMs) * 1000u;
            }
        } else {
            mRungOnUs += stats.onUs;
        }
        mLastPollMs = nowMs;

        // Read the pin back before yielding, while what this app last wrote is
        // still the most recent thing anyone wrote. See checkForKernelWrite.
        checkForKernelWrite(nowMs);

        // A yield on top of the sleeping that already happens inside each
        // period's off phase. The sleep is what actually gives the GUI thread
        // time; this just hands back the remainder of the current slice.
        mKernel.sys.yield();

        // Duty measured against the rung's wall clock rather than against the
        // time spent inside bursts, so the gaps between bursts are counted. They
        // are real off-time and the light is genuinely dimmer for them; a figure
        // that ignored them would flatter the technique.
        const uint32_t rungMs = nowMs - mRungStartedMs;
        if (rungMs > 0u) {
            // onUs / (rungMs * 1000) as a percentage, i.e. onUs / (rungMs * 10).
            // Held stretches contribute through mRungOnUs above, so a rung that
            // alternates between a modulated duty and a held 100 reports the
            // average of the two rather than half of it.
            const uint32_t pct = mRungOnUs / (rungMs * 10u);
            mAchievedDuty = static_cast<uint8_t>(pct > 100u ? 100u : pct);
        }
    }

    if ((nowMs - mRungStartedMs) >= rung.holdMs) {
        mResult.achieved  = mAchievedDuty;
        mResult.periods   = mPwm.totals().periods;
        mResult.edges     = mPin.edges();
        mResult.elapsedMs = nowMs - mRungStartedMs;
        if (mLog) {
            mLog->rungDone(mResult, rung);
        }

        if ((mRung + 1u) < ladderCount()) {
            beginRung(mRung + 1u);
        } else {
            LOG_INFO("ladder complete: %lu periods, %lu edges\n",
                     static_cast<unsigned long>(mPwm.totals().periods),
                     static_cast<unsigned long>(mPwm.totals().edges));
            handOverPin();
            if (mLog) {
                mLog->footer(mKernel.sys.getTimeMs(), mPwm.totals().periods, mPin.edges(),
                             mTotalKernelWrites);
            }
            mState = CustomMessage::PwmState::Done;
            publish();
            return;
        }
    }

    // Published on a state change and at every rung boundary (see beginRung),
    // and at no other time while the plan runs. See kIdlePublishPeriodMs: a
    // periodic publish here is a periodic GUI wake-up, and a GUI wake-up in the
    // middle of a pulse is a visible flash.
    if (mState != mLastPublishedState) {
        publish();
    }
}

void Service::writeBreadcrumb(size_t index)
{
    const Pwm::Rung& rung = ladder()[index];

    char line[160];
    const int n = std::snprintf(line, sizeof(line),
                                "rung=%u/%u duty=%u label=%s uptime_ms=%lu cycles_per_us=%lu\n"
                                "If this is the last line, the watch died here.\n",
                                static_cast<unsigned>(index + 1u),
                                static_cast<unsigned>(ladderCount()),
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
    mRungOnUs       = 0;
    mLastBurstHeld  = false;
    mFlipSecondHalf = false;
    mLastPollMs     = 0;

    const Pwm::Rung& rung = ladder()[index];
    driveDuty(rung.duty);

    mResult             = Pwm::RungResult{};
    mResult.index       = index;
    mResult.requested   = rung.duty;
    mResult.startedAtMs = mRungStartedMs;

    // Whatever this rung wants the kernel to believe, said before the first
    // burst so the contest starts from a known state.
    applyKernelAsk(rung.ask);

    if (mLog) {
        mLog->rungBegan(index, rung, mRungStartedMs);
    }

    LOG_INFO("rung %u/%u duty=%u%% for %lu ms (%s)\n", static_cast<unsigned>(index + 1u),
             static_cast<unsigned>(ladderCount()), static_cast<unsigned>(rung.duty),
             static_cast<unsigned long>(rung.holdMs), rung.label);

    writeBreadcrumb(index);
    publish();
}

void Service::applyKernelAsk(Pwm::KernelAsk ask)
{
    switch (ask) {
        case Pwm::KernelAsk::Nothing:
            return;

        case Pwm::KernelAsk::HoldOn:
            askKernelToHoldLight();
            return;

        case Pwm::KernelAsk::ShortAutoOff: {
            const Backlight::Outcome o =
                Backlight::request(mKernel, 100, Pwm::kShortAutoOffMs, kSendTimeoutMs);
            LOG_INFO("contest: kernel asked for on with %lu ms auto-off, result=%s\n",
                     static_cast<unsigned long>(Pwm::kShortAutoOffMs),
                     Backlight::resultName(o.result));
            return;
        }

        case Pwm::KernelAsk::TurnOff: {
            const Backlight::Outcome o = Backlight::request(mKernel, 0, 0, kSendTimeoutMs);
            LOG_INFO("contest: kernel told backlight off, result=%s\n",
                     Backlight::resultName(o.result));
            return;
        }
    }
}

void Service::checkForKernelWrite(uint32_t nowMs)
{
    ++mResult.samples;

    // Sampled at a burst boundary, where this app knows exactly what it last
    // wrote. Any disagreement is somebody else's write, and the only other
    // writer is the kernel.
    if (mPin.lightOnPerOdr() == mPin.lastCommandedOn()) {
        return;
    }

    ++mResult.kernelWrites;
    ++mTotalKernelWrites;
    if (mResult.firstKernelWriteMs == 0u) {
        mResult.firstKernelWriteMs = nowMs;
        LOG_INFO("kernel wrote PF3 at %lu ms (we had it %s)\n",
                 static_cast<unsigned long>(nowMs), mPin.lastCommandedOn() ? "on" : "off");
    }
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
        if (mLog) {
            mLog->refused("no peripheral registers on this build");
        }
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
        if (mLog) {
            mLog->refused("cycle counter would not calibrate to a plausible clock");
        }
        mState   = CustomMessage::PwmState::Refused;
        mDriving = false;
        publish();
        return;
    }

    if (mLog) {
        mLog->header(mKernel.sys.getTimeMs(), true, mClock.cyclesPerUs(), Pwm::kPeriodUs,
                     Pwm::kPeriodsPerBurst, ladderCount());
    }

    // Put the kernel's own state machine into "on" first, so it is not trying to
    // turn the light off underneath us for the whole run.
    askKernelToHoldLight();

    if (mUseDma) {
        const Pwm::DmaStatus st = mDma.start(ladder()[0].duty, Pwm::kPeriodUs, &sleepThunk);
        if (st != Pwm::DmaStatus::Running) {
            LOG_INFO("DMA engine refused: %s\n", Pwm::dmaStatusName(st));
            if (mLog) {
                mLog->refused(Pwm::dmaStatusName(st));
            }
            mState   = CustomMessage::PwmState::Refused;
            mDriving = false;
            publish();
            return;
        }
        // Written after the start, not with the rest of the header: the timer's
        // clock is not known until the start measures it.
        if (mLog) {
            mLog->dmaHeader(mDma.timerKhz(), mDma.timerIndex(), mDma.channelIndex());
        }
    }

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

    if (mUseDma) {
        mDma.stop();
    }
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
            if (mLog) {
                mLog->footer(mKernel.sys.getTimeMs(), mPwm.totals().periods, mPin.edges(),
                             mTotalKernelWrites);
            }
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

    const Pwm::Rung& rung = ladder()[mRung];

    guard->elapsedMs   = mKernel.sys.getTimeMs() - mRungStartedMs;
    guard->holdMs      = rung.holdMs;
    guard->edges       = mPwm.totals().edges;
    guard->periods     = mPwm.totals().periods;
    guard->cyclesPerUs  = mClock.cyclesPerUs();
    guard->runElapsedMs = (mDriveStartedMs != 0u) ? (mKernel.sys.getTimeMs() - mDriveStartedMs) : 0u;
    guard->kernelWrites = mTotalKernelWrites;

    guard->rungIndex = static_cast<uint16_t>(mRung);
    guard->rungCount = static_cast<uint16_t>(ladderCount());

    guard->requestedDuty = mFlipSecondHalf && rung.dutyB ? rung.dutyB : rung.duty;
    guard->pairDuty      = rung.dutyB;
    guard->achievedDuty  = mAchievedDuty;
    guard->state         = static_cast<uint8_t>(mState);
    guard->driving       = mDriving;

    const char* label = rung.label ? rung.label : "";
    std::strncpy(guard->label, label, CustomMessage::kLabelMax - 1);
    guard->label[CustomMessage::kLabelMax - 1] = '\0';

    guard.send();
}
