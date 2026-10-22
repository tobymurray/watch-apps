#include "Lifecycle.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace Slumber
{

void Lifecycle::line(const char *tag, const char *fmt, ...)
{
    char detail[160] = {};
    if (fmt != nullptr) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(detail, sizeof(detail), fmt, args);
        va_end(args);
    }

    char text[224];
    const int n = std::snprintf(text, sizeof(text), "%lu %s %s\n",
                                static_cast<unsigned long>(mKernel.sys.getTimeMs()),
                                (tag != nullptr) ? tag : "?", detail);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text)) {
        return;
    }

    mKernel.fs.mkdir(kLogDir);

    SDK::Interface::IFileSystem::ObjectInfo info {};
    const bool over = mKernel.fs.objectInfo(kLogPath, info) && !info.isDir &&
                      info.size >= kLogMaxBytes;

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(kLogPath);
    if (!file || !file->open(true, over)) {
        return;
    }
    // open(write, override=false) positions at offset 0, so the seek is what
    // makes this an append (SleepLab ledger row P6).
    if (!over && !file->seek(file->size())) {
        file->close();
        return;
    }

    size_t written = 0;
    file->write(text, static_cast<size_t>(n), written);
    file->flush();
    file->close();
}

uint32_t Lifecycle::bumpLaunchCount()
{
    mKernel.fs.mkdir(kLogDir);

    uint32_t count = 0;
    {
        std::unique_ptr<SDK::Interface::IFile> in = mKernel.fs.file(kLaunchCountPath);
        if (in && in->open(false, false)) {
            char buf[16] = {};
            size_t got = 0;
            if (in->read(buf, sizeof(buf) - 1, got) && got > 0) {
                count = static_cast<uint32_t>(std::strtoul(buf, nullptr, 10));
            }
            in->close();
        }
    }

    ++count;

    std::unique_ptr<SDK::Interface::IFile> out = mKernel.fs.file(kLaunchCountPath);
    if (out && out->open(true, true)) {
        char buf[16];
        const int n = std::snprintf(buf, sizeof(buf), "%lu\n", static_cast<unsigned long>(count));
        size_t written = 0;
        if (n > 0) {
            out->write(buf, static_cast<size_t>(n), written);
        }
        out->flush();
        out->close();
    }
    return count;
}

} // namespace Slumber
