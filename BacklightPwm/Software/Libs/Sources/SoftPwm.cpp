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

/// Sleep through an off phase at least this long, and spin anything shorter.
///
/// Deliberately just under a millisecond so that even a 75 percent duty, whose
/// off phase is a single millisecond at a 4 ms period, gives the CPU back. That
/// rung is the one that would otherwise spin flat out for six seconds, and
/// starving the GUI for six seconds is how this app rebooted the watch twice.
constexpr uint32_t kSleepFloorUs = 900;

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

    const uint32_t onUs  = (mPeriodUs * mDuty) / 100u;
    const uint32_t offUs = mPeriodUs - onUs;

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

        if (mSleeper != nullptr && offUs >= kSleepFloorUs) {
            // Inside the off phase, where the light is already off and nothing
            // can tell the difference. One millisecond short of what fits, so the
            // spin closes the gap rather than the sleep overshooting it.
            const uint32_t sleepMs = offUs / 1000u;
            if (sleepMs > 1u) {
                mSleeper->sleepMs(sleepMs - 1u);
            } else {
                mSleeper->sleepMs(1u);
            }
        }
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
