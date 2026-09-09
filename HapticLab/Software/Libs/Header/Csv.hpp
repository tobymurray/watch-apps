#ifndef HAPTICLAB_CSV_HPP
#define HAPTICLAB_CSV_HPP

#include <cstdint>
#include <cstdio>

#include "Patterns.hpp"
#include "Protocol.hpp"
#include "Records.hpp"
#include "Schedule.hpp"

/// The rows a run leaves behind, formatted here so the shape is testable
/// without a filesystem. CSV rather than the SDK's JSON writer, because
/// `JsonStreamWriter::add(int32_t)` writes a negative as its unsigned
/// reinterpretation on an LP64 host and `add(int64_t)` formats through `%g`;
/// ARM is unaffected, which is exactly why those survive. No field here is
/// fractional either, so nothing depends on the watch's newlib having
/// floating-point `printf` linked.
namespace Haptic {

/// The widest row any formatter here writes, with room for the note list.
constexpr size_t kMaxRowLength = 512;

/// The buffer `formatSessionJson` is given. Named here rather than at the call
/// site so `TheHeaderStillFitsWithEveryOperatorNoteAtItsCap` constrains the
/// size the Recorder really uses: the first hardware run left a 0-byte header
/// from a 1024-byte buffer while a test passed against a 2048-byte one.
constexpr size_t kSessionJsonBytes = 2048;

/// The run-level fields every row repeats, so a row can be read without a
/// join back to `session.json`.
struct RunHeader {
    char     runId[20];
    uint32_t seed;
    Contact  contact;
    Masking  masking;
    SetId    set;
    uint32_t setHash;
};

constexpr char kTrialsHeader[] =
    "schema,run_id,seed,contact,masking,set,set_hash,"
    "block,block_kind,trial_in_run,trial_in_block,"
    "presented_id,offered_0,offered_1,offered_2,"
    "button_slot_0,button_slot_1,button_slot_2,miss_button,"
    "response,response_button,correct,miss,"
    "tick_before_send,tick_accepted,modelled_end_tick,window_due_tick,window_drawn_tick,"
    "response_timestamp,wall_clock_at_send,reaction_ms,"
    "family,pulses,rhythm,amplitude,length,modelled_duration_ms,notes";

constexpr char kEventsHeader[] =
    "schema,run_id,seed,contact,masking,"
    "button_timestamp,tick_at_handling,wall_clock,trial_in_run,button_id,button_event,disposition";

constexpr char kCommandsHeader[] =
    "schema,run_id,seed,contact,masking,"
    "tick_before_send,tick_accepted,wall_clock,trial_in_run,pattern_id,"
    "declared_notes_count,send_ok,result_success,modelled_end_tick,notes";

constexpr char kPatternsHeader[] =
    "schema,run_id,set,set_hash,index,pattern_id,name,"
    "family,pulses,rhythm,amplitude,length,note_count,modelled_duration_ms,needs_amplitude_axis,notes";

constexpr char kProbeHeader[] =
    "schema,run_id,tick_before_send,tick_accepted,wall_clock,"
    "declared_notes_count,send_ok,result_success,label,notes";

/// `onset_us` and `offset_us` rather than the `*_tick_us` of schema 1: both
/// are relative to the play, not absolute ticks, and the old names said
/// otherwise. `baseline_delta` and `threshold` are what the detection actually
/// used, so a contaminated baseline is visible in the row rather than only in a
/// re-analysis.
constexpr char kEffectsHeader[] =
    "schema,run_id,effect,name,family,repeat,"
    "play_tick_us,accepted_tick_us,send_ok,result_success,"
    "baseline_delta,had_baseline,threshold,"
    "onset_us,offset_us,duration_us,energy_proxy,peak_delta,samples,"
    "window_samples,overflowed,declared_duration_ms";

/// A note list as one field: effects as `e<index>` and pauses as `p<ms>`,
/// separated by semicolons so it never needs quoting.
inline int formatNotes(char *out, size_t cap, const Note notes[], uint8_t count)
{
    int written = 0;
    for (uint8_t i = 0; i < count && written >= 0 && static_cast<size_t>(written) < cap; i++) {
        const int n = notes[i].effect != 0
                          ? std::snprintf(out + written, cap - static_cast<size_t>(written),
                                          "%se%u", i == 0 ? "" : ";", notes[i].effect)
                          : std::snprintf(out + written, cap - static_cast<size_t>(written),
                                          "%sp%u", i == 0 ? "" : ";", notes[i].pauseMs);
        if (n < 0) {
            return n;
        }
        written += n;
    }
    if (count == 0 && cap > 0) {
        out[0] = '\0';
    }
    return written;
}

inline int formatTrialRow(char *out, size_t cap, const RunHeader &run, const TrialRecord &row,
                          const Pattern patterns[])
{
    char notes[96] = {};
    formatNotes(notes, sizeof(notes), row.notes, row.noteCount);

    return std::snprintf(
        out, cap,
        "%u,%s,%lu,%s,%s,%s,%08lX,"
        "%u,%s,%u,%u,"
        "%s,%s,%s,%s,"
        "%u,%u,%u,%u,"
        "%s,%d,%u,%u,"
        "%lu,%lu,%lu,%lu,%lu,"
        "%lu,%lu,%ld,"
        "%s,%u,%s,%s,%s,%lu,%s\n",
        kSchemaVersion, run.runId, static_cast<unsigned long>(run.seed), contactName(run.contact),
        maskingName(run.masking), setName(run.set), static_cast<unsigned long>(run.setHash),
        row.block, blockKindName(row.blockKind), row.trialInRun, row.trialInBlock,
        patterns[row.presented].id, patterns[row.offered[0]].id, patterns[row.offered[1]].id,
        patterns[row.offered[2]].id,
        row.buttonForSlot[0], row.buttonForSlot[1], row.buttonForSlot[2], row.missButton,
        responseName(row.response),
        row.responseButton == kNoButton ? -1 : static_cast<int>(row.responseButton),
        row.correct ? 1u : 0u, row.miss ? 1u : 0u,
        static_cast<unsigned long>(row.tickBeforeSend), static_cast<unsigned long>(row.tickAtAccepted),
        static_cast<unsigned long>(row.modelledEndTick),
        static_cast<unsigned long>(row.windowOpenDueTick),
        static_cast<unsigned long>(row.windowOpenDrawnTick),
        static_cast<unsigned long>(row.responseTimestamp),
        static_cast<unsigned long>(row.wallClockAtSend), static_cast<long>(row.reactionMs),
        familyName(row.family), row.pulses, rhythmName(row.rhythm), amplitudeName(row.amplitude),
        lengthName(row.length), static_cast<unsigned long>(row.modelledDurationMs), notes);
}

inline int formatEventRow(char *out, size_t cap, const RunHeader &run, const EventRecord &row)
{
    return std::snprintf(
        out, cap, "%u,%s,%lu,%s,%s,%lu,%lu,%lu,%d,%u,%u,%s\n", kSchemaVersion, run.runId,
        static_cast<unsigned long>(run.seed), contactName(run.contact), maskingName(run.masking),
        static_cast<unsigned long>(row.buttonTimestamp),
        static_cast<unsigned long>(row.tickAtHandling), static_cast<unsigned long>(row.wallClock),
        row.trialIndex == kNoTrial ? -1 : static_cast<int>(row.trialIndex), row.buttonId,
        row.buttonEvent, dispositionName(row.disposition));
}

inline int formatCommandRow(char *out, size_t cap, const RunHeader &run, const CommandRecord &row,
                            const Pattern patterns[], uint8_t patternCount)
{
    char notes[96] = {};
    formatNotes(notes, sizeof(notes), row.notes, row.noteCount);

    const char *patternId = row.patternIndex < patternCount ? patterns[row.patternIndex].id : "-";

    return std::snprintf(
        out, cap, "%u,%s,%lu,%s,%s,%lu,%lu,%lu,%d,%s,%lu,%u,%u,%lu,%s\n", kSchemaVersion, run.runId,
        static_cast<unsigned long>(run.seed), contactName(run.contact), maskingName(run.masking),
        static_cast<unsigned long>(row.tickBeforeSend), static_cast<unsigned long>(row.tickAtAccepted),
        static_cast<unsigned long>(row.wallClock),
        row.trialIndex == kNoTrial ? -1 : static_cast<int>(row.trialIndex), patternId,
        static_cast<unsigned long>(row.declaredNotesCount), row.sendOk ? 1u : 0u,
        row.resultSuccess ? 1u : 0u, static_cast<unsigned long>(row.modelledEndTick), notes);
}

inline int formatPatternRow(char *out, size_t cap, const RunHeader &run, uint8_t index,
                            const Pattern &pattern, const EffectDurations &durations)
{
    char notes[96] = {};
    formatNotes(notes, sizeof(notes), pattern.notes, pattern.noteCount);

    return std::snprintf(
        out, cap, "%u,%s,%s,%08lX,%u,%s,%s,%s,%u,%s,%s,%s,%u,%lu,%u,%s\n", kSchemaVersion, run.runId,
        setName(run.set), static_cast<unsigned long>(run.setHash), index, pattern.id, pattern.name,
        familyName(pattern.family), pattern.pulses, rhythmName(pattern.rhythm),
        amplitudeName(pattern.amplitude), lengthName(pattern.length), pattern.noteCount,
        static_cast<unsigned long>(modelledDurationMs(pattern, durations)),
        needsAmplitudeAxis(pattern) ? 1u : 0u, notes);
}

inline int formatProbeRow(char *out, size_t cap, const RunHeader &run, const ProbeRecord &row)
{
    char notes[96] = {};
    formatNotes(notes, sizeof(notes), row.notes, row.noteCount);

    return std::snprintf(out, cap, "%u,%s,%lu,%lu,%lu,%lu,%u,%u,%s,%s\n", kSchemaVersion, run.runId,
                         static_cast<unsigned long>(row.tickBeforeSend),
                         static_cast<unsigned long>(row.tickAtAccepted),
                         static_cast<unsigned long>(row.wallClock),
                         static_cast<unsigned long>(row.declaredNotesCount), row.sendOk ? 1u : 0u,
                         row.resultSuccess ? 1u : 0u, row.label, notes);
}

/// Everything about a run that is not a row: the operator's notes about the
/// wrist, the derived timings, and how the run ended. Held so `session.json`
/// can be rewritten whole at the close rather than appended to with a second
/// object nothing would parse.
struct SessionMeta {
    const char *appVersion   = "";
    const char *firmware     = "";
    uint32_t    kernelAbi    = 0;
    const char *wrist        = "unrecorded";
    const char *watchPosition = "unrecorded";
    const char *strapTightness = "unrecorded";
    const char *armPosture   = "unrecorded";
    int16_t     ambientTempC = -32768; ///< Sentinel: nobody said.
    uint32_t    startTick    = 0;
    uint32_t    startWall    = 0;
    uint32_t    endTick      = 0;
    uint32_t    endWall      = 0;
    /// "running" until the run closes, then "completed", "abandoned",
    /// "stopped-by-kernel" or "cap-reached".
    const char *endReason = "running";
    const char *capsHit   = "none";

    /// What a sweep was asked for. Zero when the run is not a sweep.
    uint8_t sweepRepeats     = 0;
    uint8_t sweepFirstEffect = 0;
    uint8_t sweepLastEffect  = 0;
};

/// The run header, as JSON because it is read once by a tool rather than
/// appended to, and rewritten whole when the run closes. Written by hand rather
/// than through the SDK's writer for the reason at the top of this file.
inline int formatSessionJson(char *out, size_t cap, const RunHeader &run, const Plan &plan,
                             const Timing &timing, const EffectDurations &durations,
                             const SessionMeta &meta, uint32_t byteCapPerFile,
                             uint32_t durationCapMs)
{
    uint16_t lowestPair = 0, highestPair = 0;
    pairBalance(plan, lowestPair, highestPair);

    return std::snprintf(
        out, cap,
        "{\n"
        "  \"schema\": %u,\n"
        "  \"run_id\": \"%s\",\n"
        "  \"app_version\": \"%s\",\n"
        "  \"firmware\": \"%s\",\n"
        "  \"kernel_interface_version\": %lu,\n"
        "  \"seed\": %lu,\n"
        "  \"candidate_set\": \"%s\",\n"
        "  \"candidate_set_hash\": \"%08lX\",\n"
        "  \"pattern_count\": %u,\n"
        "  \"triple_count\": %u,\n"
        "  \"trial_count\": %u,\n"
        "  \"block_count\": %u,\n"
        "  \"pair_trials_lowest\": %u,\n"
        "  \"pair_trials_highest\": %u,\n"
        "  \"contact\": \"%s\",\n"
        "  \"masking\": \"%s\",\n"
        "  \"wrist\": \"%s\",\n"
        "  \"watch_position\": \"%s\",\n"
        "  \"strap_tightness\": \"%s\",\n"
        "  \"arm_posture\": \"%s\",\n"
        "  \"ambient_temp_c\": %d,\n"
        "  \"presentation_window_ms\": %lu,\n"
        "  \"inter_trial_ms\": %lu,\n"
        "  \"feedback_ms\": %lu,\n"
        "  \"response_timeout_ms\": %lu,\n"
        "  \"effect_durations_measured\": %s,\n"
        "  \"sweep_repeats\": %u,\n"
        "  \"sweep_first_effect\": %u,\n"
        "  \"sweep_last_effect\": %u,\n"
        "  \"byte_cap_per_file\": %lu,\n"
        "  \"duration_cap_ms\": %lu,\n"
        "  \"start_tick\": %lu,\n"
        "  \"start_wall_clock\": %lu,\n"
        "  \"end_tick\": %lu,\n"
        "  \"end_wall_clock\": %lu,\n"
        "  \"end_reason\": \"%s\",\n"
        "  \"caps_hit\": \"%s\"\n"
        "}\n",
        kSchemaVersion, run.runId, meta.appVersion, meta.firmware,
        static_cast<unsigned long>(meta.kernelAbi), static_cast<unsigned long>(run.seed),
        setName(run.set), static_cast<unsigned long>(run.setHash), plan.patternCount,
        plan.tripleCount, plan.trialCount, plan.blockCount, lowestPair, highestPair,
        contactName(run.contact), maskingName(run.masking), meta.wrist, meta.watchPosition,
        meta.strapTightness, meta.armPosture, meta.ambientTempC,
        static_cast<unsigned long>(timing.presentationMs),
        static_cast<unsigned long>(timing.interTrialMs),
        static_cast<unsigned long>(timing.feedbackMs),
        static_cast<unsigned long>(timing.responseTimeoutMs), durations.measured ? "true" : "false",
        meta.sweepRepeats, meta.sweepFirstEffect, meta.sweepLastEffect,
        static_cast<unsigned long>(byteCapPerFile), static_cast<unsigned long>(durationCapMs),
        static_cast<unsigned long>(meta.startTick), static_cast<unsigned long>(meta.startWall),
        static_cast<unsigned long>(meta.endTick), static_cast<unsigned long>(meta.endWall),
        meta.endReason, meta.capsHit);
}

} // namespace Haptic

#endif // HAPTICLAB_CSV_HPP
