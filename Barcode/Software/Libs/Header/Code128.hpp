/**
 ******************************************************************************
 * @file    Code128.hpp
 * @date    25-07-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Header-only Code 128 Subset B barcode encoder.
 ******************************************************************************
 *
 * Pure data in, data out: no allocation and no SDK/TouchGFX dependency, so it
 * can be shared by both the Service and GUI binaries as an inline header,
 * the same way Stopwatch.hpp is used from both sides of that app.
 *
 ******************************************************************************
 */

#ifndef CODE128_HPP
#define CODE128_HPP

#include <cstdint>
#include <cstddef>

namespace Code128
{

constexpr size_t kMaxDataLength = 16;   ///< Longest id this encoder accepts
constexpr uint8_t kStartB = 104;
constexpr uint8_t kStop   = 106;

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

/**
 * @brief Encoded module widths for one barcode.
 *
 * The STOP symbol is the only one with a 7th, trailing bar element (the
 * standard 13-module STOP pattern is its usual 6-element row plus one more
 * bar), which is why the buffer allows for one extra width beyond the usual
 * 6-per-symbol accounting.
 */
struct Encoded
{
    static constexpr size_t kMaxWidths = (kMaxDataLength + 2) * 6 + 7;
    uint8_t  widths[kMaxWidths]; ///< Bar/space widths in modules, first is a bar
    uint8_t  count;              ///< Valid entries in widths
    uint16_t totalModules;       ///< Sum of widths, quiet zones excluded
};

/**
 * @brief Encode an ASCII id as Code 128 Subset B.
 * @param text Printable ASCII (32-126), at most kMaxDataLength characters.
 * @param out  Receives the module widths.
 * @retval true  Encoded.
 * @retval false Empty, too long, or outside the printable range.
 */
inline bool encode(const char *text, Encoded &out)
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

    auto appendSymbol = [&](uint8_t value) {
        for (int i = 0; i < 6; i++) {
            out.widths[out.count++] = kPatterns[value][i];
            out.totalModules += kPatterns[value][i];
        }
    };

    appendSymbol(kStartB);

    uint32_t checksum = kStartB;
    for (size_t i = 0; i < length; i++) {
        const uint8_t value = static_cast<uint8_t>(text[i] - 32);
        appendSymbol(value);
        checksum += value * static_cast<uint32_t>(i + 1);
    }

    appendSymbol(static_cast<uint8_t>(checksum % 103));

    appendSymbol(kStop);
    out.widths[out.count++] = 2; // STOP's extra trailing bar
    out.totalModules += 2;

    return true;
}

} // namespace Code128

#endif // CODE128_HPP
