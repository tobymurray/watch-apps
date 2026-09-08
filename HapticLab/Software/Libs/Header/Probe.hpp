#ifndef HAPTICLAB_PROBE_HPP
#define HAPTICLAB_PROBE_HPP

#include <cstdint>

#include "Effects.hpp"
#include "Patterns.hpp"

/// Phase 0: the questions about the vibro surface that only the device can
/// answer, as a fixed list rather than a dial. A canned probe is reproducible
/// and writes a labelled row; a dial produces a number nobody can repeat.
/// Each label is the row it writes in Docs/LEDGER.md.
namespace Haptic {

/// One message a probe sends.
struct ProbeStep {
    uint8_t  noteCount;
    /// What to put in the message's own `notesCount`, which the count probe
    /// deliberately sets higher than the array holds.
    uint32_t declaredNotesCount;
    Note     notes[kMaxNotes];
    /// Ticks to wait after the previous step before sending this one; 0 sends
    /// it as soon as the previous send returns, which is what tests queueing.
    uint32_t delayBeforeMs;
};

constexpr uint8_t kMaxProbeSteps = 2;

struct ProbeCase {
    const char *label;    ///< Goes into `probe.csv` and names the ledger row.
    const char *question; ///< One line, shown on the watch.
    uint8_t     stepCount;
    ProbeStep   steps[kMaxProbeSteps];
};

namespace detail {

inline ProbeStep effectStep(uint8_t effect, uint32_t delayBeforeMs = 0)
{
    ProbeStep step {};
    step.noteCount          = 1;
    step.declaredNotesCount = 1;
    step.notes[0].effect    = effect;
    step.delayBeforeMs      = delayBeforeMs;
    return step;
}

/// Three effects separated by a gap long enough to judge them apart.
inline ProbeStep tripletStep(uint8_t a, uint8_t b, uint8_t c)
{
    ProbeStep step {};
    const uint8_t  effects[] = { a, b, c };
    const uint16_t gaps[]    = { 600, 600 };
    Pattern        pattern {};
    fillTrain(pattern, effects, 3, gaps);
    step.noteCount          = pattern.noteCount;
    step.declaredNotesCount = pattern.noteCount;
    for (uint8_t i = 0; i < pattern.noteCount; i++) {
        step.notes[i] = pattern.notes[i];
    }
    return step;
}

/// An effect, a pause, an effect: the pause is what is being timed.
inline ProbeStep pauseStep(uint16_t pauseMs)
{
    ProbeStep step {};
    step.noteCount           = 3;
    step.declaredNotesCount  = 3;
    step.notes[0].effect     = 1;
    step.notes[1].pauseMs    = quantisePause(pauseMs);
    step.notes[2].effect     = 1;
    return step;
}

/// Eight notes of alternating click and short pause, which is the longest
/// message the wire format can carry.
inline ProbeStep fullStep(uint32_t declaredNotesCount, uint32_t delayBeforeMs)
{
    ProbeStep step {};
    step.noteCount = kMaxNotes;
    for (uint8_t i = 0; i < kMaxNotes; i++) {
        if (i % 2 == 0) {
            step.notes[i].effect = 1;
        } else {
            step.notes[i].pauseMs = 200;
        }
    }
    step.declaredNotesCount = declaredNotesCount;
    step.delayBeforeMs      = delayBeforeMs;
    return step;
}

inline ProbeCase oneStep(const char *label, const char *question, const ProbeStep &step)
{
    ProbeCase probe {};
    probe.label     = label;
    probe.question  = question;
    probe.stepCount = 1;
    probe.steps[0]  = step;
    return probe;
}

inline ProbeCase twoStep(const char *label, const char *question, const ProbeStep &first,
                         const ProbeStep &second)
{
    ProbeCase probe = oneStep(label, question, first);
    probe.stepCount = 2;
    probe.steps[1]  = second;
    return probe;
}

} // namespace detail

constexpr uint8_t kProbeCaseCount = 11;

/// The probes, in the order the ledger asks the questions.
inline uint8_t buildProbes(ProbeCase out[kProbeCaseCount])
{
    using namespace detail;
    uint8_t n = 0;
    out[n++] = oneStep("V1-click-1-2-3", "Three clicks. Do 2 and 3 differ from 1?",
                       tripletStep(1, 2, 3));
    out[n++] = oneStep("V1-sharp-4-5-6", "Three sharp clicks. Do 5 and 6 differ from 4?",
                       tripletStep(4, 5, 6));
    out[n++] = oneStep("V1-bump-7-8-9", "Three bumps. Do 8 and 9 differ from 7?",
                       tripletStep(7, 8, 9));
    out[n++] = oneStep("V2-effect-127", "Effect 127. Does anything play?", effectStep(127));
    out[n++] = oneStep("V2-effect-128", "Effect 128. Clamped, ignored or refused?", effectStep(128));
    out[n++] = oneStep("V2-effect-255", "Effect 255. Clamped, ignored or refused?", effectStep(255));
    out[n++] = oneStep("V3-pause-1270", "Two clicks 1270 ms apart. Time the gap.",
                       pauseStep(1270));
    out[n++] = oneStep("V3-pause-10", "Two clicks 10 ms apart. One buzz or two?", pauseStep(10));
    out[n++] = twoStep("V4-queue", "Two full messages, no gap. Sixteen notes or eight?",
                       fullStep(kMaxNotes, 0), fullStep(kMaxNotes, 0));
    out[n++] = twoStep("V4-interrupt", "A full message cut off after 300 ms.",
                       fullStep(kMaxNotes, 0), effectStep(14, 300));
    out[n++] = oneStep("V5-count-12", "notesCount says 12, the array holds 8.",
                       fullStep(12, 0));
    return n;
}

/// The one Phase 0 question the checkout answers on its own: `IVibro::stop()`
/// exists, `IKIP::IntfID` has no vibro identifier, and `MessageTypes.hpp` has
/// no vibro-stop message, so an app cannot stop a pattern early. Recorded here
/// so the probe list and the ledger agree on what was asked.
constexpr char kNoStopClaim[] = "V6-no-stop";

} // namespace Haptic

#endif // HAPTICLAB_PROBE_HPP
