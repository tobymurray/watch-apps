/**
 ******************************************************************************
 * @file    SettingsPersistLimits.hpp
 * @brief   The sizes the settings write path is bounded by.
 ******************************************************************************
 *
 * Split out of SettingsPersist.hpp, which pulls in SDK types, so the host tests
 * can assert against the real numbers rather than copies of them. The one that
 * matters is that a splice is capped at what the reader will accept, not at the
 * size of the buffer: a rewrite that grew past `kMaxSettingsFileSize` would
 * commit and then be unreadable, and every later write would be refused before
 * it opened anything.
 ******************************************************************************
 */

#ifndef SETTINGS_PERSIST_LIMITS_HPP
#define SETTINGS_PERSIST_LIMITS_HPP

#include <cstddef>

namespace SettingsPersist
{

/// Upper bound on the settings file this app will read or write. The real file
/// was 245 bytes on 2026-09-05; this leaves headroom for the firmware adding
/// fields while still refusing an unexpectedly huge read.
constexpr size_t kMaxSettingsFileSize = 512;

/// The read buffer, which has room for a null terminator this app adds and for
/// a rewrite that has not yet been length-checked. Never the cap handed to a
/// splice -- see the note above.
constexpr size_t kBufferCapacity = kMaxSettingsFileSize + 8;

/// What a splice may grow a file to. Deliberately the reader's cap and not the
/// buffer's size, which is the whole of the difference.
constexpr size_t kSpliceCapacity = kMaxSettingsFileSize;

static_assert(kSpliceCapacity <= kMaxSettingsFileSize,
              "A splice may not produce a file the reader would refuse: it would commit, "
              "and then every later write would fail before opening anything.");
static_assert(kSpliceCapacity <= kBufferCapacity,
              "A splice may not produce more bytes than the buffer holds.");

/// Longest `Field::probeText` `validatePrimitives` will read back, which is one
/// less than the buffer it reads into.
constexpr size_t kMaxProbeTextBytes = 63;

} // namespace SettingsPersist

#endif // SETTINGS_PERSIST_LIMITS_HPP
