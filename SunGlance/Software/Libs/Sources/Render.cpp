/**
 ******************************************************************************
 * @file    Render.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   What the glance draws.
 ******************************************************************************
 */

#include "Render.hpp"

#include <cstdio>

namespace Sun
{

namespace
{

/// "in 1h12m", "in 47m", or "now" inside the last minute.
///
/// No seconds, ever. A glance is read at arm's length and a counter that ticks
/// is a thing to watch rather than a thing to notice, which is the opposite of
/// what this screen is for.
void countdown(int64_t seconds, char *out, size_t outSize)
{
    if (seconds < 60) {
        snprintf(out, outSize, "now");
        return;
    }

    const long minutes = static_cast<long>(seconds / 60);
    if (minutes < 60) {
        snprintf(out, outSize, "in %ldm", minutes);
        return;
    }

    snprintf(out, outSize, "in %ldh%02ldm", minutes / 60, minutes % 60);
}

/// One row: a time, with the word in front of it only when there is no icon to
/// say which event it is.
void rowText(const Clock &clock, EventKind kind, bool withIcons,
             char *out, size_t outSize)
{
    if (!clock.valid) {
        out[0] = '\0';
        return;
    }

    if (withIcons) {
        snprintf(out, outSize, "%02d:%02d", clock.hour, clock.minute);
        return;
    }

    snprintf(out, outSize, "%s %02d:%02d",
             (kind == EventKind::Rise) ? "rise" : "set", clock.hour, clock.minute);
}

EventKind opposite(EventKind kind)
{
    return (kind == EventKind::Rise) ? EventKind::Set : EventKind::Rise;
}

Lines message(const char *headline, const char *caption, bool caution)
{
    Lines lines;
    lines.rows    = false;
    lines.caution = caution;
    snprintf(lines.message, kLineBytes, "%s", headline);
    snprintf(lines.sub, kLineBytes, "%s", caption);
    return lines;
}

/// The caption's font never changes: it is the smallest thing on the screen and
/// shrinking it further would make it the least readable thing on a screen read
/// half awake.
constexpr int16_t kSubFontPx = 18;

/// Largest first, because the loop takes the first that fits.
constexpr int16_t kRowFonts[] = { 30, 25, 20, 18 };

/// A pixel or two of the panel left unused at the bottom. Cheap insurance: the
/// kernel reports the area, and nothing here can tell whether that number
/// includes a border the carousel draws over.
constexpr int16_t kBottomGuard = 2;

int16_t clampTo(int16_t value, int16_t low, int16_t high)
{
    if (value < low)  { return low;  }
    if (value > high) { return high; }
    return value;
}

} // namespace

int16_t lineHeightFor(int16_t fontPx)
{
    // 6/5 rather than 1.2 so this is integer arithmetic all the way down: these
    // numbers become pixel positions, and a rounding difference between the
    // test and the watch would be a rounding difference nobody could see.
    return static_cast<int16_t>((fontPx * 6) / 5);
}

Bands bandsFor(int16_t height, bool wantIcons, int16_t iconHeight)
{
    Bands out;

    if (height <= 0) {
        return out;
    }

    // The caption is placed first and gets what it needs, because it is the
    // line that says why there is no time at all. A screen that cannot show it
    // is worse than one whose digits are a size smaller.
    out.subH = clampTo(lineHeightFor(kSubFontPx), 12, static_cast<int16_t>(height / 3));

    const int16_t rowsH = static_cast<int16_t>(height - out.subH - kBottomGuard);
    out.rowH  = static_cast<int16_t>(rowsH > 0 ? rowsH / 2 : 0);
    out.rowAY = 0;
    out.rowBY = out.rowH;
    // Any odd pixel left over joins the guard at the bottom rather than being
    // handed to a row that would then disagree with the row above it.
    out.subY  = static_cast<int16_t>(out.rowH * 2);

    out.rowFontPx = kRowFonts[sizeof kRowFonts / sizeof kRowFonts[0] - 1];
    for (const int16_t px : kRowFonts) {
        if (lineHeightFor(px) <= out.rowH) {
            out.rowFontPx = px;
            break;
        }
    }

    // An icon is a fixed size and cannot shrink with the font, so a row too
    // short for one loses it rather than overlapping the row beneath.
    out.icons = wantIcons && (out.rowH >= iconHeight + 2);

    // Below this there is no arrangement of two lines and a caption that is not
    // cut off somewhere, and the smallest font is already in use. Saying so is
    // the honest answer; picking a size that will be clipped is what the first
    // version did.
    const int16_t smallest = kRowFonts[sizeof kRowFonts / sizeof kRowFonts[0] - 1];
    out.fits = (out.rowH >= lineHeightFor(smallest))
               && (out.subH >= lineHeightFor(kSubFontPx));

    return out;
}

Lines render(const View &view, bool withIcons)
{
    switch (view.trouble) {
        case Trouble::NoPosition:
            // Not "set lat/lon in input.json", which is the useful sentence and
            // does not fit: 24 characters at this font is wider than the panel,
            // and an over-long caption is silently dropped rather than
            // truncated. The glance says what is wrong; the app's card in Kira
            // and its README say what to do about it.
            //
            // The dashes are the same sentinel SleepLab uses for a figure it has
            // not earned. A blank would read as "loading" and a zero as a time.
            return message("--", "no position set", true);
        case Trouble::BadConfig:
            return message("--", "input.json rejected", true);
        case Trouble::NoClock:
            return message("--", "clock not set", true);
        case Trouble::None:
            break;
    }

    if (view.kind == EventKind::MidnightSun) {
        return message("no sunset", "the sun stays up", false);
    }

    if (view.kind == EventKind::PolarNight) {
        return message("no sunrise", "the sun stays down", false);
    }

    Lines lines;
    lines.rows      = true;
    lines.firstKind = view.kind;

    rowText(view.first, view.kind, withIcons, lines.first, kLineBytes);
    rowText(view.second, opposite(view.kind), withIcons, lines.second, kLineBytes);

    char away[16] = { 0 };
    countdown(view.secondsAway, away, sizeof away);

    if (view.zoneSuspect) {
        // The caveat outranks the countdown. Both times on the screen are still
        // true of the configured position, and the clock they are being read
        // against is somewhere else, so the one thing worth the caption's line
        // is which of those two they belong to.
        snprintf(lines.sub, kLineBytes, "times are for home");
        lines.caution = true;
        return lines;
    }

    if (view.nextDay) {
        // Both rows are tomorrow's, and "04:52" on a Tuesday evening reads as
        // this morning, which has been and gone.
        snprintf(lines.sub, kLineBytes, "tomorrow, %s", away);
        return lines;
    }

    snprintf(lines.sub, kLineBytes, "%s", away);
    return lines;
}

} // namespace Sun
