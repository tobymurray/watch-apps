#ifndef PAINT_HPP
#define PAINT_HPP

#include <cstdint>

/**
 * @brief Just enough drawing for a probe: solid rectangles and digits.
 *
 * The framebuffer is 8bpp ABGR2222, two bits per channel with the alpha pair
 * held opaque. Encoding is copied from QrGuiPoc's Rust core so the two agree.
 */
namespace Paint
{

constexpr uint8_t colour(uint8_t r2, uint8_t g2, uint8_t b2)
{
    return static_cast<uint8_t>((0x3u << 6) | ((b2 & 0x3u) << 4) |
                                ((g2 & 0x3u) << 2) | (r2 & 0x3u));
}

constexpr uint8_t BLACK = colour(0, 0, 0);
constexpr uint8_t WHITE = colour(3, 3, 3);
constexpr uint8_t GREY  = colour(1, 1, 1);
constexpr uint8_t RED   = colour(3, 0, 0);
constexpr uint8_t GREEN = colour(0, 3, 0);
constexpr uint8_t BLUE  = colour(0, 1, 3);
constexpr uint8_t AMBER = colour(3, 2, 0);

/** Digits 0-9 as five columns; within a column bit 0 is the top row. */
extern const uint8_t kDigits[10][5];

void fill(uint8_t *fb, int16_t w, int16_t h, uint8_t c);

void rect(uint8_t *fb, int16_t w, int16_t h,
          int16_t x, int16_t y, int16_t rw, int16_t rh, uint8_t c);

/** One digit at [scale] pixels per font pixel. Out-of-range values draw nothing. */
void digit(uint8_t *fb, int16_t w, int16_t h,
           int16_t x, int16_t y, int16_t scale, uint8_t value, uint8_t c);

/** [value] right-aligned to [places], zero padded, advancing left to right. */
void number(uint8_t *fb, int16_t w, int16_t h,
            int16_t x, int16_t y, int16_t scale, uint32_t value, uint8_t places,
            uint8_t c);

} // namespace Paint

#endif // PAINT_HPP
