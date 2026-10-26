// How often the watch claims a usual for synthetic sleepers, regular and not.
//
// Built from the headers the watch compiles. Usage: irregular
// Prints, per schedule over 20 seeds and evenings 21-120: the share of evenings
// with a usual, how often the next bedtime fell within 2 h of it, and the share
// with a Tonight; then how many nights a permanent switch takes to relearn.

#include <cstdio>
#include <random>

#include "Rhythm.hpp"
#include "Schedule.hpp"

namespace
{

constexpr int32_t kFirstDay = 20000;  // a noon-to-noon day index; any will do
constexpr int     kNights   = 120;
constexpr int     kFrom     = 20;
constexpr int     kSeeds    = 20;

struct Sleep {
    int32_t bedMin;   ///< minute of day
    int32_t lenMin;
};

const char *const kNames[] = {"fixed 23:00",         "fixed, Fri/Sat 01:30", "fixed, 2 nights out 03:00",
                              "day sleeper 08:30",   "near noon 11:40-12:20", "weekly rota",
                              "3-on-3-off",          "4-on-4-off",            "alternate weeks",
                              "random"};
constexpr int kSchedules = sizeof(kNames) / sizeof(kNames[0]);

Sleep night(int schedule, int s, std::mt19937 &rng)
{
    auto          jit   = [&]() { return std::uniform_int_distribution<int>(-20, 20)(rng); };
    const uint8_t wd    = Schedule::weekday(kFirstDay + s);
    const Sleep   late  = {23 * 60 + jit(), 450};
    const Sleep   early = {22 * 60 + 30 + jit(), 450};
    const Sleep   day   = {8 * 60 + 30 + jit(), 420};
    switch (schedule) {
    case 0: return late;
    case 1: return wd == Schedule::kFriday || wd == Schedule::kSaturday ? Sleep{90 + jit(), 450} : late;
    case 2: return std::uniform_int_distribution<int>(0, 6)(rng) < 2 ? Sleep{180 + jit(), 360} : late;
    case 3: return day;
    case 4: return {(11 * 60 + 40 + std::uniform_int_distribution<int>(0, 40)(rng)) % 1440, 420};
    case 5: return wd >= 1 && wd <= 4 ? day : late;
    case 6: return s % 6 < 3 ? early : day;
    case 7: return s % 8 < 4 ? early : day;
    case 8: return (s / 7) % 2 == 0 ? early : day;
    default: return {std::uniform_int_distribution<int>(0, 1439)(rng), 420};
    }
}

/// The UTC start of a sleep at `bedMin` that belongs to noon-to-noon day `d`, at offset 0.
uint32_t startOf(int32_t d, int32_t bedMin)
{
    const int64_t noon = static_cast<int64_t>(d) * 86400 + 43200;
    return static_cast<uint32_t>(noon + static_cast<int64_t>((bedMin + 720) % 1440) * 60);
}

int32_t away(int32_t a, int32_t b)
{
    const int32_t d = Rhythm::apart(a, b);
    return d < 0 ? -d : d;
}

} // namespace

int main()
{
    std::printf("%-26s %8s %8s %8s\n", "schedule", "usual", "kept", "tonight");
    for (int sc = 0; sc < kSchedules; ++sc) {
        long evenings = 0, usual = 0, kept = 0, tonight = 0;
        for (int seed = 0; seed < kSeeds; ++seed) {
            std::mt19937      rng(1000 * seed + sc);
            Rhythm::Recent    recent;
            Schedule::Learner learner;
            for (int s = 0; s < kNights; ++s) {
                const Sleep    sl    = night(sc, s, rng);
                const uint32_t start = startOf(kFirstDay + s, sl.bedMin);
                if (s >= kFrom) {
                    const Rhythm::Usual      u = recent.usualBefore(UINT32_MAX);
                    const Schedule::Expected e = Schedule::agree(learner.expect(kFirstDay + s), u);
                    ++evenings;
                    usual += u.known;
                    kept += u.known && away(Rhythm::afterNoon(start, 0), u.bed) <= Rhythm::kEmptyMin;
                    tonight += e.known;
                }
                recent.add(start, start + sl.lenMin * 60, 0, static_cast<uint16_t>(sl.lenMin));
                learner.add(start, start + sl.lenMin * 60, 0);
            }
        }
        std::printf("%-26s %7ld%% %7ld%% %7ld%%\n", kNames[sc], 100 * usual / evenings,
                    usual ? 100 * kept / usual : 0, 100 * tonight / evenings);
    }

    Rhythm::Recent    recent;
    Schedule::Learner learner;
    for (int s = 0; s < 56; ++s) {
        const uint32_t start = startOf(kFirstDay + s, 23 * 60);
        recent.add(start, start + 450 * 60, 0, 450);
        learner.add(start, start + 450 * 60, 0);
    }
    int stale = 0, back = 0;
    for (int k = 1; k <= 60 && back == 0; ++k) {
        const int32_t  d     = kFirstDay + 55 + k;
        const uint32_t start = startOf(d, 8 * 60 + 30);
        recent.add(start, start + 420 * 60, 0, 420);
        learner.add(start, start + 420 * 60, 0);
        const Schedule::Expected e = Schedule::agree(learner.expect(d + 1), recent.usualBefore(UINT32_MAX));
        stale = e.known && away(e.bed, Rhythm::afterNoon(startOf(0, 23 * 60), 0)) == 0 ? k : stale;
        back  = e.known && away(e.bed, Rhythm::afterNoon(start, 0)) <= 30 ? k : 0;
    }
    std::printf("23:00 to 08:30 for good: the old Tonight through night %d, the new from night %d\n", stale, back);
    return 0;
}
