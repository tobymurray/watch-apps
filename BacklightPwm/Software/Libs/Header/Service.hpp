/**
 ******************************************************************************
 * @file    Service.hpp
 * @brief   BacklightPwm service: takes the pin, modulates it, hands it back.
 ******************************************************************************
 *
 * ## This app writes registers. Here is the complete list
 *
 *   - `GPIOF BSRR` at `0x42021418`, bit 3 set or reset. The light.
 *   - `DEMCR` bit 24 and `DWT_CTRL` bit 0, to start the cycle counter, both read
 *     first and both restored on the way out.
 *
 * That is all of it. No `MODER`, no `OTYPER`, no `AFRL`, no `RCC`, no flash, and
 * nothing anywhere near the option bytes. PF3 is already configured the way this
 * needs it, so there is no configuration to change and none to put back.
 *
 * It is still a different category of thing from `BacklightProbe`, which only
 * ever sent a message. This one takes a pin the kernel also owns.
 *
 * ## It must yield, and the first version did not
 *
 * A burst is tens of milliseconds of busy waiting. Between bursts this service
 * calls `ISystem::yield()`, every time, without exception. The first version
 * left that out and relied on a zero-timeout `getMessage` to hand time back;
 * zero is documented as *non-blocking*, so nothing was ever handed back. The
 * service thread spun, the GUI thread never ran, the screen never left READY,
 * and the watch rebooted.
 *
 * Calibration has the same shape and had the same bug: it busy-waited on
 * `getTimeMs()` rather than calling `ISystem::delay()`. Both are fixed, and both
 * are worth remembering as one rule: an app thread that does not yield takes the
 * whole system down with it.
 *
 * ## What it does about that
 *
 * It asks first, in the only sense available: before touching the pin it sends a
 * normal `RequestBacklightSet(100, 10 minutes)`, so the kernel's own state
 * machine believes the backlight is on and is not trying to turn it off during
 * the run. Then it modulates the pin underneath that.
 *
 * The kernel can still reassert itself, on a wrist raise, an idle timeout, a
 * notification or its own auto-off. When it does, its write lands between two of
 * this app's writes and is overridden within one PWM period, four milliseconds.
 * **That contest is the experiment.** A run where the light visibly survives an
 * auto-off that should have killed it is the finding, not a bug.
 *
 * ## Giving it back
 *
 * Three paths, and all of them end the same way:
 *
 *   - The ladder finishes: the last rung is duty 0, then a
 *     `RequestBacklightSet(0, 0)` resyncs the kernel's view with the pin's state.
 *   - `Stop` from the screen: same thing, immediately.
 *   - `COMMAND_APP_STOP`, which USB insertion causes without warning: same
 *     thing, on the way out.
 *
 * A kill so abrupt that none of those run leaves the pin wherever the last write
 * put it. That is the same on or off state the kernel itself drives, through the
 * same 82R resistor on the same fixed 3V3 rail, and the kernel's next backlight
 * event overwrites it. The failure mode is a light left on, not damage.
 *
 ******************************************************************************
 */

#ifndef SERVICE_HPP
#define SERVICE_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Commands.hpp"
#include "PwmPin.hpp"
#include "PwmPlan.hpp"
#include "SoftPwm.hpp"

class Service
{
public:
    explicit Service(SDK::Kernel& kernel);

    virtual ~Service() = default;

    void run();

    /// One iteration of what run() does between message waits: at most one PWM
    /// burst, plus whatever bookkeeping has come due.
    void poll();

    /// Message-wait period run() would use next. Non-blocking while driving, so
    /// the burst cadence is not set by a sleep. What hands the CPU back is the
    /// explicit yield in poll(), not this.
    uint32_t nextWaitMs() const;

    void publish();

private:
    /// Message wait with nothing to do.
    static constexpr uint32_t kIdleWaitMs = 1000;

    /// How often to publish while the ladder runs.
    static constexpr uint32_t kPublishPeriodMs = 200;

    /// Hard ceiling on how long this app may hold the pin, independent of the
    /// ladder's own arithmetic. Comfortably longer than the ladder; nothing
    /// should ever reach it. It exists so that a timing bug ends with the light
    /// handed back rather than with the light left on.
    static constexpr uint32_t kMaxDriveMs = 120000;

    SDK::Kernel& mKernel;

    Pwm::Pf3Pin     mPin;
    Pwm::CycleClock mClock;
    Pwm::SoftPwm    mPwm;

    bool mGuiStarted = false;

    CustomMessage::PwmState mState = CustomMessage::PwmState::Idle;
    CustomMessage::PwmState mLastPublishedState = CustomMessage::PwmState::Idle;

    size_t   mRung          = 0;
    uint32_t mRungStartedMs = 0;
    uint32_t mLastPublishAtMs = 0;

    /// Achieved duty for the current rung, for the screen. Measured against the
    /// rung's wall clock so the gaps between bursts count as the off-time they
    /// really are.
    uint8_t  mAchievedDuty = 0;
    uint32_t mRungOnUs     = 0;

    /// When driving began, for the cap above.
    uint32_t mDriveStartedMs = 0;

    /// Whether calibration succeeded and the pin is ours to write. False means
    /// the app runs, shows why it declined, and touches nothing.
    bool mDriving = false;

    void handleCommand(SDK::MessageBase* msg);
    void handleStart();

    /// Duty 0, kernel resynced, cycle counter released. Idempotent.
    void handOverPin();

    /// Tell the kernel the light should be on and stay on, so it is not fighting
    /// for the pin during the run.
    void askKernelToHoldLight();

    void beginRung(size_t index);
};

#endif // SERVICE_HPP
