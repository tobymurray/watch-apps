#ifndef SLUMBER_SCHEDULE_HPP
#define SLUMBER_SCHEDULE_HPP

#include <cstddef>
#include <cstdint>

#include "Rhythm.hpp"

/// The wearer's normal, learned from the main night of each day: a usual bed
/// and wake for each night of the week, and the weekdays that keep to their
/// own hours.
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
    bool    varies = false;  ///< enough nights, but they keep no one time
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

constexpr size_t kWeekNights = 7;

/// A day in Bars with no main night.
constexpr int16_t kNoNight = -1;

/// The main night begun on each of the kWeekNights days before a day, oldest first.
struct Bars {
    int16_t bed[kWeekNights];   ///< Rhythm::afterNoon; kNoNight = none that day
    int16_t wake[kWeekNights];  ///< Rhythm::afterNoon, past 1440 when it wraps noon
    uint8_t newestWeekday = 0;  ///< the weekday the last one began on
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

inline bool within(int32_t a, int32_t b)
{
    const int32_t d = Rhythm::apart(a, b);
    return (d < 0 ? -d : d) <= Rhythm::kKeptMin;
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
                ownWake[own++] = static_cast<int16_t>(mMain[i].wake % 1440);
            } else {
                restBed[rest]    = mMain[i].bed;
                restWake[rest++] = static_cast<int16_t>(mMain[i].wake % 1440);
            }
        }
        if (own + rest < Rhythm::kMinBaselineNights) {
            return e;
        }
        e.own    = static_cast<uint8_t>(own);
        e.nights = static_cast<uint8_t>(own + rest);
        const int32_t ob = Rhythm::centre(ownBed, own), ow = Rhythm::centre(ownWake, own);
        const int32_t rb = Rhythm::centre(restBed, rest), rw = Rhythm::centre(restWake, rest);
        const bool ownKeeps  = Rhythm::kept(ownBed, own, ob) && Rhythm::kept(ownWake, own, ow);
        const bool restKeeps = Rhythm::kept(restBed, rest, rb) && Rhythm::kept(restWake, rest, rw);
        const bool near      = detail::within(ob, rb) && detail::within(ow, rw);
        const bool differs = own >= kMinPatternNights && !near;
        if (!restKeeps || differs) {
            e.varies = true;
            return e;
        }
        const int32_t n = ownKeeps && near ? static_cast<int32_t>(own) : 0;
        e.bed  = rb + Rhythm::apart(ob, rb) * n / (n + kPriorNights);
        e.wake = rw + Rhythm::apart(ow, rw) * n / (n + kPriorNights);
        e.known = true;
        e.bed   = (e.bed % 1440 + 1440) % 1440;
        e.wake  = (e.wake % 1440 + 1440) % 1440;
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
                ownWake[own++] = static_cast<int16_t>(mMain[i].wake % 1440);
            } else {
                restBed[rest]    = mMain[i].bed;
                restWake[rest++] = static_cast<int16_t>(mMain[i].wake % 1440);
            }
        }
        p.nights = static_cast<uint8_t>(own);
        if (own < kMinPatternNights || rest < kMinPatternNights) {
            return p;
        }
        const int32_t rb = Rhythm::centre(restBed, rest), rw = Rhythm::centre(restWake, rest);
        const int32_t ob = Rhythm::centre(ownBed, own), ow = Rhythm::centre(ownWake, own);
        if (!Rhythm::kept(restBed, rest, rb) || !Rhythm::kept(restWake, rest, rw)) {
            return p;
        }
        p.known     = true;
        p.bedShift  = Rhythm::apart(ob, rb);
        p.wakeShift = Rhythm::apart(ow, rw);
        for (size_t i = 0; i < own; ++i) {
            p.bedAgree += leans(Rhythm::apart(ownBed[i], rb), p.bedShift) ? 1 : 0;
            p.wakeAgree += leans(Rhythm::apart(ownWake[i], rw), p.wakeShift) ? 1 : 0;
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
            const int16_t mid = static_cast<int16_t>((mMain[i].bed + mMain[i].wake) / 2 % 1440);
            if (d == kFriday || d == kSaturday) {
                free[nf++] = mid;
            } else {
                work[nw++] = mid;
            }
        }
        if (nf >= kMinPatternNights && nw >= kMinPatternNights) {
            const int32_t f = Rhythm::centre(free, nf), k = Rhythm::centre(work, nw);
            w.jetlagKnown     = Rhythm::kept(free, nf, f) && Rhythm::kept(work, nw, k);
            w.socialJetlagMin = w.jetlagKnown ? Rhythm::apart(f, k) : 0;
        }
        return w;
    }

    /// The main nights begun on the kWeekNights days before `day`.
    Bars before(int32_t day) const
    {
        Bars b;
        for (size_t i = 0; i < kWeekNights; ++i) {
            b.bed[i]  = kNoNight;
            b.wake[i] = kNoNight;
        }
        b.newestWeekday = weekday(day - 1);
        for (size_t i = 0; i < mN; ++i) {
            const int32_t back = day - mMain[i].day;
            if (back >= 1 && back <= static_cast<int32_t>(kWeekNights)) {
                b.bed[kWeekNights - static_cast<size_t>(back)]  = mMain[i].bed;
                b.wake[kWeekNights - static_cast<size_t>(back)] = mMain[i].wake;
            }
        }
        return b;
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
        int32_t  day      = 0;
        uint32_t spanS    = 0;
        int16_t  bed      = 0;  ///< Rhythm::afterNoon
        int16_t  wake     = 0;  ///< Rhythm::afterNoon, past 1440 when it wraps noon
    };
    Main   mMain[kCap];
    size_t mN = 0;
};

/// Tonight's usual as shown: the weekday's own while it agrees with `u`, `u`
/// when the weekday lags a change of hours, and none when `u` is unknown.
inline Expected agree(const Expected &e, const Rhythm::Usual &u)
{
    if (!u.known) {
        Expected none;
        none.varies = u.varies;
        return none;
    }
    if (e.known && detail::within(e.bed, u.bed) && detail::within(e.wake, u.wake)) {
        return e;
    }
    Expected recent;
    recent.known  = true;
    recent.bed    = u.bed;
    recent.wake   = u.wake;
    recent.nights = u.nights;
    return recent;
}

} // namespace Schedule

#endif // SLUMBER_SCHEDULE_HPP
