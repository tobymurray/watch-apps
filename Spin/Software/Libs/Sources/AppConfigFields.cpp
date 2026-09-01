/**
 ******************************************************************************
 * @file    AppConfigFields.cpp
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 */

#include "AppConfigFields.hpp"

namespace SpinConfig
{

using SDK::AppConfig;

// Every value here must match app-manifest.json exactly; CI compares them, and
// the checker reads this file as TEXT -- see AppConfigFields.hpp.
//
// 0 means off for both integer fields rather than a separate toggle: there is
// no useful reading of "auto-lap every 0 minutes".
const AppConfig::Field kFields[] = {
    // Past an hour a lap split stops being a split and the ride is the lap.
    AppConfig::intField("autoLapMinutes", 0, 0, 60),
    // Five hours: longer than any indoor session, short enough that a mistyped
    // value is visibly a mistake.
    AppConfig::intField("targetMinutes", 0, 0, 300),
    // HARDWARE: the panel is reflective, so a lit gym needs no front light --
    // but with hands on the bars the wrist-tilt gesture almost never fires, and
    // a dark room means no readable clock at all. Off costs nothing most rides.
    AppConfig::boolField("keepScreenLit", false),
    // Display only; the file always records kcal.
    AppConfig::boolField("energyInKilojoules", false),
    // The one default that is on: a screen nobody knows to enable is a feature
    // nobody has, and skipping it costs one labelled click.
    AppConfig::boolField("askForKilojoules", true),

    // Five is what the watch itself ships. 0 takes the watch's own count; two,
    // because one zone is not zones; eight, where the kernel's table stops.
    AppConfig::intField("hrZoneCount", 5, 0, 8),

    // Each zone's floor in bpm; the top zone is open-ended, so eight zones need
    // eight floors. 250 is above any human maximum. All 0 spreads them by the
    // watch's own rule -- see ZoneLadder.hpp.
    AppConfig::intField("hrZone1Min", 0, 0, 250),
    AppConfig::intField("hrZone2Min", 0, 0, 250),
    AppConfig::intField("hrZone3Min", 0, 0, 250),
    AppConfig::intField("hrZone4Min", 0, 0, 250),
    AppConfig::intField("hrZone5Min", 0, 0, 250),
    AppConfig::intField("hrZone6Min", 0, 0, 250),
    AppConfig::intField("hrZone7Min", 0, 0, 250),
    AppConfig::intField("hrZone8Min", 0, 0, 250),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

static_assert(sizeof(kFields) / sizeof(kFields[0]) == kIndexCount,
              "the field table and the Index enum have diverged");

} // namespace SpinConfig
