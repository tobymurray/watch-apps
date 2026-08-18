/**
 ******************************************************************************
 * @file    WornGate.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Was the watch on a wrist? Everything else depends on the answer.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O.
 *
 * ---------------------------------------------------------------------------
 * The failure mode this exists for
 *
 * **A watch on a nightstand is perfectly still and reports a flawless night.**
 * Zero movement, no awakenings, 100 % sleep efficiency, eight hours of the
 * soundest sleep the app has ever recorded. Every number is beautiful and
 * every number is about a piece of furniture.
 *
 * That is the single failure that would discredit everything else this app
 * says, because it is silent, it is common -- people take watches off -- and
 * the output looks *better* than a real night rather than worse. So no sleep
 * number is reported at all for a night that fails this gate. Not annotated,
 * not caveated: **suppressed**. A night that failed is reported as *not worn*.
 *
 * ---------------------------------------------------------------------------
 * Why TOUCH_DETECT alone is not enough
 *
 * The obvious answer is to trust `TOUCH_DETECT` (0x140), and it is subscribed
 * and is the first thing checked. But it is a capacitive proximity reading, its
 * behaviour on a loosely-strapped sleeping wrist is unmeasured (ledger row S7),
 * and a sensor that can report "worn" for a watch resting face-down on a duvet
 * is a sensor that can fail in exactly the direction that matters.
 *
 * So the gate is worn-detection **plus a plausibility check**, and the
 * plausibility check tests for the two things a living wrist produces that a
 * table cannot:
 *
 *   - **micro-movement.** A sleeping human is never perfectly still. Respiration
 *     alone moves the wrist. Counts genuinely at the floor, epoch after epoch,
 *     are a rigid object.
 *   - **a heart rate.** Optical HR against a table surface returns nothing or
 *     nonsense, and the kernel's own trust value collapses.
 *
 * Either one alone is defeatable. A watch wedged in bedding gets some
 * movement; a watch on a warm surface occasionally yields a spurious HR. Both
 * absent together, across a large fraction of a night, is a table.
 *
 * ---------------------------------------------------------------------------
 * When heart rate was never sampled
 *
 * The HR duty cycle is configurable and can be off entirely -- it is the
 * biggest overnight power cost and Tier 0 exists partly to find out how big.
 * With HR off, half the plausibility check is unavailable, and the honest
 * response is to say so rather than to quietly pass on the remaining half.
 * `Result::hrEvidence` records which case a verdict was reached in, and a
 * verdict reached without HR can only ever be `Uncertain`, never `Worn`.
 *
 ******************************************************************************
 */

#ifndef ENGINE_WORNGATE_HPP
#define ENGINE_WORNGATE_HPP

#include <cstddef>
#include <cstdint>

#include "Engine/SleepWakeScorer.hpp"

namespace Engine
{

/**
 * @brief Verdict on whether a night was recorded from a wrist.
 */
enum class WornVerdict : uint8_t {
    /// On a wrist. Sleep numbers may be reported.
    Worn      = 0,
    /// Not on a wrist for enough of the night to report anything.
    /// **Sleep numbers are suppressed.**
    NotWorn   = 1,
    /// Cannot tell. Also suppresses sleep numbers -- an unfalsifiable number is
    /// worse than a missing one, because it will be believed.
    Uncertain = 2,
};

/**
 * @brief Was the watch on a wrist, and what says so.
 */
class WornGate
{
public:
    /// Fraction of scored epochs that must report worn, in percent.
    ///
    /// Not 100: TOUCH_DETECT's flicker behaviour on a loose sleeping wrist is
    /// unmeasured, and a sensor that dips for a few seconds an hour must not
    /// blank a real night. Not low either: a watch taken off at 02:00 and left
    /// off has to fail.
    /// TODO: set from the worn fraction and flicker rate the Tier 0 probe
    /// measures across a worn night and a table night (ledger row S7). 80 is a
    /// placeholder chosen to tolerate roughly an hour and a half of dropout in
    /// an eight-hour night.
    static constexpr uint8_t kMinWornPct = 80;

    /// Counts per scoring epoch below which an epoch shows no micro-movement.
    ///
    /// A sleeping human is never perfectly still: respiration alone moves the
    /// wrist, and the band-limited count picks it up. A rigid object on
    /// furniture produces sensor noise and nothing else, and the 0.25-3 Hz band
    /// rejects most of that.
    /// TODO: this is the constant the *table night* exists to set. Record one
    /// night worn and one night on a nightstand with the Tier 0 probe, and put
    /// the floor between the two distributions -- it should be comfortably
    /// above the table night's 95th percentile and below the worn night's 5th.
    /// 8 is a guess against EpochCounter's scale and nothing more.
    static constexpr uint32_t kMicroMovementFloor = 8;

    /// Fraction of epochs, in percent, that must show *either* micro-movement
    /// or a heart rate for the night to be plausible.
    ///
    /// Both absent together is the table signature; this is how much of the
    /// night has to look alive.
    /// TODO: same recording as kMicroMovementFloor.
    static constexpr uint8_t kMinPlausiblePct = 70;

    /// Shortest night this gate will pass, in scoring epochs.
    ///
    /// Twenty minutes. Below this there is not enough of a night to gate: a
    /// handful of epochs can be unanimous by chance, and a "night" that short
    /// has nothing worth reporting anyway.
    static constexpr size_t kMinEpochs = 20;

    /// Which halves of the plausibility check were actually available.
    enum class HrEvidence : uint8_t {
        Present = 0, ///< HR was sampled; both halves were used.
        Absent  = 1, ///< HR was never sampled. Only micro-movement was used.
    };

    struct Result
    {
        WornVerdict verdict    = WornVerdict::Uncertain;
        HrEvidence  hrEvidence = HrEvidence::Absent;
        /// False when TOUCH_DETECT said nothing at all. See evaluate().
        bool        wornReported = true;

        size_t   epochs           = 0; ///< Epochs examined.
        size_t   wornEpochs       = 0; ///< ...reporting worn at or above the scorer's floor.
        size_t   movingEpochs     = 0; ///< ...with counts above kMicroMovementFloor.
        size_t   hrEpochs         = 0; ///< ...with a valid heart rate.
        size_t   plausibleEpochs  = 0; ///< ...with either of the two above.

        uint8_t  wornPct       = 0;    ///< wornEpochs as a percentage.
        uint8_t  plausiblePct  = 0;    ///< plausibleEpochs as a percentage.

        /// True when sleep numbers may be reported. Exactly
        /// `verdict == WornVerdict::Worn`, named so callers read the intent
        /// rather than re-deriving it and getting Uncertain wrong.
        bool mayReportSleep() const { return verdict == WornVerdict::Worn; }

        /// One short line for the screen and the summary JSON, explaining the
        /// verdict rather than only stating it. Never null.
        const char *reason() const;
    };

    /**
     * @brief Judge a night.
     *
     * @param in          Scoring epochs in time order.
     * @param n           How many.
     * @param hrSampled   Whether the heart-rate sensor was subscribed at all
     *                    during the night. Distinguishes "HR delivered nothing"
     *                    -- which is evidence of a table -- from "HR was never
     *                    asked for", which is evidence of nothing.
     * @param wornReported Whether TOUCH_DETECT delivered a single sample all
     *                    night. It is an event sensor -- it publishes on a
     *                    change of state, not on a clock -- so silence normally
     *                    means "unchanged" and the recorder carries the last
     *                    state forward. Total silence across a whole night is
     *                    different: nothing was ever heard, so there is no
     *                    state to carry, and the honest verdict is Uncertain
     *                    rather than NotWorn. Telling somebody their watch was
     *                    not worn would send them to put on a watch they are
     *                    already wearing.
     */
    static Result evaluate(const ScoringInput *in, size_t n, bool hrSampled,
                           bool wornReported = true);
};

} // namespace Engine

#endif // ENGINE_WORNGATE_HPP
