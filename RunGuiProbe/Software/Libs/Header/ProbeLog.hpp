/**
 ******************************************************************************
 * @file    ProbeLog.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The probe's only output: a line-per-event file, read over USB.
 ******************************************************************************
 *
 * This app exists to answer a question that is decided in about a second, on a
 * screen that is then taken over by the very thing being tested. So the answer
 * cannot live on the screen. It lives here: an append-only text file in the
 * app's own folder, which is still there after the watch has moved on, and
 * which mounts over USB with everything else.
 *
 * `SDK::UnaLogger` is not a substitute. It goes to a debug transport nobody has
 * attached while wearing the watch, and the whole point is to find out what
 * happens on a watch on a wrist.
 *
 * ## Two files, not one
 *
 * The service and the GUI are separate processes, and this is a small embedded
 * filesystem with no reason to promise anything about two of them appending to
 * one file at once. So each half writes its own -- `probe.txt` and
 * `probe-gui.txt` -- and the kernel timestamps in the lines are what puts the
 * two back in order. Interleaving them into one file would risk destroying the
 * measurement in the act of taking it.
 *
 * ## Open, write, close, every line
 *
 * Slow, and correct in the way that matters here: the interesting outcomes
 * include the app being torn down mid-experiment, and a handle held open across
 * that loses exactly the lines that would have said so. Nothing on this path is
 * in a hot loop -- the service logs on the order of ten lines per viewing.
 *
 ******************************************************************************
 */

#ifndef PROBELOG_HPP
#define PROBELOG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace Probe
{

class Log
{
public:
    /// `path` is not copied: pass a string literal, which is what both callers
    /// do.
    Log(SDK::Kernel &kernel, const char *path);

    /// Append one printf-formatted line. The newline is added here, so no
    /// caller has to remember it and no two callers disagree about it.
    ///
    /// Failures are silent by design: this is the thing that records failures,
    /// and a logger that needs a logger has nowhere to stop. A run that wrote
    /// nothing shows up as a missing or short file, which is legible enough.
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    void line(const char *fmt, ...);

    /// Bytes above which the file is started over rather than appended to. A
    /// probe that is left installed gets scrolled past many times, and an
    /// unbounded log on a watch's storage is somebody else's bug report.
    static constexpr size_t kMaxBytes = 16u * 1024u;

private:
    /// Longest single line. Comfortably over the longest format below; a line
    /// that would exceed it is truncated rather than split.
    static constexpr size_t kLineBytes = 160;

    void append(const char *text, size_t len);

    SDK::Kernel &mKernel;
    const char  *mPath;
};

} // namespace Probe

#endif // PROBELOG_HPP
