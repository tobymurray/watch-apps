/**
 ******************************************************************************
 * @file    ProbeLog.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The probe's on-disk record. Format spec is in ProbeLog.hpp.
 ******************************************************************************
 */

#include "ProbeLog.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#define LOG_MODULE_PRX      "ProbeLog"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace Probe
{

/// The `H` row. Column names, in order, for every `M` row in the file.
///
/// Kept as one literal rather than assembled from the struct, because the
/// point of it is to be the thing a reader checks the parser against -- a
/// header generated from the same source as the row cannot catch a column
/// going out of step with its name.
static constexpr char kHeaderLine[] =
    "H,schema,1,cols,"
    "kind,uptime_ms,wall_utc,local_min,span_ms,"
    "acc_n,acc_ts_span_ms,acc_max_gap_ms,acc_batches,"
    "touch_n,touch_worn_n,touch_edges,"
    "motion_n,motion_no,motion_mot,motion_sig,"
    "ar_n,ar_still,ar_walk,ar_run,"
    "hr_n,hr_mean_x10,hr_min,hr_max,hr_trust_x10,"
    "hrex_n,hrex_opt,hrex_ext,hrex_unk,"
    "beat_n,ppg_n,ppg_ts_span_ms,"
    "spo2_n,spo2_last_x10,"
    "step_total,step_delta,"
    "batt_pct_x10,charging,usb,batt_mv,batt_ma_x10,batt_avg_ma_x10,batt_mah,"
    "wakes,msgs\n";

Log::Log(const SDK::Kernel &kernel, const char *path)
    : mKernel(kernel)
    , mPath(path)
{
}

bool Log::append(const char *text, size_t len)
{
    if (len == 0) {
        return true;
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
    // open(write, override=false) creates a missing file and keeps an existing
    // one -- but it does NOT position at the end. Both the SDK's in-memory
    // fake and FatFs open at offset 0, so writing straight away overwrites the
    // night from its first byte. The seek is what makes this an append, and it
    // is not optional: without it the log silently keeps only its newest row.
    if (!file || !file->open(true, false)) {
        mFailures++;
        return false;
    }

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
    return true;
}

bool Log::begin(uint32_t uptimeMs, int64_t wallUtc, const char *hrMode)
{
    bool ok = true;

    if (!mKernel.fs.exist(mPath)) {
        ok = append(kHeaderLine, sizeof(kHeaderLine) - 1);
    }

    char line[kLineMax];
    const int n = std::snprintf(line, sizeof(line),
                                "R,%lu,%lld,schema=%lu,hr=%s\n",
                                static_cast<unsigned long>(uptimeMs),
                                static_cast<long long>(wallUtc),
                                static_cast<unsigned long>(kLogSchema),
                                hrMode != nullptr ? hrMode : "?");
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        mFailures++;
        return false;
    }

    return append(line, static_cast<size_t>(n)) && ok;
}

bool Log::write(const MinuteRow &r)
{
    char line[kLineMax];

    // One snprintf rather than a builder. It is long, but it puts the column
    // order in exactly one place next to kHeaderLine, where the two can be
    // read against each other.
    const int n = std::snprintf(
        line, sizeof(line),
        "M,%lu,%lld,%d,%lu,"
        "%ld,%ld,%ld,%ld,"
        "%ld,%ld,%ld,"
        "%ld,%ld,%ld,%ld,"
        "%ld,%ld,%ld,%ld,"
        "%ld,%ld,%ld,%ld,%ld,"
        "%ld,%ld,%ld,%ld,"
        "%ld,%ld,%ld,"
        "%ld,%ld,"
        "%lld,%ld,"
        "%ld,%d,%d,%ld,%ld,%ld,%ld,"
        "%ld,%ld\n",
        static_cast<unsigned long>(r.uptimeMs),
        static_cast<long long>(r.wallUtc),
        static_cast<int>(r.localMin),
        static_cast<unsigned long>(r.spanMs),

        static_cast<long>(r.accN),
        static_cast<long>(r.accTsSpanMs),
        static_cast<long>(r.accMaxGapMs),
        static_cast<long>(r.accBatches),

        static_cast<long>(r.touchN),
        static_cast<long>(r.touchWornN),
        static_cast<long>(r.touchEdges),

        static_cast<long>(r.motionN),
        static_cast<long>(r.motionNo),
        static_cast<long>(r.motionMot),
        static_cast<long>(r.motionSig),

        static_cast<long>(r.arN),
        static_cast<long>(r.arStill),
        static_cast<long>(r.arWalk),
        static_cast<long>(r.arRun),

        static_cast<long>(r.hrN),
        static_cast<long>(r.hrMeanX10),
        static_cast<long>(r.hrMin),
        static_cast<long>(r.hrMax),
        static_cast<long>(r.hrTrustX10),

        static_cast<long>(r.hrExN),
        static_cast<long>(r.hrExOptN),
        static_cast<long>(r.hrExExtN),
        static_cast<long>(r.hrExUnkN),

        static_cast<long>(r.beatN),
        static_cast<long>(r.ppgN),
        static_cast<long>(r.ppgTsSpanMs),

        static_cast<long>(r.spo2N),
        static_cast<long>(r.spo2LastX10),

        static_cast<long long>(r.stepTotal),
        static_cast<long>(r.stepDelta),

        static_cast<long>(r.battPctX10),
        static_cast<int>(r.charging),
        static_cast<int>(r.usb),
        static_cast<long>(r.battMv),
        static_cast<long>(r.battMaX10),
        static_cast<long>(r.battAvgMaX10),
        static_cast<long>(r.battMah),

        static_cast<long>(r.wakes),
        static_cast<long>(r.msgs));

    if (n <= 0 || static_cast<size_t>(n) >= sizeof(line)) {
        // Truncation would produce a half row, and a half row is worse than a
        // missing one: a parser can match the wrong half of it.
        LOG_WARNING("row did not fit in %u bytes, dropped\n",
                    static_cast<unsigned>(sizeof(line)));
        mFailures++;
        return false;
    }

    return append(line, static_cast<size_t>(n));
}

} // namespace Probe
