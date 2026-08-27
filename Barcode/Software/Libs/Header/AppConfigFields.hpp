/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @date    26-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
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
 *       --check Barcode/app-manifest.json \
 *       --check-bounds Barcode/Software/Libs/Sources/AppConfigFields.cpp
 *
 * Keep the entries as single-line calls with plain literals, and keep
 * preprocessor conditionals out of the table -- the checker reads that file as
 * text, so a named constant is unreadable to it and an entry inside an `#if`
 * is counted whether or not it compiles. That is also why the table cannot be
 * generated from Barcode::kMaxCodes with a loop or a macro, and why raising
 * the number of codes means adding literal entries by hand.
 *
 * THE TABLE IS ORDERED, AND THE ORDER IS THE CONTRACT: two entries per code,
 * id first and name second, so code `i` is `kFields[2 * i]` and
 * `kFields[2 * i + 1]`. Nothing else needs to know the field names, which is
 * why there is no second list of them to drift out of step.
 *
 ******************************************************************************
 */

#ifndef APPCONFIGFIELDS_HPP
#define APPCONFIGFIELDS_HPP

#include <cstddef>

#include "SDK/AppConfig/AppConfig.hpp"

#include "Barcode.hpp"

namespace BarcodeConfig
{

/// Bare filename of the values file, matching "configFile" in the manifest.
/// Unchanged from the name this app used before the SDK had this feature, so
/// a file already sitting on a watch keeps working: the envelope this app
/// invented and the one SDK::AppConfig reads are the same document.
constexpr char kConfigFile[] = "input.json";

/// Two fields per code: see the ordering note above.
constexpr size_t kFieldsPerCode = 2;

extern const SDK::AppConfig::Field kFields[];
extern const size_t kFieldCount;

/// Field id for code @p index's value, e.g. "id3".
inline const char *idField(size_t index)
{
    return kFields[index * kFieldsPerCode].id;
}

/// Field id for code @p index's name, e.g. "name3".
inline const char *nameField(size_t index)
{
    return kFields[index * kFieldsPerCode + 1].id;
}

} // namespace BarcodeConfig

#endif // APPCONFIGFIELDS_HPP
