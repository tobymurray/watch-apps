#ifndef SLUMBER_RHYTHM_HPP
#define SLUMBER_RHYTHM_HPP

#include <cstddef>
#include <cstdint>

/// The two goal rings: Rest against a fixed goal, Rhythm as tonight's bedtime
/// against the usual one; `Slumber/README.md` records why these two.
namespace Rhythm
{

/// The AASM/SRS adult recommendation is "7 or more hours" (Watson et al. 2015).
constexpr uint16_t kRestGoalMin = 7u * 60u;

/// A bedtime this close to the usual closes the ring.
constexpr int32_t kWindowMin = 30;

/// A bedtime this far from the usual leaves the ring empty.
constexpr int32_t kEmptyMin = 120;

/// The usual is the median of up to this many nights before the one scored.
constexpr size_t kBaselineNights = 6;

/// Fewer nights than this and there is no usual yet.
constexpr size_t kMinBaselineNights = 3;

constexpr uint16_t kFull = 1000;  ///< ring fills are per mille of one lap

/// Local clock minutes counted from noon, so a night never wraps past midnight.
inline int32_t afterNoon(uint32_t utc, int32_t offsetMin)
{
    const int64_t local = static_cast<int64_t>(utc) + static_cast<int64_t>(offsetMin) * 60;
    const int32_t min   = static_cast<int32_t>(((local / 60) % 1440 + 1440) % 1440);
    return (min + 720) % 1440;
}

struct Night {
    int32_t  bed  = 0;  ///< afterNoon of the start
    int32_t  wake = 0;  ///< afterNoon of the end
    uint16_t restMin = 0;
};

struct Usual {
    bool    known = false;
    int32_t bed   = 0;  ///< afterNoon
    int32_t wake  = 0;  ///< afterNoon
};

namespace detail
{

/// Median of `n <= kBaselineNights` values, the lower-middle pair averaged down.
inline int32_t median(int32_t *v, size_t n)
{
    for (size_t i = 1; i < n; ++i) {
        for (size_t j = i; j > 0 && v[j - 1] > v[j]; --j) {
            const int32_t t = v[j];
            v[j]            = v[j - 1];
            v[j - 1]        = t;
        }
    }
    return n % 2 == 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

} // namespace detail

/// The usual bed and wake from the nights before `prior + n`, oldest first.
inline Usual usual(const Night *prior, size_t n)
{
    Usual u;
    const size_t from = n > kBaselineNights ? n - kBaselineNights : 0;
    const size_t k    = n - from;
    if (k < kMinBaselineNights) {
        return u;
    }
    int32_t beds[kBaselineNights];
    int32_t wakes[kBaselineNights];
    for (size_t i = 0; i < k; ++i) {
        beds[i]  = prior[from + i].bed;
        wakes[i] = prior[from + i].wake;
    }
    u.known = true;
    u.bed   = detail::median(beds, k);
    u.wake  = detail::median(wakes, k);
    return u;
}

/// Rest against the goal; past kFull is a second lap.
inline uint16_t restFill(uint16_t restMin)
{
    const uint32_t f = static_cast<uint32_t>(restMin) * kFull / kRestGoalMin;
    return static_cast<uint16_t>(f > 0xFFFFu ? 0xFFFFu : f);
}

/// Minutes `bed` is from the usual bedtime: negative is earlier.
inline int32_t offset(int32_t bed, const Usual &u)
{
    return bed - u.bed;
}

/// Full inside the window, empty past kEmptyMin, and linear between.
inline uint16_t rhythmFill(int32_t offsetMin)
{
    const int32_t a = offsetMin < 0 ? -offsetMin : offsetMin;
    if (a <= kWindowMin) {
        return kFull;
    }
    if (a >= kEmptyMin) {
        return 0;
    }
    return static_cast<uint16_t>(kFull - (a - kWindowMin) * kFull / (kEmptyMin - kWindowMin));
}

} // namespace Rhythm

#endif // SLUMBER_RHYTHM_HPP
