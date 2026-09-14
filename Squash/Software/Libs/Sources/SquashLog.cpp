/**
 ******************************************************************************
 * @file    SquashLog.cpp
 * @brief   The file that explains a session. Spec in the header.
 ******************************************************************************
 */

#include "SquashLog.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

namespace {

int32_t hundredths(float v)
{
    const float scaled = v * 100.0f;
    return static_cast<int32_t>(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
}

/// True when @p path already begins with @p header.
///
/// Read rather than assumed: the only thing that can say whether a file's
/// columns are this build's is the file.
bool headerMatches(const SDK::Kernel& kernel, const char* path, const char* header)
{
    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open(false, false)) {
        return true;  // unreadable: leave it alone rather than truncate it
    }

    const size_t want = std::strlen(header);
    char         head[320]{};
    size_t       got = 0;
    const bool   ok  = file->read(head, (want < sizeof(head)) ? want : sizeof(head) - 1, got);
    file->close();

    return ok && got == want && std::memcmp(head, header, want) == 0;
}

/// Append @p text, restarting the file first if it has reached @p cap.
///
/// Failure is ignored throughout; see the header for why.
/// @return true when the file was restarted, so a caller can say so elsewhere.
bool append(const SDK::Kernel& kernel,
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

    // A header written by an earlier build names fewer columns than a newer
    // build's rows carry, and the file is append-only, so every column after
    // the last shared one silently shifts. Measured: adding hr_external_n gave
    // a 25-column header above a 26-column row, and a reader took the value as
    // missing rather than as misaligned. So a header that no longer matches is
    // grounds to start the file again, exactly as reaching the cap is.
    const bool stale = exists && header != nullptr && !headerMatches(kernel, path, header);
    const bool over  = exists && (info.size >= cap || stale);
    const bool fresh = !exists || over;

    std::unique_ptr<SDK::Interface::IFile> file = kernel.fs.file(path);
    if (!file || !file->open(true, over)) {
        return false;
    }

    // open(write, override=false) positions at offset 0, not at the end -- both
    // the SDK's fake and FatFs -- so the seek is what makes this an append.
    // Without it the file keeps only its newest line.
    if (!over && !file->seek(file->size())) {
        file->close();
        return false;
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
    return over;
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
