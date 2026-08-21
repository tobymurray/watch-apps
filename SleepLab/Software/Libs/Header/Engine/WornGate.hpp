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
    ///
    /// The probe is the right instrument for *this* one: `touch_n`,
    /// `touch_worn_n` and `touch_edges` are exactly a worn fraction and a
    /// flicker rate. It is the wrong instrument for anything expressed in
    /// counts -- see kMicroMovementFloor below, whose TODO used to name it.
    static constexpr uint8_t kMinWornPct = 80;

    /// Counts per scoring epoch below which an epoch shows no micro-movement.
    ///
    /// A sleeping human is never perfectly still: respiration alone moves the
    /// wrist, and the band-limited count picks it up. A rigid object on
    /// furniture produces sensor noise and nothing else, and the 0.25-3 Hz band
    /// rejects most of that.
    ///
    /// **Measured 2026-08-20**, which is what this TODO used to ask for. The
    /// two recordings are `Recordings/2026-08-19-worn` (a full night, 1249
    /// epochs) and `Recordings/2026-08-20-table` (a stationary watch, empty
    /// room, 106 epochs), both at the same delivered 50.00 Hz so their counts
    /// are comparable (ledger row S14). Per 60 s scoring epoch:
    ///
    ///     table   p50  373   p95  390
    ///     worn    p5   414   p25  468   p50  695
    ///
    /// `night_report.py thresholds` puts the floor midway at 402; 400 is that,
    /// rounded, because the window between the two distributions is 24 counts
    /// wide and the third digit of a value inside it is noise. At 400 no table
    /// epoch passes and 1.8 % of worn epochs fail, which is what
    /// kMinPlausiblePct is for.
    ///
    /// **The previous value, 8, was a test that could not fail.** The sensor's
    /// own in-band noise floor is 357 counts at its very quietest, so every
    /// epoch ever recorded -- furniture included -- was above 8, and the
    /// micro-movement half of the plausibility check passed unconditionally.
    /// The gate was heart rate alone and nothing said so. The old comment
    /// guessed correctly that this floor was "the one most likely to be in the
    /// wrong place"; it was wrong by a factor of 45.
    ///
    /// Two limits on this number, stated so they are not forgotten. It rests on
    /// **one** table hour on **one** hard surface, and the worn night's minimum
    /// (370) is below the table's p95 (390), so the two distributions do overlap
    /// at the extreme tail. And whether the 357 floor is the accelerometer or
    /// the room is untested: the per-axis columns that would separate isotropic
    /// sensor noise from directed building vibration are schema 3, and both
    /// recordings are schema 2.
    static constexpr uint32_t kMicroMovementFloor = 400;

    /// Fraction of epochs, in percent, that must show *either* micro-movement
    /// or a heart rate for the night to be plausible.
    ///
    /// Both absent together is the table signature; this is how much of the
    /// night has to look alive.
    ///
    /// Left at 70 against the 2026-08-20 pair, and the pair says why it has
    /// room to spare: on the worn night **1249 of 1249 epochs carried a heart
    /// rate** and on the table hour **0 of 106 did**, so the pulse half of the
    /// check separated the two completely on its own. The count half at
    /// kMicroMovementFloor = 400 fails 1.8 % of worn epochs, which 70 % absorbs
    /// with three decades of margin. It is the `hr: off` night that would put
    /// this number under pressure, and there has not been one.
    static constexpr uint8_t kMinPlausiblePct = 70;

    /// Minimum kernel trust, x10, for a heart rate to count as evidence that a
    /// wrist was there.
    ///
    /// **This constant is the whole nightstand defence now**, and it is measured
    /// rather than guessed. A watch face down on a pillow for six hours produced
    /// a heart rate in 725 of 725 epochs -- median 63.4 bpm, range 44 to 99,
    /// every number plausible -- because soft fabric conforms around the optical
    /// window, blocks ambient light, and the AFE finds something periodic in the
    /// noise. Presence alone is therefore not evidence. Trust is:
    ///
    ///     pillow, 6 h    p5  8   p50  8   p95  9
    ///     worn night     p5 22   p50 30   p95 30
    ///     hard table     no heart rate at all
    ///
    /// At 20: 97.7 % of a worn night's epochs count, 0.0 % of the pillow's, and
    /// the two distributions do not touch. Placed at the worn night's own 5th
    /// percentile rather than midway, because the failure directions are not
    /// symmetric -- too low lets furniture through, too high suppresses a real
    /// night, and only the second one is silent to the wearer.
    ///
    /// TODO: one wrist, one strap tension, one night. A loose band on a cold
    /// wrist is exactly the case that would drop trust on a genuine night, and
    /// it has not been recorded. Until it has, a night suppressed by this
    /// constant should be checked against `hr_trust_x10` in its own epoch CSV
    /// before being believed.
    static constexpr int16_t kMinHrTrustX10 = 20;

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
