#include "DebugLog.hpp"

#if NOTIFY_TOGGLE_DEBUG_LOG

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace DebugLog
{

namespace
{
constexpr size_t kLineCap = 256;
constexpr size_t kLogPathCap = 32;
char sLogPath[kLogPathCap] = "debug.log";
// A cap, not a rotation policy: past this something is looping, and the right
// answer is to stop rather than to keep filling the wearer's storage.
constexpr size_t kMaxLogFileBytes = 64 * 1024;

// Static, not local: this runs on the GUI task's 10 KB stack, called from
// paths that already hold their own File object and file buffer. One shared
// line is safe because nothing here is reentrant -- single-threaded app, and
// each call finishes before anything else could reuse it.
char sLineBuf[kLineCap];
bool sSaidFull = false;

void writeLine(SDK::Interface::IFileSystem &fs, const char *text, size_t len)
{
    auto file = fs.file(sLogPath);
    if (!file) {
        return;
    }
    // Write, no truncate: IFile::open's contract is "open-or-create WITHOUT
    // truncating... position at start", so an explicit seek to the current
    // end is what makes this an append rather than an overwrite from byte 0.
    if (!file->open(true, false)) {
        return;
    }
    const size_t currentSize = file->size();
    if (currentSize >= kMaxLogFileBytes) {
        // One line over the cap, once: a log that simply stopped would read as
        // a run that stopped, which is the wrong thing to conclude from it.
        if (!sSaidFull) {
            sSaidFull = true;
            static constexpr char kFull[] = "--- log full; later lines dropped ---\n";
            file->seek(currentSize);
            size_t n = 0;
            file->write(kFull, sizeof(kFull) - 1, n);
            file->flush();
        }
        file->close();
        return;
    }
    file->seek(currentSize);

    size_t bytesWritten = 0;
    file->write(text, len, bytesWritten);
    static constexpr char kNewline = '\n';
    file->write(&kNewline, 1, bytesWritten);
    file->flush();
    file->close();
}

} // namespace

void setLogPath(const char *path)
{
    std::strncpy(sLogPath, path, kLogPathCap - 1);
    sLogPath[kLogPathCap - 1] = '\0';
}

void append(SDK::Interface::IFileSystem &fs, const char *line)
{
    writeLine(fs, line, std::strlen(line));
}

void appendf(SDK::Interface::IFileSystem &fs, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(sLineBuf, sizeof(sLineBuf), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }
    const size_t len = (static_cast<size_t>(n) < sizeof(sLineBuf)) ? static_cast<size_t>(n) : sizeof(sLineBuf) - 1;
    writeLine(fs, sLineBuf, len);
}

} // namespace DebugLog

#endif // NOTIFY_TOGGLE_DEBUG_LOG
