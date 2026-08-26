/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @date    26-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 *
 * app-manifest.json never reaches the watch, so the binary carries its own
 * copy of what it declared: one constexpr table giving the id its type,
 * default and bounds. That duplication is the SDK's design, and it is what
 * lets SDK::AppConfig bound a value it should never have received. CI checks
 * the two agree:
 *
 *   python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
 *       --check Barcode/app-manifest.json \
 *       --check-bounds Barcode/Software/Libs/Sources/AppConfigFields.cpp
 *
 * Keep the entries as single-line calls with plain literals, and keep
 * preprocessor conditionals out of the table -- the checker reads this file
 * as text, so a named constant is unreadable to it and an entry inside an
 * `#if` is counted whether or not it compiles.
 *
 ******************************************************************************
 */

#ifndef APPCONFIGFIELDS_HPP
#define APPCONFIGFIELDS_HPP

#include <cstddef>

#include "SDK/AppConfig/AppConfig.hpp"

namespace BarcodeConfig
{

/// Bare filename of the values file, matching "configFile" in the manifest.
/// Unchanged from the name this app used before the SDK had this feature, so
/// a file already sitting on a watch keeps working: the envelope this app
/// invented and the one SDK::AppConfig reads are the same document.
constexpr char kConfigFile[] = "input.json";

/// The single declared field. See Barcode.hpp for why its declared maximum is
/// one byte longer than the longest id this app will actually draw.
extern const SDK::AppConfig::Field kFields[];
extern const size_t kFieldCount;

} // namespace BarcodeConfig

#endif // APPCONFIGFIELDS_HPP
