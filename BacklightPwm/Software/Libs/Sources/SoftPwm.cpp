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


} // namespace

SoftPwm::SoftPwm(IPin& pin, const IClock& clock)
    : mPin(pin)
    , mClock(clock)
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

void SoftPwm::off()
{
    mPin.lightOff();
    ++mTotals.edges;
}

BurstStats SoftPwm::runBurst(uint32_t maxPeriods)
{
    BurstStats stats;

    const uint32_t startUs = mClock.nowUs();

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
        stats.held    = true;
        stats.onUs    = 0;
        stats.periods = 0;

        mTotals.edges += stats.edges;
        return stats;
    }

    const uint32_t onUs = (mPeriodUs * mDuty) / 100u;

    for (uint32_t i = 0; i < maxPeriods; ++i) {
        // Re-based every period. The sleep below stops the cycle counter, so any
        // attempt to carry a timeline across it would drift; measuring each
        // period from its own start cannot.
        const uint32_t cycleStart = mClock.nowUs();

        mPin.lightOn();
        ++stats.edges;
        const uint32_t offAt = spinUntil(cycleStart + onUs);

        mPin.lightOff();
        ++stats.edges;

        // Spun, not slept. See the header: delay() is a quarter of a period
        // coarse, and the cycle counter stops while it sleeps, so neither the
        // edges nor the remaining time survive it.
        spinUntil(cycleStart + mPeriodUs);

        // Measured from the edges actually issued, not from the requested
        // on-time, so busy-wait overshoot lands in the reported duty rather than
        // disappearing.
        stats.onUs += offAt - cycleStart;
        ++stats.periods;
    }

    stats.totalUs = mClock.nowUs() - startUs;

    mTotals.periods += stats.periods;
    mTotals.onUs    += stats.onUs;
    mTotals.totalUs += stats.totalUs;
    mTotals.edges   += stats.edges;
    return stats;
}

} // namespace Pwm
