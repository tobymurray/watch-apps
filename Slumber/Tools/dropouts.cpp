// Real counts, made-up silence: how a recorded night survives heart-rate dropouts.
//
// Built from the headers the watch compiles. Usage: dropouts <csv>[,<csv>...] ...
// Each argument is one recording, SleepLab epoch CSVs of any schema joined by commas. For
// each it prints how often the count is above kSensorFloorCount, the longest stretch whose
// 5.5-minute median count is not, then silences heart rate for 30, 60, 90 and 180 minutes
// starting every 15 minutes across the longest period and counts how often the card still
// shows that period whole (within 10 minutes of each edge), a shorter one, or none; and
// last, what the card shows with heart rate silent throughout.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "LastNight.hpp"
#include "SleepPeriods.hpp"

using namespace SleepPeriods;

namespace
{

std::vector<std::string> split(const std::string &s, char c)
{
    std::vector<std::string> out;
    std::stringstream        ss(s);
    std::string              cell;
    while (std::getline(ss, cell, c)) {
        out.push_back(cell);
    }
    return out;
}

void load(const std::string &path, std::vector<Epoch> &out)
{
    std::ifstream                 f(path);
    std::string                   line;
    std::map<std::string, size_t> col;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto cells = split(line, ',');
        if (col.empty()) {
            for (size_t i = 0; i < cells.size(); ++i) {
                col[cells[i]] = i;
            }
            continue;
        }
        auto  get = [&](const char *c) { return std::atol(cells.at(col.at(c)).c_str()); };
        Epoch e;
        e.utc        = static_cast<uint32_t>(get("wall_utc"));
        e.count      = static_cast<uint32_t>(std::max(0L, get("count")));
        e.steps      = static_cast<uint16_t>(std::max(0L, get("step_delta")));
        e.hasHr      = get("hr_samples") > 0;
        e.hrAsked    = get("hr_samples") >= 0;
        e.hrTrustX10 = static_cast<uint8_t>(std::max(0L, get("hr_trust_x10")));
        e.hrX10      = static_cast<uint16_t>(std::max(0L, get("hr_mean_x10")));
        out.push_back(e);
    }
}

uint32_t medianCount(const std::vector<Epoch> &e, size_t i)
{
    std::vector<uint32_t> w;
    for (size_t j = i >= kMedianHalfWidth ? i - kMedianHalfWidth : 0; j < e.size() && j <= i + kMedianHalfWidth; ++j) {
        const uint32_t d = e[j].utc > e[i].utc ? e[j].utc - e[i].utc : e[i].utc - e[j].utc;
        if (d <= kMedianHalfWidth * kEpochS) {
            w.push_back(e[j].count);
        }
    }
    std::sort(w.begin(), w.end());
    return w[w.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
    for (int a = 1; a < argc; ++a) {
        std::vector<Epoch> base;
        for (const std::string &p : split(argv[a], ',')) {
            load(p, base);
        }
        if (base.empty()) {
            continue;
        }
        std::sort(base.begin(), base.end(), [](const Epoch &x, const Epoch &y) { return x.utc < y.utc; });
        base.erase(std::unique(base.begin(), base.end(), [](const Epoch &x, const Epoch &y) { return x.utc == y.utc; }),
                   base.end());

        size_t above = 0, run = 0, longest = 0;
        for (size_t i = 0; i < base.size(); ++i) {
            above += base[i].count > kSensorFloorCount ? 1 : 0;
            run     = medianCount(base, i) <= kSensorFloorCount ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        const uint32_t now = base.back().utc + 3 * 3600;
        Period         ref[64];
        const size_t   rn = find(base.data(), base.size(), ref, 64, now);
        if (rn == 0) {
            continue;
        }
        const Period *night = std::max_element(ref, ref + rn, [](const Period &x, const Period &y) {
            return x.endUtc - x.startUtc < y.endUtc - y.startUtc;
        });
        std::printf("%s\n  %zu epochs, %.1f%% above the floor; longest 5.5-min median at it %.1f min; "
                    "longest period %u min, rest %u, %s\n",
                    argv[a], base.size(), 100.0 * static_cast<double>(above) / static_cast<double>(base.size()),
                    static_cast<double>(longest) * kEpochS / 60.0, (night->endUtc - night->startUtc) / 60,
                    night->restMin, night->worn == Worn::Worn ? "worn" : night->worn == Worn::NotWorn ? "not worn" : "uncertain");
        for (uint32_t silentMin : {30u, 60u, 90u, 180u}) {
            int whole = 0, shorter = 0, none = 0;
            for (uint32_t from = night->startUtc; from + silentMin * 60u <= night->endUtc; from += 15u * 60u) {
                std::vector<Epoch> e = base;
                for (Epoch &x : e) {
                    if (x.utc >= from && x.utc < from + silentMin * 60u) {
                        x.hasHr = false;
                    }
                }
                Period       p[64];
                const size_t n = find(e.data(), e.size(), p, 64, now);
                const auto   c = LastNight::choose(p, n, now);
                if (c.kind != LastNight::Kind::Worn) {
                    ++none;
                } else if (c.period->startUtc <= night->startUtc + 600 && c.period->endUtc + 600 >= night->endUtc) {
                    ++whole;
                } else {
                    ++shorter;
                }
            }
            std::printf("  silent %3u min: whole %2d, shorter %2d, none %2d\n", silentMin, whole, shorter, none);
        }
        std::vector<Epoch> e = base;
        for (Epoch &x : e) {
            x.hasHr = false;
        }
        Period       p[64];
        const size_t n = find(e.data(), e.size(), p, 64, now);
        const auto   c = LastNight::choose(p, n, now);
        std::printf("  silent throughout: %s", c.kind == LastNight::Kind::Worn      ? "a night"
                                               : c.kind == LastNight::Kind::NotWorn ? "watch was off"
                                                                                    : "no night");
        if (c.period != nullptr) {
            std::printf(", %u min of rest", c.period->restMin);
        }
        std::printf("\n");
    }
    return 0;
}
