/**
 ******************************************************************************
 * @file    profile_export.cpp
 * @brief   Not a test: writes real files with the real writers.
 ******************************************************************************
 *
 * The unit tests pin the writers to this repository's reading of its own
 * format. This pins them to the readers that actually consume it: it drives a
 * scenario through the real `Service`, dumps everything the run wrote to a
 * directory, and `RunReportRoundtrip.cmake` then runs `profile_report.py` and
 * `profile_diff.py` over the result.
 *
 * Two profiles are exported, from two different "firmware versions" with a
 * deliberate behavioural difference between them, so the diff tool has
 * something real to find rather than two identical files.
 *
 * Usage: profile-export <outdir>
 *
 ******************************************************************************
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#include "RunHarness.hpp"

namespace
{

bool writeOut(const std::string &dir, const std::string &name,
              const std::string &content)
{
    // The volume is flat here; the app writes into `runs/`, so a slash in a
    // name becomes a subdirectory that has to exist.
    const size_t slash = name.find('/');
    if (slash != std::string::npos) {
        const std::string sub = dir + "/" + name.substr(0, slash);
        ::mkdir(sub.c_str(), 0755);
    }

    const std::string path = dir + "/" + name;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "profile-export: cannot write %s\n", path.c_str());
        return false;
    }
    const size_t n = std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return n == content.size();
}

/// Everything a run left on the volume, out to a real directory.
bool dump(Harness::Runner &run, const std::string &dir)
{
    ::mkdir(dir.c_str(), 0755);
    for (const auto &kv : run.fs().files) {
        if (!kv.second.exists) {
            continue;
        }
        if (!writeOut(dir, kv.first, kv.second.content)) {
            return false;
        }
    }
    return true;
}

/// A scenario whose channels behave the way hardware has been measured to.
Harness::Scenario device(const std::string &firmware, bool cadenceRegression)
{
    Harness::Scenario s;
    s.firmware   = firmware;
    s.hardware   = "HW_v2.1";
    s.durationMs = 6u * 60u * 1000u;
    s.settingsJson =
        "{\"schema\":1,\"values\":{\"interval_sec\":30,\"period_ms\":40,"
        "\"latency_ms\":5000}}";
    s.commandAtMs  = 1000;
    s.command      = CustomMessage::Command::StartSoak;
    s.guiOpensAtMs = 200000;

    Harness::Channel accel = Harness::accelerometerAsMeasured(1);
    accel.quantum = 4.0f / 65536.0f;   // a 16-bit ADC at +-2 g
    if (cadenceRegression) {
        // The second "firmware" delivers the accelerometer at half the rate and
        // in a wider frame. Both are the kind of change `profile_diff.py` exists
        // to surface, and `RUNNING_CADENCE`'s 4 -> 2 field shrink says the second
        // is not hypothetical.
        accel.deliveredPeriodMs = 42;
        accel.fields            = 4;
    }
    s.channels.push_back(accel);

    s.channels.push_back(Harness::touchAsMeasured(2));

    Harness::Channel hr;
    hr.type              = 0x41;
    hr.handle            = 3;
    hr.descriptor        = "pah8316-hr";
    hr.deliveredPeriodMs = 1000;
    hr.deliveredBatchMs  = 1000;
    hr.fields            = 2;
    hr.valueBase         = 58.0f;
    hr.valueStep         = 0.01f;
    s.channels.push_back(hr);

    Harness::Channel batt;
    batt.type              = 0x120;
    batt.handle            = 4;
    batt.descriptor        = "max17262-soc";
    batt.deliveredPeriodMs = 10000;
    batt.deliveredBatchMs  = 10000;
    batt.fields            = 1;
    batt.valueBase         = 100.0f;
    // Ledger row S18: the percent gauge did not move at all across 8.45 h in
    // which the fuel gauge lost 10 mAh.
    batt.stuck             = true;
    s.channels.push_back(batt);

    // The two types with no producer on this firmware.
    s.channels.push_back(Harness::noProducer(0xF1));   // SPO2, row S4
    s.channels.push_back(Harness::noProducer(0x40));   // HEART_BEAT, row S5

    return s;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
        return 2;
    }
    const std::string out = argv[1];
    ::mkdir(out.c_str(), 0755);

    {
        Harness::Runner run;
        run.execute(device("1.4.0", false));
        if (!dump(run, out + "/fw-1.4.0")) { return 1; }
    }
    {
        Harness::Runner run;
        run.execute(device("1.5.0", true));
        if (!dump(run, out + "/fw-1.5.0")) { return 1; }
    }

    std::printf("profile-export: wrote two profiles under %s\n", out.c_str());
    return 0;
}
