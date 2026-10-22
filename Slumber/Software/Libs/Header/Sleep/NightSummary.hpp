#ifndef SLUMBER_NIGHT_SUMMARY_HPP
#define SLUMBER_NIGHT_SUMMARY_HPP

#include <cstddef>
#include <cstdint>

#include "SleepPeriods.hpp"

/// What a period held besides its edges: the numbers a night is reported with.
namespace NightSummary
{

/// Consecutive epochs above SleepPeriods' rest ceiling that make a restless
/// spell. Measured over 23 worn nights (SleepLab and Slumber): movement inside
/// them came in runs of 1-2 min, 198 runs of a minute or more and none of 5, so
/// a wrist cannot see a wearer lying awake and still, and this counts movement,
/// not awakenings.
constexpr size_t kSpellEpochs = 2;

/// The lowest heart rate is the lowest mean over a window this long.
constexpr uint32_t kHrLowWindowS = 10u * 60u;

/// A window needs this many trusted readings of its 20 epochs to count.
constexpr size_t kHrLowMinReadings = 15;

/// Heart rates outside this span of bpm are not a sleeping wrist's.
constexpr uint8_t kHrMinBpm = 30;
constexpr uint8_t kHrMaxBpm = 149;

struct Summary {
    uint16_t restlessSpells = 0;
    uint16_t longestRestMin = 0;  ///< unbroken by movement or missing data
};

/// Sleeping heart rate over a period, from epochs whose reading is trusted.
struct Heart {
    uint8_t  medianBpm = 0;  ///< 0 = under SleepPeriods::kMinHrCoveragePct of epochs trusted
    uint8_t  lowBpm    = 0;  ///< 0 = no window had kHrLowMinReadings
    uint32_t lowUtc    = 0;  ///< s, the middle of the lowest window
};

/// Whether `e` read a heart rate trusted enough to count, that rounds into kHrMinBpm..kHrMaxBpm.
inline bool trustedHr(const SleepPeriods::Epoch &e)
{
    return e.hasHr && e.hrTrustX10 >= SleepPeriods::kWornTrustX10 && e.hrX10 + 5u >= kHrMinBpm * 10u &&
           e.hrX10 + 5u < (kHrMaxBpm + 1u) * 10u;
}

/// The median of the trusted readings in `p`, to the nearest bpm, and the
/// lowest kHrLowWindowS mean.
inline Heart heart(const SleepPeriods::Epoch *e, size_t n, const SleepPeriods::Period &p)
{
    Heart    h;
    uint16_t bins[kHrMaxBpm - kHrMinBpm + 1] = {};
    size_t   present  = 0;
    size_t   readings = 0;
    uint32_t bestSum  = 0;
    size_t   bestN    = 0;
    uint32_t sum      = 0;
    size_t   inWindow = 0;
    size_t   j        = 0;
    for (size_t i = 0; i < n; ++i) {
        if (e[i].utc < p.startUtc || e[i].utc >= p.endUtc) {
            continue;
        }
        ++present;
        if (trustedHr(e[i])) {
            ++bins[(e[i].hrX10 + 5u) / 10u - kHrMinBpm];
            ++readings;
        }
        j = j < i ? i : j;
        while (j < n && e[j].utc < e[i].utc + kHrLowWindowS && e[j].utc < p.endUtc) {
            if (trustedHr(e[j])) {
                sum += e[j].hrX10;
                ++inWindow;
            }
            ++j;
        }
        if (inWindow >= kHrLowMinReadings && (bestN == 0 || sum * bestN < bestSum * inWindow)) {
            bestSum  = sum;
            bestN    = inWindow;
            h.lowUtc = e[i].utc + kHrLowWindowS / 2;
        }
        if (trustedHr(e[i])) {
            sum -= e[i].hrX10;
            --inWindow;
        }
    }
    if (present == 0 || readings * 100u < present * SleepPeriods::kMinHrCoveragePct) {
        return Heart{};
    }
    size_t seen = 0;
    for (size_t b = 0; b < sizeof(bins) / sizeof(bins[0]); ++b) {
        seen += bins[b];
        if (seen * 2u >= readings) {
            h.medianBpm = static_cast<uint8_t>(kHrMinBpm + b);
            break;
        }
    }
    if (bestN > 0) {
        h.lowBpm = static_cast<uint8_t>((bestSum + bestN * 5u) / (bestN * 10u));
    } else {
        h.lowUtc = 0;
    }
    return h;
}

inline Summary summarise(const SleepPeriods::Epoch *e, size_t n, const SleepPeriods::Period &p)
{
    using namespace SleepPeriods;
    Summary  s;
    size_t   moving  = 0;
    uint32_t rest    = 0;
    uint32_t longest = 0;
    uint32_t prevUtc = 0;
    for (size_t i = 0; i < n; ++i) {
        if (e[i].utc < p.startUtc || e[i].utc >= p.endUtc) {
            continue;
        }
        if (prevUtc != 0 && e[i].utc - prevUtc > kMaxStepS) {
            moving = 0;
            rest   = 0;
        }
        prevUtc = e[i].utc;

        if (detail::activity(e[i]) >= kRestCountCeiling) {
            if (++moving == kSpellEpochs) {
                ++s.restlessSpells;
            }
        } else {
            moving = 0;
        }
        rest    = detail::isRest(e, n, i) ? rest + 1 : 0;
        longest = rest > longest ? rest : longest;
    }
    s.longestRestMin = static_cast<uint16_t>(longest * kEpochS / 60u);
    return s;
}

} // namespace NightSummary

#endif // SLUMBER_NIGHT_SUMMARY_HPP
