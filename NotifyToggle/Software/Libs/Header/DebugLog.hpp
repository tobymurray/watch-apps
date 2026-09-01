/**
 ******************************************************************************
 * @file    DebugLog.hpp
 * @brief   Best-effort append-only logging to a file in this app's own
 *          sandbox directory, for diagnosing the settings.json path/format
 *          assumptions without a debug UART adapter.
 ******************************************************************************
 *
 * Writes to "debug.log", a bare filename -- always inside this app's own
 * directory, the one path convention that was never in question. Every
 * function here is best-effort: a logging failure is swallowed and must
 * never affect, delay, or retry the real logic it is diagnosing.
 *
 * This is a temporary diagnostic aid, not something to ship long-term: it is
 * wired in from Gui.cpp, LiveSettings.cpp and SettingsPersist.cpp behind no
 * flag right now because the point of this pass is to see exactly what a
 * real device does.
 ******************************************************************************
 */

#ifndef DEBUG_LOG_HPP
#define DEBUG_LOG_HPP

#include <cstddef>

#include "SDK/Interfaces/IFileSystem.hpp"

namespace DebugLog
{

/// Selects which file subsequent append/appendf/appendBytes/listDirectory
/// calls write to (default "debug.log"). GUI and Service each set their own
/// distinct name at startup so their logs -- taken from two different
/// processes, quite possibly two different filesystem sandboxes -- never
/// collide or get mistaken for each other.
void setLogPath(const char *path);

/// Appends one line (a trailing newline is added) to the current log path.
void append(SDK::Interface::IFileSystem &fs, const char *line);

/// As append(), with printf-style formatting into a bounded internal buffer.
void appendf(SDK::Interface::IFileSystem &fs, const char *fmt, ...);

/// Appends `prefix: <bytes, non-printable replaced with '.', capped>`.
/// For logging exactly what a read actually returned, safely.
void appendBytes(SDK::Interface::IFileSystem &fs, const char *prefix, const char *data, size_t len);

/// Appends a directory listing of `path` (one line per entry, or a note if
/// the directory can't be opened or is empty). This is the key diagnostic:
/// it shows what this app can actually see at `path`, rather than inferring
/// it from a single file open failing.
void listDirectory(SDK::Interface::IFileSystem &fs, const char *path);

/// Read-only: probes a handful of FatFs-style numbered-drive roots
/// ("0:/" .. "3:/") for existence and, where a root exists, lists it. Never
/// opens anything for writing. Exists because one `..` from this app's own
/// directory was found to land on a different volume than "Apps/" and
/// "settings.json" -- this maps what else is reachable that way.
void probeDriveRoots(SDK::Interface::IFileSystem &fs);

/// Read-only: checks a few spellings of the "../SharedData/" escape hatch
/// RustGuiPoc's hardware notes document as working. A plain listing of `..`
/// already enumerated everything there and found no SharedData entry, but
/// that only rules out SharedData existing *as a child of whatever bare ".."
/// resolves to* -- it does not prove "../SharedData/" as a longer path
/// string resolves the same way, so this checks the actual string directly
/// via IFileSystem::exist() (the call already proven safe on this device,
/// even against paths that turn out wrong) rather than inferring.
void probeSharedData(SDK::Interface::IFileSystem &fs);

/// Read-only: lists two real (not bare) two-hop-up paths -- "../../Apps/"
/// and "../../Apps/../" -- to test whether a second real ".." hop resolves
/// correctly once a bare, nothing-after-it ".." is ruled out as the cause of
/// the earlier wrong-volume listings (see probeSharedData's doc comment).
void probeTwoHopResolution(SDK::Interface::IFileSystem &fs);

} // namespace DebugLog

#endif // DEBUG_LOG_HPP
