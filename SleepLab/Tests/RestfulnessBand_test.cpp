/**
 * Host tests for the restfulness band.
 *
 * The band is the single most misreadable output this app has: anything
 * four-level drawn across a night looks like a hypnogram. So as well as the
 * arithmetic, these tests pin the things that keep it from becoming one --
 * that the method string travels with it, that awake epochs get no band at
 * all, and that a band computed without heart rate says so.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "Engine/RestfulnessBand.hpp"
#include "NightFixture.hpp"

namespace {

using Engine::kAbsent;
using Engine::Restfulness;
using Engine::RestfulnessBand;
using Engine::ScoringInput;
using Engine::Verdict;

TEST(RestfulnessBand, TheMethodStringSaysWhatTheBandIsNot)
{
    // It is written verbatim into every summary JSON. A file that does not say
    // which rule produced its band cannot be compared with one that does -- and
    // a reader that sees an unknown string must decline to plot rather than
    // plot it as though it were the current rule.
    EXPECT_NE(std::strstr(RestfulnessBand::kMethod, "not a sleep stage"), nullptr);
    EXPECT_NE(std::strstr(RestfulnessBand::kMethod, "v1"), nullptr);
    EXPECT_EQ(std::strstr(RestfulnessBand::kMethod, "REM"), nullptr);
    EXPECT_EQ(std::strstr(RestfulnessBand::kCaption, "deep sleep"), nullptr);
}

TEST(RestfulnessBand, AwakeAndUnscorableEpochsGetNoBand)
{
    // A restfulness value for an epoch the wearer was awake in describes
    // nothing, and drawing one would fill the gaps in the strip with something
    // that looks like data.
    std::vector<ScoringInput> in;
    Fixture::run(in, 5, 10);
    std::vector<Verdict> v = { Verdict::Sleep, Verdict::Wake, Verdict::Sleep,
                               Verdict::Unscorable, Verdict::Sleep };
    std::vector<Restfulness> out(5);

    RestfulnessBand::compute(in.data(), v.data(), 5, 500, out.data());

    EXPECT_NE(out[0], Restfulness::Unknown);
    EXPECT_EQ(out[1], Restfulness::Unknown);
    EXPECT_EQ(out[3], Restfulness::Unknown);
}

TEST(RestfulnessBand, StillerAndSlowerScoresMoreSettled)
{
    std::vector<ScoringInput> in = {
        Fixture::epoch(/*count=*/5,   /*hrX10=*/500),   // still, at the minimum
        Fixture::epoch(/*count=*/60,  /*hrX10=*/540),   // middling
        Fixture::epoch(/*count=*/300, /*hrX10=*/600),   // restless, raised HR
    };
    std::vector<Verdict> v(3, Verdict::Sleep);
    std::vector<Restfulness> out(3);

    RestfulnessBand::compute(in.data(), v.data(), 3, /*hrMinX10=*/500, out.data());

    EXPECT_EQ(out[0], Restfulness::Deepest);
    EXPECT_EQ(out[1], Restfulness::Settled);
    EXPECT_EQ(out[2], Restfulness::Restless);
}

TEST(RestfulnessBand, HeartRateIsJudgedAgainstTheNightsOwnMinimum)
{
    // Never against an absolute bpm. The same epoch is "settled" on a night
    // whose minimum is high and "restless" on one whose minimum is low, which
    // is the whole point of a personal reference.
    std::vector<ScoringInput> in = { Fixture::epoch(5, 560) };
    std::vector<Verdict> v(1, Verdict::Sleep);
    std::vector<Restfulness> out(1);

    RestfulnessBand::compute(in.data(), v.data(), 1, /*hrMinX10=*/550, out.data());
    const Restfulness nearMin = out[0];

    RestfulnessBand::compute(in.data(), v.data(), 1, /*hrMinX10=*/460, out.data());
    const Restfulness farAbove = out[0];

    EXPECT_GT(static_cast<int>(nearMin), static_cast<int>(farAbove));
}

TEST(RestfulnessBand, WithoutHeartRateTheBandIsMovementOnlyAndSaysSo)
{
    // "movement and heart rate" and "movement" are not the same method, and
    // the summary file must not claim the first when it did the second.
    std::vector<ScoringInput> in;
    for (int i = 0; i < 5; ++i) {
        in.push_back(Fixture::epoch(5, static_cast<int16_t>(kAbsent)));
    }
    std::vector<Verdict> v(5, Verdict::Sleep);
    std::vector<Restfulness> out(5);

    EXPECT_FALSE(RestfulnessBand::compute(in.data(), v.data(), 5, kAbsent,
                                          out.data()));
    // Still produces a usable band from movement alone, on the same scale --
    // an epoch that happened to lack a heart rate must not draw darker than
    // its neighbours for a reason the wearer cannot see.
    EXPECT_EQ(out[0], Restfulness::Deepest);
}

TEST(RestfulnessBand, ReportsWhetherHeartRateActuallyContributed)
{
    std::vector<ScoringInput> in;
    Fixture::run(in, 5, 5);
    std::vector<Verdict> v(5, Verdict::Sleep);
    std::vector<Restfulness> out(5);

    EXPECT_TRUE(RestfulnessBand::compute(in.data(), v.data(), 5, 500, out.data()));
}

TEST(RestfulnessBand, NullInputsAreRefusedRatherThanDereferenced)
{
    std::vector<Restfulness> out(5);
    EXPECT_FALSE(RestfulnessBand::compute(nullptr, nullptr, 5, 500, out.data()));
}

} // namespace
