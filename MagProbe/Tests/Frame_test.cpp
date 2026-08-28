#include "Mag/Frame.hpp"

#include <gtest/gtest.h>

using Mag::Delivery;
using Mag::FrameShape;
using Mag::Resolve;

TEST(Frame, FieldCountsMapToShapes)
{
    EXPECT_EQ(Mag::classifyFields(0), FrameShape::Empty);
    EXPECT_EQ(Mag::classifyFields(1), FrameShape::TooNarrow);
    EXPECT_EQ(Mag::classifyFields(2), FrameShape::TooNarrow);
    EXPECT_EQ(Mag::classifyFields(3), FrameShape::ThreeAxis);
    EXPECT_EQ(Mag::classifyFields(4), FrameShape::Wider);
    EXPECT_EQ(Mag::classifyFields(64), FrameShape::Wider);
}

// The distinction that cost SleepLab every night it would have recorded, and
// the reason this is an enum rather than a bool.
TEST(Frame, NoProducerSilentAndStalledAreThreeDifferentAnswers)
{
    EXPECT_EQ(Mag::classifyDelivery(Resolve::NoProducer, 0, 0, 2500),
              Delivery::Unknown);
    EXPECT_EQ(Mag::classifyDelivery(Resolve::Resolved, 0, 0, 2500),
              Delivery::Silent);
    EXPECT_EQ(Mag::classifyDelivery(Resolve::Resolved, 1, 100, 2500),
              Delivery::Delivering);
    EXPECT_EQ(Mag::classifyDelivery(Resolve::Resolved, 1, 9000, 2500),
              Delivery::Stalled);
}

TEST(Frame, DeliveryIsUnansweredWithoutADriver)
{
    // Even a sample count, which cannot happen without a driver, does not turn
    // into a delivery verdict: the question does not apply.
    EXPECT_EQ(Mag::classifyDelivery(Resolve::NotAsked, 500, 0, 2500),
              Delivery::Unknown);
}

TEST(Frame, TheStaleBoundaryIsInclusive)
{
    EXPECT_EQ(Mag::classifyDelivery(Resolve::Resolved, 1, 2500, 2500),
              Delivery::Delivering);
    EXPECT_EQ(Mag::classifyDelivery(Resolve::Resolved, 1, 2501, 2500),
              Delivery::Stalled);
}

TEST(Verdict, TheFirstThingThatIsWrongIsWhatGetsReported)
{
    EXPECT_STREQ(Mag::verdict(Resolve::NotAsked, Delivery::Unknown, FrameShape::Unknown).headline,
                 "PENDING");
    EXPECT_STREQ(Mag::verdict(Resolve::NoProducer, Delivery::Unknown, FrameShape::Unknown).headline,
                 "NO COMPASS");
    EXPECT_STREQ(Mag::verdict(Resolve::Resolved, Delivery::Silent, FrameShape::Unknown).reason,
                 "RESOLVED BUT SILENT");
    EXPECT_STREQ(Mag::verdict(Resolve::Resolved, Delivery::Stalled, FrameShape::ThreeAxis).headline,
                 "STALLED");
    EXPECT_STREQ(Mag::verdict(Resolve::Resolved, Delivery::Delivering, FrameShape::TooNarrow).headline,
                 "NO COMPASS");
    EXPECT_STREQ(Mag::verdict(Resolve::Resolved, Delivery::Delivering, FrameShape::ThreeAxis).headline,
                 "DELIVERING");
}

TEST(Verdict, AWiderFrameStillCountsAsDelivering)
{
    // Extra fields are a finding worth recording and not a reason to refuse a
    // heading: the first three may well still be the axes.
    EXPECT_STREQ(Mag::verdict(Resolve::Resolved, Delivery::Delivering, FrameShape::Wider).headline,
                 "DELIVERING");
}

TEST(Verdict, ANoIsNeverReportedAsAnythingElse)
{
    // Whatever else is true, a magnetometer with no producer and one that
    // resolved and never spoke are both "no compass". Asserted across every
    // frame shape, because a shape cannot rescue a missing driver.
    for (uint8_t sh = 0; sh <= static_cast<uint8_t>(FrameShape::Wider); ++sh) {
        const FrameShape shape = static_cast<FrameShape>(sh);
        EXPECT_STREQ(Mag::verdict(Resolve::NoProducer, Delivery::Unknown, shape).headline,
                     "NO COMPASS");
        EXPECT_STREQ(Mag::verdict(Resolve::Resolved, Delivery::Silent, shape).headline,
                     "NO COMPASS");
    }
}
