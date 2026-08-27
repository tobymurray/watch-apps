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

/// How long to count cycles over when working out the clock rate. Long enough
/// that the millisecond tick's own quantisation is under a percent, short enough
/// that startup does not visibly stall.
constexpr uint32_t kCalibrateMs = 100;

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

bool CycleClock::calibrate(uint32_t (*millis)())
{
#if defined(SIMULATOR) || !defined(__ARM_ARCH)
    (void)millis;
    LOG_INFO("no cycle counter on this build\n");
    return false;
#else
    if (millis == nullptr) {
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

    // Measure across whole millisecond ticks rather than from an arbitrary
    // instant, so the tick's own granularity does not land inside the interval
    // being measured.
    const uint32_t t0 = millis();
    while (millis() == t0) { }
    const uint32_t startMs = millis();
    const uint32_t startCy = rd(kDwtCycnt);

    while ((millis() - startMs) < kCalibrateMs) { }

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
    if (mCyclesPerUs == 0u) {
        LOG_INFO("core clock below 1 MHz? %lu cycles in %lu ms, refusing\n",
                 static_cast<unsigned long>(elapsedCy), static_cast<unsigned long>(elapsedMs));
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
