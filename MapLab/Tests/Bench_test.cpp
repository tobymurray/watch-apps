/**
 * @file Bench_test.cpp
 * @brief The timing harness, driven by a clock the test controls.
 *
 * The harness exists because the device offers milliseconds and several
 * subjects cost microseconds. These tests pin the three behaviours that make
 * that safe: it scales the iteration count until the measurement is above the
 * floor, it says so when the clock never moved, and its arithmetic does not
 * overflow on a subject that takes seconds.
 */
#include <gtest/gtest.h>

#include <Bench.hpp>

namespace {

using namespace MapLab;

/// A clock the subject advances, so a test can describe a cost exactly.
struct FakeClock {
    uint32_t t = 0;
    uint32_t nowMs() const { return t; }
};

TEST(Bench, ScalesIterationsUntilTheFloorIsCleared)
{
    FakeClock clock;
    // One millisecond per call: 200 calls is the floor exactly.
    const BenchResult r = measure(clock, [&]() -> uint32_t { clock.t += 1; return 1; }, 200);
    EXPECT_TRUE(r.valid);
    EXPECT_GE(r.elapsedMs, 200u);
    EXPECT_EQ(r.usPerOp, 1000u);
    EXPECT_EQ(r.checksum, r.iterations);
}

TEST(Bench, AFastSubjectStillGetsAnHonestPerOpCost)
{
    FakeClock clock;
    // 1 ms per 100 calls => 10 us each.
    uint32_t calls = 0;
    const BenchResult r = measure(clock, [&]() -> uint32_t {
        if (++calls % 100 == 0) {
            clock.t += 1;
        }
        return 1;
    }, 200);
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.usPerOp, 10u);
}

TEST(Bench, ASubjectSlowerThanTheFloorRunsOnce)
{
    FakeClock clock;
    const BenchResult r = measure(clock, [&]() -> uint32_t { clock.t += 900; return 3; }, 200);
    EXPECT_EQ(r.iterations, 1u);
    EXPECT_EQ(r.elapsedMs, 900u);
    EXPECT_EQ(r.usPerOp, 900000u);
    EXPECT_EQ(r.checksum, 3u);
}

TEST(Bench, AStoppedClockIsReportedInvalidRatherThanInfinitelyFast)
{
    struct DeadClock { uint32_t nowMs() const { return 7; } } clock;
    const BenchResult r = measure(clock, []() -> uint32_t { return 1; }, 200, 64);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.iterations, 64u);
    EXPECT_EQ(r.elapsedMs, 0u);
}

TEST(Bench, LongRunsDoNotOverflowTheMicrosecondArithmetic)
{
    // elapsed * 1000 overflows a uint32 above 4.29 s, which is well inside
    // the range of a whole-viewport render on this device.
    FakeClock clock;
    const BenchResult r = measure(clock, [&]() -> uint32_t { clock.t += 10000; return 1; }, 200);
    EXPECT_EQ(r.iterations, 1u);
    EXPECT_EQ(r.usPerOp, 10000000u);
}

TEST(Bench, TheClockWrappingIsMeasuredCorrectly)
{
    // getTimeMs() is a 32-bit uptime counter that wraps at ~49.7 days.
    // Unsigned subtraction gets this right; a signed comparison would report
    // a negative elapsed time and a nonsense cost.
    struct WrapClock {
        uint32_t t = 0xFFFFFF00u;
        uint32_t nowMs() const { return t; }
    } clock;
    const BenchResult r = measure(clock, [&]() -> uint32_t { clock.t += 250; return 1; }, 200);
    EXPECT_TRUE(r.valid);
    EXPECT_EQ(r.usPerOp, 250000u);
}

} // namespace
