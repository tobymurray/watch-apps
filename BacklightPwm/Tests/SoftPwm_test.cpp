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

/// Advances the fake clock by the requested sleep, and counts how much was given
/// back. That count is the whole point of the sleeper existing.
class FakeSleeper : public ISleeper
{
public:
    explicit FakeSleeper(FakeClock& clock) : mClock(clock) {}

    void sleepMs(uint32_t ms) override
    {
        sleptMs += ms;
        ++calls;
        mClock.advance(ms * 1000u);
    }

    uint32_t sleptMs = 0;
    uint32_t calls   = 0;

private:
    FakeClock& mClock;
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

    const BurstStats stats = pwm.runBurst(40000);

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
        pwm.runBurst(40000);

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

    const BurstStats stats = pwm.runBurst(40000);

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
        const BurstStats stats = pwm.runBurst(40000);
        const uint32_t after = clock.nowUs();

        EXPECT_TRUE(stats.held) << "duty " << static_cast<unsigned>(duty) << " did not report held";
        EXPECT_LT(after - before, 100u)
            << "duty " << static_cast<unsigned>(duty) << " burned clock time holding a level";
    }
}

TEST(SoftPwm, SleepsThroughPartOfALongOffPhase)
{
    // At 25 percent duty three quarters of every period is off time that does not
    // need the CPU. Only part of it is actually slept, and the arithmetic is worth
    // stating because it bounds how much this helps: `delay()` takes whole
    // milliseconds and may overshoot by a tick, so the engine sleeps one
    // millisecond fewer than would fit and spins the rest. Against a 4 ms period
    // that leaves about a quarter of it recovered, not three quarters.
    FakeClock clock(1);
    RecordingPin pin(clock);
    FakeSleeper sleeper(clock);
    SoftPwm pwm(pin, clock, &sleeper);
    pwm.setPeriodUs(4000);
    pwm.setDuty(25);

    const BurstStats stats = pwm.runBurst(40000);

    ASSERT_GT(stats.periods, 0u);
    EXPECT_GT(sleeper.calls, 0u) << "the off phase was spun, not slept";
    EXPECT_GE(sleeper.sleptMs, stats.periods) << "less than a millisecond per period recovered";
}

TEST(SoftPwm, DoesNotSleepWhenTheOffPhaseIsTooShortToBeSafe)
{
    // At 75 percent the off phase is one millisecond, which is the same as
    // `delay()`'s granularity and its worst-case overshoot. Sleeping there would
    // stretch the period rather than save anything, so this rung genuinely does
    // spin. Recorded as a test because it is the honest limit of the technique:
    // the high end of the ladder still costs a full thread.
    FakeClock clock(1);
    RecordingPin pin(clock);
    FakeSleeper sleeper(clock);
    SoftPwm pwm(pin, clock, &sleeper);
    pwm.setPeriodUs(4000);
    pwm.setDuty(75);

    pwm.runBurst(40000);

    EXPECT_EQ(sleeper.calls, 0u) << "slept through an off phase shorter than a tick";
}

TEST(SoftPwm, StillWorksWithNoSleeperAtAll)
{
    // The sleeper is optional, and without one the engine must simply spin. A
    // host build has no kernel to sleep on.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock, nullptr);
    pwm.setPeriodUs(4000);
    pwm.setDuty(50);

    const BurstStats stats = pwm.runBurst(40000);
    EXPECT_GE(stats.periods, 8u);
    EXPECT_NEAR(measuredDuty(pin.edges), 50.0, 6.0);
}

TEST(SoftPwm, SleepingDoesNotWreckTheDuty)
{
    // Sleeping is only worth doing if the waveform survives it. The sleeper here
    // advances the clock exactly, so this checks the arithmetic rather than the
    // kernel's timer accuracy, which is what the on-screen achieved duty is for.
    FakeClock clock(1);
    RecordingPin pin(clock);
    FakeSleeper sleeper(clock);
    SoftPwm pwm(pin, clock, &sleeper);
    pwm.setPeriodUs(4000);
    pwm.setDuty(25);

    pwm.runBurst(40000);
    EXPECT_NEAR(measuredDuty(pin.edges), 25.0, 6.0);
}

TEST(SoftPwm, ZeroIsHeldOffRatherThanPulsed)
{
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setDuty(0);

    const BurstStats stats = pwm.runBurst(40000);

    ASSERT_EQ(pin.edges.size(), 1u);
    EXPECT_FALSE(pin.edges[0].on);
    EXPECT_EQ(stats.onUs, 0u);
    EXPECT_EQ(stats.achievedPercent(), 0u);
}

TEST(SoftPwm, RespectsTheBurstBudget)
{
    // The budget is the watchdog margin. A burst that overran it would be the
    // thing that reboots the watch, and it would do so only on hardware.
    FakeClock clock(1);
    RecordingPin pin(clock);
    SoftPwm pwm(pin, clock);
    pwm.setPeriodUs(4000);
    pwm.setDuty(50);

    const BurstStats stats = pwm.runBurst(10000);

    EXPECT_LE(stats.totalUs, 10000u + 4000u)
        << "a burst may finish its last period but must not start another";
    EXPECT_LE(stats.periods, 3u);
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

    pwm.runBurst(40000);

    ASSERT_FALSE(pin.edges.empty());
    EXPECT_FALSE(pin.edges.back().on) << "burst ended with the light left on";
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

    pwm.runBurst(40000);
    const uint32_t afterFirst = pwm.totals().periods;
    pwm.runBurst(40000);

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

    const BurstStats stats = pwm.runBurst(40000);

    EXPECT_GE(stats.periods, 8u) << "the burst gave up at the wrap";
    EXPECT_NEAR(measuredDuty(pin.edges), 50.0, 8.0);
}

} // namespace
