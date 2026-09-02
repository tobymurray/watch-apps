/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 *
 * app-manifest.json never reaches the watch, so the binary carries its own copy
 * of what it declared. CI checks the two agree:
 *
 *   python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
 *       --check Spin/app-manifest.json \
 *       --check-bounds Spin/Software/Libs/Sources/AppConfigFields.cpp
 *
 * THAT CHECKER READS AppConfigFields.cpp AS TEXT, so every entry must stay a
 * single-line call with plain literals: a named constant is unreadable to it,
 * and an entry inside an `#if` is counted whether or not it compiles.
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
constexpr char kConfigFile[] = "app_config.json";

/// Index into kFields, in the order the manifest declares and the phone
/// renders.
enum Index : size_t {
    kAutoLapMinutes = 0,
    kTargetMinutes,
    kKeepScreenLit,
    kEnergyInKilojoules,
    kAskForKilojoules,
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

/// Matches the ceiling of the kernel's own threshold table,
/// RequestSystemSettings::skMaxHearRateTh.
constexpr size_t kMaxZones = 8;


extern const SDK::AppConfig::Field kFields[];
extern const size_t kFieldCount;

/// The floor of zone @p n, 1-based.
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
