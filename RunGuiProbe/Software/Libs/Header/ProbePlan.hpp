/**
 ******************************************************************************
 * @file    ProbePlan.hpp
 * @date    24-08-2026
 * @author  Toby Murray <toby.murray@protonmail.com>
 * @brief   When the probe fires, and what the card says while it waits.
 ******************************************************************************
 *
 * The whole app is one question -- can a `Glance`-type service put a full
 * screen up by asking the kernel? -- and the answer is a message result, not a
 * calculation. So there is very little pure logic here. What there is, is the
 * part that would be annoying to get wrong on a watch: the dwell timer, and the
 * wording that reports the outcome.
 *
 * This header compiles against nothing but the standard library, so both can be
 * argued about at a desk.
 *
 * ## Why a dwell at all
 *
 * Firing on the first tick would launch the GUI before the card had been on
 * screen long enough to read, and would make the app impossible to scroll past
 * -- every pass through the carousel would hijack the screen. Three seconds is
 * long enough that it only happens when somebody stops on the card and waits,
 * which is exactly the gesture being stood in for.
 *
 ******************************************************************************
 */

#ifndef PROBEPLAN_HPP
#define PROBEPLAN_HPP

#include <cstdint>

namespace Probe
{

/// Bytes a glance line may hold, NUL included. Held to the SDK's
/// GLANCE_TEXT_SIZE by a static_assert in Service.cpp, so this header needs no
/// SDK header of its own.
constexpr uint32_t kLineBytes = 32;

/// How long the card must stay on screen before the request is sent.
constexpr uint32_t kDwellMs = 3000;

/// What has happened to the request so far. Every value except `Waiting` is a
/// result worth writing down -- the three failures are distinct, and telling
/// them apart is most of the point of the probe:
///
///   NotSent  the message could not be allocated or the send itself failed, so
///            the kernel never saw a request. Says nothing about whether the
///            kernel would have honoured one.
///   Refused  the kernel answered, and the answer was no. This is the result
///            that means "a Glance-type app may not do this".
///   Launched the kernel answered yes. Whether a GUI actually appeared is a
///            separate question, answered by the screen and by the GUI's own
///            line in probe.txt -- not by this.
enum class Phase : uint8_t {
    Waiting = 0,
    Launched,
    Refused,
    NotSent,
};

/// Two lines of a glance card.
struct Lines {
    char top[kLineBytes];
    char bottom[kLineBytes];
};

/// Milliseconds between two kernel tick timestamps.
///
/// `EventGlanceTick::timestamp` is a uint32 of milliseconds, so it wraps every
/// 49 days and a card straddling the wrap would otherwise compute an elapsed
/// time of about seven weeks and fire instantly. Unsigned subtraction wraps to
/// the right answer; this function exists so that fact is stated in one place
/// and tested rather than being an implicit property of the arithmetic.
uint32_t elapsedSince(uint32_t startMs, uint32_t nowMs);

/// Has the card been up long enough to fire? False once it has fired, because
/// the request is sent at most once per viewing.
bool shouldFire(uint32_t elapsedMs, bool alreadyFired);

/// What the card says. `elapsedMs` is only read in the `Waiting` phase, where
/// it drives the countdown.
Lines linesFor(Phase phase, uint32_t elapsedMs);

/// A stable one-word name for the phase, for the log file.
const char *nameOf(Phase phase);

} // namespace Probe

#endif // PROBEPLAN_HPP
