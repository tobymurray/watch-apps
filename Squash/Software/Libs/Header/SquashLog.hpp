/**
 ******************************************************************************
 * @file    SquashLog.hpp
 * @brief   The two files on the volume that explain a session, without a dev tool.
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
 * Two files, because they have two readers
 *
 * `Debug/squash.log` is prose, one line per event that changes what a session
 * will contain: the profile that was loaded, whether the two languages agree
 * about their shared structs, whether a calibration exists, and how the research
 * recording and its sidecars ended. It is what somebody reads when a session did
 * not produce what they expected.
 *
 * `Debug/sessions.csv` is one row per saved session, with every field the
 * profile stores and the recorder counters beside them. It is what gets loaded
 * into something else to answer "is this metric behaving across sessions?" --
 * the question the feasibility ledger exists to make answerable, and the one
 * that cannot be answered by looking at a watch.
 *
 * The CSV duplicates `profile.json`, deliberately. The profile keeps the last
 * twenty sessions because that is all a baseline can use; the CSV keeps every
 * session until its cap, because evaluating a metric needs more history than
 * computing one does.
 *
 * ---------------------------------------------------------------------------
 * Bounded, and unable to break a session
 *
 * Both are append-only and the app runs for the device's life, so both restart
 * at a cap with a line saying they did -- an unbounded diagnostic that fills the
 * volume the recording then cannot be written to would be a poor trade.
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
#include "SquashEngine.hpp"

/// Directory, relative to the app's sandbox. The name `MapManager` uses.
constexpr char kSquashDiagDir[]      = "Debug";
/// One line per event.
constexpr char kSquashLogPath[]      = "Debug/squash.log";
/// One row per saved session.
constexpr char kSquashSessionsPath[] = "Debug/sessions.csv";

/// Truncate and start again past this. Around ten lines a session at ~90 bytes
/// is under a kilobyte, so 64 KiB is roughly seventy sessions -- months of play,
/// and nothing next to a recording's eight megabytes.
constexpr size_t kSquashLogMaxBytes = 64u * 1024u;

/// The same, for the session rows: ~150 bytes each, so about 430 sessions.
constexpr size_t kSquashSessionsMaxBytes = 64u * 1024u;

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
    /// Everything about one session that is worth having later, including the
    /// parts that are zero today because no calibration exists.
    struct Session {
        SquashSessionRecord record{};
        uint32_t profileSessions = 0;  ///< Sessions in the profile after this one.
        uint32_t calibration     = 0;  ///< squash_engine_calibration().
        uint32_t imuSamples      = 0;  ///< Rows the research recorder wrote.
        uint32_t imuBytes        = 0;
        uint16_t markers         = 0;
        uint16_t hrRows          = 0;
        uint8_t  imuStop         = 0;  ///< ImuCsvRecorder::Stop.
        uint8_t  recordingIntact = 0;
        uint8_t  profileSaved    = 0;
    };

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
    void session(const Session& s);

private:
    const SDK::Kernel& mKernel;
};

#endif // SQUASH_LOG_HPP
