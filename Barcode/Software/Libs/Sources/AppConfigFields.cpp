/**
 ******************************************************************************
 * @file    AppConfigFields.cpp
 * @date    26-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 */

#include "AppConfigFields.hpp"

namespace BarcodeConfig
{

using SDK::AppConfig;

// Every value here must match app-manifest.json exactly; CI compares them.
//
// Two entries per code, id then name, in the order the phone shows them. Each
// defaults to the empty string, which this app reads as "not set" -- see
// Barcode.hpp for why an empty default is possible here and a sentinel is not
// needed. The ids are declared one byte longer than an id can be, so an
// over-length value arrives detectably rather than truncated into a valid one.
//
// TO ADD A CODE: raise Barcode::kMaxCodes, add an "idN"/"nameN" pair here and
// the matching pair to app-manifest.json. The static_asserts below and in
// Commands.hpp fail the build if the three ever disagree.
const AppConfig::Field kFields[] = {
    AppConfig::stringField("id1", "", 0, 17),
    AppConfig::stringField("name1", "", 0, 12),
    AppConfig::stringField("id2", "", 0, 17),
    AppConfig::stringField("name2", "", 0, 12),
    AppConfig::stringField("id3", "", 0, 17),
    AppConfig::stringField("name3", "", 0, 12),
    AppConfig::stringField("id4", "", 0, 17),
    AppConfig::stringField("name4", "", 0, 12),
    AppConfig::stringField("id5", "", 0, 17),
    AppConfig::stringField("name5", "", 0, 12),
    AppConfig::stringField("id6", "", 0, 17),
    AppConfig::stringField("name6", "", 0, 12),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

static_assert(sizeof(kFields) / sizeof(kFields[0])
                  == Barcode::kMaxCodes * kFieldsPerCode,
              "the field table must hold one id and one name per code -- "
              "raise or lower it to match Barcode::kMaxCodes");

} // namespace BarcodeConfig
