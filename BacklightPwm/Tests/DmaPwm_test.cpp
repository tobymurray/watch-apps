/**
 * The divider arithmetic, which is the part of the DMA engine that has been
 * wrong twice: once by assuming the timer ran at the core clock, once by
 * measuring the timer's input clock and still getting the wrong rate. It is
 * tested here because it is the only part of that file that runs without the
 * registers, and because the failure it produces reaches the eye as a flashing
 * screen rather than as an error.
 */

#include <gtest/gtest.h>

#include "DmaPwm.hpp"

using Pwm::trimmedArr;

TEST(TrimmedArr, SlowWaveformGetsASmallerDivider)
{
    // The observed failure: running at 10 Hz when 250 was wanted. The divider
    // has to come down by the same factor the rate has to go up.
    EXPECT_EQ(trimmedArr(1811u, 10u, 250u), 72u);
}

TEST(TrimmedArr, FastWaveformGetsALargerDivider)
{
    EXPECT_EQ(trimmedArr(100u, 1000u, 250u), 400u);
}

TEST(TrimmedArr, AlreadyOnTargetIsLeftAlone)
{
    EXPECT_EQ(trimmedArr(640u, 250u, 250u), 640u);
}

TEST(TrimmedArr, ConvergesFromTheGuessThatShipped)
{
    // The v1.5.1 starting point against the rate it actually produced. Two
    // passes is what start() does, and it has to land near the target from
    // there, not merely move in the right direction.
    uint32_t arr = 1811u;
    // A hypothetical machine whose rate is exactly inverse to the divider.
    const uint32_t k = 10u * 1811u; // rate * arr, constant
    for (int i = 0; i < 2; ++i) {
        const uint32_t measured = k / arr;
        arr = trimmedArr(arr, measured, 250u);
    }
    const uint32_t settled = k / arr;
    EXPECT_GE(settled, 240u);
    EXPECT_LE(settled, 260u);
}

TEST(TrimmedArr, NeverReturnsZeroBecauseAFreeRunningTimerHasNoRate)
{
    // A large speed-up would divide to zero, which on the hardware is a timer
    // with no period at all rather than a very fast one.
    EXPECT_EQ(trimmedArr(1u, 1u, 100000u), 1u);
}

TEST(TrimmedArr, ClampsToWhatSixteenBitsHold)
{
    EXPECT_EQ(trimmedArr(60000u, 250u, 1u), Pwm::kArrMax);
}

TEST(TrimmedArr, AFailedMeasurementLeavesTheDividerWhereItWas)
{
    // measureHz returns 0 when it cannot interpret the window. Trimming against
    // that would replace a working waveform with a guess.
    EXPECT_EQ(trimmedArr(1811u, 0u, 250u), 1811u);
}
