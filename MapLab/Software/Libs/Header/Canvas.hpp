/**
 ******************************************************************************
 * @file    Canvas.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The candidate software rasteriser: 8bpp ABGR2222 primitives into a
 *          caller-owned buffer, with no heap, no SDK and no TouchGFX.
 ******************************************************************************
 *
 * This is the thing being measured, not a mock of it. If the vector pivot
 * happens, this code -- not something like it -- is what `MapKit` grows a
 * renderer out of, which is why it is written to the constraints the render
 * path will actually have rather than to the constraints a benchmark could get
 * away with:
 *
 *   - **The caller owns the pixels.** A 240x240 canvas is 57,600 bytes and must
 *     have static storage duration in the app, so the linker rather than the
 *     heap arbitrates the GUI RAM budget -- the same reason `MapKit`'s
 *     TileCache is file-static in every app's Model.cpp. This class holds a
 *     pointer and never allocates.
 *   - **No floating point anywhere.** Not for speed: for honesty. A rasteriser
 *     that quietly used doubles on the host would measure something the
 *     Cortex-M33's single-precision FPU does not do.
 *   - **Everything clips.** A primitive that walked off the buffer would be
 *     both a bug and a faster benchmark than the truth.
 *
 * ---------------------------------------------------------------------------
 * WHY THE POLYGON FILL COUNTS ITS OWN FAILURES
 *
 * `fillPolygon` collects edge crossings per scanline into a fixed array, which
 * is the only shape available with no heap. A shape complex enough to overflow
 * that array is drawn wrong -- and a rasteriser that dropped those spans
 * silently would make the *measurement* look good by drawing less than it was
 * asked to. So the budget is a named constant, the overflow is counted, and
 * every bench that fills polygons reports the counter. A non-zero value
 * invalidates the timing it appears beside.
 *
 * ---------------------------------------------------------------------------
 * WHY LINES ARE A SQUARE BRUSH AND NOT A QUAD
 *
 * `MapKit`'s trace already draws this way -- Bresenham steps stamping small
 * rectangles -- and it is device-proven at 240x240. A proper thick-line
 * polygon would be prettier at the joins and is a different measurement; this
 * one is what the existing code does, so its cost is comparable with what is
 * already on the watch. There is no anti-aliasing to lose: the panel has four
 * levels per channel and the cartography spec draws aliased on purpose.
 *
 * Pure: host-tested in `MapLab/Tests/Canvas_test.cpp`.
 ******************************************************************************
 */

#ifndef MAPLAB_CANVAS_HPP
#define MAPLAB_CANVAS_HPP

#include "Palette.hpp"

#include <cstdint>

namespace MapLab
{

/// A point in canvas pixels. int16 because the panel is 240 px and geometry
/// arrives already transformed; anything that needs more range has not been
/// clipped yet, which is the writer's job, not the renderer's.
struct Pt {
    int16_t x = 0;
    int16_t y = 0;
};

class Canvas
{
public:
    /// Crossings tracked per scanline in fillPolygon. 48 is generous for a
    /// clipped map polygon (a lake with fjords crossing one scanline 24 times
    /// is already unusual) and costs 96 bytes of stack.
    static constexpr int kMaxCrossings = 48;

    Canvas(uint8_t* pixels, int16_t width, int16_t height)
        : mPx(pixels), mW(width), mH(height) {}

    int16_t  width()  const { return mW; }
    int16_t  height() const { return mH; }
    uint8_t* pixels() const { return mPx; }
    uint32_t byteCount() const
    {
        return static_cast<uint32_t>(mW) * static_cast<uint32_t>(mH);
    }

    /// Spans dropped because a scanline exceeded kMaxCrossings, since
    /// construction or the last resetDropped(). Non-zero means anything drawn
    /// since is wrong.
    uint32_t dropped() const { return mDropped; }
    void     resetDropped()  { mDropped = 0; }

    void clear(uint8_t code);
    void plot(int16_t x, int16_t y, uint8_t code);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t code);

    /// Bresenham with a `width` x `width` brush. width < 1 is treated as 1.
    void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                   int16_t width, uint8_t code);

    /// The same, stamping only while a step counter is inside the `on` part of
    /// an on/off cycle. This is the trail signifier in
    /// MAP_CARTOGRAPHY_SPEC.md § 4 (3 on / 3 off at 2 px).
    void dashedLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    int16_t width, int16_t onPx, int16_t offPx, uint8_t code);

    void polyline(const Pt* pts, int count, int16_t width, uint8_t code);
    void dashedPolyline(const Pt* pts, int count, int16_t width,
                        int16_t onPx, int16_t offPx, uint8_t code);

    /// Even-odd scanline fill of a closed polygon; `pts` need not repeat the
    /// first point. Returns false if any scanline overflowed the crossing
    /// budget -- in which case the shape is drawn wrong and dropped() is
    /// non-zero.
    bool fillPolygon(const Pt* pts, int count, uint8_t code);

    /// Remap every pixel through a 64-entry restyle table (Palette.hpp).
    /// In place, one pass, no second buffer -- which is the cheap arrangement
    /// and also the only affordable one at 57,600 bytes.
    void applyLut(const uint8_t lut[kLutEntries]);

private:
    void hspan(int16_t y, int16_t x0, int16_t x1, uint8_t code);

    uint8_t* mPx;
    int16_t  mW;
    int16_t  mH;
    uint32_t mDropped = 0;
};

} // namespace MapLab

#endif // MAPLAB_CANVAS_HPP
