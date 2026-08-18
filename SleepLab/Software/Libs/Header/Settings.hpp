/**
 ******************************************************************************
 * @file    Settings.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What the wearer has asked for, from `settings.json` in this folder.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why a file, and why this file
 *
 * There is no supported way for a third-party watch app to receive data from a
 * companion (`Docs/companion-data-channel-analysis.md` on `una-sdk@research`),
 * and there will not be one for this app. The watch has four buttons. So a
 * bedtime window has to arrive from outside, and the only path that works today
 * is the USB mass-storage volume: `Apps/SleepLab/` is writable from any desktop
 * and is the directory this app's relative paths resolve into.
 *
 * `Barcode` is this repository's precedent and this reader keeps its four
 * rules, which come in turn from `SDK::Variant::Config`:
 *
 *   - a `schema` major that must match **exactly**, so an unknown file falls
 *     back entirely rather than guessing at rearranged keys;
 *   - a size ceiling checked **before** anything is allocated -- all five SDK
 *     settings serializers do `new char[file->size()]` with no upper bound;
 *   - an app-owned `values` subtree the reader treats as opaque;
 *   - every failure falls back to a default. A config somebody else wrote must
 *     never stop the app starting, and this app is autostart: a settings file
 *     that could prevent it running would prevent it running *for ever*, with
 *     no screen to say why.
 *
 * ---------------------------------------------------------------------------
 * Unlike Barcode, this one IS called settings.json
 *
 * Barcode deliberately avoided the name, for three reasons. Two of them do not
 * apply here and the third is answered:
 *
 *   - *"The app rewrites it whole."* SleepLab never writes this file. The
 *     watch-side settings screen is read-only; anything it could change would
 *     be destroyed the next time the file was edited over USB, and having two
 *     writers to one file is how a setting silently reverts.
 *   - *"Keeping externally-written data elsewhere makes validation a property
 *     of the filename."* It is a property of this class instead, which refuses
 *     every out-of-range value rather than clamping it.
 *   - *"The name is plausibly spoken for"* -- UNA's phone app was observed
 *     reading a `settings.json` off the watch during a sync. That is the real
 *     risk, and it is accepted: a reader that finds keys it does not recognise
 *     ignores them, this app never writes the file back, and the worst case is
 *     that something else reads a file it does not understand. In exchange the
 *     file is where a person looking for settings would look first.
 *
 * ---------------------------------------------------------------------------
 * Out of range is refused, never clamped
 *
 * `"epoch_bedtime": 2500` is a typo, and clamping it to 23:59 would record a
 * night the wearer did not ask for while looking perfectly healthy. Every
 * rejection is logged with the value and the range, and the default stands.
 *
 ******************************************************************************
 */

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Engine/NightSegmenter.hpp"

namespace SleepLab
{

/// Schema major this build parses. Must match the file exactly.
constexpr uint32_t kSettingsSchema = 1;

/// Refused before a byte is allocated.
constexpr size_t kSettingsMaxBytes = 4096;

/// Relative to the app's sandbox -- `Apps/SleepLab/` on the USB volume.
constexpr char kSettingsPath[] = "settings.json";

/**
 * @brief How the optical heart-rate sensor is driven overnight.
 *
 * Configurable because it is the largest overnight power cost and Tier 0
 * exists partly to find out how large. If continuous HR turns out to cost more
 * than about a third of the battery, `Duty` becomes the default -- and the
 * mode has to be recorded per night either way, because a night's heart-rate
 * coverage is not comparable across modes.
 */
enum class HrMode : uint8_t {
    Continuous = 0,
    Off        = 1,
    Duty       = 2,
};

const char *toString(HrMode mode);

/**
 * @brief Everything the wearer can set, already validated.
 */
struct Settings
{
    // -- When a night is ------------------------------------------------------

    /// Passed straight to the segmenter. Defaults 21:00-11:00, which crosses
    /// midnight -- the normal case, and the one that is easy to get wrong.
    Engine::SegmenterConfig segmenter {};

    // -- Heart rate -----------------------------------------------------------

    HrMode   hrMode       = HrMode::Continuous;
    uint16_t hrDutyOnSec  = 60;
    uint16_t hrDutyPerSec = 300;

    // -- Smart alarm ----------------------------------------------------------

    /// Off by default. An alarm is the one thing this app does that can go
    /// wrong loudly, at 06:00, for somebody asleep -- so it is opt-in, and the
    /// README says to test it on a weekend before trusting it.
    bool     alarmEnabled = false;

    /// Hard deadline, local minutes past midnight. The alarm fires here
    /// whatever the scorer thinks.
    int16_t  alarmDeadlineMin = 7 * 60;

    /// How long before the deadline the smart window opens, in minutes.
    ///
    /// Thirty. Inside it, the alarm fires at the first epoch scored anything
    /// other than sleep; outside it, only the deadline fires. Longer than
    /// about half an hour and "woke you at the best moment" becomes "woke you
    /// early", which is the same complaint by a different name.
    uint16_t alarmWindowMin = 30;

    // -- Research recording ---------------------------------------------------

    /// Stream raw accelerometer samples to CSV as well as epochs.
    ///
    /// Off by default and capped hard. Epochs cost ~46 KB a night; raw at 25 Hz
    /// costs about **31 MB for eight hours**, scaling Squash's measured
    /// ~4.3 KiB/s at 100 Hz. Nobody wants that by accident.
    bool     rawRecording = false;

    /// Cap on the raw recording, megabytes. Self-defence, not device
    /// awareness: the SDK exposes no free-space query.
    uint16_t rawMaxMb     = 64;

    /// Cap on the raw recording, minutes.
    uint16_t rawMaxMin    = 120;
};

/// Why the file is, or is not, being used.
enum class SettingsStatus : uint8_t {
    Absent,      ///< No file. All defaults, which are usable.
    TooLarge,    ///< Over kSettingsMaxBytes; refused before allocating.
    Unreadable,  ///< Present, but the read failed.
    NotJson,     ///< Empty, or coreJSON rejected it.
    WrongSchema, ///< No `schema`, or a major this build does not parse.
    Ok,          ///< Parsed. Unknown keys keep their defaults.
};

const char *toString(SettingsStatus status);

/**
 * @brief Read `settings.json`.
 *
 * @param out  Receives the settings. Left at defaults on any failure, so the
 *             caller never has to check the status before using it.
 * @return     Why the file was or was not used. Worth surfacing on screen: on
 *             a watch with no keyboard, "the file you wrote is malformed" is
 *             not discoverable any other way.
 */
SettingsStatus loadSettings(const SDK::Kernel &kernel, Settings &out,
                            const char *path = kSettingsPath);

} // namespace SleepLab

#endif // SETTINGS_HPP
