/**
 ******************************************************************************
 * @file    SleepWakeScorer.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Cole-Kripke scoring with Webster rescoring. Spec in the header.
 ******************************************************************************
 */

#include "Engine/SleepWakeScorer.hpp"

namespace Engine
{

// Out-of-line definitions for the constexpr arrays, so they can be ODR-used
// (taking a reference to one in a test, for instance) under C++14 linkage
// rules. Harmless under C++17's inline-variable rule.
constexpr float SleepWakeScorer::kWeights[7];
constexpr SleepWakeScorer::AfterWakeRule SleepWakeScorer::kAfterWakeRules[3];
constexpr SleepWakeScorer::ShortBoutRule SleepWakeScorer::kShortBoutRules[2];

namespace {

/// Whether this epoch has enough behind it to be scored at all.
bool scorable(const ScoringInput &e)
{
    return e.samples >= SleepWakeScorer::kMinSamplesPerEpoch &&
           e.wornPct >= SleepWakeScorer::kMinWornPct;
}

} // namespace

float SleepWakeScorer::discriminant(const ScoringInput *in, size_t n, size_t at)
{
    float sum = 0.0f;

    for (int k = -kLookBack; k <= kLookAhead; ++k) {
        const ptrdiff_t idx = static_cast<ptrdiff_t>(at) + k;

        // Off either end of the night, the window has nothing to read. Treated
        // as zero activity rather than as the nearest real epoch: clamping
        // would repeat the first minute four times and let one restless
        // movement at lights-out dominate the whole opening window.
        //
        // The direction of the resulting error is worth naming: zero activity
        // pushes D down, which biases the first four and last two minutes of
        // every night *towards sleep*. In the same direction as actigraphy's
        // known bias, and for two minutes at each end of eight hours, which is
        // why it is accepted rather than corrected with a scheme nobody has
        // validated.
        const float a = (idx < 0 || static_cast<size_t>(idx) >= n)
                            ? 0.0f
                            : static_cast<float>(in[idx].count) * kCountScale;

        sum += kWeights[k + kLookBack] * a;
    }

    // in[at].rmssdX10 is deliberately not read. There is no producer for it,
    // and a term weighted by a channel that is always absent would be dead code
    // pretending to be a feature. See the file comment in the header.

    return kP * sum;
}

Verdict SleepWakeScorer::rawVerdict(const ScoringInput *in, size_t n, size_t at)
{
    if (at >= n) {
        return Verdict::Unscorable;
    }
    if (!scorable(in[at])) {
        return Verdict::Unscorable;
    }
    return (discriminant(in, n, at) < kThreshold) ? Verdict::Sleep : Verdict::Wake;
}

void SleepWakeScorer::rescoreAfterWake(Verdict *v, size_t n)
{
    // Webster rules 1-3: a run of wake is followed by a stretch of sleep that
    // the raw scorer called too early. Walk forward, tracking the length of the
    // wake run just ended, and blank the first few minutes of the sleep that
    // follows it.
    //
    // The rules are applied against the *original* wake runs, so the wake this
    // pass writes cannot itself lengthen a run and cascade into the next sleep
    // block. Without that, a night with frequent brief awakenings unravels into
    // continuous wake.

    size_t i = 0;
    while (i < n) {
        // Measure a run of wake. Unscorable epochs end a run without extending
        // it: we do not know what happened in them, and counting them as wake
        // would manufacture the very precondition these rules trigger on.
        if (v[i] != Verdict::Wake) {
            ++i;
            continue;
        }

        size_t wakeStart = i;
        while (i < n && v[i] == Verdict::Wake) {
            ++i;
        }
        const int wakeRun = static_cast<int>(i - wakeStart);

        // How many minutes of the sleep that follows to rescore. Longest
        // matching rule wins -- the rules are cumulative thresholds, not
        // alternatives.
        int rescore = 0;
        for (const auto &rule : kAfterWakeRules) {
            if (wakeRun >= rule.wakeMinutes && rule.rescoreMinutes > rescore) {
                rescore = rule.rescoreMinutes;
            }
        }

        int written = 0;
        for (int k = 0; k < rescore && (i + static_cast<size_t>(k)) < n; ++k) {
            const size_t at = i + static_cast<size_t>(k);
            if (v[at] != Verdict::Sleep) {
                break; // The sleep block ended before the rule ran out.
            }
            v[at] = Verdict::Wake;
            ++written;
        }

        // Resume scanning after what was actually rewritten, so those epochs are
        // not re-read as a fresh wake run -- and *only* after them.
        //
        // Advancing by the whole rule length instead stepped over epochs the pass
        // had not touched. When the sleep block ran out before the rule did, the
        // remainder of the skip landed inside the wake run that followed, and that
        // run was then measured short: 15 wake, 1 sleep, 11 wake read the
        // 11-minute run as 8, which drops it from rule 2 (three minutes rescored)
        // to rule 1 (one), and the two minutes the published algorithm calls wake
        // are called sleep. Same direction as actigraphy's own bias, which is why
        // it does not show up in an output.
        i += static_cast<size_t>(written);
    }
}

void SleepWakeScorer::rescoreShortBouts(Verdict *v, size_t n)
{
    // Webster rules 4-5: a short island of sleep in a sea of wake is not sleep.
    //
    // Decided against the state *before* this pass, so two adjacent short bouts
    // cannot each be justified by wake the other one was rewritten into.
    // Without the copy, rule 4 turns a 6-minute bout into wake and rule 5 then
    // sees enough surrounding wake to eat the next bout too.

    // A whole night of verdicts is one byte each, so the snapshot is 960 bytes
    // on the stack -- cheaper than any scheme for avoiding it.
    Verdict before[kMaxScoringEpochs];
    for (size_t i = 0; i < n; ++i) {
        before[i] = v[i];
    }

    size_t i = 0;
    while (i < n) {
        if (before[i] != Verdict::Sleep) {
            ++i;
            continue;
        }

        const size_t sleepStart = i;
        while (i < n && before[i] == Verdict::Sleep) {
            ++i;
        }
        const size_t sleepEnd = i; // one past
        const int    bout     = static_cast<int>(sleepEnd - sleepStart);

        // Wake immediately before and after, in the pre-pass state.
        int wakeBefore = 0;
        for (ptrdiff_t k = static_cast<ptrdiff_t>(sleepStart) - 1;
             k >= 0 && before[k] == Verdict::Wake; --k) {
            ++wakeBefore;
        }
        int wakeAfter = 0;
        for (size_t k = sleepEnd; k < n && before[k] == Verdict::Wake; ++k) {
            ++wakeAfter;
        }

        for (const auto &rule : kShortBoutRules) {
            if (bout <= rule.sleepMinutes &&
                wakeBefore >= rule.wakeMinutes &&
                wakeAfter  >= rule.wakeMinutes) {
                for (size_t k = sleepStart; k < sleepEnd; ++k) {
                    v[k] = Verdict::Wake;
                }
                break;
            }
        }
    }
}

size_t SleepWakeScorer::score(const ScoringInput *in, size_t n, Verdict *out)
{
    if (in == nullptr || out == nullptr) {
        return 0;
    }
    if (n > kMaxScoringEpochs) {
        n = kMaxScoringEpochs;
    }

    for (size_t i = 0; i < n; ++i) {
        out[i] = rawVerdict(in, n, i);
    }

    rescoreAfterWake(out, n);
    rescoreShortBouts(out, n);

    return n;
}

} // namespace Engine
