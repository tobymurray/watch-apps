/**
 ******************************************************************************
 * @file    WornGate.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Was the watch on a wrist? Rationale is in the header.
 ******************************************************************************
 */

#include "Engine/WornGate.hpp"

namespace Engine
{

const char *WornGate::Result::reason() const
{
    switch (verdict) {
        case WornVerdict::Worn:
            return "worn";

        case WornVerdict::NotWorn:
            // One way to fail now, because there is one test: neither
            // micro-movement nor a trusted pulse for enough of the night.
            // `wornPct` no longer branches here -- it reads 100 on a wrist and
            // on a pillow alike, so a message chosen from it would be chosen at
            // random.
            return "no movement or pulse - watch was not on a wrist";

        case WornVerdict::Uncertain:
        default:
            if (epochs < kMinEpochs) {
                return "too short to judge";
            }
            if (hrEvidence == HrEvidence::Absent) {
                return "heart rate was off - cannot confirm it was worn";
            }
            return "cannot confirm it was worn";
    }
}

WornGate::Result WornGate::evaluate(const ScoringInput *in, size_t n,
                                    bool hrSampled, bool wornReported)
{
    Result r;
    r.hrEvidence   = hrSampled ? HrEvidence::Present : HrEvidence::Absent;
    r.wornReported = wornReported;

    if (in == nullptr || n < kMinEpochs) {
        r.epochs  = (in == nullptr) ? 0 : n;
        r.verdict = WornVerdict::Uncertain;
        return r;
    }

    r.epochs = n;

    for (size_t i = 0; i < n; ++i) {
        const ScoringInput &e = in[i];

        // The same worn floor the scorer uses, so the two cannot disagree
        // about what "worn" means for a single epoch while disagreeing about
        // the night.
        const bool worn   = e.wornPct >= SleepWakeScorer::kMinWornPct;
        const bool moving = e.count > kMicroMovementFloor;
        // Present *and* trusted. A pillow produces a heart rate in every epoch;
        // what it does not produce is a trusted one. See kMinHrTrustX10.
        const bool hasHr  = e.hrMeanX10 != static_cast<int16_t>(kAbsent) &&
                            e.hrMeanX10 > 0 &&
                            e.hrTrustX10 != static_cast<int16_t>(kAbsent) &&
                            e.hrTrustX10 >= kMinHrTrustX10;

        if (worn)   { r.wornEpochs++; }
        if (moving) { r.movingEpochs++; }
        if (hasHr)  { r.hrEpochs++; }

        // The plausibility test. With HR unavailable this degrades to
        // micro-movement alone, which is why a verdict reached that way can
        // never be Worn -- see below.
        if (moving || (hrSampled && hasHr)) {
            r.plausibleEpochs++;
        }
    }

    r.wornPct      = static_cast<uint8_t>(r.wornEpochs * 100u / n);
    r.plausiblePct = static_cast<uint8_t>(r.plausibleEpochs * 100u / n);

    // **TOUCH_DETECT no longer gates anything.** It is still counted, still
    // reported and still in every epoch row -- and it is not evidence.
    //
    // Measured across three recordings on 0.3.0: a genuinely worn night
    // delivered `touch_n` = 0 for all 910 of its epochs, and a watch face down on
    // a pillow delivered 10. Both read `worn_pct` 100, because a sample-less
    // epoch carries the last known state forward and the last known state is
    // "worn" in both cases. So the channel says the same thing about a wrist and
    // about furniture, which is the definition of carrying no information.
    //
    // Worse than useless, in fact: while `!wornReported` forced Uncertain, a
    // night whose touch sensor happened to say nothing at all was suppressed
    // outright -- and that is what a real worn night looks like on this
    // hardware. The rule written to stop the app telling somebody to put on a
    // watch they were already wearing would have thrown their night away
    // instead.
    //
    // What replaces it is the plausibility check, which is now the whole of the
    // gate: micro-movement above a floor set from a stationary reference, or a
    // heart rate the kernel actually trusts.
    if (r.plausiblePct < kMinPlausiblePct) {
        // Neither movement nor a trusted pulse. That is the nightstand case and
        // it is the one this whole class exists for -- and it is the case a
        // pillow passed before trust was required, with a plausible-looking
        // heart rate in every epoch of six hours.
        r.verdict = WornVerdict::NotWorn;
        return r;
    }

    if (!hrSampled) {
        // Half the evidence was never gathered. The night may well be fine --
        // but "probably worn" is not a basis for printing a sleep efficiency,
        // and Uncertain suppresses the numbers exactly as NotWorn does. The
        // difference is only in what the screen says about why.
        r.verdict = WornVerdict::Uncertain;
        return r;
    }

    r.verdict = WornVerdict::Worn;
    return r;
}

} // namespace Engine
