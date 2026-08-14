/**
 ******************************************************************************
 * @file    MapTileView.hpp
 * @brief   Full-screen map widget: rawtiles basemap mosaic + GPS trace.
 *
 * One custom Widget paints everything in a single draw() pass, in order:
 * background fill, up to a 2x2 mosaic of 256 px tiles via LCD::blitCopy
 * (the device-proven path -- drawPartialBitmap has two confirmed clipping
 * defects on this target), the breadcrumb polyline (Bresenham steps of small
 * fillRects: no canvas buffer, no painter, correct over the blit by
 * construction), and the position marker. North-up; the viewport is
 * centred on the last fix.
 *
 * All data is borrowed from the app's MapSession (pack Container, TileCache,
 * TraceBuffer) because TouchGFX destroys and re-creates screens on every
 * transition -- nothing heavy may live in the view.
 ******************************************************************************
 */

#ifndef MAPKIT_MAPTILEVIEW_HPP
#define MAPKIT_MAPTILEVIEW_HPP

#include <MapKit/MapMath.hpp>
#include <MapKit/TileCache.hpp>
#include <MapKit/TraceBuffer.hpp>
#include <SDK/RawTiles/Container.hpp>

#include <touchgfx/widgets/Widget.hpp>

namespace MapKit
{

class MapTileView : public touchgfx::Widget
{
public:
    void setSources(const SDK::RawTiles::Container* container,
                    TileCache* cache,
                    const TraceBuffer* trace)
    {
        mContainer = container;
        mCache     = cache;
        mTrace     = trace;
    }

    /// Centre (world px at MapMath::TRACE_ZOOM), display zoom and fix state
    /// for the next draw.
    void setViewport(int64_t centerX, int64_t centerY, uint8_t zoom, bool fix)
    {
        mCenterX = centerX;
        mCenterY = centerY;
        mZoom    = zoom;
        mFix     = fix;
        invalidate();
    }

    /// Draw the trace and marker but no basemap. Used for every state in which
    /// there is no pack to draw from -- the activity does not depend on the
    /// map, so the breadcrumb must survive a missing, unverified or corrupt
    /// pack. @p zoom still sets the trace's scale.
    void setTraceOnlyViewport(int64_t centerX, int64_t centerY, uint8_t zoom, bool fix)
    {
        mSuppressTiles = true;
        setViewport(centerX, centerY, zoom, fix);
    }

    void setViewportWithTiles(int64_t centerX, int64_t centerY, uint8_t zoom, bool fix)
    {
        mSuppressTiles = false;
        setViewport(centerX, centerY, zoom, fix);
    }

    virtual touchgfx::Rect getSolidRect() const
    {
        return touchgfx::Rect(0, 0, getWidth(), getHeight());
    }

    virtual void draw(const touchgfx::Rect& area) const;

private:
    void drawTiles(const touchgfx::Rect& absArea,
                   int64_t viewOriginX, int64_t viewOriginY,
                   int16_t absDX, int16_t absDY) const;
    void drawTrace(const touchgfx::Rect& absArea,
                   int64_t viewOriginX, int64_t viewOriginY,
                   int16_t absDX, int16_t absDY) const;
    void drawMarker(const touchgfx::Rect& absArea,
                    int16_t absDX, int16_t absDY) const;
    static void dot(const touchgfx::Rect& clip, int16_t absX, int16_t absY,
                    int16_t size, uint8_t r, uint8_t g, uint8_t b);

    const SDK::RawTiles::Container* mContainer = nullptr;
    TileCache*                      mCache     = nullptr;
    const TraceBuffer*              mTrace     = nullptr;
    int64_t                         mCenterX   = 0;
    int64_t                         mCenterY   = 0;
    uint8_t                         mZoom      = MapMath::TRACE_ZOOM;
    bool                            mFix       = false;
    bool                            mSuppressTiles = true;
};

} // namespace MapKit

#endif // MAPKIT_MAPTILEVIEW_HPP
