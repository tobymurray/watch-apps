#ifndef SLUMBER_SCHEDULE_HPP
#define SLUMBER_SCHEDULE_HPP

#include <cstddef>
#include <cstdint>

#include "Rhythm.hpp"

/// The calendar the week and Tonight are laid out on: which evening a night
/// belongs to, and the main night of each of the last seven.
namespace Schedule
{

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

constexpr size_t kWeekNights = 7;

/// A day in Bars with no main night.
constexpr int16_t kNoNight = -1;

/// The main night begun on each of the kWeekNights days before a day, oldest first.
struct Bars {
    int16_t bed[kWeekNights];   ///< Rhythm::afterNoon; kNoNight = none that day
    int16_t wake[kWeekNights];  ///< Rhythm::afterNoon, past 1440 when it wraps noon
    uint8_t newestWeekday = 0;  ///< the weekday the last one began on
};

/// The main nights in `recent` begun on the kWeekNights days before `day`.
inline Bars bars(const Rhythm::Recent &recent, int32_t day)
{
    Bars b;
    for (size_t i = 0; i < kWeekNights; ++i) {
        b.bed[i]  = kNoNight;
        b.wake[i] = kNoNight;
    }
    b.newestWeekday = weekday(day - 1);
    recent.each([&](int32_t nightDay, const Rhythm::Night &n) {
        const int32_t back = day - nightDay;
        if (back >= 1 && back <= static_cast<int32_t>(kWeekNights)) {
            const size_t at = kWeekNights - static_cast<size_t>(back);
            b.bed[at]       = static_cast<int16_t>(n.bed);
            b.wake[at]      = static_cast<int16_t>(n.wake < n.bed ? n.wake + 1440 : n.wake);
        }
    });
    return b;
}

} // namespace Schedule

#endif // SLUMBER_SCHEDULE_HPP
