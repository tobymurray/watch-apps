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
// Three entries per code, id then name then format, in the order the phone
// shows them. Each defaults to the empty string, which this app reads as "not
// set" -- see Barcode.hpp for why an empty default is possible here and a
// sentinel is not needed. The ids are declared one byte longer than an id can
// be, so an over-length value arrives detectably rather than truncated into a
// valid one.
//
// The format fields are the only ones with a non-empty default, and that is
// deliberate twice over. The phone renders a string field as a plain text box,
// so a pre-filled "Code128" is the only thing on the form that tells a wearer
// what the vocabulary is -- an empty box would need them to read the
// description to learn that "QRCode" is the other word. And SDK::AppConfig
// hands back the declared default for a key that is not in the file, so an
// input.json written before this field existed reads as Code128 and keeps
// meaning exactly what it meant. Barcode::parseFormat() also treats an empty
// value as Code128, for a file that was hand-edited to clear it; there are
// tests for both.
//
// TO ADD A CODE: raise Barcode::kMaxCodes, add an "idN"/"nameN"/"fmtN" triple
// here and the matching triple to app-manifest.json. The static_asserts below
// and in Commands.hpp fail the build if the three ever disagree.
//
// boostBacklight is last and alone: one setting for every code rather than a
// fourth per-code field, so it is not part of the kMaxCodes * kFieldsPerCode
// count below and gets its own +1.
const AppConfig::Field kFields[] = {
    AppConfig::stringField("id1", "", 0, 23),
    AppConfig::stringField("name1", "", 0, 12),
    AppConfig::stringField("fmt1", "Code128", 0, 8),
    AppConfig::stringField("id2", "", 0, 23),
    AppConfig::stringField("name2", "", 0, 12),
    AppConfig::stringField("fmt2", "Code128", 0, 8),
    AppConfig::stringField("id3", "", 0, 23),
    AppConfig::stringField("name3", "", 0, 12),
    AppConfig::stringField("fmt3", "Code128", 0, 8),
    AppConfig::stringField("id4", "", 0, 23),
    AppConfig::stringField("name4", "", 0, 12),
    AppConfig::stringField("fmt4", "Code128", 0, 8),
    AppConfig::stringField("id5", "", 0, 23),
    AppConfig::stringField("name5", "", 0, 12),
    AppConfig::stringField("fmt5", "Code128", 0, 8),
    AppConfig::stringField("id6", "", 0, 23),
    AppConfig::stringField("name6", "", 0, 12),
    AppConfig::stringField("fmt6", "Code128", 0, 8),
    AppConfig::boolField("boostBacklight", true),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

// The lengths above are spelled as literals because CI reads this file as
// text: validate_app_config.py --check-bounds refuses a named constant with
// "use plain literals (not named constants or expressions) so CI can compare
// them", since it has no compiler to fold one with. That leaves the table and
// the header free to drift, so these say what the literals were copied from
// and fail the build instead.
static_assert(Barcode::kConfigMaxLength == 23,
              "the idN maxLength above no longer matches Barcode::kConfigMaxLength "
              "-- update all six, and app-manifest.json with them");
static_assert(Barcode::kMaxNameLength == 12,
              "the nameN maxLength above no longer matches Barcode::kMaxNameLength "
              "-- update all six, and app-manifest.json with them");
static_assert(Barcode::kConfigFormatMaxLength == 8,
              "the fmtN maxLength above no longer matches "
              "Barcode::kConfigFormatMaxLength -- update all six, and "
              "app-manifest.json with them");

static_assert(sizeof(kFields) / sizeof(kFields[0])
                  == Barcode::kMaxCodes * kFieldsPerCode + 1,
              "the field table must hold one id, one name and one format per "
              "code plus the single boostBacklight setting -- raise or lower "
              "it to match Barcode::kMaxCodes");

} // namespace BarcodeConfig
