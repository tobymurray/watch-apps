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
};

const Rung* ladder();
size_t      ladderSize();

/// PWM period the ladder runs at. 250 Hz: fast enough that neither the eye nor a
/// phone camera's rolling shutter turns it into visible banding, slow enough that
/// a busy wait places its edges accurately.
constexpr uint32_t kPeriodUs = 4000;

/// How much PWM one service poll performs before yielding.
///
/// The watchdog safety margin, and the number to reach for if the watch reboots
/// during a run. Two periods at 250 Hz.
///
/// It was 40 ms, and the watch rebooted. That was not this value's fault (the
/// service was not yielding at all, so no burst length would have saved it), but
/// 8 ms is a better place to start now that it does: the shorter the burst, the
/// sooner everything else on the system gets a turn, at the cost of a slightly
/// larger share of each rung spent in the gaps between bursts. That cost shows
/// up honestly in the achieved duty on screen.
constexpr uint32_t kBurstUs = 8000;

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
