/**
 ******************************************************************************
 * @file    Alerts_test.cpp
 * @brief   That every pattern a ride can produce is tellable from every other.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "Alerts.hpp"

using namespace Alerts;

namespace {

const char *nameOf(const Alert &a)
{
    if (&a == &kLap)         { return "lap"; }
    if (&a == &kTarget)      { return "target"; }
    if (&a == &kStepHarder)  { return "step harder"; }
    if (&a == &kStepEasier)  { return "step easier"; }
    return "session end";
}

} // namespace

TEST(Alerts, NoTwoFeelTheSame)
{
    // The static_assert in the header says this too; here it names the pair.
    for (size_t i = 0; i < kAllCount; ++i) {
        for (size_t j = i + 1; j < kAllCount; ++j) {
            EXPECT_TRUE(distinctOnVibro(kAll[i], kAll[j]))
                << nameOf(kAll[i]) << " and " << nameOf(kAll[j])
                << " feel the same on the wrist";
            EXPECT_TRUE(distinctOnBuzzer(kAll[i], kAll[j]))
                << nameOf(kAll[i]) << " and " << nameOf(kAll[j])
                << " sound the same";
        }
    }
}

TEST(Alerts, TheTwoStepAlertsDifferInShapeRatherThanCount)
{
    // The rule: two pips against three pips is the wrong axis. These differ in
    // texture, in how many events they have, and in the length of each.
    EXPECT_NE(kStepHarder.texture, kStepEasier.texture);
    EXPECT_EQ(beepCount(kStepHarder), 1u);
    EXPECT_EQ(beepCount(kStepEasier), 2u);
    // Long and strong against short and light, which is the mapping a rider is
    // meant to infer rather than memorise.
    EXPECT_GT(kStepHarder.beepMs[0], kStepEasier.beepMs[0] * 4u);
}

TEST(Alerts, TheLapAndTheTargetAreUnchanged)
{
    // A rider already knows these from every other ride; a session must not
    // redefine them.
    EXPECT_EQ(kLap.texture, Texture::Alert);
    EXPECT_EQ(kLap.vibroCount, 1u);
    EXPECT_EQ(beepCount(kLap), 2u);
    EXPECT_EQ(kLap.beepMs[0], 120u);
    EXPECT_EQ(kLap.beepGapMs, 100u);

    EXPECT_EQ(kTarget.texture, Texture::Alert);
    EXPECT_EQ(kTarget.vibroCount, 2u);
    EXPECT_EQ(beepCount(kTarget), 3u);
    EXPECT_EQ(kTarget.beepMs[0], 200u);
}

TEST(Alerts, NothingExceedsWhatTheMessagesCanCarry)
{
    // RequestBuzzerPlay::skMaxNotes is 10 and RequestVibroPlay's is 8, and both
    // count pauses, so N events need 2N-1 notes.
    for (const Alert &a : kAll) {
        ASSERT_GT(a.vibroCount, 0u);
        EXPECT_LE(2u * a.vibroCount - 1u, 8u) << nameOf(a) << " needs too many vibro notes";
        const size_t beeps = beepCount(a);
        ASSERT_GT(beeps, 0u);
        EXPECT_LE(2u * beeps - 1u, 10u) << nameOf(a) << " needs too many buzzer notes";
    }
}
