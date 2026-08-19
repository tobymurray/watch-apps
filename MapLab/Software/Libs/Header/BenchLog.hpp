/**
 ******************************************************************************
 * @file    BenchLog.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The measurements, on disk, in the form the report script parses.
 ******************************************************************************
 *
 * NORMATIVE FORMAT. `Tools/maplab_report.py` parses exactly what is described
 * here; this comment and that script are the two halves of one contract, and
 * `kLogSchema` is what stops them drifting apart silently.
 *
 * Line-oriented CSV with a kind in the first column, because one flat table
 * cannot say both "a run started here, on this build" and "this bench cost
 * this much". Three kinds:
 *
 *   H  header    one per file, written when the file is created
 *   R  run       one per suite run: which build, both clocks, why
 *   B  bench     one measurement
 *
 * Columns, after the kind:
 *
 *   H  schema, note
 *   R  runIndex, uptimeMs, wallUtc, buildVersion, subject
 *   B  runIndex, uptimeMs, group, id, name, iterations, elapsedMs, usPerOp,
 *      valid, a, b, c, note
 *
 * `a`, `b`, `c` are three bench-specific integers whose meaning is fixed per
 * bench id and documented in the README's table -- points rendered, bytes
 * read, features drawn. They are deliberately not named columns: a schema that
 * needed a migration every time a bench learned to report one more number
 * would be a schema nobody updated.
 *
 * Every number is an integer. `usPerOp` is microseconds. A measurement that
 * could not be taken is `-1`, never 0, and `valid=0` marks a run the clock
 * never advanced across -- a lower bound rather than a result. Zero and
 * unmeasured must never read the same.
 *
 * Appended forever, across launches and boots. The R row carries both clocks
 * so a reader can separate an app restart from a device reboot the way
 * `SleepLab/Probe`'s log does -- uptime climbing across an R row is a restart
 * within one boot, uptime jumping backwards is a reboot. That matters here for
 * one specific reason: **the watchdog bench is expected to restart the
 * device**, and the log is how its last completed step is recovered.
 ******************************************************************************
 */

#ifndef MAPLAB_BENCHLOG_HPP
#define MAPLAB_BENCHLOG_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace MapLab
{

/// Bumped when a column is added, removed or reinterpreted. The reader refuses
/// a file whose schema it does not know rather than mapping by position.
constexpr uint32_t kLogSchema = 1;

/// Relative to the app's sandbox -- `Apps/MapLab/` on the USB volume.
constexpr char kLogPath[] = "maplab_log.csv";

/// One measurement, as it reaches the log.
struct BenchRow {
    const char* group      = "";
    const char* id         = "";
    const char* name       = "";
    uint32_t    iterations = 0;
    uint32_t    elapsedMs  = 0;
    uint32_t    usPerOp    = 0;
    bool        valid      = false;
    int32_t     a          = -1;
    int32_t     b          = -1;
    int32_t     c          = -1;
    const char* note       = "";
};

/**
 * @brief Appends rows, creating the file and its header on first use.
 *
 * Best-effort by construction: a failed write is counted and never propagated
 * into the benchmark. A measurement that could not be stored is worth less
 * than one that could, but a suite that aborted because storage was full would
 * be worth nothing at all -- and the failure counter on screen is what tells
 * you which you are looking at.
 */
class BenchLog
{
public:
    explicit BenchLog(const SDK::Kernel& kernel) : mKernel(kernel) {}

    /// Opens the run: writes the header if the file is new, then one R row.
    /// Returns the run index, which every subsequent row carries.
    uint32_t beginRun(const char* buildVersion, const char* subject);

    void write(const BenchRow& row);

    uint32_t rowsWritten() const { return mRows; }
    uint32_t failures()    const { return mFailures; }
    uint32_t bytesWritten() const { return mBytes; }

private:
    void append(const char* text, size_t length);

    const SDK::Kernel& mKernel;
    uint32_t mRunIndex = 0;
    uint32_t mRows     = 0;
    uint32_t mFailures = 0;
    uint32_t mBytes    = 0;
};

} // namespace MapLab

#endif // MAPLAB_BENCHLOG_HPP
