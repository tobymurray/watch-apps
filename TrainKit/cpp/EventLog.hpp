/**
 ******************************************************************************
 * @file    EventLog.hpp
 * @brief   Best-effort, bounded, append-only diagnostics to a file in the
 *          app's own directory.
 ******************************************************************************
 *
 * `LOG_*` on this watch reaches a debug UART adapter and nothing else, so on
 * real hardware a discarded recovery measurement is invisible: a ride that
 * measured nothing and a ride whose windows were all correctly declined look
 * identical. This is how they are told apart -- the same answer
 * NotifyToggle/Software/Libs/Header/DebugLog.hpp reached for the same reason.
 *
 * Writes to a bare filename, which is always inside the app's own directory --
 * the one path convention that was never in question. Every function is
 * best-effort: a logging failure is swallowed and must never affect, delay or
 * retry the thing it is diagnosing.
 *
 * The file is held OPEN for the life of the ride rather than opened per line.
 * A FatFs open/write/flush/close every second would land inside the same
 * one-second tick the recovery window is measured on, and a tick served late is
 * a gap the detector counts against the window -- a logger that changed the
 * measurement would be worse than no logger.
 *
 * This is a diagnostic aid for the first hardware sessions, not a permanent
 * feature. See Spin/Docs/RECOVERY-FIELD-TEST.md.
 *
 ******************************************************************************
 */

#ifndef TRAINKIT_EVENT_LOG_HPP
#define TRAINKIT_EVENT_LOG_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "SDK/Interfaces/IFileSystem.hpp"

namespace TrainKit {

class EventLog {
public:
    /// Where it stops. Past this the file is left exactly as it is and a single
    /// line says so, because a diagnostic that fills the card is a bug of its
    /// own.
    static constexpr size_t kMaxBytes = 128u * 1024u;

    /// Longest single line. Anything longer is truncated rather than split.
    static constexpr size_t kMaxLine = 256;

    /// @param path  a bare filename, so it lands in the app's own directory.
    EventLog(SDK::Interface::IFileSystem& fs, const char* path);
    ~EventLog();

    /// Open for appending, keeping whatever is already there.
    void open();

    /// Flush and close. Safe to call when never opened.
    void close();

    /// One line, printf-style, with a newline added. Never fails visibly.
    void line(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    /// Push what has been written to storage. Cheap enough for an event, too
    /// expensive for every second.
    void sync();

    /// Delete the file, so a session starts with an empty one.
    void reset();

private:
    SDK::Interface::IFileSystem&             mFs;
    const char*                              mPath;
    std::unique_ptr<SDK::Interface::IFile>   mFile;
    size_t                                   mBytes = 0;
    bool                                     mFull  = false;
};

} // namespace TrainKit

#endif // TRAINKIT_EVENT_LOG_HPP
