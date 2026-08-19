/**
 * Host tests for the wording.
 *
 * Two things are being checked, and the second is the one that bites.
 *
 * **What it says.** Every state the glance can be in has a screen here, written
 * out in full, so changing the wording means changing a test on purpose rather
 * than discovering on a Tuesday that the caption has said the wrong thing since
 * March.
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
constexpr size_t kSubBudget   = 22;
constexpr size_t kValueBudget = 12;  // font 30, so fewer characters for more pixels
constexpr size_t kTitleBudget = 14;

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
    EXPECT_LT(std::strlen(lines.title), Sun::kLineBytes) << lines.title;
    EXPECT_LT(std::strlen(lines.value), Sun::kLineBytes) << lines.value;
    EXPECT_LT(std::strlen(lines.sub), Sun::kLineBytes) << lines.sub;

    EXPECT_LE(std::strlen(lines.title), kTitleBudget) << lines.title;
    EXPECT_LE(std::strlen(lines.value), kValueBudget) << lines.value;
    EXPECT_LE(std::strlen(lines.sub), kSubBudget) << lines.sub;
}

Sun::View rising()
{
    Sun::View v;
    v.kind        = Sun::EventKind::Rise;
    v.when        = at(6, 12);
    v.other       = at(20, 41);
    v.secondsAway = 72 * 60;
    return v;
}

Sun::View setting()
{
    Sun::View v;
    v.kind        = Sun::EventKind::Set;
    v.when        = at(20, 41);
    v.other       = at(6, 12);
    v.secondsAway = 185 * 60;
    return v;
}

TEST(Render, BeforeSunrise)
{
    const Sun::Lines lines = Sun::render(rising());
    EXPECT_STREQ(lines.title, "sunrise");
    EXPECT_STREQ(lines.value, "06:12");
    EXPECT_STREQ(lines.sub, "in 1h12m, sets 20:41");
    EXPECT_FALSE(lines.caution);
    expectFits(lines);
}

TEST(Render, DuringTheDay)
{
    const Sun::Lines lines = Sun::render(setting());
    EXPECT_STREQ(lines.title, "sunset");
    EXPECT_STREQ(lines.value, "20:41");
    // Past tense, because that sunrise has already happened.
    EXPECT_STREQ(lines.sub, "in 3h05m, rose 06:12");
    expectFits(lines);
}

TEST(Render, AfterSunsetTheDayIsNamed)
{
    Sun::View v = rising();
    v.when        = at(6, 14);
    v.other       = Sun::Clock {};
    v.nextDay     = true;
    v.secondsAway = (9 * 60 + 33) * 60;

    const Sun::Lines lines = Sun::render(v);
    EXPECT_STREQ(lines.title, "sunrise");
    EXPECT_STREQ(lines.value, "06:14");
    EXPECT_STREQ(lines.sub, "tomorrow, in 9h33m");
    expectFits(lines);
}

TEST(Render, TheCountdownIsMinutesAtMost)
{
    Sun::View v = rising();
    v.other = Sun::Clock {};

    v.secondsAway = 0;
    EXPECT_STREQ(Sun::render(v).sub, "now");
    v.secondsAway = 59;
    EXPECT_STREQ(Sun::render(v).sub, "now");
    v.secondsAway = 60;
    EXPECT_STREQ(Sun::render(v).sub, "in 1m");
    v.secondsAway = 59 * 60 + 59;
    EXPECT_STREQ(Sun::render(v).sub, "in 59m");
    v.secondsAway = 60 * 60;
    EXPECT_STREQ(Sun::render(v).sub, "in 1h00m");
    v.secondsAway = (13 * 60 + 5) * 60;
    EXPECT_STREQ(Sun::render(v).sub, "in 13h05m");
}

TEST(Render, TheLongestRealCaptionStillFits)
{
    // The widest thing this screen can be asked to say: a two-digit hour
    // countdown next to a paired time.
    Sun::View v = setting();
    v.when        = at(23, 59);
    v.other       = at(11, 11);
    v.secondsAway = (23 * 60 + 59) * 60;

    const Sun::Lines lines = Sun::render(v);
    EXPECT_STREQ(lines.sub, "in 23h59m, rose 11:11");
    expectFits(lines);
}

TEST(Render, MidnightSunIsNotAMissingTime)
{
    Sun::View v;
    v.kind = Sun::EventKind::MidnightSun;

    const Sun::Lines lines = Sun::render(v);
    EXPECT_STREQ(lines.title, "midnight sun");
    EXPECT_STREQ(lines.value, "no sunset");
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

    const Sun::Lines lines = Sun::render(v);
    EXPECT_STREQ(lines.title, "polar night");
    EXPECT_STREQ(lines.value, "no sunrise");
    EXPECT_STREQ(lines.sub, "the sun stays down");
    EXPECT_FALSE(lines.caution);
    expectFits(lines);
}

TEST(Render, NoPositionShowsNoTime)
{
    Sun::View v;
    v.trouble = Sun::Trouble::NoPosition;

    const Sun::Lines lines = Sun::render(v);
    EXPECT_STREQ(lines.value, "--");
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
    EXPECT_STREQ(Sun::render(v).sub, "input.json rejected");

    v.trouble = Sun::Trouble::NoClock;
    EXPECT_STREQ(Sun::render(v).sub, "clock not set");

    v.trouble = Sun::Trouble::NoPosition;
    EXPECT_STREQ(Sun::render(v).sub, "no position set");
}

TEST(Render, TroubleOutranksAnythingElseTheViewCarries)
{
    // A stale schedule left in the view must never reach the screen through a
    // state that means "there is no position to compute one from".
    Sun::View v = rising();
    v.trouble = Sun::Trouble::NoPosition;

    const Sun::Lines lines = Sun::render(v);
    EXPECT_STREQ(lines.value, "--");
    EXPECT_STRNE(lines.value, "06:12");
}

TEST(Render, ATimeZoneThatDisagreesReplacesTheConvenience)
{
    Sun::View v = rising();
    v.zoneSuspect = true;

    const Sun::Lines lines = Sun::render(v);
    // The time is still shown -- it is not wrong, it is just not local.
    EXPECT_STREQ(lines.value, "06:12");
    EXPECT_STREQ(lines.sub, "times are for home");
    EXPECT_TRUE(lines.caution);
    expectFits(lines);
}

TEST(Render, AnInvalidClockDrawsDashesAndNotZeros)
{
    Sun::View v = rising();
    v.when  = Sun::Clock {};
    v.other = Sun::Clock {};

    EXPECT_STREQ(Sun::render(v).value, "--:--");
}

} // namespace
