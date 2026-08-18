/**
 ******************************************************************************
 * @file    NightSummary.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The standard actigraphy metrics. Definitions are in the header.
 ******************************************************************************
 */

#include "Engine/NightSummary.hpp"

namespace Engine
{
namespace {

/// First epoch of the first run of at least kOnsetRunMin consecutive Sleep
/// epochs, or -1 if the night never contains one.
///
/// Unscorable epochs break a run rather than extending it. Treating them as
/// sleep would let a delivery outage at 23:05 declare sleep onset; treating
/// them as wake would be a claim about the wearer we cannot make. Breaking the
/// run makes onset the first ten minutes we actually observed to be sleep,
/// which is what the definition says.
int32_t findOnset(const Verdict *v, size_t n)
{
    size_t run = 0;
    for (size_t i = 0; i < n; ++i) {
        if (v[i] == Verdict::Sleep) {
            if (++run >= static_cast<size_t>(NightAnalyser::kOnsetRunMin)) {
                return static_cast<int32_t>(i + 1 - run);
            }
        } else {
            run = 0;
        }
    }
    return kAbsent;
}

/// Index of the last Sleep epoch, or -1.
int32_t findFinalWake(const Verdict *v, size_t n)
{
    for (size_t i = n; i-- > 0;) {
        if (v[i] == Verdict::Sleep) {
            return static_cast<int32_t>(i);
        }
    }
    return kAbsent;
}

} // namespace

NightSummary NightAnalyser::analyse(const ScoringInput *in, const Verdict *v,
                                    size_t n, const WornGate::Result &gate,
                                    uint16_t flags)
{
    NightSummary s;
    s.worn         = gate.verdict;
    s.interruption = flags;
    s.epochs       = n;

    if (in == nullptr || v == nullptr || n == 0) {
        return s;
    }

    for (size_t i = 0; i < n; ++i) {
        if (v[i] == Verdict::Unscorable) {
            s.unscorable++;
        }
    }

    // Heart rate is reported whatever the gate says. It is a measurement of
    // the sensor, not a claim about sleep, and a night that failed the gate
    // still usefully shows whether the optical path was producing anything.
    {
        int64_t sum = 0;
        for (size_t i = 0; i < n; ++i) {
            const int16_t hr = in[i].hrMeanX10;
            if (hr == static_cast<int16_t>(kAbsent) || hr <= 0) {
                continue;
            }
            if (s.hrEpochs == 0 || hr < s.hrMinX10) {
                s.hrMinX10   = hr;
                s.hrMinEpoch = static_cast<int32_t>(i);
            }
            sum += hr;
            s.hrEpochs++;
        }
        if (s.hrEpochs > 0) {
            s.hrMeanX10 = static_cast<int32_t>(sum / static_cast<int64_t>(s.hrEpochs));
        }
    }

    // Delivery, not sleep -- so it is filled before the gate and stays filled for
    // a night whose sleep numbers are suppressed. A night that failed the gate is
    // exactly the night somebody needs to know the delivered rate of.
    {
        uint16_t counts[kMaxScoringEpochs];
        size_t   have = 0;
        for (size_t i = 0; i < n && i < kMaxScoringEpochs; ++i) {
            counts[have++] = in[i].samples;
        }
        if (have > 0) {
            // Insertion sort: a night is at most 960 entries, once.
            for (size_t i = 1; i < have; ++i) {
                const uint16_t v = counts[i];
                size_t j = i;
                while (j > 0 && counts[j - 1] > v) {
                    counts[j] = counts[j - 1];
                    --j;
                }
                counts[j] = v;
            }
            s.accSamplesMin    = static_cast<int32_t>(counts[0]);
            s.accSamplesMedian = static_cast<int32_t>(counts[(have - 1) / 2]);
            // Over the scoring epoch's own nominal length, which is what the
            // pairing guarantees; a *stalled* epoch is visible in the CSV's own
            // span_ms and is what the data-gap flag is for.
            s.accHzX10 = static_cast<int32_t>(
                s.accSamplesMedian * 10 /
                static_cast<int32_t>(kScoringEpochMs / 1000));
        }
    }

    // ---- The gate ----------------------------------------------------------
    //
    // This is where the honesty contract is actually enforced, and it is one
    // early return on purpose: there is no path below it that can fill a sleep
    // field for a night that did not pass. A gate implemented as a flag on the
    // output, checked by every consumer, is a gate that one consumer will
    // forget.
    if (!gate.mayReportSleep()) {
        return s;
    }

    // Time in bed is the whole session. A session that ran is a session the
    // wearer was in bed for -- the segmenter is what decides where it starts
    // and ends, and second-guessing it here would give two different answers
    // to the same question.
    s.timeInBedMin = static_cast<int32_t>(n);

    s.onsetEpoch     = findOnset(v, n);
    s.finalWakeEpoch = findFinalWake(v, n);

    if (s.onsetEpoch == kAbsent || s.finalWakeEpoch == kAbsent ||
        s.finalWakeEpoch < s.onsetEpoch) {
        // The session ran but never contained ten consecutive minutes of
        // sleep. Time in bed stands -- it was measured -- and everything that
        // depends on an onset stays absent. A night awake is a real outcome and
        // must not be reported as zero minutes of sleep, which reads as a
        // measurement rather than as an absence.
        return s;
    }

    const size_t onset = static_cast<size_t>(s.onsetEpoch);
    const size_t final_ = static_cast<size_t>(s.finalWakeEpoch);

    s.onsetLatencyMin = s.onsetEpoch;

    int32_t sleep = 0, waso = 0, still = 0, moving = 0;
    int32_t awakenings = 0;
    size_t  wakeRun = 0;

    for (size_t i = onset; i <= final_; ++i) {
        if (in[i].count > kMovementFloor) {
            moving++;
        } else {
            still++;
        }

        if (v[i] == Verdict::Sleep) {
            if (wakeRun > 0) {
                // A wake run just ended. Counted on its trailing edge so a run
                // still open at final wake is not counted -- by construction
                // there is none, since final wake is a Sleep epoch.
                if (s.awakeningsListed < kMaxListedAwakenings) {
                    s.awakeningMin[s.awakeningsListed++] =
                        static_cast<uint16_t>(wakeRun);
                }
                awakenings++;
                wakeRun = 0;
            }
            sleep++;
        } else if (v[i] == Verdict::Wake) {
            waso++;
            wakeRun++;
        } else {
            // Unscorable inside the night. Not sleep, not wake, and not an
            // awakening: it breaks a wake run without ending it as one,
            // because we do not know that the wearer woke.
            wakeRun = 0;
        }
    }

    s.totalSleepMin    = sleep;
    s.wasoMin          = waso;
    s.awakenings       = awakenings;
    s.stillInBedMin    = still;

    const int32_t span = static_cast<int32_t>(final_ - onset + 1);
    s.movementIndexPct = (span > 0) ? (moving * 100 / span) : kAbsent;

    // Against time in bed, the conventional denominator. See the field comment
    // -- the onset-to-final-wake variant is also called sleep efficiency and
    // gives a systematically flattering number.
    s.efficiencyPct = (s.timeInBedMin > 0)
                          ? (s.totalSleepMin * 100 / s.timeInBedMin)
                          : kAbsent;

    s.hasSleep = true;
    return s;
}

} // namespace Engine
