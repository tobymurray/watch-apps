/**
 ******************************************************************************
 * @file    RoundPanel.hpp
 * @date    20-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The panel is a circle. Widget boxes have to fit inside it.
 ******************************************************************************
 *
 * Pure C++17. No TouchGFX header, no SDK header -- so the arithmetic that
 * decides where every widget goes is reachable from the host tests, which is
 * the whole point of it being here rather than inline in the view.
 *
 * ---------------------------------------------------------------------------
 * Why this exists
 *
 * `UNAview_LS012` is a round 240x240 memory LCD. The framebuffer is square and
 * the glass is not: at the vertical centre all 240 columns are visible, and at
 * the top and bottom rows none of them are. A widget positioned against the
 * framebuffer's bounding box is therefore correct in the middle of the screen
 * and wrong at both ends, and the failure is silent -- the pixels are written,
 * they simply never appear.
 *
 * The layout it replaced carried the right idea and the wrong number: a fixed
 * 26 px inset, with a comment saying "the top and bottom lines are the ones
 * that clip". They are. At y = 30 the chord is 159 px, so a 188 px box inset by
 * 26 lost 14.6 px off each end, and the caption at y = 188..208 lost 17.4 --
 * the caption being the one widget whose entire job is to say the strip above
 * it is not a hypnogram.
 *
 * A constant cannot be right for every row, so nothing here is a constant.
 *
 * ---------------------------------------------------------------------------
 * The arithmetic
 *
 * For a circle of radius r centred at (r, r), the visible half-width at row y
 * is sqrt(r^2 - (y - r)^2). A box spanning rows [y, y + h) is bounded by its
 * *worst* row, which is whichever of its first and last rows is further from
 * the centre -- never the average, and never the row you happened to think of
 * as the widget's position.
 *
 ******************************************************************************
 */

#ifndef GUI_COMMON_ROUNDPANEL_HPP
#define GUI_COMMON_ROUNDPANEL_HPP

#include <cmath>
#include <cstdint>

namespace RoundPanel
{

/// Panel size in pixels, square framebuffer, round glass.
constexpr int16_t kSize   = 240;
constexpr int16_t kRadius = kSize / 2;

/// Visible half-width at row @p y, in pixels. Zero outside the glass.
inline float halfWidthAt(int16_t y)
{
    const float dy = static_cast<float>(y) - static_cast<float>(kRadius);
    const float rr = static_cast<float>(kRadius) * static_cast<float>(kRadius);
    const float d2 = rr - dy * dy;
    return d2 <= 0.0f ? 0.0f : std::sqrt(d2);
}

/// The narrowest visible half-width across the rows a box of height @p h
/// starting at @p y occupies. This is the number that binds.
inline float halfWidthOfBox(int16_t y, int16_t h)
{
    const int16_t last = static_cast<int16_t>(y + (h > 0 ? h - 1 : 0));
    const float a = halfWidthAt(y);
    const float b = halfWidthAt(last);
    return a < b ? a : b;
}

/// Left edge of the widest box of height @p h that fits at row @p y, rounded
/// inward so the result is safe rather than exact.
inline int16_t insetFor(int16_t y, int16_t h)
{
    const float hw = halfWidthOfBox(y, h);
    return static_cast<int16_t>(std::ceil(static_cast<float>(kRadius) - hw));
}

/// Width of that box. Symmetric about the centre, so a centred string stays
/// centred on the glass and not merely on the framebuffer.
inline int16_t widthFor(int16_t y, int16_t h)
{
    return static_cast<int16_t>(kSize - 2 * insetFor(y, h));
}

/// Whether a box is entirely visible. What the host test asserts, for every
/// rectangle the view actually uses.
inline bool fits(int16_t x, int16_t y, int16_t w, int16_t h)
{
    const float hw = halfWidthOfBox(y, h);
    const float lo = static_cast<float>(kRadius) - hw;
    const float hi = static_cast<float>(kRadius) + hw;
    return static_cast<float>(x) >= lo && static_cast<float>(x + w) <= hi;
}

} // namespace RoundPanel

#endif // GUI_COMMON_ROUNDPANEL_HPP
