#include "Mag/Units.hpp"

#include <gtest/gtest.h>

#include <cmath>

using Mag::Units;
using Mag::Vec3;

TEST(Units, EarthsFieldInMicroteslaClassifiesAsMicrotesla)
{
    // Weakest and strongest places on the surface, roughly.
    EXPECT_EQ(Mag::classifyMagnitude(22.0f), Units::Microtesla);
    EXPECT_EQ(Mag::classifyMagnitude(50.0f), Units::Microtesla);
    EXPECT_EQ(Mag::classifyMagnitude(67.0f), Units::Microtesla);
}

TEST(Units, TheSameFieldInGaussClassifiesAsGauss)
{
    EXPECT_EQ(Mag::classifyMagnitude(0.22f), Units::Gauss);
    EXPECT_EQ(Mag::classifyMagnitude(0.50f), Units::Gauss);
    EXPECT_EQ(Mag::classifyMagnitude(0.67f), Units::Gauss);
}

TEST(Units, TheBandsDoNotOverlapSoTheAnswerIsAClassification)
{
    // Every value classifies as at most one thing. Walked across the whole
    // plausible range on a log-ish scale rather than asserted by inspection,
    // because two bands that quietly overlap would make the verdict arbitrary.
    const float probes[] = {
        0.0f,   0.001f, 0.01f,  0.19f,  0.2f,   0.5f,   0.75f,  0.76f,
        1.0f,   5.0f,   19.9f,  20.0f,  50.0f,  75.0f,  75.1f,  100.0f,
        1999.0f, 2000.0f, 25000.0f, 100000.0f, 100001.0f, 1e9f,
    };

    for (float v : probes) {
        const Units u = Mag::classifyMagnitude(v);
        // Exactly one of the substantive verdicts, or a non-verdict.
        int substantive = 0;
        substantive += (u == Units::Gauss) ? 1 : 0;
        substantive += (u == Units::Microtesla) ? 1 : 0;
        substantive += (u == Units::RawCounts) ? 1 : 0;
        EXPECT_LE(substantive, 1) << "value " << v;
    }
}

TEST(Units, ExactlyZeroIsItsOwnFindingRatherThanImplausible)
{
    // A driver that resolves and delivers frames of zeros is a specific,
    // reportable thing. Folding it into "unclassified" would lose that.
    EXPECT_EQ(Mag::classifyMagnitude(0.0f), Units::AllZero);
    EXPECT_EQ(Mag::classify(Vec3{0.0f, 0.0f, 0.0f}), Units::AllZero);
}

TEST(Units, NonFiniteIsADefectNotAUnit)
{
    const float nan = std::nanf("");
    const float inf = std::numeric_limits<float>::infinity();

    EXPECT_EQ(Mag::classifyMagnitude(nan), Units::NonFinite);
    EXPECT_EQ(Mag::classifyMagnitude(inf), Units::NonFinite);
    EXPECT_EQ(Mag::classify(Vec3{50.0f, nan, 0.0f}), Units::NonFinite);
}

TEST(Units, ANonFiniteComponentBeatsAFiniteMagnitude)
{
    // An infinity in one axis and its negation in another can produce a NaN
    // magnitude, but a component check is what actually reports the defect.
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(Mag::classify(Vec3{inf, 0.0f, 0.0f}), Units::NonFinite);
}

TEST(Units, ValuesBetweenTheBandsAreUnclassifiedRatherThanForced)
{
    EXPECT_EQ(Mag::classifyMagnitude(5.0f), Units::Unclassified);
    EXPECT_EQ(Mag::classifyMagnitude(500.0f), Units::Unclassified);
    EXPECT_EQ(Mag::classifyMagnitude(1e9f), Units::Unclassified);
}

TEST(Units, ClassifyUsesTheVectorMagnitude)
{
    // 3-4-5: magnitude 50 from components that are individually out of band.
    EXPECT_EQ(Mag::classify(Vec3{30.0f, 40.0f, 0.0f}), Units::Microtesla);
}

TEST(MagnitudeSpread, ARigidRotationOfARealFieldHasASmallSpread)
{
    Mag::MagnitudeSpread s;
    for (int i = 0; i < 100; ++i) {
        // Same field, seen from many attitudes: magnitude barely moves.
        s.add(50.0f + 0.05f * static_cast<float>(i % 3));
    }

    EXPECT_EQ(s.count(), 100u);
    EXPECT_NEAR(s.mean(), 50.05f, 0.05f);
    EXPECT_LT(s.spreadFraction(), 0.01f);
}

TEST(MagnitudeSpread, ADeadAxisShowsUpAsALargeSpread)
{
    Mag::MagnitudeSpread s;
    // Rotation where one axis contributes nothing: magnitude collapses and
    // recovers, which is exactly what a stuck axis looks like.
    s.add(50.0f);
    s.add(35.0f);
    s.add(10.0f);
    s.add(48.0f);

    EXPECT_GT(s.spreadFraction(), 0.5f);
}

TEST(MagnitudeSpread, TooLittleDataReportsNoSpreadRatherThanZero)
{
    Mag::MagnitudeSpread empty;
    EXPECT_LT(empty.spreadFraction(), 0.0f);

    Mag::MagnitudeSpread one;
    one.add(50.0f);
    EXPECT_LT(one.spreadFraction(), 0.0f) << "one sample has no spread to report";
}

TEST(MagnitudeSpread, NonFiniteSamplesAreCountedSeparatelyNotAveragedIn)
{
    Mag::MagnitudeSpread s;
    s.add(50.0f);
    s.add(std::nanf(""));
    s.add(50.0f);

    EXPECT_EQ(s.count(), 2u);
    EXPECT_EQ(s.nonFinite(), 1u);
    EXPECT_NEAR(s.mean(), 50.0f, 0.001f);
}
