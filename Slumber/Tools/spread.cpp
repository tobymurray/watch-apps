// How often Last night calls an ordinary sleeper late or early, under a fixed window and under
// a reach of k times the wearer's own spread.
//
// Built from the headers the watch compiles. Usage: spread
// Sleepers go to bed at 23:00 plus a normal error of the given SD, with 0-2 nights a week three
// hours late, 40 seeds of 60 nights each, scored from night 15. Prints the share of scored
// nights beyond 30 minutes, beyond max(30, k * spread) for each k, and the spread's quartiles.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "Rhythm.hpp"

int main()
{
    const double ks[] = {1.0, 1.5, 2.0, 2.5, 3.0};
    std::printf("%4s %6s | %6s | %5s %5s %5s %5s %5s | %s\n", "sd", "out/wk", ">30m", "k1", "k1.5", "k2", "k2.5",
                "k3", "spread p25/p50/p75");
    for (int out : {0, 1, 2}) {
        for (int sd : {15, 30, 45, 60, 90}) {
            long             scored = 0, fixed = 0, flagged[5] = {};
            std::vector<int> spreads;
            for (int seed = 0; seed < 40; ++seed) {
                std::mt19937                       rng(static_cast<unsigned>(seed * 77 + sd + 1000 * out));
                std::normal_distribution<double>   bedError(0, sd);
                std::uniform_int_distribution<int> weekday(0, 6);
                Rhythm::Recent                     recent;
                for (int d = 0; d < 60; ++d) {
                    int bed = 23 * 60 + static_cast<int>(std::lround(bedError(rng)));
                    if (weekday(rng) < out) {
                        bed += 180;
                    }
                    const uint32_t start = 20000u * 86400u + static_cast<uint32_t>(d) * 86400u + 43200u +
                                           static_cast<uint32_t>(((bed - 720) % 1440 + 1440) % 1440) * 60u;
                    const Rhythm::Usual u = recent.usualBefore(start);
                    if (d >= 14 && u.known) {
                        ++scored;
                        const int off = std::abs(Rhythm::offset(Rhythm::afterNoon(start, 0), u));
                        fixed += off > 30;
                        for (int i = 0; i < 5; ++i) {
                            flagged[i] += off > std::max(30.0, ks[i] * u.spread);
                        }
                        spreads.push_back(u.spread);
                    }
                    recent.add(start, start + 450 * 60, 0, 450);
                }
            }
            std::sort(spreads.begin(), spreads.end());
            auto q = [&](double f) { return spreads[static_cast<size_t>(f * static_cast<double>(spreads.size() - 1))]; };
            std::printf("%4d %6d | %5ld%% |", sd, out, 100 * fixed / scored);
            for (long f : flagged) {
                std::printf(" %4ld%%", 100 * f / scored);
            }
            std::printf(" | %d/%d/%d\n", q(0.25), q(0.5), q(0.75));
        }
    }
    return 0;
}
