#include <gui/containers/SleepStrip.hpp>

#include <touchgfx/hal/HAL.hpp>

#include "Engine/RestfulnessBand.hpp"
#include "Engine/SleepWakeScorer.hpp"

namespace {

using touchgfx::Color;

/// Awake. A warm accent against the cool sleep tones, so an awakening reads as
/// an interruption of the night rather than as another level of it -- which is
/// the whole reason the strip carries the sleep/wake verdict as well as the
/// band.
const touchgfx::colortype kWakeColor    = Color::getColorFromRGB(255, 170, 0);
/// No data: outside the night, or every epoch in the bucket unscorable.
/// Deliberately dim and neutral -- an absence must not compete with a finding.
const touchgfx::colortype kNoDataColor  = Color::getColorFromRGB(85, 85, 85);

/// The three restfulness levels, dimmest to brightest. Every channel value is
/// one of the four the ABGR2222 framebuffer can actually hold.
const touchgfx::colortype kBandColor[4] = {
    Color::getColorFromRGB(85, 85, 85),      // Unknown
    Color::getColorFromRGB(0, 85, 85),       // Restless
    Color::getColorFromRGB(0, 170, 170),     // Settled
    Color::getColorFromRGB(0, 255, 255),     // Deepest
};

} // namespace

SleepStrip::SleepStrip()
{
    setWidth(kWidth);
    setHeight(kHeight);
    for (uint16_t i = 0; i < CustomMessage::kStripBuckets; i++) {
        mBuckets[i] = CustomMessage::Strip::pack(
            CustomMessage::Strip::kVerdictNone, 0);
    }
}

void SleepStrip::setStrip(const uint8_t *buckets, uint16_t used)
{
    if (buckets == nullptr) {
        mUsed = 0;
        return;
    }
    mUsed = (used <= CustomMessage::kStripBuckets) ? used
                                                   : CustomMessage::kStripBuckets;
    for (uint16_t i = 0; i < CustomMessage::kStripBuckets; i++) {
        mBuckets[i] = buckets[i];
    }
    invalidate();
}

int16_t SleepStrip::barHeight(uint8_t band)
{
    switch (band) {
        case static_cast<uint8_t>(Engine::Restfulness::Deepest):  return kHeight;
        case static_cast<uint8_t>(Engine::Restfulness::Settled):  return kHeight * 2 / 3;
        case static_cast<uint8_t>(Engine::Restfulness::Restless): return kHeight / 3;
        default:                                                  return kHeight / 4;
    }
}

touchgfx::Rect SleepStrip::getSolidRect() const
{
    // Nothing here is transparent, but the bars are bottom-aligned and shorter
    // than the widget, so only the strip's own baseline row is guaranteed
    // solid. Claiming the whole rect would leave the background above a short
    // bar undrawn.
    return touchgfx::Rect(0, kHeight - 1, kWidth, 1);
}

void SleepStrip::draw(const touchgfx::Rect &invalidatedArea) const
{
    for (uint16_t b = 0; b < CustomMessage::kStripBuckets; b++) {
        const uint8_t packed  = mBuckets[b];
        const uint8_t verdict = CustomMessage::Strip::verdict(packed);
        const uint8_t band    = CustomMessage::Strip::band(packed);

        touchgfx::colortype colour;
        int16_t             h;

        if (b >= mUsed || verdict == CustomMessage::Strip::kVerdictNone) {
            colour = kNoDataColor;
            h      = 2;   // a baseline, so the strip's extent stays visible
        } else if (verdict == static_cast<uint8_t>(Engine::Verdict::Wake)) {
            colour = kWakeColor;
            h      = kHeight;
        } else if (verdict == static_cast<uint8_t>(Engine::Verdict::Unscorable)) {
            colour = kNoDataColor;
            h      = 2;
        } else {
            colour = kBandColor[band & 0x03];
            h      = barHeight(band);
        }

        // Bottom-aligned, so the strip reads as a bar chart of how settled the
        // night was rather than as a band floating in the middle of nothing.
        touchgfx::Rect r(static_cast<int16_t>(b * kBucketW),
                         static_cast<int16_t>(kHeight - h),
                         kBucketW, h);
        r &= invalidatedArea;
        if (r.isEmpty()) {
            continue;
        }
        translateRectToAbsolute(r);
        touchgfx::HAL::lcd().fillRect(r, colour);
    }
}
