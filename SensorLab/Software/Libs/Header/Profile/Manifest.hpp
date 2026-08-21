/**
 ******************************************************************************
 * @file    Manifest.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The run manifest: the primary key of the whole exercise.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Not boilerplate
 *
 * A profile whose firmware version is unknown cannot be diffed, and a profile
 * that cannot be diffed answers none of the four questions this app exists for.
 * The manifest is therefore not metadata attached to the measurements -- the
 * measurements are attached to *it*.
 *
 * `profile.json` is named with the firmware version for the same reason. One
 * file per firmware, so `profile_diff.py` has two things to compare and neither
 * of them is a moving target.
 *
 * ---------------------------------------------------------------------------
 * Where the firmware version comes from
 *
 * `SDK::Message::RequestSystemInfo` (`Messages/CommandMessages.hpp:238`)
 * returns `firmwareVersion[16]` and `hardwareVersion[16]`. **Nothing in the SDK
 * uses it**, and the only app in either repository that does is `FwDump`, whose
 * `DeviceContext.cpp` reads it so the firmware version does not have to be
 * recovered with `strings` afterwards.
 *
 * That matters here more than it did there. SleepLab's ledger row P1 -- "the
 * watch runs the 1.4 firmware line" -- is LIKELY rather than CONFIRMED because
 * it was *reported by the device's owner* rather than read from the device. This
 * request is what makes it CONFIRMED, and it makes the primary key of every
 * profile a thing the kernel said rather than a thing somebody typed.
 *
 * A kernel that does not implement the request leaves `haveSystemInfo` false,
 * and the manifest then records the *settings file's* firmware string with its
 * provenance marked as declared rather than read. Both are usable; only one is
 * evidence, and the profile says which it has.
 *
 * ---------------------------------------------------------------------------
 * Recording the act of measuring
 *
 * A dt distribution measured while eight other types were streaming is not the
 * same measurement as one taken alone. Both are worth having as long as which
 * is which is recorded, so the manifest carries the whole subscription set, the
 * requested period and latency, and whether a GUI was attached -- because the
 * screen being on is a power state and a scheduling state, and neither is
 * neutral.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_MANIFEST_HPP
#define SENSORLAB_MANIFEST_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Catalogue/Catalogue.hpp"

namespace SensorLab::Profile
{

/// Widest version string the kernel can return, plus a terminator. The message
/// declares `char[16]` with no guarantee of one.
constexpr size_t kVersionMax = 17;

/// How a run stopped. **Never inferred**: a run that ended at a plug-in event
/// is marked truncated rather than completed, because a truncated run's
/// distributions are shorter than they look and a reader has to know.
enum class RunEnd : uint8_t
{
    /// Still open. What a state file says while a run is in progress, and what
    /// a manifest says if the app was killed without writing an end.
    InProgress = 0,
    /// The sweep finished, or the user stopped it.
    Completed,
    /// The user aborted a guided step. Records nothing partial.
    Aborted,
    /// `COMMAND_APP_STOP` arrived. Almost always the USB cable: plugging in
    /// terminates every running app (ledger row P8).
    TruncatedByUsb,
    /// Detected on the *next* launch, by an uptime that went backwards.
    TruncatedByReboot,
};

const char *toString(RunEnd e);

/// Which of the two clocks a claim's `observed_at` came from, and both are
/// always recorded. Uptime is monotonic and says nothing about "23:00"; wall
/// clock knows what 23:00 means and can jump. A duration is only ever derived
/// from uptime; a time of day is only ever read from the wall clock.
struct Clocks
{
    uint32_t uptimeMs = 0;
    /// `time(nullptr)`, or -1 when the clock is unreadable.
    int64_t  wallUtc  = -1;
};

/**
 * @brief Everything a run has to say about itself.
 *
 * Written twice: into `runs/<run_id>.json` when the run opens (with
 * `end == InProgress`) and again when it closes. Two writes rather than one at
 * the end, because a run that was terminated by the cable would otherwise have
 * no manifest at all -- and a raw log with no manifest is a file nobody can
 * interpret.
 */
struct RunManifest
{
    /// Monotonic per device, from `state.json`. Part of every claim row.
    uint32_t runId = 0;

    // -- The primary key ----------------------------------------------------

    /// From `RequestSystemInfo`, when the kernel answers.
    char firmware[kVersionMax] {};
    char hardware[kVersionMax] {};
    /// True when the two above were read from the kernel. False means they came
    /// from `settings.json` or are empty, and the profile says so rather than
    /// presenting a declared version as a read one.
    bool haveSystemInfo = false;

    /// `KERNEL_INTERFACE_VERSION` this build was compiled against. Not the
    /// kernel's own -- an app carrying 3 refuses to run on a v2 kernel, so a
    /// running app proves the kernel is at least this, and nothing more.
    uint32_t kernelInterfaceVersion = 0;

    /// This app's `BUILD_VERSION`, and the catalogue and table versions. Two
    /// profiles from different catalogue versions are still diffable claim by
    /// claim; a claim missing from one of them means "this build could not
    /// measure it", not "the device changed", and only these say which.
    char     appVersion[kVersionMax] {};
    uint32_t catalogueVersion = 0;
    uint32_t typeTableVersion = 0;
    /// The SDK tree the type table was generated from.
    char     sdkTag[kVersionMax] {};

    // -- The act of measuring ------------------------------------------------

    /// Bit per `Catalogue::kTypes` index: this run asked for the type.
    /// 37 types, so 64 bits with room. **A dt distribution measured alongside
    /// eight other streams is a different measurement from one taken alone**,
    /// and this is what says which it was.
    uint64_t typesAsked    = 0;
    /// ...and which resolved a driver. Absent and silent are different
    /// findings, and these two bitmaps are what keeps them apart.
    uint64_t typesResolved = 0;
    /// ...and which delivered at least one sample.
    uint64_t typesDelivered = 0;

    /// Requested period and latency, as asked for rather than as delivered.
    /// Layer 6 exists because the two differ; a manifest that recorded only one
    /// of them could not show it.
    float    requestedPeriodMs  = 0.0f;
    uint32_t requestedLatencyMs = 0;

    /// Whether a GUI was attached for any part of the run. The screen being on
    /// is a power state and a scheduling state, and neither is neutral.
    bool guiAttached = false;
    /// Whether the cable was ever detected. A run with this set is suspect
    /// whatever its `end` says, because "it was on the charger" belongs in the
    /// data rather than in somebody's memory.
    bool sawCharging = false;

    // -- Span and outcome ----------------------------------------------------

    Clocks started {};
    Clocks ended {};
    RunEnd end = RunEnd::InProgress;

    /// Sample-log rows appended, and writes that failed. A run that could not
    /// write is not a run whose numbers are shorter -- it is a run whose
    /// numbers are missing, and the report has to say so.
    uint32_t rowsWritten  = 0;
    uint32_t rowFailures  = 0;
    uint64_t bytesWritten = 0;

    /// Uptime span, correct across the ~49.7-day wrap. Never derived from the
    /// two wall-clock readings, which can jump.
    uint32_t durationMs() const
    {
        return static_cast<uint32_t>(ended.uptimeMs - started.uptimeMs);
    }
};

/**
 * @brief Ask the kernel what firmware it is running.
 *
 * A bounded request/response: with a non-zero timeout `sendMessage` returns only
 * once the kernel has filled the message in place, so this cannot hang, and a
 * kernel that does not implement it leaves @p out untouched and returns false
 * rather than blocking startup. Follows `FwDump/.../DeviceContext.cpp`.
 *
 * @return true when the kernel answered and the strings are the device's own.
 */
bool readSystemInfo(const SDK::Kernel &kernel, RunManifest &out);

/// Fill in everything that comes from this build rather than from the device.
void stampBuild(RunManifest &out, const char *appVersion);

/// The profile filename for a manifest, e.g. "profile-1.4.0.json".
///
/// Sanitised: anything that is not a digit, a letter, a dot or a dash becomes a
/// dash, because the string comes from the kernel and FatFs has opinions. An
/// empty or unreadable version yields "profile-unknown.json" -- which is a
/// filename that says, in the one place somebody will definitely look, that the
/// profile cannot be diffed.
size_t profileFileName(char *out, size_t outSize, const RunManifest &m);

/// "runs/<run_id>.csv" and "runs/<run_id>.json".
size_t runLogFileName(char *out, size_t outSize, uint32_t runId);
size_t runManifestFileName(char *out, size_t outSize, uint32_t runId);

} // namespace SensorLab::Profile

#endif // SENSORLAB_MANIFEST_HPP
