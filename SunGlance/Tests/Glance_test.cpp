/**
 * The whole app, from a file on the watch to the strings the kernel receives.
 *
 * Every other test here checks a part. This one checks that the parts are
 * joined up: that a config file on the storage becomes a position, that the
 * position and the clock become a schedule, that the schedule becomes three
 * lines, and -- the part that has burned this repository before -- that those
 * lines are actually *sent* rather than merely computed.
 *
 * The times asserted are this code's own, not astral's: Solar_test is where the
 * arithmetic is checked against an independent implementation, and repeating
 * that here would only make these tests fail for reasons that have nothing to
 * do with what they are about. What these assert is that the number on the
 * screen is the number the core produced, rendered in the local zone, next to
 * the right word.
 *
 * TZ=UTC is set on the test rather than assumed -- see the CMakeLists. Every
 * assertion below is a clock reading, and a machine in Ottawa would otherwise
 * disagree with a machine in Sydney about all of them.
 */

#include <gtest/gtest.h>

#include <string>

#include "GlanceHarness.hpp"

namespace {

using SunGlanceTest::Rig;

// London, and the day this app was written. Rise 04:50:39 UTC, set 19:17:00.
constexpr int64_t kRise = 1787028639;
constexpr int64_t kSet  = 1787080620;
/// The next morning's sunrise, for the after-sunset case.
constexpr int64_t kRiseTomorrow = 1787115135;

/// An hour and a bit before dawn, deliberately 30 seconds off a whole minute:
/// starting exactly 72 minutes out would put the countdown on a boundary, and a
/// test of "one second changes nothing" would then be measuring the boundary
/// rather than the guard.
constexpr int64_t kBeforeDawn = kRise - (72 * 60 + 30);

std::string configFor(const std::string &lat, const std::string &lon)
{
    return "{\n  \"schema\": 1,\n  \"values\": {\n"
           "    \"lat\": \"" + lat + "\",\n"
           "    \"lon\": \"" + lon + "\"\n  }\n}\n";
}

std::string london() { return configFor("51.5074", "-0.1278"); }

TEST(Glance, SendsTheNextEventOnce)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);              // 03:38 UTC, an hour and a bit before dawn
    rig.comm.viewing(3);

    rig.run();

    // Three ticks, one update: the form is sent when something changed and not
    // when a frame happened. At 60 Hz the difference is the whole IPC budget.
    ASSERT_EQ(rig.updates().size(), 1u);

    const auto &update = rig.updates().front();
    EXPECT_EQ(update.name, "Sun");
    ASSERT_EQ(update.texts.size(), 3u);
    EXPECT_EQ(update.title(), "sunrise");
    EXPECT_EQ(update.value(), "04:50");
    EXPECT_EQ(update.sub(), "in 1h12m, sets 19:17");
}

TEST(Glance, DuringTheDayItCountsDownToSunset)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kSet - (8 * 60 + 30) * 60);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().title(), "sunset");
    EXPECT_EQ(rig.updates().front().value(), "19:17");
    EXPECT_EQ(rig.updates().front().sub(), "in 8h30m, rose 04:50");
}

TEST(Glance, AfterSunsetItRollsToTomorrow)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kSet + 60);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().title(), "sunrise");
    EXPECT_EQ(rig.updates().front().value(), "04:52");
    // Tomorrow's sunrise really is a day and a bit away, and the caption says
    // which day before it says how long.
    const int64_t away = kRiseTomorrow - (kSet + 60);
    EXPECT_EQ(rig.updates().front().sub(),
              "tomorrow, in " + std::to_string(away / 3600) + "h"
                              + std::to_string((away % 3600) / 60) + "m");
}

TEST(Glance, TheMinuteTickingOverSendsExactlyOneMoreUpdate)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.viewing(4);
    // Between the second and third frames, a minute passes.
    rig.comm.setClockStep(3, 60);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 2u);
    EXPECT_EQ(rig.updates().at(0).sub(), "in 1h12m, sets 19:17");
    EXPECT_EQ(rig.updates().at(1).sub(), "in 1h11m, sets 19:17");
    // The headline did not move, and was still resent -- the form is one
    // object, so a caption change carries the whole thing. Worth knowing, and
    // cheap at once a minute.
    EXPECT_EQ(rig.updates().at(1).value(), "04:50");
}

TEST(Glance, ASecondPassingIsNotEnoughToSendAnything)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.viewing(4);
    rig.comm.setClockStep(3, 1);

    rig.run();

    // The screen shows minutes. A second is a recompute -- cheap, and it has to
    // happen or the minute boundary would be missed -- but nothing to send.
    EXPECT_EQ(rig.updates().size(), 1u);
}

TEST(Glance, WithNoConfigItSaysSoInsteadOfGuessing)
{
    Rig rig;
    rig.at(kRise);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    const auto &update = rig.updates().front();
    EXPECT_EQ(update.value(), "--");
    EXPECT_EQ(update.sub(), "no position set");
    // Amber, because this is a caveat rather than a caption.
    EXPECT_EQ(update.subColor(), static_cast<uint8_t>(GLANCE_COLOR_YELLOW_DARK));
}

TEST(Glance, AConfigItCannotReadIsNotTheSameAsNoConfig)
{
    Rig rig;
    rig.seed("input.json", configFor("45,4215", "-75,6972"));
    rig.at(kRise);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().sub(), "input.json rejected");
}

TEST(Glance, APositionOnTheOtherSideOfTheWorldIsFlagged)
{
    // Sydney's coordinates on a watch running UTC: the times are right for
    // Sydney and the clock they are shown against is not.
    Rig rig;
    rig.seed("input.json", configFor("-33.8688", "151.2093"));
    rig.at(kRise);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().sub(), "times are for home");
    EXPECT_EQ(rig.updates().front().subColor(), static_cast<uint8_t>(GLANCE_COLOR_YELLOW_DARK));
}

TEST(Glance, MidnightSunIsDrawnAsItself)
{
    Rig rig;
    rig.seed("input.json", configFor("78.2232", "15.6267"));  // Longyearbyen
    rig.at(kRise);                                            // mid-August: still up
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().title(), "midnight sun");
    EXPECT_EQ(rig.updates().front().value(), "no sunset");
    // Not a caveat: the sky is doing this on purpose.
    EXPECT_EQ(rig.updates().front().subColor(), static_cast<uint8_t>(GLANCE_COLOR_GRAY));
}

TEST(Glance, AGlanceAreaTooSmallToDrawInIsDeclined)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kRise);
    rig.comm.maxControls = 2;
    rig.comm.viewing(2);

    rig.run();

    EXPECT_TRUE(rig.updates().empty());
    // And it left rather than drawing two thirds of a screen.
    EXPECT_EQ(rig.system.lastExitStatus, 0);
}

TEST(Glance, AClockThatIsNotSetShowsNoTimes)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(0);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().value(), "--");
    EXPECT_EQ(rig.updates().front().sub(), "clock not set");
}

TEST(Glance, TheServiceReturnsWhenTheCardScrollsAway)
{
    // A Glance-type app's run() must return on EVENT_GLANCE_STOP. If it does
    // not, this test hangs rather than failing -- which is itself the signal.
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kRise);
    rig.comm.viewing(1);

    rig.run();

    SUCCEED();
}

} // namespace
