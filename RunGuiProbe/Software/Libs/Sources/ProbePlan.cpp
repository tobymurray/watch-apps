/**
 ******************************************************************************
 * @file    ProbePlan.cpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   When the probe fires, and what the card says while it waits.
 ******************************************************************************
 */

#include "ProbePlan.hpp"

#include <cstdio>
#include <cstring>

namespace Probe
{

namespace
{

/// Copy into a fixed line, NUL-terminated, truncated rather than overrun.
void setLine(char (&dst)[kLineBytes], const char *src)
{
    std::snprintf(dst, sizeof dst, "%s", src);
}

} // namespace

uint32_t elapsedSince(uint32_t startMs, uint32_t nowMs)
{
    // Deliberately unsigned: 0x00000100 - 0xFFFFFF00 is 512, which is the
    // elapsed time across the wrap rather than the 49 days a signed reading
    // would give.
    return nowMs - startMs;
}

bool shouldFire(uint32_t elapsedMs, bool alreadyFired)
{
    if (alreadyFired) {
        return false;
    }
    return elapsedMs >= kDwellMs;
}

Lines linesFor(Phase phase, uint32_t elapsedMs)
{
    Lines lines {};

    switch (phase) {
        case Phase::Waiting: {
            // Rounded up, so a card that has just appeared reads "3" rather
            // than "2": the number is a promise about the future, and one that
            // undercounts is the wrong way round.
            const uint32_t remaining = (elapsedMs >= kDwellMs) ? 0u : (kDwellMs - elapsedMs);
            const unsigned seconds   = static_cast<unsigned>((remaining + 999u) / 1000u);
            std::snprintf(lines.top, sizeof lines.top, "asking in %us", seconds);
            setLine(lines.bottom, "stay on this card");
            break;
        }

        case Phase::Launched:
            setLine(lines.top, "kernel said yes");
            setLine(lines.bottom, "a screen should be up");
            break;

        case Phase::Refused:
            setLine(lines.top, "kernel said no");
            setLine(lines.bottom, "glance apps cannot");
            break;

        case Phase::NotSent:
            setLine(lines.top, "not sent");
            setLine(lines.bottom, "alloc or send failed");
            break;
    }

    return lines;
}

const char *nameOf(Phase phase)
{
    switch (phase) {
        case Phase::Waiting:  return "waiting";
        case Phase::Launched: return "launched";
        case Phase::Refused:  return "refused";
        case Phase::NotSent:  return "not-sent";
    }
    return "?";
}

} // namespace Probe
