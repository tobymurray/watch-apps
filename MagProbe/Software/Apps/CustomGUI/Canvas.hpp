#ifndef CANVAS_HPP
#define CANVAS_HPP

#include <cstddef>
#include <cstdint>

/// Drawing into the watch's 8bpp ABGR2222 framebuffer.
///
/// Two bits a channel, so four levels each and 64 colours. Bits 7:6 are alpha,
/// 5:4 blue, 3:2 green, 1:0 red. Everything here writes full alpha: the
/// framebuffer is the whole frame, not a layer over one.
///
/// No clipping decisions are left to the caller. Every primitive clips, because
/// a diagnostic screen whose text runs off the edge is the case that happens on
/// hardware and never in a test, and a one-pixel overrun into a 240x240 buffer
/// is a memory bug rather than a cosmetic one.
namespace Abgr2222 {

/// Full alpha, each channel 0 to 3. At namespace scope because a constexpr
/// static member function cannot be called to initialise a static member of the
/// same class: the class is not complete yet at that point.
constexpr uint8_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint8_t>(0xC0u | ((b & 0x3u) << 4) | ((g & 0x3u) << 2) | (r & 0x3u));
}

} // namespace Abgr2222

class Canvas {
public:
    Canvas(uint8_t* buffer, uint16_t width, uint16_t height, size_t capacity);

    static constexpr uint8_t rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return Abgr2222::rgb(r, g, b);
    }

    // The panel drops dark-on-light text: bright glyphs on the dark background
    // render crisply, and dark thin glyphs on a light fill vanish outright
    // (measured on hardware, RustGuiPoc's README). So there is no light fill
    // colour here, and nothing in this app draws dark ink.
    static constexpr uint8_t kBlack  = Abgr2222::rgb(0, 0, 0);
    static constexpr uint8_t kWhite  = Abgr2222::rgb(3, 3, 3);
    static constexpr uint8_t kGrey   = Abgr2222::rgb(1, 1, 1);
    static constexpr uint8_t kRed    = Abgr2222::rgb(3, 0, 0);
    static constexpr uint8_t kGreen  = Abgr2222::rgb(0, 3, 0);
    static constexpr uint8_t kBlue   = Abgr2222::rgb(0, 0, 3);
    static constexpr uint8_t kYellow = Abgr2222::rgb(3, 3, 0);
    static constexpr uint8_t kCyan   = Abgr2222::rgb(0, 3, 3);

    uint16_t width() const { return mWidth; }
    uint16_t height() const { return mHeight; }
    bool     usable() const { return mBuffer != nullptr && mWidth > 0 && mHeight > 0; }

    void clear(uint8_t colour);
    void pixel(int32_t x, int32_t y, uint8_t colour);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t colour);
    void hLine(int32_t x, int32_t y, int32_t length, uint8_t colour);
    void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t colour);

    /// Text at `scale` times the 5x7 glyph size, one blank column between
    /// characters. Returns the x coordinate just past the last glyph, so callers
    /// can chain runs of different colours on one line.
    int32_t text(int32_t x, int32_t y, const char* str, uint8_t colour, uint8_t scale = 1);

    /// Width in pixels that `text` would occupy. No trailing gap.
    static int32_t textWidth(const char* str, uint8_t scale = 1);

    static int32_t textHeight(uint8_t scale = 1);

    /// Read back one pixel. For tests and for the framebuffer dump; the device
    /// never needs it.
    uint8_t at(int32_t x, int32_t y) const;

    /// The display is round: the panel masks a circle out of the square
    /// framebuffer, so the usable width depends on how far down the screen you
    /// are. A fixed rectangular inset does not work -- at 10 px from the top the
    /// circle is only about 90 px wide, and text drawn at a centre-safe inset
    /// there is simply not on the glass.
    ///
    /// Returns the leftmost x at which a run of text `glyphHeight` tall,
    /// starting at row `y`, is fully inside the mask. Negative when the row has
    /// no usable width at all.
    ///
    /// The constraint is taken from whichever of the run's top and bottom rows
    /// is farther from the centre, because that is the one the circle cuts
    /// first.
    int32_t safeLeft(int32_t y, int32_t glyphHeight) const;

    /// Mirror of safeLeft: the first x past the usable area.
    int32_t safeRight(int32_t y, int32_t glyphHeight) const;

    /// Draw `str` centred on the row, at the largest scale from `maxScale` down
    /// to 1 that fits inside the mask at that row. Returns the scale used, or 0
    /// when even single size does not fit.
    ///
    /// Every headline goes through this rather than through a hand-picked scale,
    /// because a string that outgrows its row is invisible on the device and
    /// invisible to any test that only counts ink.
    uint8_t textCentredFitted(int32_t y, const char* str, uint8_t colour, uint8_t maxScale);

    /// How far inside the circle to stay. The mask edge is not perfectly sharp
    /// and a glyph touching it reads as damaged rather than as clipped.
    static constexpr int32_t kBezelMargin = 3;

private:
    bool inBounds(int32_t x, int32_t y) const;

    uint8_t* mBuffer;
    uint16_t mWidth;
    uint16_t mHeight;
    size_t   mCapacity;
};

#endif // CANVAS_HPP
