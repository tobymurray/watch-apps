#include <gtest/gtest.h>

#include <vector>

#include "Protocol.hpp"

using namespace Haptic;

namespace {

/// Drives a whole session the way the GUI half does: ticks at the panel's own
/// 10 fps, and clicks when the harness says so.
class Harness
{
public:
    explicit Harness(SetId set = SetId::RhythmAndFamily, uint8_t passes = 1, uint32_t seed = 4242)
    {
        mPatternCount = buildSet(set, mPatterns);
        buildPlan(set, passes, seed, mPlan);
        mTiming  = timingFor(mPatterns, mPatternCount, EffectDurations::nominal());
        mSession = new Session(mPlan, mPatterns, mTiming);
    }

    ~Harness() { delete mSession; }

    Session &session() { return *mSession; }
    const Plan &plan() const { return mPlan; }
    const Timing &timing() const { return mTiming; }
    uint32_t now() const { return mNow; }
    const std::vector<TrialRecord> &trials() const { return mTrials; }
    const std::vector<uint8_t> &played() const { return mPlayed; }

    /// One frame of the GUI's tick, including the two clocks only the caller
    /// can know and the frame-pushed callback.
    void tick()
    {
        mNow += kFrameMs;
        apply(mSession->onTick(mNow));
        mSession->noteFramePushed(mNow);
    }

    void tickFor(uint32_t ms)
    {
        for (uint32_t elapsed = 0; elapsed < ms; elapsed += kFrameMs) {
            tick();
        }
    }

    EventDisposition click(uint8_t button)
    {
        EventDisposition disposition = EventDisposition::Unmapped;
        apply(mSession->onButton(button, kEventClick, mNow, mNow, disposition));
        return disposition;
    }

    EventDisposition press(uint8_t button, uint8_t event)
    {
        EventDisposition disposition = EventDisposition::Unmapped;
        apply(mSession->onButton(button, event, mNow, mNow, disposition));
        return disposition;
    }

    /// Presses the button of whichever alternative is the right one, which is
    /// what a wearer who can tell them all apart would do.
    EventDisposition answerCorrectly()
    {
        const uint8_t presented = presentedPattern(mPlan, mSession->trialIndex());
        const Triple &triple    = mSession->offered();
        for (uint8_t slot = 0; slot < 3; slot++) {
            if (triple.member[slot] == presented) {
                return click(static_cast<uint8_t>(kButtonSlot0 + slot));
            }
        }
        return EventDisposition::Unmapped;
    }

    void reachFirstTrial()
    {
        click(kButtonSlot1); // start, from Idle
        click(kButtonSlot0);
        click(kButtonSlot1);
        click(kButtonSlot2);
        click(kButtonNotFelt);
        EXPECT_TRUE(mSession->calibrated());
        click(kButtonSlot1); // calibration is proven; begin
        EXPECT_EQ(mSession->stage(), Stage::Learn);
        click(kButtonNotFelt); // done learning
        EXPECT_EQ(mSession->stage(), Stage::TrialWait);
    }

    static constexpr uint32_t kFrameMs = 100;

private:
    void apply(const Step &step)
    {
        if (step.playPattern) {
            mPlayed.push_back(step.pattern);
            if (step.trial != kNoTrial) {
                mSession->notePlayClocks(mNow, mNow + 3, mNow);
            }
        }
        if (step.recordTrial) {
            mTrials.push_back(step.record);
        }
    }

    Pattern  mPatterns[kMaxPatterns] {};
    uint8_t  mPatternCount = 0;
    Plan     mPlan {};
    Timing   mTiming {};
    Session *mSession = nullptr;
    uint32_t mNow     = 1000;

    std::vector<TrialRecord> mTrials;
    std::vector<uint8_t>     mPlayed;
};

} // namespace

TEST(Session, TheRunStartsIdleAndOnlyR1MovesIt)
{
    Harness h;
    EXPECT_EQ(h.session().stage(), Stage::Idle);
    h.click(kButtonSlot0);
    EXPECT_EQ(h.session().stage(), Stage::Idle);
    h.click(kButtonSlot1);
    EXPECT_EQ(h.session().stage(), Stage::Calibration);
}

/// If the wearer can be wrong about which button they pressed, every confusion
/// number is contaminated by response error rather than perceptual error.
TEST(Session, CalibrationNeedsAllFourButtonsBeforeItWillGoOn)
{
    Harness h;
    h.click(kButtonSlot1);

    h.click(kButtonSlot0);
    h.click(kButtonSlot1);
    EXPECT_FALSE(h.session().calibrated());
    EXPECT_EQ(h.session().stage(), Stage::Calibration) << "R1 advanced before all four were seen";

    h.click(kButtonSlot2);
    h.click(kButtonNotFelt);
    EXPECT_TRUE(h.session().calibrated());
    EXPECT_EQ(h.session().stage(), Stage::Calibration) << "the press that completed the set also advanced";

    h.click(kButtonSlot1);
    EXPECT_EQ(h.session().stage(), Stage::Learn);
}

TEST(Session, TheLearningStageReplaysOnDemandAndWalksTheSetBothWays)
{
    Harness h;
    h.click(kButtonSlot1);
    for (uint8_t b = 0; b < kButtonCount; b++) {
        h.click(b);
    }
    h.click(kButtonSlot1);
    ASSERT_EQ(h.session().stage(), Stage::Learn);

    EXPECT_EQ(h.session().learnIndex(), 0);
    h.click(kButtonSlot1); // play
    ASSERT_EQ(h.played().size(), 1u);
    EXPECT_EQ(h.played().back(), 0);

    h.click(kButtonSlot2); // next
    EXPECT_EQ(h.session().learnIndex(), 1);
    h.click(kButtonSlot0); // previous
    EXPECT_EQ(h.session().learnIndex(), 0);
    h.click(kButtonSlot0); // previous, wrapping
    EXPECT_EQ(h.session().learnIndex(), h.plan().patternCount - 1);
}

TEST(Session, ATrialPlaysOnceTheInterTrialIntervalHasElapsedAndNotBefore)
{
    Harness h;
    h.reachFirstTrial();
    const size_t before = h.played().size();

    h.tickFor(kInterTrialMs - 2 * Harness::kFrameMs);
    EXPECT_EQ(h.played().size(), before) << "played before the interval was up";
    EXPECT_EQ(h.session().stage(), Stage::TrialWait);

    h.tickFor(3 * Harness::kFrameMs);
    EXPECT_EQ(h.played().size(), before + 1);
    EXPECT_EQ(h.session().stage(), Stage::TrialPlaying);
}

/// The presentation window is the same length for every pattern, so how long
/// the screen says PLAYING leaks nothing about which pattern played.
TEST(Session, TheResponseWindowOpensAFixedTimeAfterTheAsk)
{
    Harness h;
    h.reachFirstTrial();
    h.tickFor(kInterTrialMs + Harness::kFrameMs);
    ASSERT_EQ(h.session().stage(), Stage::TrialPlaying);
    const uint32_t askedAt = h.now();

    h.tickFor(h.timing().presentationMs - 2 * Harness::kFrameMs);
    EXPECT_EQ(h.session().stage(), Stage::TrialPlaying);

    h.tickFor(3 * Harness::kFrameMs);
    ASSERT_EQ(h.session().stage(), Stage::TrialAnswer);
    const int32_t opened = sinceTick(askedAt, h.now());
    EXPECT_GE(opened, static_cast<int32_t>(h.timing().presentationMs));
    EXPECT_LT(opened, static_cast<int32_t>(h.timing().presentationMs) + 4 * static_cast<int32_t>(Harness::kFrameMs));
}

/// A press during playback is evidence the wearer decided before the pattern
/// finished, which is a real result about that pattern and is invisible if only
/// accepted responses are kept.
TEST(Session, APressDuringPlaybackIsRefusedAndSaysWhy)
{
    Harness h;
    h.reachFirstTrial();
    h.tickFor(kInterTrialMs + Harness::kFrameMs);
    ASSERT_EQ(h.session().stage(), Stage::TrialPlaying);

    EXPECT_EQ(h.click(kButtonSlot0), EventDisposition::DuringPlayback);
    EXPECT_EQ(h.trials().size(), 0u) << "a press during playback was taken as a response";
    EXPECT_EQ(h.session().stage(), Stage::TrialPlaying);
}

TEST(Session, APressBetweenTrialsIsRefusedAndSaysWhy)
{
    Harness h;
    h.reachFirstTrial();
    EXPECT_EQ(h.click(kButtonNotFelt), EventDisposition::BetweenTrials)
        << "the miss key between trials must not abort the run";
    EXPECT_EQ(h.session().stage(), Stage::TrialWait);
}

TEST(Session, OnlyClicksAreActedOn)
{
    Harness h;
    h.reachFirstTrial();
    for (uint8_t event : { 0u, 1u, 3u, 4u, 5u, 6u }) { // PRESS, RELEASE, LONG_PRESS, HOLD_*
        EXPECT_EQ(h.press(kButtonSlot0, event), EventDisposition::NotAClick) << int(event);
    }
}

TEST(Session, ACorrectAnswerIsRecordedWithItsClocksAndItsParameterisation)
{
    Harness h;
    h.reachFirstTrial();
    h.tickFor(kInterTrialMs + h.timing().presentationMs + 2 * Harness::kFrameMs);
    ASSERT_EQ(h.session().stage(), Stage::TrialAnswer);

    const uint8_t presented = presentedPattern(h.plan(), h.session().trialIndex());
    EXPECT_EQ(h.answerCorrectly(), EventDisposition::TakenAsResponse);
    ASSERT_EQ(h.trials().size(), 1u);

    const TrialRecord &row = h.trials().front();
    EXPECT_TRUE(row.correct);
    EXPECT_FALSE(row.miss);
    EXPECT_EQ(row.presented, presented);
    EXPECT_EQ(row.blockKind, BlockKind::Training);
    EXPECT_GT(row.noteCount, 0) << "the parameterisation was not copied inline";
    EXPECT_GT(row.modelledDurationMs, 0u);
    EXPECT_GT(row.tickBeforeSend, 0u);
    EXPECT_GT(row.tickAtAccepted, row.tickBeforeSend);
    EXPECT_GT(row.modelledEndTick, row.tickBeforeSend);
    EXPECT_GT(row.windowOpenDueTick, 0u);
    EXPECT_GT(row.windowOpenDrawnTick, 0u);
    EXPECT_GE(row.reactionMs, 0);
    // Every slot's button, so a mapping that was supposed to be fixed and was
    // not is visible in the data.
    EXPECT_EQ(row.buttonForSlot[0], kButtonSlot0);
    EXPECT_EQ(row.buttonForSlot[1], kButtonSlot1);
    EXPECT_EQ(row.buttonForSlot[2], kButtonSlot2);
    EXPECT_EQ(row.missButton, kButtonNotFelt);
}

TEST(Session, TheMissKeyIsAMissRatherThanAWrongAnswer)
{
    Harness h;
    h.reachFirstTrial();
    h.tickFor(kInterTrialMs + h.timing().presentationMs + 2 * Harness::kFrameMs);
    ASSERT_EQ(h.session().stage(), Stage::TrialAnswer);

    h.click(kButtonNotFelt);
    ASSERT_EQ(h.trials().size(), 1u);
    EXPECT_TRUE(h.trials().front().miss);
    EXPECT_FALSE(h.trials().front().correct);
    EXPECT_EQ(h.trials().front().response, Response::NotFelt);
}

/// A window that closed with nothing pressed is not the same as a wearer
/// saying they felt nothing, and the two must not be one column.
TEST(Session, ATimeoutIsDistinctFromAMiss)
{
    Harness h;
    h.reachFirstTrial();
    h.tickFor(kInterTrialMs + h.timing().presentationMs + 2 * Harness::kFrameMs);
    ASSERT_EQ(h.session().stage(), Stage::TrialAnswer);

    h.tickFor(kResponseTimeoutMs + 2 * Harness::kFrameMs);
    ASSERT_GE(h.trials().size(), 1u);
    const TrialRecord &row = h.trials().front();
    EXPECT_EQ(row.response, Response::None);
    EXPECT_FALSE(row.miss);
    EXPECT_FALSE(row.correct);
    EXPECT_EQ(row.responseButton, kNoButton);
    EXPECT_EQ(row.reactionMs, 0) << "a reaction time was invented for a response nobody gave";
}

TEST(Session, TrainingShowsFeedbackAndTheTestBlocksDoNot)
{
    Harness h;
    h.reachFirstTrial();
    h.tickFor(kInterTrialMs + h.timing().presentationMs + 2 * Harness::kFrameMs);
    ASSERT_EQ(h.session().blockKind(), BlockKind::Training);
    h.answerCorrectly();
    EXPECT_EQ(h.session().stage(), Stage::TrialFeedback);
    EXPECT_TRUE(h.session().lastWasCorrect());

    h.tickFor(kFeedbackMs + 2 * Harness::kFrameMs);
    EXPECT_EQ(h.session().stage(), Stage::TrialWait);
}

/// The whole run, driven the way a wearer would: every trial answered
/// correctly, every rest continued through.
TEST(Session, AWholeRunReachesDoneWithOneRowPerTrial)
{
    Harness h(SetId::RhythmAndFamily, 1, 909);
    h.reachFirstTrial();

    for (int guard = 0; guard < 200000 && h.session().stage() != Stage::Done; guard++) {
        switch (h.session().stage()) {
        case Stage::TrialAnswer:
            h.answerCorrectly();
            break;
        case Stage::Rest:
            h.click(kButtonSlot1);
            break;
        default:
            h.tick();
            break;
        }
    }

    EXPECT_EQ(h.session().stage(), Stage::Done);
    EXPECT_EQ(h.trials().size(), h.plan().trialCount);
    for (const TrialRecord &row : h.trials()) {
        EXPECT_TRUE(row.correct) << "trial " << row.trialInRun;
    }

    // Every trial index appears exactly once, in order.
    for (size_t i = 0; i < h.trials().size(); i++) {
        EXPECT_EQ(h.trials()[i].trialInRun, i);
    }
}

TEST(Session, TheRestScreenEndsTheRunOnR2AndContinuesOnR1)
{
    Harness h(SetId::RhythmAndFamily, 1, 5150);
    h.reachFirstTrial();
    for (int guard = 0; guard < 200000 && h.session().stage() != Stage::Rest; guard++) {
        if (h.session().stage() == Stage::TrialAnswer) {
            h.answerCorrectly();
        } else {
            h.tick();
        }
    }
    ASSERT_EQ(h.session().stage(), Stage::Rest);

    h.click(kButtonNotFelt);
    EXPECT_EQ(h.session().stage(), Stage::Aborted);
}

/// `getTimeMs()` is device uptime: 32-bit, wrapping at about 49.7 days. Every
/// duration goes through one signed difference, so a run across the wrap has to
/// behave like a run away from it.
TEST(Session, ARunAcrossTheUptimeWrapBehavesLikeATwinAwayFromIt)
{
    EXPECT_EQ(sinceTick(0xFFFFFF00u, 0x00000100u), 512);
    EXPECT_EQ(sinceTick(0x00000100u, 0xFFFFFF00u), -512);

    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::RhythmAndFamily, patterns);
    Plan plan {};
    buildPlan(SetId::RhythmAndFamily, 1, 1, plan);
    const Timing timing = timingFor(patterns, count, EffectDurations::nominal());

    const auto runFrom = [&](uint32_t start) {
        Session session(plan, patterns, timing);
        uint32_t now = start;
        EventDisposition ignored = EventDisposition::Unmapped;
        session.onButton(kButtonSlot1, kEventClick, now, now, ignored);
        for (uint8_t b = 0; b < kButtonCount; b++) {
            session.onButton(b, kEventClick, now, now, ignored);
        }
        session.onButton(kButtonSlot1, kEventClick, now, now, ignored);
        session.onButton(kButtonNotFelt, kEventClick, now, now, ignored);

        std::vector<uint32_t> stageChanges;
        Stage last = session.stage();
        for (int i = 0; i < 200; i++) {
            now += 100;
            session.onTick(now);
            session.noteFramePushed(now);
            if (session.stage() != last) {
                stageChanges.push_back(static_cast<uint32_t>(i));
                last = session.stage();
            }
        }
        return stageChanges;
    };

    EXPECT_EQ(runFrom(0xFFFFF000u), runFrom(1000u));
}
