/**
 ******************************************************************************
 * @file    SecondsAccrual.hpp
 * @brief   How much active time a tick is responsible for.
 ******************************************************************************
 *
 * Anything the ride accrues per second -- time in a heart-rate zone, calories
 * -- has to be attributed to a span of time, not to the arrival of a tick.
 * Those are different counts: N ticks span N-1 seconds, because the first one
 * fires before any time has passed.
 *
 * MEASURED, on two real rides: 6 bucket-seconds against 5 seconds of active
 * time, and 186 against 185. Both exactly one over, every time, because every
 * tick banked a flat second. The per-record heart rate was right to count per
 * tick -- a record is an instant -- but zone seconds and calories are
 * durations and were not. Re-measure by totalling `time_in_hr_zone` out of a
 * .fit and comparing it against `total_timer_time`.
 *
 * Taking the difference against a running total also attributes a tick the
 * Service was too busy to serve on time, which a flat 1 silently dropped.
 *
 * The property worth having: the values handed out always sum to the active
 * time last passed in, so `time_in_hr_zone` totals `total_timer_time` by
 * construction rather than by luck. Tests/SecondsAccrual_test.cpp is that
 * claim, over tick sequences that stall, repeat and jump.
 *
 * Header-only over no SDK types, so it can be checked without a kernel -- the
 * same arrangement as HrHold.hpp and ZoneLadder.hpp.
 *
 ******************************************************************************
 */

#ifndef SECONDS_ACCRUAL_HPP
#define SECONDS_ACCRUAL_HPP

#include <ctime>

class SecondsAccrual {
public:
    /// Active seconds since the last call, and marks them attributed.
    ///
    /// @param activeNow  the ride's active time so far, monotonic.
    /// @return           0 when no whole second has passed, so a caller can
    ///                   skip the work rather than attribute nothing.
    std::time_t take(std::time_t activeNow)
    {
        // Never negative: a counter that went backwards is a bug elsewhere,
        // and handing back a negative span would spread it into every bucket
        // this feeds rather than leaving it where it happened.
        if (activeNow <= mAttributed) {
            return 0;
        }
        const std::time_t seconds = activeNow - mAttributed;
        mAttributed = activeNow;
        return seconds;
    }

    /// Active seconds handed out so far.
    std::time_t attributed() const { return mAttributed; }

    void reset() { mAttributed = 0; }

private:
    std::time_t mAttributed = 0;
};

#endif  // SECONDS_ACCRUAL_HPP
