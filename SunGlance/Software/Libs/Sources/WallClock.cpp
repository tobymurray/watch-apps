/**
 ******************************************************************************
 * @file    WallClock.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The one place this app reads the wall clock.
 ******************************************************************************
 */

#include "WallClock.hpp"

#include <ctime>

namespace Sun
{

namespace
{

WallClockFn gSource = nullptr;

} // namespace

int64_t wallClockUtc()
{
    if (gSource != nullptr) {
        return gSource();
    }

    const std::time_t now = std::time(nullptr);
    // Normalised so callers have one sentinel to test rather than two: an
    // unset clock reads back as 0 or as a negative depending on where it is
    // asked, and -1 is what the rest of this app means by "unknown".
    return (now <= 0) ? -1 : static_cast<int64_t>(now);
}

void setWallClockSource(WallClockFn fn)
{
    gSource = fn;
}

} // namespace Sun
