#ifndef HAPTICLAB_RECORDER_HPP
#define HAPTICLAB_RECORDER_HPP

#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

#include "Csv.hpp"
#include "Patterns.hpp"
#include "Protocol.hpp"
#include "Records.hpp"
#include "Schedule.hpp"

/// The run directory, and the only thing a wrist-hour leaves behind.
///
/// Every row is opened, appended, flushed and closed on its own. Rows arrive
/// seconds apart, so the cost is irrelevant, and the property that matters is
/// that a session which dies at trial 50 leaves 49 usable trials. It also keeps
/// this half to one open handle at a time, which the IMU sink in the Service
/// half cannot do and does not need to.
namespace Haptic {

/// A cap is self-defence rather than device awareness: the SDK exposes no
/// free-space query, so there is nothing to ask. The arithmetic behind these
/// two is in HapticLab/README.md under "What a run costs".
constexpr uint32_t kByteCapPerFile = 1u << 20;
constexpr uint32_t kRunDurationCapMs = 4u * 60u * 60u * 1000u;

enum class RunFile : uint8_t {
    Trials = 0,
    Events,
    Commands,
    Patterns,
    Probe,
    Count,
};

class Recorder
{
public:
    explicit Recorder(SDK::Kernel &kernel)
        : mKernel(kernel)
    {}

    /// Creates the run directory and writes the header, the candidate set and
    /// every file's first line. Returns false if the run cannot be recorded at
    /// all, which the menu says before the wrist-hour rather than after it.
    bool open(const RunHeader &run, const Plan &plan, const Pattern patterns[], uint8_t count,
              const Timing &timing, const EffectDurations &durations, const SessionMeta &meta);

    void appendTrial(const TrialRecord &row, const Pattern patterns[]);
    void appendEvent(const EventRecord &row);
    void appendCommand(const CommandRecord &row, const Pattern patterns[], uint8_t count);
    void appendProbe(const ProbeRecord &row);

    /// Rewrites `session.json` whole with the end fields filled in. A run that
    /// ended early is a run with an end reason recorded, not a missing file.
    void close(const char *reason, uint32_t endTick, uint32_t endWall);

    bool ok() const { return mOk; }
    bool capHit() const { return mCapHit; }
    /// Which file hit its cap first, for `session.json`.
    const char *capsHit() const { return mCapHit ? mCapName : "none"; }
    const char *directory() const { return mDir; }

    /// Whether the run has been going long enough to stop on its own.
    bool overDurationCap(uint32_t nowTick) const
    {
        return mOk && sinceTick(mMeta.startTick, nowTick) >= static_cast<int32_t>(kRunDurationCapMs);
    }

    /// A run id from the wall clock, which is what pairs a directory with the
    /// files a phone or a laptop pulled off the watch.
    static void makeRunId(char (&out)[20], uint32_t wallClock);

private:
    /// Appends one already-formatted row, refusing it if its worst case would
    /// take the file past its cap.
    void append(RunFile file, const char *row, int length);
    bool writeHeaderLine(RunFile file, const char *header);
    bool writeSession(const char *reason, uint32_t endTick, uint32_t endWall);
    void path(char (&out)[96], const char *leaf) const;
    static const char *leafFor(RunFile file);

    SDK::Kernel &mKernel;

    char mDir[64] = {};
    bool mOk      = false;
    bool mCapHit  = false;
    const char *mCapName = "none";

    uint32_t mBytes[static_cast<size_t>(RunFile::Count)] = {};

    RunHeader       mRun {};
    Timing          mTiming {};
    EffectDurations mDurations = EffectDurations::nominal();
    SessionMeta     mMeta {};
    const Plan     *mPlan = nullptr;
};

} // namespace Haptic

#endif // HAPTICLAB_RECORDER_HPP
