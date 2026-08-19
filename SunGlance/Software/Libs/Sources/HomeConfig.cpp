/**
 ******************************************************************************
 * @file    HomeConfig.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The home position, as written into the app's folder from a desktop.
 ******************************************************************************
 */

#include "HomeConfig.hpp"

#define LOG_MODULE_PRX      "HomeConfig"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace Sun
{

namespace
{

/// Longer than any coordinate `parseDegrees` will accept, so a value that is
/// too long is refused by the parser -- which knows why -- rather than being
/// silently truncated by the copy out of the document.
constexpr size_t kValueBytes = 32;

} // namespace

HomeConfig::HomeConfig(const SDK::Kernel &kernel, const char *path)
    : mReader(kernel, path)
{
}

bool HomeConfig::refresh()
{
    const bool changed = mReader.refresh();
    if (!changed && mSeen) {
        return false;
    }

    mSeen = true;
    reload();
    return true;
}

void HomeConfig::reload()
{
    mFix = Fix {};

    switch (mReader.status()) {
        case InputConfig::Status::Absent:
            mStatus = Status::Absent;
            return;
        case InputConfig::Status::Ok:
            break;
        default:
            // Too large, unreadable, not JSON, or a schema major this build
            // does not parse. All of them mean the same thing to the person
            // holding the watch: there is a file and it is not working.
            LOG_WARNING("input.json unusable (status %u)\n",
                        static_cast<unsigned>(mReader.status()));
            mStatus = Status::Rejected;
            return;
    }

    char latText[kValueBytes] = { 0 };
    char lonText[kValueBytes] = { 0 };

    double lat = 0.0;
    double lon = 0.0;

    if (!mReader.getString(kLatQuery, latText, sizeof latText)
        || !mReader.getString(kLonQuery, lonText, sizeof lonText)
        || !parseDegrees(latText, 90.0, lat)
        || !parseDegrees(lonText, 180.0, lon)) {
        // A valid document that does not carry a usable coordinate. Rejected
        // rather than absent: the file is there, so "not set yet" would send
        // its author looking for something they already did.
        LOG_WARNING("no usable %s / %s in input.json\n", kLatQuery, kLonQuery);
        mStatus = Status::Rejected;
        return;
    }

    mFix.source = Fix::Source::Config;
    mFix.latDeg = lat;
    mFix.lonDeg = lon;
    // Timeless, not stale: a home somebody typed in does not get less true
    // overnight, and dating it "now" would make an age display lie later.
    mFix.utc    = -1;

    mStatus = Status::Ok;
    LOG_INFO("home %d.%03d, %d.%03d\n",
             static_cast<int>(lat), static_cast<int>((lat < 0 ? -lat : lat) * 1000) % 1000,
             static_cast<int>(lon), static_cast<int>((lon < 0 ? -lon : lon) * 1000) % 1000);
}

} // namespace Sun
