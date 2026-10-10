#ifndef SLUMBER_LAST_NIGHT_HPP
#define SLUMBER_LAST_NIGHT_HPP

#include <cstddef>
#include <cstdint>

#include "SleepPeriods.hpp"

/// Which of SleepPeriods' periods the card reports as last night.
namespace LastNight
{

/// A night ended longer ago than this is no longer "last night".
constexpr uint32_t kRecentS = 20u * 60u * 60u;

enum class Kind : uint8_t {
    None,     ///< nothing recent to report
    Worn,     ///< a night on the wrist
    NotWorn,  ///< rest found, but on a table
};

struct Choice {
    Kind                        kind   = Kind::None;
    const SleepPeriods::Period *period = nullptr;
};

inline int rank(Kind k)
{
    return k == Kind::Worn ? 2 : k == Kind::NotWorn ? 1 : 0;
}

/// The longest worn period that ended within kRecentS of `nowUtc`, so a night
/// split by a long awakening reports its larger half and a later nap does not
/// replace it; failing that, the longest recent not-worn one.
inline Choice choose(const SleepPeriods::Period *p, size_t n, uint32_t nowUtc)
{
    Choice   best;
    uint32_t bestSpan = 0;
    for (size_t i = 0; i < n; ++i) {
        const SleepPeriods::Period &q = p[i];
        if (q.endUtc > nowUtc || nowUtc - q.endUtc > kRecentS) {
            continue;
        }
        const Kind k = q.worn == SleepPeriods::Worn::Worn      ? Kind::Worn
                       : q.worn == SleepPeriods::Worn::NotWorn ? Kind::NotWorn
                                                               : Kind::None;
        const uint32_t span = q.endUtc - q.startUtc;
        if (rank(k) > rank(best.kind) || (k != Kind::None && k == best.kind && span > bestSpan)) {
            best.kind   = k;
            best.period = &q;
            bestSpan    = span;
        }
    }
    return best;
}

} // namespace LastNight

#endif // SLUMBER_LAST_NIGHT_HPP
