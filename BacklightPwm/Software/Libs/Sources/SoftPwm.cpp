/**
 ******************************************************************************
 * @file    SoftPwm.cpp
 * @brief   The burst loop.
 ******************************************************************************
 */

#include "SoftPwm.hpp"

namespace Pwm
{

namespace
{
/// Below this the busy wait spends more time in loop overhead than in the delay
/// it is trying to produce, and the duty stops meaning anything.
constexpr uint32_t kMinPeriodUs = 200;    // 5 kHz

/// Above this the eye sees flicker rather than a dimmed light, which would make
/// every photograph taken of it worthless.
constexpr uint32_t kMaxPeriodUs = 20000;  // 50 Hz

/// Only sleep through an off phase longer than this, and always spin the last
/// millisecond. `delay()` takes whole milliseconds and may overshoot by a tick,
/// which against a 4 ms period would distort the duty badly if it were used for
/// the fine end of the wait.
constexpr uint32_t kSleepFloorUs = 2000;
} // namespace

SoftPwm::SoftPwm(IPin& pin, const IClock& clock, ISleeper* sleeper)
    : mPin(pin)
    , mClock(clock)
    , mSleeper(sleeper)
{
}

void SoftPwm::setDuty(uint8_t percent)
{
    mDuty = percent > 100u ? 100u : percent;
}

void SoftPwm::setPeriodUs(uint32_t periodUs)
{
    if (periodUs < kMinPeriodUs) {
        periodUs = kMinPeriodUs;
    } else if (periodUs > kMaxPeriodUs) {
        periodUs = kMaxPeriodUs;
    }
    mPeriodUs = periodUs;
}

uint32_t SoftPwm::spinUntil(uint32_t deadlineUs) const
{
    uint32_t now = mClock.nowUs();
    // Signed comparison of the difference, so this is correct when the clock
    // wraps between the deadline being computed and being reached.
    while (static_cast<int32_t>(deadlineUs - now) > 0) {
        now = mClock.nowUs();
    }
    return now;
}

uint32_t SoftPwm::sleepThenSpinUntil(uint32_t deadlineUs)
{
    if (mSleeper != nullptr) {
        const uint32_t now       = mClock.nowUs();
        const int32_t  remaining = static_cast<int32_t>(deadlineUs - now);

        if (remaining > static_cast<int32_t>(kSleepFloorUs)) {
            // One millisecond short of the target, so the spin below closes the
            // gap rather than the sleep overshooting past it.
            const uint32_t sleepMs = (static_cast<uint32_t>(remaining) / 1000u) - 1u;
            if (sleepMs > 0u) {
                mSleeper->sleepMs(sleepMs);
            }
        }
    }
    return spinUntil(deadlineUs);
}

void SoftPwm::off()
{
    mPin.lightOff();
    ++mTotals.edges;
}

BurstStats SoftPwm::runBurst(uint32_t budgetUs)
{
    BurstStats stats;

    const uint32_t startUs = mClock.nowUs();
    const uint32_t endUs   = startUs + budgetUs;

    // The two endpoints are held rather than modulated. A 100 percent duty that
    // still toggled every period would show any transition glitch as a dimming
    // at full brightness, which is precisely the artefact that would discredit
    // the whole result.
    if (mDuty == 0u || mDuty == 100u) {
        if (mDuty == 0u) {
            mPin.lightOff();
        } else {
            mPin.lightOn();
        }
        ++stats.edges;

        // Set and return. Holding a level costs nothing, and the caller is
        // expected to sleep rather than call straight back; see BurstStats::held.
        // The first version spun out the whole budget here, which bought nothing
        // and cost eight seconds of the ladder at full CPU.
        stats.held    = true;
        stats.totalUs = 0;
        stats.onUs    = 0;
        stats.periods = 0;

        mTotals.edges += stats.edges;
        return stats;
    }

    const uint32_t onUs = (mPeriodUs * mDuty) / 100u;

    uint32_t cycleStart = startUs;
    while (static_cast<int32_t>(endUs - cycleStart) >= static_cast<int32_t>(mPeriodUs)) {
        mPin.lightOn();
        ++stats.edges;
        const uint32_t offAt = spinUntil(cycleStart + onUs);

        mPin.lightOff();
        ++stats.edges;
        // The off phase does not need the CPU. This is where most of it goes back.
        const uint32_t cycleEnd = sleepThenSpinUntil(cycleStart + mPeriodUs);

        // Measured from the edges actually issued, not from the requested
        // on-time, so busy-wait overshoot lands in the reported duty rather than
        // disappearing.
        stats.onUs += offAt - cycleStart;
        ++stats.periods;

        cycleStart = cycleEnd;
    }

    // Ends on a period boundary. Whatever is left of the budget is shorter than
    // one period, and starting a pulse that the caller would interrupt is worse
    // than handing the time back.
    stats.totalUs = cycleStart - startUs;

    mTotals.periods += stats.periods;
    mTotals.onUs    += stats.onUs;
    mTotals.totalUs += stats.totalUs;
    mTotals.edges   += stats.edges;
    return stats;
}

} // namespace Pwm
