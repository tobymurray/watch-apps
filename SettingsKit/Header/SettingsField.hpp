/**
 ******************************************************************************
 * @file    SettingsField.hpp
 * @brief   Which field an app owns in `2:/settings.json`, the sizes the write
 *          path is bounded by, and the splice that respects them.
 ******************************************************************************
 *
 * Free of SDK types so `Tests/` can drive `isWellFormed` and `spliceWithinReadCap`
 * directly -- the two pieces of this mechanism whose mistakes are silent.
 ******************************************************************************
 */

#ifndef SETTINGS_FIELD_HPP
#define SETTINGS_FIELD_HPP

#include <cstddef>

#include "SettingsSplice.hpp"

namespace SettingsPersist
{

/// Upper bound on the settings file this app will read or write. The real file
/// was 245 bytes on 2026-09-05; this leaves headroom for the firmware adding
/// fields while still refusing an unexpectedly huge read.
constexpr size_t kMaxSettingsFileSize = 512;

/// The read buffer: room for a null terminator this app adds, and slack.
constexpr size_t kBufferCapacity = kMaxSettingsFileSize + 8;

/// Longest `Field::probeText` `validatePrimitives` will read back, one less
/// than the buffer it reads into.
constexpr size_t kMaxProbeTextBytes = 63;

/// Longest scratch path a `Field` may carry. `setPath` truncates silently past
/// the File object's own path buffer, which surfaces only as a primitive check
/// failing for no stated reason.
constexpr size_t kMaxScratchPathBytes = 64;

/// The field an app owns, and the scratch names it writes under. Initialise
/// with designated initialisers and assert `isWellFormed`: five members are
/// interchangeable `const char *`, and `../README.md` says what a transposition
/// costs.
struct Field {
    /// Rewrites this field in a settings.json buffer.
    SettingsSplice::Result (*splice)(char *buf, size_t &len, size_t capacity, bool value,
                                     size_t *valueOffsetOut);
    const char *name;        ///< DebugLog only.
    const char *tmpPath;     ///< Written whole before the real file is touched at all.
    const char *prevPath;    ///< Where the current file waits while the rename lands.
    const char *probeAPath;  ///< validatePrimitives scratch; never settings.json.
    const char *probeBPath;
    const char *probeText;   ///< At most kMaxProbeTextBytes.
};

namespace detail
{

constexpr size_t constexprStrlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

constexpr bool sameString(const char *a, const char *b)
{
    for (size_t i = 0;; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

} // namespace detail

/// True if every member holds the kind of value its name promises -- not a
/// claim that any path is the right one, only that a value has not landed in
/// the wrong member.
constexpr bool isWellFormed(const Field &f)
{
    if (f.splice == nullptr || f.name == nullptr || f.probeText == nullptr) {
        return false;
    }
    if (detail::constexprStrlen(f.probeText) > kMaxProbeTextBytes) {
        return false;
    }

    const char *const paths[] = {f.tmpPath, f.prevPath, f.probeAPath, f.probeBPath};

    // Every path proved non-null before any of them is compared: `sameString`
    // reads both of its arguments, so a later null would be dereferenced by an
    // earlier iteration's comparison.
    for (size_t i = 0; i < 4; ++i) {
        if (paths[i] == nullptr) {
            return false;
        }
        const size_t n = detail::constexprStrlen(paths[i]);
        if (n == 0 || n > kMaxScratchPathBytes) {
            return false;
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        if (detail::sameString(paths[i], "2:/settings.json")) {
            return false;
        }
        for (size_t j = i + 1; j < 4; ++j) {
            if (detail::sameString(paths[i], paths[j])) {
                return false;
            }
        }
    }
    return true;
}

/// Rewrites `f`'s field in `buf`, capped at what the reader will accept.
///
/// The cap is the point of this function. Handing a splice the size of the
/// buffer instead lets it produce a file that commits and is then refused by
/// every later read -- a durable change reported as unsaved, and no further
/// write possible on that watch by any app. `Tests/` drives this rather than
/// the splice directly, so the cap is what is under test.
inline SettingsSplice::Result spliceWithinReadCap(const Field &f, char *buf, size_t &len,
                                                  bool value, size_t *valueOffsetOut)
{
    return f.splice(buf, len, kMaxSettingsFileSize, value, valueOffsetOut);
}

} // namespace SettingsPersist

#endif // SETTINGS_FIELD_HPP
