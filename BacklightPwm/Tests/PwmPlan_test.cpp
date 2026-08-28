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
        EXPECT_TRUE(provokes)
            << "rung " << i << " (" << ladder()[i].label
            << ") is measured after the kernel was already provoked at rung " << firstProvoking;
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
            EXPECT_GT(ladder()[i].holdMs, kShortAutoOffMs * 3u)
                << "rung " << i << " ends too soon after the auto-off to see the aftermath";
        }
    }
}

TEST(PwmPlan, StartsAndEndsDark)
{
    ASSERT_GT(ladderSize(), 1u);
    EXPECT_EQ(ladder()[0].duty, 0u) << "no dark baseline to compare the lit rungs against";
    EXPECT_EQ(ladder()[ladderSize() - 1].duty, 0u) << "the run leaves the light on";
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

TEST(PwmPlan, EveryBurstIsFollowedByARealSleep)
{
    // The GUI thread has to get time, and a yield does not give it any: a run on
    // 2026-08-28 froze the screen for the whole thirty seconds of modulation and
    // then rebooted, while the ladder underneath ran perfectly. Only a sleep
    // actually lets another thread run.
    EXPECT_GT(kPostBurstSleepMs, 0u) << "nothing sleeps between bursts; the GUI will starve";

    // And it has to be a meaningful share. A burst of 8 ms followed by 1 ms is
    // 11 percent; the run that starved the GUI was giving it nothing at all, so
    // err generous.
    const uint32_t burstMs = kBurstUs / 1000u;
    EXPECT_GE(kPostBurstSleepMs * 5u, burstMs)
        << "sleeping " << kPostBurstSleepMs << " ms per " << burstMs
        << " ms burst leaves under a fifth of the time for everything else";
}

TEST(PwmPlan, TheSleepDoesNotSwampTheDutyItIsProtecting)
{
    // The other side of it: the post-burst sleep is off-time on every modulated
    // rung, so it scales them all down uniformly. Too large and the low rungs
    // vanish into the noise and the ladder stops being readable.
    const uint32_t burstMs = kBurstUs / 1000u;
    EXPECT_LE(kPostBurstSleepMs * 2u, burstMs)
        << "the sleep is more than a third of the cycle; the ladder will compress";
}

TEST(PwmPlan, TheBurstFitsComfortablyInsideAWatchdogPeriod)
{
    // This is the number that decides whether the watch reboots mid-run. FwDump
    // budgets its slices at roughly 50 ms of work for the same reason; anything
    // approaching a second here would be asking for the failure that rebooted
    // the watch during MapManager's pack verify.
    EXPECT_LE(kBurstUs, 100000u) << "burst budget is long enough to risk the liveness watchdog";
    EXPECT_GE(kBurstUs, kPeriodUs) << "a burst shorter than one period emits nothing";
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
