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
// recordImu is off by default: its only effect is to start filling flash, so
// the wearer has to ask for it rather than opt out.
//
// The two caps are settings rather than constants because the right value is
// the session the wearer is about to record, which the binary cannot know. The
// bounds are what the recorder can honour, not what is sensible: at ~4.3 KiB/s
// the 240-minute ceiling costs ~62 MB, so whichever cap is lower is the one
// that stops the run.
const AppConfig::Field kFields[] = {
    AppConfig::boolField("recordImu", false),
    AppConfig::intField("maxMinutes", 90, 1, 240),
    AppConfig::intField("maxMegabytes", 32, 1, 256),
};

const size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

static_assert(sizeof(kFields) / sizeof(kFields[0]) == kIndexCount,
              "the field table and the Index enum have diverged");

} // namespace SquashConfig
