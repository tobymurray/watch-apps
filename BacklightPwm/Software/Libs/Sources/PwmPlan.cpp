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
#define BREATHER(n) {0, kBreatherMs, n, KernelAsk::Nothing, 0}

constexpr Rung kLadder[] = {
    {0, 3000, "off", KernelAsk::HoldOn, 0},

    // Each modulated rung spins flat out and is followed by a breather where the
    // service sleeps. The breathers are dark, which is why they are between the
    // measurements rather than inside them.
    {100, kMeterMs, "d100", KernelAsk::Nothing, 0},
    BREATHER("gap1"),
    {75, kMeterMs, "d75", KernelAsk::Nothing, 0},
    BREATHER("gap2"),
    {50, kMeterMs, "d50", KernelAsk::Nothing, 0},
    BREATHER("gap3"),
    {25, kMeterMs, "d25", KernelAsk::Nothing, 0},
    BREATHER("gap4"),
    {10, kMeterMs, "d10", KernelAsk::Nothing, 0},
    BREATHER("gap5"),
    {1, kMeterMs, "d1", KernelAsk::Nothing, 0},
    BREATHER("gap6"),

    // Back to full, briefly. The eye is far better at spotting a difference
    // between adjacent frames than at judging an absolute level, so returning to
    // 100 immediately after 1 makes the range obvious to an observer in a way
    // that six separate photographs do not.
    {100, 3000, "d100_again", KernelAsk::Nothing, 0},
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
    {50, 6000, "contest_autooff", KernelAsk::ShortAutoOff, 0},
    BREATHER("gap8"),

    // Blunter: the kernel is told the backlight should be off, and this app
    // carries on driving it at 50 percent. If the light stays visible, an app
    // can hold this pin against the kernel's stated intent; if it goes dark and
    // stays dark, it cannot, and that is the honest ceiling on this technique.
    {50, 6000, "contest_off", KernelAsk::TurnOff, 0},

    {0, 2000, "off_end", KernelAsk::TurnOff, 0},
};


/**
 * The discrimination ladder.
 *
 * The standard ladder answers "can this dim", and it has. This answers the
 * question that decides what a vendor should expose: **how many levels are worth
 * having.**
 *
 * Absolute brightness is the wrong thing to measure for that, and the earlier
 * runs show why. The numbers came off an auto-exposing phone camera through an
 * sRGB gamma curve, which is fine for "these six are different" and useless for
 * "how far apart are they". So this ladder does not measure brightness at all.
 * Each rung alternates between two adjacent duties, and the only question is
 * whether the change is visible. That comparison is against the frame half a
 * second ago rather than an absolute scale, so exposure, white balance and gamma
 * all cancel.
 *
 * The pairs are spaced geometrically, roughly 1.4x apart, and crowded at the
 * bottom because that is where the earlier run put most of the usable range. The
 * answer is the pair at which the flipping stops being visible: everything below
 * it is resolvable, everything above is wasted precision.
 */
constexpr Rung kFlipLadder[] = {
    {  0, 2500, "off",     KernelAsk::HoldOn,  0 },

    // Full brightness first, as a sanity anchor: if this pair is not obviously
    // flipping then the run is broken and nothing below it means anything.
    { 70, kFlipHoldMs, "f070_100", KernelAsk::Nothing, 100 },
    BREATHER("g01"),
    { 50, kFlipHoldMs, "f050_070", KernelAsk::Nothing, 70 },
    BREATHER("g02"),
    { 35, kFlipHoldMs, "f035_050", KernelAsk::Nothing, 50 },
    BREATHER("g03"),
    { 25, kFlipHoldMs, "f025_035", KernelAsk::Nothing, 35 },
    BREATHER("g04"),
    { 18, kFlipHoldMs, "f018_025", KernelAsk::Nothing, 25 },
    BREATHER("g05"),
    { 12, kFlipHoldMs, "f012_018", KernelAsk::Nothing, 18 },
    BREATHER("g06"),
    {  8, kFlipHoldMs, "f008_012", KernelAsk::Nothing, 12 },
    BREATHER("g07"),
    {  5, kFlipHoldMs, "f005_008", KernelAsk::Nothing, 8 },
    BREATHER("g08"),
    {  3, kFlipHoldMs, "f003_005", KernelAsk::Nothing, 5 },
    BREATHER("g09"),
    {  2, kFlipHoldMs, "f002_003", KernelAsk::Nothing, 3 },
    BREATHER("g10"),
    {  1, kFlipHoldMs, "f001_002", KernelAsk::Nothing, 2 },
    BREATHER("g11"),

    // And the floor: is one percent distinguishable from off at all? The
    // standard ladder put it three points above the off level, which is close
    // enough to be worth asking directly.
    {  0, kFlipHoldMs, "f000_001", KernelAsk::Nothing, 1 },

    {  0, 2000, "off_end", KernelAsk::TurnOff, 0 },
};

#undef BREATHER

} // namespace

const Rung* ladder() { return kLadder; }

size_t ladderSize() { return sizeof(kLadder) / sizeof(kLadder[0]); }

const Rung* flipLadder() { return kFlipLadder; }

size_t flipLadderSize() { return sizeof(kFlipLadder) / sizeof(kFlipLadder[0]); }

} // namespace Pwm
