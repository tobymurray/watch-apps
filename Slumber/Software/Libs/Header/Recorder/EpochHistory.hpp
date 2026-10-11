#ifndef SLUMBER_EPOCH_HISTORY_HPP
#define SLUMBER_EPOCH_HISTORY_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "DayLog.hpp"
#include "EpochRow.hpp"
#include "LocalTime.hpp"
#include "SleepPeriods.hpp"

namespace Slumber
{

/// The last kWindowS of SleepPeriods' inputs, in RAM for the card and in
/// `Epochs/<yyyymmdd>.bin` so a restart (a USB plug stops the app) does not
/// lose the night it is about to report. The day CSV is the record; this is
/// its cache, so a missing file or one under another header is rebuilt from it.
class EpochHistory
{
public:
    static constexpr char     kDir[]     = "Epochs";
    /// A night, the hour SleepPeriods waits before settling it, and the day before.
    static constexpr uint32_t kWindowS   = 36u * 60u * 60u;
    static constexpr size_t   kCap       = kWindowS / SleepPeriods::kEpochS;
    static constexpr size_t   kRecSize   = 12;
    static constexpr size_t   kFlushRecs = DayLog::kFlushRows;
    static constexpr const char *kHeader =
        "slumber-epochs 1: 12-byte little-endian records of utc u32, count u32, "
        "steps u16, hr_trust_x10 u8, flags u8 (1 = heart rate read, 2 = asked)\n";

    explicit EpochHistory(SDK::Interface::IFileSystem &fs)
        : mFs(fs)
    {}

    /// Reloads the files around `nowUtc`; the epochs already held are dropped.
    /// Returns how many days were rebuilt from their day file.
    unsigned restore(uint32_t nowUtc, int32_t offsetMin)
    {
        mN = 0;
        unsigned rebuilt = 0;
        char last[9] = {};
        for (int daysBack = 2; daysBack >= 0; --daysBack) {
            const int64_t at = static_cast<int64_t>(nowUtc) - daysBack * 86400;
            char          stem[9];
            LocalTime::dayStem(at, offsetMin, stem);
            if (std::strcmp(stem, last) != 0) {
                if (!load(stem, nowUtc) && rebuild(stem, nowUtc)) {
                    ++rebuilt;
                }
                std::memcpy(last, stem, sizeof(last));
            }
        }
        return rebuilt;
    }

    /// False if a write this call made failed; the epoch is held in RAM either way.
    bool add(const SleepPeriods::Epoch &e, const char *stem)
    {
        bool ok = true;
        if (std::strncmp(stem, mStem, sizeof(mStem)) != 0) {
            ok = flush();
            std::strncpy(mStem, stem, sizeof(mStem) - 1);
            mChecked = false;
        }
        push(e);
        encode(e, mPending + mPendingN * kRecSize);
        if (++mPendingN >= kFlushRecs) {
            ok = flush() && ok;
        }
        return ok;
    }

    bool flush()
    {
        if (mPendingN == 0 || mStem[0] == '\0') {
            return true;
        }
        char path[32];
        std::snprintf(path, sizeof(path), "%s/%s.bin", kDir, mStem);
        mFs.mkdir(kDir);
        if (!mChecked) {
            mChecked = startsWith(mFs, path, kHeader) || mFs.remove(path);
        }
        const bool ok = mChecked && appendFile(mFs, path, kHeader,
                                               reinterpret_cast<const char *>(mPending),
                                               mPendingN * kRecSize);
        mPendingN = 0;
        return ok;
    }

    const SleepPeriods::Epoch *data() const { return mEpochs; }
    size_t                     size() const { return mN; }

    /// The epoch a day-file row describes: the row's `utc` is when it closed,
    /// an epoch is keyed by when it opened.
    static SleepPeriods::Epoch fromRow(int64_t utc, uint32_t count, int32_t steps, int32_t hrN,
                                       int32_t hrTrustX10)
    {
        SleepPeriods::Epoch e;
        e.utc        = static_cast<uint32_t>(utc) - SleepPeriods::kEpochS;
        e.count      = count;
        e.steps      = clampTo<uint16_t>(steps, 0xFFFFu);
        e.hasHr      = hrN > 0;
        e.hrAsked    = hrN >= 0;
        e.hrTrustX10 = e.hasHr ? clampTo<uint8_t>(hrTrustX10, 0xFFu) : 0;
        return e;
    }

    static void encode(const SleepPeriods::Epoch &e, uint8_t *out)
    {
        put32(out, e.utc);
        put32(out + 4, e.count);
        out[8]  = static_cast<uint8_t>(e.steps);
        out[9]  = static_cast<uint8_t>(e.steps >> 8);
        out[10] = e.hrTrustX10;
        out[11] = static_cast<uint8_t>((e.hasHr ? 1u : 0u) | (e.hrAsked ? 2u : 0u));
    }

    static SleepPeriods::Epoch decode(const uint8_t *in)
    {
        SleepPeriods::Epoch e;
        e.utc        = get32(in);
        e.count      = get32(in + 4);
        e.steps      = static_cast<uint16_t>(in[8] | (in[9] << 8));
        e.hrTrustX10 = in[10];
        e.hasHr      = (in[11] & 1u) != 0;
        e.hrAsked    = (in[11] & 2u) != 0;
        return e;
    }

private:
    /// Keeps `mEpochs` sorted, as SleepPeriods requires: a clock set backwards
    /// starts the history again rather than interleave two timelines.
    void push(const SleepPeriods::Epoch &e)
    {
        if (mN > 0 && e.utc <= mEpochs[mN - 1].utc) {
            mN = 0;
        }
        if (mN == kCap) {
            std::memmove(mEpochs, mEpochs + 1, (kCap - 1) * sizeof(mEpochs[0]));
            --mN;
        }
        mEpochs[mN++] = e;
    }

    template <typename T>
    static T clampTo(int32_t v, uint32_t max)
    {
        return v <= 0 ? T{0} : (static_cast<uint32_t>(v) >= max ? static_cast<T>(max) : static_cast<T>(v));
    }

    /// True if `e` is inside the window and after the epochs already held.
    bool keep(const SleepPeriods::Epoch &e, uint32_t nowUtc)
    {
        if (e.utc > nowUtc || nowUtc - e.utc > kWindowS || (mN > 0 && e.utc <= mEpochs[mN - 1].utc)) {
            return false;
        }
        push(e);
        return true;
    }

    /// False if the cache file is absent or under another header, so the day
    /// file must stand in for it; true otherwise, even if it could not be read.
    bool load(const char *stem, uint32_t nowUtc)
    {
        char path[32];
        std::snprintf(path, sizeof(path), "%s/%s.bin", kDir, stem);
        if (!mFs.exist(path) || !startsWith(mFs, path, kHeader)) {
            return false;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(path);
        if (!f || !f->open(false, false)) {
            return true;
        }
        const size_t hl = std::strlen(kHeader);
        if (f->seek(hl)) {
            uint8_t buf[kRecSize * 64];
            size_t  got = 0;
            while (f->read(reinterpret_cast<char *>(buf), sizeof(buf), got) && got >= kRecSize) {
                for (size_t at = 0; at + kRecSize <= got; at += kRecSize) {
                    keep(decode(buf + at), nowUtc);
                }
                if (got < sizeof(buf)) {
                    break;
                }
            }
        }
        f->close();
        return true;
    }

    /// Index of `name` among EpochRow's columns, or -1.
    static int column(const char *name)
    {
        const size_t len = std::strlen(name);
        int          at  = 0;
        for (const char *p = EpochRow::kHeader; *p != '\0' && *p != '\n'; ++at) {
            const char *end = p + std::strcspn(p, ",\n");
            if (static_cast<size_t>(end - p) == len && std::strncmp(p, name, len) == 0) {
                return at;
            }
            p = *end == ',' ? end + 1 : end;
        }
        return -1;
    }

    static int columnCount()
    {
        int n = 1;
        for (const char *p = EpochRow::kHeader; *p != '\0'; ++p) {
            n += *p == ',' ? 1 : 0;
        }
        return n;
    }

    /// Reads `Days/<stem>.csv` into the history and writes what it kept to
    /// `Epochs/<stem>.bin`. Only a day file under this build's header is read.
    bool rebuild(const char *stem, uint32_t nowUtc)
    {
        char csv[32];
        char bin[32];
        std::snprintf(csv, sizeof(csv), "%s/%s.csv", DayLog::kDir, stem);
        std::snprintf(bin, sizeof(bin), "%s/%s.bin", kDir, stem);
        char head[DayLog::kHeaderCap];
        DayLog::header(head);
        if (!mFs.exist(csv) || !startsWith(mFs, csv, head)) {
            return false;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(csv);
        if (!f || !f->open(false, false)) {
            return false;
        }
        if (mFs.exist(bin) && !mFs.remove(bin)) {
            f->close();
            return false;
        }
        mFs.mkdir(kDir);

        Columns cols;
        RowReader rows{};
        uint8_t   out[kRecSize * 64];
        size_t    outN = 0;
        char      buf[512];
        size_t    got = 0;
        bool      ok  = f->seek(std::strlen(head));
        while (ok && f->read(buf, sizeof(buf), got) && got > 0) {
            for (size_t i = 0; i < got; ++i) {
                SleepPeriods::Epoch e;
                if (!rows.take(buf[i], cols, e)) {
                    continue;
                }
                if (keep(e, nowUtc)) {
                    encode(e, out + outN * kRecSize);
                    if (++outN == sizeof(out) / kRecSize) {
                        ok = appendFile(mFs, bin, kHeader, reinterpret_cast<const char *>(out),
                                        outN * kRecSize);
                        outN = 0;
                    }
                }
            }
            if (got < sizeof(buf)) {
                break;
            }
        }
        f->close();
        if (ok && outN > 0) {
            ok = appendFile(mFs, bin, kHeader, reinterpret_cast<const char *>(out), outN * kRecSize);
        }
        return ok;
    }

    struct Columns {
        int utc   = column("utc");
        int count = column("count");
        int steps = column("steps");
        int hrN   = column("hr_n");
        int trust = column("hr_trust_x10");
        int total = columnCount();
    };

    /// Assembles a day file's bytes into rows; a row cut short by a crash, or
    /// one too long to be a row, is skipped.
    struct RowReader {
        char   line[DayLog::kRowCap];
        size_t len      = 0;
        bool   overflow = false;

        bool take(char c, const Columns &cols, SleepPeriods::Epoch &e)
        {
            if (c != '\n') {
                if (len < sizeof(line) - 1) {
                    line[len++] = c;
                } else {
                    overflow = true;
                }
                return false;
            }
            line[len]        = '\0';
            const bool whole = !overflow;
            len              = 0;
            overflow         = false;
            return whole && parse(cols, e);
        }

        bool parse(const Columns &cols, SleepPeriods::Epoch &e) const
        {
            const int   want[5] = {cols.utc, cols.count, cols.steps, cols.hrN, cols.trust};
            int64_t     v[5]    = {};
            int         at      = 0;
            const char *p       = line;
            for (;; ++at) {
                char          *end = nullptr;
                const int64_t  n   = std::strtoll(p, &end, 10);
                if (end == p || (*end != ',' && *end != '\0')) {
                    return false;
                }
                for (int k = 0; k < 5; ++k) {
                    if (at == want[k]) {
                        v[k] = n;
                    }
                }
                if (*end == '\0') {
                    break;
                }
                p = end + 1;
            }
            if (at + 1 != cols.total || v[0] <= 0) {
                return false;
            }
            e = fromRow(v[0], static_cast<uint32_t>(v[1]), static_cast<int32_t>(v[2]),
                        static_cast<int32_t>(v[3]), static_cast<int32_t>(v[4]));
            return true;
        }
    };

    static void put32(uint8_t *p, uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            p[i] = static_cast<uint8_t>(v >> (8 * i));
        }
    }

    static uint32_t get32(const uint8_t *p)
    {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    SDK::Interface::IFileSystem &mFs;
    SleepPeriods::Epoch mEpochs[kCap] = {};
    size_t   mN = 0;
    char     mStem[9] = {};
    uint8_t  mPending[kRecSize * kFlushRecs] = {};
    size_t   mPendingN = 0;
    bool     mChecked  = false;
};

} // namespace Slumber

#endif // SLUMBER_EPOCH_HISTORY_HPP
