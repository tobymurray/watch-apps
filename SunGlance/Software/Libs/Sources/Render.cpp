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

/// Largest first, because the search takes the first that fits.
constexpr int16_t kRowFonts[]  = { 30, 25, 20, 18 };
constexpr size_t  kRowFontCount = sizeof kRowFonts / sizeof kRowFonts[0];

/// Between an icon and the time it labels.
constexpr int16_t kIconGap = 5;
/// Between the two pairs, side by side. Wider than the icon gap on purpose: it
/// is what makes them read as two things rather than four.
constexpr int16_t kPairGap = 10;

/// Nothing is painted over the right edge, but a couple of pixels of margin
/// keeps a time from ending flush against the bezel. The left inset is
/// `kSafeLeftInset` in the header, where the tests can hold the layout to it.
constexpr int16_t kSafeRight = 8;

/// A pixel or two of the panel left unused at the bottom. Cheap insurance: the
/// kernel reports the area, and nothing here can tell whether that number
/// includes a border the carousel draws over.
constexpr int16_t kBottomGuard = 2;

/// Centre a thing of size @p size in a band starting at @p start of size @p of.
int16_t centreIn(int16_t start, int16_t of, int16_t size)
{
    const int16_t offset = static_cast<int16_t>((of - size) / 2);
    return static_cast<int16_t>(start + (offset > 0 ? offset : 0));
}

/// Does one arrangement fit at one font size, given icons or not?
bool shapeFits(Arrangement arrangement, bool icons, int16_t fontPx,
               int16_t width, int16_t bandH, int16_t iconW, int16_t iconH)
{
    if (lineHeightFor(fontPx) > bandH) {
        return false;
    }
    if (icons && bandH < iconH + 2) {
        return false;
    }

    const int16_t groupW = static_cast<int16_t>(timeWidthFor(fontPx)
                                                + (icons ? iconW + kIconGap : 0));

    if (arrangement == Arrangement::SideBySide) {
        return (2 * groupW + kPairGap) <= width;
    }
    return groupW <= width;
}

} // namespace

int16_t lineHeightFor(int16_t fontPx)
{
    // 6/5 rather than 1.2 so this is integer arithmetic all the way down: these
    // numbers become pixel positions, and a rounding difference between the
    // test and the watch would be a difference nobody could see.
    return static_cast<int16_t>((fontPx * 6) / 5);
}

int16_t timeWidthFor(int16_t fontPx)
{
    // 2.8 em for "04:50": four digits at about 0.62 and a colon at about 0.35
    // is 2.83, so this is the glyphs and very little else. It was 2.9 until the
    // panel turned out to be 240 wide with a scroll bar over part of it, and
    // every pixel of slack in here is a pixel the pair cannot use.
    return static_cast<int16_t>((fontPx * 28) / 10);
}

Layout layoutFor(int16_t width, int16_t height, bool wantIcons,
                 int16_t iconW, int16_t iconH)
{
    Layout out;

    if (width <= 0 || height <= 0) {
        return out;
    }

    // Everything below is laid out in the part of the panel that is actually
    // free to draw in, and only the caption spans the full reported width --
    // it is centred text with room to spare either side, so the scroll bar
    // passes over its margin rather than over a glyph.
    const int16_t usableW = static_cast<int16_t>(width - kSafeLeftInset - kSafeRight);
    if (usableW <= 0) {
        return out;
    }

    // The caption is placed first and gets exactly its line height, because it
    // is the line that says why there is no time at all. Squeezing it was the
    // shape of the original bug in miniature: a band smaller than the font that
    // goes in it, and nothing to notice. A panel that cannot spare those pixels
    // does not fit, and says so below.
    out.sub.h = lineHeightFor(kSubFontPx);
    out.sub.w = width;
    out.sub.x = 0;

    const int16_t rowsH = static_cast<int16_t>(height - out.sub.h - kBottomGuard);
    if (rowsH <= 0) {
        return out;
    }
    out.sub.y = rowsH;

    // Icons before font size: they are what lets the rows carry no words. Among
    // shapes that keep them, the larger font wins; side by side breaks ties,
    // because the panel is wider than it is tall.
    const Arrangement order[] = { Arrangement::SideBySide, Arrangement::Stacked };

    bool found = false;
    for (int pass = 0; pass < 2 && !found; pass++) {
        const bool icons = (pass == 0) && wantIcons;
        if (pass == 0 && !wantIcons) {
            continue;
        }

        for (size_t f = 0; f < kRowFontCount && !found; f++) {
            for (const Arrangement arrangement : order) {
                const int16_t bandH = (arrangement == Arrangement::SideBySide)
                                          ? rowsH
                                          : static_cast<int16_t>(rowsH / 2);
                if (!shapeFits(arrangement, icons, kRowFonts[f], usableW, bandH, iconW, iconH)) {
                    continue;
                }
                out.arrangement = arrangement;
                out.icons       = icons;
                out.rowFontPx   = kRowFonts[f];
                out.fits        = true;
                found           = true;
                break;
            }
        }
    }

    if (!found) {
        return out;
    }

    const int16_t lineH  = lineHeightFor(out.rowFontPx);
    const int16_t timeW  = timeWidthFor(out.rowFontPx);
    const int16_t groupW = static_cast<int16_t>(timeW + (out.icons ? iconW + kIconGap : 0));

    if (out.arrangement == Arrangement::SideBySide) {
        const int16_t total = static_cast<int16_t>(2 * groupW + kPairGap);
        const int16_t left  = centreIn(kSafeLeftInset, usableW, total);
        const int16_t right = static_cast<int16_t>(left + groupW + kPairGap);
        const int16_t textY = centreIn(0, rowsH, lineH);
        const int16_t iconY = centreIn(0, rowsH, iconH);

        out.textFirst  = Box { static_cast<int16_t>(left + (out.icons ? iconW + kIconGap : 0)),
                               textY, timeW, lineH };
        out.textSecond = Box { static_cast<int16_t>(right + (out.icons ? iconW + kIconGap : 0)),
                               textY, timeW, lineH };
        out.iconFirst  = Box { left, iconY, iconW, iconH };
        out.iconSecond = Box { right, iconY, iconW, iconH };
    } else {
        const int16_t bandH = static_cast<int16_t>(rowsH / 2);
        const int16_t left  = centreIn(kSafeLeftInset, usableW, groupW);
        const int16_t textX = static_cast<int16_t>(left + (out.icons ? iconW + kIconGap : 0));

        out.textFirst  = Box { textX, centreIn(0, bandH, lineH), timeW, lineH };
        out.textSecond = Box { textX, centreIn(bandH, bandH, lineH), timeW, lineH };
        out.iconFirst  = Box { left, centreIn(0, bandH, iconH), iconW, iconH };
        out.iconSecond = Box { left, centreIn(bandH, bandH, iconH), iconW, iconH };
    }

    // A time sitting against its icon runs from the left of its box; one
    // standing on its own is centred in it.
    out.textCentred = !out.icons;

    // The message replaces both times, so it gets the whole row band and the
    // full width -- it is a sentence, not a number.
    out.message = Box { 0, centreIn(0, rowsH, lineH), width, lineH };

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
