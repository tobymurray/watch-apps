#include "Fmt.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

namespace {

std::string fixed(float v, uint8_t decimals, size_t cap = 32)
{
    char buf[64] = {'\0'};
    Fmt::fixed(buf, cap < sizeof(buf) ? cap : sizeof(buf), v, decimals);
    return std::string(buf);
}

std::string integer(int32_t v, size_t cap = 32)
{
    char buf[64] = {'\0'};
    Fmt::integer(buf, cap < sizeof(buf) ? cap : sizeof(buf), v);
    return std::string(buf);
}

} // namespace

TEST(Fmt, Integers)
{
    EXPECT_EQ(integer(0), "0");
    EXPECT_EQ(integer(7), "7");
    EXPECT_EQ(integer(-7), "-7");
    EXPECT_EQ(integer(1234567), "1234567");
    EXPECT_EQ(integer(-1234567), "-1234567");
}

TEST(Fmt, TheMostNegativeIntegerDoesNotOverflowWhenNegated)
{
    EXPECT_EQ(integer(std::numeric_limits<int32_t>::min()), "-2147483648");
    EXPECT_EQ(integer(std::numeric_limits<int32_t>::max()), "2147483647");
}

TEST(Fmt, FixedPoint)
{
    EXPECT_EQ(fixed(12.34f, 2), "12.34");
    EXPECT_EQ(fixed(-12.34f, 2), "-12.34");
    EXPECT_EQ(fixed(0.0f, 2), "0.00");
    EXPECT_EQ(fixed(50.0f, 1), "50.0");
    EXPECT_EQ(fixed(359.9f, 1), "359.9");
}

TEST(Fmt, ZeroDecimalsRounds)
{
    EXPECT_EQ(fixed(12.4f, 0), "12");
    EXPECT_EQ(fixed(12.6f, 0), "13");
    EXPECT_EQ(fixed(-12.6f, 0), "-13");
    EXPECT_EQ(fixed(-0.4f, 0), "-0") << "still below zero";
}

// A negative value whose whole part is zero has to keep its sign, which is the
// case an integer-part-then-fraction formatter classically drops.
TEST(Fmt, SmallNegativesKeepTheirSign)
{
    EXPECT_EQ(fixed(-0.5f, 1), "-0.5");
    EXPECT_EQ(fixed(-0.05f, 2), "-0.05");
    EXPECT_EQ(fixed(-0.004f, 2), "-0.00") << "rounds to zero, and it was negative";
}

TEST(Fmt, FractionalLeadingZerosSurvive)
{
    EXPECT_EQ(fixed(1.05f, 2), "1.05");
    EXPECT_EQ(fixed(1.005f, 3), "1.005");
    EXPECT_EQ(fixed(0.001f, 3), "0.001");
}

TEST(Fmt, Rounding)
{
    EXPECT_EQ(fixed(1.005f, 2), "1.01");
    EXPECT_EQ(fixed(1.994f, 2), "1.99");
    EXPECT_EQ(fixed(1.996f, 2), "2.00") << "the carry has to reach the whole part";
    EXPECT_EQ(fixed(9.999f, 2), "10.00");
    EXPECT_EQ(fixed(-9.999f, 2), "-10.00");
}

TEST(Fmt, NonFiniteRendersAsAMissingValueRatherThanAsZero)
{
    EXPECT_EQ(fixed(std::nanf(""), 2), "---");
    EXPECT_EQ(fixed(std::numeric_limits<float>::infinity(), 2), "---");
    EXPECT_EQ(fixed(-std::numeric_limits<float>::infinity(), 2), "---");
}

TEST(Fmt, ValuesTooLargeToScaleRenderAsMissingRatherThanWrapping)
{
    EXPECT_EQ(fixed(1e30f, 2), "---");
    EXPECT_EQ(fixed(-1e30f, 2), "---");
}

TEST(Fmt, AShortBufferWritesNothingRatherThanATruncatedNumber)
{
    // "12.34" needs six bytes with its terminator. Five must refuse, because a
    // truncated "12.3" reads as a different measurement.
    char buf[8];
    EXPECT_EQ(Fmt::fixed(buf, 5, 12.34f, 2), 0u);
    EXPECT_STREQ(buf, "");

    EXPECT_EQ(Fmt::fixed(buf, 6, 12.34f, 2), 5u);
    EXPECT_STREQ(buf, "12.34");
}

TEST(Fmt, ACapOfZeroOrANullBufferIsSafe)
{
    char buf[4] = {'x', 'x', 'x', 'x'};
    EXPECT_EQ(Fmt::fixed(buf, 0, 1.0f, 1), 0u);
    EXPECT_EQ(buf[0], 'x') << "nothing written at all";
    EXPECT_EQ(Fmt::fixed(nullptr, 8, 1.0f, 1), 0u);
    EXPECT_EQ(Fmt::integer(nullptr, 8, 1), 0u);
}

TEST(Fmt, ScaleReportsFailureRatherThanReturningRubbish)
{
    int32_t out = 12345;

    EXPECT_FALSE(Fmt::scale(std::nanf(""), 2, out));
    EXPECT_EQ(out, 12345) << "left alone on failure";

    EXPECT_TRUE(Fmt::scale(1.5f, 1, out));
    EXPECT_EQ(out, 15);
}
