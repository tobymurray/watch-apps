#include "EventLog.hpp"

#include <cstdarg>
#include <cstdio>

namespace TrainKit {

EventLog::EventLog(SDK::Interface::IFileSystem& fs, const char* path)
    : mFs(fs), mPath(path)
{
}

EventLog::~EventLog()
{
    close();
}

void EventLog::open()
{
    if (mFile) {
        return;
    }

    mBytes = 0;
    if (mFs.exist(mPath)) {
        SDK::Interface::IFileSystem::ObjectInfo info{};
        if (mFs.objectInfo(mPath, info)) {
            mBytes = info.size;
        }
    }
    mFull = mBytes >= kMaxBytes;

    std::unique_ptr<SDK::Interface::IFile> file = mFs.file(mPath);
    if (!file) {
        return;
    }
    // override = false so an existing file keeps what is in it. Whether that
    // also creates a missing one depends on how the kernel maps the pair, so a
    // first run falls back to the creating form -- which is only reached when
    // there is nothing to lose.
    if (!file->open(true, false) && !(mBytes == 0 && file->open(true, true))) {
        return;   // no log this ride, and nothing else changes
    }

    // If the open truncated after all, trust what is actually there: seeking
    // past the end of a file this did not write is worse than losing the tail.
    if (file->size() < mBytes) {
        mBytes = file->size();
    }
    // Appending is a seek, not an open mode: an opened file starts at 0 and
    // would write over the ride before this one.
    file->seek(mBytes);
    mFile = std::move(file);
}

void EventLog::close()
{
    if (!mFile) {
        return;
    }
    mFile->flush();
    mFile->close();
    mFile.reset();
}

void EventLog::sync()
{
    if (mFile) {
        mFile->flush();
    }
}

void EventLog::reset()
{
    close();
    mFs.remove(mPath);
    mBytes = 0;
    mFull  = false;
}

void EventLog::line(const char* fmt, ...)
{
    if (!mFile || mFull) {
        return;
    }

    char buf[kMaxLine];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    if (static_cast<size_t>(n) >= sizeof(buf) - 1) {
        n = static_cast<int>(sizeof(buf) - 2);
    }
    buf[n++] = '\n';

    size_t wrote = 0;
    if (!mFile->write(buf, static_cast<size_t>(n), wrote)) {
        return;
    }
    mBytes += wrote;

    if (mBytes >= kMaxBytes) {
        mFull = true;
        static const char kStop[] = "log full\n";
        mFile->write(kStop, sizeof(kStop) - 1, wrote);
        mFile->flush();
    }
}

} // namespace TrainKit
