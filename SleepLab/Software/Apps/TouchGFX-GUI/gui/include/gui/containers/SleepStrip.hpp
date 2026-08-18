#ifndef SLEEPSTRIP_HPP
#define SLEEPSTRIP_HPP

#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/Color.hpp>

#include "Commands.hpp"

/**
 * @class SleepStrip
 * @brief The night, drawn as one bar per bucket.
 *
 * **This is not a hypnogram.** It looks like one, which is precisely why the
 * caption beneath it says what it is and why the colours below are chosen so
 * that awake reads as an interruption rather than as another stage. See
 * `Engine::RestfulnessBand` -- the band is an ordinal index over movement and
 * heart rate, and this device cannot produce sleep stages at all.
 *
 * A custom Widget rather than 100 `Box`es: a hundred drawables is a hundred
 * entries in the screen's list, each with its own invalidation bookkeeping, to
 * draw a hundred two-pixel rectangles.
 *
 * The palette is four levels per channel -- the framebuffer is 8 bpp ABGR2222 --
 * so every colour here is built from 0, 85, 170 or 255. Anything between them
 * quantises to one of those anyway, and picking them deliberately is the
 * difference between a chosen palette and a banded one.
 */
class SleepStrip : public touchgfx::Widget
{
public:
    /// Pixels per bucket. kStripBuckets * this is the widget's width.
    static constexpr int16_t kBucketW = CustomMessage::kStripPixelsPerBucket;
    static constexpr int16_t kWidth   = CustomMessage::kStripBuckets * kBucketW;
    static constexpr int16_t kHeight  = 26;

    SleepStrip();

    /// Take a new night. Copies, because the message goes back to the kernel's
    /// pool the moment the handler returns.
    void setStrip(const uint8_t *buckets, uint16_t used);

    void draw(const touchgfx::Rect &invalidatedArea) const override;
    touchgfx::Rect getSolidRect() const override;

private:
    /// Bar height for a restfulness level, as a fraction of kHeight.
    ///
    /// Height as well as colour, so the strip is still readable to someone who
    /// cannot separate the tones -- on a 4-level-per-channel panel the two
    /// middle teals are genuinely close.
    static int16_t barHeight(uint8_t band);

    uint8_t  mBuckets[CustomMessage::kStripBuckets] = {};
    uint16_t mUsed = 0;
};

#endif // SLEEPSTRIP_HPP
