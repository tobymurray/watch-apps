#include "Mag/Heading.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using Mag::Result;
using Mag::UpConvention;
using Mag::Vec3;

constexpr float kDegToRad = static_cast<float>(M_PI) / 180.0f;

/// The device lies flat, face up, so up is +Z under the default convention.
constexpr Vec3 kFlat{0.0f, 0.0f, 1.0f};

/// A field of the given total strength, at the given dip, whose horizontal part
/// points along the device's +Y axis. This is the "heading zero" field.
Vec3 fieldAtDip(float strength, float dipDeg)
{
    const float dip = dipDeg * kDegToRad;
    return Vec3{0.0f, strength * std::cos(dip), -strength * std::sin(dip)};
}

/// Rotate a device-frame vector as the device itself is turned clockwise about
/// its own +Z axis, viewed from above. Turning the device clockwise moves every
/// world-fixed vector counterclockwise in the device frame, which is the sign
/// that is easy to get backwards, so it is written once here and reused.
Vec3 deviceTurnedClockwise(const Vec3& v, float degrees)
{
    const float a = degrees * kDegToRad;
    return Vec3{v.x * std::cos(a) - v.y * std::sin(a),
                v.x * std::sin(a) + v.y * std::cos(a),
                v.z};
}

} // namespace

TEST(Heading, PointingAtMagneticNorthReadsZero)
{
    const Result r = Mag::compute(fieldAtDip(50.0f, 0.0f), kFlat);

    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.headingDeg, 0.0f, 0.01f);
    EXPECT_STREQ(Mag::cardinal(r.headingDeg), "N");
}

TEST(Heading, TurningTheWatchClockwiseIncreasesTheHeading)
{
    const Vec3 north = fieldAtDip(50.0f, 0.0f);

    struct Case {
        float turn;
        float expected;
        const char* cardinal;
    };
    const Case cases[] = {
        {  0.0f,   0.0f, "N"},
        { 45.0f,  45.0f, "NE"},
        { 90.0f,  90.0f, "E"},
        {180.0f, 180.0f, "S"},
        {270.0f, 270.0f, "W"},
        {359.0f, 359.0f, "N"},
    };

    for (const Case& c : cases) {
        const Result r = Mag::compute(deviceTurnedClockwise(north, c.turn), kFlat);
        ASSERT_TRUE(r.valid) << "turn " << c.turn;
        EXPECT_NEAR(r.headingDeg, c.expected, 0.05f) << "turn " << c.turn;
        EXPECT_STREQ(Mag::cardinal(r.headingDeg), c.cardinal) << "turn " << c.turn;
    }
}

// The reason this module exists rather than the tutorial's atan2f(y, x).
//
// At 70 degrees of dip the vertical component is nearly three times the
// horizontal, so tilting the wrist leaks a large signal into the horizontal
// axes. The naive heading swings by tens of degrees; the compensated one does
// not move.
TEST(Heading, TiltDoesNotMoveTheHeadingAndWouldWreckTheNaiveOne)
{
    constexpr float kDip      = 70.0f;
    constexpr float kStrength = 50.0f;
    const Vec3      field     = fieldAtDip(kStrength, kDip);

    float worstCompensated = 0.0f;
    float worstNaive       = 0.0f;

    // Roll the watch about its own +Y axis, which is the motion of turning a
    // wrist over while still pointing the same way.
    for (float rollDeg = -30.0f; rollDeg <= 30.0f; rollDeg += 5.0f) {
        const float roll = rollDeg * kDegToRad;

        // Rotating the device about +Y by roll moves world-fixed vectors the
        // other way in the device frame.
        auto rotateAboutY = [&](const Vec3& v) {
            return Vec3{v.x * std::cos(roll) + v.z * std::sin(roll),
                        v.y,
                        -v.x * std::sin(roll) + v.z * std::cos(roll)};
        };

        const Vec3 m = rotateAboutY(field);
        const Vec3 a = rotateAboutY(kFlat);

        const Result r = Mag::compute(m, a);
        ASSERT_TRUE(r.valid) << "roll " << rollDeg;

        // Wrap the error into [-180, 180] so 359.9 counts as near zero.
        float err = r.headingDeg;
        if (err > 180.0f) {
            err -= 360.0f;
        }
        worstCompensated = std::max(worstCompensated, std::fabs(err));

        // What the tutorial would report from the same samples.
        float naive = std::atan2(m.y, m.x) * (180.0f / static_cast<float>(M_PI));
        if (naive < 0.0f) {
            naive += 360.0f;
        }
        // Its zero is at +X rather than +Y, so compare against its own
        // untilted reading rather than against north.
        float naiveErr = naive - 90.0f;
        if (naiveErr > 180.0f) {
            naiveErr -= 360.0f;
        }
        if (naiveErr < -180.0f) {
            naiveErr += 360.0f;
        }
        worstNaive = std::max(worstNaive, std::fabs(naiveErr));
    }

    EXPECT_LT(worstCompensated, 0.1f);
    EXPECT_GT(worstNaive, 20.0f) << "the naive heading is supposed to be bad here";
}

TEST(Heading, DipIsRecoveredAndIsSignedDownwardPositive)
{
    for (float dip = -80.0f; dip <= 80.0f; dip += 10.0f) {
        const Result r = Mag::compute(fieldAtDip(50.0f, dip), kFlat);
        ASSERT_TRUE(r.valid) << "dip " << dip;
        EXPECT_NEAR(r.dipDeg, dip, 0.05f) << "dip " << dip;
    }
}

TEST(Heading, DipIsIndependentOfTheUnitScale)
{
    const Result microtesla = Mag::compute(fieldAtDip(50.0f, 65.0f), kFlat);
    const Result gauss      = Mag::compute(fieldAtDip(0.5f, 65.0f), kFlat);
    const Result counts     = Mag::compute(fieldAtDip(25000.0f, 65.0f), kFlat);

    ASSERT_TRUE(microtesla.valid);
    ASSERT_TRUE(gauss.valid);
    ASSERT_TRUE(counts.valid);

    EXPECT_NEAR(microtesla.dipDeg, 65.0f, 0.05f);
    EXPECT_NEAR(gauss.dipDeg, 65.0f, 0.05f);
    EXPECT_NEAR(counts.dipDeg, 65.0f, 0.05f);
}

TEST(Heading, AVerticalFieldHasNoHeadingButStillReportsItsDip)
{
    const Result r = Mag::compute(Vec3{0.0f, 0.0f, -50.0f}, kFlat);

    EXPECT_FALSE(r.valid);
    EXPECT_NEAR(r.dipDeg, 90.0f, 0.01f);
}

TEST(Heading, TwelveOClockStraightUpHasNoHeading)
{
    // Watch on its edge with +Y pointing at the sky: the forward axis has no
    // horizontal part, so there is no direction to report.
    const Result r = Mag::compute(fieldAtDip(50.0f, 0.0f), Vec3{0.0f, 1.0f, 0.0f});
    EXPECT_FALSE(r.valid);
}

TEST(Heading, NonFiniteAndZeroInputsAreRejectedRatherThanPropagated)
{
    const float nan = std::nanf("");

    EXPECT_FALSE(Mag::compute(Vec3{nan, 0.0f, 0.0f}, kFlat).valid);
    EXPECT_FALSE(Mag::compute(fieldAtDip(50.0f, 0.0f), Vec3{nan, 0.0f, 1.0f}).valid);
    EXPECT_FALSE(Mag::compute(Vec3{0.0f, 0.0f, 0.0f}, kFlat).valid);
    EXPECT_FALSE(Mag::compute(fieldAtDip(50.0f, 0.0f), Vec3{0.0f, 0.0f, 0.0f}).valid);
}

TEST(Heading, MotionIsFlaggedRatherThanSuppressed)
{
    const Vec3 field = fieldAtDip(50.0f, 0.0f);

    const Result still = Mag::compute(field, kFlat);
    EXPECT_TRUE(still.valid);
    EXPECT_TRUE(still.levelled);

    // Half a g of extra acceleration: the up vector is no longer up.
    const Result moving = Mag::compute(field, Vec3{0.0f, 0.0f, 1.6f});
    EXPECT_TRUE(moving.valid) << "still computed, so the reading can be argued with";
    EXPECT_FALSE(moving.levelled);
}

TEST(Heading, TheDownConventionFlipsTheUpVector)
{
    const Vec3 field = fieldAtDip(50.0f, 60.0f);

    const Result up = Mag::compute(field, kFlat, UpConvention::AccelPointsUp);
    const Result down =
        Mag::compute(field, Vec3{0.0f, 0.0f, -1.0f}, UpConvention::AccelPointsDown);

    ASSERT_TRUE(up.valid);
    ASSERT_TRUE(down.valid);
    EXPECT_NEAR(up.headingDeg, down.headingDeg, 0.01f);
    EXPECT_NEAR(up.dipDeg, down.dipDeg, 0.01f);
}

TEST(Cardinal, BoundariesRoundToTheNearestPoint)
{
    EXPECT_STREQ(Mag::cardinal(0.0f), "N");
    EXPECT_STREQ(Mag::cardinal(22.4f), "N");
    EXPECT_STREQ(Mag::cardinal(22.6f), "NE");
    EXPECT_STREQ(Mag::cardinal(337.6f), "N");
    EXPECT_STREQ(Mag::cardinal(360.0f), "N");
    EXPECT_STREQ(Mag::cardinal(-45.0f), "NW");
    EXPECT_STREQ(Mag::cardinal(std::nanf("")), "?");
}
