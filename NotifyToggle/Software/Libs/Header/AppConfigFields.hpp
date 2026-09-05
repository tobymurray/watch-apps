/**
 ******************************************************************************
 * @file    AppConfigFields.hpp
 * @brief   The one setting this app declares, and the file it is read from.
 ******************************************************************************
 *
 * The same field is declared in `app-manifest.json`, and nothing checks that
 * the two agree on id, type and default.
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
