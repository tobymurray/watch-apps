#!/usr/bin/env python3
"""Write the synthetic run the report round trip reads.

    python3 HapticLab/Tests/fixtures/make_fixture.py HapticLab/Tests/fixtures

The output is committed beside this script, because the round trip is a
contract about column names and the tool's own arithmetic: a fixture generated
at test time from the same assumptions the tool makes would agree with it by
construction. Regenerate and commit when a column is added, and bump the schema
in Records.hpp in the same commit.

WHAT IT EXERCISES, and why each one is here rather than a happy path:

  * a clean triple with enough trials that a zero-confusion pair earns its
    place in the distinguishable set (44 pair trials puts the Wilson upper
    bound at about 8 %, under the 10 % threshold);
  * a second triple sampled too thinly to report, so the "dropped for want of
    trials" path is covered rather than assumed;
  * a miss, which is a presentation nobody felt and must not count as evidence
    that any pair is distinguishable;
  * a response window that closed with nothing pressed, which is a different
    thing from a miss;
  * button events the trial logic ignored, including one during playback.
"""
import os
import sys

SCHEMA = 1
RUN_ID = "20260908T101500"
SEED = 4242

PATTERNS = [
    # id, name, family, pulses, rhythm, amplitude, length, notes
    ("P01", "TAP", "click", 1, "single", "full", "brief", "e1"),
    ("P02", "TICK", "tick", 1, "single", "full", "brief", "e24"),
    ("P03", "BUZZ", "buzz", 1, "single", "full", "brief", "e14"),
    ("P04", "TWOTICK", "tick", 2, "even", "full", "brief", "e24;p80;e24"),
]

TRIALS_HEADER = (
    "schema,run_id,seed,contact,masking,set,set_hash,"
    "block,block_kind,trial_in_run,trial_in_block,"
    "presented_id,offered_0,offered_1,offered_2,"
    "button_slot_0,button_slot_1,button_slot_2,miss_button,"
    "response,response_button,correct,miss,"
    "tick_before_send,tick_accepted,modelled_end_tick,window_due_tick,window_drawn_tick,"
    "response_timestamp,wall_clock_at_send,reaction_ms,"
    "family,pulses,rhythm,amplitude,length,modelled_duration_ms,notes"
)

EVENTS_HEADER = (
    "schema,run_id,seed,contact,masking,"
    "button_timestamp,tick_at_handling,wall_clock,trial_in_run,button_id,button_event,disposition"
)

PATTERNS_HEADER = (
    "schema,run_id,set,set_hash,index,pattern_id,name,"
    "family,pulses,rhythm,amplitude,length,note_count,modelled_duration_ms,needs_amplitude_axis,notes"
)

SET_HASH = "1A2B3C4D"
CONTACT = "wrist"
MASKING = "unmasked"
SET = "rhythm-and-family"

# The clean triple, and the one sampled too thinly to report on.
CLEAN = ("P01", "P02", "P03")
THIN = ("P01", "P02", "P04")

BY_ID = {p[0]: p for p in PATTERNS}


def trial_row(index, block, block_kind, offered, presented, response, correct, miss, reaction):
    pattern = BY_ID[presented]
    tick = 100000 + index * 8000
    return ",".join(
        str(field)
        for field in (
            SCHEMA, RUN_ID, SEED, CONTACT, MASKING, SET, SET_HASH,
            block, block_kind, index, index,
            presented, offered[0], offered[1], offered[2],
            0, 1, 2, 3,
            response,
            {"none": -1, "not-felt": 3, "slot0": 0, "slot1": 1, "slot2": 2}[response],
            1 if correct else 0, 1 if miss else 0,
            tick, tick + 3, tick + 3400, tick + 3800, tick + 3840,
            0 if response == "none" else tick + 3840 + reaction,
            1789215300 + index * 8, reaction,
            pattern[2], pattern[3], pattern[4], pattern[5], pattern[6], 3400, pattern[7],
        )
    )


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    run_dir = os.path.join(out_dir, RUN_ID)
    os.makedirs(run_dir, exist_ok=True)

    trials = [TRIALS_HEADER]
    events = [EVENTS_HEADER]
    index = 0

    def add(block, kind, offered, presented, response, correct, miss, reaction):
        nonlocal index
        trials.append(
            trial_row(index, block, kind, offered, presented, response, correct, miss, reaction)
        )
        index += 1

    # Twelve training presentations, every one of them right.
    for repeat in range(4):
        for slot, presented in enumerate(CLEAN):
            add(0, "training", CLEAN, presented, f"slot{slot}", True, False, 700 + repeat * 20)

    # Twenty-four more of the clean triple per member, all right, which is what
    # puts 48 test trials behind each of its three pairs: enough that a
    # zero-confusion pair's Wilson upper bound clears the 10 % threshold with
    # room to spare rather than by a rounding error.
    for repeat in range(24):
        for slot, presented in enumerate(CLEAN):
            add(1, "test", CLEAN, presented, f"slot{slot}", True, False, 640 + repeat * 5)

    # The thin triple: three presentations a member, two of them wrong.
    add(1, "test", THIN, "P01", "slot0", True, False, 690)
    add(1, "test", THIN, "P01", "slot0", True, False, 705)
    add(1, "test", THIN, "P01", "slot0", True, False, 715)
    add(1, "test", THIN, "P02", "slot1", True, False, 720)
    add(1, "test", THIN, "P02", "slot1", True, False, 728)
    add(1, "test", THIN, "P02", "slot1", True, False, 735)
    # The two wrong answers are both P04 taken for something else, so the pair
    # the clean triple is meant to prove stays clean and the pair that is meant
    # to be undersampled is the one carrying the confusions.
    add(1, "test", THIN, "P04", "slot2", True, False, 810)
    add(1, "test", THIN, "P04", "slot0", False, False, 980)
    add(1, "test", THIN, "P04", "slot1", False, False, 990)

    # A presentation nobody felt, and a window that closed with nothing pressed.
    add(1, "test", CLEAN, "P03", "not-felt", False, True, 1500)
    add(1, "test", CLEAN, "P03", "not-felt", False, True, 1620)
    add(1, "test", CLEAN, "P01", "none", False, False, 0)

    # The event log, including the presses the trial logic ignored.
    def event(trial, button, code, disposition, timestamp):
        events.append(
            ",".join(
                str(field)
                for field in (SCHEMA, RUN_ID, SEED, CONTACT, MASKING, timestamp, timestamp + 1,
                              1789215300, trial, button, code, disposition)
            )
        )

    for i in range(index):
        event(i, i % 3, 2, "response", 200000 + i * 8000)
    event(7, 0, 2, "during-playback", 260000)
    event(9, 3, 2, "between-trials", 280000)
    event(11, 1, 0, "not-a-click", 300000)

    patterns = [PATTERNS_HEADER]
    for i, pattern in enumerate(PATTERNS):
        patterns.append(
            ",".join(
                str(field)
                for field in (SCHEMA, RUN_ID, SET, SET_HASH, i, pattern[0], pattern[1],
                              pattern[2], pattern[3], pattern[4], pattern[5], pattern[6],
                              pattern[7].count(";") + 1, 3400, 0, pattern[7])
            )
        )

    session = f"""{{
  "schema": {SCHEMA},
  "run_id": "{RUN_ID}",
  "app_version": "0.1.0",
  "firmware": "1.4.0",
  "kernel_interface_version": 3,
  "seed": {SEED},
  "candidate_set": "{SET}",
  "candidate_set_hash": "{SET_HASH}",
  "pattern_count": {len(PATTERNS)},
  "triple_count": 2,
  "trial_count": {index},
  "block_count": 2,
  "pair_trials_lowest": 6,
  "pair_trials_highest": 54,
  "contact": "{CONTACT}",
  "masking": "{MASKING}",
  "wrist": "left",
  "watch_position": "one finger above the ulnar head",
  "strap_tightness": "snug",
  "arm_posture": "arm down at the side",
  "ambient_temp_c": 21,
  "presentation_window_ms": 3800,
  "inter_trial_ms": 3000,
  "feedback_ms": 1500,
  "response_timeout_ms": 8000,
  "effect_durations_measured": false,
  "byte_cap_per_file": 1048576,
  "duration_cap_ms": 14400000,
  "start_tick": 100000,
  "start_wall_clock": 1789215300,
  "end_tick": 900000,
  "end_wall_clock": 1789216200,
  "end_reason": "completed",
  "caps_hit": "none"
}}
"""

    for leaf, lines in (("trials.csv", trials), ("events.csv", events),
                        ("patterns.csv", patterns)):
        with open(os.path.join(run_dir, leaf), "w") as handle:
            handle.write("\n".join(lines) + "\n")
    with open(os.path.join(run_dir, "session.json"), "w") as handle:
        handle.write(session)

    print(f"wrote {run_dir} with {index} trials")


if __name__ == "__main__":
    main()
