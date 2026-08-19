#include <gui/containers/LabCanvasView.hpp>

#include <touchgfx/Color.hpp>
#include <touchgfx/hal/HAL.hpp>

using namespace touchgfx;

void LabCanvasView::setSource(const uint8_t *pixels, int16_t w, int16_t h)
{
    mPixels = pixels;
    mSrcW   = w;
    mSrcH   = h;
    invalidate();
}

void LabCanvasView::startBlitBench(const uint8_t *source, int16_t srcW, int16_t srcH,
                                   uint32_t repeats, bool mosaic)
{
    mBenchSrc  = source;
    mBenchW    = srcW;
    mBenchH    = srcH;
    mRepeats   = repeats;
    mMosaic    = mosaic;
    mHasResult = false;
    invalidate();
}

bool LabCanvasView::takeResult(uint32_t &iterations, uint32_t &elapsedMs, int32_t &bytesPerBlit)
{
    if (!mHasResult) {
        return false;
    }
    iterations   = mDone;
    elapsedMs    = mElapsedMs;
    bytesPerBlit = static_cast<int32_t>(mMosaic
                       ? static_cast<int32_t>(getWidth()) * getHeight()   // one screen's worth
                       : static_cast<int32_t>(mBenchW) * mBenchH);
    mHasResult   = false;
    mBenchSrc    = nullptr;
    mRepeats     = 0;
    return true;
}

Rect LabCanvasView::getSolidRect() const
{
    // The widget always covers itself: it either blits a full-screen canvas or
    // fills first. Saying so lets TouchGFX skip drawing anything underneath.
    return Rect(0, 0, getWidth(), getHeight());
}

void LabCanvasView::blitOnce(const Rect &absArea, const uint8_t *src,
                             int16_t srcW, int16_t srcH, bool mosaic) const
{
    if (!mosaic) {
        const Rect source(absArea.x, absArea.y, srcW, srcH);
        const Rect blitRect(0, 0,
                            static_cast<int16_t>(absArea.width < srcW ? absArea.width : srcW),
                            static_cast<int16_t>(absArea.height < srcH ? absArea.height : srcH));
        HAL::lcd().blitCopy(src, Bitmap::ABGR2222, source, blitRect, 255, false);
        return;
    }

    // Four quadrants of the source tile, laid out to cover the same screen
    // area one full-screen blit covers -- the shipped raster path's 2x2 mosaic
    // arithmetic, minus the tile cache.
    const int16_t half = static_cast<int16_t>(absArea.width / 2);
    for (int q = 0; q < 4; ++q) {
        const int16_t dx = static_cast<int16_t>(absArea.x + (q % 2) * half);
        const int16_t dy = static_cast<int16_t>(absArea.y + (q / 2) * half);
        const Rect source(dx, dy, srcW, srcH);
        const Rect blitRect(0, 0, half, half);
        HAL::lcd().blitCopy(src, Bitmap::ABGR2222, source, blitRect, 255, false);
    }
}

void LabCanvasView::draw(const Rect &area) const
{
    Rect absArea = area;
    translateRectToAbsolute(absArea);

    if (mBenchSrc != nullptr && mRepeats > 0 && mKernel != nullptr) {
        const uint32_t t0 = mKernel->sys.getTimeMs();
        for (uint32_t i = 0; i < mRepeats; ++i) {
            blitOnce(absArea, mBenchSrc, mBenchW, mBenchH, mMosaic);
        }
        mElapsedMs = mKernel->sys.getTimeMs() - t0;
        mDone      = mRepeats;
        mHasResult = true;
        return;
    }

    if (mPixels == nullptr) {
        HAL::lcd().fillRect(absArea, Color::getColorFromRGB(0, 0, 0), 255);
        return;
    }
    blitOnce(absArea, mPixels, mSrcW, mSrcH, false);
}
