#include "Canvas.hpp"

#include "Font5x7.hpp"

#include <cmath>

namespace {

constexpr uint8_t kGlyphAdvance = Font5x7::kWidth + 1;

int32_t absDiff(int32_t a, int32_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

} // namespace

Canvas::Canvas(uint8_t* buffer, uint16_t width, uint16_t height, size_t capacity)
    : mBuffer(buffer)
    , mWidth(width)
    , mHeight(height)
    , mCapacity(capacity)
{
    // A geometry that does not fit the buffer it was handed is refused outright
    // rather than clipped to. The alternative is drawing a frame that is the
    // wrong shape and looks deliberate.
    const size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (mBuffer == nullptr || needed == 0 || needed > mCapacity) {
        mBuffer = nullptr;
        mWidth  = 0;
        mHeight = 0;
    }
}

bool Canvas::inBounds(int32_t x, int32_t y) const
{
    return mBuffer != nullptr &&
           x >= 0 && y >= 0 &&
           x < static_cast<int32_t>(mWidth) &&
           y < static_cast<int32_t>(mHeight);
}

void Canvas::clear(uint8_t colour)
{
    if (!usable()) {
        return;
    }
    const size_t n = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
    for (size_t i = 0; i < n; ++i) {
        mBuffer[i] = colour;
    }
}

void Canvas::pixel(int32_t x, int32_t y, uint8_t colour)
{
    if (!inBounds(x, y)) {
        return;
    }
    mBuffer[static_cast<size_t>(y) * mWidth + static_cast<size_t>(x)] = colour;
}

uint8_t Canvas::at(int32_t x, int32_t y) const
{
    if (!inBounds(x, y)) {
        return 0;
    }
    return mBuffer[static_cast<size_t>(y) * mWidth + static_cast<size_t>(x)];
}

void Canvas::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t colour)
{
    if (!usable() || w <= 0 || h <= 0) {
        return;
    }
    for (int32_t row = y; row < y + h; ++row) {
        for (int32_t col = x; col < x + w; ++col) {
            pixel(col, row, colour);
        }
    }
}

void Canvas::hLine(int32_t x, int32_t y, int32_t length, uint8_t colour)
{
    fillRect(x, y, length, 1, colour);
}

void Canvas::line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t colour)
{
    // Integer Bresenham. The needle is the only curve this app draws and it is
    // straight, so nothing here needs to be smoother than this.
    const int32_t dx = absDiff(x1, x0);
    const int32_t dy = -absDiff(y1, y0);
    const int32_t sx = (x0 < x1) ? 1 : -1;
    const int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t       err = dx + dy;

    while (true) {
        pixel(x0, y0, colour);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0  += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0  += sy;
        }
    }
}

int32_t Canvas::textWidth(const char* str, uint8_t scale)
{
    if (str == nullptr || scale == 0) {
        return 0;
    }
    int32_t n = 0;
    while (str[n] != '\0') {
        ++n;
    }
    if (n == 0) {
        return 0;
    }
    // n glyphs, n-1 gaps.
    return (n * Font5x7::kWidth + (n - 1)) * scale;
}

int32_t Canvas::textHeight(uint8_t scale)
{
    return Font5x7::kHeight * (scale == 0 ? 1 : scale);
}

int32_t Canvas::text(int32_t x, int32_t y, const char* str, uint8_t colour, uint8_t scale)
{
    if (!usable() || str == nullptr || scale == 0) {
        return x;
    }

    int32_t cursor = x;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        const uint8_t* cols = Font5x7::glyph(str[i]);

        for (uint8_t col = 0; col < Font5x7::kWidth; ++col) {
            const uint8_t bits = cols[col];
            for (uint8_t row = 0; row < Font5x7::kHeight; ++row) {
                if ((bits & (1u << row)) == 0) {
                    continue;
                }
                if (scale == 1) {
                    pixel(cursor + col, y + row, colour);
                } else {
                    fillRect(cursor + col * scale, y + row * scale, scale, scale, colour);
                }
            }
        }
        cursor += kGlyphAdvance * scale;
    }

    // Back off the trailing inter-character gap so the return value is the edge
    // of the ink, which is what a caller chaining a second run wants.
    return cursor - scale;
}

namespace {

/// Half-width of the mask circle at a given distance from its centre row, or a
/// negative value when the row is outside it.
float halfWidthAt(float radius, float dy)
{
    const float inside = radius * radius - dy * dy;
    if (inside <= 0.0f) {
        return -1.0f;
    }
    return std::sqrt(inside);
}

} // namespace

int32_t Canvas::safeLeft(int32_t y, int32_t glyphHeight) const
{
    if (!usable()) {
        return -1;
    }

    const float cx = static_cast<float>(mWidth - 1) / 2.0f;
    const float cy = static_cast<float>(mHeight - 1) / 2.0f;
    const float r =
        static_cast<float>(mWidth < mHeight ? mWidth : mHeight) / 2.0f -
        static_cast<float>(kBezelMargin);

    const int32_t last = y + (glyphHeight > 0 ? glyphHeight - 1 : 0);
    const float   dyA  = std::fabs(static_cast<float>(y) - cy);
    const float   dyB  = std::fabs(static_cast<float>(last) - cy);
    const float   dy   = (dyA > dyB) ? dyA : dyB;

    const float half = halfWidthAt(r, dy);
    if (half < 0.0f) {
        return -1;
    }

    const int32_t x = static_cast<int32_t>(std::ceil(cx - half));
    return (x < 0) ? 0 : x;
}

int32_t Canvas::safeRight(int32_t y, int32_t glyphHeight) const
{
    const int32_t left = safeLeft(y, glyphHeight);
    if (left < 0) {
        return -1;
    }
    // Symmetric about the centre column.
    return static_cast<int32_t>(mWidth) - left;
}

uint8_t Canvas::textCentredFitted(int32_t y, const char* str, uint8_t colour, uint8_t maxScale)
{
    if (!usable() || str == nullptr || maxScale == 0) {
        return 0;
    }

    for (uint8_t scale = maxScale; scale >= 1; --scale) {
        const int32_t h    = textHeight(scale);
        const int32_t left = safeLeft(y, h);
        if (left < 0) {
            return 0;
        }

        const int32_t available = static_cast<int32_t>(mWidth) - 2 * left;
        const int32_t needed    = textWidth(str, scale);
        if (needed <= available) {
            text((static_cast<int32_t>(mWidth) - needed) / 2, y, str, colour, scale);
            return scale;
        }
    }
    return 0;
}
