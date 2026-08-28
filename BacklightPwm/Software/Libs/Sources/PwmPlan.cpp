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
    {  0, 3000,     "off",   KernelAsk::HoldOn },
    {100, kMeterMs, "d100",  KernelAsk::Nothing },
    { 75, kMeterMs, "d75",   KernelAsk::Nothing },
    { 50, kMeterMs, "d50",   KernelAsk::Nothing },
    { 25, kMeterMs, "d25",   KernelAsk::Nothing },
    { 10, kMeterMs, "d10",   KernelAsk::Nothing },
    {  1, kMeterMs, "d1",    KernelAsk::Nothing },

    // Back to full, briefly. The eye is far better at spotting a difference
    // between adjacent frames than at judging an absolute level, so returning to
    // 100 immediately after 1 makes the range obvious to an observer in a way
    // that six separate photographs do not.
    {100, 3000,     "d100_again", KernelAsk::Nothing },

    // == The contest. Everything above is the ladder; this is the experiment ==
    //
    // Placed last on purpose. If the watch dies here, the ladder is already
    // filmed and the results file already written, and the death is itself the
    // answer to how well an app can hold a pin the kernel wants back.

    // The kernel is asked for a two second auto-off while this app keeps
    // modulating at 50 percent for ten. At about two seconds the kernel's own
    // timer fires and writes the pin off. Does the next burst overwrite it, or
    // does the light stay dark? Whichever happens, ODR is sampled every burst
    // and the disagreements are counted.
    { 50, 10000, "contest_autooff", KernelAsk::ShortAutoOff },

    // Blunter: the kernel is told the backlight should be off, and this app
    // carries on driving it at 50 percent. If the light stays visible, an app
    // can hold this pin against the kernel's stated intent; if it goes dark and
    // stays dark, it cannot, and that is the honest ceiling on this technique.
    { 50, 8000,  "contest_off",     KernelAsk::TurnOff },

    {  0, 2000,  "off_end",         KernelAsk::TurnOff },
};

} // namespace

const Rung* ladder() { return kLadder; }

size_t ladderSize() { return sizeof(kLadder) / sizeof(kLadder[0]); }

} // namespace Pwm
