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

struct Summary {
    uint16_t restlessSpells = 0;
    uint16_t longestRestMin = 0;  ///< unbroken by movement or missing data
};

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
