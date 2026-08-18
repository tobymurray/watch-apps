/**
 ******************************************************************************
 * @file    ProbeConfig.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What this run of the probe measures, from `probe.json`.
 ******************************************************************************
 *
 * The probe has no screen and no buttons, so the only way to say "tonight,
 * measure the no-heart-rate case" is a file written over USB into the app's
 * own folder. That is the same answer Barcode, Squash and FwDump each reached
 * for the same reason -- there is no supported companion channel to a
 * third-party watch app (`Docs/companion-data-channel-analysis.md` on
 * `una-sdk@research`), and there will not be one for this app.
 *
 * The reader is a narrowed sibling of `Barcode/Software/Libs/Header/
 * InputConfig.hpp` and keeps its four rules, which come in turn from
 * SDK::Variant::Config:
 *
 *   - a `schema` major that must match exactly, so an unknown file falls back
 *     entirely rather than guessing at rearranged keys;
 *   - a size ceiling checked *before* anything is allocated;
 *   - an app-owned `values` subtree the reader treats as opaque;
 *   - every failure falls back to a default, because a config somebody else
 *     wrote must never stop the probe starting. A night that records the
 *     wrong mode is recoverable -- the mode is stamped in the log's `R` row.
 *     A night that records nothing is not.
 *
 * Not shared with SleepLab's own settings reader on purpose. These are two
 * independently deployable binaries, and a diagnostic that cannot be built
 * and flashed on its own is a diagnostic you will not run.
 *
 ******************************************************************************
 */

#ifndef PROBECONFIG_HPP
#define PROBECONFIG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace Probe
{

/// Bumped when a key changes meaning. Must match the file exactly.
constexpr uint32_t kConfigSchema = 1;

/// Refused before a byte is allocated. A config of a few lines is tens of
/// bytes; 4 KB is already generous, and the SDK's own settings serializers
/// `new char[file->size()]` with no ceiling at all.
constexpr size_t kConfigMaxBytes = 4096;

/// Relative to the app's sandbox -- `Apps/SleepProbe/` on the USB volume.
constexpr char kConfigPath[] = "probe.json";

/**
 * @brief How the optical heart-rate sensor is driven for the night.
 *
 * The whole reason this is configurable is Tier 0's central power question:
 * what does continuous overnight HR actually cost? That needs a night with it
 * and a night without, measured the same way, and `Duty` exists because the
 * answer may well be "too much" -- in which case periodic sampling is the
 * design, and it is better to have measured it than to discover it later.
 */
enum class HrMode : uint8_t {
    Continuous, ///< Subscribed for the whole run. The expensive case.
    Off,        ///< Never subscribed. The baseline the other two are read against.
    Duty,       ///< Subscribed for onSec out of every periodSec.
};

/// Name for the `R` log row and the debug log, so a night's data carries the
/// setting that produced it rather than relying on a note somewhere.
const char *toString(HrMode mode);

/**
 * @brief The settings, already validated, with every default applied.
 *
 * Defaults are the *most informative* run rather than the cheapest: a first
 * night with everything subscribed answers the most questions, and the
 * cheaper variants are then run deliberately against it.
 */
struct Config
{
    HrMode   hrMode      = HrMode::Continuous;
    uint16_t hrDutyOnSec = 60;   ///< Only read in HrMode::Duty.
    uint16_t hrDutyPerSec = 300; ///< Only read in HrMode::Duty. Clamped > onSec.

    /// Requested accelerometer period, milliseconds. 40 ms nominal (25 Hz) is
    /// the low end of what published actigraphy count derivations assume, and
    /// well inside the 0.25-3 Hz band those counts are filtered to. It is a
    /// *request*: the delivered rate is what the log measures, and the two are
    /// not the same number (see ProbeLog.hpp).
    uint16_t accelPeriodMs = 40;

    /// Requested batch latency, milliseconds. Batching is how a sensor stream
    /// stops being one IPC wake per sample; 5 s is a deliberate guess whose
    /// cost this probe is measuring. TODO: set from the measured wakes/minute
    /// and battery columns of the first two nights.
    uint16_t accelLatencyMs = 5000;

    /// Subscribe PPG (0xF0) and HEART_BEAT (0x40) as well. Off by default:
    /// PPG at 20 Hz all night is a lot of IPC to answer a question that a
    /// short run answers just as well, and the beat count is carried in every
    /// row regardless of this flag -- HEART_BEAT is subscribed always,
    /// precisely because the expected answer is "nothing at all" and that
    /// costs nothing to confirm.
    bool     ppgEnabled  = false;

    /// Subscribe SPO2 (0xF1). On by default: the question is whether firmware
    /// produces a single sample, and one night settles it.
    bool     spo2Enabled = true;
};

/// Why the file is, or is not, being used. Logged at start so a night run with
/// an unusable config says so in its own record.
enum class Status : uint8_t {
    Absent,      ///< No file. All defaults.
    TooLarge,    ///< Over kConfigMaxBytes; refused before allocating.
    Unreadable,  ///< Present, but the read failed.
    NotJson,     ///< Empty, or coreJSON rejected it.
    WrongSchema, ///< No `schema`, or a major this build does not parse.
    Ok,          ///< Parsed. Unknown keys keep their defaults.
};

const char *toString(Status status);

/**
 * @brief Read `probe.json` once, at start.
 *
 * Unlike Barcode's reader there is no refresh path: the probe runs one night
 * per launch and changing the mode mid-night would make the night's data mean
 * two different things. Re-flashing or a relaunch picks up a new file.
 *
 * @param kernel  Kernel, for the filesystem.
 * @param out     Receives the config. Left at its defaults on any failure, so
 *                the caller never has to check the status before using it.
 * @param path    Overridable for tests.
 * @return        Why the file was or was not used.
 */
Status load(const SDK::Kernel &kernel, Config &out, const char *path = kConfigPath);

} // namespace Probe

#endif // PROBECONFIG_HPP
