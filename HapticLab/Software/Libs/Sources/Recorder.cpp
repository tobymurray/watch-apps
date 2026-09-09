#include "Recorder.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "SDK/Interfaces/IFileSystem.hpp"

#define LOG_MODULE_PRX   "HapticRec"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace Haptic {

const char *Recorder::leafFor(RunFile file)
{
    switch (file) {
    case RunFile::Trials:   return "trials.csv";
    case RunFile::Events:   return "events.csv";
    case RunFile::Commands: return "commands.csv";
    case RunFile::Patterns: return "patterns.csv";
    case RunFile::Probe:    return "probe.csv";
    case RunFile::Count:    break;
    }
    return "unknown.csv";
}

void Recorder::makeRunId(char (&out)[20], uint32_t wallClock)
{
    const std::time_t utc = static_cast<std::time_t>(wallClock);
    std::tm           tm {};
#if WIN32
    gmtime_s(&tm, &utc);
#else
    gmtime_r(&utc, &tm);
#endif
    std::snprintf(out, sizeof(out), "%04u%02u%02uT%02u%02u%02u",
                  static_cast<unsigned>(tm.tm_year) + 1900u,
                  static_cast<unsigned>(tm.tm_mon) + 1u, static_cast<unsigned>(tm.tm_mday),
                  static_cast<unsigned>(tm.tm_hour), static_cast<unsigned>(tm.tm_min),
                  static_cast<unsigned>(tm.tm_sec));
}

void Recorder::path(char (&out)[96], const char *leaf) const
{
    std::snprintf(out, sizeof(out), "%s/%s", mDir, leaf);
}

bool Recorder::writeHeaderLine(RunFile file, const char *header)
{
    char pathBuf[96];
    path(pathBuf, leafFor(file));

    std::unique_ptr<SDK::Interface::IFile> handle = mKernel.fs.file(pathBuf);
    if (!handle || !handle->open(/*wMode=*/true, /*override=*/true)) {
        LOG_ERROR("cannot create %s\n", pathBuf);
        return false;
    }

    char line[kMaxRowLength];
    const int length = std::snprintf(line, sizeof(line), "%s\n", header);
    size_t    written = 0;
    const bool ok = length > 0 && handle->write(line, static_cast<size_t>(length), written);
    handle->flush();
    handle->close();
    if (ok) {
        mBytes[static_cast<size_t>(file)] = static_cast<uint32_t>(written);
    }
    return ok;
}

bool Recorder::open(const RunHeader &run, const Plan &plan, const Pattern patterns[],
                    uint8_t count, const Timing &timing, const EffectDurations &durations,
                    const SessionMeta &meta)
{
    mRun       = run;
    mTiming    = timing;
    mDurations = durations;
    mMeta      = meta;
    mPlan      = &plan;
    mOk        = false;
    mCapHit    = false;
    mCapName   = "none";

    // mkdir is single level, so the base has to exist before the run's own
    // directory can be made.
    if (!mKernel.fs.mkdir(kRunsDir)) {
        LOG_ERROR("cannot create %s\n", kRunsDir);
        return false;
    }
    std::snprintf(mDir, sizeof(mDir), "%s/%s", kRunsDir, run.runId);
    if (!mKernel.fs.mkdir(mDir)) {
        LOG_ERROR("cannot create %s\n", mDir);
        return false;
    }

    if (!writeHeaderLine(RunFile::Trials, kTrialsHeader) ||
        !writeHeaderLine(RunFile::Events, kEventsHeader) ||
        !writeHeaderLine(RunFile::Commands, kCommandsHeader) ||
        !writeHeaderLine(RunFile::Patterns, kPatternsHeader) ||
        !writeHeaderLine(RunFile::Probe, kProbeHeader)) {
        return false;
    }

    mOk = true;

    // The set as actually played, so a later change to the generator cannot
    // orphan this run's data.
    char row[kMaxRowLength];
    for (uint8_t i = 0; i < count; i++) {
        const int length = formatPatternRow(row, sizeof(row), mRun, i, patterns[i], mDurations);
        append(RunFile::Patterns, row, length);
    }

    if (!writeSession("running", 0, 0)) {
        mOk = false;
    }
    return mOk;
}

void Recorder::append(RunFile file, const char *row, int length)
{
    if (!mOk || length <= 0) {
        return;
    }

    const size_t slot = static_cast<size_t>(file);
    if (mBytes[slot] + static_cast<uint32_t>(length) > kByteCapPerFile) {
        if (!mCapHit) {
            mCapHit  = true;
            mCapName = leafFor(file);
            LOG_WARNING("%s reached its %lu byte cap\n", mCapName,
                        static_cast<unsigned long>(kByteCapPerFile));
        }
        return;
    }

    char pathBuf[96];
    path(pathBuf, leafFor(file));

    std::unique_ptr<SDK::Interface::IFile> handle = mKernel.fs.file(pathBuf);
    if (!handle || !handle->open(/*wMode=*/true, /*override=*/false)) {
        LOG_ERROR("cannot append to %s\n", pathBuf);
        return;
    }
    // A write-mode open positions the handle at offset zero rather than at the
    // end, so appending needs the seek.
    handle->seek(handle->size());

    size_t written = 0;
    if (handle->write(row, static_cast<size_t>(length), written)) {
        mBytes[slot] += static_cast<uint32_t>(written);
    }
    handle->flush();
    handle->close();
}

void Recorder::appendTrial(const TrialRecord &row, const Pattern patterns[])
{
    char line[kMaxRowLength];
    append(RunFile::Trials, line, formatTrialRow(line, sizeof(line), mRun, row, patterns));
}

void Recorder::appendEvent(const EventRecord &row)
{
    char line[kMaxRowLength];
    append(RunFile::Events, line, formatEventRow(line, sizeof(line), mRun, row));
}

void Recorder::appendCommand(const CommandRecord &row, const Pattern patterns[], uint8_t count)
{
    char line[kMaxRowLength];
    append(RunFile::Commands, line,
           formatCommandRow(line, sizeof(line), mRun, row, patterns, count));
}

void Recorder::appendProbe(const ProbeRecord &row)
{
    char line[kMaxRowLength];
    append(RunFile::Probe, line, formatProbeRow(line, sizeof(line), mRun, row));
}

bool Recorder::writeSession(const char *reason, uint32_t endTick, uint32_t endWall)
{
    if (mPlan == nullptr) {
        return false;
    }

    SessionMeta meta = mMeta;
    meta.endReason   = reason;
    meta.endTick     = endTick;
    meta.endWall     = endWall;
    meta.capsHit     = capsHit();

    char pathBuf[96];
    path(pathBuf, "session.json");

    std::unique_ptr<SDK::Interface::IFile> handle = mKernel.fs.file(pathBuf);
    if (!handle || !handle->open(/*wMode=*/true, /*override=*/true)) {
        LOG_ERROR("cannot write %s\n", pathBuf);
        return false;
    }

    char      body[kSessionJsonBytes];
    const int length = formatSessionJson(body, sizeof(body), mRun, *mPlan, mTiming, mDurations,
                                         meta, kByteCapPerFile, kRunDurationCapMs);

    // The open above truncated the file, so refusing to write here is what
    // leaves an empty header behind. A short header naming the run is worth
    // more than none, so anything unusable from the formatter falls back to
    // the fields the other files cannot be read without.
    char       fallback[256];
    const char *out = body;
    size_t     count = 0;
    if (length > 0 && static_cast<size_t>(length) < sizeof(body)) {
        count = static_cast<size_t>(length);
    } else {
        const int shortLength = std::snprintf(
            fallback, sizeof(fallback),
            "{\n  \"schema\": %u,\n  \"run_id\": \"%s\",\n  \"seed\": %lu,\n"
            "  \"contact\": \"%s\",\n  \"masking\": \"%s\",\n"
            "  \"candidate_set\": \"%s\",\n  \"candidate_set_hash\": \"%08lX\",\n"
            "  \"end_reason\": \"%s\",\n  \"header_truncated\": true\n}\n",
            kSchemaVersion, mRun.runId, static_cast<unsigned long>(mRun.seed),
            contactName(mRun.contact), maskingName(mRun.masking), setName(mRun.set),
            static_cast<unsigned long>(mRun.setHash), reason);
        LOG_ERROR("session header would not format (%d into %u); wrote the short form\n", length,
                  static_cast<unsigned>(sizeof(body)));
        out   = fallback;
        count = shortLength > 0 ? static_cast<size_t>(shortLength) : 0;
    }

    size_t written = 0;
    const bool ok = count > 0 && handle->write(out, count, written) && written == count;
    handle->flush();
    handle->close();
    if (!ok) {
        LOG_ERROR("could not write %s\n", pathBuf);
    }
    return ok;
}

void Recorder::close(const char *reason, uint32_t endTick, uint32_t endWall)
{
    if (!mOk) {
        return;
    }
    // Throwing away the session does not make the rows less real, so an
    // abandoned run is closed out exactly like a completed one.
    if (!writeSession(reason, endTick, endWall)) {
        LOG_ERROR("could not close the run out\n");
    }
    mOk = false;
}

} // namespace Haptic
