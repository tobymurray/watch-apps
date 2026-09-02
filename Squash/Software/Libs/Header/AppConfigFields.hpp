/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 *
 * app-manifest.json never reaches the watch, so the binary carries its own
 * copy of what it declared: one constexpr table giving each field its type and
 * default. That duplication is the SDK's design, and it is what lets
 * SDK::AppConfig bound a value it should never have received. CI checks the
 * two agree:
 *
 *   python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
 *       --check Squash/app-manifest.json \
 *       --check-bounds Squash/Software/Libs/Sources/AppConfigFields.cpp
 *
 * Keep the entries as single-line calls with plain literals, and keep
 * preprocessor conditionals out of the table -- the checker reads that file as
 * text, so a named constant is unreadable to it and an entry inside an `#if`
 * is counted whether or not it compiles.
 *
 ******************************************************************************
 */

#ifndef APPCONFIGFIELDS_HPP
#define APPCONFIGFIELDS_HPP

#include <cstddef>

#include "SDK/AppConfig/AppConfig.hpp"

namespace SquashConfig
{

/// Bare filename of the values file, matching "configFile" in the manifest.
constexpr char kConfigFile[] = "input.json";

/// Index into kFields, in the order the table and the manifest declare them,
/// which is also the order the phone renders the form in.
enum Index : size_t {
    kRecordImu = 0,
    kIndexCount,
};

extern const SDK::AppConfig::Field kFields[];
extern const size_t kFieldCount;

/// The declared id of one field, taken from the table rather than repeated.
inline const char *field(Index index)
{
    return kFields[index].id;
}

} // namespace SquashConfig

#endif // APPCONFIGFIELDS_HPP
