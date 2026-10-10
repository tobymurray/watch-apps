#ifndef SLUMBER_DAY_LOG_HPP
#define SLUMBER_DAY_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "EpochRow.hpp"

namespace Slumber
{

/// Appends to `path`, writing `header` first if the file is new or empty.
/// Holds no handle between calls, so it never keeps one of the watch's ten.
inline bool appendFile(SDK::Interface::IFileSystem &fs, const char *path, const char *header,
                       const char *data, size_t len)
{
    std::unique_ptr<SDK::Interface::IFile> f = fs.file(path);
    if (!f || !f->open(true, false)) {
        return false;
    }
    // open(write, override=false) positions at 0 (SleepLab ledger row P6).
    const size_t at = f->size();
    bool ok = f->seek(at);
    size_t written = 0;
    if (ok && at == 0 && header != nullptr) {
        const size_t hl = std::strlen(header);
        ok = f->write(header, hl, written) && written == hl;
    }
    if (ok && len > 0) {
        ok = f->write(data, len, written) && written == len;
    }
    ok = f->flush() && ok;
    f->close();
    return ok;
}

/// True if `path` is absent, empty, or starts with exactly `header`.
inline bool startsWith(SDK::Interface::IFileSystem &fs, const char *path, const char *header)
{
    if (!fs.exist(path)) {
        return true;
    }
    std::unique_ptr<SDK::Interface::IFile> f = fs.file(path);
    if (!f || !f->open(false, false)) {
        return false;
    }
    const size_t hl   = std::strlen(header);
    bool         same = f->size() == 0;
    if (!same && f->size() >= hl) {
        char   buf[512];
        size_t got = 0;
        same = hl <= sizeof(buf) && f->read(buf, hl, got) && got == hl &&
               std::memcmp(buf, header, hl) == 0;
    }
    f->close();
    return same;
}

/// Rows buffered in RAM and appended one local day per file.
///
/// A day file written under another header, by an older schema, is renamed to
/// `<stem>-<n>.csv` before the first append, so no file mixes two schemas.
class DayLog
{
public:
    static constexpr char   kDir[]    = "Days";
    static constexpr size_t kRowCap   = 320;
    /// Five minutes of rows per write; a USB unplug flushes through `flush()`.
    static constexpr size_t kFlushRows = 10;
    /// Set-aside files per day; past this, rows are dropped rather than mixed.
    static constexpr unsigned kMaxSetAside = 9;
    static constexpr const char *kPreamble =
        "# Slumber epoch log, schema 2: one row per 30 s, all day. -1 = not measured.\n";

    explicit DayLog(SDK::Interface::IFileSystem &fs)
        : mFs(fs)
    {}

    /// False if a write this call made failed; the rows it held are dropped.
    bool add(const EpochRow &row, const char *stem)
    {
        bool ok = true;
        if (std::strncmp(stem, mStem, sizeof(mStem)) != 0) {
            ok = flush();
            std::strncpy(mStem, stem, sizeof(mStem) - 1);
            mRowsInFile = 0;
            mChecked    = false;
        }
        const size_t n = row.format(mBuf + mLen, sizeof(mBuf) - mLen);
        if (n == 0) {
            return false;
        }
        mLen += n;
        ++mPending;
        ++mRowsInFile;
        if (mPending >= kFlushRows) {
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

        char header[512];
        std::snprintf(header, sizeof(header), "%s%s", kPreamble, EpochRow::kHeader);
        if (!mChecked) {
            mChecked = startsWith(mFs, path, header) || setAside(path);
        }
        const bool ok = mChecked && appendFile(mFs, path, header, mBuf, mLen);
        mLen     = 0;
        mPending = 0;
        return ok;
    }

    /// Rows added under the current stem since this process started.
    uint32_t rowsInFile() const { return mRowsInFile; }

private:
    bool setAside(const char *path)
    {
        char to[32];
        for (unsigned n = 1; n <= kMaxSetAside; ++n) {
            std::snprintf(to, sizeof(to), "%s/%s-%u.csv", kDir, mStem, n);
            if (!mFs.exist(to)) {
                return mFs.rename(path, to);
            }
        }
        return false;
    }

    SDK::Interface::IFileSystem &mFs;
    char     mStem[9] = {};
    char     mBuf[kRowCap * kFlushRows] = {};
    size_t   mLen        = 0;
    size_t   mPending    = 0;
    uint32_t mRowsInFile = 0;
    bool     mChecked    = false;  ///< the current stem's file has this header
};

} // namespace Slumber

#endif // SLUMBER_DAY_LOG_HPP
