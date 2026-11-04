#ifndef SLUMBER_SLEEP_NOW_HPP
#define SLUMBER_SLEEP_NOW_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "LastNight.hpp"
#include "NightLog.hpp"
#include "NightSummary.hpp"
#include "Rhythm.hpp"
#include "Schedule.hpp"
#include "SleepPeriods.hpp"

namespace Slumber
{

/// The shared sleep state file: what Slumber knows now, rewritten whole every
/// heartbeat; `Slumber/README.md` documents it for readers.
namespace SleepNow
{

constexpr char kPath[]    = "../SharedData/health/sleep/now.json";
constexpr char kTmpPath[] = "../SharedData/health/sleep/now.json.tmp";
constexpr char kBakPath[] = "../SharedData/health/sleep/now.json.bak";
constexpr unsigned kSchema = 2;
constexpr size_t   kCap    = 1024;

struct State {
    uint32_t writtenUtc     = 0;
    uint32_t heartbeatS     = 0;
    int32_t  offsetMin      = 0;
    uint32_t asleepSinceUtc = 0;  ///< 0 = not asleep now, as far as Slumber can tell
    const SleepPeriods::Period *lastNight = nullptr;  ///< the longest period of lastSleep
    LastNight::Sleep lastSleep;    ///< the night the card shows; no periods = none
    Rhythm::Usual usual;           ///< for tonight: every main night logged
    Rhythm::Usual usualBefore;     ///< what last night is scored against
    int32_t       tonightDay = 0;  ///< Schedule::tonight
    NightSummary::Heart lastHeart;   ///< of lastNight
};

/// Writes `s` as one JSON object; returns its length, or 0 if `cap` is short.
inline size_t format(const State &s, char *out, size_t cap)
{
    size_t len = 0;
    auto   put = [&](const char *fmt, auto... args) {
        if (len < cap) {
            const int n = std::snprintf(out + len, cap - len, fmt, args...);
            len += n > 0 ? static_cast<size_t>(n) : cap;
        }
    };
    put("{\"schema\":%u,\"source\":\"%s\",\"written\":%lu,\"heartbeat_s\":%lu,\"utc_off_min\":%ld,",
        kSchema, NightLog::kSource, static_cast<unsigned long>(s.writtenUtc),
        static_cast<unsigned long>(s.heartbeatS), static_cast<long>(s.offsetMin));
    if (s.asleepSinceUtc != 0) {
        put("\"asleep_since\":%lu,", static_cast<unsigned long>(s.asleepSinceUtc));
    } else {
        put("%s", "\"asleep_since\":null,");
    }
    const SleepPeriods::Period *p = s.lastNight;
    if (p != nullptr) {
        put("\"last_night\":{\"start\":%lu,\"end\":%lu,\"rest_min\":%u,\"worn\":\"%s\",\"settled\":%s},",
            static_cast<unsigned long>(p->startUtc), static_cast<unsigned long>(p->endUtc),
            static_cast<unsigned>(p->restMin), NightLog::name(p->worn),
            p->settled ? "true" : "false");
    } else {
        put("%s", "\"last_night\":null,");
    }
    const LastNight::Sleep &z = s.lastSleep;
    if (p != nullptr && p->worn == SleepPeriods::Worn::Worn && z.periods > 0) {
        put("\"last_sleep\":{\"start\":%lu,\"end\":%lu,\"rest_min\":%u,\"periods\":%u,\"settled\":%s},",
            static_cast<unsigned long>(z.startUtc), static_cast<unsigned long>(z.endUtc),
            static_cast<unsigned>(z.restMin), static_cast<unsigned>(z.periods), z.settled ? "true" : "false");
    } else {
        put("%s", "\"last_sleep\":null,");
    }
    if (s.usual.known) {
        put("\"usual\":{\"bed_min\":%ld,\"wake_min\":%ld,\"nights\":%u,\"spread_min\":%ld},",
            static_cast<long>(Rhythm::minuteOfDay(s.usual.bed)),
            static_cast<long>(Rhythm::minuteOfDay(s.usual.wake)), static_cast<unsigned>(s.usual.nights),
            static_cast<long>(s.usual.spread));
        put("\"tonight\":{\"weekday\":%u,\"bed_min\":%ld,\"wake_min\":%ld,\"nights\":%u},",
            static_cast<unsigned>(Schedule::weekday(s.tonightDay)),
            static_cast<long>(Rhythm::minuteOfDay(s.usual.bed)),
            static_cast<long>(Rhythm::minuteOfDay(s.usual.wake)), static_cast<unsigned>(s.usual.nights));
    } else {
        put("%s", "\"usual\":null,\"tonight\":null,");
    }
    const NightSummary::Heart &h = s.lastHeart;
    if (p != nullptr && h.medianBpm != 0) {
        put("\"heart\":{\"median_bpm\":%u,", static_cast<unsigned>(h.medianBpm));
        if (h.lowBpm != 0) {
            put("\"low_bpm\":%u,\"low_at\":%lu},", static_cast<unsigned>(h.lowBpm),
                static_cast<unsigned long>(h.lowUtc));
        } else {
            put("%s", "\"low_bpm\":null,\"low_at\":null},");
        }
    } else {
        put("%s", "\"heart\":null,");
    }
    put("\"rest_goal_min\":%u,", static_cast<unsigned>(Rhythm::kRestGoalMin));
    const bool worn = p != nullptr && p->worn == SleepPeriods::Worn::Worn;
    if (worn) {
        put("\"rest_fill\":%u,", static_cast<unsigned>(Rhythm::restFill(p->restMin)));
    } else {
        put("%s", "\"rest_fill\":null,");
    }
    if (worn && s.usualBefore.known) {
        const int32_t d = Rhythm::offset(Rhythm::afterNoon(p->startUtc, s.offsetMin), s.usualBefore);
        put("\"rhythm_fill\":%u,\"rhythm_offset_min\":%ld}", static_cast<unsigned>(Rhythm::rhythmFill(d)),
            static_cast<long>(d));
    } else {
        put("%s", "\"rhythm_fill\":null,\"rhythm_offset_min\":null}");
    }
    return len < cap ? len : 0;
}

/// Writes `s` so that a crash at any step leaves one intact copy: `rename()`
/// will not replace a file on the watch, so tmp, drop .bak, now -> .bak, tmp -> now.
inline bool publish(SDK::Interface::IFileSystem &fs, const State &s)
{
    char         json[kCap];
    const size_t len = format(s, json, sizeof(json));
    if (len == 0 || !fs.mkdir(NightLog::kDir)) {
        return false;
    }
    {
        std::unique_ptr<SDK::Interface::IFile> tmp = fs.file(kTmpPath);
        if (!tmp || !tmp->open(true, true)) {
            return false;
        }
        size_t written = 0;
        bool   ok      = tmp->write(json, len, written) && written == len;
        ok             = tmp->flush() && ok;
        tmp->close();
        if (!ok) {
            fs.remove(kTmpPath);
            return false;
        }
    }
    if (fs.exist(kPath)) {
        if (fs.exist(kBakPath)) {
            fs.remove(kBakPath);
        }
        fs.rename(kPath, kBakPath);
    }
    return fs.rename(kTmpPath, kPath);
}

} // namespace SleepNow
} // namespace Slumber

#endif // SLUMBER_SLEEP_NOW_HPP
