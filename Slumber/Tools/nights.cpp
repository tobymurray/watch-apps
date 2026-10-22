// What the watch decides about a set of day files, for analyse.py.
//
// Built from the headers the watch compiles, so the desk sees the same rest,
// wrist, periods, summary and card. Usage: nights <Days/yyyymmdd.csv>...
// Prints CSV lines tagged `epoch`, `period`, `card` or `skip`.

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "DayRow.hpp"
#include "EpochRow.hpp"
#include "LastNight.hpp"
#include "NightSummary.hpp"
#include "SleepPeriods.hpp"

using namespace SleepPeriods;

namespace
{

const char *wristName(detail::Wrist w)
{
    return w == detail::Wrist::On ? "on" : w == detail::Wrist::Off ? "off" : "unknown";
}

const char *wornName(Worn w)
{
    return w == Worn::Worn ? "worn" : w == Worn::NotWorn ? "not-worn" : "uncertain";
}

/// Appends the rows of a day file written under this build's header.
bool load(const char *path, std::vector<Epoch> &out)
{
    std::ifstream     f(path, std::ios::binary);
    const std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const size_t      at = all.find(Slumber::EpochRow::kHeader);
    if (at == std::string::npos || (at > 0 && all[at - 1] != '\n')) {
        return false;
    }
    Slumber::DayRow::Reader rows;
    for (size_t i = at + std::strlen(Slumber::EpochRow::kHeader); i < all.size(); ++i) {
        Epoch e;
        if (rows.take(all[i], e)) {
            if (!out.empty() && e.utc <= out.back().utc) {
                out.clear();
            }
            out.push_back(e);
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<Epoch> e;
    for (int i = 1; i < argc; ++i) {
        if (!load(argv[i], e)) {
            std::printf("skip,%s\n", argv[i]);
        }
    }
    if (e.empty()) {
        return 0;
    }
    for (size_t i = 0; i < e.size(); ++i) {
        std::printf("epoch,%u,%d,%s\n", e[i].utc, detail::isRest(e.data(), e.size(), i) ? 1 : 0,
                    wristName(detail::wrist(e.data(), e.size(), i)));
    }
    const uint32_t      now = e.back().utc + kEpochS;
    std::vector<Period> p(256);
    p.resize(find(e.data(), e.size(), p.data(), p.size(), now));
    for (const Period &q : p) {
        const NightSummary::Summary s = NightSummary::summarise(e.data(), e.size(), q);
        std::printf("period,%u,%u,%u,%u,%u,%u,%s,%u,%u,%d\n", q.startUtc, q.endUtc, q.restMin,
                    q.missingMin, s.longestRestMin, s.restlessSpells, wornName(q.worn), q.wornPct,
                    q.hrCoverPct, q.settled ? 1 : 0);
    }
    const LastNight::Choice c = LastNight::choose(p.data(), p.size(), now);
    if (c.period != nullptr) {
        std::printf("card,%s,%u,%u\n", c.kind == LastNight::Kind::Worn ? "night" : "watch-off",
                    c.period->startUtc, c.period->endUtc);
    }
    return 0;
}
