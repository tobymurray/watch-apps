/**
 ******************************************************************************
 * @file    Diag.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The few lines on the volume that explain a night. Spec in the header.
 ******************************************************************************
 */

#include "Diag.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "WallClock.hpp"

namespace SleepLab
{

void Diag::line(const char *tag, const char *fmt, ...)
{
    char detail[160] = {};
    if (fmt != nullptr) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(detail, sizeof(detail), fmt, args);
        va_end(args);
    }

    char text[256];
    const int n = std::snprintf(text, sizeof(text), "%lu %lld %s %s\n",
                                static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                                static_cast<long long>(wallClockUtc()),
                                (tag != nullptr) ? tag : "?",
                                detail);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text)) {
        return;
    }

    mKernel.fs.mkdir(kDiagDir);

    // Past the cap, start again rather than growing. `override=true` truncates,
    // and the first line of the new file says what happened so a reader is not
    // left wondering where last month went.
    SDK::Interface::IFileSystem::ObjectInfo info {};
    const bool over = mKernel.fs.objectInfo(kDiagPath, info) && !info.isDir &&
                      info.size >= kDiagMaxBytes;

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kDiagPath);
    if (!file || !file->open(true, over)) {
        return;
    }
    if (over) {
        const char *note = "0 -1 rotated log reached its cap and was restarted\n";
        size_t wrote = 0;
        file->write(note, std::strlen(note), wrote);
    } else if (!file->seek(file->size())) {
        // open(write, override=false) positions at offset 0, not at the end --
        // both the SDK's fake and FatFs -- so the seek is what makes this an
        // append. Without it the log keeps only its newest line.
        file->close();
        return;
    }

    size_t written = 0;
    file->write(text, static_cast<size_t>(n), written);
    file->flush();
    file->close();
}

} // namespace SleepLab
