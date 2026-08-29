/**
 ******************************************************************************
 * @file    Itf.hpp
 * @date    29-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Header-only Interleaved 2 of 5 (ITF) encoder.
 ******************************************************************************
 *
 * Pure data in, data out: no allocation and no SDK/TouchGFX dependency, the
 * same shape as Code128.hpp, and it produces the same Barcode::Encoded.
 *
 * ITF is here for interoperability and nothing else. It is not denser than
 * Code 128 subset C once its wide:narrow ratio is honest (see the ratio note
 * below), it has no check character, and it is the one symbology on this
 * screen with a documented way to decode *wrongly*. It earns its place only
 * because some cards are ITF and the till at the other end expects ITF.
 *
 * ## The shape
 *
 * Every digit is five elements, exactly two wide and three narrow -- the "2 of
 * 5". Digits are then **interleaved in pairs**: the first digit of a pair
 * contributes the five bars, the second contributes the five spaces between
 * them. That is where the length rule comes from, and why it is a rule about
 * the data rather than about the encoder.
 *
 * ## Why it refuses an odd number of digits
 *
 * The convention is to pad a leading zero. This does not, because a scanner
 * reads the padded value back: `12345` padded is `012345`, and a barcode that
 * scans as a number the wearer did not enter is the harm Barcode.hpp exists to
 * prevent. An ITF card in the wild always carries an even number of digits --
 * it has to, or it could not have been printed as ITF -- so an odd one here
 * means a mistyped id, and saying so is more useful than silently changing it.
 *
 * zint pads. That is why the oracle corpus is even-length only, and the
 * difference is stated in Tests/README.md rather than papered over.
 *
 * ## The ratio, and why it is 3:1
 *
 * ISO/IEC 16390 leaves the wide:narrow ratio to the application within a
 * range. Every ITF tutorial uses 2:1 because it is densest, and density is the
 * wrong thing to optimise on this panel:
 *
 *   - Resolution is not the binding constraint. At 3:1 an eight-digit id is a
 *     0.252 mm narrow element, twice the 5 mil reference the README uses.
 *   - What *is* likely to bite is edge definition. The panel has four levels a
 *     channel, so a bar boundary that lands mid-pixel steps rather than blends
 *     -- and a decoder classifies each element as narrow or wide by comparing
 *     it to its neighbours. The further apart those two widths are, the more
 *     rounding a symbol survives.
 *
 * So the wider ratio is bought with headroom this screen has and spent on the
 * thing it is short of. It also happens to be what zint emits, which is what
 * makes a module-for-module diff against it possible at all.
 *
 ******************************************************************************
 */

#ifndef ITF_HPP
#define ITF_HPP

#include <cstddef>
#include <cstdint>

#include "Encoded.hpp"

namespace Itf
{

/// Longest id this encoder accepts, matching Code128::kMaxDataLength so that
/// no format is the reason an id has to be shortened.
constexpr size_t kMaxDataLength = 16;

/// Narrow and wide elements, in the units Barcode::Encoded counts. The ratio
/// is these two numbers and nothing else depends on their absolute size --
/// totalModules is a sum the renderer only ever divides by.
constexpr uint8_t kNarrow = 1;
constexpr uint8_t kWide   = 3;

/// Elements per digit, of which exactly kWidePerDigit are wide.
constexpr size_t kElementsPerDigit = 5;
constexpr size_t kWidePerDigit     = 2;

/**
 * @brief Which of a digit's five elements are wide.
 *
 * The 2-of-5 table, as flags rather than widths, so the ratio above is the
 * only place a width is written down. Every row has exactly two ones, which is
 * the symbology's own structural rule and what the tests hold it to.
 */
constexpr uint8_t kWideFlags[10][kElementsPerDigit] = {
    {0, 0, 1, 1, 0}, // 0
    {1, 0, 0, 0, 1}, // 1
    {0, 1, 0, 0, 1}, // 2
    {1, 1, 0, 0, 0}, // 3
    {0, 0, 1, 0, 1}, // 4
    {1, 0, 1, 0, 0}, // 5
    {0, 1, 1, 0, 0}, // 6
    {0, 0, 0, 1, 1}, // 7
    {1, 0, 0, 1, 0}, // 8
    {0, 1, 0, 1, 0}, // 9
};

/// Elements in a symbol of @p digits digits: four for the start, five per
/// digit once interleaved, three for the stop.
constexpr size_t elementsFor(size_t digits) { return 4 + 5 * digits + 3; }

/// Units across a symbol of @p digits digits. Each digit costs two wide and
/// three narrow however it is interleaved, so this is exact.
constexpr uint16_t unitsFor(size_t digits)
{
    return static_cast<uint16_t>(4 * kNarrow
                                 + digits * (kWidePerDigit * kWide
                                             + (kElementsPerDigit - kWidePerDigit) * kNarrow)
                                 + (kWide + 2 * kNarrow));
}

/// The longest symbol this encoder can produce must fit the shared buffer.
static_assert(elementsFor(kMaxDataLength) <= Barcode::Encoded::kMaxWidths,
              "Barcode::Encoded is not sized for this encoder's longest input");

/**
 * @brief Can ITF carry @p text?
 *
 * Separate from encode() for the reason Qr::accepts() is: the service asks
 * this about every code it adopts and never draws anything, so the question
 * has to be answerable without the table. encode() calls it as its own first
 * step, so there is one definition of what ITF accepts rather than two
 * opinions that can drift.
 *
 * @retval false Empty, too long, an odd number of digits, or not all digits.
 */
inline bool accepts(const char *text)
{
    if (text == nullptr) {
        return false;
    }

    size_t length = 0;
    while (text[length] != '\0') {
        if (text[length] < '0' || text[length] > '9') {
            return false;
        }
        if (++length > kMaxDataLength) {
            return false;
        }
    }

    // Two is the shortest real symbol: one pair. Odd is refused rather than
    // padded -- see the header comment.
    return length >= 2 && (length % 2) == 0;
}

/**
 * @brief Encode a digit string as Interleaved 2 of 5.
 * @param text An even number of ASCII digits, 2..kMaxDataLength.
 * @param out  Receives the element widths.
 * @retval true  Encoded.
 * @retval false Refused; @p out is untouched apart from being reset.
 */
inline bool encode(const char *text, Barcode::Encoded &out)
{
    out.count = 0;
    out.totalModules = 0;

    if (!accepts(text)) {
        return false;
    }

    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }

    auto append = [&](uint8_t units) {
        out.widths[out.count++] = units;
        out.totalModules = static_cast<uint16_t>(out.totalModules + units);
    };

    // Start: narrow bar, narrow space, narrow bar, narrow space.
    append(kNarrow);
    append(kNarrow);
    append(kNarrow);
    append(kNarrow);

    // One pair at a time: the first digit's five elements are the bars, the
    // second's are the spaces they sit between. Appending them alternately is
    // what "interleaved" means, and it keeps Encoded's bar-first alternation
    // true without the renderer knowing anything about it.
    for (size_t i = 0; i < length; i += 2) {
        const uint8_t *bars   = kWideFlags[text[i] - '0'];
        const uint8_t *spaces = kWideFlags[text[i + 1] - '0'];
        for (size_t e = 0; e < kElementsPerDigit; e++) {
            append(bars[e] ? kWide : kNarrow);
            append(spaces[e] ? kWide : kNarrow);
        }
    }

    // Stop: wide bar, narrow space, narrow bar. The only asymmetry in the
    // symbol, and what tells a decoder which end it started from.
    append(kWide);
    append(kNarrow);
    append(kNarrow);

    return true;
}

} // namespace Itf

#endif // ITF_HPP
