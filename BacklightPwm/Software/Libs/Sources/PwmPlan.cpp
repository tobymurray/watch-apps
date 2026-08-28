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

/// Descending, so the first rung after dark is the unambiguous one. If 100
/// percent does not visibly light the screen then the pin is not being driven
/// and there is no point reading the rest.
/// A dark gap. Held, so the service sleeps through it and the GUI gets the CPU.
#define BREATHER(n) {0, kBreatherMs, n, KernelAsk::Nothing}

constexpr Rung kLadder[] = {
    {  0, 3000,     "off",   KernelAsk::HoldOn },

    // Each modulated rung spins flat out and is followed by a breather where the
    // service sleeps. The breathers are dark, which is why they are between the
    // measurements rather than inside them.
    {100, kMeterMs, "d100",  KernelAsk::Nothing },
    BREATHER("gap1"),
    { 75, kMeterMs, "d75",   KernelAsk::Nothing },
    BREATHER("gap2"),
    { 50, kMeterMs, "d50",   KernelAsk::Nothing },
    BREATHER("gap3"),
    { 25, kMeterMs, "d25",   KernelAsk::Nothing },
    BREATHER("gap4"),
    { 10, kMeterMs, "d10",   KernelAsk::Nothing },
    BREATHER("gap5"),
    {  1, kMeterMs, "d1",    KernelAsk::Nothing },
    BREATHER("gap6"),

    // Back to full, briefly. The eye is far better at spotting a difference
    // between adjacent frames than at judging an absolute level, so returning to
    // 100 immediately after 1 makes the range obvious to an observer in a way
    // that six separate photographs do not.
    {100, 3000,     "d100_again", KernelAsk::Nothing },
    BREATHER("gap7"),

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
    { 50, 6000, "contest_autooff", KernelAsk::ShortAutoOff },
    BREATHER("gap8"),

    // Blunter: the kernel is told the backlight should be off, and this app
    // carries on driving it at 50 percent. If the light stays visible, an app
    // can hold this pin against the kernel's stated intent; if it goes dark and
    // stays dark, it cannot, and that is the honest ceiling on this technique.
    { 50, 6000,  "contest_off",     KernelAsk::TurnOff },

    {  0, 2000,  "off_end",         KernelAsk::TurnOff },
};

#undef BREATHER

} // namespace

const Rung* ladder() { return kLadder; }

size_t ladderSize() { return sizeof(kLadder) / sizeof(kLadder[0]); }

} // namespace Pwm
