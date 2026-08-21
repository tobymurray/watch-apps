/**
 ******************************************************************************
 * @file    Settings.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What the operator asked for, from settings.json in this folder.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why a file
 *
 * There is no supported way for a third-party watch app to receive data from a
 * companion (`Docs/companion-data-channel-analysis.md` on `una-sdk@research`),
 * and the watch has four buttons. So anything the operator has to type arrives
 * over the USB mass-storage volume, where `Apps/SensorLab/` is writable from any
 * desktop. `Barcode` is this repository's precedent and this reader keeps its
 * four rules, which come in turn from `SDK::Variant::Config`:
 *
 *   - a `schema` major that must match **exactly**, so an unknown file falls
 *     back entirely rather than guessing at rearranged keys;
 *   - a size ceiling checked **before** anything is allocated -- all five SDK
 *     settings serializers do `new char[file->size()]` with no upper bound;
 *   - an app-owned `values` subtree the reader treats as opaque;
 *   - every failure falls back to a documented default, and logs that it did.
 *
 * Out of range is **refused, never clamped**. `"soak_minutes": 100000` is a
 * typo; clamping it to a day would run a soak nobody asked for while looking
 * perfectly healthy. Every rejection carries the value and the range into the
 * log, because on a watch with no keyboard that log line is the only way to find
 * out the file was wrong.
 *
 * ---------------------------------------------------------------------------
 * The one setting that is not a preference
 *
 * `firmware` is a fallback for the profile's primary key, used only when the
 * kernel does not answer `RequestSystemInfo`. It is recorded in the manifest as
 * *declared* rather than *read*, and the report says which it has. A declared
 * version is better than none -- a profile with no version cannot be diffed at
 * all -- but presenting one as read would put a human's memory into a field
 * every subsequent comparison keys on.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_SETTINGS_HPP
#define SENSORLAB_SETTINGS_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace SensorLab
{

/// Schema major this build parses. Must match the file exactly.
constexpr uint32_t kSettingsSchema = 1;

/// Refused before a byte is allocated.
constexpr size_t kSettingsMaxBytes = 4096;

/// Relative to the app's sandbox -- `Apps/SensorLab/` on the USB volume.
constexpr char kSettingsPath[] = "settings.json";

enum class SettingsStatus : uint8_t
{
    Ok = 0,
    Absent,
    TooLarge,
    Unreadable,
    NotJson,
    WrongSchema,
};

const char *toString(SettingsStatus s);

/// Longest declared firmware string, plus a terminator.
constexpr size_t kDeclaredVersionMax = 17;

struct Settings
{
    // -- What a soak asks the sensors for ------------------------------------
    //
    // Defaults chosen to make the *first* soak comparable with the only
    // hardware measurements that exist. Ledger row S3 measured ~48 Hz delivered
    // against a requested 40 ms period, and row S17 measured 195 ms batches
    // against a requested 5000 ms latency. Asking for the same thing again is
    // what makes the second measurement a check on the first rather than a new
    // and incomparable one.

    /// Requested sample period, milliseconds. 0 asks the driver for its default.
    uint32_t periodMs  = 40;
    /// Requested batch latency, milliseconds.
    uint32_t latencyMs = 5000;

    /// How often a soak writes an interval row per subscribed sensor.
    ///
    /// One minute, following the Sleep Probe: short enough to localise where
    /// delivery changed, long enough that a night is a file rather than a
    /// filesystem. At a dozen subscribed types this is a dozen `S` rows and up
    /// to seventy-six `V` rows a minute -- roughly 12 KB, against the probe's
    /// measured 74 KiB for a whole night at one row a minute (ledger row S9).
    uint32_t intervalSec = 60;

    /// Stop a soak after this many minutes. 0 means run until stopped.
    ///
    /// Not unbounded by default: an unattended run that is still going three
    /// days later has stopped being a measurement of anything and has started
    /// being a battery drain. TODO: raise once a soak has been measured end to
    /// end; twelve hours is chosen to cover one overnight run with margin.
    uint32_t soakMaxMinutes = 720;

    /// Cap the run log by bytes as well as by duration. Both, because the two
    /// failure modes are different: a long run fills the volume slowly and a
    /// misconfigured interval fills it fast.
    uint32_t soakMaxKb = 8192;

    // -- Which types a soak subscribes ---------------------------------------

    /// Subscribe every type that resolved a driver.
    ///
    /// **This is the honest default and it is also the most invasive one.**
    /// Measuring a sensor changes it: subscribing costs power and IPC, and a dt
    /// distribution taken while eleven other streams are running is not the same
    /// measurement as one taken alone. Both are worth having -- the manifest
    /// records which -- but a first profile wants coverage, so the default is
    /// everything and the way to get an uncontended measurement is to name one
    /// type.
    bool subscribeAll = true;

    /// When `subscribeAll` is false, the single type value to subscribe, e.g.
    /// 0x10. Zero means "none", which is a valid and occasionally useful run:
    /// it measures what the service costs with no sensors at all.
    uint32_t onlyType = 0;

    // -- Raw capture ----------------------------------------------------------

    /// Keep every sample, as the wire carried it, in `raw/<run>-<seq>.bin`.
    ///
    /// **On by default**, because every other file this app writes is derived
    /// and a statistic embeds the question it was computed to answer. On a first
    /// profile of an undocumented platform that question will be wrong, and an
    /// analysis without its inputs cannot be corrected.
    ///
    /// It is not free of the thing it measures: at the ~10 MB/h a dozen
    /// subscribed types produce, capture costs flash writes and power, so a dt
    /// distribution measured with it on is not the same measurement as one taken
    /// with it off. The manifest records which, and turning it off is a
    /// legitimate experiment rather than a degraded mode. See
    /// `Profile/RawLog.hpp`.
    bool rawCapture = true;

    /// Total raw bytes for one run, megabytes. Capture stops at the cap and the
    /// manifest records how many batches were dropped after it -- a capture that
    /// silently stopped would leave a file that still looked complete.
    ///
    /// 256 MB: a twelve-hour soak at ~10 MB/h is 70-120 MB, so this holds one
    /// with margin. `MapManager` CRC-verified 160.5 MiB of map packs on this
    /// volume, which is the right order of magnitude to fit and the wrong order
    /// to be casual about.
    uint32_t rawMaxMb = 256;

    /// Rotate to a new chunk every this many kilobytes. Following `FwDump`: a
    /// chunk interrupted by the cable loses itself and not the run, and a host
    /// can decode chunk 3 without chunk 4 ever having been written.
    uint32_t rawChunkKb = 512;

    // -- Layer 5 --------------------------------------------------------------

    /// Accumulate per-field statistics. On by default; the cost is a handful of
    /// adds and compares per field per sample, and turning it off is how a
    /// timing-only run avoids that cost being part of what it measures.
    bool fieldStats = true;

    // -- The primary key's fallback ------------------------------------------

    /// Used only when the kernel does not answer `RequestSystemInfo`.
    char declaredFirmware[kDeclaredVersionMax] = "";

    // -- Tier 5 ---------------------------------------------------------------

    /// Read sensor configuration registers directly over I2C.
    ///
    /// **Off by default and read-only for ever.** Apps on this device run with
    /// no isolation -- MPU disabled, CPU privileged, TrustZone off -- so this is
    /// possible, and `FwDump` already demonstrates it. It would turn the
    /// accelerometer's configured range and ODR from a layer 5 inference into a
    /// CONFIRMED fact. It would also, if it ever wrote, reconfigure a sensor
    /// under the kernel's own driver, and the failure would look like a sensor
    /// fault rather than like this app. **No write path exists in this
    /// codebase**, which is a stronger guarantee than a flag.
    ///
    /// Not implemented in this build: the flag is parsed and recorded in the
    /// manifest so a profile can say the tier was off, and the LIKELY inference
    /// from layer 5 is what the profile carries meanwhile.
    bool readRegisters = false;
};

/// Read `settings.json`, or leave @p out at its documented defaults.
SettingsStatus loadSettings(const SDK::Kernel &kernel, Settings &out,
                            const char *path = kSettingsPath);

} // namespace SensorLab

#endif // SENSORLAB_SETTINGS_HPP
