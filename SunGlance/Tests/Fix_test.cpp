/**
 * Host tests for reading a coordinate out of text somebody typed.
 *
 * Every case here is a real way a person writes a latitude, and the ones that
 * are rejected are rejected because accepting them would have produced a
 * plausible wrong answer rather than an obvious one.
 */

#include <gtest/gtest.h>

#include "Fix.hpp"

namespace {

constexpr double kLat = 90.0;
constexpr double kLon = 180.0;

double parsed(const char *text, double limit = kLat)
{
    double out = -999.0;
    EXPECT_TRUE(Sun::parseDegrees(text, limit, out)) << text;
    return out;
}

void rejected(const char *text, double limit = kLat)
{
    double out = -999.0;
    EXPECT_FALSE(Sun::parseDegrees(text, limit, out)) << text;
    EXPECT_DOUBLE_EQ(out, -999.0) << text;  // and left alone
}

TEST(ParseDegrees, ReadsWhatKiraWrites)
{
    EXPECT_NEAR(parsed("45.4215"), 45.4215, 1e-9);
    EXPECT_NEAR(parsed("-75.6972", kLon), -75.6972, 1e-9);
    EXPECT_NEAR(parsed("0"), 0.0, 1e-12);
    EXPECT_NEAR(parsed("0.0"), 0.0, 1e-12);
    EXPECT_NEAR(parsed("90"), 90.0, 1e-12);
    EXPECT_NEAR(parsed("-90"), -90.0, 1e-12);
    EXPECT_NEAR(parsed("+51.5"), 51.5, 1e-9);
    EXPECT_NEAR(parsed("51."), 51.0, 1e-12);
    EXPECT_NEAR(parsed("-.5"), -0.5, 1e-12);
}

TEST(ParseDegrees, RejectsTheEuropeanDecimalComma)
{
    // The expensive one: a lenient reader stops at the comma, gets 45, and
    // moves sunrise by half an hour without anything looking wrong.
    rejected("45,4215");
}

TEST(ParseDegrees, RejectsCompassLetters)
{
    // Equally expensive in the other direction: 45.4215S is a southern
    // latitude, and a reader that stops at the S returns a northern one.
    rejected("45.4215N");
    rejected("45.4215S");
    rejected("75.6972W", kLon);
}

TEST(ParseDegrees, RejectsDegreesMinutesSeconds)
{
    rejected("45 25 17");
    rejected("45:25:17");
    rejected("45d25m17s");
}

TEST(ParseDegrees, RejectsWhatIsNotANumber)
{
    rejected("");
    rejected("-");
    rejected(".");
    rejected("+");
    rejected("abc");
    rejected(nullptr);
}

TEST(ParseDegrees, RejectsTheThingsStrtodWouldHaveAccepted)
{
    rejected("1e2");
    rejected("nan");
    rejected("inf");
    rejected("  45.4");
    rejected("45.4  ");
    rejected("0x2D");
}

TEST(ParseDegrees, RejectsOffTheGlobe)
{
    rejected("90.1");
    rejected("-90.1");
    rejected("181", kLon);
    rejected("-181", kLon);
    // Not caught, and cannot be: -75.6972 is a perfectly good latitude, so
    // swapping the two fields on the form produces a real place in the Southern
    // Ocean rather than an error. The `zoneAgreesWithLongitude` check in
    // Schedule.hpp is the only thing that notices, and only when the swap also
    // moves the longitude out of the watch's time zone.
    double swapped = 0.0;
    EXPECT_TRUE(Sun::parseDegrees("-75.6972", kLat, swapped));
}

TEST(ParseDegrees, RejectsSomethingLongEnoughToBeAnAttack)
{
    rejected("45.421512345678901234567890");
    rejected("00000000000000000000000045");
}

TEST(Fix, NowhereIsAStateAndNotAPlace)
{
    // Without this, an unconfigured watch is at (0, 0) -- the Gulf of Guinea,
    // where the sun rises at six all year round and the glance looks fine.
    Sun::Fix fix;
    EXPECT_FALSE(fix.has());
    EXPECT_EQ(fix.source, Sun::Fix::Source::None);

    fix.source = Sun::Fix::Source::Config;
    fix.latDeg = 45.4215;
    fix.lonDeg = -75.6972;
    EXPECT_TRUE(fix.has());
    EXPECT_EQ(fix.utc, -1) << "a configured home does not age";
    EXPECT_DOUBLE_EQ(fix.position().latDeg, 45.4215);
    EXPECT_DOUBLE_EQ(fix.position().lonDeg, -75.6972);
}

} // namespace
