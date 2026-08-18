/**
 * Write plausible recorded nights with the real `NightStore`, so
 * `Tools/night_report.py` can be run against the writer rather than against
 * this repo's reading of the writer.
 *
 *     ./night-log-export /tmp/nights
 *     python3 ../Tools/night_report.py thresholds \
 *         --worn /tmp/nights/worn --table /tmp/nights/table
 *     python3 ../Tools/night_report.py diary /tmp/nights/worn \
 *         --diary /tmp/nights/diary.csv
 *
 * Three worn nights and one on a table, plus a diary that matches them. The
 * counts are invented, but their *shape* is not arbitrary: the worn nights
 * carry a floor of micro-movement a living wrist would produce and occasional
 * turn-overs, and the table night carries only a trickle. That is what makes
 * the threshold suggestion exercise its interesting branch -- the one where
 * the two distributions separate -- rather than only its error path.
 *
 * Nothing here is evidence about sleep, or about this hardware. It is evidence
 * that the two halves of the format agree.
 */

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "KernelTestDoubles.hpp"
#include "NightStore.hpp"

namespace {

/// The first night's start, and a night a day after it.
///
/// The diary dates below are *derived* from this rather than written out, so
/// the two cannot drift apart -- which they did once, silently, and the round
/// trip passed anyway because it only checked that the script declined to
/// quote an accuracy figure off three nights. It declines off zero too.
constexpr int64_t kFirstNight = 1755642600;

/// The local calendar date of @p utc, as the night files are named.
std::string localDate(int64_t utc)
{
    const std::time_t t = static_cast<std::time_t>(utc);
    std::tm local {};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buf;
}

/// A deterministic pseudo-random sequence. `std::rand` would make the fixture
/// depend on the platform's libc, and the whole point of a fixture is that two
/// people generating one get the same file.
uint32_t nextRandom(uint32_t &state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 16;
}

Engine::Epoch makeEpoch(int64_t startUtc, int index, bool worn, uint32_t &rng)
{
    Engine::Epoch e;
    e.uptimeMs = static_cast<uint32_t>(3600000 + index * 30000);
    e.wallUtc  = startUtc + index * 30;
    e.spanMs   = 30000;
    e.samples  = 700;

    if (worn) {
        // A living wrist: a floor of micro-movement, with a turn-over every
        // twenty minutes or so. Respiration alone moves a sleeping wrist,
        // which is the entire basis of the plausibility check.
        e.count = 18 + (nextRandom(rng) % 22);
        if (index % 41 == 0) {
            e.count += 300 + (nextRandom(rng) % 500);
        }
        e.wornPct   = 100;
        e.hrMeanX10 = static_cast<int16_t>(505 + (nextRandom(rng) % 60));
        e.hrMinX10  = e.hrMeanX10;
        e.hrSamples = 28;
        e.hrSource  = Engine::HrSource::Optical;
    } else {
        // Furniture: sensor noise and nothing else, and no pulse at all.
        e.count   = nextRandom(rng) % 4;
        e.wornPct = 100;   // the premise: TOUCH_DETECT is wrong
    }

    e.peak       = e.count * 3;
    e.stepDelta  = 0;
    e.battPctX10 = static_cast<int16_t>(880 - index / 6);
    return e;
}

/// Record one whole night into `dir`, and return its epoch CSV path.
std::string writeNight(SDK::TestSupport::KernelFixture &fx, int64_t startUtc,
                       int epochs, bool worn, uint32_t seed)
{
    SleepLab::NightStore store(fx.kernel);
    if (!store.beginNight(startUtc, 3600000)) {
        return {};
    }

    uint32_t rng = seed;
    for (int i = 0; i < epochs; i++) {
        store.appendEpoch(makeEpoch(startUtc, i, worn, rng), 0);
    }

    const std::string path = store.path();

    Engine::NightSummary s;
    s.epochs = static_cast<size_t>(epochs) / 2;
    if (worn) {
        s.worn            = Engine::WornVerdict::Worn;
        s.hasSleep        = true;
        s.timeInBedMin    = static_cast<int32_t>(s.epochs);
        // Onset a quarter of an hour in, final wake near the end -- close
        // enough to the diary below that the comparison is a real one rather
        // than a shape test.
        s.onsetEpoch      = 17;
        s.onsetLatencyMin = 17;
        s.finalWakeEpoch  = static_cast<int32_t>(s.epochs) - 4;
        s.totalSleepMin   = s.finalWakeEpoch - s.onsetEpoch - 22;
        s.wasoMin         = 22;
        s.awakenings      = 3;
        s.stillInBedMin   = s.totalSleepMin - 30;
        s.efficiencyPct   = s.totalSleepMin * 100 / s.timeInBedMin;
        s.movementIndexPct = 11;
        s.hrMinX10        = 505;
        s.hrMeanX10       = 534;
        s.hrMinEpoch      = static_cast<int32_t>(s.epochs) / 2;
        s.hrEpochs        = s.epochs;
    } else {
        s.worn = Engine::WornVerdict::NotWorn;
    }

    store.finishNight(s, "movement+hr-relative-to-night-min, 4-level ordinal, "
                         "not a sleep stage; v1", worn, "continuous");
    return path;
}

/// Spill the in-memory filesystem to disk under `root`.
bool spill(SDK::TestSupport::KernelFixture &fx, const std::string &root)
{
    for (const auto &entry : fx.fileSystem.files) {
        if (!entry.second.exists) {
            continue;
        }
        const std::string out = root + "/" + entry.first;
        const size_t slash = out.find_last_of('/');
        if (slash != std::string::npos) {
            std::string cmd = "mkdir -p '" + out.substr(0, slash) + "'";
            if (std::system(cmd.c_str()) != 0) {
                return false;
            }
        }
        FILE *f = std::fopen(out.c_str(), "wb");
        if (!f) {
            return false;
        }
        std::fwrite(entry.second.content.data(), 1, entry.second.content.size(), f);
        std::fclose(f);
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string root = (argc > 1) ? argv[1] : ".";

    // Separate fixtures, so the worn nights and the table night land in
    // different directories -- which is how the threshold command is invoked.
    {
        SDK::TestSupport::KernelFixture fx;
        for (int i = 0; i < 3; i++) {
            writeNight(fx, kFirstNight + i * 86400, 900, /*worn=*/true,
                       0x1234u + static_cast<uint32_t>(i) * 7919u);
        }
        if (!spill(fx, root + "/worn")) {
            std::fprintf(stderr, "cannot write under %s/worn\n", root.c_str());
            return 1;
        }
    }
    {
        SDK::TestSupport::KernelFixture fx;
        writeNight(fx, kFirstNight + 3 * 86400, 900, /*worn=*/false, 0x99u);
        if (!spill(fx, root + "/table")) {
            std::fprintf(stderr, "cannot write under %s/table\n", root.c_str());
            return 1;
        }
    }

    // A diary matching the three worn nights. Times to the nearest five
    // minutes, as the script's own documentation asks for -- nobody knows when
    // they fell asleep to the minute, and a diary claiming otherwise invites a
    // comparison it cannot support.
    const std::string diaryPath = root + "/diary.csv";
    FILE *d = std::fopen(diaryPath.c_str(), "w");
    if (!d) {
        std::fprintf(stderr, "cannot write %s\n", diaryPath.c_str());
        return 1;
    }
    std::fprintf(d, "date,lights_out,woke\n");
    static const char *kLights[] = { "22:45", "22:50", "22:40" };
    static const char *kWoke[]   = { "06:10", "06:05", "06:20" };
    for (int i = 0; i < 3; i++) {
        std::fprintf(d, "%s,%s,%s\n",
                     localDate(kFirstNight + i * 86400).c_str(),
                     kLights[i], kWoke[i]);
    }
    std::fclose(d);

    std::printf("%s: 3 worn nights, 1 table night, 1 diary\n", root.c_str());
    return 0;
}
