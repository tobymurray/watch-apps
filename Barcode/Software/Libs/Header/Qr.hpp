/**
 ******************************************************************************
 * @file    Qr.hpp
 * @date    28-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   Header-only QR Code encoder: version 2, level M, byte mode. Only.
 ******************************************************************************
 *
 * Pure data in, data out, no allocation and no SDK/TouchGFX dependency, the
 * same shape as Code128.hpp so both binaries can share it as an inline header.
 *
 * ## Why one version and one error-correction level
 *
 * A general QR encoder carries a table of forty versions by four levels giving
 * block counts and codeword splits, the alignment-pattern positions for each
 * version, a second BCH for the version information, interleaving across
 * blocks, and a mode selector. Fixing the symbol deletes all of it:
 *
 *   - **Version 2** is 25 modules. Its only alignment pattern is at (18,18),
 *     and version information is drawn from version 7, so neither table exists.
 *   - **Level M at version 2** is a *single* block -- 28 data codewords, 16
 *     error-correction codewords, 44 in total -- so there is no interleaving.
 *   - **Byte mode always.** The app accepts printable ASCII; alphanumeric mode
 *     is denser but uppercase-only, and since the version is fixed a denser
 *     mode would buy a smaller symbol that would not be drawn. So there is no
 *     mode selection to write or to get wrong.
 *
 * What is left is one field, one generator polynomial and one grid. Docs/QR.md
 * has the arithmetic behind the choice, including why version 1 would force
 * level L (it holds 14 bytes at M, and an id may be 16) and why version 3 does
 * not fit the panel above the id row at a whole-pixel module.
 *
 * ## What this is held to
 *
 * Reed-Solomon and mask selection cannot be checked by eye the way Code 128's
 * 107-row table can, so nothing here is asserted on inspection. The generator
 * polynomial is **computed rather than transcribed**, for exactly that reason:
 * a hand-copied 17-byte table is a silent-wrong-answer waiting to happen, and a
 * mis-encoded symbol whose error correction is computed consistently over the
 * mistake decodes to a valid, wrong value.
 *
 * encodeWithMask() exists for the tests: forcing the mask lets the grid be
 * diffed against zint mask by mask, which tests the construction independently
 * of whose penalty scoring picks which mask. See Tests/oracle/generate.cpp.
 *
 ******************************************************************************
 */

#ifndef QR_HPP
#define QR_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Matrix.hpp"

namespace Qr
{

constexpr uint8_t kVersion = 2;
constexpr uint8_t kSize    = 17 + 4 * kVersion; ///< 25 modules a side

constexpr size_t kDataCodewords  = 28;
constexpr size_t kEcCodewords    = 16;
constexpr size_t kTotalCodewords = kDataCodewords + kEcCodewords;

/**
 * @brief Longest payload, in bytes.
 *
 * Byte mode spends 4 bits on the mode indicator and 8 on the character count
 * (8 bits for versions 1 to 9), so (28 * 8 - 12) / 8 = 26. Ten more than an id
 * can be, which is the property that makes adding QR add no new way for a code
 * to be refused -- see Barcode::Problem in Barcode.hpp.
 */
constexpr size_t kMaxDataLength = (kDataCodewords * 8 - 12) / 8;

/// Level M's two-bit field, per ISO/IEC 18004 table 12: L=01, M=00, Q=11, H=10.
constexpr uint8_t kEcLevelBits = 0b00;

/// The symbol must fit the shared grid, which is sized for this version alone.
static_assert(kSize <= Barcode::Matrix::kMaxSize,
              "Barcode::Matrix is not sized for this version");

namespace detail
{

/**
 * @brief Multiply in GF(256) with QR's primitive polynomial, x^8+x^4+x^3+x^2+1.
 *
 * Russian-peasant rather than log/antilog tables: 512 bytes of flash saved
 * against a few hundred iterations on a path that runs once when a code is
 * adopted and once when it is drawn. Neither is a number this app has to care
 * about, and the loop cannot be transcribed wrongly.
 */
inline uint8_t gfMul(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    while (b != 0) {
        if (b & 1u) {
            result ^= a;
        }
        b = static_cast<uint8_t>(b >> 1);
        a = (a & 0x80u) ? static_cast<uint8_t>((a << 1) ^ 0x1Du)
                        : static_cast<uint8_t>(a << 1);
    }
    return result;
}

/**
 * @brief The degree-16 generator polynomial, as coefficients x^15..x^0.
 *
 * The leading x^16 term is implicit: the polynomial is monic. Built by
 * multiplying out (x - 2^0)(x - 2^1)...(x - 2^15) over GF(256) rather than
 * written down, so there is no table to check against a reference.
 */
inline void generatorPolynomial(uint8_t (&out)[kEcCodewords])
{
    std::memset(out, 0, kEcCodewords);
    out[kEcCodewords - 1] = 1;

    uint8_t root = 1;
    for (size_t i = 0; i < kEcCodewords; i++) {
        for (size_t j = 0; j < kEcCodewords; j++) {
            out[j] = gfMul(out[j], root);
            if (j + 1 < kEcCodewords) {
                out[j] ^= out[j + 1];
            }
        }
        root = gfMul(root, 2);
    }
}

/// Remainder of @p data divided by the generator: the error-correction block.
inline void errorCorrection(const uint8_t *data, size_t length,
                            uint8_t (&out)[kEcCodewords])
{
    uint8_t divisor[kEcCodewords];
    generatorPolynomial(divisor);

    std::memset(out, 0, kEcCodewords);
    for (size_t i = 0; i < length; i++) {
        const uint8_t factor = static_cast<uint8_t>(data[i] ^ out[0]);
        std::memmove(out, out + 1, kEcCodewords - 1);
        out[kEcCodewords - 1] = 0;
        for (size_t j = 0; j < kEcCodewords; j++) {
            out[j] ^= gfMul(divisor[j], factor);
        }
    }
}

/// Everything that is not data: finders, separators, timing, alignment, the
/// dark module and the two format-information strips. Data placement skips
/// these and the mask never inverts them.
struct Grid
{
    Barcode::Matrix &modules;
    Barcode::Matrix  function;

    explicit Grid(Barcode::Matrix &m) : modules(m), function{}
    {
        modules.size = kSize;
        std::memset(modules.bits, 0, sizeof modules.bits);
        function.size = kSize;
        std::memset(function.bits, 0, sizeof function.bits);
    }

    void setFunction(int x, int y, bool dark)
    {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) {
            return;
        }
        modules.set(static_cast<uint8_t>(x), static_cast<uint8_t>(y), dark);
        function.set(static_cast<uint8_t>(x), static_cast<uint8_t>(y), true);
    }

    bool isFunction(int x, int y) const
    {
        return function.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    }

    bool dark(int x, int y) const
    {
        return modules.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    }
};

/// A 7x7 finder with its 1-module light separator, centred on (@p cx, @p cy).
/// Drawn as concentric rings so the separator falls out of the same loop: the
/// ring at Chebyshev distance 2 and the one at 4 are light, the rest dark.
inline void drawFinder(Grid &g, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            const int ax = dx < 0 ? -dx : dx;
            const int ay = dy < 0 ? -dy : dy;
            const int ring = ax > ay ? ax : ay;
            g.setFunction(cx + dx, cy + dy, ring != 2 && ring != 4);
        }
    }
}

/// The single 5x5 alignment pattern version 2 carries, at (18,18). The other
/// three positions the row/column rule generates -- (6,6), (6,18), (18,6) --
/// collide with finder patterns and are not drawn, per ISO/IEC 18004 6.3.6.
inline void drawAlignment(Grid &g, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            const int ax = dx < 0 ? -dx : dx;
            const int ay = dy < 0 ? -dy : dy;
            g.setFunction(cx + dx, cy + dy, (ax > ay ? ax : ay) != 1);
        }
    }
}

/**
 * @brief The 15-bit format string: two data bits, a BCH(15,5) remainder, XORed.
 *
 * The XOR with 101010000010010 is what stops an all-light format area, which
 * would otherwise be indistinguishable from a blank region for level M with
 * mask 0 -- the exact combination this app uses, so it is not a hypothetical.
 */
inline uint16_t formatBits(uint8_t mask)
{
    const uint16_t data = static_cast<uint16_t>((kEcLevelBits << 3) | mask);
    uint16_t rem = data;
    for (int i = 0; i < 10; i++) {
        rem = static_cast<uint16_t>((rem << 1) ^ ((rem >> 9) * 0x537u));
    }
    return static_cast<uint16_t>(((data << 10) | (rem & 0x3FFu)) ^ 0x5412u);
}

/// Both copies of the format information, plus the module that is always dark.
/// Called once with mask 0 before data placement -- to reserve the modules --
/// and again per candidate mask, because the penalty score is evaluated over
/// the complete symbol and the format bits are part of it.
inline void drawFormat(Grid &g, uint8_t mask)
{
    const uint16_t bits = formatBits(mask);
    auto bit = [&](int i) { return ((bits >> i) & 1u) != 0; };

    for (int i = 0; i <= 5; i++) {
        g.setFunction(8, i, bit(i));
    }
    g.setFunction(8, 7, bit(6));
    g.setFunction(8, 8, bit(7));
    g.setFunction(7, 8, bit(8));
    for (int i = 9; i < 15; i++) {
        g.setFunction(14 - i, 8, bit(i));
    }

    for (int i = 0; i < 8; i++) {
        g.setFunction(kSize - 1 - i, 8, bit(i));
    }
    for (int i = 8; i < 15; i++) {
        g.setFunction(8, kSize - 15 + i, bit(i));
    }

    g.setFunction(8, kSize - 8, true);
}

inline void drawFunctionPatterns(Grid &g)
{
    // Timing first, so the finders overwrite the ends of both runs rather than
    // the other way round.
    for (int i = 0; i < kSize; i++) {
        g.setFunction(6, i, i % 2 == 0);
        g.setFunction(i, 6, i % 2 == 0);
    }

    drawFinder(g, 3, 3);
    drawFinder(g, kSize - 4, 3);
    drawFinder(g, 3, kSize - 4);

    drawAlignment(g, 18, 18);

    // Mask 0 is a placeholder: this call is here to reserve the modules before
    // data placement walks the grid. The real bits go down per candidate mask.
    drawFormat(g, 0);
}

/**
 * @brief Lay the codewords out in the two-module-wide upward/downward zigzag.
 *
 * Right to left in column pairs, skipping column 6 entirely because that is
 * the vertical timing pattern and it does not shift the pairing of the columns
 * to its left. Version 2 leaves 7 remainder bits at the end, which stay light.
 */
inline void drawCodewords(Grid &g, const uint8_t *codewords)
{
    size_t bit = 0;
    const size_t bits = kTotalCodewords * 8;

    for (int right = kSize - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (int vert = 0; vert < kSize; vert++) {
            for (int j = 0; j < 2; j++) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? kSize - 1 - vert : vert;
                if (!g.isFunction(x, y) && bit < bits) {
                    const bool dark = ((codewords[bit >> 3] >> (7 - (bit & 7))) & 1u) != 0;
                    g.modules.set(static_cast<uint8_t>(x), static_cast<uint8_t>(y), dark);
                    bit++;
                }
            }
        }
    }
}

/// XOR one of the eight masks over every non-function module. Its own inverse,
/// which is what lets the selector try each in turn on one grid.
inline void applyMask(Grid &g, uint8_t mask)
{
    for (int y = 0; y < kSize; y++) {
        for (int x = 0; x < kSize; x++) {
            if (g.isFunction(x, y)) {
                continue;
            }
            bool invert = false;
            switch (mask) {
            case 0: invert = (x + y) % 2 == 0; break;
            case 1: invert = y % 2 == 0; break;
            case 2: invert = x % 3 == 0; break;
            case 3: invert = (x + y) % 3 == 0; break;
            case 4: invert = (y / 2 + x / 3) % 2 == 0; break;
            case 5: invert = (x * y) % 2 + (x * y) % 3 == 0; break;
            case 6: invert = ((x * y) % 2 + (x * y) % 3) % 2 == 0; break;
            case 7: invert = ((x + y) % 2 + (x * y) % 3) % 2 == 0; break;
            default: break;
            }
            if (invert) {
                g.modules.flip(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
            }
        }
    }
}

/**
 * @brief The four penalty rules of ISO/IEC 18004 7.8.3, summed.
 *
 * Lower is better. Nothing about this is a correctness property -- every mask
 * produces a decodable symbol, because the mask is recorded in the format
 * information -- so a disagreement with another implementation's choice is a
 * difference of judgement and not a bug. That is why the oracle diffs a
 * *forced* mask, and this score is tested against its own definition.
 */
inline uint32_t penalty(const Grid &g)
{
    uint32_t score = 0;

    // N1: runs of five or more of one colour, along rows and along columns.
    for (int pass = 0; pass < 2; pass++) {
        for (int a = 0; a < kSize; a++) {
            int run = 1;
            bool previous = pass == 0 ? g.dark(0, a) : g.dark(a, 0);
            for (int b = 1; b < kSize; b++) {
                const bool here = pass == 0 ? g.dark(b, a) : g.dark(a, b);
                if (here == previous) {
                    run++;
                } else {
                    if (run >= 5) {
                        score += static_cast<uint32_t>(3 + run - 5);
                    }
                    run = 1;
                    previous = here;
                }
            }
            if (run >= 5) {
                score += static_cast<uint32_t>(3 + run - 5);
            }
        }
    }

    // N2: every 2x2 block of one colour.
    for (int y = 0; y < kSize - 1; y++) {
        for (int x = 0; x < kSize - 1; x++) {
            const bool c = g.dark(x, y);
            if (c == g.dark(x + 1, y) && c == g.dark(x, y + 1) && c == g.dark(x + 1, y + 1)) {
                score += 3;
            }
        }
    }

    // N3: the 1:1:3:1:1 finder-like ratio with four light modules on one side,
    // read as an eleven-module window in both directions.
    static const bool kAhead[11] = {true, false, true, true, true, false, true,
                                    false, false, false, false};
    static const bool kBehind[11] = {false, false, false, false, true, false, true,
                                     true, true, false, true};
    for (int pass = 0; pass < 2; pass++) {
        for (int a = 0; a < kSize; a++) {
            for (int b = 0; b + 11 <= kSize; b++) {
                bool ahead = true, behind = true;
                for (int k = 0; k < 11; k++) {
                    const bool here = pass == 0 ? g.dark(b + k, a) : g.dark(a, b + k);
                    ahead  = ahead  && here == kAhead[k];
                    behind = behind && here == kBehind[k];
                }
                if (ahead) {
                    score += 40;
                }
                if (behind) {
                    score += 40;
                }
            }
        }
    }

    // N4: how far the proportion of dark modules is from half, in 5% steps.
    uint32_t dark = 0;
    for (int y = 0; y < kSize; y++) {
        for (int x = 0; x < kSize; x++) {
            if (g.dark(x, y)) {
                dark++;
            }
        }
    }
    const uint32_t total = static_cast<uint32_t>(kSize) * kSize;
    // Compared as a ratio rather than a percentage so nothing rounds early.
    const int32_t deviation = static_cast<int32_t>(dark * 20) - static_cast<int32_t>(total * 10);
    const uint32_t magnitude = static_cast<uint32_t>(deviation < 0 ? -deviation : deviation);
    score += 10 * (magnitude / total);

    return score;
}

/// The bit stream: mode, length, payload, terminator, byte padding, then the
/// alternating pad codewords the standard names 11101100 and 00010001.
inline void buildCodewords(const char *text, size_t length, uint8_t *codewords)
{
    std::memset(codewords, 0, kTotalCodewords);

    size_t bit = 0;
    auto put = [&](uint32_t value, int width) {
        for (int i = width - 1; i >= 0; i--) {
            if ((value >> i) & 1u) {
                codewords[bit / 8] |= static_cast<uint8_t>(0x80u >> (bit % 8));
            }
            bit++;
        }
    };

    put(0b0100, 4);                                  // byte mode
    put(static_cast<uint32_t>(length), 8);           // 8-bit count, versions 1-9
    for (size_t i = 0; i < length; i++) {
        put(static_cast<uint8_t>(text[i]), 8);
    }

    const size_t capacity = kDataCodewords * 8;
    const size_t spare    = capacity - bit;
    put(0, static_cast<int>(spare < 4 ? spare : 4)); // terminator
    while (bit % 8 != 0) {
        put(0, 1);
    }

    for (size_t i = bit / 8, n = 0; i < kDataCodewords; i++, n++) {
        codewords[i] = (n % 2 == 0) ? 0xEC : 0x11;
    }

    uint8_t ec[kEcCodewords];
    errorCorrection(codewords, kDataCodewords, ec);
    std::memcpy(codewords + kDataCodewords, ec, kEcCodewords);
}

} // namespace detail

/**
 * @brief Can a symbol of this version carry @p text at all?
 * @retval false Empty, longer than kMaxDataLength, or outside printable ASCII.
 *
 * The whole of what this encoder refuses, and the *only* place it is decided:
 * encode() and encodeWithMask() both start by calling this, so it is not a
 * second opinion about what is drawable, it is the opinion.
 *
 * It exists because the service asks "is this drawable?" about every code it
 * adopts, and answering that by encoding one costs 2.3 KB of Reed-Solomon,
 * mask patterns and penalty scoring in a binary that never draws anything --
 * measured, against a build where Barcode::isDrawable() encoded a probe. The
 * printable-ASCII rule is not QR's; byte mode would carry a control character
 * happily. It is this app's, and it is about what the screen can show
 * underneath rather than what the symbology could hold.
 */
inline bool accepts(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0') {
        if (length >= kMaxDataLength || text[length] < 32 || text[length] > 126) {
            return false;
        }
        length++;
    }
    return length != 0;
}

/**
 * @brief Encode @p text with a caller-chosen mask. For tests and for encode().
 * @retval false Empty, longer than kMaxDataLength, or outside printable ASCII.
 *
 * A forced mask is what makes the zint diff mean something: it tests the grid
 * against an independent implementation without either of them having to agree
 * about which mask is prettiest.
 */
inline bool encodeWithMask(const char *text, Barcode::Matrix &out, uint8_t mask)
{
    if (!accepts(text) || mask > 7) {
        return false;
    }

    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }

    uint8_t codewords[kTotalCodewords];
    detail::buildCodewords(text, length, codewords);

    detail::Grid g(out);
    detail::drawFunctionPatterns(g);
    detail::drawCodewords(g, codewords);
    detail::drawFormat(g, mask);
    detail::applyMask(g, mask);

    return true;
}

/**
 * @brief Encode @p text as a QR symbol, choosing the mask that scores lowest.
 * @param text Printable ASCII (32-126), at most kMaxDataLength characters.
 * @param out  Receives a kSize x kSize module grid, excluding the quiet zone.
 * @retval true  Encoded.
 * @retval false Empty, too long, or outside the printable range. There is no
 *               fallback and no truncation -- see Barcode.hpp on why a barcode
 *               that scans as something else is worse than none.
 *
 * The quiet zone is not in the grid. It is four modules of light on every side
 * and it belongs to the renderer, the same way the white backing behind the
 * Code 128 bars is the quiet zone there rather than part of Encoded.
 */
inline bool encode(const char *text, Barcode::Matrix &out)
{
    if (!accepts(text)) {
        return false;
    }

    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }

    uint8_t codewords[kTotalCodewords];
    detail::buildCodewords(text, length, codewords);

    detail::Grid g(out);
    detail::drawFunctionPatterns(g);
    detail::drawCodewords(g, codewords);

    uint8_t  best      = 0;
    uint32_t bestScore = 0;
    for (uint8_t mask = 0; mask < 8; mask++) {
        detail::drawFormat(g, mask);
        detail::applyMask(g, mask);
        const uint32_t score = detail::penalty(g);
        detail::applyMask(g, mask); // its own inverse: put the grid back
        if (mask == 0 || score < bestScore) {
            bestScore = score;
            best      = mask;
        }
    }

    detail::drawFormat(g, best);
    detail::applyMask(g, best);
    return true;
}

/// Which mask encode() chose for @p text, read back out of the symbol's own
/// format information rather than recomputed -- so this doubles as a check
/// that the format strip round-trips. Reporting only, for the tests.
inline int chosenMask(const char *text)
{
    Barcode::Matrix probe{};
    if (!encode(text, probe)) {
        return -1;
    }

    auto bit = [&](int x, int y) {
        return probe.dark(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) ? 1u : 0u;
    };

    // The first copy, in the order drawFormat() lays it down.
    uint32_t bits = 0;
    for (int i = 0; i <= 5; i++) {
        bits |= bit(8, i) << i;
    }
    bits |= bit(8, 7) << 6;
    bits |= bit(8, 8) << 7;
    bits |= bit(7, 8) << 8;
    for (int i = 9; i < 15; i++) {
        bits |= bit(14 - i, 8) << i;
    }

    return static_cast<int>(((bits ^ 0x5412u) >> 10) & 0x07u);
}

} // namespace Qr

#endif // QR_HPP
