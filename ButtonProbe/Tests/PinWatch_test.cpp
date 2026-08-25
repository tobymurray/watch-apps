/**
 * Tests for the pin selection and the edge detection.
 *
 * This is the part of the app that decides what it is looking at, and it has to
 * be right in both directions. Watch too much and the log is a wall of bus
 * traffic with a button press somewhere in it. Watch too little -- strike out
 * the very pin the button is on -- and the app reports nothing, which reads
 * exactly like "a glance service cannot see a button", the false conclusion this
 * whole line of work would then be abandoned on.
 *
 * Every snapshot below is a plain array, so none of this needs a watch.
 */

#include <gtest/gtest.h>

#include "PinWatch.hpp"

namespace
{

using Probe::kPortCount;
using Probe::PinWatch;
using Probe::Transition;

/// A snapshot with every port reading `value`.
struct Snapshot {
    uint32_t words[kPortCount];

    explicit Snapshot(uint32_t value = 0)
    {
        for (uint32_t i = 0; i < kPortCount; ++i) {
            words[i] = value;
        }
    }

    const uint32_t *get() const { return words; }
    uint32_t       *get()       { return words; }
};

TEST(PlausibleIdr, RejectsReservedBits)
{
    // GPIOH read back FFFFFFFF on this watch in the 2026-07-29 sweep. The top
    // half of a real IDR is reserved and reads zero, so this test is exact
    // where "all ones" would not be: a real port with all sixteen pins pulled
    // high reads 0000FFFF and must still count.
    EXPECT_TRUE(Probe::plausibleIdr(0x00000000u));
    EXPECT_TRUE(Probe::plausibleIdr(0x0000FFFFu));
    EXPECT_FALSE(Probe::plausibleIdr(0xFFFFFFFFu));
    EXPECT_FALSE(Probe::plausibleIdr(0x00010000u));
}

TEST(PinWatch, StrikesOutPinsThatMovedDuringCalibration)
{
    PinWatch watch;

    Snapshot a(0x00000001u); // pin 0 high
    Snapshot b(0x00000000u); // pin 0 low -- it moves on its own
    watch.observe(a.get());
    watch.observe(b.get());
    watch.settle(a.get());

    // Pin 0 was seen at both levels, so it is a signal rather than a button.
    EXPECT_EQ(watch.maskOf(0) & 0x1u, 0u);
    // Everything else held still and is watched.
    EXPECT_EQ(watch.maskOf(0) & 0x2u, 0x2u);
}

TEST(PinWatch, ReportsAnEdgeOnAWatchedPin)
{
    PinWatch watch;

    Snapshot idle(0x0000FFFFu); // every pin pulled high, nothing moving
    watch.observe(idle.get());
    watch.settle(idle.get());

    Snapshot pressed(0x0000FFFFu);
    pressed.words[3] &= ~(1u << 4); // PD4 goes low

    Transition out[8] = {};
    ASSERT_EQ(watch.update(pressed.get(), out, 8), 1u);
    EXPECT_EQ(out[0].port, 3);
    EXPECT_EQ(out[0].pin, 4);
    EXPECT_EQ(out[0].level, 0);

    // And the release.
    ASSERT_EQ(watch.update(idle.get(), out, 8), 1u);
    EXPECT_EQ(out[0].level, 1);
}

TEST(PinWatch, SaysNothingWhenNothingMoved)
{
    PinWatch watch;
    Snapshot idle(0x0000FFFFu);
    watch.observe(idle.get());
    watch.settle(idle.get());

    Transition out[8] = {};
    EXPECT_EQ(watch.update(idle.get(), out, 8), 0u);
}

TEST(PinWatch, IgnoresPinsItStruckOut)
{
    PinWatch watch;

    Snapshot high(0x0000FFFFu);
    Snapshot low(0x0000FFFEu); // pin 0 toggling during calibration
    watch.observe(high.get());
    watch.observe(low.get());
    watch.settle(high.get());

    Snapshot moved(0x0000FFFEu); // only the struck-out pin moves
    Transition out[8] = {};
    EXPECT_EQ(watch.update(moved.get(), out, 8), 0u);
}

TEST(PinWatch, TreatsAPortWithReservedBitsSetAsAbsent)
{
    PinWatch watch;

    Snapshot idle(0x0000FFFFu);
    idle.words[7] = 0xFFFFFFFFu; // GPIOH, as this watch actually reads it
    watch.observe(idle.get());
    watch.settle(idle.get());

    EXPECT_EQ(watch.maskOf(7), 0u);

    // A port that is not answering must never look like sixteen simultaneous
    // edges, whatever it returns next.
    Snapshot next(0x0000FFFFu);
    next.words[7] = 0x00000000u;
    Transition out[64] = {};
    EXPECT_EQ(watch.update(next.get(), out, 64), 0u);
}

TEST(PinWatch, ReportsNothingBeforeCalibrationCloses)
{
    PinWatch watch;
    Snapshot idle(0x0000FFFFu);
    watch.observe(idle.get());

    Snapshot pressed(0x0000FFFEu);
    Transition out[8] = {};
    EXPECT_FALSE(watch.settled());
    EXPECT_EQ(watch.update(pressed.get(), out, 8), 0u);
}

TEST(PinWatch, CountsTheWatchedPins)
{
    PinWatch watch;
    Snapshot idle(0x0000FFFFu);
    idle.words[7] = 0xFFFFFFFFu; // absent port contributes nothing

    watch.observe(idle.get());
    const uint32_t watched = watch.settle(idle.get());

    // Seven present ports, sixteen still pins each.
    EXPECT_EQ(watched, 7u * 16u);
}

TEST(PinWatch, DoesNotOverrunACallersBuffer)
{
    PinWatch watch;
    Snapshot idle(0x0000FFFFu);
    watch.observe(idle.get());
    watch.settle(idle.get());

    Snapshot allChanged(0x00000000u); // every watched pin moves at once

    Transition out[3] = {};
    const uint32_t written = watch.update(allChanged.get(), out, 3);
    EXPECT_EQ(written, 3u);
}

} // namespace
