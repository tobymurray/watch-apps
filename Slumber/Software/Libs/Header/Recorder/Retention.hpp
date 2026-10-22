#ifndef SLUMBER_RETENTION_HPP
#define SLUMBER_RETENTION_HPP

#include <cstdint>
#include <cstdio>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "DayLog.hpp"
#include "EpochHistory.hpp"
#include "LocalTime.hpp"

namespace Slumber
{

/// Removes day files once they are older than the days kept, by computing their
/// names from the date rather than listing a directory.
namespace Retention
{

/// Local days kept, today included.
constexpr unsigned kKeepDays = 30;

/// EpochHistory::restore reads today and the two days before.
constexpr unsigned kMinKeepDays = 3;

/// Days past the kept ones that are looked for, so a stretch with the watch
/// off is still swept; a day missed here is never removed.
constexpr unsigned kLookbackDays = 60;

/// Removes one day's CSV, its set-aside copies and its epoch cache.
/// Returns how many files went.
inline unsigned removeDay(SDK::Interface::IFileSystem &fs, const char *stem)
{
    unsigned removed = 0;
    char     path[32];
    std::snprintf(path, sizeof(path), "%s/%s.csv", DayLog::kDir, stem);
    if (fs.exist(path)) {
        removed += fs.remove(path) ? 1u : 0u;
        // setAside only ever renames a day that still has its own file.
        for (unsigned n = 1; n <= 9; ++n) {
            std::snprintf(path, sizeof(path), "%s/%s-%u.csv", DayLog::kDir, stem, n);
            if (!fs.exist(path)) {
                break;
            }
            removed += fs.remove(path) ? 1u : 0u;
        }
    }
    std::snprintf(path, sizeof(path), "%s/%s.bin", EpochHistory::kDir, stem);
    if (fs.exist(path)) {
        removed += fs.remove(path) ? 1u : 0u;
    }
    return removed;
}

/// Removes the files of the kLookbackDays local days before the `keep` kept up
/// to `nowUtc`. Returns how many files went.
inline unsigned prune(SDK::Interface::IFileSystem &fs, int64_t nowUtc, int32_t offsetMin,
                      unsigned keep = kKeepDays)
{
    if (keep < kMinKeepDays) {
        keep = kMinKeepDays;
    }
    unsigned removed = 0;
    for (unsigned back = keep; back < keep + kLookbackDays; ++back) {
        char stem[9];
        LocalTime::dayStem(nowUtc - static_cast<int64_t>(back) * 86400, offsetMin, stem);
        removed += removeDay(fs, stem);
    }
    return removed;
}

} // namespace Retention
} // namespace Slumber

#endif // SLUMBER_RETENTION_HPP
