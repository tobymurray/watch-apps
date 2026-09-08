#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "FrameBuilder.hpp"
#include "Probe.hpp"

using namespace Haptic;

namespace {

/// The state the frame builder reads, driven to a chosen stage.
class Fixture
{
public:
    explicit Fixture(SetId set = SetId::RhythmAndFamily, uint32_t seed = 4242)
    {
        mPatternCount = buildSet(set, mPatterns);
        buildPlan(set, 1, seed, mPlan);
        mTiming  = timingFor(mPatterns, mPatternCount, EffectDurations::nominal());
        mSession = new Session(mPlan, mPatterns, mTiming);
    }

    ~Fixture() { delete mSession; }

    Session &session() { return *mSession; }
    const Plan &plan() const { return mPlan; }
    const Pattern *patterns() const { return mPatterns; }
    uint8_t patternCount() const { return mPatternCount; }

    haptic_lab_gui_frame frame(Mode mode = Mode::Session)
    {
        FrameInputs in;
        in.mode         = mode;
        in.patterns     = mPatterns;
        in.patternCount = mPatternCount;
        in.plan         = &mPlan;
        in.session      = mode == Mode::Session ? mSession : nullptr;
        in.probeCount   = kProbeCaseCount;

        haptic_lab_gui_frame out {};
        buildFrame(in, out);
        return out;
    }

    /// Walks the state machine to the response window of the first trial.
    void reachAnswer()
    {
        EventDisposition ignored = EventDisposition::Unmapped;
        uint32_t now = 1000;
        mSession->onButton(kButtonSlot1, kEventClick, now, now, ignored);
        for (uint8_t b = 0; b < kButtonCount; b++) {
            mSession->onButton(b, kEventClick, now, now, ignored);
        }
        mSession->onButton(kButtonSlot1, kEventClick, now, now, ignored);
        mSession->onButton(kButtonNotFelt, kEventClick, now, now, ignored);
        while (mSession->stage() != Stage::TrialAnswer) {
            now += 100;
            const Step step = mSession->onTick(now);
            if (step.playPattern && step.trial != kNoTrial) {
                mSession->notePlayClocks(now, now + 3, now);
            }
            mSession->noteFramePushed(now);
        }
    }

    /// Rewrites the plan so the trial the session is on presents `slot` of its
    /// triple, which is the only difference between the three cases the leak
    /// test compares.
    void presentSlot(uint8_t slot) { mPlan.trials[mSession->trialIndex()].slot = slot; }

private:
    Pattern  mPatterns[kMaxPatterns] {};
    uint8_t  mPatternCount = 0;
    Plan     mPlan {};
    Timing   mTiming {};
    Session *mSession = nullptr;
};

std::string bytes(const haptic_lab_gui_frame &frame)
{
    return std::string(reinterpret_cast<const char *>(&frame), sizeof(frame));
}

} // namespace

/// The scientific requirement, as a property of the code rather than a promise
/// in a document: the frame a trial screen is drawn from is byte-identical
/// whichever member of the triple was presented.
TEST(FrameBuilder, TheTrialFrameIsIdenticalWhicheverMemberWasPresented)
{
    Fixture fixture;
    fixture.reachAnswer();
    ASSERT_EQ(fixture.session().stage(), Stage::TrialAnswer);

    std::vector<std::string> frames;
    for (uint8_t slot = 0; slot < 3; slot++) {
        fixture.presentSlot(slot);
        frames.push_back(bytes(fixture.frame()));
    }

    EXPECT_EQ(frames[0], frames[1]);
    EXPECT_EQ(frames[1], frames[2]);
}

TEST(FrameBuilder, TheTrialFrameCarriesTheThreeNamesInTheTripleSOwnOrder)
{
    Fixture fixture;
    fixture.reachAnswer();
    const haptic_lab_gui_frame frame = fixture.frame();

    const Triple &triple = fixture.session().offered();
    for (uint8_t slot = 0; slot < 3; slot++) {
        EXPECT_STREQ(frame.alt[slot], fixture.patterns()[triple.member[slot]].name);
    }
    EXPECT_LT(triple.member[0], triple.member[1]);
    EXPECT_LT(triple.member[1], triple.member[2]);
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_ANSWER);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_WHICH_ONE);
    EXPECT_STREQ(frame.name, "") << "the trial frame carried a pattern's own name";
    EXPECT_EQ(frame.modelled_duration_ms, 0u) << "the trial frame carried a duration";
}

TEST(FrameBuilder, TheWaitAndPlayingFramesCarryNothingAtAll)
{
    Fixture fixture;
    fixture.reachAnswer();

    // Answer, then walk to the next trial's wait and playing stages.
    EventDisposition ignored = EventDisposition::Unmapped;
    uint32_t now = 500000;
    fixture.session().onButton(kButtonSlot0, kEventClick, now, now, ignored);
    while (fixture.session().stage() != Stage::TrialWait) {
        now += 100;
        fixture.session().onTick(now);
    }

    haptic_lab_gui_frame wait = fixture.frame();
    EXPECT_EQ(wait.screen, HAPTIC_LAB_SCREEN_WAIT);
    EXPECT_STREQ(wait.name, "");
    EXPECT_STREQ(wait.alt[0], "");
    EXPECT_EQ(wait.modelled_duration_ms, 0u);
    EXPECT_EQ(wait.feedback, HAPTIC_LAB_FEEDBACK_NONE);

    while (fixture.session().stage() != Stage::TrialPlaying) {
        now += 100;
        const Step step = fixture.session().onTick(now);
        if (step.playPattern && step.trial != kNoTrial) {
            fixture.session().notePlayClocks(now, now + 3, now);
        }
    }
    haptic_lab_gui_frame playing = fixture.frame();
    EXPECT_EQ(playing.screen, HAPTIC_LAB_SCREEN_PLAYING);
    EXPECT_STREQ(playing.name, "");
    EXPECT_STREQ(playing.alt[0], "");
    EXPECT_EQ(playing.modelled_duration_ms, 0u);
}

/// The learning screen is *allowed* to name a pattern and say how long it is,
/// because absolute identification of a learned set is the thing being
/// measured. It is a different stage from a trial, which is what makes the
/// leakage structurally impossible rather than merely avoided.
TEST(FrameBuilder, TheLearningFrameNamesThePatternAndSaysHowLongItIs)
{
    Fixture fixture;
    EventDisposition ignored = EventDisposition::Unmapped;
    uint32_t now = 1000;
    fixture.session().onButton(kButtonSlot1, kEventClick, now, now, ignored);
    for (uint8_t b = 0; b < kButtonCount; b++) {
        fixture.session().onButton(b, kEventClick, now, now, ignored);
    }
    fixture.session().onButton(kButtonSlot1, kEventClick, now, now, ignored);
    ASSERT_EQ(fixture.session().stage(), Stage::Learn);

    const haptic_lab_gui_frame frame = fixture.frame();
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_LEARN);
    EXPECT_STREQ(frame.name, fixture.patterns()[0].name);
    EXPECT_GT(frame.modelled_duration_ms, 0u);
    EXPECT_EQ(frame.learn_count, fixture.patternCount());
    EXPECT_STREQ(frame.alt[0], "");
}

TEST(FrameBuilder, TheFeedbackFrameNamesWhatWasPlayedAndSaysHowItWent)
{
    Fixture fixture;
    fixture.reachAnswer();
    const uint8_t presented = presentedPattern(fixture.plan(), fixture.session().trialIndex());
    const Triple &triple    = fixture.session().offered();

    // The wrong one on purpose: a correct answer is the easy case.
    uint8_t wrongSlot = 0;
    for (uint8_t slot = 0; slot < 3; slot++) {
        if (triple.member[slot] != presented) {
            wrongSlot = slot;
            break;
        }
    }

    EventDisposition ignored = EventDisposition::Unmapped;
    fixture.session().onButton(static_cast<uint8_t>(kButtonSlot0 + wrongSlot), kEventClick, 1, 1,
                               ignored);
    ASSERT_EQ(fixture.session().stage(), Stage::TrialFeedback);

    const haptic_lab_gui_frame frame = fixture.frame();
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_FEEDBACK);
    EXPECT_EQ(frame.feedback, HAPTIC_LAB_FEEDBACK_WRONG);
    EXPECT_STREQ(frame.name, fixture.patterns()[presented].name);
}

TEST(FrameBuilder, EveryFeedbackVerdictHasItsOwnValue)
{
    std::vector<uint8_t> seen { HAPTIC_LAB_FEEDBACK_CORRECT, HAPTIC_LAB_FEEDBACK_WRONG,
                                HAPTIC_LAB_FEEDBACK_MISS, HAPTIC_LAB_FEEDBACK_TIMEOUT,
                                HAPTIC_LAB_FEEDBACK_NONE };
    std::sort(seen.begin(), seen.end());
    EXPECT_EQ(std::unique(seen.begin(), seen.end()), seen.end());
}

TEST(FrameBuilder, TheCalibrationFrameSaysWhichButtonsHaveBeenSeen)
{
    Fixture fixture;
    EventDisposition ignored = EventDisposition::Unmapped;
    fixture.session().onButton(kButtonSlot1, kEventClick, 1, 1, ignored);
    fixture.session().onButton(kButtonSlot2, kEventClick, 1, 1, ignored);

    haptic_lab_gui_frame frame = fixture.frame();
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_CALIBRATION);
    EXPECT_EQ(frame.calibration_mask, (1u << kButtonSlot2));
    EXPECT_EQ(frame.last_button, kButtonSlot2);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_CALIBRATE);

    fixture.session().onButton(kButtonSlot0, kEventClick, 1, 1, ignored);
    fixture.session().onButton(kButtonSlot1, kEventClick, 1, 1, ignored);
    fixture.session().onButton(kButtonNotFelt, kEventClick, 1, 1, ignored);
    frame = fixture.frame();
    EXPECT_EQ(frame.calibration_mask, 0b1111u);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_CALIBRATE_DONE);
}

TEST(FrameBuilder, TheMenuSaysWhichConditionTheRunIsConfiguredFor)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::RhythmAndFamily, patterns);
    Plan plan {};
    buildPlan(SetId::RhythmAndFamily, 1, 1, plan);

    FrameInputs in;
    in.mode         = Mode::Menu;
    in.patterns     = patterns;
    in.patternCount = count;
    in.plan         = &plan;
    in.contact      = Contact::Table;
    in.masking      = Masking::Masked;

    haptic_lab_gui_frame frame {};
    buildFrame(in, frame);
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_MENU);
    EXPECT_EQ(frame.contact, static_cast<uint8_t>(Contact::Table));
    EXPECT_EQ(frame.masking, static_cast<uint8_t>(Masking::Masked));
    EXPECT_EQ(frame.pattern_count, count);
    EXPECT_EQ(frame.trials_total, plan.trialCount);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_MENU);
}

/// A run that cannot be recorded has to say so before the wrist-hour, not
/// after it.
TEST(FrameBuilder, TheMenuSaysSoWhenNothingCanBeWritten)
{
    Pattern patterns[kMaxPatterns] {};
    const uint8_t count = buildSet(SetId::RhythmAndFamily, patterns);
    Plan plan {};
    buildPlan(SetId::RhythmAndFamily, 1, 1, plan);

    FrameInputs in;
    in.mode         = Mode::Menu;
    in.patterns     = patterns;
    in.patternCount = count;
    in.plan         = &plan;
    in.filesOk      = false;

    haptic_lab_gui_frame frame {};
    buildFrame(in, frame);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_NO_FILES);

    in.filesOk = true;
    in.capHit  = true;
    buildFrame(in, frame);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_STORAGE_FULL);
}

TEST(FrameBuilder, ASessionModeWithoutASessionFallsBackToTheMenuRatherThanDrawingRubbish)
{
    FrameInputs in;
    in.mode = Mode::Session;

    haptic_lab_gui_frame frame {};
    buildFrame(in, frame);
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_MENU);
    EXPECT_EQ(frame.message_id, HAPTIC_LAB_MSG_NO_FILES);
}

TEST(FrameBuilder, TheProbeFrameCarriesTheIndexAndTheCountTheRendererChecksAgainst)
{
    Fixture fixture;
    FrameInputs in;
    in.mode       = Mode::Probe;
    in.probeIndex = 4;
    in.probeCount = kProbeCaseCount;

    haptic_lab_gui_frame frame {};
    buildFrame(in, frame);
    EXPECT_EQ(frame.screen, HAPTIC_LAB_SCREEN_PROBE);
    EXPECT_EQ(frame.probe_index, 4);
    EXPECT_EQ(frame.probe_count, kProbeCaseCount);
}
