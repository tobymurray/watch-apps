/**
 ******************************************************************************
 * @file    ProbeLog.hpp
 * @brief   The results file: the record this app exists to produce.
 ******************************************************************************
 *
 * `backlight_probe.txt`, written into the app's own folder alongside the
 * per-state `sweep_*.txt` files. It is the human-readable half of the evidence:
 * what was asked for, what the message layer said about it, and when each thing
 * happened, so a phone video of the screen can be lined up against it afterwards.
 *
 * ## Why a file and not just the log
 *
 * `LOG_INFO` needs a dev-tool UART capture at 921600 8N1 to be seen at all, and
 * the first real dump taken with FwDump lost its register context exactly that
 * way. This app runs off the cable by necessity: USB stops every running app --
 * so anything not written to storage is anything that might not survive the run.
 * Everything here goes to both.
 *
 * ## Flushed after every record
 *
 * Not buffered to the end. The run is minutes long, the app can be stopped
 * without warning (plugging in does it), and an unflushed tail is the part of
 * the experiment nobody can argue with because nobody has it. A few dozen
 * flushes over four minutes costs nothing worth having.
 *
 * ## It takes an IFile rather than opening one
 *
 * So the host tests can hand it a fake and assert on the bytes. The exact
 * wording of these lines is the deliverable; a test that pins it is a test that
 * stops a refactor from quietly changing what the record says.
 *
 ******************************************************************************
 */

#ifndef PROBE_LOG_HPP
#define PROBE_LOG_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "SDK/Interfaces/IFileSystem.hpp"

#include "BacklightRequest.hpp"
#include "IidProbe.hpp"
#include "ProbePlan.hpp"

namespace Probe
{

/**
 * @brief Appends records to an already-open file.
 *
 * Every method flushes. None of them fail loudly: a results file that lost a
 * line is still worth having, and there is nothing useful an app can do about a
 * failing filesystem halfway through an experiment except keep going and let the
 * gap be visible.
 */
class ProbeLog
{
public:
    explicit ProbeLog(SDK::Interface::IFile& file);

    /// Preamble: what this file is, what it cannot tell you, and the sweep
    /// coverage the run claims. `uptimeMs` is the nearest thing to a timestamp
    /// an app can get, and is what every later record is relative to.
    void header(uint32_t uptimeMs, bool registersAvailable, size_t sweepBlocks, bool timersIncluded);

    /// One step beginning. `atMs` is uptime, so the whole file shares a clock.
    void stepBegan(size_t index, const Step& step, uint32_t atMs);

    /// The outcome of one SetBacklight step.
    void backlight(size_t index, const Step& step, const Backlight::Outcome& outcome);

    /// Whether a labelled sweep was written, and to what.
    void sweep(size_t index, const Step& step, bool ok);

    /// The IID walk.
    void iids(size_t index, const IidProbe::Result& result);

    /// A heading from a Note step.
    void note(size_t index, const Step& step);

    /// Closing summary. `blankObservations` is the count of Observe steps run,
    /// which is how many timings a reader has to go and take off the video.
    void footer(uint32_t uptimeMs, size_t stepsRun, size_t observeSteps);

    /// Whether every write so far reported the full byte count.
    bool intact() const { return mIntact; }

private:
    SDK::Interface::IFile& mFile;
    bool                   mIntact = true;

    /// One formatted line, newline appended, flushed. Everything goes through
    /// here so truncation is handled once.
    void line(const char* format, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    void vline(const char* format, va_list args);
};

} // namespace Probe

#endif // PROBE_LOG_HPP
