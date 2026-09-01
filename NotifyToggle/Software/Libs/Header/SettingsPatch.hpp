/**
 ******************************************************************************
 * @file    SettingsPatch.hpp
 * @brief   Pure, no-I/O logic for reading/patching the one boolean this app
 *          cares about inside the watch's real settings.json.
 ******************************************************************************
 *
 * This is deliberately not a JSON parser. settings.json is a system file this
 * app does not own the schema of -- SDK::AppConfig's envelope/round-trip
 * contract does not apply to it, and a generic parse-then-reserialize would
 * risk reformatting or dropping a field a future firmware version added that
 * this code has never heard of. So instead: find the exact literal substring
 * `"phone":{"notifications":true}` or `...false}`, and if (and only if) it is
 * present exactly once, produce a new document that is a byte-for-byte copy
 * of the input with only that one substring replaced. Every other byte,
 * including formatting this code does not understand, passes through
 * untouched.
 *
 * Fails closed everywhere: if the expected shape is not found exactly once,
 * these functions report failure and touch nothing. There is no code path
 * that guesses.
 ******************************************************************************
 */

#ifndef SETTINGS_PATCH_HPP
#define SETTINGS_PATCH_HPP

#include <cstddef>

namespace SettingsPatch
{

enum class Result {
    Ok,              ///< Read: outEnabled set. Splice: out/outLen set.
    AlreadySet,      ///< Splice only: the file already says newEnabled; nothing to write.
    NotFound,        ///< The literal isn't present exactly once. Nothing was touched.
    OutputTooSmall,  ///< Splice only: `out` isn't big enough to hold the patched document.
};

/**
 * @brief   Locate the notifications flag in a settings.json buffer.
 * @param   in: The file content, exactly @p inLen bytes. Need not be
 *          NUL-terminated.
 * @param   inLen: Length of @p in in bytes.
 * @param   outEnabled: Set only when the return value is Result::Ok.
 */
Result readNotificationsFlag(const char *in, size_t inLen, bool &outEnabled);

/**
 * @brief   Produce a patched copy of a settings.json buffer with the
 *          notifications flag set to @p newEnabled.
 * @param   in: The file content, exactly @p inLen bytes. Need not be
 *          NUL-terminated.
 * @param   inLen: Length of @p in in bytes.
 * @param   newEnabled: The value the flag should have in the output.
 * @param   out: Destination buffer, capacity @p outCap. Must not overlap
 *          @p in. Left untouched unless the return value is Result::Ok.
 * @param   outCap: Capacity of @p out in bytes.
 * @param   outLen: Set only when the return value is Result::Ok.
 */
Result spliceNotificationsFlag(const char *in, size_t inLen, bool newEnabled,
                                char *out, size_t outCap, size_t &outLen);

} // namespace SettingsPatch

#endif // SETTINGS_PATCH_HPP
