// What the card and the learned night say for synthetic nights broken in the middle.
//
// Built from the headers the watch compiles. Usage: splits
// Prints, for an awakening at 03:00, a heart-rate dropout at 03:00 and an
// evening doze before a 23:30 bedtime: the periods SleepPeriods finds, the
// sleep the card shows, and the main night Rhythm::Recent learns from them.
// Sleeping counts are 260-900 with 8% at the sensor floor, as README "Finding
// a night" measured; trusted heart rate reads 55 bpm.

#include <cstdio>
#include <functional>
#include <random>
#include <vector>

#include "LastNight.hpp"
#include "Rhythm.hpp"
#include "Schedule.hpp"
#include "SleepPeriods.hpp"

using namespace SleepPeriods;

namespace
{

constexpr uint32_t kDay0 = 20000u * 86400u;

uint32_t at(int day, int h, int m)
{
    return kDay0 + static_cast<uint32_t>(day) * 86400u + static_cast<uint32_t>(h * 3600 + m * 60);
}

struct Night {
    std::mt19937       rng{1};
    std::vector<Epoch> e;

    int ui(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }

    void span(uint32_t from, uint32_t to, const std::function<void(Epoch &)> &f)
    {
        for (uint32_t t = from; t < to; t += kEpochS) {
            Epoch x;
            x.utc     = t;
            x.hrAsked = true;
            f(x);
            e.push_back(x);
        }
    }

    void awake(uint32_t from, uint32_t to, int lo = 2000, int hi = 9000)
    {
        span(from, to, [&](Epoch &x) {
            x.count      = static_cast<uint32_t>(ui(lo, hi));
            x.hasHr      = true;
            x.hrTrustX10 = 30;
            x.hrX10      = 700;
        });
    }

    void asleep(uint32_t from, uint32_t to, uint32_t silentFrom = 0, uint32_t silentTo = 0)
    {
        span(from, to, [&](Epoch &x) {
            x.count      = static_cast<uint32_t>(ui(1, 100) <= 8 ? ui(185, 245) : ui(260, 900));
            x.hasHr      = !(x.utc >= silentFrom && x.utc < silentTo);
            x.hrTrustX10 = 30;
            x.hrX10      = 550;
        });
    }

    void report(const char *what, uint32_t now)
    {
        Period       p[32];
        const size_t n = find(e.data(), e.size(), p, 32, now);
        Rhythm::Recent r;
        for (size_t i = 0; i < n; ++i) {
            if (p[i].worn == Worn::Worn) {
                r.add(p[i].startUtc, p[i].endUtc, 0, p[i].restMin);
            }
        }
        const LastNight::Choice c = LastNight::choose(p, n, now);
        const Schedule::Bars    b = Schedule::bars(r, Rhythm::noonDay(now, 0) + 1);
        const int bed = Rhythm::minuteOfDay(b.bed[Schedule::kWeekNights - 1]);
        const int wake = Rhythm::minuteOfDay(b.wake[Schedule::kWeekNights - 1] % 1440);
        const uint32_t from = c.sleep.startUtc / 60 % 1440, to = c.sleep.endUtc / 60 % 1440;
        std::printf("%-38s %2zu periods | card %02u:%02u-%02u:%02u %uh%02u | learned %02d:%02d-%02d:%02d\n", what, n,
                    from / 60, from % 60, to / 60, to % 60, c.sleep.restMin / 60u, c.sleep.restMin % 60u, bed / 60,
                    bed % 60, wake / 60, wake % 60);
    }
};

} // namespace

int main()
{
    char what[48];
    for (int up : {30, 59, 61, 90, 150, 200}) {
        Night n;
        n.awake(at(0, 21, 0), at(0, 23, 0));
        n.asleep(at(0, 23, 0), at(1, 3, 0));
        n.awake(at(1, 3, 0), at(1, 3, up), 1500, 4000);
        n.asleep(at(1, 3, up), at(1, 7, 30));
        n.awake(at(1, 7, 30), at(1, 11, 0));
        std::snprintf(what, sizeof(what), "up %d min at 03:00", up);
        n.report(what, at(1, 11, 0));
    }
    for (int silent : {20, 29, 30, 90}) {
        Night n;
        n.awake(at(0, 21, 0), at(0, 23, 0));
        n.asleep(at(0, 23, 0), at(1, 7, 0), at(1, 3, 0), at(1, 3, silent));
        n.awake(at(1, 7, 0), at(1, 11, 0));
        std::snprintf(what, sizeof(what), "heart rate silent %d min at 03:00", silent);
        n.report(what, at(1, 11, 0));
    }
    for (int before : {60, 105, 170, 190}) {
        Night n;
        n.awake(at(0, 19, 0), at(0, 23, 30) - static_cast<uint32_t>(before + 45) * 60u);
        n.asleep(at(0, 23, 30) - static_cast<uint32_t>(before + 45) * 60u, at(0, 23, 30) - static_cast<uint32_t>(before) * 60u);
        n.awake(at(0, 23, 30) - static_cast<uint32_t>(before) * 60u, at(0, 23, 30));
        n.asleep(at(0, 23, 30), at(1, 7, 0));
        n.awake(at(1, 7, 0), at(1, 11, 0));
        std::snprintf(what, sizeof(what), "45-min doze ending %d min before bed", before);
        n.report(what, at(1, 11, 0));
    }
    return 0;
}
