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
