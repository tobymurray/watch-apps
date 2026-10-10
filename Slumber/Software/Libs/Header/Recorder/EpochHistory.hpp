#ifndef SLUMBER_EPOCH_HISTORY_HPP
#define SLUMBER_EPOCH_HISTORY_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "DayLog.hpp"
#include "LocalTime.hpp"
#include "SleepPeriods.hpp"

namespace Slumber
{

/// The last kWindowS of SleepPeriods' inputs, in RAM for the card and in
/// `Epochs/<yyyymmdd>.bin` so a restart (a USB plug stops the app) does not
/// lose the night it is about to report. The day CSV is the record; this is
/// its cache, so a file under another header is deleted, not set aside.
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
    void restore(uint32_t nowUtc, int32_t offsetMin)
    {
        mN = 0;
        char last[9] = {};
        for (int daysBack = 2; daysBack >= 0; --daysBack) {
            const int64_t at = static_cast<int64_t>(nowUtc) - daysBack * 86400;
            char          stem[9];
            LocalTime::dayStem(at, offsetMin, stem);
            if (std::strcmp(stem, last) != 0) {
                load(stem, nowUtc);
                std::memcpy(last, stem, sizeof(last));
            }
        }
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

    void load(const char *stem, uint32_t nowUtc)
    {
        char path[32];
        std::snprintf(path, sizeof(path), "%s/%s.bin", kDir, stem);
        if (!mFs.exist(path) || !startsWith(mFs, path, kHeader)) {
            return;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(path);
        if (!f || !f->open(false, false)) {
            return;
        }
        const size_t hl = std::strlen(kHeader);
        if (f->seek(hl)) {
            uint8_t buf[kRecSize * 64];
            size_t  got = 0;
            while (f->read(reinterpret_cast<char *>(buf), sizeof(buf), got) && got >= kRecSize) {
                for (size_t at = 0; at + kRecSize <= got; at += kRecSize) {
                    const SleepPeriods::Epoch e = decode(buf + at);
                    if (e.utc <= nowUtc && nowUtc - e.utc <= kWindowS &&
                        (mN == 0 || e.utc > mEpochs[mN - 1].utc)) {
                        push(e);
                    }
                }
                if (got < sizeof(buf)) {
                    break;
                }
            }
        }
        f->close();
    }

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
