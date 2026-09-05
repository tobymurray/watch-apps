/**
 ******************************************************************************
 * @file    Encoded.hpp
 * @date    28-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What every symbology produces: a run of bar and space widths.
 ******************************************************************************
 *
 * This is the whole interface between "what the id says" and "what gets drawn",
 * and it was already the interface before it had a file of its own -- it just
 * lived inside Code128.hpp and was named after one symbology.
 *
 * A run of widths, alternating bar and space and always starting with a bar, is
 * a complete description of any *linear* symbology's output. Code 128, ITF,
 * EAN-13 and Code 39 differ in how a character becomes widths and in nothing
 * else; none of them can say anything to a renderer that this cannot carry. So
 * BarcodeWidget::drawCanvasWidget() consumes this and never learns a
 * symbology's name, and adding one is a new encoder rather than a new drawing
 * path. See Docs/SYMBOLOGIES.md.
 *
 * The widths have no unit. They are whatever the symbology counts in -- modules
 * for Code 128, narrow elements for ITF -- and the renderer only ever divides
 * by their sum, so a symbology needing a 2.5:1 wide:narrow ratio can express it
 * as 5 and 2 without anything downstream noticing.
 *
 ******************************************************************************
 */

#ifndef ENCODED_HPP
#define ENCODED_HPP

#include <cstddef>
#include <cstdint>

namespace Barcode
{

/**
 * @brief The most symbols one barcode may hold, excluding the stop.
 *
 * A start, one symbol per character of the longest id (Barcode::kMaxIdLength),
 * and a check. Each symbology static_asserts that its own worst case fits,
 * rather than this header reaching for a limit that belongs to the app --
 * Code128.hpp does so against kMaxDataLength.
 *
 * "One symbol per character" is the worst case and not the usual one: Code 128
 * packs a pair of digits into a single symbol where it can, which only ever
 * makes a barcode shorter than this allows for.
 */
constexpr size_t kMaxSymbols = 24;

/**
 * @brief Encoded element widths for one barcode.
 *
 * The seventh element allowed beyond the six-per-symbol accounting is Code
 * 128's: its STOP is the only symbol with a trailing seventh bar. A symbology
 * whose elements do not come six to a symbol -- ITF's do not -- simply uses
 * fewer entries.
 */
struct Encoded
{
    static constexpr size_t kMaxWidths = kMaxSymbols * 6 + 7;

    uint8_t  widths[kMaxWidths]; ///< Element widths, first is a bar
    uint8_t  count;              ///< Valid entries in widths
    uint16_t totalModules;       ///< Sum of widths, quiet zones excluded
};

} // namespace Barcode

#endif // ENCODED_HPP
