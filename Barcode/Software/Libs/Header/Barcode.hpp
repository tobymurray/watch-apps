/**
 ******************************************************************************
 * @file    Barcode.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Barcode id state shared by service and GUI.
 ******************************************************************************
 *
 * The id is the only state: unlike a stopwatch there is nothing to time, so
 * the service's whole job is to read this string out of the provisioning file
 * and hand it to the GUI.
 *
 * There is deliberately no built-in id to fall back on. A barcode is an
 * identity claim, and a plausible placeholder that scans is worse than
 * nothing -- at a parkrun finish it credits somebody else's run. With no
 * usable value the state carries the reason instead, so the GUI can say what
 * to do about it.
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

/// coreJSON query for the id inside the provisioning file. Nested under an
/// app-owned subtree the way SDK::Variant::Config nests its vocabulary under
/// "features", so a shared reader never has to know these key names.
constexpr char kIdQuery[] = "values.id";

/**
 * @brief Why the app has no id to draw.
 *
 * Carried through to the GUI rather than collapsed into one "no id" state:
 * each of these needs something different done about it, and a watch with
 * four buttons and no keyboard cannot ask.
 */
enum class Problem : uint8_t {
    None = 0,    ///< An id was accepted; id[] is valid.
    NoFile,      ///< input.json has not been written yet.
    TooLarge,    ///< Over the reader's size ceiling.
    Unreadable,  ///< Present, but the read failed.
    NotJson,     ///< Empty or malformed.
    WrongSchema, ///< Missing or unsupported "schema".
    NoKey,       ///< Parsed, but carries no values.id.
    BadValue,    ///< Present but empty, too long, or not printable ASCII.
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
