#ifndef LABCANVASVIEW_HPP
#define LABCANVASVIEW_HPP

#include "SDK/Kernel/Kernel.hpp"

#include <touchgfx/widgets/Widget.hpp>

/**
 * @class LabCanvasView
 * @brief Full-screen widget that blits a caller-owned ABGR2222 buffer, and
 *        times itself doing it.
 *
 * Two jobs in one widget, because they are the same code path:
 *
 *   1. **Show a card.** The visual suite renders into the canvas and this puts
 *      it on the panel -- through `LCD::blitCopy`, the device-proven call, not
 *      `drawPartialBitmap`, which has two confirmed clipping defects on this
 *      target (see MapKit's MapTileView, which learned this the hard way).
 *   2. **Be the blit benchmark.** `blitCopy` is only legal from inside
 *      `draw()`, so the only honest place to time it is here: the real
 *      framebuffer, the real clipping, the real invalidated rect.
 *
 * The benchmark repeats the blit N times inside one draw pass and records the
 * elapsed milliseconds. That is not the same as N frames -- it excludes
 * TouchGFX's own per-frame overhead -- and it is the right subject, because
 * the question the canvas architecture asks is "what does one full-screen blit
 * cost", not "what does a frame cost".
 *
 * `draw()` is const, so the timing state is mutable. That is a compromise the
 * widget interface forces, and it is confined to these three fields.
 */
class LabCanvasView : public touchgfx::Widget
{
public:
    void setKernel(const SDK::Kernel *kernel) { mKernel = kernel; }

    /// Point the widget at the pixels to show. `w` x `h` ABGR2222.
    void setSource(const uint8_t *pixels, int16_t w, int16_t h);

    /// Blit `source` `repeats` times on the next draw and record the cost.
    /// `mosaic` blits it as four clipped quadrants instead of one full-screen
    /// rect -- which is what the shipped raster path does with 256 px tiles,
    /// and therefore the baseline the canvas has to beat.
    void startBlitBench(const uint8_t *source, int16_t srcW, int16_t srcH,
                        uint32_t repeats, bool mosaic);

    /// Collect a finished measurement. False until a benched draw has run.
    bool takeResult(uint32_t &iterations, uint32_t &elapsedMs, int32_t &bytesPerBlit);

    virtual void draw(const touchgfx::Rect &area) const override;
    virtual touchgfx::Rect getSolidRect() const override;

private:
    void blitOnce(const touchgfx::Rect &absArea, const uint8_t *src,
                  int16_t srcW, int16_t srcH, bool mosaic) const;

    const SDK::Kernel *mKernel  = nullptr;
    const uint8_t     *mPixels  = nullptr;
    int16_t            mSrcW    = 0;
    int16_t            mSrcH    = 0;

    const uint8_t     *mBenchSrc   = nullptr;
    int16_t            mBenchW     = 0;
    int16_t            mBenchH     = 0;
    uint32_t           mRepeats    = 0;
    bool               mMosaic     = false;

    mutable uint32_t   mElapsedMs  = 0;
    mutable uint32_t   mDone       = 0;
    mutable bool       mHasResult  = false;
};

#endif // LABCANVASVIEW_HPP
