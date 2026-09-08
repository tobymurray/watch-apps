#ifndef HAPTICLAB_ANALYSIS_CONFUSION_HPP
#define HAPTICLAB_ANALYSIS_CONFUSION_HPP

#include <cmath>
#include <cstdint>

#include "Patterns.hpp"

/// The statistics the deliverable is made of. Nothing here is linked into
/// either watch ELF: it is `double` arithmetic over a whole run, which is a
/// host's job, and nothing on the watch includes it. `Tests/` is where it is
/// held, and `Tools/analyse.sh` is what runs it over a real run.
namespace Haptic::Analysis {

/// A rate with the interval that says how little is known about it.
struct Interval {
    double point;
    double low;
    double high;
    bool   reportable; ///< False when too few trials stand behind it.
};

/// z for a two-sided 95 % interval, which is the confidence every rate in the
/// report is stated at.
constexpr double kZ95 = 1.959964;

/// Below this many trials a rate is not reported at all, only the shortfall.
/// Chosen so a single confusion moves the point estimate by less than a tenth:
/// at ten trials one error is 10 %, which is the resolution the threshold is
/// compared against.
constexpr uint16_t kMinTrialsPerPair = 12;

/// Wilson score interval, which unlike the normal approximation stays inside
/// [0, 1] and does not collapse to zero width when nothing was confused.
inline Interval wilson(uint16_t successes, uint16_t trials, double z = kZ95)
{
    Interval out {};
    out.reportable = trials >= kMinTrialsPerPair;
    if (trials == 0) {
        return out;
    }

    const double n     = static_cast<double>(trials);
    const double p     = static_cast<double>(successes) / n;
    const double z2    = z * z;
    const double denom = 1.0 + z2 / n;
    const double centre = (p + z2 / (2.0 * n)) / denom;
    const double half   = (z / denom) * std::sqrt(p * (1.0 - p) / n + z2 / (4.0 * n * n));

    out.point = p;
    out.low   = centre - half > 0.0 ? centre - half : 0.0;
    out.high  = centre + half < 1.0 ? centre + half : 1.0;
    return out;
}

/// One unordered pair of patterns, counted only over the trials where both were
/// on the screen together: a pair that was never offered against each other has
/// no confusion rate, and averaging it in with the ones that were would invent
/// one.
struct PairCounts {
    uint16_t trials     = 0;
    uint16_t confusions = 0;
};

/// Every count a run's `trials.csv` yields, and nothing derived.
struct Tally {
    uint8_t  patternCount = 0;
    /// Rows are the presented pattern, columns the pattern responded with.
    uint16_t responses[kMaxPatterns][kMaxPatterns] = {};
    /// Presentations answered with the miss key, which is not a wrong answer.
    uint16_t misses[kMaxPatterns] = {};
    /// Presentations, miss responses included.
    uint16_t presentations[kMaxPatterns] = {};
    PairCounts pairs[kMaxPatterns][kMaxPatterns] = {};

    void addResponse(uint8_t presented, uint8_t responded, const uint8_t offered[3])
    {
        responses[presented][responded]++;
        presentations[presented]++;
        countPair(presented, responded, offered, responded != presented);
    }

    /// A presentation nobody felt is not evidence about any pair, so it counts
    /// towards the miss rate and towards nothing else. Counting it as a pair
    /// trial with no confusion would make every pattern in the triple look
    /// more distinguishable for having gone unnoticed.
    void addMiss(uint8_t presented, const uint8_t offered[3])
    {
        (void)offered;
        misses[presented]++;
        presentations[presented]++;
    }

    PairCounts pairFor(uint8_t a, uint8_t b) const
    {
        return a < b ? pairs[a][b] : pairs[b][a];
    }

private:
    void countPair(uint8_t presented, uint8_t responded, const uint8_t offered[3], bool wrong)
    {
        for (uint8_t i = 0; i < 3; i++) {
            const uint8_t rival = offered[i];
            if (rival == presented) {
                continue;
            }
            PairCounts &pair = presented < rival ? pairs[presented][rival] : pairs[rival][presented];
            pair.trials++;
            if (wrong && responded == rival) {
                pair.confusions++;
            }
        }
    }
};

/// The rate at which `a` and `b` are taken for each other, over the trials in
/// which they were offered together.
inline Interval pairConfusion(const Tally &tally, uint8_t a, uint8_t b)
{
    const PairCounts pair = tally.pairFor(a, b);
    return wilson(pair.confusions, pair.trials);
}

inline Interval accuracy(const Tally &tally, uint8_t pattern)
{
    return wilson(tally.responses[pattern][pattern], tally.presentations[pattern]);
}

/// Entropy of the stimulus set as it was actually presented, which is the
/// ceiling on what any amount of information transfer could reach.
inline double stimulusEntropyBits(const Tally &tally)
{
    uint32_t total = 0;
    for (uint8_t i = 0; i < tally.patternCount; i++) {
        total += tally.presentations[i];
    }
    if (total == 0) {
        return 0.0;
    }

    double h = 0.0;
    for (uint8_t i = 0; i < tally.patternCount; i++) {
        if (tally.presentations[i] == 0) {
            continue;
        }
        const double p = static_cast<double>(tally.presentations[i]) / total;
        h -= p * std::log2(p);
    }
    return h;
}

/// Information transfer in bits: the mutual information of presented against
/// responded, which is what "how many can be told apart" means formally.
/// Misses are excluded, because a presentation that was not felt says nothing
/// about whether the two patterns are distinguishable.
inline double informationTransferBits(const Tally &tally)
{
    uint32_t total = 0;
    uint32_t rowTotal[kMaxPatterns] = {};
    uint32_t colTotal[kMaxPatterns] = {};

    for (uint8_t i = 0; i < tally.patternCount; i++) {
        for (uint8_t j = 0; j < tally.patternCount; j++) {
            const uint16_t n = tally.responses[i][j];
            total += n;
            rowTotal[i] += n;
            colTotal[j] += n;
        }
    }
    if (total == 0) {
        return 0.0;
    }

    double bits = 0.0;
    for (uint8_t i = 0; i < tally.patternCount; i++) {
        for (uint8_t j = 0; j < tally.patternCount; j++) {
            const uint16_t n = tally.responses[i][j];
            if (n == 0) {
                continue;
            }
            const double joint = static_cast<double>(n) / total;
            const double independent =
                (static_cast<double>(rowTotal[i]) / total) * (static_cast<double>(colTotal[j]) / total);
            bits += joint * std::log2(joint / independent);
        }
    }
    return bits;
}

/// The answer, in the honest form: the largest group of patterns whose every
/// internal pair is confused below `threshold`, with the interval taken into
/// account rather than the point estimate alone.
struct DistinguishableSet {
    uint16_t members = 0; ///< Bitmask over the candidate set.
    uint8_t  size    = 0;
    /// Pairs that were dropped for want of trials rather than for confusion,
    /// so the reason a set is small is visible.
    uint16_t undersampledPairs = 0;
};

/// Whether `a` and `b` may sit in the same set: the pair has enough trials, and
/// the interval's upper bound is below the threshold. Using the upper bound
/// rather than the point estimate is what stops a pair confused once in twelve
/// from being called distinguishable.
inline bool mayCoexist(const Tally &tally, uint8_t a, uint8_t b, double threshold)
{
    const Interval interval = pairConfusion(tally, a, b);
    return interval.reportable && interval.high <= threshold;
}

/// Brute force over every subset, which is exact and costs nothing at this size.
inline DistinguishableSet largestDistinguishableSet(const Tally &tally, double threshold)
{
    DistinguishableSet best {};

    for (uint8_t a = 0; a < tally.patternCount; a++) {
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < tally.patternCount; b++) {
            if (!tally.pairFor(a, b).trials) {
                continue;
            }
            if (!pairConfusion(tally, a, b).reportable) {
                best.undersampledPairs++;
            }
        }
    }

    const uint32_t subsets = 1u << tally.patternCount;
    for (uint32_t mask = 1; mask < subsets; mask++) {
        uint8_t size = 0;
        for (uint8_t i = 0; i < tally.patternCount; i++) {
            if (mask & (1u << i)) {
                size++;
            }
        }
        if (size <= best.size) {
            continue;
        }

        bool ok = true;
        for (uint8_t a = 0; a < tally.patternCount && ok; a++) {
            if (!(mask & (1u << a))) {
                continue;
            }
            for (uint8_t b = static_cast<uint8_t>(a + 1); b < tally.patternCount && ok; b++) {
                if (!(mask & (1u << b))) {
                    continue;
                }
                ok = mayCoexist(tally, a, b, threshold);
            }
        }
        if (ok) {
            best.members = static_cast<uint16_t>(mask);
            best.size    = size;
        }
    }

    return best;
}

/// The worst pair any member of `set` is in, which is the number a vocabulary
/// entry has to carry.
inline Interval worstPairIn(const Tally &tally, uint16_t members, uint8_t pattern)
{
    Interval worst {};
    worst.reportable = false;
    for (uint8_t other = 0; other < tally.patternCount; other++) {
        if (other == pattern || !(members & (1u << other))) {
            continue;
        }
        const Interval interval = pairConfusion(tally, pattern, other);
        if (!worst.reportable || interval.high > worst.high) {
            worst = interval;
        }
    }
    return worst;
}

/// How many more trials a pair needs before its rate may be reported.
inline uint16_t trialsStillNeeded(const Tally &tally, uint8_t a, uint8_t b)
{
    const uint16_t have = tally.pairFor(a, b).trials;
    return have >= kMinTrialsPerPair ? 0 : static_cast<uint16_t>(kMinTrialsPerPair - have);
}

} // namespace Haptic::Analysis

#endif // HAPTICLAB_ANALYSIS_CONFUSION_HPP
