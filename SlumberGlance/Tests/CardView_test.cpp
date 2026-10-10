#include <gtest/gtest.h>

#include <string>

#include "CardView.hpp"

using CardView::make;
using CardView::Tone;
using SlumberCard::Card;
using SlumberCard::Read;

namespace
{

constexpr uint32_t kNow = 1'791'500'000u;

Card running(uint32_t upS, uint32_t sinceBeatS)
{
    Card c;
    c.seq             = 42;
    c.state           = SlumberCard::State::Probe;
    c.serviceStartUtc = kNow - upS;
    c.heartbeatUtc    = kNow - sinceBeatS;
    return c;
}

} // namespace

TEST(CardView, AFreshCardShowsUptimeAndHeartbeat)
{
    const auto v = make(Read::Ok, running(3 * 3600 + 12 * 60, 120), nullptr, kNow);
    EXPECT_EQ(std::string(v.top), "Slumber up 3h12");
    EXPECT_EQ(std::string(v.bottom), "beat 2m ago  #42");
    EXPECT_EQ(v.tone, Tone::Normal);
}

TEST(CardView, TheBackupCopyReadsTheSameAsThePrimary)
{
    const Card c = running(600, 60);
    const auto a = make(Read::Ok, c, nullptr, kNow);
    const auto b = make(Read::FromBackup, c, nullptr, kNow);
    EXPECT_EQ(std::string(a.top), std::string(b.top));
    EXPECT_EQ(std::string(a.bottom), std::string(b.bottom));
}

TEST(CardView, OneMissedHeartbeatIsNotYetStopped)
{
    const auto v = make(Read::Ok, running(7200, CardView::kStaleAfterS), nullptr, kNow);
    EXPECT_EQ(v.tone, Tone::Normal);
}

TEST(CardView, ASilentWriterIsCalledStopped)
{
    const auto v = make(Read::Ok, running(7200, 65 * 60), nullptr, kNow);
    EXPECT_EQ(std::string(v.top), "Slumber stopped");
    EXPECT_EQ(std::string(v.bottom), "last seen 1h05 ago");
    EXPECT_EQ(v.tone, Tone::Warning);
}

TEST(CardView, NothingPublishedAsksForSlumber)
{
    const auto v = make(Read::Absent, Card{}, nullptr, kNow);
    EXPECT_EQ(std::string(v.bottom), "open Slumber once");
    EXPECT_EQ(v.tone, Tone::Muted);
}

/// A write in progress must not blank a card that was showing a good value.
TEST(CardView, BusyKeepsTheLastGoodCard)
{
    const Card last = running(600, 60);
    const auto v    = make(Read::Busy, Card{}, &last, kNow);
    EXPECT_EQ(std::string(v.bottom), "beat 1m ago  #42");
}

TEST(CardView, BusyWithNothingBeforeSaysSo)
{
    const auto v = make(Read::Busy, Card{}, nullptr, kNow);
    EXPECT_EQ(std::string(v.bottom), "busy, retrying");
}

TEST(CardView, AnUnsetClockIsNotReadAsAStoppedWriter)
{
    const auto v = make(Read::Ok, running(600, 60), nullptr, 0);
    EXPECT_EQ(std::string(v.bottom), "no clock  #42");
    EXPECT_NE(v.tone, Tone::Warning);
}

/// A writer clock a little ahead of the reader's must not wrap to a huge age.
TEST(CardView, AHeartbeatFromTheFutureIsFresh)
{
    Card c         = running(600, 0);
    c.heartbeatUtc = kNow + 30;
    const auto v   = make(Read::Ok, c, nullptr, kNow);
    EXPECT_EQ(v.tone, Tone::Normal);
}

TEST(CardView, TheLongestLinesFitTheirBuffers)
{
    Card c            = running(0, 0);
    c.seq             = 4'294'967'295u;
    c.serviceStartUtc = 1;
    const auto fresh  = make(Read::Ok, c, nullptr, kNow);
    EXPECT_LT(std::string(fresh.top).size(), sizeof(fresh.top) - 1);
    EXPECT_LT(std::string(fresh.bottom).size(), sizeof(fresh.bottom) - 1);

    c.heartbeatUtc   = 1;
    const auto stale = make(Read::Ok, c, nullptr, kNow);
    EXPECT_LT(std::string(stale.bottom).size(), sizeof(stale.bottom) - 1);
}
