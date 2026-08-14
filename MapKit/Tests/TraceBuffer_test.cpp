/**
 * @file TraceBuffer_test.cpp
 * @brief Decimation and thinning: an activity of any length must fit, at
 *        progressively coarser resolution, and must never be truncated.
 */
#include <gtest/gtest.h>

#include <MapKit/TraceBuffer.hpp>

namespace {

using MapKit::TraceBuffer;

TEST(TraceBuffer, StartsEmptyAtTheInitialThreshold)
{
    TraceBuffer t;
    EXPECT_EQ(t.count(), 0u);
    EXPECT_EQ(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX);
}

TEST(TraceBuffer, FirstPointIsAlwaysKept)
{
    TraceBuffer t;
    EXPECT_TRUE(t.append(1000, 2000));
    EXPECT_EQ(t.count(), 1u);
    EXPECT_EQ(t.at(0).x, 1000);
    EXPECT_EQ(t.at(0).y, 2000);
}

TEST(TraceBuffer, PointsCloserThanTheThresholdAreDropped)
{
    TraceBuffer t;
    t.append(0, 0);
    const int32_t just_under = TraceBuffer::INITIAL_THRESHOLD_PX - 1;
    EXPECT_FALSE(t.append(just_under, just_under));
    EXPECT_EQ(t.count(), 1u);
}

TEST(TraceBuffer, MovementOnEitherAxisAloneIsEnough)
{
    // Chebyshev, not Euclidean: a fix that moved far enough in x counts even
    // if y did not move at all.
    TraceBuffer t;
    t.append(0, 0);
    EXPECT_TRUE(t.append(TraceBuffer::INITIAL_THRESHOLD_PX, 0));
    EXPECT_TRUE(t.append(TraceBuffer::INITIAL_THRESHOLD_PX,
                         TraceBuffer::INITIAL_THRESHOLD_PX));
    EXPECT_EQ(t.count(), 3u);
}

TEST(TraceBuffer, NeverExceedsCapacity)
{
    TraceBuffer t;
    // Far more appends than CAPACITY, each far enough apart to be kept until
    // thinning starts raising the bar.
    for (int32_t i = 0; i < 40000; ++i) {
        t.append(i * TraceBuffer::INITIAL_THRESHOLD_PX, 0);
    }
    EXPECT_LE(t.count(), TraceBuffer::CAPACITY);
    EXPECT_GT(t.count(), TraceBuffer::CAPACITY / 4)
        << "thinning should halve, not empty, the buffer";
}

TEST(TraceBuffer, ThinningDoublesTheThresholdRatherThanTruncating)
{
    TraceBuffer t;
    for (size_t i = 0; i < TraceBuffer::CAPACITY + 1; ++i) {
        t.append(static_cast<int32_t>(i) * TraceBuffer::INITIAL_THRESHOLD_PX, 0);
    }
    EXPECT_EQ(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX * 2);
    // Still anchored at the start of the activity: history gets coarser, it
    // does not get dropped from the front.
    EXPECT_EQ(t.at(0).x, 0);
}

TEST(TraceBuffer, ThinningKeepsTheNewestPoint)
{
    // Otherwise the drawn trace would visibly retreat from the wearer's
    // position at the moment the buffer fills.
    TraceBuffer t;
    int32_t last = 0;
    for (size_t i = 0; i < TraceBuffer::CAPACITY; ++i) {
        last = static_cast<int32_t>(i) * TraceBuffer::INITIAL_THRESHOLD_PX;
        t.append(last, 0);
    }
    ASSERT_EQ(t.count(), TraceBuffer::CAPACITY);
    EXPECT_EQ(t.at(t.count() - 1).x, last);

    const int32_t next = last + TraceBuffer::INITIAL_THRESHOLD_PX * 4;
    t.append(next, 0);
    EXPECT_EQ(t.at(t.count() - 1).x, next);
}

TEST(TraceBuffer, MonotonicAfterThinning)
{
    // Thinning keeps every second point plus the newest; the result must stay
    // in capture order or the polyline would zig-zag backwards.
    TraceBuffer t;
    for (size_t i = 0; i < TraceBuffer::CAPACITY * 3; ++i) {
        t.append(static_cast<int32_t>(i) * TraceBuffer::INITIAL_THRESHOLD_PX, 0);
    }
    for (size_t i = 1; i < t.count(); ++i) {
        EXPECT_GT(t.at(i).x, t.at(i - 1).x) << "at index " << i;
    }
}

TEST(TraceBuffer, ClearResetsCountAndThreshold)
{
    TraceBuffer t;
    for (size_t i = 0; i < TraceBuffer::CAPACITY + 1; ++i) {
        t.append(static_cast<int32_t>(i) * TraceBuffer::INITIAL_THRESHOLD_PX, 0);
    }
    ASSERT_GT(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX);
    t.clear();
    EXPECT_EQ(t.count(), 0u);
    EXPECT_EQ(t.thresholdPx(), TraceBuffer::INITIAL_THRESHOLD_PX)
        << "a new activity starts at full resolution";
}

} // namespace
