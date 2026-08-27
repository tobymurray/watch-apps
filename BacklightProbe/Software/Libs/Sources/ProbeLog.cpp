/**
 ******************************************************************************
 * @file    ProbeLog.cpp
 * @brief   The exact wording of the record.
 ******************************************************************************
 */

#include "ProbeLog.hpp"

#include <cstdio>
#include <cstring>

#define LOG_MODULE_PRX      "Probe"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "SDK/UnaLogger/Logger.h"

namespace Probe
{

namespace
{
/// Longest record any method below formats, plus room. Records are one line and
/// carry a label from the plan, which is the only unbounded-looking part: and
/// the plan's labels are all well under 60 characters.
constexpr size_t kLineMax = 200;
} // namespace

ProbeLog::ProbeLog(SDK::Interface::IFile& file)
    : mFile(file)
{
}

void ProbeLog::vline(const char* format, va_list args)
{
    char buffer[kLineMax];

    int n = std::vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    if (n < 0) {
        mIntact = false;
        return;
    }
    if (static_cast<size_t>(n) > sizeof(buffer) - 2) {
        // Truncated rather than dropped: a short line is still readable, and a
        // silently missing one is not. Flagged so the footer can say so.
        n      = static_cast<int>(sizeof(buffer) - 2);
        mIntact = false;
    }
    buffer[n]     = '\n';
    buffer[n + 1] = '\0';

    const size_t want = static_cast<size_t>(n) + 1u;
    size_t       bw   = 0;
    mFile.write(buffer, want, bw);
    if (bw != want) {
        mIntact = false;
    }
    mFile.flush();
}

void ProbeLog::line(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vline(format, args);
    va_end(args);
}

void ProbeLog::header(uint32_t uptimeMs, bool registersAvailable, size_t sweepBlocks,
                      bool timersIncluded)
{
    line("# BacklightProbe results");
    line("#");
    line("# What this file can tell you: what was requested, what the message");
    line("# layer reported back, and when. All times are kernel uptime in ms.");
    line("#");
    line("# What it CANNOT tell you: whether the backlight was actually lit.");
    line("# No app can read that back; IBacklight is not obtainable through");
    line("# queryInterface and the message carries no state. Every timing answer");
    line("# in SUITE 2 has to be read off a video of the screen, against the");
    line("# millisecond counter the OBSERVE steps draw.");
    line("#");
    line("# The register evidence is in the sweep_*.txt files next to this one.");
    line("# Diff them: dark vs lit_b100 names the pin, and lit_b100 vs lit_b001");
    line("# says whether anything downstream scales with the request.");
    line("PRB start uptime_ms=%lu", static_cast<unsigned long>(uptimeMs));
    line("PRB registers=%c sweep_blocks=%u timers=%c", registersAvailable ? 'Y' : 'N',
         static_cast<unsigned>(sweepBlocks), timersIncluded ? 'Y' : 'N');
    if (!registersAvailable) {
        line("PRB WARNING no registers on this build; no sweep was taken, and");
        line("PRB WARNING nothing here says anything about real hardware.");
    }
    if (timersIncluded) {
        line("PRB WARNING timer bases are UNCONFIRMED for this part. A base that");
        line("PRB WARNING does not decode faults and takes the app down; the last");
        line("PRB WARNING block named in a sweep file is then the one to blame.");
    }
}

void ProbeLog::stepBegan(size_t index, const Step& step, uint32_t atMs)
{
    line("STEP %02u %-7s at_ms=%lu quiet=%c  %s", static_cast<unsigned>(index),
         actionName(step.action), static_cast<unsigned long>(atMs), step.quiet ? 'Y' : 'N',
         step.label ? step.label : "");
}

void ProbeLog::backlight(size_t index, const Step& step, const Backlight::Outcome& outcome)
{
    // brightness and auto_off_ms are what went on the wire; result and
    // completed are what came back. Kept on one line so a reader can scan the
    // column and see at a glance that the result never varies with brightness.
    line("SET  %02u from=%s brightness=%u auto_off_ms=%lu send_timeout_ms=%lu "
         "sent=%c alloc_failed=%c result=%s completed=%c elapsed_ms=%lu",
         static_cast<unsigned>(index), step.sender == Sender::Gui ? "GUI" : "SVC",
         static_cast<unsigned>(outcome.brightness),
         static_cast<unsigned long>(outcome.autoOffMs),
         static_cast<unsigned long>(outcome.sendTimeoutMs), outcome.sent ? 'Y' : 'N',
         outcome.allocationFailed ? 'Y' : 'N', Backlight::resultName(outcome.result),
         outcome.completed ? 'Y' : 'N', static_cast<unsigned long>(outcome.elapsedMs));
}

void ProbeLog::sweep(size_t index, const Step& step, bool ok)
{
    line("SWEP %02u file=sweep_%s.txt written=%c", static_cast<unsigned>(index),
         step.label ? step.label : "unnamed", ok ? 'Y' : 'N');
}

void ProbeLog::iids(size_t index, const IidProbe::Result& result)
{
    line("IIDS %02u probed=%u answered=%u meaningful=%c", static_cast<unsigned>(index),
         static_cast<unsigned>(IidProbe::kCount), static_cast<unsigned>(result.nonNullCount),
         result.meaningful ? 'Y' : 'N');

    for (size_t i = 0; i < IidProbe::kCount; ++i) {
        line("IIDS %02u   %08lX -> %s%08lX", static_cast<unsigned>(index),
             static_cast<unsigned long>(result.answers[i].iid),
             result.answers[i].nonNull ? "" : "null ",
             static_cast<unsigned long>(result.answers[i].value));
    }

    if (!result.meaningful) {
        line("IIDS %02u   simulator queryInterface: says nothing about the device",
             static_cast<unsigned>(index));
    } else if (result.nonNullCount == 0) {
        // Written out because it is the load-bearing negative, and a reader
        // scanning for a conclusion should find one here rather than have to
        // infer it from six null lines.
        line("IIDS %02u   all six null: Q7 closed, no backlight interface in the gap",
             static_cast<unsigned>(index));
    } else {
        line("IIDS %02u   NOT called through. Confirm the vtable in the firmware",
             static_cast<unsigned>(index));
        line("IIDS %02u   image (Phase C) before invoking anything on these.",
             static_cast<unsigned>(index));
    }
}

void ProbeLog::note(size_t index, const Step& step)
{
    line("%s", "");
    line("### %s", step.label ? step.label : "");
    (void)index;
}

void ProbeLog::footer(uint32_t uptimeMs, size_t stepsRun, size_t observeSteps)
{
    line("%s", "");
    line("PRB done uptime_ms=%lu steps=%u", static_cast<unsigned long>(uptimeMs),
         static_cast<unsigned>(stepsRun));
    line("PRB intact=%c", mIntact ? 'Y' : 'N');
    line("PRB %u OBSERVE steps ran. Their answers are on the video, not here.",
         static_cast<unsigned>(observeSteps));
    line("PRB Next: diff the sweep files, then fill in the blank times.");
}

} // namespace Probe
