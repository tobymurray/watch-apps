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
// The default is a single space, and it is not an id -- see Barcode.hpp.
const AppConfig::Field kFields[] = {
    AppConfig::stringField("id", " ", 1, 17),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

} // namespace BarcodeConfig
