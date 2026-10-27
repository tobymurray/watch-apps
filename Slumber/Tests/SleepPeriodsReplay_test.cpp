// Replays what a watch recorded through SleepPeriods.
//
// Recorded nights are personal data and this repository is public, so they are
// never checked in: point SLUMBER_REPLAY_DIR at a copy of a watch's
// Apps/SleepLab/Nights/, or SLUMBER_DAYS_DIR at a copy of Apps/Slumber/Days/,
// and that test runs, otherwise it skips. They assert properties that hold for
// anyone's recordings rather than one person's times.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "LastNight.hpp"
#include "Segments.hpp"
#include "SleepPeriods.hpp"

using namespace SleepPeriods;

namespace
{

struct Session {
    std::string        name;
    std::string        worn;  ///< SleepLab's verdict: worn, not-worn, uncertain
    std::vector<Epoch> epochs;
};

std::vector<std::string> split(const std::string &line)
{
    std::vector<std::string> out;
    std::stringstream        ss(line);
    std::string              cell;
    while (std::getline(ss, cell, ',')) {
        out.push_back(cell);
    }
    return out;
}

std::string verdictOf(const std::string &jsonPath)
{
    std::ifstream f(jsonPath);
    std::string   all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const size_t at = all.find("\"worn\":");
    if (at == std::string::npos) {
        return "?";
    }
    const size_t from = all.find('"', at + 7) + 1;
    return all.substr(from, all.find('"', from) - from);
}

std::vector<Session> loadSessions(const std::string &dir)
{
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *ent = readdir(d)) {
            const std::string n = ent->d_name;
            if (n.size() == 19 && n.rfind("20", 0) == 0 && n.substr(15) == ".csv") {
                names.push_back(n.substr(0, 15));
            }
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());

    std::vector<Session> sessions;
    for (const std::string &n : names) {
        Session s;
        s.name = n;
        s.worn = verdictOf(dir + "/" + n + ".json");
        std::ifstream            f(dir + "/" + n + ".csv");
        std::string              line;
        std::map<std::string, size_t> col;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const auto cells = split(line);
            if (col.empty()) {
                for (size_t i = 0; i < cells.size(); ++i) {
                    col[cells[i]] = i;
                }
                continue;
            }
            auto get = [&](const char *c) { return std::atol(cells.at(col.at(c)).c_str()); };
            Epoch e;
            e.utc        = static_cast<uint32_t>(get("wall_utc"));
            e.count      = static_cast<uint32_t>(std::max(0L, get("count")));
            e.steps      = static_cast<uint16_t>(std::max(0L, get("step_delta")));
            e.hasHr      = get("hr_samples") > 0;
            e.hrAsked    = get("hr_samples") >= 0;
            e.hrTrustX10 = static_cast<uint8_t>(std::max(0L, get("hr_trust_x10")));
            e.hrX10      = static_cast<uint16_t>(std::max(0L, get("hr_mean_x10")));
            s.epochs.push_back(e);
        }
        sessions.push_back(s);
    }
    return sessions;
}

const char *name(Worn w)
{
    return w == Worn::Worn ? "worn" : w == Worn::NotWorn ? "not-worn" : "uncertain";
}

std::string local(uint32_t utc)
{
    const std::time_t t = utc;
    char              buf[20];
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M", std::localtime(&t));
    return buf;
}

struct Replay {
    std::vector<Session> sessions;
    std::vector<Epoch>   epochs;
    std::vector<Period>  periods;
};

Replay replay(const char *dir)
{
    Replay r;
    r.sessions = loadSessions(dir);
    for (const Session &s : r.sessions) {
        r.epochs.insert(r.epochs.end(), s.epochs.begin(), s.epochs.end());
    }
    std::sort(r.epochs.begin(), r.epochs.end(),
              [](const Epoch &a, const Epoch &b) { return a.utc < b.utc; });
    r.periods.resize(256);
    r.periods.resize(find(r.epochs.data(), r.epochs.size(), r.periods.data(), r.periods.size(),
                          UINT32_MAX));
    return r;
}

/// The sessions with at least one epoch inside `p`.
std::vector<const Session *> contributors(const Replay &r, const Period &p)
{
    std::vector<const Session *> out;
    for (const Session &s : r.sessions) {
        for (const Epoch &e : s.epochs) {
            if (e.utc >= p.startUtc && e.utc < p.endUtc) {
                out.push_back(&s);
                break;
            }
        }
    }
    return out;
}

} // namespace

TEST(SleepPeriodsReplay, RecordedNights)
{
    const char *dir = std::getenv("SLUMBER_REPLAY_DIR");
    if (dir == nullptr) {
        GTEST_SKIP() << "SLUMBER_REPLAY_DIR is not set";
    }
    const Replay r = replay(dir);
    ASSERT_FALSE(r.sessions.empty()) << "no sessions in " << dir;

    std::printf("%-12s %-11s %6s %6s %5s %5s %-9s %s\n", "start", "end", "hours", "rest",
                "worn%", "miss", "verdict", "SleepLab sessions");
    for (const Period &p : r.periods) {
        std::string from;
        for (const Session *s : contributors(r, p)) {
            from += s->name.substr(4, 7) + "(" + s->worn + ") ";
        }
        std::printf("%-12s %-11s %6.1f %6u %5u %5u %-9s %s\n", local(p.startUtc).c_str(),
                    local(p.endUtc).substr(6).c_str(), (p.endUtc - p.startUtc) / 3600.0,
                    p.restMin, p.wornPct, p.missingMin, name(p.worn), from.c_str());
    }

    // A watch SleepLab found off the wrist must not become worn sleep here.
    for (const Period &p : r.periods) {
        const auto from    = contributors(r, p);
        const bool anyWorn = std::any_of(from.begin(), from.end(),
                                         [](const Session *s) { return s->worn == "worn"; });
        if (!anyWorn) {
            EXPECT_NE(p.worn, Worn::Worn) << "at " << local(p.startUtc);
        }
    }

    // Two worn sessions with under 40 minutes between them are one night split
    // by SleepLab's close rules; 40 leaves room for the waking epochs that
    // closed the first, which are inside it.
    for (size_t i = 0; i + 1 < r.sessions.size(); ++i) {
        const Session &a = r.sessions[i];
        const Session &b = r.sessions[i + 1];
        if (a.worn != "worn" || b.worn != "worn" || a.epochs.empty() || b.epochs.empty()) {
            continue;
        }
        const uint32_t gap = b.epochs.front().utc - a.epochs.back().utc;
        if (gap >= 40u * 60u) {
            continue;
        }
        const uint32_t midA = a.epochs[a.epochs.size() / 2].utc;
        const uint32_t midB = b.epochs[b.epochs.size() / 2].utc;
        const bool together = std::any_of(r.periods.begin(), r.periods.end(), [&](const Period &p) {
            return p.startUtc <= midA && midA < p.endUtc && p.startUtc <= midB && midB < p.endUtc;
        });
        EXPECT_TRUE(together) << a.name << " and " << b.name << " are " << gap / 60
                              << " min apart but in different periods";
    }
}

namespace
{

/// Slumber's day files, oldest first, as one run of epochs.
std::vector<Epoch> loadDays(const std::string &dir)
{
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *ent = readdir(d)) {
            const std::string n = ent->d_name;
            if (n.size() == 12 && n.rfind("20", 0) == 0 && n.substr(8) == ".csv") {
                names.push_back(n);
            }
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());

    std::vector<Epoch> out;
    for (const std::string &n : names) {
        std::ifstream                 f(dir + "/" + n);
        std::string                   line;
        std::map<std::string, size_t> col;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const auto cells = split(line);
            if (col.empty()) {
                for (size_t i = 0; i < cells.size(); ++i) {
                    col[cells[i]] = i;
                }
                continue;
            }
            auto get = [&](const char *c) { return std::atol(cells.at(col.at(c)).c_str()); };
            if (get("utc") <= 0) {
                continue;
            }
            Epoch e;
            // A row's `utc` is when it closed.
            e.utc        = static_cast<uint32_t>(get("utc")) - kEpochS;
            e.count      = static_cast<uint32_t>(get("count"));
            e.steps      = static_cast<uint16_t>(std::max(0L, get("steps")));
            e.hasHr      = get("hr_n") > 0;
            e.hrAsked    = get("hr_n") >= 0;
            e.hrTrustX10 = static_cast<uint8_t>(std::max(0L, get("hr_trust_x10")));
            e.hrX10      = static_cast<uint16_t>(std::max(0L, get("hr_mean_x10")));
            out.push_back(e);
        }
    }
    return out;
}

} // namespace

TEST(SleepPeriodsReplay, SlumberDays)
{
    const char *dir = std::getenv("SLUMBER_DAYS_DIR");
    if (dir == nullptr) {
        GTEST_SKIP() << "SLUMBER_DAYS_DIR is not set";
    }
    const std::vector<Epoch> e = loadDays(dir);
    ASSERT_FALSE(e.empty()) << "no rows in " << dir;

    std::vector<Period> p(64);
    p.resize(find(e.data(), e.size(), p.data(), p.size(), e.back().utc + kEpochS));
    std::printf("%-12s %-6s %6s %6s %5s %5s %s\n", "start", "end", "hours", "rest", "worn%",
                "hr%", "verdict");
    for (const Period &q : p) {
        std::printf("%-12s %-6s %6.1f %6u %5u %5u %s\n", local(q.startUtc).c_str(),
                    local(q.endUtc).substr(6).c_str(), (q.endUtc - q.startUtc) / 3600.0,
                    q.restMin, q.wornPct, q.hrCoverPct, name(q.worn));
    }

    const uint32_t          end = e.back().utc + kEpochS;
    const LastNight::Choice c   = LastNight::choose(p.data(), p.size(), end);
    if (c.period != nullptr) {
        std::printf("card at the last row: %s %s-%s\n",
                    c.kind == LastNight::Kind::Worn ? "night" : "table",
                    local(c.period->startUtc).c_str(), local(c.period->endUtc).substr(6).c_str());
    }

    // A period with heart rate asked for and never trusted is a table.
    for (const Period &q : p) {
        if (q.wristCoverPct >= kMinHrCoveragePct && q.wristPct == 0) {
            EXPECT_EQ(q.worn, Worn::NotWorn) << "at " << local(q.startUtc);
        }
    }
}

/// Every recorded night becomes one character per epoch slot, its movement
/// matches the raw epochs, and the lengths are printed, since they size the
/// shared sessions file's lines.
TEST(SegmentsReplay, EveryRecordedNightBecomesAStrip)
{
    const char *dir = std::getenv("SLUMBER_REPLAY_DIR");
    if (dir == nullptr) {
        GTEST_SKIP() << "SLUMBER_REPLAY_DIR is not set";
    }
    const Replay      r = replay(dir);
    std::vector<char> out(36u * 120u + 1u);
    size_t            longest = 0;
    for (const Period &p : r.periods) {
        const size_t k = Segments::strip(r.epochs.data(), r.epochs.size(), p, out.data(), out.size());
        ASSERT_EQ(k, Segments::slots(p));
        size_t moving = 0, rawMoving = 0, none = 0;
        for (size_t i = 0; i < k; ++i) {
            moving += out[i] == Segments::kMovement ? 1 : 0;
            none += out[i] == Segments::kNoData ? 1 : 0;
        }
        for (const Epoch &e : r.epochs) {
            rawMoving += e.utc >= p.startUtc && e.utc < p.endUtc &&
                                 detail::activity(e) >= kRestCountCeiling
                             ? 1
                             : 0;
        }
        EXPECT_EQ(moving, rawMoving) << "at " << local(p.startUtc);
        longest = std::max(longest, k);
        std::printf("%-12s %5.1f h %5zu slots %4zu moving %4zu no-data\n", local(p.startUtc).c_str(),
                    (p.endUtc - p.startUtc) / 3600.0, k, moving, none);
    }
    std::printf("longest strip: %zu slots over %zu periods\n", longest, r.periods.size());
}
