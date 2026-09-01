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

// Every value here must match app-manifest.json exactly; CI compares them.
//
// Thirteen settings, and each one is here because it needs no hardware the
// watch has not got. Cadence, power and a trainer link would all need a BLE sensor
// the firmware would have to pair with, so none of them is a setting -- an
// option that cannot work is worse than an absent one.
//
// All of them default to "off" or "as before", so a wearer who never opens the
// form gets exactly the app described in the README: one lap, no alert, and
// the ordinary wrist-tilt backlight.
//
// 0 means off for both integer fields rather than a separate enable toggle:
// there is no useful reading of "auto-lap every 0 minutes" or "buzz at minute
// 0", so the value can carry the switch and the form stays three rows.
const AppConfig::Field kFields[] = {
    // 60 rather than something rounder: past an hour a lap split stops being a
    // split and the ride is the lap, which the ride already records.
    AppConfig::intField("autoLapMinutes", 0, 0, 60),
    // 300 is five hours. Longer than any indoor session, and short enough that
    // a mistyped value is visibly a mistake rather than silently accepted.
    AppConfig::intField("targetMinutes", 0, 0, 300),
    // Off by default. The panel is reflective and a lit gym needs no front
    // light, so this costs battery for nothing most of the time -- but with
    // your hands on the bars the wrist-tilt gesture almost never fires, so
    // riding in the dark without it means no readable clock at all.
    AppConfig::boolField("keepScreenLit", false),
    // Display only. The FIT file always records kcal, because that is the unit
    // the profile's total_calories field is defined in -- this switches what
    // the watch shows, not what it writes.
    AppConfig::boolField("energyInKilojoules", false),

    // ZONES. 0 means "use the watch's own", which is the default and what a
    // wearer who never opens this form gets: their zones are already set once,
    // system-wide, and a second copy is a second thing to keep in step.
    //
    // Setting a count here is for the models the watch cannot express -- a
    // three-zone polarised split, or a seven- or eight-zone ladder -- and it
    // takes the floors with it, because the count alone does not say where the
    // boundaries fall and there is no percentage rule this app could pick that
    // would not be inventing somebody's training model for them.
    //
    // Two, because one zone is not zones. Eight, because that is where the
    // kernel's own threshold table stops.
    AppConfig::intField("hrZoneCount", 0, 0, 8),

    // Each zone's floor, in bpm. Zone N runs from its floor to the next one's,
    // and the top zone is open-ended -- so eight zones need eight floors, not
    // nine. Unused ones are ignored. 250 is above any human maximum.
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
