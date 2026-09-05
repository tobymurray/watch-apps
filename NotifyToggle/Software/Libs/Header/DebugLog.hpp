/**
 ******************************************************************************
 * @file    DebugLog.hpp
 * @brief   Optional append-only logging to a file in this app's own sandbox
 *          directory, for diagnosing on a watch with no debug UART.
 ******************************************************************************
 *
 * Off unless the build defines NOTIFY_TOGGLE_DEBUG_LOG=1, because a published
 * app has no business writing diagnostics to a wearer's watch. With it off
 * every call below compiles to nothing.
 *
 * Nothing here ever logs file contents: `2:/settings.json` holds height,
 * weight, gender and date of birth, and a diagnostic aid must not leave a
 * plaintext copy of them behind.
 ******************************************************************************
 */

#ifndef DEBUG_LOG_HPP
#define DEBUG_LOG_HPP

#include <cstddef>

#include "SDK/Interfaces/IFileSystem.hpp"

#ifndef NOTIFY_TOGGLE_DEBUG_LOG
#define NOTIFY_TOGGLE_DEBUG_LOG 0
#endif

namespace DebugLog
{

#if NOTIFY_TOGGLE_DEBUG_LOG

/// Selects which file subsequent calls write to (default "debug.log"). GUI and
/// Service set distinct names, being two processes with two filesystem roots.
void setLogPath(const char *path);

/// Appends one line, with a trailing newline, to the current log path.
void append(SDK::Interface::IFileSystem &fs, const char *line);

/// As append(), with printf-style formatting into a bounded internal buffer.
void appendf(SDK::Interface::IFileSystem &fs, const char *fmt, ...);

#else

inline void setLogPath(const char *) {}
inline void append(SDK::Interface::IFileSystem &, const char *) {}
inline void appendf(SDK::Interface::IFileSystem &, const char *, ...) {}

#endif

} // namespace DebugLog

#endif // DEBUG_LOG_HPP
