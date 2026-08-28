#include "Mag/HardIron.hpp"

#include <gtest/gtest.h>

#include <cmath>

using Mag::HardIron;
using Mag::Vec3;
using Quality = Mag::HardIron::Quality;

namespace {

/// Points on a sphere of the given radius centred on `offset`, which is what a
/// magnetometer with a hard-iron offset traces out as it is rotated through
/// every attitude.
void sweepSphere(HardIron& cal, const Vec3& offset, float radius, int steps = 24)
{
    constexpr float kPi = static_cast<float>(M_PI);

    for (int i = 0; i < steps; ++i) {
        const float theta = kPi * static_cast<float>(i) / static_cast<float>(steps - 1);
        for (int j = 0; j < steps; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(steps);
            cal.add(Vec3{offset.x + radius * std::sin(theta) * std::cos(phi),
                         offset.y + radius * std::sin(theta) * std::sin(phi),
                         offset.z + radius * std::cos(theta)});
        }
    }
}

} // namespace

TEST(HardIron, RecoversTheCentreOfTheSphere)
{
    HardIron cal;
    const Vec3 offset{120.0f, -45.0f, 30.0f};
    sweepSphere(cal, offset, 50.0f);

    const Vec3 got = cal.offsets();
    EXPECT_NEAR(got.x, offset.x, 0.5f);
    EXPECT_NEAR(got.y, offset.y, 0.5f);
    EXPECT_NEAR(got.z, offset.z, 0.5f);
    EXPECT_EQ(cal.quality(), Quality::Usable);
}

TEST(HardIron, CorrectionBringsTheMagnitudeBackToTheFieldStrength)
{
    // The point of the whole exercise: an offset larger than the field itself
    // makes the uncorrected magnitude meaningless, and correcting it makes the
    // magnitude classify as a real field.
    HardIron cal;
    const Vec3  offset{200.0f, -150.0f, 90.0f};
    const float field = 50.0f;
    sweepSphere(cal, offset, field);

    const Vec3 raw{offset.x + field, offset.y, offset.z};
    EXPECT_GT(Mag::norm(raw), 200.0f) << "uncorrected, this is nothing like a field";

    Vec3 corrected{};
    ASSERT_TRUE(cal.apply(raw, corrected));
    EXPECT_NEAR(Mag::norm(corrected), field, 0.5f);
}

TEST(HardIron, AnEmptyCalibrationSubtractsNothing)
{
    HardIron cal;
    EXPECT_EQ(cal.quality(), Quality::Empty);

    const Vec3 offsets = cal.offsets();
    EXPECT_EQ(offsets.x, 0.0f);
    EXPECT_EQ(offsets.y, 0.0f);
    EXPECT_EQ(offsets.z, 0.0f);
}

TEST(HardIron, TooFewSamplesIsRefusedEvenWhenTheExtremesLookRight)
{
    HardIron cal;
    // A perfect pair of extremes on every axis, and nowhere near enough data.
    cal.add(Vec3{-50.0f, -50.0f, -50.0f});
    cal.add(Vec3{50.0f, 50.0f, 50.0f});

    EXPECT_EQ(cal.quality(), Quality::TooFewSamples);

    Vec3 out{};
    EXPECT_FALSE(cal.apply(Vec3{10.0f, 10.0f, 10.0f}, out));
}

// An offset from a partial rotation is worse than none: it is wrong, and it
// looks calibrated.
TEST(HardIron, AnUnsweptAxisIsRejectedAsLopsided)
{
    HardIron cal;
    for (int i = 0; i < 100; ++i) {
        const float t = static_cast<float>(i) / 99.0f;
        // X and Y swing fully, Z barely moves: a flat spin on a table.
        cal.add(Vec3{-50.0f + 100.0f * t, 50.0f - 100.0f * t, 1.0f * t});
    }

    EXPECT_EQ(cal.quality(), Quality::Lopsided);

    Vec3 out{};
    EXPECT_FALSE(cal.apply(Vec3{0.0f, 0.0f, 0.0f}, out));
}

TEST(HardIron, ASloppyButGenuineSweepIsAccepted)
{
    HardIron cal;
    // Every axis covered, unevenly: the smallest span is a bit under half the
    // largest, which is what an unpractised figure-of-eight looks like.
    for (int i = 0; i < 60; ++i) {
        const float t = static_cast<float>(i) / 59.0f;
        cal.add(Vec3{-50.0f + 100.0f * t,
                     -35.0f + 70.0f * t,
                     -25.0f + 50.0f * t});
    }

    EXPECT_EQ(cal.quality(), Quality::Usable);
}

TEST(HardIron, NonFiniteSamplesAreRejectedAndCounted)
{
    HardIron cal;
    sweepSphere(cal, Vec3{0.0f, 0.0f, 0.0f}, 50.0f);
    const uint32_t before = cal.samples();

    cal.add(Vec3{std::nanf(""), 0.0f, 0.0f});
    cal.add(Vec3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f});

    EXPECT_EQ(cal.samples(), before) << "a NaN must not widen the range";
    EXPECT_EQ(cal.rejected(), 2u);

    const Vec3 offsets = cal.offsets();
    EXPECT_TRUE(std::isfinite(offsets.x));
    EXPECT_NEAR(offsets.x, 0.0f, 0.5f);
}

TEST(HardIron, ResetReturnsItToEmpty)
{
    HardIron cal;
    sweepSphere(cal, Vec3{10.0f, 10.0f, 10.0f}, 50.0f);
    ASSERT_EQ(cal.quality(), Quality::Usable);

    cal.reset();

    EXPECT_EQ(cal.quality(), Quality::Empty);
    EXPECT_EQ(cal.samples(), 0u);
    EXPECT_EQ(cal.rejected(), 0u);
}

TEST(HardIron, SpansReportWhatWasActuallyCovered)
{
    HardIron cal;
    sweepSphere(cal, Vec3{0.0f, 0.0f, 0.0f}, 40.0f);

    const Vec3 spans = cal.spans();
    EXPECT_NEAR(spans.x, 80.0f, 1.0f);
    EXPECT_NEAR(spans.y, 80.0f, 1.0f);
    EXPECT_NEAR(spans.z, 80.0f, 1.0f);
}
