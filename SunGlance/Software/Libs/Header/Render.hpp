/**
 ******************************************************************************
 * @file    Render.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What the glance draws, as a pure function of what it knows.
 ******************************************************************************
 *
 * Two rows and a caption. Each row is an icon and a time -- a sun rising over a
 * horizon, or setting under one -- and they are in the order the two events
 * will happen, so the top row is always the next thing and the bottom row is
 * always the one after it. Nothing on the screen is in the past.
 *
 * That layout is why there are no words on the rows. "sunrise 04:50" spends a
 * third of the panel restating what the picture already says, and the pair of
 * times is the thing worth reading: what is actually being asked at seven in
 * the morning is not "when is sunrise" but "how much daylight is there".
 *
 * Everything here is decided without touching the SDK, the clock or the screen.
 * A glance is looked at half awake for three seconds, so the wording is the
 * product, and wording that can only be reviewed by scrolling a carousel on a
 * watch does not get reviewed.
 *
 * ## Why the lines are built into buffers instead of set directly
 *
 * `SDK::Glance::ControlText::setText()` **silently ignores** a string of
 * `GLANCE_TEXT_SIZE` bytes or more -- it checks the length and returns without
 * copying. So an over-long caption does not truncate, it leaves whatever was
 * there before, and the failure is invisible from the code. Building the lines
 * here means their lengths are asserted in a host test rather than discovered
 * by a caption that never changes.
 *
 * ## Why the icons are optional
 *
 * The kernel says how many controls the glance may have, and this screen wants
 * five. Every SDK example asks for three. Rather than decline a glance on a
 * watch that offers four, the renderer will put the words back: `withIcons`
 * false produces "rise 04:50" and "set 19:17", which fits in three controls and
 * says the same thing less prettily.
 *
 ******************************************************************************
 */

#ifndef RENDER_HPP
#define RENDER_HPP

#include <cstddef>
#include <cstdint>

#include "Schedule.hpp"

namespace Sun
{

/// Bytes per line, including the terminator. Must equal the SDK's
/// GLANCE_TEXT_SIZE, which Service.cpp static_asserts -- this header stays free
/// of SDK includes so the wording can be tested on its own.
constexpr size_t kLineBytes = 32;

/// A local clock reading of some instant, or `valid == false` for "there isn't
/// one" -- which is a real answer here: a polar day has no sunrise to show.
struct Clock
{
    int  hour   = 0;
    int  minute = 0;
    bool valid  = false;
};

/// The reasons there is no time to show. Each gets its own words, because each
/// needs something different done about it.
enum class Trouble : uint8_t {
    None,
    NoPosition,   ///< Nobody has said where this watch is.
    BadConfig,    ///< Somebody has, and it cannot be read.
    NoClock,      ///< The watch does not know what time it is.
};

/// Everything the glance knows, at the moment it is asked to draw.
struct View
{
    Trouble   trouble     = Trouble::None;
    /// Which event the first row is. The second row is the other kind.
    EventKind kind        = EventKind::Rise;
    Clock     first;                    ///< Local reading of the next event.
    Clock     second;                   ///< And of the one after it, if there is one.
    int64_t   secondsAway = -1;         ///< Until `first`; -1 when there is none.
    bool      nextDay     = false;      ///< `first` belongs to tomorrow.
    bool      zoneSuspect = false;      ///< Position and time zone disagree.
};

/// The glance, drawn.
struct Lines
{
    /// True when the two time rows are in use. False means the screen is one
    /// message instead -- a polar day, or a reason there is no time -- and
    /// `message` is what to show.
    bool      rows      = false;
    /// Which icon belongs on the first row; the second row takes the other.
    EventKind firstKind = EventKind::Rise;

    char first[kLineBytes]   = { 0 };
    /// Empty when there is no second event to show, which happens on the last
    /// day before the midnight sun.
    char second[kLineBytes]  = { 0 };
    char message[kLineBytes] = { 0 };
    char sub[kLineBytes]     = { 0 };

    /// True when the caption is a caveat rather than a convenience, so the
    /// service can colour it as one.
    bool caution = false;
};

/// A rectangle on the glance, in the kernel's coordinates.
struct Box
{
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
};

/// Which way round the two events are drawn.
enum class Arrangement : uint8_t {
    /// Side by side: next on the left, the one after it on the right, caption
    /// underneath. The panel is wide and short, so this is the shape that fits
    /// it -- it turns the binding constraint from height, which is where the
    /// clipping came from, into width, where there is room.
    SideBySide,
    /// Stacked: next above the one after it. Used when the panel is too narrow
    /// to put two times beside each other without them colliding, which is a
    /// worse failure than a small font.
    Stacked,
};

/**
 * @brief Where everything goes, for the glance area the kernel actually gave.
 *
 * Hard-coded positions were the first version's mistake and cost a clipped row
 * on hardware: the numbers were lifted from SleepLab, which has *one* line at
 * font 30 and gives it a 36-pixel band, and two of those plus a caption do not
 * fit in the same panel. What is copied here instead is the *ratio* that app
 * demonstrates on a real watch -- a line needs about 1.2 times its font size,
 * or the bottom of the digits is cut off -- and everything else is arithmetic
 * over the area the kernel reports.
 *
 * Four shapes are considered, in this order of preference:
 *
 *   1. side by side, with icons
 *   2. stacked, with icons
 *   3. side by side, words instead of icons
 *   4. stacked, words instead of icons
 *
 * Icons before font size, because they are what lets the rows carry no words at
 * all; between two shapes that both keep them, the one with the larger font
 * wins, and side by side breaks the tie. If none of the four fit, `fits` is
 * false and the caller declines the glance rather than drawing something cut
 * off.
 *
 * Every text box is exactly its line height and centred in its band, rather
 * than being handed the whole band: `GlanceText_t` has no vertical alignment,
 * so a box taller than the line leaves the kernel to decide where in it the
 * glyphs sit, and that decision is what nobody here can see.
 */
struct Layout
{
    Arrangement arrangement = Arrangement::SideBySide;
    /// False when no arrangement fits at any font size.
    bool        fits        = false;
    /// False when the icons had to go, in which case the times carry words.
    bool        icons       = false;
    int16_t     rowFontPx   = 18;

    Box iconFirst;
    Box iconSecond;
    Box textFirst;
    Box textSecond;
    /// Where a single line goes when there are no times to show -- a polar day,
    /// or a reason there is no position.
    Box message;
    Box sub;

    /// True when the row texts should be centred in their boxes rather than run
    /// from the left of them. Left-aligned is for a time sitting against its
    /// icon; centred is for one standing on its own.
    bool textCentred = true;
};

/**
 * @brief Work out that layout.
 *
 * @param wantIcons     Whether the caller has controls to spare for them.
 * @param iconW,iconH   Icons are a fixed size and cannot shrink with the font.
 */
Layout layoutFor(int16_t width, int16_t height, bool wantIcons,
                 int16_t iconW, int16_t iconH);

/// What a line of this font needs vertically, in pixels. The 1.2 is SleepLab's
/// ratio, measured on the watch this runs on rather than derived from the font.
int16_t lineHeightFor(int16_t fontPx);

/// What "04:50" needs horizontally at this font size. Poppins' digits run about
/// 0.62 em and its colon rather less; this is that with room to be wrong in,
/// because a time too wide for its box is clipped sideways instead of downwards
/// and nothing here can see either.
int16_t timeWidthFor(int16_t fontPx);

/**
 * @brief Turn what is known into what is shown.
 *
 * @param withIcons False when the kernel would not give this glance enough
 *                  controls for the icons, in which case the rows carry the
 *                  words instead.
 */
Lines render(const View &view, bool withIcons);

} // namespace Sun

#endif // RENDER_HPP
