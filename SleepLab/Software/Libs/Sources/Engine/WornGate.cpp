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
            // Two quite different ways to fail, and the difference is what the
            // wearer would do about it: one means put it back on, the other
            // means tighten the strap.
            if (wornPct < kMinWornPct) {
                return "not worn for enough of the night";
            }
            return "no movement or pulse - watch was not on a wrist";

        case WornVerdict::Uncertain:
        default:
            if (epochs < kMinEpochs) {
                return "too short to judge";
            }
            if (!wornReported) {
                return "worn sensor said nothing all night";
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
        const bool hasHr  = e.hrMeanX10 != static_cast<int16_t>(kAbsent) &&
                            e.hrMeanX10 > 0;

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

    // Before anything else: a worn sensor that never spoke leaves no evidence
    // either way, and every epoch's worn fraction is a default rather than a
    // measurement. Reading that as "taken off" would send somebody to put on a
    // watch they are already wearing.
    if (!wornReported) {
        r.verdict = WornVerdict::Uncertain;
        return r;
    }

    // Order matters. A watch that was clearly taken off is NotWorn whatever the
    // plausibility numbers say, because "you took it off" is actionable and
    // "we cannot tell" is not.
    if (r.wornPct < kMinWornPct) {
        r.verdict = WornVerdict::NotWorn;
        return r;
    }

    if (r.plausiblePct < kMinPlausiblePct) {
        // TOUCH_DETECT said worn and the wrist produced neither movement nor a
        // pulse. That is the nightstand case, and it is the one this whole
        // class exists for: it is also the case where TOUCH_DETECT was wrong,
        // so the plausibility check overrules it rather than deferring to it.
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
