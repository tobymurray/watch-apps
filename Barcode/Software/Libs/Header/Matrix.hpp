/**
 ******************************************************************************
 * @file    Matrix.hpp
 * @date    28-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What a matrix symbology produces: a square grid of light and dark.
 ******************************************************************************
 *
 * The other half of what Encoded.hpp says. That file carries a run of
 * alternating bar and space widths, which is a complete description of any
 * *linear* symbology and a useless one for a grid -- QR has no left-to-right
 * run structure to express. So a matrix symbology gets its own intermediate
 * form rather than Encoded growing a variant.
 *
 * That split is deliberate and it is the reason the linear path is untouched by
 * QR arriving: BarcodeWidget still consumes an Encoded and nothing else, with
 * no discriminant to test and no branch it did not have before. See Docs/QR.md.
 *
 * The grid has no unit and no size in pixels. It is modules, and only the
 * renderer knows what a module is worth -- 4 px, per BarcodeLayout::kQrModulePx.
 *
 * A bitset rather than a byte per module: 79 bytes against 625, and this
 * travels inside the GUI's widget rather than in a message, so the saving is
 * RAM rather than message budget. Both fit either way; the bitset is simply
 * what a grid of two-state modules is.
 *
 ******************************************************************************
 */

#ifndef MATRIX_HPP
#define MATRIX_HPP

#include <cstddef>
#include <cstdint>

namespace Barcode
{

/**
 * @brief A square grid of modules, dark or light.
 *
 * Sized for the largest matrix symbol this app draws, which is QR version 2 at
 * 25 modules a side. That is a fixed choice and not a ceiling reached by
 * accident -- Docs/QR.md sets out why the version is fixed, and version 3 does
 * not fit the panel above the id row at a whole-pixel module.
 */
struct Matrix
{
    static constexpr uint8_t kMaxSize  = 25;
    static constexpr size_t  kMaxCells = static_cast<size_t>(kMaxSize) * kMaxSize;
    static constexpr size_t  kBytes    = (kMaxCells + 7) / 8;

    uint8_t bits[kBytes]; ///< Row-major, one bit a module, 1 is dark
    uint8_t size;         ///< Modules a side, excluding the quiet zone. 0 when unset

    constexpr bool dark(uint8_t x, uint8_t y) const
    {
        const size_t i = static_cast<size_t>(y) * size + x;
        return (bits[i / 8] >> (i % 8)) & 1u;
    }

    void set(uint8_t x, uint8_t y, bool isDark)
    {
        const size_t i = static_cast<size_t>(y) * size + x;
        if (isDark) {
            bits[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        } else {
            bits[i / 8] &= static_cast<uint8_t>(~(1u << (i % 8)));
        }
    }

    void flip(uint8_t x, uint8_t y)
    {
        const size_t i = static_cast<size_t>(y) * size + x;
        bits[i / 8] ^= static_cast<uint8_t>(1u << (i % 8));
    }
};

} // namespace Barcode

#endif // MATRIX_HPP
