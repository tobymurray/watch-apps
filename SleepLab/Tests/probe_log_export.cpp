/**
 * Write a plausible probe night with the real `Probe::Log`, so
 * `Tools/probe_report.py` can be run against the writer rather than against
 * this repo's reading of the writer.
 *
 *     ./probe-log-export /tmp/night
 *     python3 ../Tools/probe_report.py /tmp/night/probe_log.csv
 *
 * The night it fabricates is deliberately awkward rather than clean: it has a
 * launch that dies after two rows (a USB session), a delivery hole in the
 * small hours, a stretch not worn, and a battery that falls. Every one of
 * those is a case the report script has a branch for, and a synthetic night
 * with none of them would exercise none of them.
 *
 * The numbers are invented. Nothing here is evidence about the hardware -- it
 * is evidence about the format.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include "KernelTestDoubles.hpp"
#include "ProbeLog.hpp"

namespace {

/// Uptime the fake device booted the run at, and the wall clock it thinks it
/// is: 22:30 local on an arbitrary evening.
constexpr uint32_t kStartUptimeMs = 3'600'000;      // an hour since boot
constexpr int64_t  kStartWallUtc  = 1'755'037'800;  // 2025-08-12T22:30:00Z-ish

Probe::MinuteRow makeRow(int minute, bool worn, bool hrOn, int battPctX10)
{
    Probe::MinuteRow r;
    r.uptimeMs = kStartUptimeMs + static_cast<uint32_t>(minute) * 60'000u;
    r.wallUtc  = kStartWallUtc + minute * 60;
    r.localMin = static_cast<int16_t>((22 * 60 + 30 + minute) % 1440);
    r.spanMs   = 60'000;

    // 25 Hz requested. Delivered is deliberately not 1500: the thinning gate
    // does not honour a requested period naively, and a fixture that pretends
    // it does would let a reader assume the two are the same number.
    r.accN        = worn ? 1421 : 1418;
    r.accTsSpanMs = 59'880;
    r.accMaxGapMs = worn ? 48 : 42;
    r.accBatches  = 12;

    r.touchN     = 60;
    r.touchWornN = worn ? 60 : 0;
    r.touchEdges = 0;

    r.motionN   = 60;
    r.motionNo  = worn ? 58 : 60;
    r.motionMot = worn ? 2 : 0;
    r.motionSig = 0;

    r.arN     = 60;
    r.arStill = 60;
    r.arWalk  = 0;
    r.arRun   = 0;

    if (hrOn) {
        r.hrN        = worn ? 58 : 0;
        r.hrMeanX10  = worn ? 522 : -1;
        r.hrMin      = worn ? 49  : -1;
        r.hrMax      = worn ? 57  : -1;
        r.hrTrustX10 = worn ? 910 : -1;
        r.hrExN      = worn ? 58 : 0;
        r.hrExOptN   = worn ? 58 : 0;
        r.hrExExtN   = 0;
        r.hrExUnkN   = worn ? 0 : 0;
    }

    // The expected answer on 1.4, and the one the script says is consistent
    // with PR #167. A fixture that fabricated beats would make the script's
    // loudest branch the one nobody ever sees.
    r.beatN = 0;
    r.spo2N = 0;

    r.stepTotal = 8421;
    r.stepDelta = 0;

    r.battPctX10   = battPctX10;
    r.charging     = 0;
    r.usb          = 0;
    r.battMv       = 3900 - minute / 2;
    r.battMaX10    = -118;
    r.battAvgMaX10 = -115;
    r.battMah      = 180 - minute / 8;

    r.wakes = 74;
    r.msgs  = 74;
    return r;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string dir = (argc > 1) ? argv[1] : ".";
    const std::string path = dir + "/probe_log.csv";

    SDK::TestSupport::KernelFixture fx;

    // --- Launch 0: two rows, then the cable goes in. ------------------------
    {
        Probe::Log log(fx.kernel, Probe::kLogPath);
        log.begin(kStartUptimeMs - 300'000, kStartWallUtc - 300, "continuous");
        for (int m = -5; m < -3; m++) {
            log.write(makeRow(m, true, true, 890));
        }
    }

    // --- Launch 1: the night. -----------------------------------------------
    {
        Probe::Log log(fx.kernel, Probe::kLogPath);
        log.begin(kStartUptimeMs, kStartWallUtc, "continuous");

        int batt = 880;
        for (int m = 0; m < 480; m++) {
            // A twelve-minute hole at ~04:00, which is exactly the failure the
            // whole probe exists to detect. Simulated by writing no rows: a
            // service that stops being scheduled leaves a gap, not a row of
            // zeroes.
            if (m >= 330 && m < 342) {
                continue;
            }
            // Off the wrist for twenty minutes in the middle -- a nightstand
            // stretch, which must not read as flawless sleep.
            const bool worn = !(m >= 200 && m < 220);

            if (m % 6 == 0 && batt > 0) {
                batt -= 1;
            }
            log.write(makeRow(m, worn, true, batt));
        }
        std::printf("wrote %llu bytes, %lu failures\n",
                    static_cast<unsigned long long>(log.bytesWritten()),
                    static_cast<unsigned long>(log.failures()));
    }

    // The in-memory filesystem is not a real one, so spill it to disk for the
    // script to read.
    const std::string blob = fx.fileSystem.readFile(Probe::kLogPath);
    FILE *out = std::fopen(path.c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return 1;
    }
    std::fwrite(blob.data(), 1, blob.size(), out);
    std::fclose(out);

    std::printf("%s (%zu bytes)\n", path.c_str(), blob.size());
    return 0;
}
