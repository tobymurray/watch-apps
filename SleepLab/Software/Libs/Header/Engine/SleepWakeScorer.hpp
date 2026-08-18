/**
 ******************************************************************************
 * @file    SleepWakeScorer.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Cole-Kripke sleep/wake scoring, with Webster's rescoring rules.
 ******************************************************************************
 *
 * Pure C++17. No SDK header, no allocation, no I/O.
 *
 * ---------------------------------------------------------------------------
 * What this is, and what it is not
 *
 * This scores each 60-second epoch as sleep or wake from activity counts. That
 * is the whole of it. It does not produce stages: light, deep and REM rest on
 * heart-rate variability plus multi-channel PPG, and this device has neither.
 * See the HRV note at the bottom of this comment and clause A2 of
 * `Docs/FEASIBILITY-LEDGER.md`.
 *
 * The asymmetry in what actigraphy can do is the single most important thing
 * to understand about every number downstream of this file. Against
 * polysomnography, wrist actigraphy achieves high **sensitivity to sleep**
 * (~85-95 % of true sleep epochs scored correctly) and poor **specificity to
 * wake** (frequently 40-60 %). In plain terms: it reliably notices sleep, and
 * it systematically mistakes lying still for sleeping. The bias has a known
 * direction -- estimated sleep time is biased **high** -- and every report this
 * app produces has to own that rather than bury it.
 *
 * ---------------------------------------------------------------------------
 * The algorithm: Cole-Kripke (1992)
 *
 *   Cole RJ, Kripke DF, Gruen W, Mullaney DJ, Gillin JC.
 *   "Automatic sleep/wake identification from wrist activity."
 *   Sleep 1992;15(5):461-469.
 *
 * For each epoch, a weighted sum over a window running from four epochs back
 * to two epochs ahead:
 *
 *   D = P * ( W-4*A-4 + W-3*A-3 + W-2*A-2 + W-1*A-1 + W0*A0 + W+1*A+1 + W+2*A+2 )
 *
 * with D < 1 scored as sleep and D >= 1 as wake. The window is *centred with
 * look-ahead*, which is why scoring is a batch pass over a finished night
 * rather than something computed as each epoch closes: the score for 03:00
 * depends on 03:02.
 *
 * The weights and P below are transcribed from the literature. **Nobody here
 * has checked them against the primary source.** That is ledger row A8, tagged
 * UNVERIFIED, and it is a one-constant-block fix if the transcription is wrong.
 * They are gathered in one place for exactly that reason.
 *
 * ---------------------------------------------------------------------------
 * kCountScale, and the easiest way to fake validation
 *
 * Cole-Kripke's coefficients were fitted against counts from a specific device
 * -- an Actillume -- whose count units are not this device's. `EpochCounter`
 * produces integrated band-limited acceleration scaled by an arbitrary
 * constant. Feeding those numbers into coefficients fitted for different units
 * would produce something that *looks* validated, cites a real paper, and means
 * nothing.
 *
 * So the bridge between the two is one named constant, kCountScale, and it is
 * currently **a guess**. It carries a TODO naming the recording that would
 * justify it. Until that recording exists, every sleep/wake number this app
 * produces is synthetic-only: the arithmetic is pinned by fixtures with known
 * answers, and the correspondence to sleep is not established.
 *
 * That is stated in the README and in the ledger, not only here.
 *
 * ---------------------------------------------------------------------------
 * Rescoring: Webster (1982)
 *
 *   Webster JB, Kripke DF, Messin S, Mullaney DJ, Wyborney G.
 *   "An activity-based sleep monitor system for ambulatory use."
 *   Sleep 1982;5(4):389-399.
 *
 * The rescoring rules belong with Cole-Kripke rather than being an optional
 * extra: the raw scorer over-calls sleep immediately after wake, and the rules
 * are what the published sensitivity/specificity figures were measured with.
 * Applying the scorer without them is not the published algorithm.
 *
 * ---------------------------------------------------------------------------
 * The HRV channel
 *
 * `ScoringInput` carries `rmssdX10`, and this scorer **ignores it**. There is
 * no firmware producer: `HEART_BEAT` (0x40) emits no events at all, so there
 * are no RR intervals; `RR_INTERVAL` (SDK PR #220) has no producer either, and
 * the kernel parses then discards the RR values a chest strap already sends.
 *
 * It is plumbed anyway because UNA's answer had a "not today" shape -- a
 * higher-rate PPG mode and on-chip HRV are both being explored, and overnight
 * rest is precisely the case optical HRV is good for. When a producer appears,
 * this is a scorer change behind one already-existing input, not a rewrite of
 * the record format and not a reason to discard the nights already on disk.
 *
 * `SleepWakeScorer_test.cpp` asserts that supplying HRV today changes nothing.
 * That test is meant to fail the day someone wires it up, which is the day the
 * staging clause in the ledger has to be revisited in writing.
 *
 ******************************************************************************
 */

#ifndef ENGINE_SLEEPWAKESCORER_HPP
#define ENGINE_SLEEPWAKESCORER_HPP

#include <cstddef>
#include <cstdint>

#include "Engine/Epoch.hpp"

namespace Engine
{

/// Longest night this engine will score, in scoring epochs.
///
/// 16 hours. Generous rather than tight: a "night" that ran 16 hours is
/// already a data-quality problem, and the segmenter is what should catch it,
/// not an array bound silently truncating it.
constexpr size_t kMaxScoringEpochs = 16u * 60u;

/**
 * @brief One scoring epoch's worth of input.
 *
 * Compact on purpose. A whole night of these is ~15 KB, against ~107 KB for a
 * night of full `Epoch` records -- and the service has to hold a night in RAM
 * to score it, because Cole-Kripke needs look-ahead and Webster needs
 * whole-night passes.
 */
struct ScoringInput
{
    /// Summed counts of the recording epochs in this scoring epoch.
    ///
    /// Summed, not averaged: a count is an integral, so the integral over a
    /// minute is the sum of the integrals over its halves. Averaging would
    /// halve every count and quietly rescale the whole night.
    uint32_t count   = 0;

    /// Accelerometer samples the count was built from, across the whole
    /// scoring epoch.
    uint16_t samples = 0;

    /// Mean fraction of the epoch reported worn, 0..100.
    uint8_t  wornPct = 0;

    int16_t  hrMeanX10 = static_cast<int16_t>(kAbsent);

    /// Reserved. Always kAbsent today; ignored by this scorer. See the file
    /// comment.
    int32_t  rmssdX10  = kAbsent;
};

/// What one epoch was scored as.
enum class Verdict : uint8_t {
    Sleep      = 0,
    Wake       = 1,
    /// Not enough evidence to say. An epoch built from almost no samples, or
    /// one where the watch was not on a wrist. Deliberately a third value
    /// rather than being folded into Wake: "we do not know" and "the wearer was
    /// awake" are different claims, and a summary that counts the first as the
    /// second reports a worse night than happened.
    Unscorable = 2,
};

/**
 * @brief Cole-Kripke with Webster rescoring, over a whole night at once.
 *
 * Stateless apart from its constants -- `score()` is effectively a free
 * function and is written as a class only so the constants have somewhere to
 * live that a test can reach.
 */
class SleepWakeScorer
{
public:
    // -- Cole-Kripke ---------------------------------------------------------

    /// Window weights, in order A-4, A-3, A-2, A-1, A0, A+1, A+2.
    ///
    /// Checked against two independent reference implementations -- pyActigraphy
    /// and `actigraph.sleepr`, which both cite p. 466 of Cole et al. 1992 and both
    /// carry exactly these seven weights with P = 0.001, this window, and sleep
    /// below the threshold. Not checked against the paper itself; ledger row A8
    /// says what that is and is not worth.
    ///
    /// The shape is the part that matters and is unmistakable: the current epoch
    /// dominates at 230, the immediate neighbours contribute a third as much, and
    /// the tail four minutes back still carries weight because waking is a gradual
    /// thing.
    static constexpr float kWeights[7] = { 106.0f, 54.0f, 58.0f, 76.0f,
                                           230.0f, 74.0f, 67.0f };

    /// Epochs of history the window needs.
    static constexpr int kLookBack  = 4;
    /// Epochs of look-ahead the window needs. This is why scoring is a batch
    /// pass over a finished night.
    static constexpr int kLookAhead = 2;

    /// Overall scale factor P. Published alongside the weights above; same
    /// corroboration, same ledger row.
    static constexpr float kP = 0.001f;

    /// Threshold on D. Sleep below, wake at or above.
    ///
    /// The direction is asserted in the tests, not only stated here. Inverting it
    /// inverts every verdict in every night and the output would still look like a
    /// night -- and one widely-read reference does describe it the other way
    /// round, which is how a reader could talk themselves into flipping it.
    static constexpr float kThreshold = 1.0f;

    /// Bridge between this device's count units and the units Cole-Kripke's
    /// coefficients were fitted for.
    ///
    /// **This is a guess.** It is the one number standing between "cites a real
    /// paper" and "is validated", and it cannot be derived -- only measured.
    ///
    /// TODO: calibrate. The recording needed is ten nights recorded with a
    /// hand-kept diary of lights-out and final wake (Tier 0's probe already
    /// records everything required). Sweep this constant, and report the mean
    /// signed error on onset and final wake against the diary at each value;
    /// the value minimising it goes here, with the residual error stated in
    /// README and in the ledger's validation table. Until then every
    /// sleep/wake figure this app prints is synthetic-only.
    ///
    /// What the starting value implies, stated so it can be argued with: the
    /// window weights sum to 665, so with kP = 0.001 the sleep/wake boundary
    /// D = 1 falls at about **273 counts per scoring epoch** held across the
    /// whole window. Against EpochCounter's measured scale -- a continuous
    /// 0.3 g movement at 1 Hz integrating to ~4500 counts per 30 s epoch --
    /// that boundary is roughly 0.01 g of sustained 1 Hz movement.
    ///
    /// That is a plausible operating point for the difference between a
    /// settled sleeper and someone awake and shifting, and it is nothing more
    /// than plausible. It has never been compared against a person.
    ///
    /// One further thing the reference implementations do and this does not: their
    /// device-to-paper bridge *saturates*. `actigraph.sleepr` maps ActiGraph
    /// counts with `min(axis1 / 100, 300)`, so an epoch above the ceiling
    /// contributes as though it were at it. A linear scale with no ceiling lets
    /// one violent minute dominate the windows of the four epochs after it and the
    /// two before it, which manufactures wake around every turn-over.
    ///
    /// Not added, because the ceiling is only meaningful in the units this
    /// constant is supposed to bridge to, and this constant is a guess -- a ceiling
    /// derived from a guess is a second guess wearing the first one's authority. It
    /// goes in with the calibration, from the same ten diary nights, and the
    /// ledger's A9 row says so.
    static constexpr float kCountScale = 0.0055f;

    // -- Webster rescoring ---------------------------------------------------
    //
    // Five rules, applied in order, each over the whole night. They exist
    // because the raw scorer over-calls sleep immediately after wake, and the
    // published accuracy figures were measured *with* them -- running
    // Cole-Kripke without them is not running Cole-Kripke.

    /// Rules 1-3: after N minutes of continuous wake, the first M minutes of
    /// sleep are rescored as wake. Pairs are (wake minutes, sleep minutes).
    struct AfterWakeRule { int wakeMinutes; int rescoreMinutes; };
    static constexpr AfterWakeRule kAfterWakeRules[3] = {
        {  4, 1 },
        { 10, 3 },
        { 15, 4 },
    };

    /// Rules 4-5: a sleep block no longer than `sleepMinutes`, with at least
    /// `wakeMinutes` of wake on *both* sides, is rescored as wake.
    ///
    /// Rule 4's wake requirement is **15**, not 10. It was transcribed as 10, and
    /// 10 is a looser precondition -- so the rule fired on patterns the published
    /// one leaves alone, and short sleep bouts became wake that should have stayed
    /// sleep. The direction is *against* actigraphy's own bias rather than with
    /// it, which is why it would not have shown up as an implausible night: it
    /// made the app under-report sleep in a way that looked like conservatism.
    ///
    /// Checked against pyActigraphy's documentation of the rules it implements and
    /// against the actigraphy-algorithm survey literature, which agree. Not
    /// against Webster et al. 1982 itself -- see ledger row A8 for what that
    /// distinction is worth.
    struct ShortBoutRule { int sleepMinutes; int wakeMinutes; };
    static constexpr ShortBoutRule kShortBoutRules[2] = {
        {  6, 15 },
        { 10, 20 },
    };

    // -- Gating --------------------------------------------------------------

    /// Below this many accelerometer samples, a scoring epoch is Unscorable.
    ///
    /// An epoch built from a handful of samples is not evidence about anything:
    /// the count is an integral over whatever fraction of the minute happened
    /// to be delivered, and a near-empty epoch integrates to near-zero, which
    /// reads as perfect stillness. That is the failure this guards -- a
    /// delivery outage scoring as the soundest sleep of the night.
    ///
    /// 120 is a fifth of the ~600 a 60-second epoch should carry at a nominal
    /// 25 Hz once the thinning gate has taken its share. Deliberately loose:
    /// the delivered rate is not the requested rate and is not yet measured.
    /// TODO: set from the delivered-rate column of the first two probe nights
    /// (ledger row S3) -- it should be a fraction of the *measured* rate, not
    /// of the requested one.
    static constexpr uint16_t kMinSamplesPerEpoch = 120;

    /// Below this worn fraction, a scoring epoch is Unscorable.
    ///
    /// Not zero: TOUCH_DETECT's behaviour on a loose sleeping wrist is
    /// unmeasured (ledger row S7) and a sensor that dips for a few seconds an
    /// hour must not blank the night. Not high either: a watch genuinely off
    /// the wrist has to reach this.
    /// TODO: set from the flicker rate the probe measures.
    static constexpr uint8_t kMinWornPct = 50;

    /**
     * @brief Score a night.
     *
     * @param in     Scoring epochs in time order.
     * @param n      How many. Clamped to kMaxScoringEpochs.
     * @param out    Receives one Verdict per input epoch. Must hold @p n.
     * @return       Epochs actually scored (i.e. @p n after clamping).
     *
     * Unscorable epochs take no part in the window: they contribute their own
     * count as usual (a not-worn epoch still has a count, and pretending
     * otherwise would put a hole in every neighbour's window) but their own
     * verdict is forced to Unscorable and the rescoring passes step over them
     * without treating them as either sleep or wake.
     */
    static size_t score(const ScoringInput *in, size_t n, Verdict *out);

    /**
     * @brief The raw Cole-Kripke verdict for one epoch, without rescoring.
     *
     * Exposed for tests, which need to pin the scorer and the rescoring rules
     * separately -- a rescoring bug that happens to cancel a scoring bug passes
     * an end-to-end test.
     */
    static Verdict rawVerdict(const ScoringInput *in, size_t n, size_t at);

    /// The Cole-Kripke D statistic for one epoch. Exposed for the same reason.
    static float discriminant(const ScoringInput *in, size_t n, size_t at);

    /**
     * @brief Webster rules 1-3, in place. Exposed for tests.
     *
     * Public for the same reason as rawVerdict(): the interesting properties
     * of these passes -- that they do not cascade -- are about what they do to
     * a *given* pattern of verdicts, and reaching that pattern through the
     * scorer means building an activity fixture that produces it, which tests
     * the scorer's calibration at the same time and fails for the wrong
     * reasons when a constant moves.
     */
    static void rescoreAfterWake(Verdict *v, size_t n);

    /// Webster rules 4-5, in place. Exposed for the same reason.
    static void rescoreShortBouts(Verdict *v, size_t n);
};

} // namespace Engine

#endif // ENGINE_SLEEPWAKESCORER_HPP
