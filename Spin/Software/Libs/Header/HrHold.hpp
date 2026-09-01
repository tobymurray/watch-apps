/**
 ******************************************************************************
 * @file    HrHold.hpp
 * @brief   Bridges a momentary dip in the heart-rate arbiter's confidence.
 ******************************************************************************
 *
 * The kernel reports a 0..3 confidence with every sample, and 0 means it does
 * not stand behind the number. Blanking the screen the instant that happens is
 * what made a wearer report the heart rate "jumping around".
 *
 * MEASURED, over two real rides: 32 seconds were reported untrusted, and in 31
 * of them the sensor still had a reading within a beat or two of its
 * neighbours. Every one lasted a second or two. The mean change between
 * consecutive readings was 0.50 and 0.18 bpm.
 *
 * So this holds the last trusted reading rather than dropping it, and there is
 * deliberately NOTHING HERE THAT AVERAGES OR SMOOTHS: at 0.5 bpm between
 * samples there is no jitter to remove, and a filter would only add lag.
 * Re-measure from a ride log by counting untrusted seconds and differencing
 * consecutive readings.
 *
 * Display-only in effect; the FIT record keeps the strict gate. See
 * Service::prepareRecordData().
 *
 ******************************************************************************
 */

#ifndef HR_HOLD_HPP
#define HR_HOLD_HPP

#include <cstdint>

class HrHold {
public:
    /// Ten seconds against observed dropouts of one or two: long enough that a
    /// real artifact never shows, short enough that a watch taken off blanks
    /// while the wearer is still looking at it.
    static constexpr uint8_t skHoldSeconds = 10;

    /// Advance one second.
    /// @return bpm to display: fresh, held, or 0 for "no heart rate".
    float update(bool trusted, float bpm)
    {
        if (trusted) {
            mHeld = bpm;
            mHeldFor = 0;
            return mHeld;
        }
        if (mHeld <= 0.0f) {
            return 0.0f;   // nothing to hold yet
        }
        if (mHeldFor >= skHoldSeconds) {
            mHeld = 0.0f;  // expired; stays expired until a trusted reading
            return 0.0f;
        }
        ++mHeldFor;
        return mHeld;
    }

    /// True while the value last returned was held rather than freshly measured.
    bool isHolding() const { return mHeldFor > 0; }

    /// Seconds the current value has gone unconfirmed. 0 when fresh.
    uint8_t heldFor() const { return mHeldFor; }

    void reset()
    {
        mHeld = 0.0f;
        mHeldFor = 0;
    }

private:
    float   mHeld    = 0.0f;
    uint8_t mHeldFor = 0;
};

#endif // HR_HOLD_HPP
