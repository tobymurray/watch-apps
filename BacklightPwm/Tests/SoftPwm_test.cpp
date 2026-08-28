/**
 * Tests for the PWM engine.
 *
 * The engine is the whole app: everything else is a screen and a message. And it
 * is testable for one reason, that it takes its pin and its clock as interfaces,
 * so a fake clock can be advanced deterministically and a fake pin can record the
 * waveform that came out.
 *
 * What these check is the thing that would ruin a run without failing: a duty
 * cycle that is not the duty cycle that was asked for. A photograph of six
 * indistinguishable brightnesses proves nothing, and if the engine silently
 * emitted 50 percent at every rung, nothing else in the app would notice.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "SoftPwm.hpp"

namespace {

using namespace Pwm;

/// Advances a fixed step every time it is read, so a busy wait terminates and
/// the resolution of the simulated clock is explicit.
class FakeClock : public IClock
{
public:
    explicit FakeClock(uint32_t stepUs, uint32_t startUs = 0)
        : mStep(stepUs)
        , mNow(startUs)
    {
    }

    uint32_t nowUs() const override
    {
        const uint32_t v = mNow;
        mNow += mStep;
        return v;
    }

    void set(uint32_t us) { mNow = us; }
    void advance(uint32_t us) { mNow += us; }

private:
    uint32_t         mStep;
    mutable uint32_t mNow;
};

/// Records every edge with the clock reading at the moment it was issued.
class RecordingPin : public IPin
{
public:
    struct Edge {
        bool     on;
        uint32_t atUs;
    };

    RecordingPin(const FakeClock& clock) : mClock(clock) {}

    void lightOn() override { edges.push_back({true, peek()}); }
    void lightOff() override { edges.push_back({false, peek()}); }

    std::vector<Edge> edges;

private:
    // Reads the clock without advancing the recorded time any further than the
    // engine itself would: the engine reads the clock right after each edge, so
    // peeking here is representative.
    uint32_t peek() const { return mClock.nowUs(); }
    const FakeClock& mClock;
};

/// Duty measured from the recorded waveform, independent of what the engine
/// reported. A test that trusted BurstStats would be checking the engine's
/// arithmetic against itself.
double measuredDuty(const std::vector<RecordingPin::Edge>& edges)
{
    if (edges.size() < 2) {
        return -1.0;
    }
    uint32_t onUs = 0;
    uint32_t total = 0;
    for (size_t i = 0; i + 1 < edges.size(); ++i) {
        const uint32_t span = edges[i + 1].atUs - edges[i].atUs;
        total += span;
        if (edges[i].on) {
            onUs += span;
        }
    }
    return total ? (100.0 * onUs) / total : -1.0;
}

TEST(SoftPwm, EmitsRoughlyTheRequestedDuty)
{
    // 1 us clock resolution, 4 ms period, 50 percent, 40 ms of budget.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(50);

    const BurstStats stats = pwm.runBurst(10);

    EXPECT_GE(stats.periods, 8u) << "a 40 ms budget at 4 ms should fit about ten periods";
    EXPECT_NEAR(measuredDuty(pin.edges), 50.0, 6.0);
    EXPECT_NEAR(static_cast<double>(stats.achievedPercent()), 50.0, 6.0);
}

TEST(SoftPwm, DistinguishesEveryRungOfTheLadder)
{
    // The point of the app: six requests must produce six different waveforms.
    // If this ever fails, the photographs will look exactly like BacklightProbe's
    // and prove the opposite of what they are meant to.
    const uint8_t rungs[] = {100, 75, 50, 25, 10, 1};
    double last = 1000.0;

    for (uint8_t duty : rungs) {
        FakeClock clock(1);
        RecordingPin pin(clock);
        SoftPwm pwm(pin, clock);
        pwm.setPeriodUs(4000);
        pwm.setDuty(duty);
        pwm.runBurst(10);

        const double got = (duty == 100) ? 100.0 : measuredDuty(pin.edges);
        EXPECT_LT(got, last) << "duty " << static_cast<unsigned>(duty)
                             << " did not come out dimmer than the rung above it";
        last = got;
    }
}

TEST(SoftPwm, FullBrightnessIsHeldRatherThanToggled)
{
    // A 100 percent duty that still toggled every period would show any
    // transition glitch as a dimming at full brightness, which is exactly the
    // artefact that would discredit the result.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setDuty(100);

    const BurstStats stats = pwm.runBurst(10);

    ASSERT_EQ(pin.edges.size(), 1u) << "full brightness emitted more than one edge";
    EXPECT_TRUE(pin.edges[0].on);
    EXPECT_TRUE(stats.held);
}

TEST(SoftPwm, AnEndpointReturnsImmediatelyRatherThanSpinning)
{
    // Holding a level needs no CPU. The first version spun out the whole budget
    // here, which is eight seconds of the ladder at full tilt for nothing, and
    // the watch rebooted near the end of a run.
    for (uint8_t duty : {uint8_t{0}, uint8_t{100}}) {
        FakeClock clock(1);
        RecordingPin pin(clock);
        SoftPwm pwm(pin, clock);
        pwm.setDuty(duty);

        const uint32_t before = clock.nowUs();
        const BurstStats stats = pwm.runBurst(10);
        const uint32_t after = clock.nowUs();

        EXPECT_TRUE(stats.held) << "duty " << static_cast<unsigned>(duty) << " did not report held";
        EXPECT_LT(after - before, 100u)
            << "duty " << static_cast<unsigned>(duty) << " burned clock time holding a level";
    }
}

TEST(SoftPwm, ZeroIsHeldOffRatherThanPulsed)
{
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setDuty(0);

    const BurstStats stats = pwm.runBurst(10);

    ASSERT_EQ(pin.edges.size(), 1u);
    EXPECT_FALSE(pin.edges[0].on);
    EXPECT_EQ(stats.onUs, 0u);
    EXPECT_EQ(stats.achievedPercent(), 0u);
}

TEST(SoftPwm, RunsExactlyTheRequestedNumberOfPeriods)
{
    // Counted in periods rather than time. Nothing sleeps inside a period any
    // more, but the count is still the honest unit: it is what the caller wants
    // to bound and it cannot be thrown off by a clock that stops.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(25);

    const BurstStats stats = pwm.runBurst(3);
    EXPECT_EQ(stats.periods, 3u) << "a burst ran the wrong number of periods";
}

TEST(SoftPwm, TheWaveformHasNoGapBetweenPeriods)
{
    // The flicker regression, pinned. Sleeping *between* bursts left the light
    // fully off for the length of the sleep, which is a 100 Hz full-depth
    // envelope on top of the PWM, and it flashed on every modulated rung.
    //
    // So: consecutive periods must abut. The off stretch at the end of one period
    // and the on edge of the next must be one period apart, with no extra
    // darkness wedged in between.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(25);

    pwm.runBurst(5);

    // Rising edges are every other entry; their spacing is the period.
    std::vector<uint32_t> rises;
    for (const auto& e : pin.edges) {
        if (e.on) {
            rises.push_back(e.atUs);
        }
    }
    ASSERT_GE(rises.size(), 3u);
    for (size_t i = 1; i < rises.size(); ++i) {
        const uint32_t gap = rises[i] - rises[i - 1];
        EXPECT_NEAR(static_cast<double>(gap), 4000.0, 600.0)
            << "period " << i << " started " << gap << " us after the last, not one period";
    }
}

TEST(SoftPwm, EndsOnAPeriodBoundary)
{
    // A burst that stopped mid-pulse would leave the light on for the whole gap
    // between bursts, which would show up as a brightness that depends on how
    // busy the message queue is.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(75);

    pwm.runBurst(10);

    ASSERT_FALSE(pin.edges.empty());
    EXPECT_FALSE(pin.edges.back().on) << "burst ended with the light left on";
}

TEST(SoftPwm, EveryRungIsFarFromEveryOther)
{
    // Stronger than "monotonically dimmer", and it exists because a run came back
    // monotonic-looking and useless: with the clock miscalibrated, 75 and 50 both
    // photographed as near-full and 25, 10 and 1 all photographed as off. The
    // ladder had collapsed into three levels while still passing a strictly
    // decreasing check.
    //
    // So: adjacent rungs must differ by a margin an eye or a meter could actually
    // resolve.
    const uint8_t rungs[] = {75, 50, 25, 10};
    double previous = 100.0;

    for (uint8_t duty : rungs) {
        FakeClock clock(1);
        RecordingPin pin(clock);
        SoftPwm pwm(pin, clock);
        pwm.setPeriodUs(4000);
        pwm.setDuty(duty);
        pwm.runBurst(10);

        const double got = measuredDuty(pin.edges);
        EXPECT_NEAR(got, static_cast<double>(duty), 6.0)
            << "duty " << static_cast<unsigned>(duty) << " came out at " << got;
        EXPECT_LT(got, previous - 8.0)
            << "duty " << static_cast<unsigned>(duty) << " is too close to the rung above it";
        previous = got;
    }
}

TEST(SoftPwm, DutyAboveOneHundredIsClamped)
{
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setDuty(200);
    EXPECT_EQ(pwm.duty(), 100u) << "an overflowing duty would wrap into a tiny on-time";
}

TEST(SoftPwm, PeriodIsClampedToAUsableRange)
{
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);

    pwm.setPeriodUs(1);
    EXPECT_GE(pwm.periodUs(), 200u) << "a period this short is loop overhead, not a duty cycle";

    pwm.setPeriodUs(10u * 1000u * 1000u);
    EXPECT_LE(pwm.periodUs(), 20000u) << "a period this long is visible flicker";
}

TEST(SoftPwm, TotalsAccumulateAcrossBursts)
{
    // The service calls runBurst many times per rung; the results file reports
    // the totals. If those reset per burst the record would understate the run
    // by two orders of magnitude.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(50);

    pwm.runBurst(10);
    const uint32_t afterFirst = pwm.totals().periods;
    pwm.runBurst(10);

    EXPECT_GT(pwm.totals().periods, afterFirst);
    EXPECT_GT(pwm.totals().edges, 0u);
}

TEST(SoftPwm, SurvivesAClockWrap)
{
    // The cycle-derived clock wraps. A burst that straddles the wrap must not
    // decide its deadline has already passed and emit nothing, nor spin forever.
    FakeClock clock(1, 0xFFFFF000u);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(50);

    const BurstStats stats = pwm.runBurst(10);

    EXPECT_GE(stats.periods, 8u) << "the burst gave up at the wrap";
    EXPECT_NEAR(measuredDuty(pin.edges), 50.0, 8.0);
}

} // namespace
