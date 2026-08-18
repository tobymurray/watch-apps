/**
 ******************************************************************************
 * @file    NightSummary.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The standard actigraphy metrics, with their standard definitions.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O.
 *
 * Every metric here has a name that means something specific in the sleep
 * literature, and each is computed to that definition rather than to a
 * plausible-sounding variant. Where a definition has a choice in it -- and
 * several do -- the choice is stated next to the field rather than left to be
 * inferred from the code. That matters more than usual because these names are
 * *borrowed*: calling something "sleep efficiency" and computing it differently
 * from everyone else is worse than inventing a new name for it.
 *
 * ---------------------------------------------------------------------------
 * What these numbers are, and what they are not
 *
 * They are derived from movement and heart rate, scored by Cole-Kripke with
 * Webster rescoring. Actigraphy has high sensitivity to sleep and poor
 * specificity to wake -- it reliably notices sleep and systematically mistakes
 * lying still for sleeping.
 *
 * So `totalSleepMin` is **biased high** and its bias direction is known. The
 * report says so. `stillInBedMin` is the number that is actually supportable
 * from this hardware, and it is reported alongside rather than instead: it is
 * what was measured, where the other is what was estimated.
 *
 * No number here is validated against polysomnography. See
 * `Docs/FEASIBILITY-LEDGER.md`.
 *
 ******************************************************************************
 */

#ifndef ENGINE_NIGHTSUMMARY_HPP
#define ENGINE_NIGHTSUMMARY_HPP

#include <cstddef>
#include <cstdint>

#include "Engine/SleepWakeScorer.hpp"
#include "Engine/WornGate.hpp"

namespace Engine
{

/// Awakenings longer than this are listed individually in the report; the rest
/// are counted. Ten is a screen-space limit, not a scientific one -- the count
/// and total WASO are always complete.
constexpr size_t kMaxListedAwakenings = 10;

/**
 * @brief Why a night is not a clean night. A bitmask, because they combine.
 *
 * Reported as the *first line* of the morning summary when non-zero, never as
 * a badge in a corner. A night with a hole in it that looks like a whole night
 * is the second-worst thing this app could produce, after a night recorded
 * from a nightstand.
 */
namespace Interruption {
    /// The charger was connected at some point. Plugging in terminates every
    /// running app, so this night has a hole in it whose length is not even
    /// knowable from inside the app.
    constexpr uint16_t kCharging   = 1u << 0;
    /// The service restarted mid-night and the session was resumed. Recording
    /// stopped for the gap; the (uptime, wall-clock) pair at each flush is what
    /// made the stitch possible at all.
    constexpr uint16_t kResumed    = 1u << 1;
    /// The wall clock moved by more than uptime says it should have -- a
    /// timezone change, a host sync or DST. Times of day either side of it are
    /// not on the same scale.
    constexpr uint16_t kClockJump  = 1u << 2;
    /// Sensor delivery stopped for longer than one epoch somewhere in the
    /// night, without the app restarting.
    constexpr uint16_t kDataGap    = 1u << 3;
    /// The night hit the maximum length the engine will score and was cut.
    constexpr uint16_t kTruncated  = 1u << 4;
    /// A write to the epoch log or the resume state was refused. The record on
    /// disk is shorter than this summary describes, and by an amount the summary
    /// cannot state -- so the summary's own numbers are about minutes that were
    /// measured and not about minutes that were kept.
    constexpr uint16_t kWriteFailed = 1u << 5;
}

/**
 * @brief One night, summarised.
 *
 * Durations are in whole minutes because the scoring epoch is a minute and
 * reporting seconds would be false precision -- a sleep onset "at 23:14:30" is
 * a claim the epoch grid cannot support.
 *
 * Every sleep field is `kAbsent` when the worn gate did not pass. Not zero:
 * zero minutes of sleep is a claim about the night, and the whole point of the
 * gate is that no such claim is available.
 */
struct NightSummary
{
    // -- Provenance -----------------------------------------------------------

    WornVerdict worn         = WornVerdict::Uncertain;
    uint16_t    interruption = 0;   ///< Bitmask of Interruption::*.
    size_t      epochs       = 0;   ///< Scoring epochs the night contained.
    size_t      unscorable   = 0;   ///< ...that could not be scored at all.

    /// True when every sleep field below carries a real value. Exactly
    /// "the gate passed and there was a sleep onset".
    bool        hasSleep     = false;

    // -- Timing, as epoch indices into the night ------------------------------
    //
    // Indices rather than clock times, because the caller holds the epochs and
    // can map an index back to both clocks -- and because a clock time computed
    // in here would have to pick one of the two clocks, which is exactly the
    // decision this engine should not be making.

    /// First epoch of the first sustained sleep block. See kOnsetRunMin.
    int32_t onsetEpoch     = kAbsent;
    /// Last epoch scored as sleep.
    int32_t finalWakeEpoch = kAbsent;

    // -- The standard metrics -------------------------------------------------

    /// Time in bed: the whole session, lights-out marker to session close.
    /// The denominator of sleep efficiency.
    int32_t timeInBedMin = kAbsent;

    /// Sleep onset latency: session start to `onsetEpoch`.
    int32_t onsetLatencyMin = kAbsent;

    /// Estimated total sleep time: epochs scored Sleep between onset and final
    /// wake, inclusive.
    ///
    /// **Biased high.** Lying still awake scores as sleep. This is an estimate
    /// with a known direction of error, not a measurement, and it is labelled
    /// that way everywhere it is shown.
    int32_t totalSleepMin = kAbsent;

    /// Wake after sleep onset: epochs scored Wake between onset and final wake.
    int32_t wasoMin = kAbsent;

    /// Awakenings: runs of Wake between onset and final wake.
    int32_t awakenings = kAbsent;
    /// Lengths of the first kMaxListedAwakenings of them, in minutes.
    uint16_t awakeningMin[kMaxListedAwakenings] = {};
    size_t   awakeningsListed = 0;

    /// Sleep efficiency: totalSleepMin / timeInBedMin, as a percentage.
    ///
    /// Against time in *bed*, which is the conventional denominator, not
    /// against the onset-to-final-wake interval -- that variant is sometimes
    /// called sleep efficiency too and gives a systematically higher number.
    int32_t efficiencyPct = kAbsent;

    /// Movement index: percentage of epochs between onset and final wake whose
    /// count exceeds the movement floor. A restlessness measure that does not
    /// depend on the scorer, so it stays meaningful even where the scorer's
    /// calibration does not.
    int32_t movementIndexPct = kAbsent;

    // -- Time in bed still ----------------------------------------------------

    /// Minutes between onset and final wake with counts below the movement
    /// floor.
    ///
    /// The number this hardware genuinely supports. `totalSleepMin` is an
    /// estimate of sleep; this is a measurement of stillness, and stillness is
    /// what was actually observed. Reported alongside rather than instead --
    /// the point is to show both and be clear which is which.
    int32_t stillInBedMin = kAbsent;

    // -- Heart rate, relative to nothing --------------------------------------
    //
    // Absolute values only. Turning these into "your resting heart rate was
    // 4 bpm above normal" needs a personal baseline earned from this user's own
    // recorded nights, and that lives in BaselineStore -- deliberately not
    // here, so a summary can never accidentally carry a comparison it has not
    // earned.

    int32_t hrMinX10        = kAbsent; ///< Lowest epoch mean of the night.
    int32_t hrMeanX10       = kAbsent; ///< Mean across scored epochs.
    /// Epoch index of the nocturnal minimum. Its timing within the night is as
    /// informative as its value, and both are personal.
    int32_t hrMinEpoch      = kAbsent;
    size_t  hrEpochs        = 0;       ///< Epochs a heart rate contributed to.
};

/**
 * @brief Derive the summary from a scored night.
 */
class NightAnalyser
{
public:
    /// Consecutive sleep epochs required to call sleep onset.
    ///
    /// Ten minutes, the conventional actigraphy definition. A single quiet
    /// minute at 22:40 is not falling asleep, and taking the first sleep epoch
    /// as onset would make onset latency a measure of how still someone lies
    /// while reading.
    static constexpr int kOnsetRunMin = 10;

    /// Counts per scoring epoch above which an epoch counts as movement, for
    /// the movement index and for `stillInBedMin`.
    ///
    /// Distinct from WornGate::kMicroMovementFloor and deliberately higher:
    /// that one asks "is anything alive here at all", this one asks "did the
    /// sleeper move". Sharing a constant between the two would tie a
    /// restlessness measure to a hardware-plausibility threshold.
    /// TODO: set from a diary-validated night -- the value should put a settled
    /// sleeper's epochs below it and a turn-over above it. 40 is a guess
    /// against EpochCounter's scale.
    static constexpr uint32_t kMovementFloor = 40;

    /**
     * @brief Summarise.
     *
     * @param in      Scoring epochs, in time order.
     * @param v       One verdict per epoch, as produced by SleepWakeScorer.
     * @param n       How many.
     * @param gate    The worn gate's verdict for this night. When it did not
     *                pass, every sleep field is left absent -- this is where
     *                the suppression in the honesty contract actually happens.
     * @param flags   Interruption bitmask the caller has assembled from the
     *                recording side (charging, resume, clock jumps, gaps).
     */
    static NightSummary analyse(const ScoringInput *in, const Verdict *v, size_t n,
                                const WornGate::Result &gate, uint16_t flags);
};

} // namespace Engine

#endif // ENGINE_NIGHTSUMMARY_HPP
