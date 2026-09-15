/**
 ******************************************************************************
 * @file    HrGate.hpp
 * @brief   Whether a heart-rate reading is one, and whether it is still current.
 ******************************************************************************
 *
 * Two decisions that every app reading the heart-rate sensor has to make, and
 * that four of them had copied and two had each solved half of. Header-only and
 * free of SDK types, so both are testable without a kernel; the caller owns the
 * clock and the sensor.
 *
 * Nothing here subscribes, parses, or chooses a sensor type. `HEART_RATE` and
 * `HEART_RATE_EX` are the caller's to pick between -- the SDK's
 * `Docs/ExternalSensors.md` says which is which, and `Docs/HR-READING-CONVERGENCE.md`
 * records what was measured about reading the wrong one.
 *
 * Zero means "no heart rate" and never a heart rate of zero, which is the rule
 * `EffortKit/src/hr.rs` owns for the whole repository. That is why
 * @ref HrGate::isPlausible returns a bool and @ref HrGate::Hold::View carries a
 * separate flag for each question rather than overloading a float.
 *
 ******************************************************************************
 */

#ifndef HR_GATE_HPP
#define HR_GATE_HPP

#include <cstdint>

namespace HrGate
{

/// Lowest bpm accepted as a reading rather than as an absent one.
///
/// INHERITED, not measured. This value and the three below arrived with the UNA
/// SDK's activity examples and are absent from its documentation and from every
/// commit message in this repository. What is known is what the recordings
/// contain: across all 6,108 rows of `Squash/Tests/pulled/*/imu_*_hr.csv`, four
/// carry exactly 0 bpm and the other 6,104 lie between 64 and 160. So this floor
/// has only ever had to separate zero from a real reading, and
/// @ref kMaxBpm has never been approached. Re-derive with `Tools/hr_analyse.py`.
constexpr float kMinBpm = 20.0f;

/// Highest bpm accepted as a reading. See @ref kMinBpm.
constexpr float kMaxBpm = 300.0f;

/// Lowest kernel confidence accepted. See @ref kMinBpm.
///
/// The kernel's scale is 0-3 and 0 is "untrusted". Across the same 6,108 rows
/// the value is 0 in 424, 1 in 3,738, 2 in 1,126 and 3 in 820, so this floor is
/// the half of the band that does any work.
constexpr float kMinTrust = 1.0f;

/// Highest kernel confidence accepted, guarding against a value off the scale.
/// See @ref kMinTrust: 3 has never been exceeded.
constexpr float kMaxTrust = 3.0f;

/**
 * @brief Is this a heart rate the kernel stands behind?
 *
 * @param bpm    The arbitrated reading.
 * @param trust  The kernel's own 0-3 confidence.
 *
 * How far to believe a reading it does stand behind is a calibration question
 * and belongs to `EffortKit::window`, not here.
 */
inline bool isPlausible(float bpm, float trust)
{
    return bpm > kMinBpm && bpm <= kMaxBpm && trust >= kMinTrust && trust <= kMaxTrust;
}

/**
 * @class Hold
 * @brief What the screen should show, and what a file may record.
 *
 * Two different failures, which two apps had each guarded against one of. A
 * frame can arrive carrying a value nobody believes, and frames can stop
 * arriving altogether. The first is momentary and blanking on it makes a
 * working instrument flicker; the second means the sensor is gone and showing
 * the last number is worse than showing none, because a plausible figure is
 * indistinguishable from a live one.
 *
 * So there are two windows and they are not interchangeable:
 *
 * - @ref kTrustHoldMs bridges seconds the kernel did not stand behind, while
 *   frames keep arriving.
 * - @ref kArrivalGateMs blanks everything when frames stop arriving at all. It
 *   is the outer bound: no hold survives the stream stopping.
 *
 * And two answers, because the screen and the file ask different things. The
 * screen may show a held reading; a file must record the second as having none.
 */
class Hold
{
public:
    /// How long the stream may go quiet before nothing is shown at all.
    ///
    /// MEASURED. Across the 6,101 inter-arrival gaps in every recording under
    /// `Squash/Tests/pulled`, 96.5% are ~1005 ms and the largest ever observed
    /// is 2117 ms; no gap of three seconds or more has occurred. Three is above
    /// every one of them while still declaring a dead sensor within three
    /// seconds. Falsified by a recording carrying a longer gap; re-derive with
    /// `Tools/hr_analyse.py`.
    ///
    /// This bounds delivery to the app, not the sensor's own cadence: no app in
    /// this repository records `getTimestamp()`, so the sensor's cadence has
    /// never been measured.
    static constexpr uint32_t kArrivalGateMs = 3000;

    /// How long a reading may go unconfirmed and still be shown.
    ///
    /// ASSERTED, not measured, and carried over from `Spin::HrHold` unchanged:
    /// long enough that a real artifact never shows, short enough that a watch
    /// taken off blanks while the wearer is still looking at it. The longest
    /// untrusted run ever recorded is 4 seconds, so this is 2.5x it. Settled by
    /// a recording made with the watch deliberately removed mid-session, which
    /// `Squash/Tests/pulled` does not contain.
    static constexpr uint32_t kTrustHoldMs = 10000;

    /// What is known about the heart rate at one instant.
    struct View {
        float bpm      = 0.0f;   ///< bpm to show; 0 = show no heart rate
        bool  held     = false;  ///< the bpm shown was carried over, not measured now
        bool  measured = false;  ///< a plausible reading arrived inside the arrival gate
    };

    /**
     * @brief Take one frame from the sensor.
     *
     * @param bpm    The arbitrated reading.
     * @param trust  The kernel's own 0-3 confidence.
     * @param nowMs  Monotonic uptime.
     *
     * Called on arrival, however often that is. An implausible frame still
     * counts as an arrival -- the stream is alive, this second just has no
     * reading in it.
     */
    void onReading(float bpm, float trust, uint32_t nowMs)
    {
        mArrivedMs   = nowMs;
        mHasArrived  = true;
        mLastWasGood = isPlausible(bpm, trust);
        if (mLastWasGood) {
            mGoodBpm = bpm;
            mGoodMs  = nowMs;
            mHasGood = true;
        }
    }

    /**
     * @brief What is known at @p nowMs.
     *
     * Called on the display or record tick, which is a different clock from
     * arrivals and may be faster or slower than them.
     */
    View view(uint32_t nowMs) const
    {
        View v;
        if (!mHasArrived || elapsed(nowMs, mArrivedMs) >= kArrivalGateMs) {
            return v;
        }
        if (!mHasGood || elapsed(nowMs, mGoodMs) >= kTrustHoldMs) {
            return v;
        }
        v.bpm      = mGoodBpm;
        v.held     = !mLastWasGood;
        v.measured = mLastWasGood;
        return v;
    }

    /// Forget everything, so a new session cannot inherit the last one's beat.
    void reset()
    {
        *this = Hold{};
    }

private:
    /// Unsigned subtraction, which is what makes this correct across the uptime
    /// counter's 49.7-day wrap.
    static uint32_t elapsed(uint32_t nowMs, uint32_t thenMs)
    {
        return nowMs - thenMs;
    }

    float    mGoodBpm     = 0.0f;
    uint32_t mGoodMs      = 0;
    uint32_t mArrivedMs   = 0;
    bool     mHasGood     = false;
    bool     mHasArrived  = false;
    bool     mLastWasGood = false;
};

} // namespace HrGate

#endif // HR_GATE_HPP
