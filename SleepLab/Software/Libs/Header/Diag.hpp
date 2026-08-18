/**
 ******************************************************************************
 * @file    Diag.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The few lines on the volume that explain a night that went wrong.
 ******************************************************************************
 *
 * ---------------------------------------------------------------------------
 * Why a file, when there is already a log
 *
 * The service logs through `LOG_INFO`, which needs a UART capture and a dev
 * tool attached to the watch. Nobody has one attached at 03:00, and by morning
 * the ring has gone round. So every diagnosis of an overnight failure had to
 * come from the night's own artifacts -- and the failure that matters most is
 * precisely the one where those artifacts do not exist.
 *
 * **If a night produces no epoch CSV at all, nothing on the volume said why.**
 * Not a wrong number: no file, no history row, no clue, and the only honest
 * response would have been to run another night. That is the failure mode the
 * economics of this app cannot afford, because eight hours buys nothing.
 *
 * `MapManager` set the precedent with `Debug/mapmanager_verify.log` and `FwDump`
 * with its manifest. This is the same idea at the smallest size that answers the
 * question.
 *
 * ---------------------------------------------------------------------------
 * What goes in, and what deliberately does not
 *
 * In: the things that decide whether a night can happen at all, written at
 * launch before anything has had a chance to fail --
 *
 *   - which sensor drivers resolved, in the probe's own single-letter form,
 *     because that block is what caught TOUCH_DETECT and SPO2 in two minutes on
 *     hardware (ledger rows S12, S4);
 *   - the settings actually in force, so a night is never diagnosed against the
 *     settings somebody meant to write;
 *   - whether a night was found in progress, and how the restart was classified;
 *   - whether the volume could be written at all.
 *
 * Then one line per event that changes what the morning will contain: a night
 * opening, closing with its verdict, being discarded, or failing to be written.
 *
 * Out: anything per-epoch. That is the CSV's job and duplicating it here would
 * cost 1 900 opens a night to say what is already said. This file is ~20 lines
 * and ~2 KB a night, and it is the file somebody reads when the CSV is missing.
 *
 * Bounded, because it is append-only and the app runs for the device's life: at
 * the cap it starts again with a line saying it did, so the newest launches are
 * always the ones kept. An unbounded diagnostic log is a way to fill the volume
 * that the night then cannot be written to, which would be a fine joke.
 *
 ******************************************************************************
 */

#ifndef DIAG_HPP
#define DIAG_HPP

#include <cstddef>
#include <cstdint>

#include "SDK/Kernel/Kernel.hpp"

namespace SleepLab
{

/// Directory, relative to the app's sandbox. The name `MapManager` uses.
constexpr char kDiagDir[]  = "Debug";
/// The log itself.
constexpr char kDiagPath[] = "Debug/sleeplab.log";

/// Truncate and start again past this. Twenty lines a launch at ~80 bytes is
/// ~1.6 KB, so 64 KB is about forty launches -- more than a month of nights, and
/// nothing next to a volume that has carried 160 MiB of map packs.
constexpr size_t kDiagMaxBytes = 64u * 1024u;

/**
 * @brief Append-only, bounded, and holds no handle between calls.
 *
 * Same discipline as the epoch log and for the same reason: the process can
 * vanish without warning at any point, so a line that is not flushed and closed
 * is a line that did not happen.
 */
class Diag
{
public:
    explicit Diag(const SDK::Kernel &kernel) : mKernel(kernel) {}

    /**
     * @brief Write one line, stamped with both clocks.
     *
     * @param tag   Short event name: "launch", "sensors", "night", "fail".
     * @param fmt   printf-style detail. Kept under ~120 characters.
     *
     * Failure is ignored on purpose. A diagnostic log that can fail a night by
     * being unwritable is worse than no diagnostic log, and the case where this
     * cannot be written is itself diagnosable: the file is absent.
     */
    void line(const char *tag, const char *fmt, ...);

private:
    const SDK::Kernel &mKernel;
};

} // namespace SleepLab

#endif // DIAG_HPP
