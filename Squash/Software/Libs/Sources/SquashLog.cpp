/**
 ******************************************************************************
 * @file    SquashLog.cpp
 * @brief   The two files that explain a session. Spec in the header.
 ******************************************************************************
 */

#include "SquashLog.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

namespace {

/// The header the session CSV is written with, and the order its rows follow.
constexpr char kSessionsHeader[] =
    "utc,active_s,hr_mean_x100,hr_max_x100,hr_covered_s,hr_source,segmented,"
    "rally_count,rally_s,rest_s,off_court_s,rec_short_x100,rec_short_n,"
    "rec_long_x100,rec_long_n,discarded,profile_sessions,calibration,"
    "imu_stop,imu_samples,imu_bytes,markers,hr_rows,intact,saved\n";

int32_t hundredths(float v)
{
    const float scaled = v * 100.0f;
    return static_cast<int32_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

/// Append @p text, restarting the file first if it has reached @p cap.
///
/// Failure is ignored throughout; see the header for why.
void append(const SDK::Kernel& kernel,
            const char*        path,
            const char*        header,
            const char*        rotatedNote,
            size_t             cap,
            const char*        text,
            size_t             len)
{
    kernel.fs.mkdir(kSquashDiagDir);

    SDK::Interface::IFileSystem::ObjectInfo info{};
    const bool exists = kernel.fs.objectInfo(path, info) && !info.isDir;
    const bool over   = exists && info.size >= cap;
    const bool fresh  = !exists || over;

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open(true, over)) {
        return;
    }

    // open(write, override=false) positions at offset 0, not at the end -- both
    // the SDK's fake and FatFs -- so the seek is what makes this an append.
    // Without it the file keeps only its newest line.
    if (!over && !file->seek(file->size())) {
        file->close();
        return;
    }

    size_t wrote = 0;
    if (fresh) {
        if (over && rotatedNote != nullptr) {
            file->write(rotatedNote, std::strlen(rotatedNote), wrote);
        }
        if (header != nullptr) {
            file->write(header, std::strlen(header), wrote);
        }
    }

    file->write(text, len, wrote);
    file->flush();
    file->close();
}

} // namespace

void SquashLog::line(const char* tag, const char* fmt, ...)
{
    char detail[160]{};
    if (fmt != nullptr) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(detail, sizeof(detail), fmt, args);
        va_end(args);
    }

    char      text[256];
    const int n = std::snprintf(text, sizeof(text), "%lu %lld %s %s\n",
                                static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                                static_cast<long long>(std::time(nullptr)),
                                (tag != nullptr) ? tag : "?",
                                detail);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text)) {
        return;
    }

    append(mKernel, kSquashLogPath, nullptr,
           "0 -1 rotated log reached its cap and was restarted\n",
           kSquashLogMaxBytes, text, static_cast<size_t>(n));
}

void SquashLog::session(const Session& s)
{
    const SquashSessionRecord& r = s.record;

    char      row[320];
    const int n = std::snprintf(
        row, sizeof(row),
        "%lu,%lu,%ld,%ld,%lu,%u,%u,%lu,%lu,%lu,%lu,%ld,%u,%ld,%u,%u,%lu,%lu,%u,%lu,%lu,%u,%u,%u,%u\n",
        static_cast<unsigned long>(r.startedUtc), static_cast<unsigned long>(r.activeS),
        static_cast<long>(hundredths(r.hrMean)), static_cast<long>(hundredths(r.hrMax)),
        static_cast<unsigned long>(r.hrCoveredS), r.hrSource, r.segmented,
        static_cast<unsigned long>(r.rallyCount), static_cast<unsigned long>(r.rallyS),
        static_cast<unsigned long>(r.restS), static_cast<unsigned long>(r.offCourtS),
        static_cast<long>(hundredths(r.recoveryShortMean)), r.recoveryShortN,
        static_cast<long>(hundredths(r.recoveryLongMean)), r.recoveryLongN,
        r.discardedWindows,
        static_cast<unsigned long>(s.profileSessions), static_cast<unsigned long>(s.calibration),
        s.imuStop, static_cast<unsigned long>(s.imuSamples),
        static_cast<unsigned long>(s.imuBytes), s.markers, s.hrRows,
        s.recordingIntact, s.profileSaved);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(row)) {
        return;
    }

    append(mKernel, kSquashSessionsPath, kSessionsHeader,
           "# rotated: reached the cap and was restarted\n",
           kSquashSessionsMaxBytes, row, static_cast<size_t>(n));
}
