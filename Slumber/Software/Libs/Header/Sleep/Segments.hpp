#ifndef SLUMBER_SEGMENTS_HPP
#define SLUMBER_SEGMENTS_HPP

#include <cstddef>
#include <cstdint>

#include "SleepPeriods.hpp"

/// A period as one character per 30 s epoch, the shape other apps read a night
/// in: slot k covers `startUtc + k * kEpochS` onwards.
namespace Segments
{

/// What the wrist saw in an epoch, before any smoothing. Neither is a claim
/// about being awake: a wearer lying awake and still is Rest.
constexpr char kRest     = 'r';
constexpr char kMovement = 'm';
constexpr char kNoData   = '-';

/// Slots in `p`, the length `strip` writes.
inline size_t slots(const SleepPeriods::Period &p)
{
    using SleepPeriods::kEpochS;
    return p.endUtc > p.startUtc ? (p.endUtc - p.startUtc + kEpochS - 1) / kEpochS : 0;
}

/// Writes `slots(p)` characters and a NUL to `out`, or nothing and returns 0 if
/// `cap` cannot hold them; an epoch lands in the slot its start is nearest.
inline size_t strip(const SleepPeriods::Epoch *e, size_t n, const SleepPeriods::Period &p,
                    char *out, size_t cap)
{
    using namespace SleepPeriods;
    const size_t k = slots(p);
    if (k == 0 || k + 1 > cap) {
        return 0;
    }
    for (size_t i = 0; i < k; ++i) {
        out[i] = kNoData;
    }
    out[k] = '\0';
    for (size_t i = 0; i < n; ++i) {
        if (e[i].utc < p.startUtc || e[i].utc >= p.endUtc) {
            continue;
        }
        const size_t at = (e[i].utc - p.startUtc + kEpochS / 2) / kEpochS;
        if (at >= k) {
            continue;
        }
        const char c = detail::activity(e[i]) >= kRestCountCeiling ? kMovement : kRest;
        if (out[at] != kMovement) {
            out[at] = c;
        }
    }
    return k;
}

} // namespace Segments

#endif // SLUMBER_SEGMENTS_HPP
