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

/**
 * Which idle modes count as keeping up. The consequence of getting this wrong
 * is not a wrong number on a screen: choose too loosely and the ladder idles in
 * a mode that stops the waveform, which is the flashing this whole exercise is
 * trying to remove.
 */

using Pwm::keepsUp;

TEST(KeepsUp, TheGatedCaseThatWasMeasuredIsNotCloseEnough)
{
    // The one real measurement: 27 Hz across a long sleep against 255 spinning.
    EXPECT_FALSE(keepsUp(27u, 255u));
}

TEST(KeepsUp, AnExactMatchKeepsUp)
{
    EXPECT_TRUE(keepsUp(255u, 255u));
}

TEST(KeepsUp, CountingNoiseOfAPassOrTwoStillKeepsUp)
{
    // The rate is counted passes over a window, so neighbouring values differ by
    // the quantisation and not by anything real.
    EXPECT_TRUE(keepsUp(252u, 255u));
}

TEST(KeepsUp, TenPercentDownIsTheEdgeAndIsAccepted)
{
    EXPECT_TRUE(keepsUp(230u, 255u));   // 255 - 25
}

TEST(KeepsUp, MoreThanTenPercentDownIsRejected)
{
    EXPECT_FALSE(keepsUp(228u, 255u));
}

TEST(KeepsUp, FasterThanSpinningKeepsUp)
{
    // Can happen by a pass of quantisation. It is not a reason to reject a mode.
    EXPECT_TRUE(keepsUp(257u, 255u));
}

TEST(KeepsUp, NothingKeepsUpWithAWaveformThatWasNotRunning)
{
    // A zero spin rate means the measurement failed, and every mode comparing
    // equal to it would otherwise be declared good.
    EXPECT_FALSE(keepsUp(0u, 0u));
}
