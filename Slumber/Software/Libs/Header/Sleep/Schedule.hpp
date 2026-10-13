#ifndef SLUMBER_SCHEDULE_HPP
#define SLUMBER_SCHEDULE_HPP

#include <cstddef>
#include <cstdint>

#include "Rhythm.hpp"

/// The wearer's week as it is lived, learned from the main night of each day:
/// a usual bed and wake for each night of the week, and the weekdays that
/// keep to their own hours.
namespace Schedule
{

/// Nights older than this many days are forgotten, so a new routine replaces an old one.
constexpr int32_t kWindowDays = 56;

/// The rest of the week weighs as this many of a weekday's own nights, so one
/// odd night barely moves that weekday's usual.
constexpr int32_t kPriorNights = 2;

/// A weekday needs this many nights, and so does the rest of the week, before
/// it can be said to differ.
constexpr size_t kMinPatternNights = 3;

/// A weekday whose median is this far from the rest of the week's differs.
constexpr int32_t kPatternMin = 45;

/// And this share of its nights must lean the same way by half as much.
constexpr unsigned kAgreePct = 75;

/// Before this local time, tonight is still the night begun the evening before.
constexpr int32_t kDayTurnMin = 4 * 60;

constexpr uint8_t kSunday   = 0;
constexpr uint8_t kFriday   = 5;
constexpr uint8_t kSaturday = 6;

/// 0 is Sunday; 1970-01-01, day 0, was a Thursday.
inline uint8_t weekday(int32_t day)
{
    return static_cast<uint8_t>((day % 7 + 11) % 7);
}

/// The day whose evening the next night, or the one under way, begins on.
inline int32_t tonight(uint32_t nowUtc, int32_t offsetMin)
{
    const int64_t local = static_cast<int64_t>(nowUtc) + (static_cast<int64_t>(offsetMin) - kDayTurnMin) * 60;
    return static_cast<int32_t>(local >= 0 ? local / 86400 : (local - 86399) / 86400);
}

struct Expected {
    bool    known  = false;
    int32_t bed    = 0;  ///< Rhythm::afterNoon
    int32_t wake   = 0;  ///< Rhythm::afterNoon
    uint8_t own    = 0;  ///< nights of this weekday behind it
    uint8_t nights = 0;  ///< nights behind it in all
};

/// One weekday against the rest of the week; shifts are its median minus theirs.
struct Pattern {
    bool    known      = false;
    int32_t bedShift   = 0;  ///< minutes; positive is later
    int32_t wakeShift  = 0;  ///< minutes; positive is later
    uint8_t nights     = 0;
    uint8_t bedAgree   = 0;  ///< nights leaning the way bedShift does
    uint8_t wakeAgree  = 0;  ///< nights leaning the way wakeShift does
};

/// Bit `weekday` is set for each weekday that differs that way.
struct Week {
    uint8_t laterBed    = 0;
    uint8_t earlierBed  = 0;
    uint8_t laterWake   = 0;
    uint8_t earlierWake = 0;
    bool    jetlagKnown = false;
    int32_t socialJetlagMin = 0;  ///< Friday and Saturday nights' midpoint minus the others'
};

namespace detail
{

inline int32_t median(int16_t *v, size_t n)
{
    for (size_t i = 1; i < n; ++i) {
        for (size_t j = i; j > 0 && v[j - 1] > v[j]; --j) {
            const int16_t t = v[j];
            v[j]            = v[j - 1];
            v[j - 1]        = t;
        }
    }
    return n % 2 == 1 ? v[n / 2] : (static_cast<int32_t>(v[n / 2 - 1]) + v[n / 2]) / 2;
}

/// Whether `shift` is a difference and enough of `n` nights lean its way.
inline bool holds(int32_t shift, uint8_t agree, uint8_t n)
{
    const int32_t a = shift < 0 ? -shift : shift;
    return a >= kPatternMin && static_cast<unsigned>(agree) * 100u >= static_cast<unsigned>(n) * kAgreePct;
}

} // namespace detail

/// The main nights of the last kWindowDays noon-to-noon days, oldest first.
class Learner
{
public:
    static constexpr size_t kCap = static_cast<size_t>(kWindowDays);

    /// Keeps the longest period begun on each day as that day's night.
    void add(uint32_t startUtc, uint32_t endUtc, int32_t offsetMin)
    {
        const int32_t day    = Rhythm::noonDay(startUtc, offsetMin);
        const int32_t newest = mN > 0 && mMain[mN - 1].day > day ? mMain[mN - 1].day : day;
        if (day <= newest - kWindowDays) {
            return;
        }
        forgetBefore(newest - kWindowDays + 1);
        const int32_t bed  = Rhythm::afterNoon(startUtc, offsetMin);
        int32_t       wake = Rhythm::afterNoon(endUtc, offsetMin);
        if (wake < bed) {
            wake += 1440;
        }
        const Main m{day, endUtc - startUtc, static_cast<int16_t>(bed), static_cast<int16_t>(wake)};
        size_t     at = mN;
        while (at > 0 && mMain[at - 1].day > day) {
            --at;
        }
        if (at > 0 && mMain[at - 1].day == day) {
            if (m.spanS > mMain[at - 1].spanS) {
                mMain[at - 1] = m;
            }
            return;
        }
        for (size_t i = mN; i > at; --i) {
            mMain[i] = mMain[i - 1];
        }
        mMain[at] = m;
        ++mN;
    }

    /// The usual for the night begun on `day`, from the nights before it: the
    /// weekday's own median pulled toward the rest of the week's.
    Expected expect(int32_t day) const
    {
        Expected e;
        const uint8_t wd = weekday(day);
        int16_t       ownBed[kCap], ownWake[kCap], restBed[kCap], restWake[kCap];
        size_t        own = 0, rest = 0;
        for (size_t i = 0; i < mN && mMain[i].day < day; ++i) {
            if (weekday(mMain[i].day) == wd) {
                ownBed[own]    = mMain[i].bed;
                ownWake[own++] = mMain[i].wake;
            } else {
                restBed[rest]    = mMain[i].bed;
                restWake[rest++] = mMain[i].wake;
            }
        }
        if (own + rest < Rhythm::kMinBaselineNights) {
            return e;
        }
        e.known  = true;
        e.own    = static_cast<uint8_t>(own);
        e.nights = static_cast<uint8_t>(own + rest);
        if (rest == 0) {
            e.bed  = detail::median(ownBed, own);
            e.wake = detail::median(ownWake, own);
        } else if (own == 0) {
            e.bed  = detail::median(restBed, rest);
            e.wake = detail::median(restWake, rest);
        } else {
            const int32_t n = static_cast<int32_t>(own);
            e.bed  = (n * detail::median(ownBed, own) + kPriorNights * detail::median(restBed, rest)) /
                    (n + kPriorNights);
            e.wake = (n * detail::median(ownWake, own) + kPriorNights * detail::median(restWake, rest)) /
                     (n + kPriorNights);
        }
        e.bed %= 1440;
        e.wake %= 1440;
        return e;
    }

    /// Weekday `wd` against the rest of the week, over every night kept.
    Pattern pattern(uint8_t wd) const
    {
        Pattern p;
        int16_t ownBed[kCap], ownWake[kCap], restBed[kCap], restWake[kCap];
        size_t  own = 0, rest = 0;
        for (size_t i = 0; i < mN; ++i) {
            if (weekday(mMain[i].day) == wd) {
                ownBed[own]    = mMain[i].bed;
                ownWake[own++] = mMain[i].wake;
            } else {
                restBed[rest]    = mMain[i].bed;
                restWake[rest++] = mMain[i].wake;
            }
        }
        p.nights = static_cast<uint8_t>(own);
        if (own < kMinPatternNights || rest < kMinPatternNights) {
            return p;
        }
        const int32_t usualBed  = detail::median(restBed, rest);
        const int32_t usualWake = detail::median(restWake, rest);
        p.known     = true;
        p.bedShift  = detail::median(ownBed, own) - usualBed;
        p.wakeShift = detail::median(ownWake, own) - usualWake;
        for (size_t i = 0; i < own; ++i) {
            p.bedAgree += leans(ownBed[i] - usualBed, p.bedShift) ? 1 : 0;
            p.wakeAgree += leans(ownWake[i] - usualWake, p.wakeShift) ? 1 : 0;
        }
        return p;
    }

    /// Which weekdays differ, and how far the weekend's sleep sits from the week's.
    Week week() const
    {
        Week w;
        for (uint8_t d = kSunday; d <= kSaturday; ++d) {
            const Pattern p   = pattern(d);
            const uint8_t bit = static_cast<uint8_t>(1u << d);
            if (!p.known) {
                continue;
            }
            if (detail::holds(p.bedShift, p.bedAgree, p.nights)) {
                (p.bedShift > 0 ? w.laterBed : w.earlierBed) |= bit;
            }
            if (detail::holds(p.wakeShift, p.wakeAgree, p.nights)) {
                (p.wakeShift > 0 ? w.laterWake : w.earlierWake) |= bit;
            }
        }
        int16_t free[kCap], work[kCap];
        size_t  nf = 0, nw = 0;
        for (size_t i = 0; i < mN; ++i) {
            const uint8_t d   = weekday(mMain[i].day);
            const int16_t mid = static_cast<int16_t>((mMain[i].bed + mMain[i].wake) / 2);
            if (d == kFriday || d == kSaturday) {
                free[nf++] = mid;
            } else {
                work[nw++] = mid;
            }
        }
        if (nf >= kMinPatternNights && nw >= kMinPatternNights) {
            w.jetlagKnown     = true;
            w.socialJetlagMin = detail::median(free, nf) - detail::median(work, nw);
        }
        return w;
    }

    size_t size() const { return mN; }

private:
    /// Drops every night begun before `day`; what is left spans under kWindowDays, so fits.
    void forgetBefore(int32_t day)
    {
        size_t drop = 0;
        while (drop < mN && mMain[drop].day < day) {
            ++drop;
        }
        for (size_t i = drop; i < mN; ++i) {
            mMain[i - drop] = mMain[i];
        }
        mN -= drop;
    }

    /// Whether a night `delta` from the rest of the week leans the way of
    /// `shift` by at least half the pattern threshold.
    static bool leans(int32_t delta, int32_t shift)
    {
        return shift > 0 ? delta >= kPatternMin / 2 : shift < 0 ? delta <= -kPatternMin / 2 : false;
    }

    struct Main {
        int32_t  day   = 0;
        uint32_t spanS = 0;
        int16_t  bed   = 0;  ///< Rhythm::afterNoon
        int16_t  wake  = 0;  ///< Rhythm::afterNoon, past 1440 when it wraps noon
    };
    Main   mMain[kCap];
    size_t mN = 0;
};

} // namespace Schedule

#endif // SLUMBER_SCHEDULE_HPP
