#include <MapKit/MapTileView.hpp>

#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/Color.hpp>

#include <algorithm>
#include <cstdlib>

using namespace touchgfx;

namespace MapKit
{
namespace
{
// Trace styling: chunky breadcrumb, sized to stay legible over a basemap on a
// display with 2 bits per channel.
constexpr int16_t kTraceDot   = 3;   // px square per Bresenham step
constexpr uint8_t kTraceR     = 220;
constexpr uint8_t kTraceG     = 40;
constexpr uint8_t kTraceB     = 40;
}

void MapTileView::draw(const Rect& area) const
{
    Rect absArea = area;
    translateRectToAbsolute(absArea);

    // Land/no-data background: light warm gray, close to OSM's land tint,
    // so absent tiles at the coverage edge read as "beyond the map".
    HAL::lcd().fillRect(absArea, Color::getColorFromRGB(224, 220, 212), 255);

    const int16_t absDX = static_cast<int16_t>(absArea.x - area.x);
    const int16_t absDY = static_cast<int16_t>(absArea.y - area.y);

    // Viewport origin in world pixels at the display zoom.
    const int64_t cx = MapMath::rescale(mCenterX, MapMath::TRACE_ZOOM, mZoom);
    const int64_t cy = MapMath::rescale(mCenterY, MapMath::TRACE_ZOOM, mZoom);
    const int64_t viewOriginX = cx - getWidth() / 2;
    const int64_t viewOriginY = cy - getHeight() / 2;

    if (!mSuppressTiles && mContainer != nullptr && mContainer->isOpen() && mCache != nullptr) {
        drawTiles(absArea, viewOriginX, viewOriginY, absDX, absDY);
    }
    // The trace and marker draw in every state. A missing, unverified or
    // corrupt pack must not cost the wearer the breadcrumb of the activity
    // they are actually recording.
    if (mTrace != nullptr) {
        drawTrace(absArea, viewOriginX, viewOriginY, absDX, absDY);
    }
    drawMarker(absArea, absDX, absDY);
}

void MapTileView::drawTiles(const Rect& absArea,
                            int64_t viewOriginX, int64_t viewOriginY,
                            int16_t absDX, int16_t absDY) const
{
    const int64_t worldMax = MapMath::worldSizePx(mZoom) - 1;
    const int64_t left     = std::max<int64_t>(viewOriginX, 0);
    const int64_t top      = std::max<int64_t>(viewOriginY, 0);
    const int64_t right    = std::min<int64_t>(viewOriginX + getWidth() - 1, worldMax);
    const int64_t bottom   = std::min<int64_t>(viewOriginY + getHeight() - 1, worldMax);
    if (left > right || top > bottom) {
        return;
    }

    for (int64_t ty = top >> MapMath::TILE_SHIFT; ty <= (bottom >> MapMath::TILE_SHIFT); ++ty) {
        for (int64_t tx = left >> MapMath::TILE_SHIFT; tx <= (right >> MapMath::TILE_SHIFT); ++tx) {
            const uint8_t* pixels = mCache->get(*mContainer, mZoom,
                                                static_cast<uint32_t>(tx),
                                                static_cast<uint32_t>(ty));
            if (pixels == nullptr) {
                continue; // outside pack coverage (or a failed read)
            }
            // Tile's absolute on-screen rect; origin may be negative.
            // Pre-clip against the invalidated area and hand blitCopy a
            // source-relative blitRect -- the device-proven recipe.
            const int16_t tileAbsX = static_cast<int16_t>(
                absDX + (tx * MapMath::TILE_DIM - viewOriginX));
            const int16_t tileAbsY = static_cast<int16_t>(
                absDY + (ty * MapMath::TILE_DIM - viewOriginY));
            const Rect source(tileAbsX, tileAbsY, MapMath::TILE_DIM, MapMath::TILE_DIM);

            const int16_t ix0 = std::max(source.x, absArea.x);
            const int16_t iy0 = std::max(source.y, absArea.y);
            const int16_t ix1 = std::min<int16_t>(source.x + source.width,
                                                  absArea.x + absArea.width);
            const int16_t iy1 = std::min<int16_t>(source.y + source.height,
                                                  absArea.y + absArea.height);
            if (ix1 <= ix0 || iy1 <= iy0) {
                continue;
            }
            const Rect blitRect(static_cast<int16_t>(ix0 - source.x),
                                static_cast<int16_t>(iy0 - source.y),
                                static_cast<int16_t>(ix1 - ix0),
                                static_cast<int16_t>(iy1 - iy0));
            HAL::lcd().blitCopy(pixels, Bitmap::ABGR2222, source, blitRect, 255, false);
        }
    }
}

void MapTileView::dot(const Rect& clip, int16_t absX, int16_t absY,
                      int16_t size, uint8_t r, uint8_t g, uint8_t b)
{
    const int16_t x0 = std::max<int16_t>(absX, clip.x);
    const int16_t y0 = std::max<int16_t>(absY, clip.y);
    const int16_t x1 = std::min<int16_t>(absX + size, clip.x + clip.width);
    const int16_t y1 = std::min<int16_t>(absY + size, clip.y + clip.height);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    HAL::lcd().fillRect(Rect(x0, y0, static_cast<int16_t>(x1 - x0),
                             static_cast<int16_t>(y1 - y0)),
                        Color::getColorFromRGB(r, g, b), 255);
}

void MapTileView::drawTrace(const Rect& absArea,
                            int64_t viewOriginX, int64_t viewOriginY,
                            int16_t absDX, int16_t absDY) const
{
    const size_t n = mTrace->count();
    if (n < 2) {
        return;
    }
    // Widget-relative culling box, padded a dot's width.
    const int32_t lo = -kTraceDot;
    const int32_t hiX = getWidth() + kTraceDot;
    const int32_t hiY = getHeight() + kTraceDot;

    int32_t prevX = 0;
    int32_t prevY = 0;
    for (size_t i = 0; i < n; ++i) {
        const TraceBuffer::Point& p = mTrace->at(i);
        const int32_t x = static_cast<int32_t>(
            MapMath::rescale(p.x, MapMath::TRACE_ZOOM, mZoom) - viewOriginX);
        const int32_t y = static_cast<int32_t>(
            MapMath::rescale(p.y, MapMath::TRACE_ZOOM, mZoom) - viewOriginY);
        if (i > 0) {
            const bool prevIn = prevX >= lo && prevX < hiX && prevY >= lo && prevY < hiY;
            const bool curIn  = x >= lo && x < hiX && y >= lo && y < hiY;
            if (prevIn || curIn) {
                // Bresenham between the projected points; a dot per step.
                int32_t sx = prevX;
                int32_t sy = prevY;
                const int32_t dx = std::abs(x - sx);
                const int32_t dy = -std::abs(y - sy);
                const int32_t stepX = sx < x ? 1 : -1;
                const int32_t stepY = sy < y ? 1 : -1;
                int32_t err = dx + dy;
                while (true) {
                    if (sx >= lo && sx < hiX && sy >= lo && sy < hiY) {
                        dot(absArea,
                            static_cast<int16_t>(absDX + sx - kTraceDot / 2),
                            static_cast<int16_t>(absDY + sy - kTraceDot / 2),
                            kTraceDot, kTraceR, kTraceG, kTraceB);
                    }
                    if (sx == x && sy == y) {
                        break;
                    }
                    const int32_t e2 = 2 * err;
                    if (e2 >= dy) {
                        err += dy;
                        sx += stepX;
                    }
                    if (e2 <= dx) {
                        err += dx;
                        sy += stepY;
                    }
                }
            }
        }
        prevX = x;
        prevY = y;
    }
}

void MapTileView::drawMarker(const Rect& absArea, int16_t absDX, int16_t absDY) const
{
    // The viewport is centred on the last fix, so the marker sits at the
    // widget centre: black ring, white core when fixed / gray when stale.
    const int16_t cx = static_cast<int16_t>(absDX + getWidth() / 2);
    const int16_t cy = static_cast<int16_t>(absDY + getHeight() / 2);
    dot(absArea, cx - 6, cy - 6, 13, 0, 0, 0);
    if (mFix) {
        dot(absArea, cx - 4, cy - 4, 9, 255, 255, 255);
        dot(absArea, cx - 1, cy - 1, 3, 0, 0, 0);
    } else {
        dot(absArea, cx - 4, cy - 4, 9, 160, 160, 160);
    }
}

} // namespace MapKit
