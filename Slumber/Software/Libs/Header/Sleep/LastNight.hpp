#ifndef SLUMBER_LAST_NIGHT_HPP
#define SLUMBER_LAST_NIGHT_HPP

#include <cstddef>
#include <cstdint>

#include "SleepPeriods.hpp"

/// Which sleep the card reports as last night.
namespace LastNight
{

/// A night ended longer ago than this is no longer "last night".
constexpr uint32_t kRecentS = 20u * 60u * 60u;

enum class Kind : uint8_t {
    None,     ///< nothing recent to report
    Worn,     ///< a night on the wrist
    NotWorn,  ///< rest found, but on a table
};

/// One or more periods reported as one night.
struct Sleep {
    uint32_t startUtc = 0;  ///< the first period's start
    uint32_t endUtc   = 0;  ///< the last period's end
    uint32_t spanS    = 0;  ///< the periods' lengths summed, leaving out the gaps between them
    uint16_t restMin  = 0;  ///< the periods' rest summed
    uint8_t  periods  = 0;
    bool     settled  = false;  ///< the last period is settled
    const SleepPeriods::Period *longest = nullptr;
    const SleepPeriods::Period *last    = nullptr;
};

struct Choice {
    Kind                        kind   = Kind::None;
    const SleepPeriods::Period *period = nullptr;  ///< the sleep's longest period
    Sleep                       sleep;
};

namespace detail
{

inline void extend(Sleep &s, const SleepPeriods::Period &q)
{
    if (s.periods == 0) {
        s.startUtc = q.startUtc;
    }
    if (s.longest == nullptr || q.endUtc - q.startUtc > s.longest->endUtc - s.longest->startUtc) {
        s.longest = &q;
    }
    s.endUtc = q.endUtc;
    s.spanS += q.endUtc - q.startUtc;
    s.restMin = static_cast<uint16_t>(s.restMin + q.restMin);
    s.settled = q.settled;
    s.last    = &q;
    ++s.periods;
}

/// Calls `f(sleep)` for each sleep of recent worn periods, in time order.
template <typename F>
void eachSleep(const SleepPeriods::Period *p, size_t n, uint32_t nowUtc, F f)
{
    Sleep s;
    for (size_t i = 0; i < n; ++i) {
        const SleepPeriods::Period &q = p[i];
        if (q.worn != SleepPeriods::Worn::Worn || q.endUtc > nowUtc || nowUtc - q.endUtc > kRecentS) {
            continue;
        }
        if (s.periods > 0 && q.startUtc - s.endUtc >= SleepPeriods::kSameSleepGapS) {
            f(s);
            s = Sleep{};
        }
        extend(s, q);
    }
    if (s.periods > 0) {
        f(s);
    }
}

} // namespace detail

/// The latest recent sleep at least half as long as the longest recent sleep,
/// else the longest recent not-worn period; `p` in time order.
inline Choice choose(const SleepPeriods::Period *p, size_t n, uint32_t nowUtc)
{
    uint32_t longestS = 0;
    detail::eachSleep(p, n, nowUtc, [&](const Sleep &s) { longestS = s.spanS > longestS ? s.spanS : longestS; });
    Choice best;
    detail::eachSleep(p, n, nowUtc, [&](const Sleep &s) {
        if (2u * static_cast<uint64_t>(s.spanS) >= longestS) {
            best = Choice{Kind::Worn, s.longest, s};
        }
    });
    if (best.kind == Kind::Worn) {
        return best;
    }

    uint32_t bestS = 0;
    for (size_t i = 0; i < n; ++i) {
        const SleepPeriods::Period &q = p[i];
        if (q.worn == SleepPeriods::Worn::NotWorn && q.endUtc <= nowUtc && nowUtc - q.endUtc <= kRecentS &&
            q.endUtc - q.startUtc > bestS) {
            best = Choice{Kind::NotWorn, &q, Sleep{}};
            detail::extend(best.sleep, q);
            bestS = q.endUtc - q.startUtc;
        }
    }
    return best;
}

/// When the worn period still running at `lastEpochUtc` began, or 0. A period
/// needs SleepPeriods::kMinBlockS of rest to exist, so this lags falling asleep
/// by that much.
inline uint32_t asleepSince(const SleepPeriods::Period *p, size_t n, uint32_t lastEpochUtc)
{
    using namespace SleepPeriods;
    for (size_t i = n; i > 0; --i) {
        const Period &q = p[i - 1];
        if (!q.settled && q.worn == Worn::Worn && q.endUtc + kMaxStepS >= lastEpochUtc + kEpochS) {
            return q.startUtc;
        }
    }
    return 0;
}

} // namespace LastNight

#endif // SLUMBER_LAST_NIGHT_HPP
