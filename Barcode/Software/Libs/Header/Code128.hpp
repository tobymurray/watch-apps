/**
 ******************************************************************************
 * @file    Code128.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Header-only Code 128 encoder, subsets B and C.
 ******************************************************************************
 *
 * Pure data in, data out: no allocation and no SDK/TouchGFX dependency, so it
 * can be shared by both the Service and GUI binaries as an inline header,
 * the same way Stopwatch.hpp is used from both sides of that app.
 *
 * Subset B is the general case and encodes one printable ASCII character per
 * symbol. Subset C encodes a *pair* of digits per symbol, and the encoder
 * switches into it wherever that makes the barcode shorter -- see
 * chooseSubset() below for why that matters more than it sounds.
 *
 * The switching is invisible from outside: the accepted input is exactly what
 * it was when this was subset B alone (1..16 printable ASCII), and a scanner
 * decodes the same string either way, because subset switching is part of the
 * symbology rather than an extension to it. What changes is only how wide the
 * bars come out.
 *
 ******************************************************************************
 */

#ifndef CODE128_HPP
#define CODE128_HPP

#include <cstdint>
#include <cstddef>

#include "Encoded.hpp"

namespace Code128
{

constexpr size_t kMaxDataLength = 22;   ///< Longest id this encoder accepts

constexpr uint8_t kCodeC  = 99;   ///< Switch to subset C, from A or B
constexpr uint8_t kCodeB  = 100;  ///< Switch to subset B, from A or C
constexpr uint8_t kStartB = 104;
constexpr uint8_t kStartC = 105;
constexpr uint8_t kStop   = 106;

/// Every symbol this encoder can emit has to fit the shared buffer. The bound
/// is the subset-B worst case -- one symbol a character, plus a start and a
/// check -- and subset C only ever uses fewer, so this covers both.
static_assert(kMaxDataLength + 2 <= Barcode::kMaxSymbols,
              "Barcode::Encoded is not sized for this encoder's longest input");

/// Widths (in modules) of each of the 107 Code 128 symbols, 6 elements per
/// symbol alternating bar/space/bar/space/bar/space, each row summing to 11.
/// Table per ISO/IEC 15417 (cross-checked against the published reference
/// table on 2026-07-25, ordered by Code128 symbol value).
constexpr uint8_t kPatterns[107][6] = {
    {2,1,2,2,2,2}, {2,2,2,1,2,2}, {2,2,2,2,2,1}, {1,2,1,2,2,3},
    {1,2,1,3,2,2}, {1,3,1,2,2,2}, {1,2,2,2,1,3}, {1,2,2,3,1,2},
    {1,3,2,2,1,2}, {2,2,1,2,1,3}, {2,2,1,3,1,2}, {2,3,1,2,1,2},
    {1,1,2,2,3,2}, {1,2,2,1,3,2}, {1,2,2,2,3,1}, {1,1,3,2,2,2},
    {1,2,3,1,2,2}, {1,2,3,2,2,1}, {2,2,3,2,1,1}, {2,2,1,1,3,2},
    {2,2,1,2,3,1}, {2,1,3,2,1,2}, {2,2,3,1,1,2}, {3,1,2,1,3,1},
    {3,1,1,2,2,2}, {3,2,1,1,2,2}, {3,2,1,2,2,1}, {3,1,2,2,1,2},
    {3,2,2,1,1,2}, {3,2,2,2,1,1}, {2,1,2,1,2,3}, {2,1,2,3,2,1},
    {2,3,2,1,2,1}, {1,1,1,3,2,3}, {1,3,1,1,2,3}, {1,3,1,3,2,1},
    {1,1,2,3,1,3}, {1,3,2,1,1,3}, {1,3,2,3,1,1}, {2,1,1,3,1,3},
    {2,3,1,1,1,3}, {2,3,1,3,1,1}, {1,1,2,1,3,3}, {1,1,2,3,3,1},
    {1,3,2,1,3,1}, {1,1,3,1,2,3}, {1,1,3,3,2,1}, {1,3,3,1,2,1},
    {3,1,3,1,2,1}, {2,1,1,3,3,1}, {2,3,1,1,3,1}, {2,1,3,1,1,3},
    {2,1,3,3,1,1}, {2,1,3,1,3,1}, {3,1,1,1,2,3}, {3,1,1,3,2,1},
    {3,3,1,1,2,1}, {3,1,2,1,1,3}, {3,1,2,3,1,1}, {3,3,2,1,1,1},
    {3,1,4,1,1,1}, {2,2,1,4,1,1}, {4,3,1,1,1,1}, {1,1,1,2,2,4},
    {1,1,1,4,2,2}, {1,2,1,1,2,4}, {1,2,1,4,2,1}, {1,4,1,1,2,2},
    {1,4,1,2,2,1}, {1,1,2,2,1,4}, {1,1,2,4,1,2}, {1,2,2,1,1,4},
    {1,2,2,4,1,1}, {1,4,2,1,1,2}, {1,4,2,2,1,1}, {2,4,1,2,1,1},
    {2,2,1,1,1,4}, {4,1,3,1,1,1}, {2,4,1,1,1,2}, {1,3,4,1,1,1},
    {1,1,1,2,4,2}, {1,2,1,1,4,2}, {1,2,1,2,4,1}, {1,1,4,2,1,2},
    {1,2,4,1,1,2}, {1,2,4,2,1,1}, {4,1,1,2,1,2}, {4,2,1,1,1,2},
    {4,2,1,2,1,1}, {2,1,2,1,4,1}, {2,1,4,1,2,1}, {4,1,2,1,2,1},
    {1,1,1,1,4,3}, {1,1,1,3,4,1}, {1,3,1,1,4,1}, {1,1,4,1,1,3},
    {1,1,4,3,1,1}, {4,1,1,1,1,3}, {4,1,1,3,1,1}, {1,1,3,1,4,1},
    {1,1,4,1,3,1}, {3,1,1,1,4,1}, {4,1,1,1,3,1}, {2,1,1,4,1,2},
    {2,1,1,2,1,4}, {2,1,1,2,3,2}, {2,3,3,1,1,1},
};

namespace detail
{

constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }

/// How many digits run from @p at, up to @p length.
inline size_t digitRun(const char *text, size_t at, size_t length)
{
    size_t n = 0;
    while (at + n < length && isDigit(text[at + n])) {
        n++;
    }
    return n;
}

} // namespace detail

/**
 * @brief Encode an ASCII id as Code 128, switching subsets to shorten it.
 * @param text Printable ASCII (32-126), at most kMaxDataLength characters.
 * @param out  Receives the element widths.
 * @retval true  Encoded.
 * @retval false Empty, too long, or outside the printable range.
 *
 * ## Why bother switching
 *
 * A symbol is 11 modules whatever it holds, and the bars get a fixed 200px, so
 * fewer symbols is a *wider* module and a wider module is the one thing that
 * decides whether this app's barcode can be read at all. `BarcodeLayout.hpp`
 * has the arithmetic: at 8 characters of subset B the module comes out at
 * 0.205 mm. Every symbol saved is about 8% back.
 *
 * **There is nothing in ISO/IEC 15417 to compare that against.** An earlier
 * version of this comment said 0.205 mm was "above ISO/IEC 15417's 0.19 mm
 * general minimum and well below the 0.25 mm retail floor", and named the
 * retail floor again below. Both figures were wrong and f92e1ce retracted them
 * from the README and BarcodeLayout.hpp -- the standard leaves X-dimension
 * among "the parameters to be defined by applications" and sets no floor. This
 * comment was missed by that retraction. What survives is scanner capability:
 * 127 um (5 mil) as the low end of scanner-selection guidance for a
 * mid-density symbology, and 76 um (3 mil) from Zebra's LS2208 spec sheet.
 *
 * Subset C halves the symbols a digit run costs. Two examples, both real:
 *
 *   - `12345678`, a membership number: 123 modules in B, 79 in C. The module
 *     goes 0.205 mm -> 0.319 mm, comfortably past the 5 mil line.
 *   - `A1234567`, a parkrun athlete id: 123 modules -> 101, because the `A`
 *     and the odd leading digit stay in B and the remaining six pair up.
 *     0.205 mm -> 0.249 mm, a 19% wider bar.
 *
 * ## Why this is not a compatibility risk
 *
 * Subset switching is not an extension or an option. Every conformant Code 128
 * reader implements it, because a decoder cannot know which subset a symbol
 * started in without following the start character and the switches. The
 * decoded string is identical; only the bars differ.
 */
inline bool encode(const char *text, Barcode::Encoded &out)
{
    size_t length = 0;
    while (text[length] != '\0') {
        if (length >= kMaxDataLength || text[length] < 32 || text[length] > 126) {
            return false;
        }
        length++;
    }
    if (length == 0) {
        return false;
    }

    out.count = 0;
    out.totalModules = 0;

    // Never reached with the bound above holding, and checked anyway: the
    // reason it holds is now a proof about the switching rules rather than
    // something visible by inspection, so a wrong proof must not become a
    // buffer overrun.
    bool overflow = false;

    auto draw = [&](uint8_t value) {
        if (static_cast<size_t>(out.count) + 6 > Barcode::Encoded::kMaxWidths) {
            overflow = true;
            return;
        }
        for (int i = 0; i < 6; i++) {
            out.widths[out.count++] = kPatterns[value][i];
            out.totalModules = static_cast<uint16_t>(out.totalModules + kPatterns[value][i]);
        }
    };

    // The check character is the weighted sum of every symbol drawn before it,
    // the start counting once and each one after by its position. The subset
    // switches are symbols like any other and carry their own weight, which is
    // the one thing easy to get wrong here.
    uint32_t checksum = 0;
    uint16_t weight   = 0;

    auto emit = [&](uint8_t value) {
        draw(value);
        checksum += static_cast<uint32_t>(value) * (weight == 0 ? 1u : weight);
        weight++;
    };

    // Start in C when the id opens with enough digits to pay for it. Four is
    // the break-even: the switch costs a symbol and each pair saves one, so
    // four digits are three symbols in C against four in B. An id that is
    // *entirely* digits pays from two, because starting in C costs nothing --
    // Start C replaces Start B rather than following it.
    const size_t lead = detail::digitRun(text, 0, length);
    bool inC = (lead >= 4) || (lead == length && lead >= 2);

    emit(inC ? kStartC : kStartB);

    size_t i = 0;
    while (i < length) {
        if (inC) {
            if (detail::digitRun(text, i, length) >= 2) {
                emit(static_cast<uint8_t>((text[i] - '0') * 10 + (text[i + 1] - '0')));
                i += 2;
            } else {
                // One digit left, or a non-digit: C has nothing to offer.
                emit(kCodeB);
                inC = false;
            }
        } else {
            const size_t run = detail::digitRun(text, i, length);
            if (run >= 4 && (run % 2) != 0) {
                // An odd run wastes the pairing on its last digit. Spending the
                // first one here in B lands the rest on an even boundary, which
                // is what makes A1234567 cost nine symbols instead of ten.
                emit(static_cast<uint8_t>(text[i] - 32));
                i++;
            } else if (run >= 4) {
                emit(kCodeC);
                inC = true;
            } else {
                emit(static_cast<uint8_t>(text[i] - 32));
                i++;
            }
        }
    }

    // Neither the check character nor the stop is weighed into the checksum.
    draw(static_cast<uint8_t>(checksum % 103));
    draw(kStop);

    if (static_cast<size_t>(out.count) < Barcode::Encoded::kMaxWidths) {
        out.widths[out.count++] = 2; // STOP's extra trailing bar
        out.totalModules = static_cast<uint16_t>(out.totalModules + 2);
    } else {
        overflow = true;
    }

    return !overflow;
}

} // namespace Code128

#endif // CODE128_HPP
