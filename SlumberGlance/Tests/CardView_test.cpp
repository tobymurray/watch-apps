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

TEST(CardView, RunningWithNoNightSaysNoneYet)
{
    const auto v = make(Read::Ok, running(3 * 3600 + 12 * 60, 120), nullptr, kNow, true);
    EXPECT_EQ(std::string(v.top), "No night yet");
    EXPECT_EQ(std::string(v.bottom), "wear it to bed");
    EXPECT_EQ(v.tone, Tone::Muted);
}

TEST(CardView, TheBackupCopyReadsTheSameAsThePrimary)
{
    const Card c = running(600, 60);
    const auto a = make(Read::Ok, c, nullptr, kNow, true);
    const auto b = make(Read::FromBackup, c, nullptr, kNow, true);
    EXPECT_EQ(std::string(a.top), std::string(b.top));
    EXPECT_EQ(std::string(a.bottom), std::string(b.bottom));
}

TEST(CardView, OneMissedHeartbeatIsNotYetStopped)
{
    const auto v = make(Read::Ok, running(7200, CardView::kStaleAfterS), nullptr, kNow, true);
    EXPECT_NE(v.tone, Tone::Warning);
}

TEST(CardView, ASilentWriterIsCalledStopped)
{
    const auto v = make(Read::Ok, running(7200, 65 * 60), nullptr, kNow, true);
    EXPECT_EQ(std::string(v.top), "Slumber stopped");
    EXPECT_EQ(std::string(v.bottom), "last seen 1h05 ago");
    EXPECT_EQ(v.tone, Tone::Warning);
}

TEST(CardView, NothingPublishedAsksForSlumber)
{
    const auto v = make(Read::Absent, Card{}, nullptr, kNow, true);
    EXPECT_EQ(std::string(v.bottom), "open Slumber once");
    EXPECT_EQ(v.tone, Tone::Muted);
}

/// A write in progress must not blank a card that was showing a good value.
TEST(CardView, BusyKeepsTheLastGoodCard)
{
    Card last          = running(600, 60);
    last.state         = SlumberCard::State::Report;
    last.onsetMinOfDay = 23 * 60 + 47;
    last.wakeMinOfDay  = 7 * 60 + 5;
    last.restMin       = 7 * 60 + 2;
    const auto v       = make(Read::Busy, Card{}, &last, kNow, true);
    EXPECT_EQ(std::string(v.top), "23:47-07:05");
}

TEST(CardView, BusyWithNothingBeforeSaysSo)
{
    const auto v = make(Read::Busy, Card{}, nullptr, kNow, true);
    EXPECT_EQ(std::string(v.bottom), "one moment");
}

TEST(CardView, AnUnsetClockIsNotReadAsAStoppedWriter)
{
    const auto v = make(Read::Ok, running(600, 60), nullptr, 0, true);
    EXPECT_EQ(std::string(v.bottom), "waiting for clock");
    EXPECT_NE(v.tone, Tone::Warning);
}

/// A writer clock a little ahead of the reader's must not wrap to a huge age.
TEST(CardView, AHeartbeatFromTheFutureIsFresh)
{
    Card c         = running(600, 0);
    c.heartbeatUtc = kNow + 30;
    const auto v   = make(Read::Ok, c, nullptr, kNow, true);
    EXPECT_NE(v.tone, Tone::Warning);
}

TEST(CardView, TheLongestLinesFitTheirBuffers)
{
    Card c            = running(0, 0);
    c.seq             = 4'294'967'295u;
    c.serviceStartUtc = 1;
    const auto fresh  = make(Read::Ok, c, nullptr, kNow, true);
    EXPECT_LT(std::string(fresh.top).size(), sizeof(fresh.top) - 1);
    EXPECT_LT(std::string(fresh.bottom).size(), sizeof(fresh.bottom) - 1);

    c.heartbeatUtc   = 1;
    const auto stale = make(Read::Ok, c, nullptr, kNow, true);
    EXPECT_LT(std::string(stale.bottom).size(), sizeof(stale.bottom) - 1);
}

namespace
{

Card night(SlumberCard::State state)
{
    Card c          = running(3600, 60);
    c.state         = state;
    c.onsetMinOfDay = 23 * 60 + 47;
    c.wakeMinOfDay  = 7 * 60 + 5;
    c.restMin       = 7 * 60 + 2;
    c.spanMin       = 7 * 60 + 18;
    c.validUntilUtc = kNow + 3600;
    return c;
}

} // namespace

TEST(CardView, ASettledNightShowsItsTimesAndRest)
{
    const auto v = make(Read::Ok, night(SlumberCard::State::Report), nullptr, kNow, true);
    EXPECT_EQ(std::string(v.top), "23:47-07:05");
    EXPECT_EQ(std::string(v.bottom), "7h02 at rest");
    EXPECT_EQ(v.tone, Tone::Normal);
}

TEST(CardView, WithoutTheIconTheNightIsNamed)
{
    const auto v = make(Read::Ok, night(SlumberCard::State::Report), nullptr, kNow, false);
    EXPECT_EQ(std::string(v.top), "Night 23:47-07:05");
}

TEST(CardView, AnUnsettledNightSaysSoFar)
{
    const auto v = make(Read::Ok, night(SlumberCard::State::Provisional), nullptr, kNow, true);
    EXPECT_EQ(std::string(v.bottom), "7h02 so far");
}

TEST(CardView, ANightOnATableIsNotReportedAsSleep)
{
    const auto v = make(Read::Ok, night(SlumberCard::State::NotWorn), nullptr, kNow, true);
    EXPECT_EQ(std::string(v.top), "No night");
    EXPECT_EQ(v.tone, Tone::Muted);
}

TEST(CardView, AnExpiredNightFallsBackToNoneYet)
{
    Card c          = night(SlumberCard::State::Report);
    c.validUntilUtc = kNow - 1;
    EXPECT_EQ(std::string(make(Read::Ok, c, nullptr, kNow, true).top), "No night yet");
}

TEST(CardView, StoppedOutranksLastNight)
{
    Card c         = night(SlumberCard::State::Report);
    c.heartbeatUtc = kNow - 65 * 60;
    EXPECT_EQ(std::string(make(Read::Ok, c, nullptr, kNow, true).top), "Slumber stopped");
}
