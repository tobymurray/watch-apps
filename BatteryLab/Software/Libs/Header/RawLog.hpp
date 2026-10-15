#ifndef BATTERYLAB_RAW_LOG_HPP
#define BATTERYLAB_RAW_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "RawLine.hpp"

namespace BatteryLab
{

/// Rows buffered in RAM and appended one UTC day per file, holding no handle between writes.
class RawLog
{
public:
    static constexpr char   kDir[]      = "Raw";
    static constexpr size_t kFlushLines = 32;

    explicit RawLog(SDK::Interface::IFileSystem &fs)
        : mFs(fs)
    {}

    /// `stem` is the file's name without `.csv`; false if a write this call made failed.
    bool add(const Sample &s, const char *stem)
    {
        bool ok = true;
        if (std::strncmp(stem, mStem, sizeof(mStem)) != 0) {
            ok = flush();
            std::strncpy(mStem, stem, sizeof(mStem) - 1);
        }
        const size_t n = format(s, mBuf + mLen, sizeof(mBuf) - mLen);
        if (n == 0) {
            return false;
        }
        mLen += n;
        ++mLines;
        if (++mPending >= kFlushLines) {
            ok = flush() && ok;
        }
        return ok;
    }

    bool flush()
    {
        if (mLen == 0 || mStem[0] == '\0') {
            return true;
        }
        char path[32];
        std::snprintf(path, sizeof(path), "%s/%s.csv", kDir, mStem);
        mFs.mkdir(kDir);
        const bool ok = append(path);
        mLen     = 0;
        mPending = 0;
        return ok;
    }

    uint32_t lines() const { return mLines; }

private:
    bool append(const char *path)
    {
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(path);
        if (!f || !f->open(true, false)) {
            return false;
        }
        // open(write, override=false) positions at 0 (SleepLab ledger row P6).
        const size_t at      = f->size();
        bool         ok      = f->seek(at);
        size_t       written = 0;
        if (ok && at == 0) {
            const size_t hl = sizeof(kHeader) - 1;
            ok = f->write(kHeader, hl, written) && written == hl;
        }
        if (ok) {
            ok = f->write(mBuf, mLen, written) && written == mLen;
        }
        ok = f->flush() && ok;
        f->close();
        return ok;
    }

    SDK::Interface::IFileSystem &mFs;
    char     mStem[9] = {};
    char     mBuf[kLineCap * kFlushLines] = {};
    size_t   mLen     = 0;
    size_t   mPending = 0;
    uint32_t mLines   = 0;
};

} // namespace BatteryLab

#endif // BATTERYLAB_RAW_LOG_HPP
