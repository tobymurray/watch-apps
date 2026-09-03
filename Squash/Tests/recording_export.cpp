/**
 ******************************************************************************
 * @file    recording_export.cpp
 * @brief   Writes a recording with the real writers, for the real reader to read.
 ******************************************************************************
 *
 * The watch writes three files in C++ and the analyser reads them in Rust. Every
 * other test checks one side or the other; nothing checked that they agree, and
 * a disagreement is not visible until a session has already been played and the
 * data turns out to be unreadable.
 *
 * So this writes a short session through `ImuCsvRecorder`, `ImuMarkerLog` and
 * `HrCsvLog` -- the same objects the Service uses, through the same sink
 * interface -- into a directory the caller names. `Tools/docker-build.sh
 * roundtrip` then runs `phase-a` over the result and fails on any warning,
 * which is what a format disagreement produces.
 *
 * Usage: recording-export <out-dir>
 */

#include "HrCsvLog.hpp"
#include "ImuCsvRecorder.hpp"
#include "ImuMarkerLog.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

/// Writes straight to a host file, standing in for ImuFileSink's SDK::Kernel one.
class FileSink : public ImuCsvRecorder::ISink {
public:
    explicit FileSink(const std::string& path) : mFile(std::fopen(path.c_str(), "wb")) {}

    ~FileSink() override
    {
        if (mFile != nullptr) {
            std::fclose(mFile);
        }
    }

    bool ok() const { return mFile != nullptr; }

    bool write(const char* data, size_t len) override
    {
        return mFile != nullptr && std::fwrite(data, 1, len, mFile) == len;
    }

    bool flush() override { return mFile != nullptr && std::fflush(mFile) == 0; }

private:
    std::FILE* mFile = nullptr;
};

/// One second of movement at `level`, which stands in for nothing in particular:
/// the point is the file's shape, not its contents.
int16_t wobble(int i, int level)
{
    return static_cast<int16_t>(level * std::sin(i * 0.37));
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: recording-export <out-dir>\n");
        return 2;
    }

    const std::string dir  = argv[1];
    const std::string stem = dir + "/imu_20260903T120000";

    FileSink samples(stem + ".csv");
    FileSink markers(stem + "_events.csv");
    FileSink hr(stem + "_hr.csv");
    if (!samples.ok() || !markers.ok() || !hr.ok()) {
        std::fprintf(stderr, "could not create files under %s\n", dir.c_str());
        return 1;
    }

    ImuCsvRecorder recorder;
    ImuMarkerLog   markerLog;
    HrCsvLog       hrLog;

    // All three begun from the same tick, which is what makes a row in one and a
    // row in another with the same t_ms the same instant.
    constexpr uint32_t kStartMs = 1'000'000;
    if (!recorder.begin(samples, kStartMs) || !markerLog.begin(markers, kStartMs)
        || !hrLog.begin(hr, kStartMs)) {
        std::fprintf(stderr, "a writer refused to start\n");
        return 1;
    }

    // Six stretches at alternating levels, thirty seconds each, with a marker at
    // every boundary -- the shape the recording protocol asks for.
    constexpr int kStretches      = 6;
    constexpr int kSecondsPer     = 30;
    constexpr int kSamplesPerSec  = 100;
    float         bpm             = 95.0f;

    for (int stretch = 0; stretch < kStretches; ++stretch) {
        const bool  busy   = (stretch % 2) == 0;
        const int   level  = busy ? 6000 : 400;
        const float target = busy ? 175.0f : 135.0f;

        for (int s = 0; s < kSecondsPer; ++s) {
            for (int i = 0; i < kSamplesPerSec; ++i) {
                const uint32_t t = kStartMs
                                 + static_cast<uint32_t>(stretch) * kSecondsPer * 1000u
                                 + static_cast<uint32_t>(s) * 1000u
                                 + static_cast<uint32_t>(i) * 10u;
                ImuCsvRecorder::Sample raw{};
                raw.ax = wobble(i, level);
                raw.ay = wobble(i + 3, level);
                raw.az = static_cast<int16_t>(4096 + wobble(i + 7, level / 2));
                raw.gx = wobble(i + 1, level * 2);
                raw.gy = wobble(i + 5, level * 2);
                raw.gz = wobble(i + 9, level * 2);
                if (!recorder.onSample(t, raw)) {
                    std::fprintf(stderr, "recorder stopped early: %u\n",
                                 static_cast<unsigned>(recorder.stopReason()));
                    return 1;
                }
            }

            bpm += (target - bpm) * 0.08f;
            HrCsvLog::Sample beat{};
            beat.bpm         = bpm;
            beat.opticalBpm  = bpm - 1.25f;
            beat.externalBpm = bpm;
            beat.trust       = 2;
            beat.source      = HrCsvLog::Source::EXTERNAL;
            const uint32_t t = kStartMs
                             + static_cast<uint32_t>(stretch) * kSecondsPer * 1000u
                             + static_cast<uint32_t>(s) * 1000u;
            hrLog.onSample(t, beat);
        }

        if (stretch + 1 < kStretches) {
            const uint32_t t = kStartMs
                             + static_cast<uint32_t>(stretch + 1) * kSecondsPer * 1000u;
            markerLog.mark(t);
        }
    }

    recorder.end();
    markerLog.end();
    hrLog.end();

    // The labels file is the wearer's, so it is written here by hand rather than
    // by any of the three -- exactly as it would be after a session.
    std::FILE* labels = std::fopen((stem + "_labels.txt").c_str(), "wb");
    if (labels == nullptr) {
        return 1;
    }
    std::fputs("# exported by recording_export, not a real session\n"
               "alternate rally rest\n",
               labels);
    std::fclose(labels);

    std::printf("%u samples, %u markers, %u beats\n",
                static_cast<unsigned>(recorder.sampleCount()),
                static_cast<unsigned>(markerLog.markerCount()),
                static_cast<unsigned>(hrLog.sampleCount()));
    return 0;
}
