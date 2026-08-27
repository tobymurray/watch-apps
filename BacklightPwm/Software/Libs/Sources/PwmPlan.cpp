/**
 ******************************************************************************
 * @file    PwmPlan.cpp
 * @brief   The rungs.
 ******************************************************************************
 */

#include "PwmPlan.hpp"

namespace Pwm
{

namespace
{

/// Long enough to get a phone camera or a light meter onto a static frame
/// without hurrying. Same figure BacklightProbe uses, so the two sets of
/// photographs are taken under the same conditions.
constexpr uint32_t kMeterMs = 6000;

/// Descending, so the first rung after dark is the unambiguous one. If 100
/// percent does not visibly light the screen then the pin is not being driven
/// and there is no point reading the rest.
constexpr Rung kLadder[] = {
    {  0, 3000,     "off"  },
    {100, kMeterMs, "d100" },
    { 75, kMeterMs, "d75"  },
    { 50, kMeterMs, "d50"  },
    { 25, kMeterMs, "d25"  },
    { 10, kMeterMs, "d10"  },
    {  1, kMeterMs, "d1"   },

    // Back to full, briefly. The eye is far better at spotting a difference
    // between adjacent frames than at judging an absolute level, so returning to
    // 100 immediately after 1 makes the range obvious to an observer in a way
    // that six separate photographs do not.
    {100, 3000,     "d100_again" },

    {  0, 2000,     "off_end" },
};

} // namespace

const Rung* ladder() { return kLadder; }

size_t ladderSize() { return sizeof(kLadder) / sizeof(kLadder[0]); }

} // namespace Pwm
