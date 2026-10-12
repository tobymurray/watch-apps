#ifndef SLUMBER_NIGHT_LOG_HPP
#define SLUMBER_NIGHT_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "EpochHistory.hpp"
#include "LocalTime.hpp"
#include "NightSummary.hpp"
#include "Rhythm.hpp"
#include "Segments.hpp"
#include "SleepPeriods.hpp"

namespace Slumber
{

/// The shared sleep sessions file: one JSON line per period once SleepPeriods
/// has settled it, appended so other apps can read every night Slumber found;
/// `Slumber/README.md` documents the line for readers.
class NightLog
{
public:
    static constexpr char     kDir[]    = "../SharedData/health/sleep";
    static constexpr char     kPath[]   = "../SharedData/health/sleep/sessions.jsonl";
    static constexpr char     kSource[] = "Slumber";
    static constexpr unsigned kSchema   = 1;
    /// Every key a reader needs to find a session fits in a line's first bytes.
    static constexpr size_t   kPrefixCap = 384;

    explicit NightLog(SDK::Interface::IFileSystem &fs)
        : mFs(fs)
    {}

    /// True if `p` is settled and starts after the last period logged.
    bool due(const SleepPeriods::Period &p)
    {
        ensureRead();
        return mRead && p.settled && p.startUtc >= mLastEndUtc;
    }

    /// Appends `p`, which `due` accepted, with its epochs from `e`; false if the
    /// write failed, so it is offered again.
    bool add(const SleepPeriods::Period &p, const NightSummary::Summary &s, int32_t offsetMin,
             const SleepPeriods::Epoch *e, size_t n)
    {
        char      head[kPrefixCap];
        const int len = prefix(p, s, offsetMin, head, sizeof(head));
        if (len <= 0 || static_cast<size_t>(len) >= sizeof(head)) {
            return false;
        }
        const size_t slots = Segments::strip(e, n, p, mStrip, sizeof(mStrip));
        if (slots == 0) {
            return false;
        }
        if (!append(head, static_cast<size_t>(len), slots)) {
            return false;
        }
        mLastEndUtc = p.endUtc;
        if (p.worn == SleepPeriods::Worn::Worn) {
            mRecent.add(p.startUtc, p.endUtc, offsetMin, p.restMin);
        }
        return true;
    }

    /// The main nights read back from the file and logged since.
    const Rhythm::Recent &recent()
    {
        ensureRead();
        return mRecent;
    }

    static const char *name(SleepPeriods::Worn w)
    {
        return w == SleepPeriods::Worn::Worn      ? "worn"
               : w == SleepPeriods::Worn::NotWorn ? "not_worn"
                                                  : "uncertain";
    }

    /// The line up to the opening quote of `epochs`, NUL-terminated.
    static int prefix(const SleepPeriods::Period &p, const NightSummary::Summary &s,
                      int32_t offsetMin, char *out, size_t cap)
    {
        char from[17];
        char to[17];
        LocalTime::stamp(p.startUtc, offsetMin, from);
        LocalTime::stamp(p.endUtc, offsetMin, to);
        return std::snprintf(
            out, cap,
            "{\"schema\":%u,\"source\":\"%s\",\"start\":%lu,\"end\":%lu,\"utc_off_min\":%ld,"
            "\"rest_min\":%u,\"worn\":\"%s\",\"start_local\":\"%s\",\"end_local\":\"%s\","
            "\"span_min\":%lu,\"missing_min\":%u,\"longest_rest_min\":%u,\"restless_spells\":%u,"
            "\"worn_pct\":%u,\"hr_cover_pct\":%u,\"epoch_s\":%u,\"epochs\":\"",
            kSchema, kSource, static_cast<unsigned long>(p.startUtc),
            static_cast<unsigned long>(p.endUtc), static_cast<long>(offsetMin),
            static_cast<unsigned>(p.restMin), name(p.worn), from, to,
            static_cast<unsigned long>((p.endUtc - p.startUtc) / 60u),
            static_cast<unsigned>(p.missingMin), static_cast<unsigned>(s.longestRestMin),
            static_cast<unsigned>(s.restlessSpells), static_cast<unsigned>(p.wornPct),
            static_cast<unsigned>(p.hrCoverPct), static_cast<unsigned>(SleepPeriods::kEpochS));
    }

private:
    /// Writes one line in a single open; a line some earlier write left torn is
    /// closed off first, so it cannot run into this one.
    bool append(const char *head, size_t headLen, size_t slots)
    {
        mFs.mkdir(kDir);
        bool torn = false;
        if (!endsLine(torn)) {
            return false;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(kPath);
        if (!f || !f->open(true, false)) {
            return false;
        }
        size_t done = 0;
        bool   ok   = f->seek(f->size());
        if (ok && torn) {
            ok = f->write("\n", 1, done) && done == 1;
        }
        static constexpr char kTail[] = "\"}\n";
        ok = ok && f->write(head, headLen, done) && done == headLen;
        ok = ok && f->write(mStrip, slots, done) && done == slots;
        ok = ok && f->write(kTail, sizeof(kTail) - 1, done) && done == sizeof(kTail) - 1;
        ok = f->flush() && ok;
        f->close();
        return ok;
    }

    /// Whether the file's last byte leaves a line open; false if it could not
    /// be read. A handle opened to write cannot read, so this opens its own.
    bool endsLine(bool &torn)
    {
        torn = false;
        if (!mFs.exist(kPath)) {
            return true;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(kPath);
        if (!f || !f->open(false, false)) {
            return false;
        }
        const size_t at   = f->size();
        char         last = '\n';
        size_t       got  = 0;
        const bool   ok   = at == 0 || (f->seek(at - 1) && f->read(&last, 1, got) && got == 1);
        f->close();
        torn = last != '\n';
        return ok;
    }

    void ensureRead()
    {
        if (!mRead) {
            mRead = readBack();
        }
    }

    /// Reads back every complete line this source wrote; false if the file
    /// exists but could not be read, so nothing is logged twice.
    bool readBack()
    {
        mLastEndUtc = 0;
        mRecent     = Rhythm::Recent{};
        if (!mFs.exist(kPath)) {
            return true;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(kPath);
        if (!f || !f->open(false, false)) {
            return false;
        }
        char   line[kPrefixCap];
        size_t len = 0;
        char   buf[256];
        size_t got = 0;
        while (f->read(buf, sizeof(buf), got) && got > 0) {
            for (size_t i = 0; i < got; ++i) {
                if (buf[i] == '\n') {
                    line[len] = '\0';
                    note(line);
                    len = 0;
                } else if (len < sizeof(line) - 1) {
                    line[len++] = buf[i];
                }
            }
            if (got < sizeof(buf)) {
                break;
            }
        }
        f->close();
        return true;
    }

    static bool number(const char *line, const char *key, long &out)
    {
        const char *at = std::strstr(line, key);
        if (at == nullptr) {
            return false;
        }
        char *end = nullptr;
        out       = std::strtol(at + std::strlen(key), &end, 10);
        return end != at + std::strlen(key);
    }

    void note(const char *line)
    {
        if (std::strstr(line, "\"source\":\"Slumber\"") == nullptr) {
            return;
        }
        long start = 0, end = 0, off = 0, rest = 0;
        if (!number(line, "\"start\":", start) || !number(line, "\"end\":", end) ||
            !number(line, "\"utc_off_min\":", off) || !number(line, "\"rest_min\":", rest)) {
            return;
        }
        if (static_cast<uint32_t>(end) > mLastEndUtc) {
            mLastEndUtc = static_cast<uint32_t>(end);
        }
        if (std::strstr(line, "\"worn\":\"worn\"") != nullptr && end > start) {
            mRecent.add(static_cast<uint32_t>(start), static_cast<uint32_t>(end),
                        static_cast<int32_t>(off), static_cast<uint16_t>(rest));
        }
    }

    SDK::Interface::IFileSystem &mFs;
    Rhythm::Recent mRecent;
    char           mStrip[EpochHistory::kCap + 1] = {};
    uint32_t       mLastEndUtc = 0;
    bool           mRead       = false;
};

} // namespace Slumber

#endif // SLUMBER_NIGHT_LOG_HPP
