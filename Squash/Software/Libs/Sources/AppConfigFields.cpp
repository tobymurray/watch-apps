/**
 ******************************************************************************
 * @file    AppConfigFields.cpp
 * @brief   The app's copy of the configuration contract in app-manifest.json.
 ******************************************************************************
 */

#include "AppConfigFields.hpp"

namespace SquashConfig
{

using SDK::AppConfig;

// Every value here must match app-manifest.json exactly; CI compares them.
//
// One setting, and it is off by default, so a wearer who never opens the form
// gets the app the README describes: a squash session, a heart rate, and a FIT
// file. Turning it on is the whole of the research mode, and its only effect is
// to start filling flash -- which is why off is the default and why the wearer
// has to ask for it rather than opt out.
const AppConfig::Field kFields[] = {
    AppConfig::boolField("recordImu", false),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

static_assert(sizeof(kFields) / sizeof(kFields[0]) == kIndexCount,
              "the field table and the Index enum have diverged");

} // namespace SquashConfig
