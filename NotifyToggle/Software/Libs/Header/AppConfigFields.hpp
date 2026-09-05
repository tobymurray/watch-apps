/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   The one setting this app declares, and the file it is read from.
 ******************************************************************************
 *
 * Declared in `app-manifest.json` as well; the companion app reads that copy to
 * build the prompt, and this copy is what the watch reads back. Both must agree
 * on the id, the type and the default, and nothing checks that for you.
 ******************************************************************************
 */

#ifndef APP_CONFIG_FIELDS_HPP
#define APP_CONFIG_FIELDS_HPP

#include "SDK/AppConfig/AppConfig.hpp"

namespace NotifyToggleConfig
{

constexpr const char *kConfigFile = "notify_config.json";

/// Off by default, and deliberately: with it off the app never writes anything
/// to the watch's filesystem, so the whole commit path -- the only part that
/// can leave a wearer without a settings file -- does not run at all. Turning
/// it on is a decision made in the companion app, where there is room to say
/// what it costs.
constexpr const char *kSaveToSettings = "saveToSettings";

constexpr SDK::AppConfig::Field kFields[] = {
    SDK::AppConfig::boolField(kSaveToSettings, false),
};

constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

} // namespace NotifyToggleConfig

#endif // APP_CONFIG_FIELDS_HPP
