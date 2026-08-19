#include "BenchLog.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace MapLab
{

void BenchLog::append(const char* text, size_t length)
{
    auto file = mKernel.fs.file(kLogPath);
    if (!file) {
        ++mFailures;
        return;
    }
    // Opened for write without override, then seeked to the end: the log is an
    // append-only record across every launch, and a truncating open would
    // throw away the run that the watchdog bench just restarted the device in
    // the middle of.
    if (!file->open(true, false)) {
        ++mFailures;
        return;
    }
    if (!file->seek(file->size())) {
        file->close();
        ++mFailures;
        return;
    }
    size_t written = 0;
    if (!file->write(text, length, written) || written != length) {
        ++mFailures;
    } else {
        mBytes += static_cast<uint32_t>(written);
    }
    file->flush();
    file->close();
}

uint32_t BenchLog::beginRun(const char* buildVersion, const char* subject)
{
    char line[256];

    const bool fresh = !mKernel.fs.exist(kLogPath);
    if (fresh) {
        const int n = std::snprintf(
            line, sizeof(line),
            "H,%lu,maplab bench log; see Software/Libs/Header/BenchLog.hpp\n",
            static_cast<unsigned long>(kLogSchema));
        if (n > 0) {
            append(line, static_cast<size_t>(n));
        }
    }

    // The run index is derived from the clock rather than counted, because a
    // counter would have to survive a restart and the restart is one of the
    // things being measured. Uptime in seconds is unique enough within a boot
    // and readable enough in a diff.
    mRunIndex = mKernel.sys.getTimeMs() / 1000u;

    const long long wall = static_cast<long long>(std::time(nullptr));
    const int n = std::snprintf(line, sizeof(line), "R,%lu,%lu,%lld,%s,%s\n",
                                static_cast<unsigned long>(mRunIndex),
                                static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                                wall, buildVersion, subject);
    if (n > 0) {
        append(line, static_cast<size_t>(n));
        ++mRows;
    }
    return mRunIndex;
}

void BenchLog::write(const BenchRow& row)
{
    char line[256];
    const int n = std::snprintf(
        line, sizeof(line), "B,%lu,%lu,%s,%s,%s,%lu,%lu,%lu,%d,%ld,%ld,%ld,%s\n",
        static_cast<unsigned long>(mRunIndex),
        static_cast<unsigned long>(mKernel.sys.getTimeMs()),
        row.group, row.id, row.name,
        static_cast<unsigned long>(row.iterations),
        static_cast<unsigned long>(row.elapsedMs),
        static_cast<unsigned long>(row.usPerOp),
        row.valid ? 1 : 0,
        static_cast<long>(row.a), static_cast<long>(row.b), static_cast<long>(row.c),
        row.note);
    if (n <= 0) {
        ++mFailures;
        return;
    }
    append(line, static_cast<size_t>(n));
    ++mRows;
}

} // namespace MapLab
