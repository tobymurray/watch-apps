/**
 ******************************************************************************
 * @file    PwmPin.cpp
 * @brief   The three register addresses this app touches.
 ******************************************************************************
 */

#include "PwmPin.hpp"

#define LOG_MODULE_PRX      "Pin"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace
{

#if defined(SIMULATOR) || !defined(__ARM_ARCH)
constexpr bool kHasRegisters = false;
#else
constexpr bool kHasRegisters = true;

/// GPIOF base 0x42021400, confirmed by the 2026-08-27 sweep; BSRR at offset 0x18.
constexpr uint32_t kGpiofBsrr = 0x42021418u;

/// PF3. BR (bit reset, upper half) drives the pin low, which lights the LED
/// because the FET it gates is P-channel. BS (bit set) releases it.
constexpr uint32_t kBr3 = 1u << (16 + 3);
constexpr uint32_t kBs3 = 1u << 3;

constexpr uint32_t kDemcr    = 0xE000EDFCu; ///< bit 24 TRCENA
constexpr uint32_t kDwtCtrl  = 0xE0001000u; ///< bit 0 CYCCNTENA
constexpr uint32_t kDwtCycnt = 0xE0001004u;

constexpr uint32_t kTrcena     = 1u << 24;
constexpr uint32_t kCyccntena  = 1u << 0;

inline uint32_t rd(uint32_t a) { return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(a)); }
inline void     wr(uint32_t a, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(a)) = v; }
#endif

/// How long to count cycles over when working out the clock rate.
///
/// Short, because this interval is a **busy wait** and busy waits are what
/// rebooted this watch once already. 25 ms is three PWM bursts, and the tick
/// quantisation over it is a few percent, which is far better than this
/// measurement needs.
constexpr uint32_t kCalibrateMs = 25;

/// Refuse a calibration slower than this. The part is a Cortex-M33 running an
/// RTOS, a GUI and a BLE stack; single-digit megahertz is not a plausible answer,
/// it is a broken measurement.
///
/// This floor exists because its absence cost a whole run. Calibrating across
/// `delay()` returned **1 cycle per microsecond**, which is nonsense but not
/// zero, so it sailed past the only check there was and every timing in the app
/// came out about 160 times too fast. The ladder still looked plausible on the
/// screen and meant nothing.
constexpr uint32_t kMinCyclesPerUs = 8;

} // namespace

namespace Pwm
{

bool available() { return kHasRegisters; }

void Pf3Pin::lightOn()
{
#if !defined(SIMULATOR) && defined(__ARM_ARCH)
    wr(kGpiofBsrr, kBr3);
#endif
    ++mEdges;
}

void Pf3Pin::lightOff()
{
#if !defined(SIMULATOR) && defined(__ARM_ARCH)
    wr(kGpiofBsrr, kBs3);
#endif
    ++mEdges;
}

bool CycleClock::calibrate(uint32_t (*millis)(), void (*sleepMs)(uint32_t))
{
#if defined(SIMULATOR) || !defined(__ARM_ARCH)
    (void)millis;
    (void)sleepMs;
    LOG_INFO("no cycle counter on this build\n");
    return false;
#else
    if (millis == nullptr || sleepMs == nullptr) {
        return false;
    }

    mPrevDemcr   = rd(kDemcr);
    mPrevDwtCtrl = rd(kDwtCtrl);
    mWasAlreadyOn = ((mPrevDemcr & kTrcena) != 0u) && ((mPrevDwtCtrl & kCyccntena) != 0u);

    if (!mWasAlreadyOn) {
        wr(kDemcr, mPrevDemcr | kTrcena);
        wr(kDwtCtrl, rd(kDwtCtrl) | kCyccntena);
        mTouched = true;
    }

    // A busy wait, deliberately, and this is the one place in the app that has to
    // be one.
    //
    // `DWT_CYCCNT` counts core clocks and **stops when the core sleeps**. So
    // calibrating across `ISystem::delay()` measures the cycles the CPU happened
    // to be awake for during that interval rather than the cycles in it, and
    // returns a number far too small. That is exactly what happened: it reported
    // 1 cycle per microsecond, the PWM period became 4000 cycles instead of
    // 4000 microseconds, and the whole ladder ran at tens of kilohertz.
    //
    // Bounded at 25 ms, which is the same order as a single PWM burst.
    (void)sleepMs;
    const uint32_t startMs = millis();
    const uint32_t startCy = rd(kDwtCycnt);

    while ((millis() - startMs) < kCalibrateMs) {
        // Spinning on purpose: the core must stay awake for the counter to count.
    }

    const uint32_t elapsedMs = millis() - startMs;
    const uint32_t elapsedCy = rd(kDwtCycnt) - startCy;

    if (elapsedCy == 0u || elapsedMs == 0u) {
        // The counter is not running, and no amount of writing to it helped.
        // Declining is the right answer: a PWM timed off a dead counter would
        // produce a waveform nobody could vouch for, and it would look like a
        // result.
        LOG_INFO("cycle counter did not advance, refusing to drive\n");
        release();
        return false;
    }

    mCyclesPerUs = elapsedCy / (elapsedMs * 1000u);
    mLastCycles  = rd(kDwtCycnt);
    mAccumUs     = 0;
    mCarry       = 0;
    if (mCyclesPerUs < kMinCyclesPerUs) {
        // Nonsense rather than zero, which is the case that got through before.
        LOG_INFO("calibration implausible: %lu cycles in %lu ms is %lu MHz, refusing\n",
                 static_cast<unsigned long>(elapsedCy), static_cast<unsigned long>(elapsedMs),
                 static_cast<unsigned long>(mCyclesPerUs));
        release();
        return false;
    }

    LOG_INFO("cycle counter %s, %lu cycles/us (~%lu MHz)\n",
             mWasAlreadyOn ? "already on" : "enabled by us",
             static_cast<unsigned long>(mCyclesPerUs), static_cast<unsigned long>(mCyclesPerUs));
    return true;
#endif
}

void CycleClock::release()
{
#if !defined(SIMULATOR) && defined(__ARM_ARCH)
    if (mTouched) {
        wr(kDwtCtrl, mPrevDwtCtrl);
        wr(kDemcr, mPrevDemcr);
        mTouched = false;
    }
#endif
    mCyclesPerUs = 0;
}

uint32_t CycleClock::nowUs() const
{
#if defined(SIMULATOR) || !defined(__ARM_ARCH)
    return 0;
#else
    if (mCyclesPerUs == 0u) {
        return 0;
    }

    // Differences first, then conversion. See the members' comment: the counter
    // wraps at 2^32 cycles and a microsecond quotient does not, so subtracting
    // two quotients would produce a large positive nonsense value once every
    // wrap. Cycles subtract correctly across it.
    const uint32_t cycles = rd(kDwtCycnt);
    const uint32_t delta  = cycles - mLastCycles;
    mLastCycles = cycles;

    mCarry += delta;
    mAccumUs += mCarry / mCyclesPerUs;
    mCarry %= mCyclesPerUs;
    return mAccumUs;
#endif
}

} // namespace Pwm
