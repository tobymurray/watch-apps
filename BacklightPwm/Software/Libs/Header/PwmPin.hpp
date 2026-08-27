/**
 ******************************************************************************
 * @file    PwmPin.hpp
 * @brief   The one pin this app drives, and the clock it times edges against.
 ******************************************************************************
 *
 * ## What this app writes, in full
 *
 * `GPIOF BSRR` at `0x42021418`, and nothing else in the GPIO block. Not `MODER`,
 * not `OTYPER`, not `PUPDR`, not `AFRL`, not `RCC`. It does not need to: the
 * 2026-08-27 investigation measured PF3 already configured as a general purpose
 * open drain output, active low, which is exactly what a software PWM wants. The
 * kernel has done the setup; this app only modulates.
 *
 * That has three consequences worth stating plainly, because they are what make
 * this experiment cheap to reverse:
 *
 *   - **There is no state to restore.** `BSRR` is write only, reads as zero, and
 *     self clears. Stop writing it and the pin is exactly as the kernel left it.
 *   - **There is no read-modify-write race.** `BSRR` sets or clears individual
 *     bits atomically, so a write here can never clobber another pin on port F
 *     even if the kernel writes one in the same instant. Using `ODR` would have
 *     had that hazard for no benefit.
 *   - **A crash cannot leave the hardware misconfigured.** The worst an abrupt
 *     kill can do is leave the pin at whichever level the last write chose. That
 *     is the same on state or off state the kernel itself uses, through the same
 *     82R resistor on the same fixed 3V3 rail, and the kernel's next backlight
 *     event overwrites it.
 *
 * The timing source does need two writes, and they are debug registers rather
 * than anything the running system depends on. See `CycleClock`.
 *
 * ## Why PF3 and this polarity
 *
 * From `UNAWatch/una-hardware`, `UNAview_LS012 Rev1.3`: PF3 (ball D3) drives the
 * gate of `Q1`, an NTK3139PT1G P-channel MOSFET, high side, with `R1` 10K pulling
 * the gate up to 3V3 and `R2` 82R in series with the LED. A P-channel gate turns
 * **on** when pulled low, which is why the measured `ODR` bit 3 reads 0 when the
 * light is lit.
 *
 * So: `BR3` lights it, `BS3` extinguishes it. Getting that backwards produces a
 * PWM that is inverted rather than broken, which is the kind of mistake that
 * looks like a result, so the two are named for what they do to the light rather
 * than for what they do to the pin.
 *
 ******************************************************************************
 */

#ifndef PWM_PIN_HPP
#define PWM_PIN_HPP

#include <cstdint>

namespace Pwm
{

/// The light, as the PWM engine sees it. Abstract so the host tests can record
/// a waveform instead of driving hardware; there is no other reason for it.
class IPin
{
public:
    virtual ~IPin() = default;

    /// Light on. On device: pull PF3 low, turning the P-channel FET on.
    virtual void lightOn() = 0;

    /// Light off. On device: release PF3, letting R1 pull the gate up.
    virtual void lightOff() = 0;
};

/// Microsecond time source. Abstract for the same reason.
class IClock
{
public:
    virtual ~IClock() = default;

    /// Free running, wraps. Differences are what matter, never absolute values.
    virtual uint32_t nowUs() const = 0;
};

/**
 * @brief Giving time back to the kernel, in whole milliseconds.
 *
 * The off phase of a PWM period does not need the CPU. Holding it by spinning
 * works and is what the first version did, at the cost of running the whole
 * ladder at 100 percent CPU for 44 seconds, which the watch did not survive.
 * Sleeping through it instead drops the cost of each rung to roughly its own duty
 * cycle.
 *
 * Millisecond granularity is coarse against a 4 ms period, so a sleep is only
 * used when the remaining off time is comfortably longer than one tick and the
 * last stretch is always spun. What it costs in timing accuracy shows up in the
 * achieved duty, which is measured rather than assumed.
 */
class ISleeper
{
public:
    virtual ~ISleeper() = default;
    virtual void sleepMs(uint32_t ms) = 0;
};

/// True on a build with the registers this file needs.
bool available();

/**
 * @brief PF3 through `GPIOF BSRR`.
 *
 * A no-op on host builds rather than a fabrication: a PWM engine driving nothing
 * is honest, and one driving a variable pretending to be a pin invites somebody
 * to read host output as a hardware result.
 */
class Pf3Pin : public IPin
{
public:
    void lightOn() override;
    void lightOff() override;

    /// How many edges have been written. Read by the results file so a run that
    /// produced no light can be told apart from one that never wrote anything.
    uint32_t edges() const { return mEdges; }

private:
    uint32_t mEdges = 0;
};

/**
 * @brief Microseconds from the Cortex-M cycle counter, calibrated at runtime.
 *
 * `getTimeMs()` is the only clock the SDK offers an app and it is three orders of
 * magnitude too coarse for this: a 250 Hz PWM needs edges placed to tens of
 * microseconds. So this uses `DWT_CYCCNT`, which counts core clocks.
 *
 * **It calibrates rather than assuming a core frequency.** The clock tree on this
 * unit has never been decoded (the prior investigation captured RCC three times
 * and left the MSIRANGE/PLL bits unread), so a hardcoded MHz figure would be a
 * guess sitting underneath every timing number this app produces. Instead
 * `calibrate()` counts cycles across a known `getTimeMs()` interval and derives
 * cycles-per-microsecond from what the part is actually doing.
 *
 * ## The two writes
 *
 * `DEMCR.TRCENA` and `DWT_CTRL.CYCCNTENA`, both read first and both restored by
 * `release()`. They enable a counter; they change no program behaviour, touch no
 * peripheral the system uses, and are not option bytes. If the counter refuses to
 * run, `calibrate()` fails and the app declines to PWM rather than emitting a
 * waveform whose timing it cannot vouch for.
 */
class CycleClock : public IClock
{
public:
    /**
     * @brief Enable the counter if needed and measure its rate.
     * @return False if the counter will not run, in which case drive nothing.
     *
     * Takes `sleepMs` rather than spinning on `millis`. The first version of this
     * busy-waited on `getTimeMs()` and the watch rebooted: an app thread that
     * never yields starves everything else, including whatever would have
     * repainted the screen to show it was working. `ISystem::delay` hands the
     * time back to the kernel and comes back when the interval has passed, which
     * is what an app is supposed to do to wait.
     */
    bool calibrate(uint32_t (*millis)(), void (*sleepMs)(uint32_t));

    /// Restores DEMCR and DWT_CTRL to whatever they were before calibrate().
    void release();

    uint32_t nowUs() const override;

    /// Measured, not assumed. Zero until calibrate() succeeds.
    uint32_t cyclesPerUs() const { return mCyclesPerUs; }

    /// Whether the counter was already running before this app touched it.
    bool wasAlreadyOn() const { return mWasAlreadyOn; }

private:
    uint32_t mCyclesPerUs  = 0;
    uint32_t mPrevDemcr    = 0;
    uint32_t mPrevDwtCtrl  = 0;
    bool     mWasAlreadyOn = false;
    bool     mTouched      = false;

    /// The cycle counter wraps at 2^32 cycles, which is tens of seconds at this
    /// core clock. Dividing the raw counter into microseconds and subtracting
    /// two of those would be wrong across that wrap, because the quotient's
    /// range is not a power of two. So differences are taken in cycles, where
    /// unsigned subtraction is exact across the wrap, and accumulated. mCarry
    /// keeps the sub-microsecond remainder so the accumulator does not drift by
    /// up to a microsecond on every call.
    mutable uint32_t mLastCycles = 0;
    mutable uint32_t mAccumUs    = 0;
    mutable uint32_t mCarry      = 0;
};

} // namespace Pwm

#endif // PWM_PIN_HPP
