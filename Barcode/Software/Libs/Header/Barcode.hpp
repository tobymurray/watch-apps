/**
 ******************************************************************************
 * @file    Barcode.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Barcode state shared by service and GUI.
 ******************************************************************************
 *
 * The codes are the only state: unlike a stopwatch there is nothing to time,
 * so the service's whole job is to read them out of the configuration and hand
 * them to the GUI.
 *
 * There is deliberately no usable code to fall back on. A barcode is an
 * identity claim, and a plausible placeholder that scans is worse than
 * nothing -- at a parkrun finish it credits somebody else's run. With no
 * usable code the state carries the reason instead, so the GUI can say what to
 * do about it.
 *
 * Every declared field must have a default, so each one defaults to the
 * **empty string**, which this app reads as "not set". That is only possible
 * because the fields declare no `minLength`: an empty value has to be legal
 * for the reader to hand it back, and a field that forbids it would force a
 * made-up placeholder instead.
 *
 ******************************************************************************
 */

#ifndef BARCODE_HPP
#define BARCODE_HPP

#include <cstddef>
#include <cstdint>

namespace Barcode
{

/**
 * @brief How many codes the watch will hold.
 *
 * **To change this, edit this number and the declaration in two places** --
 * `configFields` in app-manifest.json, and `kFields` in AppConfigFields.cpp,
 * two entries per code. Nothing else: the service, the message and the screen
 * are all written against this constant. Two static_asserts hold the three in
 * agreement, so a mismatch is a build error and never a silent one.
 *
 * Six is not a hardware limit, it is a judgement:
 *
 *   - The message ceiling is the real cap. A full state has to fit one
 *     256-byte kernel message pool block, which lands the maximum at 7. See
 *     the static_assert in Commands.hpp, which fails the build rather than
 *     letting a state be silently truncated.
 *   - The config form is the practical cap. The phone renders one flat list
 *     with no grouping, so six codes is already twelve rows to scroll.
 *   - The values file is not a constraint at all: six codes cost about 500
 *     bytes of the 8 KB budget.
 *
 * Six covers the case this exists for -- a family at a parkrun finish, plus a
 * membership card or two -- with room to raise it to seven.
 */
constexpr size_t kMaxCodes = 6;

/// Longest id the encoder accepts. Matches Code128::kMaxDataLength.
constexpr size_t kMaxIdLength = 16;

/// Longest name that fits the screen at the label font's size.
constexpr size_t kMaxNameLength = 12;

/**
 * @brief Declared maximum of an id field: one byte longer than an id.
 *
 * SDK::AppConfig truncates a stored string to the field's declared maxLength
 * on a UTF-8 boundary and tells the caller nothing about having done it, so a
 * field declared at 16 would hand back the first 16 characters of a 30
 * character value as though the user had typed exactly that. A shortened id is
 * a *wrong* id -- it scans, and it scans as somebody else -- which is the one
 * outcome this app exists to prevent.
 *
 * Declaring 17 buys the distinction back. Anything too long to be an id
 * arrives as 17 bytes, the length check refuses it, and the wearer is told.
 * The phone never offers a 17-character value in the first place: the field's
 * pattern caps entry at 16, so the extra byte is reachable only by a
 * hand-edited file, which is exactly the untrusted path that needed defending.
 */
constexpr size_t kConfigMaxLength = 17;

/**
 * @brief Why the app has no code to draw.
 *
 * Carried through to the GUI rather than collapsed into one "no code" state:
 * each of these needs something different done about it, and a watch with four
 * buttons and no keyboard cannot ask.
 *
 * Coarser than it used to be. This app once read the file itself and could
 * name six distinct ways it was unusable; SDK::AppConfig reports every one of
 * them as `isLoaded() == false` and writes the detail to a log the wearer
 * cannot see. NoConfig is that whole set.
 *
 * Only reported when **no** code is usable. A single bad entry alongside good
 * ones is skipped rather than announced -- the phone validates before it
 * writes, so one bad entry means a hand-edited file, and refusing to show the
 * codes that are fine would be the wrong trade at a finish funnel.
 */
enum class Problem : uint8_t {
    None = 0,  ///< At least one code was accepted; codes[0..count) are valid.
    NoConfig,  ///< No usable input.json: absent, oversized, malformed, or wrong schema.
    NoValue,   ///< A config was read, but it carries no usable code.
    NotSet,    ///< Read, and every id field is empty: nobody has set a code yet.
    BadValue,  ///< Codes were supplied, but none is drawable.
};

/**
 * @brief Which symbology a code is drawn as.
 *
 * One value, and the enum is still worth having: it is what lets the service
 * and the widget hand a code to Barcode::encode() without either of them
 * naming an encoder. Docs/SYMBOLOGIES.md has the ranking of what might join it
 * and what each would cost.
 *
 * Code128 is **0 on purpose**. Every Code and State in this app is
 * value-initialised somewhere -- makeUnsetState(), the message's constructor,
 * adoptCode()'s local -- so zero has to be a format that draws, not a hole.
 *
 * Nothing chooses between subsets here. Code 128's B and C are picked inside
 * the encoder, per id, because the choice is arithmetic about that id's digits
 * and not something a wearer or a config file should have an opinion about.
 */
enum class Format : uint8_t {
    Code128 = 0,
};

/// One code, what to call it on screen, and how to draw it.
struct Code
{
    char   id[kMaxIdLength + 1];     ///< NUL-terminated, never empty.
    char   name[kMaxNameLength + 1]; ///< NUL-terminated; empty when unnamed.
    Format format;                   ///< Always accepted this id: see adoptCode().
};

/**
 * @brief Every usable code, in declared order.
 *
 * Compacted by the service: empty and unusable slots are dropped, so
 * `codes[0..count)` are all drawable and the GUI can cycle without ever
 * landing on a gap.
 */
struct State
{
    Code    codes[kMaxCodes];
    uint8_t count;   ///< Usable codes. Zero unless problem == None.
    Problem problem;
};

/// A state carrying no codes, and the reason why.
inline State makeUnsetState(Problem problem)
{
    State state{};
    state.problem = problem;
    return state;
}

} // namespace Barcode

#endif // BARCODE_HPP
