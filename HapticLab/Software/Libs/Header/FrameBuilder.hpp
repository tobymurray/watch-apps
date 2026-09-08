#ifndef HAPTICLAB_FRAMEBUILDER_HPP
#define HAPTICLAB_FRAMEBUILDER_HPP

#include <cstdint>
#include <cstring>

#include "Patterns.hpp"
#include "Protocol.hpp"
#include "Records.hpp"
#include "Schedule.hpp"
#include "haptic_lab_gui.h"

/// Turns the session's state into the one struct the renderer draws. This is
/// the file the scientific requirement lives in: a trial screen cannot name,
/// hint at or narrow the pattern about to be played, and the only way to be
/// sure of that is for the code that builds a trial frame never to read the
/// presented pattern. `Tests/FrameBuilder_test.cpp` is what holds it.
namespace Haptic {

/// What the app is doing, which is more than the protocol knows about: the
/// probe and the effect sweep are not part of a session.
enum class Mode : uint8_t {
    Menu = 0,
    Probe,
    ProbeSent,
    Sweep,
    Session,
    Count,
};

struct FrameInputs {
    Mode           mode         = Mode::Menu;
    const Pattern *patterns     = nullptr;
    uint8_t        patternCount = 0;
    const Plan    *plan         = nullptr;
    /// Null outside `Mode::Session`.
    const Session *session      = nullptr;
    uint8_t        probeIndex   = 0;
    uint8_t        probeCount   = 0;
    uint16_t       sweepIndex   = 0;
    uint16_t       sweepCount   = 0;
    Contact        contact      = Contact::Wrist;
    Masking        masking      = Masking::Unmasked;
    /// False when the run directory could not be created: the operator has to
    /// know before the wrist-hour, not after it.
    bool filesOk = true;
    bool capHit  = false;
    EffectDurations durations = EffectDurations::nominal();
};

namespace detail {

inline void copyName(char (&dest)[9], const char *src)
{
    std::memset(dest, 0, sizeof(dest));
    if (src == nullptr) {
        return;
    }
    for (size_t i = 0; i + 1 < sizeof(dest) && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
}

inline uint8_t screenFor(Stage stage)
{
    switch (stage) {
    case Stage::Calibration:   return HAPTIC_LAB_SCREEN_CALIBRATION;
    case Stage::Learn:         return HAPTIC_LAB_SCREEN_LEARN;
    case Stage::TrialWait:     return HAPTIC_LAB_SCREEN_WAIT;
    case Stage::TrialPlaying:  return HAPTIC_LAB_SCREEN_PLAYING;
    case Stage::TrialAnswer:   return HAPTIC_LAB_SCREEN_ANSWER;
    case Stage::TrialFeedback: return HAPTIC_LAB_SCREEN_FEEDBACK;
    case Stage::Rest:          return HAPTIC_LAB_SCREEN_REST;
    case Stage::Done:          return HAPTIC_LAB_SCREEN_DONE;
    case Stage::Aborted:       return HAPTIC_LAB_SCREEN_ABORTED;
    case Stage::Idle:
    case Stage::Count:
        break;
    }
    return HAPTIC_LAB_SCREEN_MENU;
}

inline uint8_t feedbackFor(const Session &session)
{
    if (session.lastWasCorrect()) {
        return HAPTIC_LAB_FEEDBACK_CORRECT;
    }
    if (session.lastWasMiss()) {
        return HAPTIC_LAB_FEEDBACK_MISS;
    }
    if (session.lastResponse() == Response::None) {
        return HAPTIC_LAB_FEEDBACK_TIMEOUT;
    }
    return HAPTIC_LAB_FEEDBACK_WRONG;
}

} // namespace detail

inline void buildFrame(const FrameInputs &in, haptic_lab_gui_frame &out)
{
    std::memset(&out, 0, sizeof(out));
    out.last_button   = HAPTIC_LAB_NO_BUTTON;
    out.probe_count   = in.probeCount;
    out.probe_index   = in.probeIndex;
    out.pattern_count = in.patternCount;
    out.contact       = static_cast<uint8_t>(in.contact);
    out.masking       = static_cast<uint8_t>(in.masking);
    out.trials_total  = in.plan != nullptr ? in.plan->trialCount : 0;

    switch (in.mode) {
    case Mode::Menu:
        out.screen     = HAPTIC_LAB_SCREEN_MENU;
        out.message_id = !in.filesOk  ? HAPTIC_LAB_MSG_NO_FILES
                         : in.capHit  ? HAPTIC_LAB_MSG_STORAGE_FULL
                                      : HAPTIC_LAB_MSG_MENU;
        return;

    case Mode::Probe:
        out.screen = HAPTIC_LAB_SCREEN_PROBE;
        return;

    case Mode::ProbeSent:
        out.screen = HAPTIC_LAB_SCREEN_PROBE_SENT;
        return;

    case Mode::Sweep:
        out.screen      = HAPTIC_LAB_SCREEN_SWEEP;
        out.sweep_index = in.sweepIndex;
        out.sweep_count = in.sweepCount;
        out.message_id  = HAPTIC_LAB_MSG_SWEEP;
        return;

    case Mode::Session:
    case Mode::Count:
        break;
    }

    if (in.session == nullptr || in.plan == nullptr || in.patterns == nullptr) {
        out.screen     = HAPTIC_LAB_SCREEN_MENU;
        out.message_id = HAPTIC_LAB_MSG_NO_FILES;
        return;
    }

    const Session &session = *in.session;
    out.screen      = detail::screenFor(session.stage());
    out.block       = session.block();
    out.block_count = in.plan->blockCount;
    out.block_kind  = static_cast<uint8_t>(session.blockKind());
    out.block_trials = session.blockTrialCount();
    out.learn_count = in.patternCount;

    switch (session.stage()) {
    case Stage::Calibration:
        out.calibration_mask = session.calibrationMask();
        out.last_button      = session.lastCalibrated();
        out.message_id = session.calibrated() ? HAPTIC_LAB_MSG_CALIBRATE_DONE
                                              : HAPTIC_LAB_MSG_CALIBRATE;
        return;

    case Stage::Learn: {
        // The learning screen is allowed to name a pattern and replay it on
        // demand, and should: absolute identification of a learned set is what
        // is being measured.
        const uint8_t index = session.learnIndex();
        out.learn_index     = index;
        if (index < in.patternCount) {
            detail::copyName(out.name, in.patterns[index].name);
            out.modelled_duration_ms = modelledDurationMs(in.patterns[index], in.durations);
        }
        return;
    }

    case Stage::TrialAnswer: {
        // The three, in the triple's own ascending order, which does not depend
        // on which of them was presented. Nothing else about the trial is read
        // here, and there is no field on the frame that could carry it.
        const Triple &triple = session.offered();
        for (uint8_t slot = 0; slot < 3; slot++) {
            const uint8_t pattern = triple.member[slot];
            if (pattern < in.patternCount) {
                detail::copyName(out.alt[slot], in.patterns[pattern].name);
            }
        }
        out.message_id = HAPTIC_LAB_MSG_WHICH_ONE;
        return;
    }

    case Stage::TrialFeedback: {
        // After the response, and in the training block only, so naming the
        // pattern here cannot reach a trial that has not been answered.
        out.feedback            = detail::feedbackFor(session);
        const uint8_t presented = session.lastPresented();
        if (presented < in.patternCount) {
            detail::copyName(out.name, in.patterns[presented].name);
        }
        return;
    }

    case Stage::Rest:
        // The block the wearer is about to start, which is the one the rest
        // screen is counting down to.
        out.block_kind   = static_cast<uint8_t>(session.blockKind());
        out.block_trials = session.blockTrialCount();
        return;

    case Stage::TrialWait:
    case Stage::TrialPlaying:
        // Deliberately nothing: a screen with the alternatives already on it
        // invites an answer to a pattern that has not finished playing.
        return;

    case Stage::Done:
    case Stage::Aborted:
        out.trials_total = in.plan->trialCount;
        return;

    case Stage::Idle:
    case Stage::Count:
        break;
    }
}

} // namespace Haptic

#endif // HAPTICLAB_FRAMEBUILDER_HPP
