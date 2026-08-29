/**
 ******************************************************************************
 * @file    PwmPlan.hpp
 * @brief   The ladder this app climbs, as a table.
 ******************************************************************************
 *
 * Deliberately the same rungs and the same hold time as `BacklightProbe`'s
 * Suite 1: 100, 75, 50, 25, 10, 1. That is the whole point of the exercise. Those
 * six requests produced six byte-identical register states and one indivisible
 * brightness; these six produce six different duty cycles on the same pin. Two
 * sets of photographs taken the same way, of the same screen, at the same six
 * numbers, and the difference between them is the finding.
 *
 * Anyone changing these values should change `BacklightProbe`'s to match, or the
 * comparison stops being a comparison.
 *
 ******************************************************************************
 */

#ifndef PWM_PLAN_HPP
#define PWM_PLAN_HPP

#include <cstddef>
#include <cstdint>

namespace Pwm
{

/// What to tell the kernel as a rung begins.
///
/// The ladder itself wants the kernel quiet and out of the way. The contest
/// rungs at the end deliberately provoke it, because "what happens when the app
/// and the kernel both own this pin" is the question Phase E existed to answer
/// and no amount of quiet running answers it.
enum class KernelAsk : uint8_t {
    Nothing      = 0, ///< Leave the kernel's backlight state as it is.
    HoldOn       = 1, ///< Ask for full brightness with a ten minute auto-off.
    ShortAutoOff = 2, ///< Ask for full brightness with a two second auto-off.
    TurnOff      = 3, ///< Tell the kernel the backlight should be off.
};

struct Rung {
    uint8_t     duty;   ///< Percent on, matching RequestBacklightSet::brightness.
    uint32_t    holdMs; ///< How long to sit here, for the camera or the meter.
    const char* label;
    KernelAsk   ask;    ///< Sent to the kernel as the rung begins.

    /// The other duty of a discrimination pair, or 0 for an ordinary rung.
    ///
    /// When set, the rung alternates between `duty` and `dutyB` every
    /// `kFlipMs`. That turns "how bright is this" into "did anything change",
    /// which is a question an eye answers well and an uncalibrated camera answers
    /// at all: the comparison is against the frame half a second ago rather than
    /// against an absolute scale, so auto-exposure, white balance and gamma all
    /// cancel out.
    uint8_t dutyB;
};

/// The standard ladder: six levels, held, for photographing and metering.
const Rung* ladder();
size_t      ladderSize();

/// The discrimination ladder: pairs of adjacent levels, alternating, to find
/// where two duties stop being distinguishable. Selected by a marker file; see
/// Service.
const Rung* flipLadder();
size_t      flipLadderSize();

/// PWM period the ladder runs at. 250 Hz: fast enough that neither the eye nor a
/// phone camera's rolling shutter turns it into visible banding, slow enough that
/// a busy wait places its edges accurately.
constexpr uint32_t kPeriodUs = 4000;

/// Whole PWM periods per service poll.
///
/// Counted in periods rather than microseconds because a period now sleeps
/// through its own off phase, and the cycle counter stops while it does; a time
/// budget measured against that clock would overrun by whatever was slept.
///
/// Two periods is 8 ms at 250 Hz, which is the same slice the time-based version
/// used. It is still the number to reach for if the watch ever reboots mid-run.
constexpr uint32_t kPeriodsPerBurst = 2;

/// How long a modulated rung runs before the ladder hands the CPU back.
///
/// A modulated rung spins flat out, because nothing else places its edges
/// accurately (see SoftPwm.hpp). That starves the GUI thread, and thirty
/// consecutive seconds of it rebooted the watch. So no rung runs for long, and
/// every one is followed by a breather.
///
/// Four seconds is still long enough to photograph and meter comfortably.
constexpr uint32_t kMeterMs = 4000;

/// A dark, idle gap after every modulated rung.
///
/// This is where the CPU goes back. The light is off, the service holds the pin
/// and blocks on the message queue, and the GUI gets a clear run at repainting
/// and at whatever the kernel needs of it.
///
/// Between rungs rather than inside them, because a gap inside a rung is a flash
/// and a gap between rungs is just the boundary between two measurements. It also
/// makes the rungs easier to pick out of a video.
constexpr uint32_t kBreatherMs = 1500;

/// How long each half of a discrimination pair is held.
///
/// Long enough to read as a step rather than a flicker, short enough that the
/// two halves are still being compared against each other rather than
/// remembered. Just under a second.
constexpr uint32_t kFlipMs = 800;

/// How long a discrimination pair runs: three full alternations, which is enough
/// to be sure without making the run any longer than it has to be. Still short
/// enough to sit inside the starvation budget a modulated rung has.
constexpr uint32_t kFlipHoldMs = kFlipMs * 6;

/// The short auto-off used by the contest rung. Two seconds into a ten second
/// rung, so the expiry lands in the middle of it with plenty of run either side.
constexpr uint32_t kShortAutoOffMs = 2000;

/// Auto-off asked of the kernel before the ladder starts.
///
/// Long, and not because the ladder is long. The point is to put the kernel's own
/// state machine into "backlight on" and keep it there, so that anything seen
/// during the run is this app modulating the pin rather than the kernel and this
/// app taking turns writing it. If the light dies mid-ladder anyway, that is the
/// kernel reasserting itself and it is the most interesting thing the run can
/// produce.
constexpr uint32_t kKernelHoldMs = 600000;

} // namespace Pwm

#endif // PWM_PLAN_HPP
