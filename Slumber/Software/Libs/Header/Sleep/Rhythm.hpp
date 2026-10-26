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
constexpr size_t kMinBaselineNights = 7;

/// Whether there is a usual at all is asked of this many nights, so a rota's
/// last block cannot pass for a habit.
constexpr size_t kKeptNights = 14;

/// A night this close to the usual keeps it, chosen on synthetic schedules
/// rather than wearers: `Slumber/README.md` "Irregular hours" has the sweep.
constexpr int32_t kKeptMin = 4 * 60;

constexpr uint16_t kFull = 1000;  ///< ring fills are per mille of one lap

/// Local clock minutes counted from noon, so a night never wraps past midnight.
inline int32_t afterNoon(uint32_t utc, int32_t offsetMin)
{
    const int64_t local = static_cast<int64_t>(utc) + static_cast<int64_t>(offsetMin) * 60;
    const int32_t min   = static_cast<int32_t>(((local / 60) % 1440 + 1440) % 1440);
    return (min + 720) % 1440;
}

/// The noon-to-noon local day `utc` falls in, as days since 1970-01-01; a night
/// belongs to the evening it began on.
inline int32_t noonDay(uint32_t utc, int32_t offsetMin)
{
    const int64_t local = static_cast<int64_t>(utc) + static_cast<int64_t>(offsetMin) * 60 - 43200;
    return static_cast<int32_t>(local >= 0 ? local / 86400 : (local - 86399) / 86400);
}

struct Night {
    int32_t  bed  = 0;  ///< afterNoon of the start
    int32_t  wake = 0;  ///< afterNoon of the end
    uint16_t restMin = 0;
};

struct Usual {
    bool    known  = false;
    int32_t bed    = 0;  ///< afterNoon
    int32_t wake   = 0;  ///< afterNoon
    uint8_t nights = 0;  ///< the median is of this many
    bool    varies = false;  ///< enough nights, but most are not near any one time
};

/// Shortest signed distance from `b` to `a` round the clock, in (-720, 720].
inline int32_t apart(int32_t a, int32_t b)
{
    const int32_t d = ((a - b) % 1440 + 1440) % 1440;
    return d > 720 ? d - 1440 : d;
}

/// The median of clock times, cut where the clock has its widest stretch with
/// none, so 11:50 and 12:10 are 20 minutes apart; sorts `v`.
template <typename T>
inline int32_t centre(T *v, size_t n)
{
    if (n == 0) {
        return 0;
    }
    for (size_t i = 1; i < n; ++i) {
        for (size_t j = i; j > 0 && v[j - 1] > v[j]; --j) {
            const T t = v[j];
            v[j]      = v[j - 1];
            v[j - 1]  = t;
        }
    }
    size_t  cut    = 0;
    int32_t widest = static_cast<int32_t>(v[0]) + 1440 - v[n - 1];
    for (size_t i = 1; i < n; ++i) {
        if (v[i] - v[i - 1] > widest) {
            widest = v[i] - v[i - 1];
            cut    = i;
        }
    }
    auto unwrapped = [&](size_t j) {
        return cut + j < n ? static_cast<int32_t>(v[cut + j]) : static_cast<int32_t>(v[cut + j - n]) + 1440;
    };
    return (n % 2 == 1 ? unwrapped(n / 2) : (unwrapped(n / 2 - 1) + unwrapped(n / 2)) / 2) % 1440;
}

/// Whether two thirds of the times lie within kKeptMin of `at`.
template <typename T>
inline bool kept(const T *v, size_t n, int32_t at)
{
    size_t near = 0;
    for (size_t i = 0; i < n; ++i) {
        const int32_t d = apart(v[i], at);
        near += (d < 0 ? -d : d) <= kKeptMin ? 1 : 0;
    }
    return n > 0 && 3 * near >= 2 * n;
}

/// The usual bed and wake from the nights before `prior + n`, oldest first:
/// the median of the last kBaselineNights, if the last kKeptNights keep it.
inline Usual usual(const Night *prior, size_t n)
{
    Usual u;
    if (n < kMinBaselineNights) {
        return u;
    }
    const size_t from = n > kBaselineNights ? n - kBaselineNights : 0;
    const size_t k    = n - from;
    int32_t beds[kBaselineNights];
    int32_t wakes[kBaselineNights];
    for (size_t i = 0; i < k; ++i) {
        beds[i]  = prior[from + i].bed;
        wakes[i] = prior[from + i].wake;
    }
    u.bed    = centre(beds, k);
    u.wake   = centre(wakes, k);
    u.nights = static_cast<uint8_t>(k);
    const size_t gFrom = n > kKeptNights ? n - kKeptNights : 0;
    int32_t      gBeds[kKeptNights];
    int32_t      gWakes[kKeptNights];
    for (size_t i = gFrom; i < n; ++i) {
        gBeds[i - gFrom]  = prior[i].bed;
        gWakes[i - gFrom] = prior[i].wake;
    }
    u.known  = kept(gBeds, n - gFrom, u.bed) && kept(gWakes, n - gFrom, u.wake);
    u.varies = !u.known;
    return u;
}

/// Minutes past local midnight of an afterNoon time.
inline int32_t minuteOfDay(int32_t afterNoonMin)
{
    return (afterNoonMin + 720) % 1440;
}

/// The main night of each recent noon-to-noon local day, the longest worn
/// period that began in it, oldest first; a nap beside a night is not one.
class Recent
{
public:
    static constexpr size_t kCap = kKeptNights + 1;

    void add(uint32_t startUtc, uint32_t endUtc, int32_t offsetMin, uint16_t restMin)
    {
        const int32_t day = noonDay(startUtc, offsetMin);
        Main m{day, startUtc, endUtc - startUtc,
               Night{afterNoon(startUtc, offsetMin), afterNoon(endUtc, offsetMin), restMin}};
        for (size_t i = 0; i < mN; ++i) {
            if (mMain[i].day == day) {
                if (m.spanS > mMain[i].spanS) {
                    mMain[i] = m;
                }
                return;
            }
        }
        size_t at = mN;
        while (at > 0 && mMain[at - 1].day > day) {
            --at;
        }
        if (mN == kCap) {
            if (at == 0) {
                return;
            }
            for (size_t i = 1; i < mN; ++i) {
                mMain[i - 1] = mMain[i];
            }
            --mN;
            --at;
        }
        for (size_t i = mN; i > at; --i) {
            mMain[i] = mMain[i - 1];
        }
        mMain[at] = m;
        ++mN;
    }

    /// The usual from the main nights that began before `beforeUtc`.
    Usual usualBefore(uint32_t beforeUtc) const
    {
        Night  prior[kCap];
        size_t k = 0;
        for (size_t i = 0; i < mN; ++i) {
            if (mMain[i].startUtc < beforeUtc) {
                prior[k++] = mMain[i].night;
            }
        }
        return usual(prior, k);
    }

    size_t size() const { return mN; }

    /// Nights still to log before there can be a usual.
    uint8_t nightsToGo() const
    {
        return static_cast<uint8_t>(mN < kMinBaselineNights ? kMinBaselineNights - mN : 0);
    }

private:
    struct Main {
        int32_t  day     = 0;
        uint32_t startUtc = 0;
        uint32_t spanS   = 0;
        Night    night;
    };
    Main   mMain[kCap];
    size_t mN = 0;
};

/// Rest against the goal; past kFull is a second lap.
inline uint16_t restFill(uint16_t restMin)
{
    const uint32_t f = static_cast<uint32_t>(restMin) * kFull / kRestGoalMin;
    return static_cast<uint16_t>(f > 0xFFFFu ? 0xFFFFu : f);
}

/// Minutes `bed` is from the usual bedtime: negative is earlier.
inline int32_t offset(int32_t bed, const Usual &u)
{
    return apart(bed, u.bed);
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
