/**
 * Host tests for the wording and the shape of the screen.
 *
 * Three things are being checked, and the last is the one that bites.
 *
 * **What it says.** Every state the glance can be in has a screen here, written
 * out in full, so changing the wording means changing a test on purpose rather
 * than discovering on a Tuesday that the caption has said the wrong thing since
 * March.
 *
 * **Which icon goes where.** The two rows are the next event and the one after
 * it, and the icons follow them round: before dawn the sunrise is on top, an
 * hour later the sunset is. Getting that backwards is invisible in the code and
 * obvious on the wrist.
 *
 * **Whether it fits.** `ControlText::setText()` ignores a string that does not
 * fit its buffer -- no truncation, no error, the control simply keeps what it
 * had. The 32-byte cap is therefore checked here on every line of every state.
 * The tighter character budget is an *estimate* of the panel's width in
 * Poppins at these sizes and has not been confirmed against hardware; it is
 * here so a caption that grows past what is known to fit fails at a desk.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Render.hpp"

namespace {

/// Roughly what the 225-pixel-wide caption band holds in Poppins SemiBold 18,
/// at about 10 pixels per lowercase character. An estimate, deliberately
/// conservative, and unverified on a watch -- see the file comment.
constexpr size_t kSubBudget = 22;
/// The rows are font 30 next to a 24-pixel icon, so they hold far less. "04:50"
/// is five; "rise 04:50" is the widest thing the no-icon fallback produces.
constexpr size_t kRowBudget = 12;

Sun::Clock at(int hour, int minute)
{
    Sun::Clock c;
    c.hour   = hour;
    c.minute = minute;
    c.valid  = true;
    return c;
}

void expectFits(const Sun::Lines &lines)
{
    EXPECT_LT(std::strlen(lines.first), Sun::kLineBytes) << lines.first;
    EXPECT_LT(std::strlen(lines.second), Sun::kLineBytes) << lines.second;
    EXPECT_LT(std::strlen(lines.message), Sun::kLineBytes) << lines.message;
    EXPECT_LT(std::strlen(lines.sub), Sun::kLineBytes) << lines.sub;

    EXPECT_LE(std::strlen(lines.first), kRowBudget) << lines.first;
    EXPECT_LE(std::strlen(lines.second), kRowBudget) << lines.second;
    EXPECT_LE(std::strlen(lines.message), kRowBudget) << lines.message;
    EXPECT_LE(std::strlen(lines.sub), kSubBudget) << lines.sub;
}

/// Before dawn: sunrise first, then today's sunset.
Sun::View rising()
{
    Sun::View v;
    v.kind        = Sun::EventKind::Rise;
    v.first       = at(4, 50);
    v.second      = at(19, 17);
    v.secondsAway = 72 * 60;
    return v;
}

/// Daylight: sunset first, then tomorrow's sunrise.
Sun::View setting()
{
    Sun::View v;
    v.kind        = Sun::EventKind::Set;
    v.first       = at(19, 17);
    v.second      = at(4, 52);
    v.secondsAway = 185 * 60;
    return v;
}

TEST(Render, BeforeSunriseTheSunriseIsOnTop)
{
    const Sun::Lines lines = Sun::render(rising(), true);
    EXPECT_TRUE(lines.rows);
    EXPECT_EQ(lines.firstKind, Sun::EventKind::Rise);
    EXPECT_STREQ(lines.first, "04:50");
    EXPECT_STREQ(lines.second, "19:17");
    EXPECT_STREQ(lines.sub, "in 1h12m");
    EXPECT_FALSE(lines.caution);
    expectFits(lines);
}

TEST(Render, DuringTheDayTheSunsetIsOnTop)
{
    const Sun::Lines lines = Sun::render(setting(), true);
    EXPECT_TRUE(lines.rows);
    EXPECT_EQ(lines.firstKind, Sun::EventKind::Set);
    EXPECT_STREQ(lines.first, "19:17");
    // Tomorrow's sunrise, not this morning's: nothing on the screen is in the
    // past.
    EXPECT_STREQ(lines.second, "04:52");
    EXPECT_STREQ(lines.sub, "in 3h05m");
    expectFits(lines);
}

TEST(Render, TheRowsCarryNoWordsWhenThereAreIcons)
{
    // The point of the icons: the panel is 241 pixels wide and "sunrise" spends
    // a third of it saying what the picture says.
    const Sun::Lines lines = Sun::render(rising(), true);
    EXPECT_EQ(std::string(lines.first).find("rise"), std::string::npos);
    EXPECT_EQ(std::string(lines.second).find("set"), std::string::npos);
}

TEST(Render, WithoutIconsTheWordsComeBack)
{
    const Sun::Lines lines = Sun::render(rising(), false);
    EXPECT_TRUE(lines.rows);
    EXPECT_STREQ(lines.first, "rise 04:50");
    EXPECT_STREQ(lines.second, "set 19:17");
    expectFits(lines);

    const Sun::Lines evening = Sun::render(setting(), false);
    EXPECT_STREQ(evening.first, "set 19:17");
    EXPECT_STREQ(evening.second, "rise 04:52");
    expectFits(evening);
}

TEST(Render, AfterSunsetTheDayIsNamed)
{
    Sun::View v = setting();
    v.kind        = Sun::EventKind::Rise;
    v.first       = at(4, 52);
    v.second      = at(19, 14);
    v.nextDay     = true;
    v.secondsAway = (9 * 60 + 34) * 60;

    const Sun::Lines lines = Sun::render(v, true);
    EXPECT_STREQ(lines.first, "04:52");
    EXPECT_STREQ(lines.second, "19:14");
    EXPECT_STREQ(lines.sub, "tomorrow, in 9h34m");
    expectFits(lines);
}

TEST(Render, ASecondEventThereIsNoneOfLeavesTheRowEmpty)
{
    // The last sunset before the midnight sun: there is no next sunrise, and an
    // empty string is what tells the service to hide the row rather than draw a
    // time that is not coming.
    Sun::View v = setting();
    v.second = Sun::Clock {};

    const Sun::Lines lines = Sun::render(v, true);
    EXPECT_TRUE(lines.rows);
    EXPECT_STREQ(lines.first, "19:17");
    EXPECT_STREQ(lines.second, "");
    expectFits(lines);
}

TEST(Render, TheCountdownIsMinutesAtMost)
{
    Sun::View v = rising();

    v.secondsAway = 0;
    EXPECT_STREQ(Sun::render(v, true).sub, "now");
    v.secondsAway = 59;
    EXPECT_STREQ(Sun::render(v, true).sub, "now");
    v.secondsAway = 60;
    EXPECT_STREQ(Sun::render(v, true).sub, "in 1m");
    v.secondsAway = 59 * 60 + 59;
    EXPECT_STREQ(Sun::render(v, true).sub, "in 59m");
    v.secondsAway = 60 * 60;
    EXPECT_STREQ(Sun::render(v, true).sub, "in 1h00m");
    v.secondsAway = (13 * 60 + 5) * 60;
    EXPECT_STREQ(Sun::render(v, true).sub, "in 13h05m");
}

TEST(Render, MidnightSunIsNotAMissingTime)
{
    Sun::View v;
    v.kind = Sun::EventKind::MidnightSun;

    const Sun::Lines lines = Sun::render(v, true);
    EXPECT_FALSE(lines.rows) << "there are no times, so there are no rows";
    EXPECT_STREQ(lines.message, "no sunset");
    EXPECT_STREQ(lines.sub, "the sun stays up");
    // Not a caveat: this is what the sky is doing, and it is the correct
    // answer rather than a shortfall.
    EXPECT_FALSE(lines.caution);
    expectFits(lines);
}

TEST(Render, PolarNightIsNotAMissingTime)
{
    Sun::View v;
    v.kind = Sun::EventKind::PolarNight;

    const Sun::Lines lines = Sun::render(v, true);
    EXPECT_FALSE(lines.rows);
    EXPECT_STREQ(lines.message, "no sunrise");
    EXPECT_STREQ(lines.sub, "the sun stays down");
    EXPECT_FALSE(lines.caution);
    expectFits(lines);
}

TEST(Render, NoPositionShowsNoTime)
{
    Sun::View v;
    v.trouble = Sun::Trouble::NoPosition;

    const Sun::Lines lines = Sun::render(v, true);
    EXPECT_FALSE(lines.rows);
    EXPECT_STREQ(lines.message, "--");
    EXPECT_STREQ(lines.sub, "no position set");
    EXPECT_TRUE(lines.caution);
    expectFits(lines);
}

TEST(Render, EachTroubleSaysWhichOneItIs)
{
    // Collapsing these to one message is how somebody ends up looking for a
    // setting they already wrote, or rewriting a file that was never the
    // problem.
    Sun::View v;

    v.trouble = Sun::Trouble::BadConfig;
    EXPECT_STREQ(Sun::render(v, true).sub, "input.json rejected");

    v.trouble = Sun::Trouble::NoClock;
    EXPECT_STREQ(Sun::render(v, true).sub, "clock not set");

    v.trouble = Sun::Trouble::NoPosition;
    EXPECT_STREQ(Sun::render(v, true).sub, "no position set");
}

TEST(Render, TroubleOutranksAnythingElseTheViewCarries)
{
    // A stale schedule left in the view must never reach the screen through a
    // state that means "there is no position to compute one from".
    Sun::View v = rising();
    v.trouble = Sun::Trouble::NoPosition;

    const Sun::Lines lines = Sun::render(v, true);
    EXPECT_FALSE(lines.rows);
    EXPECT_STREQ(lines.message, "--");
    EXPECT_STREQ(lines.first, "");
}

// -- The bands ---------------------------------------------------------------
//
// The layout arithmetic, which exists because the first version's hard-coded
// bands clipped the bottom row on a real watch. These are the properties that
// failure would have broken.

constexpr int16_t kIconHeight = 21;

TEST(Bands, ALineGetsMoreRoomThanItsFontSize)
{
    // The ratio SleepLab demonstrates on the watch this runs on: font 30 in a
    // 36-pixel band, font 18 in 22. Anything tighter is what clipped.
    EXPECT_EQ(Sun::lineHeightFor(30), 36);
    EXPECT_EQ(Sun::lineHeightFor(25), 30);
    EXPECT_EQ(Sun::lineHeightFor(20), 24);
    EXPECT_EQ(Sun::lineHeightFor(18), 21);
}

TEST(Bands, TheFontFitsTheRowItIsPutIn)
{
    // The invariant the clipped row broke. Where it cannot hold -- a panel too
    // short for two lines and a caption at any size -- the answer is `fits`
    // false and no glance at all, rather than a size that will be cut off.
    for (int16_t height = 20; height <= 140; height++) {
        const Sun::Bands bands = Sun::bandsFor(height, true, kIconHeight);
        if (!bands.fits) {
            continue;
        }
        ASSERT_LE(Sun::lineHeightFor(bands.rowFontPx), bands.rowH)
            << "height " << height << " gave font " << bands.rowFontPx
            << " a row of " << bands.rowH;
        ASSERT_LE(Sun::lineHeightFor(18), bands.subH) << height;
    }
}

TEST(Bands, APanelTooShortToDrawInSaysSo)
{
    EXPECT_FALSE(Sun::bandsFor(44, true, kIconHeight).fits);
    EXPECT_FALSE(Sun::bandsFor(56, true, kIconHeight).fits);
    // And the panel this app was written against is fine.
    EXPECT_TRUE(Sun::bandsFor(88, true, kIconHeight).fits);
    EXPECT_TRUE(Sun::bandsFor(84, true, kIconHeight).fits);

    // Once it fits, it keeps fitting: a taller panel never becomes undrawable.
    bool seenFitting = false;
    for (int16_t height = 20; height <= 200; height++) {
        const bool fits = Sun::bandsFor(height, true, kIconHeight).fits;
        if (fits) {
            seenFitting = true;
        }
        ASSERT_TRUE(!seenFitting || fits) << "height " << height;
    }
}

TEST(Bands, NothingIsDrawnPastTheBottomOfThePanel)
{
    for (int16_t height = 20; height <= 140; height++) {
        const Sun::Bands bands = Sun::bandsFor(height, true, kIconHeight);
        ASSERT_GE(bands.rowAY, 0) << height;
        ASSERT_EQ(bands.rowBY, bands.rowAY + bands.rowH) << height;
        ASSERT_LE(bands.subY + bands.subH, height) << height;
        ASSERT_LE(bands.rowBY + bands.rowH, bands.subY) << height;
    }
}

TEST(Bands, TheCaptionIsNeverSacrificedForTheRows)
{
    // It is the line that says why there is no time at all. A screen that
    // cannot show it is worse than one whose digits are a size smaller.
    for (int16_t height = 40; height <= 140; height++) {
        const Sun::Bands bands = Sun::bandsFor(height, true, kIconHeight);
        ASSERT_GE(bands.subH, 12) << height;
    }
}

TEST(Bands, ABiggerPanelGetsABiggerFont)
{
    int16_t previous = 0;
    for (int16_t height = 40; height <= 140; height++) {
        const Sun::Bands bands = Sun::bandsFor(height, true, kIconHeight);
        ASSERT_GE(bands.rowFontPx, previous) << "font shrank at height " << height;
        previous = bands.rowFontPx;
    }
    // And the panel this was written against does not get the size that clipped.
    EXPECT_EQ(Sun::bandsFor(88, true, kIconHeight).rowFontPx, 25);
    EXPECT_EQ(Sun::bandsFor(88, true, kIconHeight).rowH, 32);
}

TEST(Bands, ARowTooShortForAnIconLosesIt)
{
    // The icons are a fixed 21 pixels and cannot shrink with the font, so a
    // short panel drops them rather than letting them overlap the row beneath.
    EXPECT_TRUE(Sun::bandsFor(88, true, kIconHeight).icons);
    EXPECT_FALSE(Sun::bandsFor(56, true, kIconHeight).icons);
    // And a caller with no controls to spare for them never gets them.
    EXPECT_FALSE(Sun::bandsFor(88, false, kIconHeight).icons);
}

TEST(Bands, AnAbsurdPanelIsNotACrash)
{
    for (int16_t height : { static_cast<int16_t>(-1), static_cast<int16_t>(0),
                            static_cast<int16_t>(1), static_cast<int16_t>(8) }) {
        const Sun::Bands bands = Sun::bandsFor(height, true, kIconHeight);
        EXPECT_GE(bands.rowH, 0) << height;
        EXPECT_FALSE(bands.icons) << height;
    }
}

TEST(Render, ATimeZoneThatDisagreesReplacesTheCountdown)
{
    Sun::View v = rising();
    v.zoneSuspect = true;

    const Sun::Lines lines = Sun::render(v, true);
    // The times are still shown -- they are not wrong, they are just not local.
    EXPECT_STREQ(lines.first, "04:50");
    EXPECT_STREQ(lines.second, "19:17");
    EXPECT_STREQ(lines.sub, "times are for home");
    EXPECT_TRUE(lines.caution);
    expectFits(lines);
}

} // namespace
