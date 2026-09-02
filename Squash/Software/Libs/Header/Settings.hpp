/**
 ******************************************************************************
 * @file    Settings.hpp
 * @date    08-04-2025
 * @author  Denys Saienko <denys.saienko@droid-technologies.com>
 * @brief   Application settings structure and alert sub-types.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <cstdint>

/**
 * @brief Application settings persisted to storage.
 */
struct Settings {
    /// Settings version.
    static constexpr uint8_t kVersion = 1;

    // Fields
    uint32_t version = kVersion;    ///< Settings version.

    bool     phoneNotifEn  = true;  ///< Flag to enable receiving phone notification when app is run.

    // Raw-IMU research recording is deliberately NOT a field here. It is
    // decided from outside the watch, and this file is rewritten whole on every
    // save, so a key the app did not put here does not survive the next
    // settings change. It is a declared config field now -- see
    // AppConfigFields.hpp.
};

#endif // SETTINGS_HPP
