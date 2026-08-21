/**
 ******************************************************************************
 * @file    RunLog.hpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   runs/<run_id>.csv: the raw record, appended, never regenerable.
 ******************************************************************************
 *
 * NORMATIVE FORMAT. `Tools/profile_report.py` parses exactly what is described
 * here, and a ctest runs the real script against a file written by the real
 * writer, so this comment and that script are the two halves of one contract.
 *
 * ---------------------------------------------------------------------------
 * Why there is a raw log at all when there is a profile
 *
 * `profile.json` is derived. Every figure in it is a statistic, and a statistic
 * embeds the question it was computed to answer. When the question turns out to
 * be the wrong one -- and on a first profile of an undocumented platform it will
 * be -- a derived document cannot be re-derived. **An analysis without its
 * inputs cannot be corrected.**
 *
 * So this file holds what the statistics were computed from: per-interval
 * delivery and per-field domain, per sensor, at a cadence coarse enough to
 * store and fine enough to localise where something changed.
 *
 * It does *not* hold every sample. At the measured ~48 Hz for one sensor
 * (ledger row S3) with a dozen subscribed, a full sample log is tens of
 * megabytes an hour, and the volume that has to survive the night is the same
 * one the profile is written to. Per-sample capture is the guided protocols'
 * job (Tier 4), where a run is ninety seconds and the raw samples genuinely are
 * the deliverable.
 *
 * ---------------------------------------------------------------------------
 * Record kinds
 *
 * Line-oriented CSV with a `kind` first column, because one flat table cannot
 * say both "a run began here" and "this is what the last interval delivered".
 *
 *   H  header      one per file, written only when the file is created
 *   R  run open    one per run, both clocks, plus the manifest's primary key
 *   E  existence   one per sensor type, from the layer 1 sweep
 *   S  stream      one per subscribed type per interval: layers 2, 3 and 4
 *   V  value       one per field per interval: layer 5
 *   X  run end     how the run stopped, and the totals
 *
 * ---------------------------------------------------------------------------
 * Every numeric field is an integer, and here is why
 *
 * The watch's newlib may not link floating-point `printf`, and when it does not
 * `%f` prints nothing at runtime rather than failing at build time. So nothing
 * here formats a float: real-valued columns carry the value as a mantissa and a
 * decimal exponent -- `<name>_m` and `<name>_e`, meaning `m * 10^e` -- which is
 * the same encoding `profile.json` uses and for the same reason. See
 * `Profile/Decimal.hpp`.
 *
 * A missing measurement is `-1`, never `0`. Zero samples delivered in an
 * interval is a finding; a sensor that was never subscribed is not, and the two
 * must not read the same.
 *
 * ---------------------------------------------------------------------------
 * The append discipline, which is not optional
 *
 * `open(write, override=false)` creates a missing file and keeps an existing one
 * -- but it positions the handle at **offset 0, not end of file** (ledger row
 * P6). Both the SDK's in-memory fake and FatFs do this. Writing straight away
 * overwrites the run from its first byte, and the failure is invisible until
 * somebody reads the file. The `seek(size())` is what makes this an append.
 *
 * Open, write, flush, close around every row rather than holding a handle
 * across the run: the writer is interrupted by a USB connection that terminates
 * the process without warning, and the cost is one open per interval against
 * losing the run.
 *
 ******************************************************************************
 */

#ifndef SENSORLAB_RUNLOG_HPP
#define SENSORLAB_RUNLOG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Profile/Manifest.hpp"
#include "Stats/FieldStats.hpp"
#include "Stats/StreamStats.hpp"

namespace SensorLab::Profile
{

/// Bumped when a column is added, removed or reinterpreted. The reader refuses
/// a file whose schema it does not know rather than mapping columns by position
/// and quietly reporting the wrong sensor's counts.
constexpr uint32_t kRunLogSchema = 1;

/// Widest line the format can produce, plus slack. The `S` row is the widest:
/// eighteen integer columns, several of them mantissa/exponent pairs.
constexpr size_t kRunLogLineMax = 512;

/// What layer 1 found out about one type. Written as an `E` row.
struct ExistenceRecord
{
    uint32_t typeValue    = 0;
    /// **Recorded at full 32-bit width, from the raw message.**
    /// `SDK::Sensor::Connection` stores the handle as `uint8_t` while
    /// `RequestDefault::handle` is `uint32_t`, so any handle above 255
    /// truncates silently inside the SDK. This app never lets `Connection` be
    /// its only record of a handle.
    uint32_t handle       = 0;
    bool     resolved     = false;
    bool     connected    = false;
    /// `RequestList`'s count. Nobody has ever seen this answer; -1 when the
    /// request was not answered at all.
    int32_t  driverCount  = -1;
    /// `RequestGetDesc`'s 32 chars, or "" when none came back.
    const char *descriptor = "";
};

/**
 * @brief Append-only writer for the format above.
 */
class RunLog
{
public:
    explicit RunLog(const SDK::Kernel &kernel);

    /// Open a run: create `runs/`, write the header if the file is new, then an
    /// `R` row carrying both clocks and the manifest's primary key.
    ///
    /// A reader can then tell the three restart cases apart without inference:
    /// uptime climbing across an `R` row is an app restart within one boot;
    /// uptime jumping backwards is a device reboot; a wall clock that moved
    /// while uptime did not agree is the wall clock having been changed.
    bool begin(const RunManifest &manifest);

    bool writeExistence(const RunManifest &manifest, const ExistenceRecord &rec);

    /// One interval's delivery and timing for one subscribed type.
    /// @param intervalMs The uptime span the row covers. Not assumed to be the
    ///                   nominal interval: a row is written when the loop next
    ///                   wakes at or after its deadline, and every rate in the
    ///                   report is computed against this rather than against a
    ///                   nominal minute.
    bool writeStream(uint32_t uptimeMs, int64_t wallUtc, uint32_t typeValue,
                     uint32_t intervalMs, const Stats::StreamStats &s);

    /// One interval's domain for one field.
    bool writeValue(uint32_t uptimeMs, uint32_t typeValue, uint8_t fieldIdx,
                    const Stats::FieldStats &f);

    /// Close the run. Written even when the cable is going in, because where a
    /// run stopped is the finding and a row that never reached storage cannot
    /// say where that was.
    bool end(const RunManifest &manifest);

    /// Rows this run failed to write. The only way to notice storage filling up
    /// on a device with no screen attached.
    uint32_t failures() const { return mFailures; }
    uint32_t rows()     const { return mRows; }
    uint64_t bytes()    const { return mBytes; }

private:
    bool append(const char *path, const char *text, size_t len);

    const SDK::Kernel &mKernel;
    uint32_t           mFailures = 0;
    uint32_t           mRows     = 0;
    uint64_t           mBytes    = 0;
    char               mPath[32] {};
};

// ---------------------------------------------------------------------------
// Resume
// ---------------------------------------------------------------------------

/// Relative to the app's sandbox -- `Apps/SensorLab/` on the USB volume.
constexpr char kStatePath[] = "state.json";

/**
 * @brief Enough to resume a long run across an app restart, following FwDump.
 *
 * A soak that has to survive being terminated by the cable cannot keep its
 * progress only in RAM. This is rewritten after each flush, and on start the
 * service reads it and does one of two things **explicitly, never silently**:
 * resumes the run, or closes it as truncated.
 *
 * Which of the two depends on the uptime. An uptime that went *backwards* since
 * the state was written means the device rebooted, so the sensor pipelines
 * restarted, so every since-boot counter reset and every distribution in the
 * old run has a discontinuity in it that nothing can locate afterwards -- that
 * run is closed as `TruncatedByReboot` and a new one opened. An uptime that
 * climbed is an app restart within one boot, which is recoverable.
 */
struct RunState
{
    uint32_t schema = kRunLogSchema;
    /// The run that was open, or 0 for none.
    uint32_t runId = 0;
    /// Next run id to hand out. Monotonic per device: it survives the app being
    /// removed and reinstalled only as far as the volume does, which is the
    /// right scope -- a run id is a key into files on this volume.
    uint32_t nextRunId = 1;
    /// Uptime at the last flush. Compared against `getTimeMs()` on start.
    uint32_t lastUptimeMs = 0;
    int64_t  lastWallUtc  = -1;
    uint32_t rowsWritten  = 0;
    /// Which phase the run was in, so a resumed soak does not restart at the
    /// existence sweep and overwrite its own layer 1 answers with identical
    /// ones under a new run id.
    uint8_t  phase = 0;
    bool     runOpen = false;
};

/// Read `state.json`. Returns false when absent or unparseable, leaving @p out
/// at its defaults -- which is the correct behaviour for a first launch and for
/// a corrupted file alike, because in both cases there is no run to resume.
bool readState(const SDK::Kernel &kernel, RunState &out);

/// Rewrite `state.json`. Whole-file, not appended: it is a position, not a log.
bool writeState(const SDK::Kernel &kernel, const RunState &state);

} // namespace SensorLab::Profile

#endif // SENSORLAB_RUNLOG_HPP
