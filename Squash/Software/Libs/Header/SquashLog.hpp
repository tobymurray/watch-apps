/**
 ******************************************************************************
 * @file    SquashLog.hpp
 * @brief   The file on the volume that explains a session, without a dev tool.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why files, when there is already a log
 *
 * The Service logs through `LOG_INFO`, which needs a UART capture and a dev
 * tool attached to the watch. Nobody has one attached on court, and the ring has
 * gone round by the time the watch is back at a desk. So everything anyone
 * would want to know about a session has to be on the volume, or it is gone.
 *
 * `SleepLab/Docs/FEASIBILITY-LEDGER.md` records how that was learnt there, and
 * `MapManager` set the precedent with `Debug/mapmanager_verify.log`. This is the
 * same idea sized for an activity rather than a night.
 *
 * ---------------------------------------------------------------------------
 * One file, and what belongs in it
 *
 * `Debug/squash.log` is prose, one line per event that changes what a session
 * will contain: which build is running, whether the two languages agree about
 * their shared structs, and how the recording and its sidecars ended. It is
 * what somebody reads when a session did not produce what they expected.
 *
 * There is no per-session CSV beside it. One existed when this app computed
 * zones, calories and recovery windows, and it answered "is this metric
 * behaving across sessions?". This app computes no such metric -- the segmenter
 * has no calibration and nothing is derived on the watch -- so the row would be
 * counters that the recording's own sidecars already carry. It earns its place
 * again when there is a metric to track.
 *
 * ---------------------------------------------------------------------------
 * Bounded, and unable to break a session
 *
 * It is append-only and the app runs for the device's life, so it restarts at a
 * cap with a line saying it did -- an unbounded diagnostic that fills the volume
 * the recording then cannot be written to would be a poor trade.
 *
 * Every failure here is ignored. A diagnostic that can fail a session by being
 * unwritable is worse than no diagnostic, and the case where it cannot be
 * written is itself diagnosable: the file is absent.
 ******************************************************************************
 */

#ifndef SQUASH_LOG_HPP
#define SQUASH_LOG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

/// Directory, relative to the app's sandbox. The name `MapManager` uses.
constexpr char kSquashDiagDir[]      = "Debug";
/// One line per event.
constexpr char kSquashLogPath[]      = "Debug/squash.log";
/// Truncate and start again past this. Around ten lines a session at ~90 bytes
/// is under a kilobyte, so 64 KiB is roughly seventy sessions -- months of play,
/// and nothing next to a recording's eight megabytes.
constexpr size_t kSquashLogMaxBytes = 64u * 1024u;

/**
 * @class SquashLog
 * @brief Append-only, bounded, and holds no handle between calls.
 *
 * The process can be terminated without warning -- plugging in USB stops every
 * running app -- so a line that is not flushed and closed is a line that did not
 * happen.
 */
class SquashLog {
public:
    explicit SquashLog(const SDK::Kernel& kernel) : mKernel(kernel) {}

    SquashLog(const SquashLog&)            = delete;
    SquashLog& operator=(const SquashLog&) = delete;

    /**
     * @brief Write one event line, stamped with uptime and wall clock.
     * @param tag Short event name: "launch", "profile", "abi", "session", "imu".
     * @param fmt printf-style detail, kept under ~120 characters.
     */
    void line(const char* tag, const char* fmt, ...);

    /// Append one session row, writing the header if the file is new.

private:
    const SDK::Kernel& mKernel;
};

#endif // SQUASH_LOG_HPP
