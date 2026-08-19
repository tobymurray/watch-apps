/**
 ******************************************************************************
 * @file    NightStore.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   A night on disk. Normative format is in the header.
 ******************************************************************************
 */

#include "NightStore.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

#include "SDK/JSON/JsonStreamWriter.hpp"

#include "Engine/RestfulnessBand.hpp"
#include "Engine/SleepWakeScorer.hpp"

#define LOG_MODULE_PRX      "NightStore"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace SleepLab
{
namespace {

/// Column names of the epoch CSV, in order. One literal, so the header and the
/// row formatter below can be read against each other -- a header generated
/// from the same source as the row cannot catch a column going out of step
/// with its name.
constexpr char kEpochHeader[] =
    "# SleepLab epoch log, schema 2. One row per 30 s recording epoch.\n"
    "# uptime_ms is device uptime and survives an app restart; wall_utc is the\n"
    "# wall clock and can jump. Durations come from uptime, times of day from\n"
    "# the wall clock, and -1 means not measured (never 0).\n"
    "# Schema 2 adds the delivery and power columns the Tier 0 probe used to own,\n"
    "# so one night can answer both what the wearer did and what the hardware did.\n"
    "# They are absent, not zero, when the diagnostics setting is off.\n"
    "uptime_ms,wall_utc,span_ms,count,peak,samples,"
    "motion,sig_motion,step_delta,"
    "hr_mean_x10,hr_min_x10,hr_samples,hr_source,"
    "worn_pct,worn_edges,batt_pct_x10,charging,"
    "rmssd_x10,sdnn_x10,rr_count,"
    "acc_batches,acc_max_gap_ms,touch_n,hr_trust_x10,"
    "hrex_opt,hrex_ext,hrex_unk,"
    "batt_mv,batt_ma_x10,batt_avg_ma_x10,batt_mah,"
    "wakes,msgs\n";

constexpr char kIndexHeader[] =
    "# SleepLab night index, schema 1. One row per completed night.\n"
    "# The personal baseline is rebuilt from this file and from nothing else.\n"
    "# Rows whose worn verdict is not 0 (worn) are excluded from it.\n"
    "start_utc,tib_min,tst_min,eff_pct,hr_min_x10,hr_min_at_pct,worn,interruption\n";

/// Longest an epoch row can be, plus slack.
///
/// Schema 2's 33 columns run to about 200 characters of digits in the worst case;
/// 384 is comfortable slack. `appendEpoch` refuses to write a row that did not
/// fit rather than writing a truncated one, so being wrong here costs epochs
/// rather than corrupting the file -- but it costs *all* of them, silently until
/// the write-failure flag was added, so the slack is deliberate.
constexpr size_t kRowMax = 384;

/// Format a session-start time as the file's stem.
///
/// Local, not UTC: the file is named for the evening a person went to bed, and
/// a UTC stem would name half the year's nights with the wrong date.
void stemFor(int64_t startUtc, uint32_t seq, char *out, size_t outSize)
{
    if (startUtc > 0) {
        const std::time_t t = static_cast<std::time_t>(startUtc);
        std::tm local {};
#if defined(_WIN32) || defined(_WIN64)
        const bool ok = (localtime_s(&local, &t) == 0);
#else
        const bool ok = (localtime_r(&t, &local) != nullptr);
#endif
        if (ok) {
            std::snprintf(out, outSize, "%04d%02d%02dT%02d%02d%02d",
                          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                          local.tm_hour, local.tm_min, local.tm_sec);
            return;
        }
    }
    // No readable clock. The night still records; it simply cannot be filed by
    // date, and a name that says so is better than one that lies.
    std::snprintf(out, outSize, "unknown%03lu",
                  static_cast<unsigned long>(seq % 1000u));
}

/// Whether @p path is one this app's own recorder could have created.
///
/// `Nights/<stem>.csv`, with no separator or parent reference in the stem, and
/// short enough that swapping ".csv" for ".json" still fits kMaxNightPath.
bool isNightPath(const char *path)
{
    if (path == nullptr) {
        return false;
    }
    const size_t len = std::strlen(path);

    // ".json" is one byte longer than ".csv", and the summary path is built in a
    // buffer of kMaxNightPath bytes including its terminator.
    if (len + 1 + 1 > kMaxNightPath) {
        return false;
    }

    const size_t dirLen = std::strlen(kNightsDir);
    if (len < dirLen + 1 + 1 + 4) {
        return false;
    }
    if (std::strncmp(path, kNightsDir, dirLen) != 0 || path[dirLen] != '/') {
        return false;
    }
    if (std::strcmp(path + len - 4, ".csv") != 0) {
        return false;
    }

    // The stem is one path component and nothing else.
    for (size_t i = dirLen + 1; i < len - 4; ++i) {
        const char c = path[i];
        if (c == '/' || c == '\\' || c == ':') {
            return false;
        }
        if (c == '.' && path[i + 1] == '.') {
            return false;
        }
    }
    return true;
}

} // namespace

NightStore::NightStore(const SDK::Kernel &kernel)
    : mKernel(kernel)
{
}

bool NightStore::append(const char *path, const char *text, size_t len)
{
    if (len == 0) {
        return true;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(path);
    if (!file || !file->open(true, false)) {
        LOG_WARNING("cannot open %s for append\n", path);
        return false;
    }

    // open(write, override=false) creates a missing file and keeps an existing
    // one -- but positions at offset 0, not at the end. Both the SDK's fake and
    // FatFs behave this way, so the seek is what makes this an append. Without
    // it a night keeps only its newest row.
    if (!file->seek(file->size())) {
        file->close();
        return false;
    }

    size_t     written = 0;
    bool       ok      = file->write(text, len, written) && written == len;

    // Flush before close, not instead of it: a row still in the FAT cache when
    // the USB cable goes in is a row that never happened.
    //
    // And both are part of whether the write happened. FatFs's `f_close` syncs,
    // and when the sync fails it keeps the FIL valid and its lock-table entry
    // held -- so a close that fails is both a row that was never committed and a
    // lock slot that will not come back. Reporting that as a successful write is
    // how a night ends up shorter than its summary says with nothing to show
    // why.
    if (!file->flush()) { ok = false; }
    if (!file->close()) { ok = false; }
    return ok;
}


// -- Starting and resuming ----------------------------------------------------

ResumeState NightStore::readState(uint32_t nowMs, int64_t nowUtc)
{
    ResumeState s;

    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (!mKernel.fs.objectInfo(kStatePath, info) || info.isDir ||
        info.size == 0 || info.size > 512) {
        return s;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kStatePath);
    if (!file || !file->open()) {
        return s;
    }

    char   buf[512] = {};
    size_t read = 0;
    const bool ok = file->read(buf, info.size, read);
    file->close();
    if (!ok || read == 0) {
        return s;
    }
    buf[read < sizeof(buf) ? read : sizeof(buf) - 1] = '\0';

    // "STATE <path> <epochs> <uptimeMs> <wallUtc> <flags> <startUtc>"
    char          path[kMaxNightPath] = {};
    unsigned long epochs = 0, uptime = 0;
    long long     wall   = 0;
    unsigned      flags  = 0;
    long long     startUtc = 0;

    // The width in the scanf format has to track kMaxNightPath, and there is no
    // way to write that as an expression, so it is asserted instead.
    static_assert(kMaxNightPath == 64, "the %63s below must match kMaxNightPath");
    if (std::sscanf(buf, "STATE %63s %lu %lu %lld %u %lld",
                    path, &epochs, &uptime, &wall, &flags, &startUtc) != 6) {
        LOG_WARNING("night_state.txt is unreadable; starting fresh\n");
        return s;
    }

    // The path is used verbatim: to test existence, to append every epoch to,
    // and -- with ".csv" swapped for ".json" -- to build the summary path in a
    // buffer of this same size, which ".json" makes one byte longer. So it has
    // to be a path this app could have written, and the check is on the shape
    // rather than on a blocklist: `Nights/<stem>.csv`, no path separators in the
    // stem, and short enough to survive the extension swap.
    //
    // The file is the app's own and lives inside the app's sandbox, so the
    // surface this guards is corruption rather than attack -- it is rewritten
    // 1 900 times a night and the power can go at any of them.
    if (!isNightPath(path)) {
        LOG_WARNING("night_state.txt names %s, which is not a night this app "
                    "writes; starting fresh\n", path);
        return ResumeState{};
    }

    s.present  = true;
    std::snprintf(s.path, sizeof(s.path), "%s", path);
    s.epochs   = static_cast<uint32_t>(epochs);
    s.uptimeMs = static_cast<uint32_t>(uptime);
    s.wallUtc  = static_cast<int64_t>(wall);
    s.flags    = static_cast<uint16_t>(flags);
    s.startUtc = static_cast<int64_t>(startUtc);

    // Classify the restart. Uptime is the only clock that can do this: it
    // survives an app restart and resets only on a device reboot, so uptime
    // that has gone backwards is a reboot and uptime that has climbed is a
    // relaunch inside one boot -- almost always a USB session.
    //
    // Signed difference, because uptime wraps at ~49.7 days and an unsigned
    // compare across the wrap would call every relaunch a reboot.
    const int32_t elapsed = static_cast<int32_t>(nowMs - s.uptimeMs);
    if (elapsed < 0) {
        s.deviceRebooted = true;
        // No uptime to measure the outage with -- it reset. The wall clock is
        // the only witness, and it is allowed to be wrong; a gap it reports as
        // negative or absurd is treated as unknown rather than as a correction.
        if (nowUtc > 0 && s.wallUtc > 0 && nowUtc > s.wallUtc) {
            const int64_t offMin = (nowUtc - s.wallUtc) / 60;
            if (offMin > 0 && offMin < 24 * 60) {
                s.gapMinutes = static_cast<uint32_t>(offMin);
            }
        }
    } else {
        s.appRestarted = true;
        s.gapMinutes   = static_cast<uint32_t>(elapsed) / 60000u;
    }

    // A wall clock that moved by far more than uptime says it should have.
    // Only checkable when both readings exist; a device that was off has no
    // uptime to compare against, so the reboot case is skipped.
    if (!s.deviceRebooted && nowUtc > 0 && s.wallUtc > 0) {
        const int64_t wallElapsedMs = (nowUtc - s.wallUtc) * 1000;
        const int64_t drift = wallElapsedMs - static_cast<int64_t>(elapsed);
        // Two minutes of slack: the wall clock has one-second resolution and
        // the two readings are taken at slightly different moments.
        if (drift > 120000 || drift < -120000) {
            s.flags |= Engine::Interruption::kClockJump;
        }
    }

    if (s.deviceRebooted || s.appRestarted) {
        s.flags |= Engine::Interruption::kResumed;
    }

    LOG_INFO("resuming %s at %lu epochs (%s)\n", s.path,
             static_cast<unsigned long>(s.epochs),
             s.deviceRebooted ? "device rebooted" : "app restarted");
    return s;
}

bool NightStore::beginNight(int64_t startUtc, uint32_t nowMs,
                            const char *provenance)
{
    mKernel.fs.mkdir(kNightsDir);

    char stem[32];
    stemFor(startUtc, mUnnamedSeq++, stem, sizeof(stem));
    std::snprintf(mPath, sizeof(mPath), "%s/%s.csv", kNightsDir, stem);

    mEpochs        = 0;
    mLastUptimeMs  = nowMs;
    mLastWallUtc   = startUtc;
    mStartWallUtc  = startUtc;

    if (mKernel.fs.exist(mPath)) {
        // Two sessions opening in the same second. Vanishingly unlikely, but
        // appending into an existing night's file would splice two nights
        // together with no marker between them, which is worse than the
        // suffix being ugly.
        std::snprintf(mPath, sizeof(mPath), "%s/%s_b.csv", kNightsDir, stem);
    }

    if (!append(mPath, kEpochHeader, sizeof(kEpochHeader) - 1)) {
        LOG_WARNING("cannot create %s\n", mPath);
        mPath[0] = '\0';
        return false;
    }

    // What produced this file, in the file. The summary JSON carries the same and
    // more, and is written only when the night closes -- so a night the USB cable
    // ended left a record that could not say which build wrote it or what settings
    // it ran under. A comment line costs ~120 bytes once and every reader of the
    // CSV already skips `#`.
    if (provenance != nullptr && provenance[0] != '\0') {
        char line[192];
        const int n = std::snprintf(line, sizeof(line), "# %s\n", provenance);
        if (n > 0 && static_cast<size_t>(n) < sizeof(line)) {
            append(mPath, line, static_cast<size_t>(n));
        }
    }

    LOG_INFO("night opened: %s\n", mPath);
    return writeState(0);
}

bool NightStore::resumeNight(const ResumeState &state)
{
    if (!state.present || state.path[0] == '\0') {
        return false;
    }
    if (!mKernel.fs.exist(state.path)) {
        // The state names a file that is gone -- deleted over USB, most
        // likely. Nothing to resume into; the caller starts fresh.
        LOG_WARNING("%s named by night_state.txt is missing\n", state.path);
        return false;
    }

    std::snprintf(mPath, sizeof(mPath), "%s", state.path);
    mEpochs        = state.epochs;
    mLastUptimeMs  = state.uptimeMs;
    mLastWallUtc   = state.wallUtc;
    mStartWallUtc  = state.startUtc;
    LOG_INFO("night resumed: %s at %lu epochs\n", mPath,
             static_cast<unsigned long>(mEpochs));
    return true;
}


// -- Recording ----------------------------------------------------------------

bool NightStore::writeState(uint16_t flags)
{
    char line[kMaxNightPath + 128];
    const int n = std::snprintf(line, sizeof(line),
                                "STATE %s %lu %lu %lld %u %lld\n",
                                mPath,
                                static_cast<unsigned long>(mEpochs),
                                static_cast<unsigned long>(mLastUptimeMs),
                                static_cast<long long>(mLastWallUtc),
                                static_cast<unsigned>(flags),
                                static_cast<long long>(mStartWallUtc));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        return false;
    }

    // Rewritten whole rather than appended, so what is on disk is always one
    // complete line. A half-written second line is worse than a stale first
    // one: a parser can match the wrong half.
    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kStatePath);
    if (!file || !file->open(true, true)) {
        return false;
    }
    size_t     written = 0;
    const bool ok = file->write(line, static_cast<size_t>(n), written) &&
                    written == static_cast<size_t>(n);
    file->flush();
    file->close();
    return ok;
}

bool NightStore::appendEpoch(const Engine::Epoch &e, uint16_t flags)
{
    if (!isOpen()) {
        return false;
    }

    char line[kRowMax];
    const int n = std::snprintf(
        line, sizeof(line),
        "%lu,%lld,%lu,%lu,%lu,%u,"
        "%u,%u,%ld,"
        "%d,%d,%u,%u,"
        "%u,%u,%d,%d,"
        "%ld,%ld,%u,"
        "%u,%u,%u,%d,"
        "%u,%u,%u,"
        "%ld,%ld,%ld,%ld,"
        "%u,%u\n",
        static_cast<unsigned long>(e.uptimeMs),
        static_cast<long long>(e.wallUtc),
        static_cast<unsigned long>(e.spanMs),
        static_cast<unsigned long>(e.count),
        static_cast<unsigned long>(e.peak),
        static_cast<unsigned>(e.samples),

        static_cast<unsigned>(e.motionEvents),
        static_cast<unsigned>(e.sigMotion),
        static_cast<long>(e.stepDelta),

        static_cast<int>(e.hrMeanX10),
        static_cast<int>(e.hrMinX10),
        static_cast<unsigned>(e.hrSamples),
        static_cast<unsigned>(e.hrSource),

        static_cast<unsigned>(e.wornPct),
        static_cast<unsigned>(e.wornEdges),
        static_cast<int>(e.battPctX10),
        e.charging ? 1 : 0,

        // Reserved and always absent today -- see Engine::Epoch. Written every
        // row anyway, so the column exists in every night already on disk and
        // a future HRV producer needs no schema bump and no re-recording.
        static_cast<long>(e.rmssdX10),
        static_cast<long>(e.sdnnX10),
        static_cast<unsigned>(e.rrCount),

        // Delivery and power -- schema 2. What the epoch was built from, rather
        // than what it measured.
        static_cast<unsigned>(e.accBatches),
        static_cast<unsigned>(e.accMaxGapMs),
        static_cast<unsigned>(e.touchSamples),
        static_cast<int>(e.hrTrustX10),

        static_cast<unsigned>(e.hrexOptical),
        static_cast<unsigned>(e.hrexExternal),
        static_cast<unsigned>(e.hrexUnknown),

        static_cast<long>(e.battMv),
        static_cast<long>(e.battMaX10),
        static_cast<long>(e.battAvgMaX10),
        static_cast<long>(e.battMah),

        static_cast<unsigned>(e.wakes),
        static_cast<unsigned>(e.msgs));

    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        LOG_WARNING("epoch row did not fit; dropped\n");
        return false;
    }

    if (!append(mPath, line, static_cast<size_t>(n))) {
        return false;
    }

    mEpochs++;
    mLastUptimeMs = e.uptimeMs;
    mLastWallUtc  = e.wallUtc;

    // Rewritten after every flush, not on a slower cadence. A state file that
    // lagged the data by even one epoch would resume the night at the wrong
    // length, and time in bed is built from that length.
    return writeState(flags);
}


// -- Finishing ------------------------------------------------------------------

namespace {

/// Emit a metric, or JSON `null` when it was not measured.
///
/// Never zero. A night that failed the worn gate has no total sleep time, and
/// `0` would be read as a measurement of a terrible night rather than as the
/// absence of a claim.
void addOrNull(SDK::JsonStreamWriter &w, const char *key, int32_t v)
{
    if (v == Engine::kAbsent) {
        w.addNull(key);
    } else {
        w.add(key, static_cast<int32_t>(v));
    }
}

const char *wornName(Engine::WornVerdict v)
{
    switch (v) {
        case Engine::WornVerdict::Worn:    return "worn";
        case Engine::WornVerdict::NotWorn: return "not-worn";
        default:                           return "uncertain";
    }
}

} // namespace

bool NightStore::finishNight(const Engine::NightSummary &s,
                             const char *bandMethod, bool bandUsedHr,
                             const char *hrMode)
{
    if (!isOpen()) {
        return false;
    }

    // -- The summary JSON ---------------------------------------------------
    char jsonPath[kMaxNightPath];
    std::snprintf(jsonPath, sizeof(jsonPath), "%s", mPath);
    const size_t len = std::strlen(jsonPath);
    if (len > 4) {
        std::snprintf(jsonPath + len - 4, 6, ".json");
    }

    bool jsonOk = false;
    {
        std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(jsonPath);
        if (file && file->open(true, true)) {
            SDK::JsonStreamWriter w(file.get());

            w.startMap();
            w.add("schema", static_cast<int32_t>(kSummaryJsonSchema));
            w.add("app", "SleepLab");

            // Every file says how it was produced. A band drawn last month by a
            // different rule is a different measurement, and a summary that
            // does not carry its method cannot be compared with one that does.
            w.startMap("method");
            w.add("scorer", "cole-kripke-1992 + webster-1982 rescoring");
            w.add("epoch_sec", static_cast<int32_t>(Engine::kEpochMs / 1000));
            w.add("scoring_epoch_sec",
                  static_cast<int32_t>(Engine::kScoringEpochMs / 1000));
            w.add("restfulness_band", bandMethod);
            w.add("restfulness_used_hr", bandUsedHr);
            w.add("hr_mode", hrMode);
            // The app that produced this file. A night recorded by a different
            // build is a different measurement -- a threshold moved, a filter
            // changed -- and a file that does not say which build wrote it cannot
            // be compared with one that does. `kAppVersion` is bumped by hand
            // whenever anything that would move a number changes.
            w.add("app_version", kAppVersion);
            // The constants that scored *this* night, so it can be re-scored
            // offline against the counts in its own CSV even after they change.
            // Without them an old night is uninterpretable the moment a threshold
            // moves, which is precisely what the diary calibration is going to do.
            w.startMap("constants");
            w.add("count_scale_x1e6",
                  static_cast<int32_t>(Engine::SleepWakeScorer::kCountScale *
                                       1000000.0f + 0.5f));
            w.add("threshold_x1e3",
                  static_cast<int32_t>(Engine::SleepWakeScorer::kThreshold *
                                       1000.0f + 0.5f));
            w.add("p_x1e6", static_cast<int32_t>(
                                Engine::SleepWakeScorer::kP * 1000000.0f + 0.5f));
            w.add("min_samples_per_epoch",
                  static_cast<int32_t>(
                      Engine::SleepWakeScorer::kMinSamplesPerEpoch));
            w.add("min_worn_pct",
                  static_cast<int32_t>(Engine::SleepWakeScorer::kMinWornPct));
            w.add("movement_floor",
                  static_cast<int32_t>(Engine::NightAnalyser::kMovementFloor));
            w.add("micro_movement_floor",
                  static_cast<int32_t>(Engine::WornGate::kMicroMovementFloor));
            w.add("gate_min_worn_pct",
                  static_cast<int32_t>(Engine::WornGate::kMinWornPct));
            w.add("gate_min_plausible_pct",
                  static_cast<int32_t>(Engine::WornGate::kMinPlausiblePct));
            w.add("onset_run_min",
                  static_cast<int32_t>(Engine::NightAnalyser::kOnsetRunMin));
            w.endMap();
            // Stated in the file itself, not only in a README the file will be
            // separated from.
            w.add("validated_against",
                  "synthetic fixtures only; no polysomnography");
            w.add("bias",
                  "actigraphy over-reports sleep: lying still awake scores as "
                  "sleep");
            w.endMap();

            w.startMap("provenance");
            w.add("worn", wornName(s.worn));
            w.add("worn_ok", s.worn == Engine::WornVerdict::Worn);
            w.add("interrupted", s.interruption != 0);
            w.add("interruption_bits", static_cast<int32_t>(s.interruption));
            w.add("charging",  (s.interruption & Engine::Interruption::kCharging)  != 0);
            w.add("resumed",   (s.interruption & Engine::Interruption::kResumed)   != 0);
            w.add("clock_jump",(s.interruption & Engine::Interruption::kClockJump) != 0);
            w.add("data_gap",  (s.interruption & Engine::Interruption::kDataGap)   != 0);
            w.add("truncated", (s.interruption & Engine::Interruption::kTruncated) != 0);
            // The one bit that is about the record rather than about the night:
            // the numbers below describe minutes that were measured, and the CSV
            // is missing some of them.
            w.add("write_failed",
                  (s.interruption & Engine::Interruption::kWriteFailed) != 0);
            w.add("epochs",      static_cast<int32_t>(s.epochs));
            w.add("unscorable",  static_cast<int32_t>(s.unscorable));
            // What the night was actually built from. A count is only comparable
            // with another count taken at a similar delivered rate, and the rate
            // is neither the requested one nor constant between nights -- so the
            // only way to know whether two nights can be compared is for each to
            // say. Also the one column that separates "delivery stopped" from
            // "delivery degraded", which are different problems.
            addOrNull(w, "acc_samples_min",    s.accSamplesMin);
            addOrNull(w, "acc_samples_median", s.accSamplesMedian);
            addOrNull(w, "acc_hz_x10",         s.accHzX10);
            w.endMap();

            w.startMap("sleep");
            // hasSleep is the single flag a reader should branch on. Every
            // field below is null when it is false.
            w.add("reported", s.hasSleep);
            addOrNull(w, "time_in_bed_min",   s.timeInBedMin);
            addOrNull(w, "onset_latency_min", s.onsetLatencyMin);
            addOrNull(w, "total_sleep_min",   s.totalSleepMin);
            addOrNull(w, "waso_min",          s.wasoMin);
            addOrNull(w, "awakenings",        s.awakenings);
            addOrNull(w, "efficiency_pct",    s.efficiencyPct);
            addOrNull(w, "movement_index_pct", s.movementIndexPct);
            // The measurement, next to the estimate. Both, always.
            addOrNull(w, "still_in_bed_min",  s.stillInBedMin);
            addOrNull(w, "onset_epoch",       s.onsetEpoch);
            addOrNull(w, "final_wake_epoch",  s.finalWakeEpoch);

            w.startArray("awakening_min");
            for (size_t i = 0; i < s.awakeningsListed; ++i) {
                w.add(static_cast<int32_t>(s.awakeningMin[i]));
            }
            w.endArray();
            w.endMap();

            // Absolute values only. Turning these into "4 bpm above normal"
            // needs a personal baseline, which lives in the index and is
            // deliberately not folded in here -- a summary must never carry a
            // comparison it has not earned.
            w.startMap("heart_rate");
            addOrNull(w, "min_x10",   s.hrMinX10);
            addOrNull(w, "mean_x10",  s.hrMeanX10);
            addOrNull(w, "min_epoch", s.hrMinEpoch);
            w.add("epochs", static_cast<int32_t>(s.hrEpochs));
            w.endMap();

            // Named so nobody has to guess which columns the epoch CSV has.
            w.startMap("files");
            w.add("epochs_csv", mPath);
            w.add("epochs_csv_schema", static_cast<int32_t>(kEpochCsvSchema));
            w.endMap();

            w.endMap();

            jsonOk = !w.isError();
            file->flush();
            file->close();
        }
    }
    if (!jsonOk) {
        LOG_WARNING("could not write %s\n", jsonPath);
    }

    // -- The index row ------------------------------------------------------
    if (!mKernel.fs.exist(kIndexPath)) {
        append(kIndexPath, kIndexHeader, sizeof(kIndexHeader) - 1);
    }

    // Where in the night the HR minimum fell, as a percentage. Absolute epoch
    // index would not compare across nights of different lengths, and the
    // timing is as personal and as informative as the value.
    int32_t hrMinAtPct = Engine::kAbsent;
    if (s.hrMinEpoch != Engine::kAbsent && s.epochs > 0) {
        hrMinAtPct = static_cast<int32_t>(s.hrMinEpoch * 100 /
                                          static_cast<int32_t>(s.epochs));
    }

    char row[192];
    const int n = std::snprintf(row, sizeof(row),
                                "%lld,%ld,%ld,%ld,%ld,%ld,%u,%u\n",
                                // The session's *start*, which is the night's
                                // identity: it names the file and it is what
                                // the history sorts on. Not the last epoch's
                                // clock.
                                static_cast<long long>(mStartWallUtc),
                                static_cast<long>(s.timeInBedMin),
                                static_cast<long>(s.totalSleepMin),
                                static_cast<long>(s.efficiencyPct),
                                static_cast<long>(s.hrMinX10),
                                static_cast<long>(hrMinAtPct),
                                static_cast<unsigned>(s.worn),
                                static_cast<unsigned>(s.interruption));
    bool indexOk = false;
    if (n > 0 && static_cast<size_t>(n) < sizeof(row)) {
        indexOk = append(kIndexPath, row, static_cast<size_t>(n));
    }

    // Cleared last, and unconditionally, and the difference between those two
    // words is the finding here.
    //
    // Last, so a *crash* anywhere above resumes the night rather than losing it.
    // That much holds. But a refused write is not a crash: control reaches this
    // line, and keeping the state file to protect the night would be worse than
    // losing the index row. The relaunch resumes into a night that has already
    // been summarised, and 07:00 is inside a 21:00-11:00 bedtime window, so the
    // session stays open all morning appending the wearer's breakfast to last
    // night's CSV and then files a "night" that ran until eleven. Splicing a
    // morning onto a night is a worse outcome than a missing history row.
    //
    // So the night is closed either way, and what changes is that the failure is
    // no longer silent: the caller raises `kWriteFailed`, the report's first line
    // becomes INCOMPLETE, and the epoch CSV is still on the volume to be copied
    // off. What is lost is the history row and the baseline sample, and a missing
    // night in the history is visible.
    mKernel.fs.remove(kStatePath);
    mPath[0] = '\0';
    mEpochs  = 0;

    return jsonOk && indexOk;
}

void NightStore::discardNight()
{
    // The epoch CSV is deliberately left on disk. It is real data somebody may
    // want, and deleting a user's files is not this app's job -- what matters
    // is that no index row is written, so it never reaches the history or the
    // baseline.
    LOG_INFO("night discarded (too short): %s kept, not indexed\n", mPath);
    mKernel.fs.remove(kStatePath);
    mPath[0] = '\0';
    mEpochs  = 0;
}


// -- History --------------------------------------------------------------------

size_t NightStore::readHistory(IndexRow *out, size_t maxOut) const
{
    if (out == nullptr || maxOut == 0) {
        return 0;
    }

    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (!mKernel.fs.objectInfo(kIndexPath, info) || info.isDir || info.size == 0) {
        return 0;
    }

    // Read the tail only. At ~50 bytes a row a decade is ~180 KB, and the
    // history never needs more than the window -- so the read is bounded by
    // what is wanted rather than by how long the app has been installed.
    constexpr size_t kTailBytes = 4096;
    const size_t     want  = info.size < kTailBytes ? info.size : kTailBytes;
    const size_t     start = info.size - want;

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kIndexPath);
    if (!file || !file->open()) {
        return 0;
    }
    if (start > 0 && !file->seek(start)) {
        file->close();
        return 0;
    }

    std::unique_ptr<char[]> buf(new (std::nothrow) char[want + 1]);
    if (!buf) {
        file->close();
        return 0;
    }
    size_t read = 0;
    const bool ok = file->read(buf.get(), want, read);
    file->close();
    if (!ok || read == 0) {
        return 0;
    }
    buf[read] = '\0';

    // Rows land oldest-first in a ring, then are unrolled into `out` so the
    // caller always gets oldest-first regardless of how many there were.
    IndexRow ring[kMaxHistory];
    size_t   count = 0, next = 0;

    const char *p = buf.get();
    // A tail read almost always starts mid-row. Skip to the first newline so a
    // half row is never parsed -- its leading field would be a truncated
    // timestamp, which parses fine and is wrong.
    if (start > 0) {
        const char *nl = std::strchr(p, '\n');
        if (nl == nullptr) {
            return 0;
        }
        p = nl + 1;
    }

    while (*p != '\0') {
        const char *nl  = std::strchr(p, '\n');
        const size_t len = (nl != nullptr) ? static_cast<size_t>(nl - p)
                                           : std::strlen(p);
        if (len > 0 && *p != '#') {
            long long lv = 0;
            long tib = 0, tst = 0, eff = 0, hrmin = 0, hrat = 0;
            unsigned worn = 0, interruption = 0;
            if (std::sscanf(p, "%lld,%ld,%ld,%ld,%ld,%ld,%u,%u",
                            &lv, &tib, &tst, &eff, &hrmin, &hrat,
                            &worn, &interruption) == 8) {
                IndexRow &r = ring[next];
                r.startUtc      = static_cast<int64_t>(lv);
                r.timeInBedMin  = static_cast<int32_t>(tib);
                r.totalSleepMin = static_cast<int32_t>(tst);
                r.efficiencyPct = static_cast<int32_t>(eff);
                r.hrMinX10      = static_cast<int32_t>(hrmin);
                r.hrMinAtPct    = static_cast<int32_t>(hrat);
                r.worn          = static_cast<uint8_t>(worn);
                r.interruption  = static_cast<uint16_t>(interruption);
                next = (next + 1) % kMaxHistory;
                if (count < kMaxHistory) {
                    count++;
                }
            }
        }
        if (nl == nullptr) {
            break;
        }
        p = nl + 1;
    }

    const size_t emit  = (count < maxOut) ? count : maxOut;
    // Oldest of the ones being emitted, so a caller asking for fewer than are
    // held gets the *newest* few rather than the oldest few.
    const size_t first = (next + kMaxHistory - emit) % kMaxHistory;
    for (size_t i = 0; i < emit; ++i) {
        out[i] = ring[(first + i) % kMaxHistory];
    }
    return emit;
}

void NightStore::loadBaseline(Engine::BaselineStore &out) const
{
    IndexRow rows[kMaxHistory];
    const size_t n = readHistory(rows, kMaxHistory);

    out = Engine::BaselineStore{};
    for (size_t i = 0; i < n; ++i) {
        // A night that failed the worn gate never reaches the baseline. A
        // nightstand's flawless efficiency would poison it for four weeks and
        // make every real night afterwards look bad.
        if (rows[i].worn != static_cast<uint8_t>(Engine::WornVerdict::Worn)) {
            continue;
        }
        Engine::BaselineStore::Sample s;
        s.hrMinX10      = rows[i].hrMinX10;
        s.efficiencyPct = rows[i].efficiencyPct;
        s.totalSleepMin = rows[i].totalSleepMin;
        s.hrMinAtPct    = rows[i].hrMinAtPct;
        out.add(s);
    }
}

} // namespace SleepLab
