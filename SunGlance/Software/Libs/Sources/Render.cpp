/**
 ******************************************************************************
 * @file    Render.cpp
 * @date    18-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   The three lines the glance shows.
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

void clockText(const Clock &clock, char *out, size_t outSize)
{
    if (!clock.valid) {
        snprintf(out, outSize, "--:--");
        return;
    }
    snprintf(out, outSize, "%02d:%02d", clock.hour, clock.minute);
}

Lines trouble(const char *caption)
{
    Lines lines;
    snprintf(lines.title, kLineBytes, "sun");
    // The same sentinel SleepLab uses for a figure it has not earned. A blank
    // would read as "loading" and a zero would read as a time.
    snprintf(lines.value, kLineBytes, "--");
    snprintf(lines.sub, kLineBytes, "%s", caption);
    lines.caution = true;
    return lines;
}

} // namespace

Lines render(const View &view)
{
    switch (view.trouble) {
        case Trouble::NoPosition:
            // Not "set lat/lon in input.json", which is the useful sentence and
            // does not fit: 24 characters at this font is wider than the panel,
            // and an over-long caption is silently dropped rather than
            // truncated. The glance says what is wrong; the app's card in Kira
            // and its README say what to do about it.
            return trouble("no position set");
        case Trouble::BadConfig:
            return trouble("input.json rejected");
        case Trouble::NoClock:
            return trouble("clock not set");
        case Trouble::None:
            break;
    }

    Lines lines;

    if (view.kind == EventKind::MidnightSun) {
        snprintf(lines.title, kLineBytes, "midnight sun");
        snprintf(lines.value, kLineBytes, "no sunset");
        snprintf(lines.sub, kLineBytes, "the sun stays up");
        return lines;
    }

    if (view.kind == EventKind::PolarNight) {
        snprintf(lines.title, kLineBytes, "polar night");
        snprintf(lines.value, kLineBytes, "no sunrise");
        snprintf(lines.sub, kLineBytes, "the sun stays down");
        return lines;
    }

    const bool rising = (view.kind == EventKind::Rise);

    snprintf(lines.title, kLineBytes, rising ? "sunrise" : "sunset");
    clockText(view.when, lines.value, kLineBytes);

    char away[16] = { 0 };
    countdown(view.secondsAway, away, sizeof away);

    if (view.zoneSuspect) {
        // The caveat outranks the convenience. Everything on the screen is
        // still true of the configured position, and the clock it is being read
        // against is somewhere else, so the one thing worth the caption's line
        // is which of those two the times belong to.
        snprintf(lines.sub, kLineBytes, "times are for home");
        lines.caution = true;
        return lines;
    }

    if (view.nextDay) {
        // "06:14" on a Tuesday evening reads as this morning, which has been
        // and gone. Saying which day comes before saying how far away it is.
        snprintf(lines.sub, kLineBytes, "tomorrow, %s", away);
        return lines;
    }

    if (view.other.valid) {
        char paired[8] = { 0 };
        clockText(view.other, paired, sizeof paired);
        // Past tense for the event that has already happened, which is the
        // sunrise whenever the headline is a sunset.
        snprintf(lines.sub, kLineBytes, "%s, %s %s",
                 away, rising ? "sets" : "rose", paired);
        return lines;
    }

    snprintf(lines.sub, kLineBytes, "%s", away);
    return lines;
}

} // namespace Sun
