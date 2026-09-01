#include "DebugLog.hpp"

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
// Diagnostic session cap, not a rotation policy: this file is a temporary
// aid, so once it's this big something is looping and the right fix is to
// stop, not to keep growing it. ~64K is generous for one debug session's
// worth of the periodic re-read poll.
constexpr size_t kMaxLogFileBytes = 64 * 1024;

// Static, not local: this runs on the GUI task's 10 KB stack, called from
// deep inside SettingsFile's own read/write paths, which have their own
// static buffers for the same reason (see SettingsFile.cpp). One shared
// scratch line is safe because nothing here is reentrant or concurrent --
// single-threaded app, and each of these functions finishes writing and
// returns before anything else could reuse it.
char sLineBuf[kLineCap];
SDK::Interface::IFileSystem::ObjectInfo sDirItem;

/// Writes exactly `len` bytes of `text` plus a trailing newline. Does not
/// itself copy `text` anywhere -- the caller decides whether that needs a
/// buffer at all (append() doesn't; appendf()/appendBytes() format directly
/// into sLineBuf, so there is exactly one buffer in play, not one per call).
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

void appendBytes(SDK::Interface::IFileSystem &fs, const char *prefix, const char *data, size_t len)
{
    size_t n = 0;
    for (const char *p = prefix; *p != '\0' && n < sizeof(sLineBuf) - 6; ++p) {
        sLineBuf[n++] = *p;
    }
    sLineBuf[n++] = ':';
    sLineBuf[n++] = ' ';

    const size_t capped = (len < 150) ? len : 150;
    for (size_t i = 0; i < capped && n < sizeof(sLineBuf) - 4; ++i) {
        const char c = data[i];
        sLineBuf[n++] = (c >= 32 && c < 127) ? c : '.';
    }
    if (len > capped) {
        sLineBuf[n++] = '.';
        sLineBuf[n++] = '.';
        sLineBuf[n++] = '.';
    }
    writeLine(fs, sLineBuf, n);
}

void listDirectory(SDK::Interface::IFileSystem &fs, const char *path)
{
    appendf(fs, "listdir %s :", path);

    auto dir = fs.dir(path);
    if (!dir || !dir->open()) {
        append(fs, "  (could not open)");
        return;
    }

    int count = 0;
    while (dir->readNext(sDirItem)) {
        appendf(fs, "  %s%s (%zu bytes) hidden=%d system=%d",
                sDirItem.name, sDirItem.isDir ? "/" : "", sDirItem.size,
                sDirItem.isHidden ? 1 : 0, sDirItem.isSystem ? 1 : 0);
        if (++count >= 40) {
            append(fs, "  ...(truncated)");
            break;
        }
    }
    if (count == 0) {
        append(fs, "  (empty)");
    }
    dir->close();
}

void probeDriveRoots(SDK::Interface::IFileSystem &fs)
{
    append(fs, "probing numbered drive roots (read-only: exist() + listing)");
    static constexpr const char *kRoots[] = { "0:/", "1:/", "2:/", "3:/" };
    for (const char *root : kRoots) {
        const bool rootExists = fs.exist(root);
        appendf(fs, "exist(%s)=%d", root, rootExists ? 1 : 0);
        if (rootExists) {
            listDirectory(fs, root);
        }

        // Also try the specific file this app actually wants, at this root,
        // without ever opening it for writing.
        char settingsAtRoot[24];
        std::snprintf(settingsAtRoot, sizeof(settingsAtRoot), "%ssettings.json", root);
        appendf(fs, "exist(%s)=%d", settingsAtRoot, fs.exist(settingsAtRoot) ? 1 : 0);
    }
}

void probeSharedData(SDK::Interface::IFileSystem &fs)
{
    append(fs, "probing SharedData spellings (read-only: exist(), then listing if present)");
    static constexpr const char *kCandidates[] = {
        "../SharedData/",
        "../SharedData",
        "../../SharedData/",
        "SharedData/",
    };
    for (const char *candidate : kCandidates) {
        const bool candidateExists = fs.exist(candidate);
        appendf(fs, "exist(%s)=%d", candidate, candidateExists ? 1 : 0);
        if (candidateExists) {
            listDirectory(fs, candidate);
        }
    }
}

void probeTwoHopResolution(SDK::Interface::IFileSystem &fs)
{
    // "../SharedData/" (one leading ".." plus a real subpath) resolved
    // correctly; bare "../" and "../../" (nothing after the dots) did not --
    // they both hit whatever this firmware's parser does with a totally bare
    // "..", which turned out to be unrelated to real parent-directory
    // resolution. So a bare "../../" was never a valid test of "does a
    // second real hop work". This checks with real subpaths instead, on
    // known-from-BLE-FTS-research targets two real hops up from this app's
    // own directory: "Apps/" itself (very recognisable -- sibling app
    // folders) and a route into and back out of it that never leaves a bare
    // ".." floating with nothing after it anywhere in the string.
    append(fs, "probing two-hop resolution with real subpaths (not bare '..')");
    listDirectory(fs, "../../Apps/");
    listDirectory(fs, "../../Apps/../");

    // One ".." (in "../SharedData/") resolves correctly; every two-".."
    // arrangement tried so far falls back to the same wrong fixed volume.
    // This checks whether it's specifically "a second '..' token anywhere",
    // or whether leading with a real name before any dots changes anything.
    append(fs, "probing whether leading-token shape changes two-hop resolution");
    static constexpr const char *kShapes[] = {
        "/../settings.json",
        "./../settings.json",
        "Apps/../../settings.json",
        "../Apps/../settings.json",
    };
    for (const char *shape : kShapes) {
        appendf(fs, "exist(%s)=%d", shape, fs.exist(shape) ? 1 : 0);
    }
}

} // namespace DebugLog
