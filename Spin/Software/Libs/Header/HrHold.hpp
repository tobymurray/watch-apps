/**
 ******************************************************************************
 * @file    HrHold.hpp
 * @brief   Bridges a momentary dip in the heart-rate arbiter's confidence.
 ******************************************************************************
 *
 * The kernel reports a 0..3 confidence with every heart-rate sample, and 0
 * means it does not stand behind the number. Blanking the screen the instant
 * that happens is wrong, and measurably so: across two real rides, 32 seconds
 * were reported untrusted and in 31 of them the sensor still had a reading
 * within a beat or two of its neighbours. Every one of those was a single
 * second, or two. What the wearer saw was the number disappearing and coming
 * straight back, several times a minute, on a signal whose consecutive samples
 * differ by 0.2 bpm on average.
 *
 * So the last trusted reading is held for a short while rather than dropped.
 * Past the window it goes, because a watch taken off the wrist, or a strap out
 * of range, must stop showing a heart rate that is no longer anyone's.
 *
 * WHAT THIS IS NOT
 *
 * It is not a filter, and there is deliberately nothing here that averages or
 * smooths. The same two rides put the mean change between consecutive readings
 * at 0.50 and 0.18 bpm: there is no jitter to remove, and a filter would only
 * add lag to a signal that does not need it.
 *
 * It is also display-only in effect. The FIT file's per-record `heart_rate`
 * keeps the strict gate, so a held second is still recorded as "no reading" and
 * `hr_source` still says 0. The distinction the file makes is between what was
 * measured and what was not; the distinction the screen makes is between what
 * we believe your heart rate is and having no idea. Those are different
 * questions and they deserve different answers.
 *
 * Header-only and free of SDK types on purpose: this is the one piece of the
 * heart-rate path with a rule in it, so it is the one piece worth testing
 * without a kernel. See Tests/HrHold_test.cpp.
 *
 ******************************************************************************
 */

#ifndef HR_HOLD_HPP
#define HR_HOLD_HPP

#include <cstdint>

class HrHold {
public:
    /// How long a reading survives without confirmation. Ten seconds against
    /// observed dropouts of one to two: long enough that a real artifact never
    /// shows, short enough that a watch taken off, or a strap walked out of
    /// range, blanks while the wearer is still looking at it.
    static constexpr uint8_t skHoldSeconds = 10;

    /// Advance one second.
    ///
    /// @param trusted  the arbiter stood behind this second's reading
    /// @param bpm      that reading; ignored unless @p trusted
    /// @return         what to display: the fresh reading, the held one, or 0
    ///                 for "no heart rate", which the screen draws as `---`.
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
