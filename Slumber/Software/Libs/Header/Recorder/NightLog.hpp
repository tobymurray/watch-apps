#ifndef SLUMBER_NIGHT_LOG_HPP
#define SLUMBER_NIGHT_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "DayLog.hpp"
#include "LocalTime.hpp"
#include "NightSummary.hpp"
#include "SleepPeriods.hpp"

namespace Slumber
{

/// `Nights.csv`: one line per period once SleepPeriods has settled it, so the
/// nights outlive the 36 h the card looks back over.
class NightLog
{
public:
    static constexpr char kPath[] = "Nights.csv";
    static constexpr const char *kHeader =
        "# Slumber nights, schema 1: one line per period once it is settled.\n"
        "start_utc,end_utc,utc_off_min,start_local,end_local,span_min,rest_min,missing_min,"
        "longest_rest_min,restless_spells,worn,worn_pct,hr_cover_pct\n";

    explicit NightLog(SDK::Interface::IFileSystem &fs)
        : mFs(fs)
    {}

    /// True if `p` is settled and starts after the last period logged.
    bool due(const SleepPeriods::Period &p)
    {
        if (!mRead) {
            mRead = readLastEnd();
        }
        return mRead && p.settled && p.startUtc >= mLastEndUtc;
    }

    /// Appends `p`, which `due` accepted; false if the write failed.
    bool add(const SleepPeriods::Period &p, const NightSummary::Summary &s, int32_t offsetMin)
    {
        char from[17];
        char to[17];
        LocalTime::stamp(p.startUtc, offsetMin, from);
        LocalTime::stamp(p.endUtc, offsetMin, to);
        char      line[160];
        const int len = std::snprintf(
            line, sizeof(line), "%lu,%lu,%ld,%s,%s,%lu,%u,%u,%u,%u,%s,%u,%u\n",
            static_cast<unsigned long>(p.startUtc), static_cast<unsigned long>(p.endUtc),
            static_cast<long>(offsetMin), from, to,
            static_cast<unsigned long>((p.endUtc - p.startUtc) / 60u),
            static_cast<unsigned>(p.restMin), static_cast<unsigned>(p.missingMin),
            static_cast<unsigned>(s.longestRestMin), static_cast<unsigned>(s.restlessSpells),
            name(p.worn), static_cast<unsigned>(p.wornPct), static_cast<unsigned>(p.hrCoverPct));
        if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) {
            return false;
        }
        if (!mChecked) {
            mChecked = startsWith(mFs, kPath, kHeader) || setAside(mFs, kPath);
        }
        if (!mChecked || !appendFile(mFs, kPath, kHeader, line, static_cast<size_t>(len))) {
            return false;
        }
        mLastEndUtc = p.endUtc;
        return true;
    }

    static const char *name(SleepPeriods::Worn w)
    {
        return w == SleepPeriods::Worn::Worn      ? "worn"
               : w == SleepPeriods::Worn::NotWorn ? "not-worn"
                                                  : "uncertain";
    }

private:
    /// Finds the latest `end_utc` already in the file; false if it could not
    /// be read, so nothing is logged twice.
    bool readLastEnd()
    {
        mLastEndUtc = 0;
        if (!mFs.exist(kPath)) {
            return true;
        }
        if (!startsWith(mFs, kPath, kHeader)) {
            return true;
        }
        std::unique_ptr<SDK::Interface::IFile> f = mFs.file(kPath);
        if (!f || !f->open(false, false)) {
            return false;
        }
        char   line[160];
        size_t len   = 0;
        int    comma = 0;
        char   buf[256];
        size_t got = 0;
        while (f->read(buf, sizeof(buf), got) && got > 0) {
            for (size_t i = 0; i < got; ++i) {
                const char c = buf[i];
                if (c == '\n') {
                    line[len] = '\0';
                    noteEnd(line);
                    len   = 0;
                    comma = 0;
                } else if (len < sizeof(line) - 1 && comma < 2) {
                    comma += c == ',' ? 1 : 0;
                    line[len++] = c;
                }
            }
            if (got < sizeof(buf)) {
                break;
            }
        }
        f->close();
        return true;
    }

    void noteEnd(const char *line)
    {
        const char *comma = std::strchr(line, ',');
        if (comma == nullptr || *line < '0' || *line > '9') {
            return;
        }
        const unsigned long end = std::strtoul(comma + 1, nullptr, 10);
        if (end > mLastEndUtc) {
            mLastEndUtc = static_cast<uint32_t>(end);
        }
    }

    SDK::Interface::IFileSystem &mFs;
    uint32_t mLastEndUtc = 0;
    bool     mRead       = false;
    bool     mChecked    = false;
};

} // namespace Slumber

#endif // SLUMBER_NIGHT_LOG_HPP
