/**
 ******************************************************************************
 * @file    Barcode.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Barcode id state shared by service and GUI.
 ******************************************************************************
 *
 * The id is the only state: unlike a stopwatch there is nothing to time, so
 * the service's whole job is to read this string out of the configuration and
 * hand it to the GUI.
 *
 * There is deliberately no usable id to fall back on. A barcode is an
 * identity claim, and a plausible placeholder that scans is worse than
 * nothing -- at a parkrun finish it credits somebody else's run. With no
 * usable value the state carries the reason instead, so the GUI can say what
 * to do about it.
 *
 * SDK::AppConfig will not let a field go without a default (every field in
 * app-manifest.json must declare one, and it must satisfy the field's own
 * constraints), so "no default" is expressed the only way the format allows:
 * a single space, declared as the default and treated by this app as the
 * absence of an id. It is a legal Code 128 character, which is why the check
 * is an explicit comparison and not a side effect of validation.
 *
 ******************************************************************************
 */

#ifndef BARCODE_HPP
#define BARCODE_HPP

#include <cstddef>
#include <cstdint>

namespace Barcode
{

constexpr size_t kMaxIdLength = 16; ///< Matches Code128::kMaxDataLength

/**
 * @brief Declared maximum of the config field: one byte longer than an id.
 *
 * SDK::AppConfig truncates a stored string to the field's declared maxLength
 * on a UTF-8 boundary and tells the caller nothing about having done it, so a
 * field declared at 16 would hand back the first 16 characters of a 30
 * character value as though the user had typed exactly that. A shortened id
 * is a *wrong* id -- it scans, and it scans as somebody else -- which is the
 * one outcome this app exists to prevent.
 *
 * Declaring 17 buys the distinction back. Anything the file offers that is
 * too long to be an id arrives as 17 bytes, the length check below refuses
 * it, and the wearer is told. The phone never offers a 17-character value in
 * the first place: the field's pattern caps entry at 16, so the extra byte is
 * reachable only by a hand-edited file, which is exactly the untrusted path
 * that needed defending.
 */
constexpr size_t kConfigMaxLength = 17;

/// Field id in app-manifest.json and in the values file.
constexpr char kIdField[] = "id";

/**
 * @brief The declared default, which is not an id.
 *
 * Kept identical to "default" in app-manifest.json; CI compares the field
 * table against the manifest, but nothing checks this constant, so it is the
 * one place the sentinel can silently drift.
 */
constexpr char kUnsetId[] = " ";

/**
 * @brief Why the app has no id to draw.
 *
 * Carried through to the GUI rather than collapsed into one "no id" state:
 * each of these needs something different done about it, and a watch with
 * four buttons and no keyboard cannot ask.
 *
 * Coarser than it used to be. This app once read the file itself and could
 * name six distinct ways it was unusable; SDK::AppConfig reports every one of
 * them as `isLoaded() == false` and writes the detail to a log the wearer
 * cannot see. NoConfig is that whole set, and the prompt has to send people
 * to the log or the phone rather than name the fault.
 */
enum class Problem : uint8_t {
    None = 0,  ///< An id was accepted; id[] is valid.
    NoConfig,  ///< No usable input.json: absent, oversized, malformed, or wrong schema.
    NoValue,   ///< A config was read, but it carries no usable id.
    NotSet,    ///< Read, but still holding the declared default: nobody has set an id.
    BadValue,  ///< A value arrived that is too long, not plain ASCII, or unencodable.
};

struct State
{
    char    id[kMaxIdLength + 1]; ///< NUL-terminated; empty unless problem == None
    Problem problem;
};

/// A state carrying no id, and the reason why.
inline State makeUnsetState(Problem problem)
{
    State state{};
    state.problem = problem;
    return state;
}

} // namespace Barcode

#endif // BARCODE_HPP
