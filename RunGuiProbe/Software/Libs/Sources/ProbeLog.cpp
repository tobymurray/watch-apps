/**
 ******************************************************************************
 * @file    ProbeLog.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The probe's only output: a line-per-event file, read over USB.
 ******************************************************************************
 */

#include "ProbeLog.hpp"

#include <cstdarg>
#include <cstdio>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

namespace Probe
{

Log::Log(SDK::Kernel &kernel, const char *path)
    : mKernel(kernel)
    , mPath(path)
{
}

void Log::line(const char *fmt, ...)
{
    char text[kLineBytes];

    va_list args;
    va_start(args, fmt);
    const int len = std::vsnprintf(text, sizeof text - 1, fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }

    // vsnprintf returns what it *would* have written, so a truncated line
    // reports more than the buffer holds. Clamp before using it as a length, or
    // the write below reads past the end of the buffer.
    size_t used = static_cast<size_t>(len);
    if (used > sizeof text - 2) {
        used = sizeof text - 2;
    }

    text[used]     = '\n';
    text[used + 1] = '\0';

    append(text, used + 1);
}

void Log::append(const char *text, size_t len)
{
    // Where the write goes, and whether anything already there is kept. A file
    // over the cap is started again from empty rather than grown: see
    // kMaxBytes.
    size_t at      = 0;
    bool   rolling = false;

    SDK::Interface::IFileSystem::ObjectInfo info {};
    if (mKernel.fs.objectInfo(mPath, info)) {
        if (info.size >= kMaxBytes) {
            rolling = true;
        } else {
            at = info.size;
        }
    }

    std::unique_ptr<SDK::Interface::IFile> file = mKernel.fs.file(mPath);
    if (!file) {
        return;
    }

    // `override` truncates. It is wanted only for the roll and for the first
    // write to a file that is not there yet -- in both cases there is nothing
    // to preserve.
    const bool truncate = rolling || (at == 0);
    if (!file->open(/*wMode=*/true, /*override=*/truncate)) {
        return;
    }

    // Every close() below is paired with the open() above, including on the
    // failure paths: a handle left open holds a lock slot on this filesystem,
    // and a probe that exhausts them after a few hundred lines would look
    // exactly like the app dying, which is one of the outcomes being measured.
    if (at != 0 && !file->seek(at)) {
        file->close();
        return;
    }

    size_t written = 0;
    file->write(text, len, written);
    file->flush();
    file->close();
}

} // namespace Probe
