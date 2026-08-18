/**
 ******************************************************************************
 * @file    WallClock.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The one place this app reads the wall clock. Spec in the header.
 ******************************************************************************
 */

#include "WallClock.hpp"

#include <ctime>

namespace SleepLab
{
namespace {

int64_t systemClock()
{
    const std::time_t t = std::time(nullptr);
    return (t > 0) ? static_cast<int64_t>(t) : -1;
}

WallClockFn gSource = &systemClock;

} // namespace

int64_t wallClockUtc()
{
    const int64_t t = gSource();
    return (t > 0) ? t : -1;
}

void setWallClockSource(WallClockFn fn)
{
    gSource = (fn != nullptr) ? fn : &systemClock;
}

} // namespace SleepLab
