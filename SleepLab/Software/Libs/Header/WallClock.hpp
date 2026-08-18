/**
 ******************************************************************************
 * @file    WallClock.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The one place this app reads the wall clock.
 ******************************************************************************
 *
 * Every wall-clock read in the recorder goes through `wallClockUtc()`. Two
 * reasons, and the second is the one that pays for the file existing:
 *
 *   - **There is exactly one of them.** The platform rule is that no duration
 *     is ever derived from two wall-clock readings -- the clock jumps on a
 *     timezone change, a host sync or DST, and a jump would silently rewrite an
 *     interval. A rule like that is enforceable when the reads are countable
 *     and are not when they are scattered through a thousand lines.
 *
 *   - **A night can be replayed at a desk.** `std::time(nullptr)` cannot be
 *     moved, so a service that calls it directly can only ever be tested at
 *     whatever time of day the test happens to run -- which means the bedtime
 *     window, the segmenter's window exit, the alarm and every time-of-day
 *     label are untestable, and those are precisely the parts whose failure
 *     costs a night. The uptime clock is already injectable through
 *     `SDK::Interface::ISystem`; this is the wall clock's equivalent, and it is
 *     the difference between eight hours of feedback and eight seconds.
 *
 * The seam is a function pointer rather than a class because there is one
 * caller-visible fact here -- what time it is -- and a virtual interface for
 * one integer would be ceremony. It is set once, before `run()`, and never
 * from the sample path.
 *
 ******************************************************************************
 */

#ifndef WALLCLOCK_HPP
#define WALLCLOCK_HPP

#include <cstdint>

namespace SleepLab
{

/// Signature of a wall-clock source: UTC seconds, or <= 0 when unreadable.
using WallClockFn = int64_t (*)();

/**
 * @brief The wall clock, in UTC seconds, or -1 when it cannot be read.
 *
 * Normalised, so callers have one sentinel to test rather than two: the
 * platform returns 0 or a negative from an unset clock depending on where it
 * is asked, and `-1` is what `Engine::Epoch::wallUtc` and the state file both
 * already mean by "unknown".
 */
int64_t wallClockUtc();

/**
 * @brief Replace the wall-clock source. Host tests only.
 *
 * Passing `nullptr` restores `std::time(nullptr)`. Not thread-safe and not
 * meant to be: it is called once, before the service starts.
 */
void setWallClockSource(WallClockFn fn);

} // namespace SleepLab

#endif // WALLCLOCK_HPP
