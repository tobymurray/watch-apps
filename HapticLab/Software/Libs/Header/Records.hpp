#ifndef HAPTICLAB_RECORDS_HPP
#define HAPTICLAB_RECORDS_HPP

#include <cstdint>

#include "Patterns.hpp"
#include "Schedule.hpp"

/// What a run leaves behind. A wrist-hour is unrepeatable, so the default here
/// is to record: every field that is cheap to write and conceivably
/// informative is in a row, and the analysis decides later what mattered.
namespace Haptic {

/// Where run directories go. Outside any activity tree, so a research input
/// does not ride along with whatever syncs workouts.
constexpr char kRunsDir[] = "Runs";

/// Bumped whenever a column is added, removed or reinterpreted. It is the first
/// field of every row so a reader written before a change can refuse rather
/// than misparse.
constexpr uint16_t kSchemaVersion = 1;

/// Whether the watch was on the wrist, and whether the ears were masked. The
/// no-contact condition is the control that says how much of the result came
/// through hearing or sight rather than skin.
enum class Contact : uint8_t {
    Wrist = 0,
    Table, ///< On a table, arm off it: any accuracy here is leakage.
    Count,
};

enum class Masking : uint8_t {
    Unmasked = 0,
    Masked, ///< Masking noise over the ears.
    Count,
};

/// Which button the wearer pressed, as the trial screen means it.
enum class Response : uint8_t {
    None = 0,   ///< The response window closed with nothing pressed.
    Slot0,      ///< The first alternative on the screen.
    Slot1,
    Slot2,
    NotFelt,    ///< The miss key: a presentation that never arrived.
    Count,
};

/// Why a button event was not taken as the trial's response, which is what
/// makes the discarded presses worth keeping.
enum class EventDisposition : uint8_t {
    TakenAsResponse = 0,
    DuringPlayback,      ///< Decided before the pattern finished.
    AfterResponse,       ///< A second press once the answer was already in.
    Unmapped,            ///< A button that means nothing on this screen.
    BetweenTrials,
    NotAClick,           ///< PRESS, RELEASE, LONG_PRESS or a HOLD_*.
    Count,
};

/// A `RequestVibroPlay` as it was actually sent, note by note.
struct CommandRecord {
    uint32_t tickBeforeSend;
    uint32_t tickAtAccepted;
    uint32_t wallClock;
    uint16_t trialIndex;    ///< 0xFFFF when the play was not part of a trial.
    uint8_t  patternIndex;  ///< 0xFF for a Phase 0 or Phase 1 play.
    uint8_t  noteCount;
    /// What `notesCount` was set to on the wire, which the probe deliberately
    /// makes differ from `noteCount`.
    uint32_t declaredNotesCount;
    Note     notes[kMaxNotes];
    bool     sendOk;
    bool     resultSuccess;
    /// What the app believes the pattern's end was, which is a model: a
    /// completed `RequestVibroPlay` means accepted, never finished.
    uint32_t modelledEndTick;
};

/// One button event, whether or not the trial logic wanted it.
struct EventRecord {
    uint32_t         buttonTimestamp; ///< `EventButton`'s own clock.
    uint32_t         tickAtHandling;
    uint32_t         wallClock;
    uint16_t         trialIndex;      ///< 0xFFFF outside a trial.
    uint8_t          buttonId;
    uint8_t          buttonEvent;
    EventDisposition disposition;
};

/// One presentation and what came back. Wide on purpose: the redundancy against
/// `patterns.csv` means a row can be read without a join, and the four clocks
/// are all recorded rather than the differences between them so a later
/// question about queue delay or drift is answerable.
struct TrialRecord {
    uint16_t  trialInRun;
    uint16_t  trialInBlock;
    uint8_t   block;
    BlockKind blockKind;

    uint8_t presented;      ///< Index into the candidate set.
    uint8_t offered[3];     ///< In the order the screen drew them.
    uint8_t buttonForSlot[3]; ///< `EventButton::Id` per slot, as drawn.
    uint8_t missButton;

    Response response;
    uint8_t  responseButton; ///< 0xFF when nothing was pressed.
    bool     correct;
    bool     miss;

    uint32_t tickBeforeSend;
    uint32_t tickAtAccepted;
    uint32_t modelledEndTick;
    uint32_t windowOpenDueTick;   ///< When the response window should have opened.
    uint32_t windowOpenDrawnTick; ///< When the frame that says so went out.
    uint32_t responseTimestamp;   ///< `EventButton`'s own clock, 0 for no response.
    uint32_t wallClockAtSend;

    /// From the drawn frame rather than from the arithmetic: the wearer answers
    /// what they can see, and the frame is quantised to the GUI's own tick.
    int32_t reactionMs;

    /// The presented pattern, repeated inline.
    Family    family;
    uint8_t   pulses;
    Rhythm    rhythm;
    Amplitude amplitude;
    Length    length;
    uint8_t   noteCount;
    Note      notes[kMaxNotes];
    uint32_t  modelledDurationMs;
};

/// One Phase 0 probe: what was asked of the vibro surface and what came back.
struct ProbeRecord {
    uint32_t tickBeforeSend;
    uint32_t tickAtAccepted;
    uint32_t wallClock;
    uint8_t  noteCount;
    Note     notes[kMaxNotes];
    /// What `notesCount` was set to, which is not always `noteCount`: probing
    /// what the device does above eight means sending a count the array cannot
    /// hold.
    uint32_t declaredNotesCount;
    bool     sendOk;
    bool     resultSuccess;
    char     label[24];
};

/// Signed difference between two uptime readings, which is what makes every
/// duration in this app survive the 32-bit wrap at about 49.7 days.
inline int32_t sinceTick(uint32_t earlier, uint32_t later)
{
    return static_cast<int32_t>(later - earlier);
}

inline const char *contactName(Contact contact)
{
    switch (contact) {
    case Contact::Wrist: return "wrist";
    case Contact::Table: return "table";
    case Contact::Count: break;
    }
    return "unknown";
}

inline const char *maskingName(Masking masking)
{
    switch (masking) {
    case Masking::Unmasked: return "unmasked";
    case Masking::Masked:   return "masked";
    case Masking::Count:    break;
    }
    return "unknown";
}

inline const char *responseName(Response response)
{
    switch (response) {
    case Response::None:    return "none";
    case Response::Slot0:   return "slot0";
    case Response::Slot1:   return "slot1";
    case Response::Slot2:   return "slot2";
    case Response::NotFelt: return "not-felt";
    case Response::Count:   break;
    }
    return "unknown";
}

inline const char *dispositionName(EventDisposition disposition)
{
    switch (disposition) {
    case EventDisposition::TakenAsResponse: return "response";
    case EventDisposition::DuringPlayback:  return "during-playback";
    case EventDisposition::AfterResponse:   return "after-response";
    case EventDisposition::Unmapped:        return "unmapped";
    case EventDisposition::BetweenTrials:   return "between-trials";
    case EventDisposition::NotAClick:       return "not-a-click";
    case EventDisposition::Count:           break;
    }
    return "unknown";
}

/// Which slot of the offered three, if any, a pattern sits in.
inline int8_t slotOf(const uint8_t offered[3], uint8_t pattern)
{
    for (int8_t i = 0; i < 3; i++) {
        if (offered[i] == pattern) {
            return i;
        }
    }
    return -1;
}

} // namespace Haptic

#endif // HAPTICLAB_RECORDS_HPP
