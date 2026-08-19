/**
 ******************************************************************************
 * @file    Render.hpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The three lines the glance shows, as a pure function of what it knows.
 ******************************************************************************
 *
 * Everything the user actually sees is decided here, and nothing here touches
 * the SDK, the clock or the screen. That split is the point: a glance is looked
 * at half awake for three seconds, so the wording is the product, and wording
 * that can only be reviewed by scrolling a carousel on a watch does not get
 * reviewed.
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
 * ## Why a caution flag rather than a colour
 *
 * The renderer decides *that* a line is a caveat; the service decides what
 * amber looks like. Keeping the palette out of here is what lets the wording
 * be tested without linking the glance headers.
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
    EventKind kind        = EventKind::Rise;
    Clock     when;                     ///< Local reading of the event.
    Clock     other;                    ///< The paired event, if there is one.
    int64_t   secondsAway = -1;         ///< Until `when`; -1 when there is none.
    bool      nextDay     = false;      ///< `when` belongs to tomorrow.
    bool      zoneSuspect = false;      ///< Position and time zone disagree.
};

/// The glance, drawn.
struct Lines
{
    char title[kLineBytes] = { 0 };
    char value[kLineBytes] = { 0 };
    char sub[kLineBytes]   = { 0 };
    /// True when the caption is a caveat rather than a convenience, so the
    /// service can colour it as one.
    bool caution = false;
};

Lines render(const View &view);

} // namespace Sun

#endif // RENDER_HPP
