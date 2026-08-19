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

} // namespace

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
