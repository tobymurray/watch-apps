//! The frames worth looking at, shared by `preview` and the unit tests, so
//! "every screen still draws" is checked against the list a human reviews.
//!
//! The three round-screen tests run over this list, so a screen that is not
//! here is a screen nothing checks. Every screen the app can reach has an
//! entry, and so does the worst case of each: the longest pattern names, the
//! widest counters, and the probe question that needs the most lines.

use crate::{
    Frame, FEEDBACK_CORRECT, FEEDBACK_MISS, FEEDBACK_TIMEOUT, FEEDBACK_WRONG, MSG_CALIBRATE,
    MSG_CALIBRATE_DONE, MSG_MENU, MSG_NO_FILES, MSG_STORAGE_FULL, MSG_SWEEP, PROBE_QUESTIONS,
    SCREEN_ABORTED, SCREEN_ANSWER, SCREEN_CALIBRATION, SCREEN_DONE, SCREEN_FEEDBACK, SCREEN_LEARN,
    SCREEN_MENU, SCREEN_PLAYING, SCREEN_PROBE, SCREEN_PROBE_SENT, SCREEN_REST, SCREEN_SWEEP,
    SCREEN_WAIT,
};

fn name(text: &str) -> [u8; 9] {
    let mut out = [0u8; 9];
    let bytes = text.as_bytes();
    assert!(bytes.len() < out.len(), "{text} is longer than a pattern name can be");
    out[..bytes.len()].copy_from_slice(bytes);
    out
}

fn menu(contact: u8, masking: u8) -> Frame {
    Frame {
        screen: SCREEN_MENU,
        message_id: MSG_MENU,
        // What the twelve-pattern set at the default pass count actually
        // plans, so the menu scene is the arithmetic rather than a placeholder.
        pattern_count: 12,
        trials_total: 297,
        contact,
        masking,
        ..Frame::default()
    }
}

fn calibration(mask: u8, last: u8) -> Frame {
    Frame {
        screen: SCREEN_CALIBRATION,
        message_id: if mask == 0b1111 { MSG_CALIBRATE_DONE } else { MSG_CALIBRATE },
        calibration_mask: mask,
        last_button: last,
        ..Frame::default()
    }
}

fn learn(text: &str, index: u8, count: u8, duration_ms: u32) -> Frame {
    Frame {
        screen: SCREEN_LEARN,
        name: name(text),
        learn_index: index,
        learn_count: count,
        modelled_duration_ms: duration_ms,
        ..Frame::default()
    }
}

fn answer(a: &str, b: &str, c: &str) -> Frame {
    Frame {
        screen: SCREEN_ANSWER,
        alt: [name(a), name(b), name(c)],
        ..Frame::default()
    }
}

fn feedback(verdict: u8, text: &str) -> Frame {
    Frame { screen: SCREEN_FEEDBACK, feedback: verdict, name: name(text), ..Frame::default() }
}

fn rest(block: u8, block_count: u8, trials: u16, kind: u8) -> Frame {
    Frame {
        screen: SCREEN_REST,
        block,
        block_count,
        block_trials: trials,
        block_kind: kind,
        ..Frame::default()
    }
}

fn closed(screen: u8, trials: u16, patterns: u8) -> Frame {
    Frame { screen, trials_total: trials, pattern_count: patterns, ..Frame::default() }
}

fn probe(screen: u8, index: u8) -> Frame {
    Frame {
        screen,
        probe_index: index,
        probe_count: PROBE_QUESTIONS.len() as u8,
        ..Frame::default()
    }
}

fn sweep(index: u16, count: u16) -> Frame {
    Frame {
        screen: SCREEN_SWEEP,
        sweep_index: index,
        sweep_count: count,
        message_id: MSG_SWEEP,
        ..Frame::default()
    }
}

pub fn scenes() -> Vec<(&'static str, Frame)> {
    let mut out = vec![
        ("menu_wrist_unmasked", menu(0, 0)),
        // The control condition, which the operator has to be able to see
        // before the run rather than after it.
        ("menu_table_unmasked", menu(1, 0)),
        ("menu_wrist_masked", menu(0, 1)),
        ("menu_no_files", Frame { message_id: MSG_NO_FILES, ..menu(0, 0) }),
        ("calibration_none", calibration(0b0000, crate::NO_BUTTON)),
        ("calibration_two", calibration(0b0101, 2)),
        ("calibration_all", calibration(0b1111, 1)),
        ("learn_first", learn("TAP", 0, 12, 1000)),
        // The longest name and the widest duration the model can produce.
        ("learn_longest_name", learn("MMMMMMMM", 11, 12, 3400)),
        ("learn_twotick", learn("TWOTICK", 5, 16, 2080)),
        ("wait", Frame { screen: SCREEN_WAIT, ..Frame::default() }),
        ("playing", Frame { screen: SCREEN_PLAYING, ..Frame::default() }),
        ("answer_short", answer("TAP", "TICK", "BUZZ")),
        ("answer_real", answer("DOUBLE", "TWOTICK", "PULSE")),
        // Three names at the cap, which is the widest the response rows get.
        ("answer_longest", answer("MMMMMMMM", "MMMMMMMM", "MMMMMMMM")),
        ("feedback_correct", feedback(FEEDBACK_CORRECT, "TAP")),
        ("feedback_wrong", feedback(FEEDBACK_WRONG, "TWOTICK")),
        ("feedback_miss", feedback(FEEDBACK_MISS, "TAPLOW")),
        ("feedback_timeout", feedback(FEEDBACK_TIMEOUT, "MMMMMMMM")),
        ("rest_first_test", rest(1, 8, 24, 1)),
        ("rest_before_training", rest(0, 8, 24, 0)),
        ("rest_widest", rest(15, 16, 512, 1)),
        ("done", closed(SCREEN_DONE, 297, 12)),
        ("done_widest", closed(SCREEN_DONE, 512, 16)),
        ("aborted", closed(SCREEN_ABORTED, 41, 12)),
        ("sweep_start", sweep(1, 127)),
        ("sweep_end", sweep(127, 127)),
        ("storage_full", Frame { message_id: MSG_STORAGE_FULL, ..menu(0, 0) }),
    ];

    // Every probe, because each carries its own question and the questions are
    // the longest prose in the app.
    for index in 0..PROBE_QUESTIONS.len() as u8 {
        out.push((PROBE_SCENE_NAMES[index as usize], probe(SCREEN_PROBE, index)));
    }
    out.push(("probe_sent", probe(SCREEN_PROBE_SENT, 0)));
    out.push(("probe_sent_last", probe(SCREEN_PROBE_SENT, 10)));

    out
}

/// One name per probe, so a failure says which question broke the layout.
const PROBE_SCENE_NAMES: [&str; PROBE_QUESTIONS.len()] = [
    "probe_00_click_123",
    "probe_01_sharp_456",
    "probe_02_bump_789",
    "probe_03_effect_127",
    "probe_04_effect_128",
    "probe_05_effect_255",
    "probe_06_pause_1270",
    "probe_07_pause_10",
    "probe_08_queue",
    "probe_09_interrupt",
    "probe_10_count_12",
];
