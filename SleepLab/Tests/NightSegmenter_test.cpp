/**
 * Host tests for session boundaries.
 *
 * The window tests were written first, deliberately. A bedtime window of
 * 21:00-11:00 is not `start <= t < end` -- that predicate is false for every
 * minute of it -- and a window that never opens looks exactly like a user who
 * never went to bed, which is a bug nobody would report.
 */

#include <gtest/gtest.h>

#include "Engine/Epoch.hpp"
#include "Engine/NightSegmenter.hpp"
#include "Engine/SleepWakeScorer.hpp"

namespace {

using Engine::inWindow;
using Engine::kAbsent;
using Engine::NightSegmenter;
using Engine::SegmenterConfig;

using Event = NightSegmenter::Event;
using State = NightSegmenter::State;

/// Local minutes past midnight, from a readable hh:mm.
constexpr int16_t at(int h, int m) { return static_cast<int16_t>(h * 60 + m); }

/// Feed @p n epochs of the same kind, advancing the clock a minute each time.
/// Returns the last update.
NightSegmenter::Update feed(NightSegmenter &s, int16_t &clock, size_t n,
                            bool worn, uint32_t count, int32_t steps = 0)
{
    NightSegmenter::Update u;
    for (size_t i = 0; i < n; ++i) {
        u = s.update(clock, worn, count, steps);
        clock = static_cast<int16_t>((clock + 1) % Engine::kMinutesPerDay);
        if (u.event != Event::None) {
            return u;   // stop at the transition so the caller can inspect it
        }
    }
    return u;
}

// -- Windows that cross midnight ---------------------------------------------------

/// Counts either side of the segmenter's own thresholds, derived from them
/// rather than written as literals. The literals in this file were 10 and 4000,
/// chosen when stillnessCountMax was 60 and activityCountMin 250; when those
/// moved to measured values on 2026-08-20, 4000 stopped being "active" and the
/// close scenarios failed without saying why. Derived, a constant that moves
/// takes its fixtures with it.
constexpr uint32_t kStillCount  = Engine::SegmenterConfig{}.stillnessCountMax / 2;
constexpr uint32_t kActiveCount = Engine::SegmenterConfig{}.activityCountMin * 2;

TEST(Window, AWrappedWindowContainsBothSidesOfMidnight)
{
    const int16_t start = at(21, 0), end = at(11, 0);

    EXPECT_TRUE(inWindow(at(21,  0), start, end)) << "inclusive at the start";
    EXPECT_TRUE(inWindow(at(23, 59), start, end));
    EXPECT_TRUE(inWindow(at( 0,  0), start, end)) << "midnight itself";
    EXPECT_TRUE(inWindow(at( 3, 30), start, end));
    EXPECT_TRUE(inWindow(at(10, 59), start, end));

    EXPECT_FALSE(inWindow(at(11,  0), start, end)) << "exclusive at the end";
    EXPECT_FALSE(inWindow(at(14,  0), start, end));
    EXPECT_FALSE(inWindow(at(20, 59), start, end));
}

TEST(Window, AnUnwrappedWindowStillWorks)
{
    const int16_t start = at(1, 0), end = at(9, 0);

    EXPECT_FALSE(inWindow(at( 0, 59), start, end));
    EXPECT_TRUE (inWindow(at( 1,  0), start, end));
    EXPECT_TRUE (inWindow(at( 8, 59), start, end));
    EXPECT_FALSE(inWindow(at( 9,  0), start, end));
    EXPECT_FALSE(inWindow(at(23,  0), start, end));
}

TEST(Window, AZeroWidthWindowIsNeverRatherThanAlways)
{
    // The two readings are wildly different and one of them fails visibly: no
    // night is ever recorded and the user goes and looks at the setting.
    // "Always" would record continuously and look like it was working.
    EXPECT_FALSE(inWindow(at(22, 0), at(22, 0), at(22, 0)));
    EXPECT_FALSE(inWindow(at( 3, 0), at(22, 0), at(22, 0)));
}

TEST(Window, AnUnreadableClockIsOutsideEveryWindow)
{
    EXPECT_FALSE(inWindow(-1, at(21, 0), at(11, 0)));
    EXPECT_FALSE(inWindow(2000, at(21, 0), at(11, 0)));
}

// -- Opening ------------------------------------------------------------------------

TEST(Segmenter, ANightOpensOnSustainedStillnessInsideTheWindow)
{
    NightSegmenter s;
    int16_t clock = at(22, 30);

    // Fourteen still minutes is not yet enough.
    auto u = feed(s, clock, 14, true, kStillCount);
    EXPECT_EQ(u.event, Event::None);
    EXPECT_EQ(s.state(), State::Idle);

    // The fifteenth opens it.
    u = s.update(clock, true, 10, 0);
    EXPECT_EQ(u.event, Event::Opened);
    EXPECT_EQ(u.state, State::Open);
}

TEST(Segmenter, OpeningBackdatesToTheStartOfTheStillRun)
{
    // Those fifteen minutes were part of the night; they had simply not proved
    // it yet. Throwing them away would shorten every night by a quarter hour.
    NightSegmenter s;
    int16_t clock = at(22, 30);

    const auto u = feed(s, clock, 20, true, kStillCount);
    ASSERT_EQ(u.event, Event::Opened);
    EXPECT_EQ(u.backdateEpochs, SegmenterConfig{}.stillnessToOpenMin);
    EXPECT_EQ(s.sessionEpochs(), SegmenterConfig{}.stillnessToOpenMin);
}

TEST(Segmenter, StillnessOutsideTheWindowOpensNothing)
{
    // Sitting still at 15:00 is not going to bed.
    NightSegmenter s;
    int16_t clock = at(15, 0);
    EXPECT_EQ(feed(s, clock, 120, true, kStillCount).event, Event::None);
    EXPECT_EQ(s.state(), State::Idle);
}

TEST(Segmenter, StillnessWhileNotWornOpensNothing)
{
    // A watch on a desk overnight is as still as a sleeper and must not start
    // a night. The worn gate would catch it later; not starting is cheaper and
    // keeps the history clean.
    NightSegmenter s;
    int16_t clock = at(23, 0);
    EXPECT_EQ(feed(s, clock, 120, /*worn=*/false, 2).event, Event::None);
}

TEST(Segmenter, AMovementBreaksTheStillRun)
{
    NightSegmenter s;
    int16_t clock = at(22, 30);

    feed(s, clock, 10, true, kStillCount);
    s.update(clock, true, 5000, 0);        // one restless minute
    clock = static_cast<int16_t>(clock + 1);

    // Ten more still minutes: the run restarted, so this is not enough.
    EXPECT_EQ(feed(s, clock, 10, true, kStillCount).event, Event::None);
}

TEST(Segmenter, WithNoWallClockANightCannotOpen)
{
    // "Bedtime" is a time of day, and without a clock there is no such thing.
    NightSegmenter s;
    for (int i = 0; i < 200; ++i) {
        EXPECT_EQ(s.update(kAbsent, true, 5, 0).event, Event::None);
    }
}

// -- Closing ------------------------------------------------------------------------

TEST(Segmenter, StepsCloseANightOnceItIsLongEnough)
{
    // Steps are the least ambiguous out-of-bed signal there is: a sleeping
    // wrist does not accumulate them.
    NightSegmenter s;
    int16_t clock = at(22, 30);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);

    feed(s, clock, 300, true, kStillCount);

    const auto u = s.update(clock, true, 300, /*steps=*/40);
    EXPECT_EQ(u.event, Event::Closed);
    EXPECT_EQ(s.state(), State::Idle);
}

TEST(Segmenter, ATripToTheBathroomDoesNotSplitANightInTwo)
{
    // Before the minimum duration, activity cannot close the session. Without
    // that guard a 00:30 bathroom trip would end the night and start a second
    // one, and both halves would then fail the minimum and vanish entirely --
    // losing the whole night to a two-minute interruption.
    NightSegmenter s;
    int16_t clock = at(23, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);

    feed(s, clock, 30, true, kStillCount);
    // Fifteen active minutes with steps, well past activityToCloseMin -- but
    // only ~65 minutes into the session, under minSessionMin.
    const auto u = feed(s, clock, 15, true, kActiveCount, 60);

    EXPECT_EQ(u.event, Event::None);
    EXPECT_EQ(s.state(), State::Open);
}

TEST(Segmenter, SustainedActivityClosesANightThatIsLongEnough)
{
    NightSegmenter s;
    int16_t clock = at(22, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);
    feed(s, clock, 400, true, kStillCount);

    const auto u = feed(s, clock, 20, true, kActiveCount);
    EXPECT_EQ(u.event, Event::Closed);
}

TEST(Segmenter, OneRestlessMinuteDoesNotCloseANight)
{
    NightSegmenter s;
    int16_t clock = at(22, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);
    feed(s, clock, 300, true, kStillCount);

    // A turn-over: a few active minutes, then settled again.
    EXPECT_EQ(feed(s, clock, 3, true, kActiveCount).event, Event::None);
    EXPECT_EQ(feed(s, clock, 30, true, kStillCount).event, Event::None);
    EXPECT_EQ(s.state(), State::Open);
}

TEST(Segmenter, LeavingTheWindowClosesTheNightUnconditionally)
{
    // A session still open at 11:00 has stopped being a night whatever the
    // accelerometer says. The most likely cause is a watch left on a desk.
    NightSegmenter s;
    int16_t clock = at(22, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);

    // Run right through to 11:00, perfectly still the whole way.
    const auto u = feed(s, clock, 900, true, kStillCount);
    EXPECT_EQ(u.event, Event::Closed);
}

TEST(Segmenter, ASessionTooShortToReportIsDiscardedRatherThanClosed)
{
    // A 40-minute "night" in the history would corrupt the baseline as surely
    // as a wrong one, so the caller has to be told to throw it away.
    SegmenterConfig cfg;
    cfg.windowStartMin = at(22, 0);
    cfg.windowEndMin   = at(23, 0);    // a deliberately short window
    NightSegmenter s(cfg);

    int16_t clock = at(22, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);

    const auto u = feed(s, clock, 200, true, kStillCount);
    EXPECT_EQ(u.event, Event::Discarded);
    EXPECT_LT(u.sessionEpochs, cfg.minSessionMin);
}

TEST(Segmenter, AnOpenSessionSurvivesTheClockBecomingUnreadable)
{
    // The session is already known to be a night. Ending it over a clock the
    // app does not control would lose real data.
    NightSegmenter s;
    int16_t clock = at(23, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);

    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(s.update(kAbsent, true, 10, 0).event, Event::None);
    }
    EXPECT_EQ(s.state(), State::Open);
}

// -- Clock jumps ----------------------------------------------------------------------

TEST(Segmenter, AClockJumpIsReportedAndNotAbsorbed)
{
    NightSegmenter s;
    int16_t clock = at(22, 0);
    feed(s, clock, 20, true, kStillCount);

    // An hour forward, still inside the window: a timezone change, a host
    // sync, or DST.
    const auto u = s.update(static_cast<int16_t>(clock + 60), true, 10, 0);
    EXPECT_TRUE(u.clockJumped);
    EXPECT_EQ(u.event, Event::None) << "a jump alone does not end the night";
}

TEST(Segmenter, AJumpThatAlsoClosesTheSessionStillReportsTheJump)
{
    // The clock moving forward past the end of the window is both at once, and
    // it is the case where the flag matters most -- it is what marks the night
    // interrupted. An Update rebuilt inside the close path would drop it.
    NightSegmenter s;
    int16_t clock = at(22, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);
    feed(s, clock, 300, true, kStillCount);

    // Jump from the small hours to midday: out of the window, and a jump.
    const auto u = s.update(at(13, 0), true, 10, 0);
    EXPECT_NE(u.event, Event::None) << "the session should have closed";
    EXPECT_TRUE(u.clockJumped) << "and the jump must survive the close path";
}

TEST(Segmenter, CrossingMidnightIsNotAClockJump)
{
    // The one false positive that would mark every single night interrupted.
    NightSegmenter s;
    s.update(at(23, 58), true, 10, 0);
    EXPECT_FALSE(s.update(at(23, 59), true, 10, 0).clockJumped);
    EXPECT_FALSE(s.update(at( 0,  0), true, 10, 0).clockJumped);
    EXPECT_FALSE(s.update(at( 0,  1), true, 10, 0).clockJumped);
}

TEST(Segmenter, AnEpochThatLandsSlightlyLateIsNotAClockJump)
{
    NightSegmenter s;
    s.update(at(23, 0), true, 10, 0);
    EXPECT_FALSE(s.update(at(23, 2), true, 10, 0).clockJumped);
}

// -- Restart --------------------------------------------------------------------------

TEST(Segmenter, ResumeReentersAnOpenSessionRatherThanStartingASecond)
{
    // The USB cable, or a crash. Without this the morning would show two half
    // nights, and both might be short enough to be discarded.
    NightSegmenter s;
    s.resumeOpen(200);

    EXPECT_EQ(s.state(), State::Open);
    EXPECT_EQ(s.sessionEpochs(), 200);

    int16_t clock = at(2, 30);
    const auto u = feed(s, clock, 30, true, kStillCount);
    EXPECT_EQ(u.event, Event::None);
    EXPECT_EQ(s.state(), State::Open);
}

TEST(Segmenter, AResumedSessionDoesNotInheritAnActivityRun)
{
    // Whatever the wearer was doing before the restart is not evidence about
    // now. A resumed session carrying a nine-epoch activity run would close on
    // its very first active epoch afterwards.
    NightSegmenter s;
    s.resumeOpen(300);

    int16_t clock = at(3, 0);
    // Nine active minutes: one short of activityToCloseMin from a clean start.
    EXPECT_EQ(feed(s, clock, 9, true, kActiveCount).event, Event::None);
    EXPECT_EQ(s.state(), State::Open);
}

TEST(Segmenter, FinishClosesAnOpenSessionForShutdown)
{
    // The kernel is stopping the app, almost always because the USB cable went
    // in. A session left open is a night that never gets written.
    NightSegmenter s;
    int16_t clock = at(22, 0);
    ASSERT_EQ(feed(s, clock, 20, true, kStillCount).event, Event::Opened);
    feed(s, clock, 300, true, kStillCount);

    const auto u = s.finish();
    EXPECT_EQ(u.event, Event::Closed);
    EXPECT_EQ(s.state(), State::Idle);
}

TEST(Segmenter, FinishOnAnIdleSegmenterDoesNothing)
{
    NightSegmenter s;
    EXPECT_EQ(s.finish().event, Event::None);
}

/// A window one minute wide. Nothing forbids it -- `min_night_min` and the window
/// are validated independently -- and a session cannot open in it, because opening
/// needs fifteen consecutive still epochs *inside* the window. It must fail
/// visibly, by recording nothing, rather than by opening something it then cannot
/// close.
TEST(Segmenter, AWindowOneMinuteWideOpensNothing)
{
    Engine::SegmenterConfig cfg;
    cfg.windowStartMin = 23 * 60;
    cfg.windowEndMin   = 23 * 60 + 1;
    Engine::NightSegmenter seg(cfg);

    // Twenty passes over the one minute the window contains, still and worn every
    // time. The still run resets each time the clock leaves the window, which is
    // every other update.
    for (int i = 0; i < 20; ++i) {
        seg.update(23 * 60, true, 10, Engine::kAbsent);
        seg.update(23 * 60 + 1, true, 10, Engine::kAbsent);
    }
    EXPECT_EQ(seg.state(), Engine::NightSegmenter::State::Idle)
        << "a one-minute window opened a session";
}

/// A session that reaches its fifteenth still epoch on the *last* minute of the
/// window. It opens -- the run was inside the window -- and then the very next
/// epoch is outside it and closes the session, which is short and must be
/// discarded rather than reported.
TEST(Segmenter, ASessionOpeningInTheLastMinuteOfTheWindowIsDiscardedNotReported)
{
    Engine::SegmenterConfig cfg;
    cfg.windowStartMin = 21 * 60;
    cfg.windowEndMin   = 11 * 60;
    Engine::NightSegmenter seg(cfg);

    // Fifteen still epochs ending at 10:59, the last minute inside the window.
    int16_t minute = static_cast<int16_t>(11 * 60 - 15);
    Engine::NightSegmenter::Update u;
    for (int i = 0; i < 15; ++i, ++minute) {
        u = seg.update(minute, true, 10, Engine::kAbsent);
    }
    ASSERT_EQ(u.event, Engine::NightSegmenter::Event::Opened);
    ASSERT_EQ(u.backdateEpochs, 15);

    // 11:00 is outside the window, so the session ends immediately.
    u = seg.update(static_cast<int16_t>(11 * 60), true, 10, Engine::kAbsent);
    EXPECT_EQ(u.event, Engine::NightSegmenter::Event::Discarded)
        << "a sixteen-minute session was reported as a night";
    EXPECT_EQ(seg.state(), Engine::NightSegmenter::State::Idle);
}

/// `resumeOpen` is given an epoch count from a state file, which is a number off
/// disk and can be anything. A count past the array the caller holds must not
/// produce a session that claims to be longer than the engine can score.
TEST(Segmenter, ResumeWithAnAbsurdEpochCountIsStillBounded)
{
    Engine::NightSegmenter seg;
    seg.resumeOpen(0xFFFFu);
    ASSERT_EQ(seg.state(), Engine::NightSegmenter::State::Open);

    const Engine::NightSegmenter::Update u =
        seg.update(2 * 60, true, 10, Engine::kAbsent);
    EXPECT_EQ(u.event, Engine::NightSegmenter::Event::Closed)
        << "a resumed session claiming 65 535 epochs kept running";
    EXPECT_LE(seg.sessionEpochs(), Engine::kMaxScoringEpochs);
}

} // namespace

// ---------------------------------------------------------------------------
// A session that cannot end
// ---------------------------------------------------------------------------

/// `SleepWakeScorer.hpp` says of its 16-hour bound: "a 'night' that ran 16 hours
/// is already a data-quality problem, and the segmenter is what should catch it,
/// not an array bound silently truncating it."
///
/// There is no such rule here. Leaving the bedtime window is the only thing that
/// ends a long session, and that needs a readable wall clock -- which an open
/// session deliberately does not require, because losing real data over a clock
/// the app does not control would be the wrong trade. So a session opened while
/// the clock was readable and continued after it stopped being readable never
/// ends: it keeps counting, `mSessionEpochs` is a uint16 and wraps at 45 days,
/// and once it has wrapped below `minSessionMin` the session can no longer even
/// close on activity.
TEST(NightSegmenter, ASessionCannotRunForEver)
{
    Engine::NightSegmenter seg;

    // Open it honestly: fifteen still, worn epochs inside the window.
    for (int i = 0; i < 15; ++i) {
        seg.update(22 * 60, true, 10, Engine::kAbsent);
    }
    ASSERT_EQ(seg.state(), Engine::NightSegmenter::State::Open);

    // Then the wall clock stops being readable -- a device that lost its clock,
    // or a launch before the host has synced one.
    bool closed = false;
    for (long i = 0; i < 40000 && !closed; ++i) {
        const Engine::NightSegmenter::Update u =
            seg.update(-1, true, 10, Engine::kAbsent);
        if (u.event == Engine::NightSegmenter::Event::Closed ||
            u.event == Engine::NightSegmenter::Event::Discarded) {
            closed = true;
        }
    }

    EXPECT_TRUE(closed)
        << "the session was still open after 40 000 epochs -- 28 days -- with "
           "sessionEpochs at " << seg.sessionEpochs();
    // And whenever it does end, its length must be a real length rather than one
    // that has been round a uint16.
    EXPECT_LE(seg.sessionEpochs(), Engine::kMaxScoringEpochs);
}

/// The bound has to be a bound on the *session*, not only a guard on the clock:
/// a window wide enough to hold sixteen hours must not produce a night longer
/// than the engine will score.
TEST(NightSegmenter, ASessionInAVeryWideWindowIsStillBounded)
{
    Engine::SegmenterConfig cfg;
    cfg.windowStartMin = 0;
    cfg.windowEndMin   = 23 * 60 + 59;   // very nearly all day
    Engine::NightSegmenter seg(cfg);

    for (int i = 0; i < 15; ++i) {
        seg.update(60, true, 10, Engine::kAbsent);
    }
    ASSERT_EQ(seg.state(), Engine::NightSegmenter::State::Open);

    bool closed = false;
    int16_t minute = 60;
    for (long i = 0; i < 40000 && !closed; ++i) {
        minute = static_cast<int16_t>((minute + 1) % Engine::kMinutesPerDay);
        const Engine::NightSegmenter::Update u =
            seg.update(minute, true, 10, Engine::kAbsent);
        if (u.event == Engine::NightSegmenter::Event::Closed ||
            u.event == Engine::NightSegmenter::Event::Discarded) {
            closed = true;
            EXPECT_LE(u.sessionEpochs, Engine::kMaxScoringEpochs)
                << "the session ran " << u.sessionEpochs
                << " epochs, past what the engine will score";
        }
    }
    EXPECT_TRUE(closed);
}
