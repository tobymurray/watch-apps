/**
 * Tests for the probe's dwell timer and its wording.
 *
 * Neither is complicated, and both are the kind of thing that is discovered to
 * be wrong on a wrist rather than at a desk: a countdown that fires instantly
 * makes the app impossible to scroll past, and a card that reports the wrong
 * one of three outcomes makes the whole experiment worthless.
 */

#include <cstring>

#include <gtest/gtest.h>

#include "ProbePlan.hpp"

namespace
{

TEST(ElapsedSince, CountsForwardNormally)
{
    EXPECT_EQ(Probe::elapsedSince(1000, 1000), 0u);
    EXPECT_EQ(Probe::elapsedSince(1000, 4000), 3000u);
}

TEST(ElapsedSince, SurvivesTheMillisecondWrap)
{
    // The kernel's tick timestamp is a uint32 of milliseconds and wraps every
    // 49 days. A card on screen across the wrap must read as half a second
    // elapsed, not as seven weeks -- which would fire the request instantly.
    EXPECT_EQ(Probe::elapsedSince(0xFFFFFF00u, 0x00000100u), 512u);
}

TEST(ShouldFire, WaitsForTheDwellThenFiresOnce)
{
    EXPECT_FALSE(Probe::shouldFire(0, false));
    EXPECT_FALSE(Probe::shouldFire(Probe::kDwellMs - 1, false));
    EXPECT_TRUE(Probe::shouldFire(Probe::kDwellMs, false));
    EXPECT_TRUE(Probe::shouldFire(Probe::kDwellMs + 5000, false));
}

TEST(ShouldFire, NeverFiresTwiceInOneViewing)
{
    EXPECT_FALSE(Probe::shouldFire(Probe::kDwellMs, true));
    EXPECT_FALSE(Probe::shouldFire(Probe::kDwellMs * 10, true));
}

TEST(Lines, CountsDownRoundingUp)
{
    // A card that has just appeared promises three seconds, not two: the number
    // is a statement about the future and one that undercounts is the wrong way
    // round.
    EXPECT_STREQ(Probe::linesFor(Probe::Phase::Waiting, 0).top, "asking in 3s");
    EXPECT_STREQ(Probe::linesFor(Probe::Phase::Waiting, 1).top, "asking in 3s");
    EXPECT_STREQ(Probe::linesFor(Probe::Phase::Waiting, 2000).top, "asking in 1s");
    EXPECT_STREQ(Probe::linesFor(Probe::Phase::Waiting, 2999).top, "asking in 1s");
    EXPECT_STREQ(Probe::linesFor(Probe::Phase::Waiting, 3000).top, "asking in 0s");
    // Past the dwell the countdown clamps rather than wrapping to a huge number.
    EXPECT_STREQ(Probe::linesFor(Probe::Phase::Waiting, 9999).top, "asking in 0s");
}

TEST(Lines, TellsTheThreeOutcomesApart)
{
    const Probe::Lines launched = Probe::linesFor(Probe::Phase::Launched, 0);
    const Probe::Lines refused  = Probe::linesFor(Probe::Phase::Refused, 0);
    const Probe::Lines notSent  = Probe::linesFor(Probe::Phase::NotSent, 0);

    EXPECT_STRNE(launched.top, refused.top);
    EXPECT_STRNE(refused.top, notSent.top);
    EXPECT_STRNE(launched.top, notSent.top);

    // "not sent" is not a refusal, and reading it as one would retire a whole
    // line of work on the strength of an allocation failure.
    EXPECT_STRNE(refused.bottom, notSent.bottom);
}

TEST(Lines, FitTheGlanceTextBuffer)
{
    const Probe::Phase phases[] = {
        Probe::Phase::Waiting,
        Probe::Phase::Launched,
        Probe::Phase::Refused,
        Probe::Phase::NotSent,
    };

    for (const Probe::Phase phase : phases) {
        const Probe::Lines lines = Probe::linesFor(phase, 0);
        EXPECT_LT(std::strlen(lines.top), Probe::kLineBytes) << Probe::nameOf(phase);
        EXPECT_LT(std::strlen(lines.bottom), Probe::kLineBytes) << Probe::nameOf(phase);
    }
}

TEST(NameOf, IsDistinctPerPhase)
{
    EXPECT_STREQ(Probe::nameOf(Probe::Phase::Waiting), "waiting");
    EXPECT_STREQ(Probe::nameOf(Probe::Phase::Launched), "launched");
    EXPECT_STREQ(Probe::nameOf(Probe::Phase::Refused), "refused");
    EXPECT_STREQ(Probe::nameOf(Probe::Phase::NotSent), "not-sent");
}

} // namespace
