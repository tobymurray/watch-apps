/**
 ******************************************************************************
 * @file    WallClock.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The one place this app reads the wall clock.
 ******************************************************************************
 *
 * Every visible thing this app produces is a function of what time it is, so a
 * service that called `std::time(nullptr)` directly could only ever be tested
 * at whatever o'clock the test happened to run -- and the interesting instants
 * are the minute before sunrise, the minute after sunset, and midnight. Those
 * are not times to be at a desk.
 *
 * The seam is a function pointer rather than an interface because there is one
 * caller-visible fact here -- what time it is -- and a virtual class for one
 * integer would be ceremony. It is set once, before `run()`, and only ever by a
 * test; on a watch nothing calls the setter and `std::time` is what answers.
 *
 * Lifted from `SleepLab/Software/Libs/Header/WallClock.hpp`, which explains the
 * other half of why that app has one: a rule that no duration is ever derived
 * from two wall-clock readings is enforceable when the reads are countable.
 * This app derives exactly one duration from the clock -- how long until the
 * next event -- and it is a countdown to a fixed instant rather than an
 * interval between two readings, so a clock that jumps moves the countdown
 * rather than corrupting a record.
 *
 ******************************************************************************
 */

#ifndef WALLCLOCK_HPP
#define WALLCLOCK_HPP

#include <cstdint>

namespace Sun
{

/// Signature of a wall-clock source: UTC seconds, or <= 0 when unreadable.
using WallClockFn = int64_t (*)();

/// The wall clock, in UTC seconds, or -1 when it cannot be read.
int64_t wallClockUtc();

/// Replace the wall-clock source. Host tests only; `nullptr` restores
/// `std::time`. Not thread-safe and not meant to be.
void setWallClockSource(WallClockFn fn);

} // namespace Sun

#endif // WALLCLOCK_HPP
