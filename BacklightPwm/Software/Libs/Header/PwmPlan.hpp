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

struct Rung {
    uint8_t     duty;   ///< Percent on, matching RequestBacklightSet::brightness.
    uint32_t    holdMs; ///< How long to sit here, for the camera or the meter.
    const char* label;
};

const Rung* ladder();
size_t      ladderSize();

/// PWM period the ladder runs at. 250 Hz: fast enough that neither the eye nor a
/// phone camera's rolling shutter turns it into visible banding, slow enough that
/// a busy wait places its edges accurately.
constexpr uint32_t kPeriodUs = 4000;

/// How much PWM one service poll performs before returning to the message queue.
///
/// This is the watchdog safety margin, and it is the number to reach for if the
/// watch ever reboots during a run. Ten periods at 250 Hz. `FwDump` budgets its
/// slices at roughly 50 ms of work for the same reason.
constexpr uint32_t kBurstUs = 40000;

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
