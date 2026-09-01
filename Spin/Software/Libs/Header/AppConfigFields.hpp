/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 *
 * app-manifest.json never reaches the watch, so the binary carries its own
 * copy of what it declared: one constexpr table giving each field its type,
 * default and bounds. That duplication is the SDK's design, and it is what
 * lets SDK::AppConfig bound a value it should never have received. CI checks
 * the two agree:
 *
 *   python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
 *       --check Spin/app-manifest.json \
 *       --check-bounds Spin/Software/Libs/Sources/AppConfigFields.cpp
 *
 * Keep the entries as single-line calls with plain literals, and keep
 * preprocessor conditionals out of the table -- the checker reads that file as
 * text, so a named constant is unreadable to it and an entry inside an `#if`
 * is counted whether or not it compiles.
 *
 * There is no second list of field-name strings anywhere: the ids live in the
 * table and are reached through `field()` below, so a rename cannot leave a
 * stale copy behind for the compiler to accept.
 *
 ******************************************************************************
 */

#ifndef APPCONFIGFIELDS_HPP
#define APPCONFIGFIELDS_HPP

#include <cstddef>

#include "SDK/AppConfig/AppConfig.hpp"

namespace SpinConfig
{

/// Bare filename of the values file, matching "configFile" in the manifest.
/// The name the SDK's own documentation uses; Spin has no earlier file on any
/// watch to stay compatible with, so there is nothing to preserve by choosing
/// something else.
constexpr char kConfigFile[] = "app_config.json";

/// Index into kFields. The order here is the order in the table and in the
/// manifest, which is also the order the phone renders the form in.
enum Index : size_t {
    kAutoLapMinutes = 0,
    kTargetMinutes,
    kKeepScreenLit,
    kEnergyInKilojoules,
    kHrZoneCount,
    kHrZone1Min,
    kHrZone2Min,
    kHrZone3Min,
    kHrZone4Min,
    kHrZone5Min,
    kHrZone6Min,
    kHrZone7Min,
    kHrZone8Min,
    kIndexCount,
};

/// Most zones this app will draw or record. The kernel's own settings message
/// tops out at eight thresholds too (RequestSystemSettings::skMaxHearRateTh),
/// so this is not a limit invented here.
constexpr size_t kMaxZones = 8;


extern const SDK::AppConfig::Field kFields[];
extern const size_t kFieldCount;

/// The floor of zone @p n, 1-based. Zone n runs from its own floor up to the
/// next zone's, and the top zone is open-ended.
inline const char *zoneMinField(size_t n)
{
    return kFields[kHrZone1Min + (n - 1)].id;
}

/// The declared id of one field, taken from the table rather than repeated.
inline const char *field(Index index)
{
    return kFields[index].id;
}

} // namespace SpinConfig

#endif // APPCONFIGFIELDS_HPP
