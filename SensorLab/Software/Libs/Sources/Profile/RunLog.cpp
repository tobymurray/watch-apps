/**
 ******************************************************************************
 * @file    RunLog.cpp
 * @date    21-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   runs/<run_id>.csv. NORMATIVE FORMAT is in the header.
 ******************************************************************************
 */

#include "Profile/RunLog.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

#include "SDK/JSON/JsonStreamReader.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"

#include "Profile/Decimal.hpp"

#define LOG_MODULE_PRX      "RunLog"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SensorLab::Profile
{

namespace
{

/// The `runs/` subdirectory. Created on demand: `mkdir` on an existing
/// directory is a harmless failure and is not treated as one.
constexpr char kRunsDir[] = "runs";

/// The `H` row. Column names, in order, for every row kind in the file.
///
/// Kept as one literal rather than assembled from the writers, because the point
/// of it is to be the thing a reader checks the parser against -- a header
/// generated from the same source as the rows cannot catch a column going out of
/// step with its name.
///
/// `_m`/`_e` pairs are a mantissa and a decimal exponent: value = m * 10^e. See
/// Decimal.hpp for why no column is a float.
constexpr char kHeaderLine[] =
    "H,schema,1,kinds,R;E;S;V;X\n"
    "H,R,kind,uptime_ms,wall_utc,run_id,firmware,hardware,fw_read,app_version,"
    "catalogue_version,type_table_version,kernel_iface\n"
    "H,E,kind,uptime_ms,run_id,type,resolved,handle,driver_count,connected,descriptor\n"
    "H,S,kind,uptime_ms,wall_utc,type,interval_ms,samples,batches,field_count,"
    "field_count_alt,ts_span_ms,longest_gap_ms,dt_min_m,dt_min_e,dt_p05_m,dt_p05_e,"
    "dt_p50_m,dt_p50_e,dt_p95_m,dt_p95_e,dt_max_m,dt_max_e,dt_bin_width_m,dt_bin_width_e,"
    "dt_over,batch_p50_m,batch_p50_e,batch_dt_p50_m,batch_dt_p50_e,"
    "us_over_999,non_monotonic,skew_ppm_m,skew_ppm_e,first_offset_ms,last_offset_ms,"
    "cadence,first_sample_ms\n"
    "H,V,kind,uptime_ms,type,field,n,nonfinite,denormal,min_m,min_e,max_m,max_e,"
    "mean_m,mean_e,lsb_m,lsb_e,stuck_max_run,ever_changed,distinct,distinct_over\n"
    "H,X,kind,uptime_ms,wall_utc,run_id,end,rows,failures,bytes\n";

/// Emit a float as the pair of columns `,<m>,<e>`.
int writePair(char *out, size_t outSize, float v)
{
    const Decimal d = decompose(v);
    if (d.kind != DecimalKind::Finite) {
        // A non-finite statistic is written as the sentinel pair (0, 127),
        // which no real measurement can produce: the exponent range a finite
        // value can reach is [-51, 32]. Distinguishable, integer-only, and it
        // keeps the column count fixed so a reader never has to count fields.
        return std::snprintf(out, outSize, ",0,127");
    }
    return std::snprintf(out, outSize, ",%ld,%d",
                         static_cast<long>(d.mantissa),
                         static_cast<int>(d.exponent));
}

/// A `-1`-when-absent integer, the Sleep Probe's convention.
int32_t orMissing(bool have, uint32_t v)
{
    return have ? static_cast<int32_t>(v) : -1;
}

} // namespace

RunLog::RunLog(const SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

bool RunLog::append(const char *path, const char *text, size_t len)
{
    if (len == 0) {
        return true;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(path);
    if (!file || !file->open(true, false)) {
        mFailures++;
        return false;
    }

    // open(write, override=false) keeps an existing file but positions at
    // offset 0, not end of file (ledger row P6). The seek is what makes this an
    // append, and without it the log silently keeps only its newest row.
    if (!file->seek(file->size())) {
        file->close();
        mFailures++;
        return false;
    }

    size_t     written = 0;
    const bool ok      = file->write(text, len, written) && written == len;

    // Flush before close, not instead of it: a row that is in the FAT cache
    // when the cable goes in is a row that never happened.
    file->flush();
    file->close();

    if (!ok) {
        mFailures++;
        return false;
    }

    mBytes += written;
    mRows++;
    return true;
}

bool RunLog::begin(const RunManifest &manifest)
{
    // Harmless when it already exists; the SDK's fake and FatFs both report
    // failure for that case and neither distinguishes it from a real error, so
    // the return value is deliberately ignored and the append below is what
    // actually reports a problem.
    mKernel.fs.mkdir(kRunsDir);

    if (runLogFileName(mPath, sizeof(mPath), manifest.runId) == 0) {
        mFailures++;
        return false;
    }

    bool ok = true;
    if (!mKernel.fs.exist(mPath)) {
        ok = append(mPath, kHeaderLine, sizeof(kHeaderLine) - 1);
    }

    char line[kRunLogLineMax];
    const int n = std::snprintf(
        line, sizeof(line),
        "R,%lu,%lld,%lu,%s,%s,%d,%s,%lu,%lu,%lu\n",
        static_cast<unsigned long>(manifest.started.uptimeMs),
        static_cast<long long>(manifest.started.wallUtc),
        static_cast<unsigned long>(manifest.runId),
        manifest.firmware[0] != '\0' ? manifest.firmware : "unknown",
        manifest.hardware[0] != '\0' ? manifest.hardware : "unknown",
        manifest.haveSystemInfo ? 1 : 0,
        manifest.appVersion[0] != '\0' ? manifest.appVersion : "unknown",
        static_cast<unsigned long>(manifest.catalogueVersion),
        static_cast<unsigned long>(manifest.typeTableVersion),
        static_cast<unsigned long>(manifest.kernelInterfaceVersion));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        mFailures++;
        return false;
    }
    return append(mPath, line, static_cast<size_t>(n)) && ok;
}

bool RunLog::writeExistence(const RunManifest &manifest,
                            const ExistenceRecord &rec)
{
    char line[kRunLogLineMax];
    // The descriptor is a string the kernel chose, so commas in it would break
    // the row. Written last, and any comma in it becomes a space -- the only
    // sanitising this format does, and it is noted here rather than silently.
    char desc[34] {};
    {
        const char *src = (rec.descriptor != nullptr) ? rec.descriptor : "";
        size_t      i   = 0;
        for (; i + 1 < sizeof(desc) && src[i] != '\0'; i++) {
            const char c = src[i];
            desc[i] = (c == ',' || c == '\n' || c == '\r') ? ' ' : c;
        }
        desc[i] = '\0';
    }

    const int n = std::snprintf(
        line, sizeof(line),
        "E,%lu,%lu,0x%lx,%d,0x%lx,%ld,%d,%s\n",
        static_cast<unsigned long>(manifest.started.uptimeMs),
        static_cast<unsigned long>(manifest.runId),
        static_cast<unsigned long>(rec.typeValue),
        rec.resolved ? 1 : 0,
        // Full 32-bit width, from the raw message. Connection's uint8_t would
        // have truncated anything above 255 without saying so.
        static_cast<unsigned long>(rec.handle),
        static_cast<long>(rec.driverCount),
        rec.connected ? 1 : 0,
        desc);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        mFailures++;
        return false;
    }
    return append(mPath, line, static_cast<size_t>(n));
}

bool RunLog::writeStream(uint32_t uptimeMs, int64_t wallUtc, uint32_t typeValue,
                         uint32_t intervalMs, const Stats::StreamStats &s)
{
    char   line[kRunLogLineMax];
    size_t n   = 0;
    int    got = 0;

    got = std::snprintf(line, sizeof(line),
                        "S,%lu,%lld,0x%lx,%lu,%lu,%lu,%d,%d,%lu,%lu",
                        static_cast<unsigned long>(uptimeMs),
                        static_cast<long long>(wallUtc),
                        static_cast<unsigned long>(typeValue),
                        static_cast<unsigned long>(intervalMs),
                        static_cast<unsigned long>(s.samples()),
                        static_cast<unsigned long>(s.batches()),
                        static_cast<int>(s.fieldCount()),
                        // -1 rather than 0 when the width never changed: a
                        // second width of zero and no second width must not
                        // read the same.
                        s.fieldCountStable()
                            ? -1 : static_cast<int>(s.fieldCountAlternate()),
                        static_cast<unsigned long>(s.timestampSpanMs()),
                        static_cast<unsigned long>(s.longestGapMs()));
    if (got <= 0) { mFailures++; return false; }
    n = static_cast<size_t>(got);

    const Stats::DtHistogram &dt = s.dt();
    // Exact min and max, bin-midpoint quantiles, and the bin width alongside
    // them -- because a p95 read off 1 ms bins and one read off 10 ms bins are
    // different numbers and a reader has to be able to tell which this is.
    const float pairs[] = {
        dt.min(), dt.quantile(0.05f), dt.quantile(0.5f), dt.quantile(0.95f),
        dt.max(), dt.binWidth(),
    };
    for (float v : pairs) {
        got = writePair(line + n, sizeof(line) - n, v);
        if (got <= 0) { mFailures++; return false; }
        n += static_cast<size_t>(got);
    }

    got = std::snprintf(line + n, sizeof(line) - n, ",%lu",
                        static_cast<unsigned long>(dt.overflow()));
    if (got <= 0) { mFailures++; return false; }
    n += static_cast<size_t>(got);

    // Samples per batch, and batch arrival interval. Separate quantities from
    // sample dt: 5000 ms requested, 195 ms delivered (ledger row S17).
    const float more[] = {
        s.batchSizes().quantile(0.5f),
        s.batchIntervals().quantile(0.5f),
    };
    for (float v : more) {
        got = writePair(line + n, sizeof(line) - n, v);
        if (got <= 0) { mFailures++; return false; }
        n += static_cast<size_t>(got);
    }

    got = std::snprintf(line + n, sizeof(line) - n, ",%lu,%lu",
                        static_cast<unsigned long>(s.usOver999()),
                        static_cast<unsigned long>(s.nonMonotonic()));
    if (got <= 0) { mFailures++; return false; }
    n += static_cast<size_t>(got);

    got = writePair(line + n, sizeof(line) - n,
                    s.hasSkew() ? s.skewPpm() : 0.0f);
    if (got <= 0) { mFailures++; return false; }
    n += static_cast<size_t>(got);

    got = std::snprintf(line + n, sizeof(line) - n, ",%ld,%ld,%d,%ld\n",
                        static_cast<long>(s.firstOffsetMs()),
                        static_cast<long>(s.lastOffsetMs()),
                        static_cast<int>(s.cadence()),
                        static_cast<long>(orMissing(s.hasFirstSample(),
                                                    s.firstSampleMs())));
    if (got <= 0) { mFailures++; return false; }
    n += static_cast<size_t>(got);

    if (n >= sizeof(line)) {
        // Truncated rather than overflowed -- snprintf is safe -- but a
        // truncated row is a corrupt row, so it is dropped and counted.
        mFailures++;
        return false;
    }
    return append(mPath, line, n);
}

bool RunLog::writeValue(uint32_t uptimeMs, uint32_t typeValue, uint8_t fieldIdx,
                        const Stats::FieldStats &f)
{
    char   line[kRunLogLineMax];
    size_t n   = 0;
    int    got = 0;

    got = std::snprintf(line, sizeof(line), "V,%lu,0x%lx,%u,%lu,%lu,%lu",
                        static_cast<unsigned long>(uptimeMs),
                        static_cast<unsigned long>(typeValue),
                        static_cast<unsigned>(fieldIdx),
                        static_cast<unsigned long>(f.count()),
                        static_cast<unsigned long>(f.nonFinite()),
                        static_cast<unsigned long>(f.denormal()));
    if (got <= 0) { mFailures++; return false; }
    n = static_cast<size_t>(got);

    const float pairs[] = { f.min(), f.max(), f.mean() };
    for (float v : pairs) {
        got = writePair(line + n, sizeof(line) - n, v);
        if (got <= 0) { mFailures++; return false; }
        n += static_cast<size_t>(got);
    }

    // The LSB is a lower bound and only meaningful once the value varied. The
    // sentinel pair says "not established" rather than reporting a zero that
    // would read as a measurement of infinite resolution.
    if (f.hasLsb()) {
        got = writePair(line + n, sizeof(line) - n, f.lsb());
    } else {
        got = std::snprintf(line + n, sizeof(line) - n, ",0,127");
    }
    if (got <= 0) { mFailures++; return false; }
    n += static_cast<size_t>(got);

    got = std::snprintf(line + n, sizeof(line) - n, ",%lu,%d,%u,%d\n",
                        static_cast<unsigned long>(f.stuckMaxRun()),
                        f.everChanged() ? 1 : 0,
                        static_cast<unsigned>(f.distinctCount()),
                        f.distinctOverflowed() ? 1 : 0);
    if (got <= 0) { mFailures++; return false; }
    n += static_cast<size_t>(got);

    if (n >= sizeof(line)) {
        mFailures++;
        return false;
    }
    return append(mPath, line, n);
}

bool RunLog::end(const RunManifest &manifest)
{
    char      line[kRunLogLineMax];
    const int n = std::snprintf(
        line, sizeof(line), "X,%lu,%lld,%lu,%s,%lu,%lu,%llu\n",
        static_cast<unsigned long>(manifest.ended.uptimeMs),
        static_cast<long long>(manifest.ended.wallUtc),
        static_cast<unsigned long>(manifest.runId),
        toString(manifest.end),
        static_cast<unsigned long>(mRows),
        static_cast<unsigned long>(mFailures),
        static_cast<unsigned long long>(mBytes));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        mFailures++;
        return false;
    }
    return append(mPath, line, static_cast<size_t>(n));
}

// ---------------------------------------------------------------------------
// Resume state
// ---------------------------------------------------------------------------

bool readState(const SDK::Kernel &kernel, RunState &out)
{
    out = RunState {};

    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (!kernel.fs.objectInfo(kStatePath, info) || info.isDir || info.size == 0) {
        return false;
    }
    // A state file this app wrote is a few hundred bytes. A ceiling, checked
    // before anything is allocated, because every SDK settings serializer does
    // `new char[file->size()]` with no upper bound.
    if (info.size > 4096) {
        LOG_WARNING("%s is %u bytes; ignoring it\n", kStatePath,
                    static_cast<unsigned>(info.size));
        return false;
    }

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(kStatePath);
    if (!file || !file->open()) {
        return false;
    }
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[info.size]);
    if (!buffer) {
        file->close();
        return false;
    }
    size_t     read = 0;
    const bool ok   = file->read(buffer.get(), info.size, read) && read == info.size;
    file->close();
    if (!ok) {
        return false;
    }

    SDK::JsonStreamReader json(buffer.get(), info.size);
    if (!json.validate()) {
        LOG_WARNING("%s is not valid JSON; starting fresh\n", kStatePath);
        return false;
    }

    uint32_t schema = 0;
    if (!json.get("schema", schema) || schema != kRunLogSchema) {
        LOG_WARNING("%s has schema %lu, expected %lu; starting fresh\n",
                    kStatePath, static_cast<unsigned long>(schema),
                    static_cast<unsigned long>(kRunLogSchema));
        return false;
    }

    uint32_t v = 0;
    if (json.get("run_id", v))         { out.runId = v; }
    if (json.get("next_run_id", v))    { out.nextRunId = (v > 0) ? v : 1; }
    if (json.get("last_uptime_ms", v)) { out.lastUptimeMs = v; }
    if (json.get("rows_written", v))   { out.rowsWritten = v; }
    if (json.get("phase", v))          { out.phase = static_cast<uint8_t>(v); }

    // Read back the string form. A file written by an older build with a bare
    // number is tolerated -- the reader tries both -- because a state file this
    // app cannot read is a run it silently fails to close.
    std::string_view wall;
    if (json.get("last_wall_utc", wall) && !wall.empty()) {
        char    buf[24] {};
        size_t  i = 0;
        for (; i + 1 < sizeof(buf) && i < wall.size(); i++) {
            buf[i] = wall[i];
        }
        buf[i] = '\0';
        char *end = nullptr;
        const long long v = std::strtoll(buf, &end, 10);
        if (end != buf) {
            out.lastWallUtc = static_cast<int64_t>(v);
        }
    } else {
        int64_t legacy = -1;
        if (json.get("last_wall_utc", legacy)) { out.lastWallUtc = legacy; }
    }

    bool open = false;
    if (json.get("run_open", open)) { out.runOpen = open; }

    // A state file naming a run id at or above the next one to hand out is
    // internally inconsistent -- the two are written together, so this only
    // happens after a partial write. Refuse it rather than reuse a run id,
    // which would append this run's rows to a previous run's file.
    if (out.runId >= out.nextRunId) {
        LOG_WARNING("%s: run_id %lu >= next_run_id %lu; starting fresh\n",
                    kStatePath, static_cast<unsigned long>(out.runId),
                    static_cast<unsigned long>(out.nextRunId));
        out = RunState {};
        return false;
    }

    return true;
}

bool writeState(const SDK::Kernel &kernel, const RunState &state)
{
    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(kStatePath);
    // override = true: this is a position, not a log. Rewritten whole after
    // each flush.
    if (!file || !file->open(true, true)) {
        return false;
    }

    // The wall clock goes out as a *string*. `JsonStreamWriter::add(int64_t)`
    // casts to `double` and formats with `%g`, which is six significant digits
    // -- enough to turn 1755553500 into 1.75555e+09 and lose the seconds. The
    // only SDK number path this app trusts is `add(uint32_t)`; see the note in
    // ProfileWriter.cpp and Docs/FINDINGS.md.
    char wall[24];
    const int wn = std::snprintf(wall, sizeof(wall), "%lld",
                                 static_cast<long long>(state.lastWallUtc));

    SDK::JsonStreamWriter w(file.get());
    {
        SDK::JsonStreamWriter::MapScope root(w);
        w.add("schema",         kRunLogSchema);
        w.add("run_id",         state.runId);
        w.add("next_run_id",    state.nextRunId);
        w.add("last_uptime_ms", state.lastUptimeMs);
        if (wn > 0 && static_cast<size_t>(wn) < sizeof(wall)) {
            w.add("last_wall_utc", wall);
        } else {
            w.addNull("last_wall_utc");
        }
        w.add("rows_written",   state.rowsWritten);
        w.add("phase",          static_cast<uint32_t>(state.phase));
        w.add("run_open",       state.runOpen);
    }
    w.flush();
    const bool ok = !w.isError();
    file->flush();
    file->close();
    return ok;
}

} // namespace SensorLab::Profile
