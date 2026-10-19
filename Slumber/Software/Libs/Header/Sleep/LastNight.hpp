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

/// Worn periods closer than this are one sleep broken by an awakening, not a night and a
/// nap: the split nights replayed in README "Finding a night" were 64-108 min apart.
constexpr uint32_t kSameSleepGapS = 3u * 60u * 60u;

namespace detail
{

/// Calls `f(longestPeriod, spanS)` for each sleep of recent worn periods, in time order.
template <typename F>
void eachSleep(const SleepPeriods::Period *p, size_t n, uint32_t nowUtc, F f)
{
    const SleepPeriods::Period *longest = nullptr;
    uint32_t                    spanS = 0, endUtc = 0;
    for (size_t i = 0; i < n; ++i) {
        const SleepPeriods::Period &q = p[i];
        if (q.worn != SleepPeriods::Worn::Worn || q.endUtc > nowUtc || nowUtc - q.endUtc > kRecentS) {
            continue;
        }
        if (longest != nullptr && q.startUtc - endUtc >= kSameSleepGapS) {
            f(longest, spanS);
            longest = nullptr;
            spanS   = 0;
        }
        if (longest == nullptr || q.endUtc - q.startUtc > longest->endUtc - longest->startUtc) {
            longest = &q;
        }
        spanS += q.endUtc - q.startUtc;
        endUtc = q.endUtc;
    }
    if (longest != nullptr) {
        f(longest, spanS);
    }
}

} // namespace detail

/// The longest period of the latest recent sleep at least half as long as the
/// longest recent sleep, else the longest recent not-worn period; `p` in time order.
inline Choice choose(const SleepPeriods::Period *p, size_t n, uint32_t nowUtc)
{
    uint32_t longestS = 0;
    detail::eachSleep(p, n, nowUtc, [&](const SleepPeriods::Period *, uint32_t spanS) {
        longestS = spanS > longestS ? spanS : longestS;
    });
    Choice best;
    detail::eachSleep(p, n, nowUtc, [&](const SleepPeriods::Period *q, uint32_t spanS) {
        if (2u * static_cast<uint64_t>(spanS) >= longestS) {
            best = Choice{Kind::Worn, q};
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
            best  = Choice{Kind::NotWorn, &q};
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
