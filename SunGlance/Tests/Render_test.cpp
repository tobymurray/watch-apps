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

// -- The layout --------------------------------------------------------------
//
// The arithmetic that decides where everything goes, which exists because the
// first version's hard-coded positions clipped the bottom row on a real watch.
// These are the properties that failure would have broken.

constexpr int16_t kIconW = 24;
constexpr int16_t kIconH = 21;

/// The panel, as measured on the watch and reported by glance.txt on
/// 2026-08-19. Not the 241x88 this was first written against: the real area is
/// a third shorter, which is why the stacked arrangement could never have
/// worked here -- two lines and a caption need 65 pixels and there are 60.
constexpr int16_t kPanelW = 240;
constexpr int16_t kPanelH = 60;

Sun::Layout panel(int16_t width = kPanelW, int16_t height = kPanelH, bool wantIcons = true)
{
    return Sun::layoutFor(width, height, wantIcons, kIconW, kIconH);
}

TEST(Layout, ALineGetsMoreRoomThanItsFontSize)
{
    // The ratio SleepLab demonstrates on the watch this runs on: font 30 in a
    // 36-pixel band, font 18 in 22. Anything tighter is what clipped.
    EXPECT_EQ(Sun::lineHeightFor(30), 36);
    EXPECT_EQ(Sun::lineHeightFor(25), 30);
    EXPECT_EQ(Sun::lineHeightFor(20), 24);
    EXPECT_EQ(Sun::lineHeightFor(18), 21);
}

TEST(Layout, NothingIsDrawnUnderTheScrollIndicator)
{
    // The bug this constant exists for: the first build on a watch centred its
    // content in the reported width, which put the left icon at x=11, under the
    // scroll bar the carousel paints over the glance.
    for (int16_t width = 120; width <= 260; width += 3) {
        for (int16_t height = 40; height <= 140; height += 3) {
            const Sun::Layout out = Sun::layoutFor(width, height, true, kIconW, kIconH);
            if (!out.fits) {
                continue;
            }
            const std::string where = "at " + std::to_string(width) + "x" + std::to_string(height);
            const Sun::Box &leftMost = out.icons ? out.iconFirst : out.textFirst;
            ASSERT_GE(leftMost.x, Sun::kSafeLeftInset) << where;
            ASSERT_GE(out.textFirst.x, Sun::kSafeLeftInset) << where;
        }
    }

    // The caption is the one exception, and deliberately: it spans the full
    // width, but it is centred text whose margins the indicator passes over
    // rather than a glyph.
    EXPECT_EQ(panel().sub.x, 0);
    EXPECT_EQ(panel().sub.w, kPanelW);
}

TEST(Layout, TheRealPanelGetsBothTimesSideBySideWithIcons)
{
    const Sun::Layout out = panel();
    ASSERT_TRUE(out.fits);
    EXPECT_EQ(out.arrangement, Sun::Arrangement::SideBySide);
    EXPECT_TRUE(out.icons);

    // Same row, different columns, next event on the left.
    EXPECT_EQ(out.textFirst.y, out.textSecond.y);
    EXPECT_LT(out.textFirst.x, out.textSecond.x);
    EXPECT_EQ(out.iconFirst.y, out.iconSecond.y);
    EXPECT_LT(out.iconFirst.x, out.textFirst.x) << "each icon labels the time to its right";
    EXPECT_LT(out.textFirst.x + out.textFirst.w, out.iconSecond.x) << "and the pairs do not overlap";
}

TEST(Layout, TheRealPanelIsTooShortToStackIn)
{
    // 60 pixels: a caption at 21 and two lines at 21 each is 63 before any
    // guard, so the arrangement that shipped first could not have worked here
    // at any font size. Side by side is not a preference on this watch, it is
    // the only thing that fits.
    const Sun::Layout out = panel();
    ASSERT_TRUE(out.fits);
    EXPECT_EQ(out.arrangement, Sun::Arrangement::SideBySide);

    const int16_t stackedNeeds = 2 * Sun::lineHeightFor(18) + Sun::lineHeightFor(18);
    EXPECT_GT(stackedNeeds, kPanelH) << "if this ever passes, stacking became possible";
}

TEST(Layout, ANarrowPanelStacksInsteadOfColliding)
{
    // Two times side by side need width. Where there is not enough, stacking is
    // the answer -- overlapping digits are a worse failure than a smaller font.
    const Sun::Layout out = panel(120, 100);
    ASSERT_TRUE(out.fits);
    EXPECT_EQ(out.arrangement, Sun::Arrangement::Stacked);
    EXPECT_LT(out.textFirst.y, out.textSecond.y);
    EXPECT_EQ(out.textFirst.x, out.textSecond.x);
}

TEST(Layout, NothingOverlapsAnythingElse)
{
    for (int16_t width = 100; width <= 260; width += 3) {
        for (int16_t height = 40; height <= 140; height += 3) {
            const Sun::Layout out = Sun::layoutFor(width, height, true, kIconW, kIconH);
            if (!out.fits) {
                continue;
            }

            const std::string where = "at " + std::to_string(width) + "x" + std::to_string(height);

            // Everything inside the panel.
            ASSERT_GE(out.textFirst.x, 0) << where;
            ASSERT_LE(out.textSecond.x + out.textSecond.w, width) << where;
            ASSERT_LE(out.sub.y + out.sub.h, height) << where;
            ASSERT_LE(out.textFirst.y + out.textFirst.h, out.sub.y) << where;
            ASSERT_LE(out.textSecond.y + out.textSecond.h, out.sub.y) << where;

            // The two times never sit on top of one another.
            const bool sameRow = (out.textFirst.y == out.textSecond.y);
            if (sameRow) {
                ASSERT_LE(out.textFirst.x + out.textFirst.w, out.textSecond.x) << where;
            } else {
                ASSERT_LE(out.textFirst.y + out.textFirst.h, out.textSecond.y) << where;
            }

            if (out.icons) {
                ASSERT_LE(out.iconFirst.x + out.iconFirst.w, out.textFirst.x) << where;
                ASSERT_LE(out.iconSecond.x + out.iconSecond.w, out.textSecond.x) << where;
                if (sameRow) {
                    ASSERT_LE(out.textFirst.x + out.textFirst.w, out.iconSecond.x) << where;
                }
                ASSERT_LE(out.iconFirst.y + out.iconFirst.h, out.sub.y) << where;
                ASSERT_LE(out.iconSecond.y + out.iconSecond.h, out.sub.y) << where;
            }
        }
    }
}

TEST(Layout, TheFontFitsTheBoxItIsPutIn)
{
    // The invariant the clipped row broke. Where nothing can hold it -- a panel
    // too small at any size -- the answer is `fits` false and no glance at all,
    // rather than a size that will be cut off.
    for (int16_t width = 100; width <= 260; width += 3) {
        for (int16_t height = 20; height <= 140; height += 3) {
            const Sun::Layout out = Sun::layoutFor(width, height, true, kIconW, kIconH);
            if (!out.fits) {
                continue;
            }
            const int16_t line = Sun::lineHeightFor(out.rowFontPx);
            ASSERT_EQ(out.textFirst.h, line) << width << "x" << height;
            ASSERT_GE(out.textFirst.w, Sun::timeWidthFor(out.rowFontPx)) << width << "x" << height;
            ASSERT_GE(out.sub.h, Sun::lineHeightFor(18)) << width << "x" << height;
        }
    }
}

TEST(Layout, IconsAreKeptEvenAtTheCostOfAFontSize)
{
    // They are what lets the rows carry no words at all, so they outrank the
    // digits being one size bigger.
    const Sun::Layout withIcons = panel(kPanelW, kPanelH, true);
    const Sun::Layout without   = panel(kPanelW, kPanelH, false);
    ASSERT_TRUE(withIcons.fits);
    ASSERT_TRUE(without.fits);
    EXPECT_TRUE(withIcons.icons);
    EXPECT_FALSE(without.icons);
    EXPECT_GE(without.rowFontPx, withIcons.rowFontPx) << "dropping them can only help the font";
}

TEST(Layout, ATimeStandingOnItsOwnIsCentredAndOneBesideAnIconIsNot)
{
    EXPECT_FALSE(panel(kPanelW, kPanelH, true).textCentred);
    EXPECT_TRUE(panel(kPanelW, kPanelH, false).textCentred);
}

TEST(Layout, APanelTooSmallToDrawInSaysSo)
{
    // Too short for a caption plus any line at all.
    EXPECT_FALSE(Sun::layoutFor(240, 28, true, kIconW, kIconH).fits);
    // Too narrow for even one time, once the scroll bar's inset is taken off.
    EXPECT_FALSE(Sun::layoutFor(40, 88, true, kIconW, kIconH).fits);

    EXPECT_TRUE(panel().fits);
    EXPECT_TRUE(panel(240, 88).fits);
}

TEST(Layout, SideBySideRescuesAPanelTooShortToStackIn)
{
    // A panel with room for one line and a caption cannot hold two rows at any
    // size -- but it can hold two times beside each other, which is the point
    // of the arrangement. Under the old stacked-only layout this was a declined
    // glance.
    const Sun::Layout out = panel(240, 46);
    ASSERT_TRUE(out.fits);
    EXPECT_EQ(out.arrangement, Sun::Arrangement::SideBySide);
    EXPECT_EQ(out.textFirst.y, out.textSecond.y);
    EXPECT_EQ(out.rowFontPx, 18) << "the smallest font, but a whole one";

    // Two pixels shorter and the icons no longer have a band to sit in, so they
    // go and the words come back rather than the glance going away.
    const Sun::Layout shorter = panel(240, 44);
    ASSERT_TRUE(shorter.fits);
    EXPECT_FALSE(shorter.icons);
    EXPECT_TRUE(shorter.textCentred);
}

TEST(Layout, AnAbsurdPanelIsNotACrash)
{
    for (int16_t size : { static_cast<int16_t>(-1), static_cast<int16_t>(0),
                          static_cast<int16_t>(1), static_cast<int16_t>(8) }) {
        const Sun::Layout out = Sun::layoutFor(size, size, true, kIconW, kIconH);
        EXPECT_FALSE(out.fits) << size;
        EXPECT_FALSE(out.icons) << size;
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
