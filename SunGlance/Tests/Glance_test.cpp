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

#include <cstring>
#include <string>

#include "GlanceHarness.hpp"

#include "Icons.h"

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

/// Compare what the kernel was handed against an icon by *content*, not by
/// address. The generated header declares the arrays `static const`, so this
/// translation unit has its own copy and the pointers are legitimately
/// different -- which is also why Service.cpp is the only file in the app that
/// includes it.
bool isIcon(const uint8_t *buffer, const uint8_t *icon, size_t size)
{
    return buffer != nullptr && std::memcmp(buffer, icon, size) == 0;
}

bool isSunrise(const uint8_t *buffer)
{
    return isIcon(buffer, ICON_SUNRISE_ABGR2222, ICON_SUNRISE_SIZE);
}

bool isSunset(const uint8_t *buffer)
{
    return isIcon(buffer, ICON_SUNSET_ABGR2222, ICON_SUNSET_SIZE);
}

TEST(Glance, SendsBothUpcomingEventsOnce)
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
    ASSERT_EQ(update.images.size(), 2u);

    EXPECT_EQ(update.first(), "04:50");
    EXPECT_EQ(update.second(), "19:17");
    EXPECT_EQ(update.sub(), "in 1h12m");

    // Sunrise on top, because it is what happens next.
    EXPECT_TRUE(isSunrise(update.iconFirst()));
    EXPECT_TRUE(isSunset(update.iconSecond()));
    EXPECT_TRUE(update.iconsShown());
}

TEST(Glance, DuringTheDayTheIconsSwapRound)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kSet - (8 * 60 + 30) * 60);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    const auto &update = rig.updates().front();

    EXPECT_EQ(update.first(), "19:17");
    // Tomorrow's sunrise, not this morning's: nothing on the screen is behind
    // you.
    EXPECT_EQ(update.second(), "04:52");
    EXPECT_EQ(update.sub(), "in 8h30m");
    EXPECT_TRUE(isSunset(update.iconFirst()));
    EXPECT_TRUE(isSunrise(update.iconSecond()));
}

TEST(Glance, AfterSunsetBothRowsAreTomorrows)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kSet + 60);
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    const auto &update = rig.updates().front();

    EXPECT_EQ(update.first(), "04:52");
    EXPECT_EQ(update.second(), "19:14");
    EXPECT_TRUE(isSunrise(update.iconFirst()));
    // And the caption says which day, because "04:52" at eight in the evening
    // otherwise reads as this morning.
    const int64_t away = kRiseTomorrow - (kSet + 60);
    EXPECT_EQ(update.sub(),
              "tomorrow, in " + std::to_string(away / 3600) + "h"
                              + std::to_string((away % 3600) / 60) + "m");
}

TEST(Glance, WithoutEnoughControlsForIconsTheWordsComeBack)
{
    // Every SDK glance example asks for three controls. A kernel that offers
    // only that gets a screen that still says both times.
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.maxControls = 4;
    rig.comm.viewing(2);

    rig.run();

    ASSERT_EQ(rig.updates().size(), 1u);
    const auto &update = rig.updates().front();

    EXPECT_TRUE(update.images.empty()) << "no room for icons, so none were made";
    EXPECT_EQ(update.first(), "rise 04:50");
    EXPECT_EQ(update.second(), "set 19:17");
    EXPECT_EQ(update.sub(), "in 1h12m");
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
    EXPECT_EQ(rig.updates().at(0).sub(), "in 1h12m");
    EXPECT_EQ(rig.updates().at(1).sub(), "in 1h11m");
    // The times did not move, and were still resent -- the form is one object,
    // so a caption change carries the whole thing. Worth knowing, and cheap at
    // once a minute.
    EXPECT_EQ(rig.updates().at(1).first(), "04:50");
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
    EXPECT_EQ(update.first(), "--");
    EXPECT_EQ(update.sub(), "no position set");
    // Amber, because this is a caveat rather than a caption.
    EXPECT_EQ(update.subColor(), static_cast<uint8_t>(GLANCE_COLOR_YELLOW_DARK));
    // And no icons or second row: there is no event for a picture to label.
    EXPECT_FALSE(update.iconsShown());
    EXPECT_FALSE(update.secondIconShown());
    EXPECT_FALSE(update.secondShown());
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
    EXPECT_EQ(rig.updates().front().first(), "no sunset");
    EXPECT_EQ(rig.updates().front().sub(), "the sun stays up");
    EXPECT_FALSE(rig.updates().front().iconsShown());
    // Not a caveat: the sky is doing this on purpose.
    EXPECT_EQ(rig.updates().front().subColor(), static_cast<uint8_t>(GLANCE_COLOR_GRAY));
}

TEST(Glance, AGlanceAreaTooSmallToDrawInIsDeclined)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kRise);
    rig.comm.maxControls = 2;   // below the three this screen cannot go under
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
    EXPECT_EQ(rig.updates().front().first(), "--");
    EXPECT_EQ(rig.updates().front().sub(), "clock not set");
}

TEST(Glance, ItWritesDownWhatTheKernelGaveIt)
{
    // The file that exists because the first layout was tuned against another
    // app's numbers and clipped a row. Whatever the panel turns out to be, it
    // is now readable over USB instead of guessed at.
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.viewing(1);

    rig.run();

    const std::string note = rig.fs.readFile("glance.txt");
    EXPECT_NE(note.find("area 240x60"), std::string::npos) << note;
    EXPECT_NE(note.find("side-by-side"), std::string::npos) << note;
    EXPECT_NE(note.find("font25"), std::string::npos) << note;
    EXPECT_NE(note.find("icons yes"), std::string::npos) << note;
}

TEST(Glance, ANarrowPanelStacksRatherThanCollide)
{
    // Two times side by side need width. Where there is not enough, the rows go
    // back to being stacked -- overlapping digits are a worse failure than a
    // smaller font, and either beats the clipping this all started with.
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.width  = 120;
    rig.comm.height = 110;
    rig.comm.viewing(1);

    rig.run();

    const std::string note = rig.fs.readFile("glance.txt");
    EXPECT_NE(note.find("stacked"), std::string::npos) << note;
    ASSERT_EQ(rig.updates().size(), 1u);
    EXPECT_EQ(rig.updates().front().first(), "04:50");
    EXPECT_EQ(rig.updates().front().second(), "19:17");
}

TEST(Glance, APanelTooSmallToDrawInIsDeclinedButMeasuredFirst)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.height = 28;
    rig.comm.viewing(2);

    rig.run();

    EXPECT_TRUE(rig.updates().empty()) << "nothing clipped was drawn";
    // But the measurement is on the watch, which is the only way anybody finds
    // out what the panel actually was.
    const std::string note = rig.fs.readFile("glance.txt");
    EXPECT_NE(note.find("area 240x28"), std::string::npos) << note;
    EXPECT_NE(note.find("fits no"), std::string::npos) << note;
}

TEST(Glance, TheGeometryNoteIsNotRewrittenWhenNothingChanged)
{
    Rig rig;
    rig.seed("input.json", london());
    rig.at(kBeforeDawn);
    rig.comm.viewing(1);
    rig.run();

    const size_t afterFirst = rig.fs.bytesWritten;
    EXPECT_GT(afterFirst, 0u);

    // A second viewing of the same card on the same watch: the note already
    // says what it would say, and a write cycle is not spent repeating it.
    rig.comm.updates.clear();
    rig.comm.viewing(1);
    rig.run();

    EXPECT_EQ(rig.fs.bytesWritten, afterFirst);
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
