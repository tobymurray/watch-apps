#include "Paint.hpp"

namespace Paint
{

// A plain 5x7 cell font. Only digits: the wearer needs a count, and everything
// with words in it goes to the log file instead.
const uint8_t kDigits[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
};

void fill(uint8_t *fb, int16_t w, int16_t h, uint8_t c)
{
    const int32_t count = static_cast<int32_t>(w) * static_cast<int32_t>(h);
    for (int32_t i = 0; i < count; i++) {
        fb[i] = c;
    }
}

void rect(uint8_t *fb, int16_t w, int16_t h,
          int16_t x, int16_t y, int16_t rw, int16_t rh, uint8_t c)
{
    for (int16_t row = y; row < y + rh; row++) {
        if (row < 0 || row >= h) {
            continue;
        }
        for (int16_t col = x; col < x + rw; col++) {
            if (col < 0 || col >= w) {
                continue;
            }
            fb[static_cast<int32_t>(row) * w + col] = c;
        }
    }
}

void digit(uint8_t *fb, int16_t w, int16_t h,
           int16_t x, int16_t y, int16_t scale, uint8_t value, uint8_t c)
{
    if (value > 9) {
        return;
    }
    for (int16_t col = 0; col < 5; col++) {
        const uint8_t bits = kDigits[value][col];
        for (int16_t row = 0; row < 7; row++) {
            if ((bits >> row) & 0x1u) {
                rect(fb, w, h,
                     static_cast<int16_t>(x + col * scale),
                     static_cast<int16_t>(y + row * scale),
                     scale, scale, c);
            }
        }
    }
}

void number(uint8_t *fb, int16_t w, int16_t h,
            int16_t x, int16_t y, int16_t scale, uint32_t value, uint8_t places,
            uint8_t c)
{
    // Advance is the glyph plus one cell of gap, so digits do not touch.
    const int16_t advance = static_cast<int16_t>(6 * scale);
    for (uint8_t i = 0; i < places; i++) {
        uint32_t divisor = 1;
        for (uint8_t p = static_cast<uint8_t>(places - 1 - i); p > 0; p--) {
            divisor *= 10u;
        }
        digit(fb, w, h, static_cast<int16_t>(x + i * advance), y, scale,
              static_cast<uint8_t>((value / divisor) % 10u), c);
    }
}

} // namespace Paint
