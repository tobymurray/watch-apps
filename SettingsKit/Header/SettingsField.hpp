/**
 ******************************************************************************
 * @file    SettingsField.hpp
 * @brief   Which field an app owns in `2:/settings.json`, and the scratch names
 *          it commits under.
 ******************************************************************************
 *
 * Free of SDK types, like SettingsSplice.hpp, so `isWellFormed` can be driven
 * by the host tests. It is the only thing standing between a mistyped
 * descriptor and a commit that moves the wearer's only settings file somewhere
 * recovery will not look for it.
 ******************************************************************************
 */

#ifndef SETTINGS_FIELD_HPP
#define SETTINGS_FIELD_HPP

#include <cstddef>

#include "SettingsPersistLimits.hpp"
#include "SettingsSplice.hpp"

namespace SettingsPersist
{

/// The field an app owns, and the scratch names it writes under. Every path
/// here has to be that app's alone: two apps sharing one would have each
/// mistaking the other's half-finished commit for its own, and the file being
/// moved aside is the wearer's only settings file.
///
/// Initialise with designated initialisers and check it with `isWellFormed`.
/// Five of these members are interchangeable `const char *`, so a positional
/// initialiser binds by position while the names are only comments -- and a
/// `tmpPath`/`prevPath` transposition is silent until a commit loses power,
/// when recovery looks for the wearer's only settings file under the name the
/// other member holds.
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

/// True if every member holds the kind of value its name promises: four
/// distinct scratch paths, none of them the real settings file, and a probe
/// text that fits the buffer it is read back into. Not a claim that any path is
/// the right one; only that a value has not landed in the wrong member.
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
        if (paths[i] == nullptr || detail::constexprStrlen(paths[i]) == 0) {
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

} // namespace SettingsPersist

#endif // SETTINGS_FIELD_HPP
