/**
 * Assertions about the experiment rather than about the code.
 *
 * The ladder only means something as a comparison: the same six numbers
 * `BacklightProbe` sent to the kernel, driven here as duty cycles instead. If
 * these rungs drift away from that set, the two apps' photographs stop being
 * comparable and the result quietly evaporates without anything failing.
 */

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "PwmPlan.hpp"

namespace {

using namespace Pwm;

TEST(PwmPlan, CoversTheSameRungsBacklightProbeRequested)
{
    // BacklightProbe's Suite 1 sends brightness 100, 75, 50, 25, 10 and 1. The
    // whole argument is "those six produced one brightness, these six produce
    // six", so the sets have to match.
    const std::set<int> expected{100, 75, 50, 25, 10, 1};
    std::set<int> got;
    for (size_t i = 0; i < ladderSize(); ++i) {
        if (ladder()[i].duty > 0) {
            got.insert(ladder()[i].duty);
        }
    }
    EXPECT_EQ(got, expected)
        << "the ladder no longer matches BacklightProbe's, so the comparison is broken";
}

TEST(PwmPlan, TheProvokingRungsAreAContiguousSuffix)
{
    // The contest rungs answer the question Phase E was gated on, and they must
    // sit at the very end. Two reasons, and the second is the one a first draft
    // of this test missed:
    //
    //   - If the watch dies during them, the ladder is already filmed and
    //     already written down.
    //   - Nothing measured may run *after* the kernel has been provoked. A stray
    //     TurnOff in the middle would leave the kernel fighting for the pin
    //     during rungs being photographed, and every brightness after it would
    //     be suspect while still looking perfectly ordinary.
    //
    // So: once a provoking rung appears, every rung after it must provoke too.
    size_t firstProvoking = ladderSize();
    for (size_t i = 0; i < ladderSize(); ++i) {
        const KernelAsk ask = ladder()[i].ask;
        if (ask == KernelAsk::ShortAutoOff || ask == KernelAsk::TurnOff) {
            firstProvoking = i;
            break;
        }
    }

    ASSERT_LT(firstProvoking, ladderSize()) << "nothing in the plan ever provokes the kernel";

    for (size_t i = firstProvoking; i < ladderSize(); ++i) {
        const KernelAsk ask = ladder()[i].ask;
        const bool provokes = (ask == KernelAsk::ShortAutoOff || ask == KernelAsk::TurnOff);

        // A dark breather is allowed through: it measures nothing, so the kernel
        // being provoked cannot spoil it. Anything that lights the screen after
        // the contest has begun is a measurement taken while the kernel is
        // fighting for the pin, which is what must not happen.
        const bool darkBreather = (ladder()[i].duty == 0);

        EXPECT_TRUE(provokes || darkBreather)
            << "rung " << i << " (" << ladder()[i].label
            << ") lights the screen after the kernel was provoked at rung " << firstProvoking;
    }

    // And enough of the ladder must survive in front of it to be worth filming.
    EXPECT_GE(firstProvoking, 6u) << "the contest starts before the ladder has finished";
}

TEST(PwmPlan, TheKernelIsAskedToHoldTheLightBeforeAnyMeasuredRung)
{
    // Without this the kernel's own auto-off could expire partway through the
    // ladder and every rung after it would be measuring the wrong thing.
    ASSERT_GT(ladderSize(), 0u);
    EXPECT_EQ(ladder()[0].ask, KernelAsk::HoldOn);
}

TEST(PwmPlan, TheContestRungOutlastsTheAutoOffItProvokes)
{
    // The point is to watch the kernel's timer expire mid-rung and see what
    // happens next. A rung shorter than the auto-off would end before the
    // interesting moment.
    for (size_t i = 0; i < ladderSize(); ++i) {
        if (ladder()[i].ask == KernelAsk::ShortAutoOff) {
            // What matters is how long the rung runs *after* the kernel's timer
            // fires, since that is the window the answer appears in.
            ASSERT_GT(ladder()[i].holdMs, kShortAutoOffMs);
            EXPECT_GE(ladder()[i].holdMs - kShortAutoOffMs, 3000u)
                << "rung " << i << " leaves only "
                << (ladder()[i].holdMs - kShortAutoOffMs)
                << " ms after the auto-off fires, which is not long enough to see"
                   " whether the light survived it";
        }
    }
}

TEST(PwmPlan, StartsAndEndsDark)
{
    ASSERT_GT(ladderSize(), 1u);
    EXPECT_EQ(ladder()[0].duty, 0u) << "no dark baseline to compare the lit rungs against";
    EXPECT_EQ(ladder()[ladderSize() - 1].duty, 0u) << "the run leaves the light on";
}

TEST(PwmPlan, NoModulatedRungRunsLongEnoughToStarveTheGui)
{
    // A modulated rung spins flat out, and thirty consecutive seconds of that
    // rebooted the watch. Every one must be short, and every one must be
    // followed by a breather where the service sleeps and the GUI catches up.
    for (size_t i = 0; i < ladderSize(); ++i) {
        const uint8_t duty = ladder()[i].duty;
        if (duty == 0 || duty == 100) {
            continue; // Held: costs no CPU at all.
        }

        EXPECT_LE(ladder()[i].holdMs, 6000u)
            << "rung " << i << " (" << ladder()[i].label << ") spins for "
            << ladder()[i].holdMs << " ms without letting anything else run";

        ASSERT_LT(i + 1, ladderSize())
            << "rung " << i << " modulates and nothing follows it";
        EXPECT_EQ(ladder()[i + 1].duty, 0u)
            << "rung " << i << " (" << ladder()[i].label
            << ") is not followed by a dark breather, so the GUI never catches up";
    }
}

TEST(PwmPlan, TheBreathersAreLongEnoughToBeWorthHaving)
{
    // A breather that is shorter than a couple of GUI frames is not a breather.
    uint32_t breathers = 0;
    for (size_t i = 1; i < ladderSize(); ++i) {
        if (ladder()[i].duty == 0 && ladder()[i - 1].duty != 0
            && ladder()[i].ask == KernelAsk::Nothing) {
            ++breathers;
            EXPECT_GE(ladder()[i].holdMs, 500u)
                << "breather at rung " << i << " is too short to be one";
        }
    }
    EXPECT_GE(breathers, 5u) << "not enough breathers to break up the modulated run";
}

TEST(PwmPlan, EveryRungIsHeldLongEnoughToPhotograph)
{
    for (size_t i = 0; i < ladderSize(); ++i) {
        if (ladder()[i].duty == 0) {
            continue;
        }
        EXPECT_GE(ladder()[i].holdMs, 2000u)
            << "rung " << i << " (" << ladder()[i].label << ") is too short to meter";
    }
}

TEST(PwmPlan, LabelsAreUniqueAndPresent)
{
    // They name photographs. Two rungs sharing a label makes a set of images
    // that cannot be sorted afterwards.
    std::set<std::string> seen;
    for (size_t i = 0; i < ladderSize(); ++i) {
        ASSERT_NE(ladder()[i].label, nullptr) << "rung " << i << " has no label";
        EXPECT_TRUE(seen.insert(ladder()[i].label).second)
            << "duplicate rung label \"" << ladder()[i].label << "\"";
    }
}

TEST(PwmPlan, TheBurstIsShortEnoughToKeepAnsweringTheQueue)
{
    // How long the service goes between message-queue checks. FwDump budgets its
    // slices at roughly 50 ms for the same reason; anything approaching a second
    // is asking for the failure that rebooted the watch during MapManager's pack
    // verify.
    EXPECT_GE(kPeriodsPerBurst, 1u) << "a burst of no periods emits nothing";
    EXPECT_LE(kPeriodsPerBurst * kPeriodUs, 50000u)
        << "a burst spans " << (kPeriodsPerBurst * kPeriodUs) / 1000u
        << " ms between queue checks, which is long enough to risk the watchdog";
}

TEST(PwmPlan, ThePeriodIsAboveTheFlickerThreshold)
{
    // Below about 100 Hz the eye sees flicker rather than dimming, and every
    // photograph taken of it would be worthless.
    EXPECT_LE(kPeriodUs, 10000u) << "under 100 Hz: this will look like flicker, not brightness";
    EXPECT_GE(kPeriodUs, 500u) << "over 2 kHz a busy wait cannot place edges accurately";
}

TEST(PwmPlan, TheKernelHoldOutlastsTheWholeLadder)
{
    // The kernel is asked to hold the light on for the run. If that request
    // expired mid-ladder the kernel would drive the pin off underneath the PWM,
    // and the resulting mess would look like the technique failing.
    uint32_t total = 0;
    for (size_t i = 0; i < ladderSize(); ++i) {
        total += ladder()[i].holdMs;
    }
    EXPECT_GT(kKernelHoldMs, total * 2u)
        << "the kernel's hold (" << kKernelHoldMs << " ms) is not comfortably longer than the "
        << "ladder (" << total << " ms)";
}

} // namespace
