//! The frames worth looking at, shared by `preview` and the golden test, so
//! "every screen still draws" is checked against the list a human reviews.
//!
//! The drills named here are Protocol A: forehand and backhand, drive and drop.
//! Drive against drop is the energy contrast the saturation measurements say
//! should separate most cleanly, and both hands of both is four classes with
//! enough reps in one session to mean something.

use crate::{
    Frame, NAME_LEN, SCREEN_BETWEEN, SCREEN_DONE, SCREEN_DRILL, SCREEN_PICK,
};

fn named(s: &str) -> [u8; NAME_LEN] {
    let mut out = [0u8; NAME_LEN];
    for (i, b) in s.bytes().take(NAME_LEN - 1).enumerate() {
        out[i] = b;
    }
    out
}

fn drill(line1: &str, line2: &str, step: u16, detected: u32, target: u32) -> Frame {
    Frame {
        screen: SCREEN_DRILL,
        recording: 1,
        line1: named(line1),
        line2: named(line2),
        protocol: named("DRIVE DROP"),
        step,
        step_count: 4,
        detected,
        target,
        elapsed_s: 95,
        hr_bpm: 128,
        ..Frame::default()
    }
}

pub fn scenes() -> Vec<(&'static str, Frame)> {
    let mut v = vec![
        (
            "pick_drive_drop",
            Frame {
                screen: SCREEN_PICK,
                protocol: named("DRIVE DROP"),
                protocol_count: 3,
                protocol_pick: 1,
                step_count: 4,
                ..Frame::default()
            },
        ),
        (
            "pick_straight_cross",
            Frame {
                screen: SCREEN_PICK,
                protocol: named("STRAIGHT X"),
                protocol_count: 3,
                protocol_pick: 2,
                step_count: 4,
                ..Frame::default()
            },
        ),
    ];

    // The four drills of Protocol A, at the counts they would really reach.
    v.push(("drill_fh_drive", drill("FOREHAND", "DRIVE", 1, 12, 30)));
    v.push(("drill_fh_drop", drill("FOREHAND", "DROP", 2, 7, 20)));
    v.push(("drill_bh_drive", drill("BACKHAND", "DRIVE", 3, 29, 30)));
    // Past the target: the count is not clamped, because the wearer's own count
    // is the truth and a clamped number would hide a detector over-firing.
    v.push(("drill_bh_drop_over", drill("BACKHAND", "DROP", 4, 26, 20)));

    // A rest step is a negative control: whatever the detector counts here is
    // false, and seeing it on the screen is the point.
    v.push((
        "drill_rest",
        Frame { is_rest: 1, ..drill("REST", "", 3, 1, 0) },
    ));

    // A timed drill asks for no number, so none is drawn.
    v.push(("drill_untargeted", drill("BOAST", "", 2, 41, 0)));

    // A name this build has never heard of, from a file the wearer edited.
    v.push(("drill_unknown_name", drill("VOLLEY KILL", "CROSSCOURT", 5, 3, 25)));

    v.push((
        "between",
        Frame {
            screen: SCREEN_BETWEEN,
            detected: 31,
            line1: named("BACKHAND"),
            line2: named("DRIVE"),
            step: 3,
            step_count: 4,
            ..Frame::default()
        },
    ));

    v.push((
        "done",
        Frame {
            screen: SCREEN_DONE,
            protocol: named("DRIVE DROP"),
            rec_s: 1_140,
            rec_kb: 4_900,
            detected: 103,
            ..Frame::default()
        },
    ));

    v.push((
        "drill_not_recording",
        Frame { recording: 0, ..drill("FOREHAND", "DRIVE", 1, 0, 30) },
    ));

    v
}
