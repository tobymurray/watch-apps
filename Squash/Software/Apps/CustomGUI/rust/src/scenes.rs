//! The frames worth looking at, shared by `preview` and the golden test, so
//! "every screen still draws" is checked against the list a human reviews.
//!
//! The numbers are not invented: the signal values are quartiles of the 1,800
//! epochs in `Squash/Tests/pulled/20260913-v0.6.0-70min-match`, so a scene
//! showing a bar near its top is showing what a real stroke did.

use crate::{
    Frame, HR_EXTERNAL, HR_NONE, HR_OPTICAL, LABEL_DRILL, LABEL_IDLE, LABEL_NONE,
    LABEL_OFF_COURT, LABEL_RALLY, LABEL_REST, LABEL_WARMUP, REC_DURATION_LIMIT, REC_NONE, REC_SINK_ERROR,
    SCREEN_DISCARDED, SCREEN_LABEL, SCREEN_PAUSED, SCREEN_PROFILE, SCREEN_READY, SCREEN_SAVED,
};

/// 90 minutes and 32 MiB, the defaults the manifest declares.
const CAP_S: u32 = 90 * 60;
const CAP_KB: u32 = 32 * 1024;

fn ready(armed: u8) -> Frame {
    Frame {
        screen: SCREEN_READY,
        armed,
        rec_cap_s: CAP_S,
        rec_cap_kb: CAP_KB,
        hr_bpm: 71,
        hr_trust: 3,
        hr_source: HR_OPTICAL,
        ..Frame::default()
    }
}

fn profiling(label: u8, rec_s: u32, gyro_mag: u32, accel_var_k: u32) -> Frame {
    Frame {
        screen: SCREEN_PROFILE,
        recording: 1,
        label,
        label_s: 14,
        elapsed_s: rec_s,
        rec_s,
        rec_cap_s: CAP_S,
        rec_kb: rec_s * 43 / 10,
        rec_cap_kb: CAP_KB,
        gyro_mag,
        accel_var_k,
        markers: 12,
        hr_bpm: 148,
        hr_trust: 3,
        hr_source: HR_OPTICAL,
        ..Frame::default()
    }
}

pub fn scenes() -> Vec<(&'static str, Frame)> {
    let mut v = vec![
        ("ready_armed", ready(1)),
        ("ready_not_armed", Frame { hr_bpm: 0, hr_source: HR_NONE, ..ready(0) }),
    ];

    // The quartiles of the match recording: p25, median, p75, p95.
    v.push(("profile_rest_quiet", profiling(LABEL_REST, 240, 1_385, 431)));
    v.push(("profile_rally_median", profiling(LABEL_RALLY, 903, 2_398, 2_001)));
    v.push(("profile_rally_hard", profiling(LABEL_RALLY, 1_500, 8_452, 157_907)));

    // Saturation is signal on this hardware, and 13.8% of the match's epochs
    // had some, so a scene has to carry it.
    v.push((
        "profile_saturating",
        Frame { sat_accel_pct: 22, sat_gyro_pct: 6, ..profiling(LABEL_RALLY, 1_802, 12_183, 249_351) },
    ));

    v.push((
        "profile_strap",
        Frame { hr_source: HR_EXTERNAL, hr_bpm: 160, ..profiling(LABEL_DRILL, 1_200, 5_571, 7_874) },
    ));
    v.push((
        "profile_hr_untrusted",
        Frame { hr_trust: 0, ..profiling(LABEL_RALLY, 600, 3_657, 5_000) },
    ));
    v.push((
        "profile_no_hr",
        Frame { hr_bpm: 0, hr_source: HR_NONE, ..profiling(LABEL_WARMUP, 90, 900, 300) },
    ));
    v.push(("profile_no_label", profiling(LABEL_NONE, 30, 400, 40)));
    v.push(("profile_off_court", profiling(LABEL_OFF_COURT, 2_400, 148, 6)));
    v.push(("profile_idle", profiling(LABEL_IDLE, 60, 55, 2)));

    // The case that cost 40 minutes of the 2026-09-13 match: the cap tripped,
    // the session ran on, and nothing said so.
    v.push((
        "profile_near_cap",
        Frame { rec_s: CAP_S - 200, ..profiling(LABEL_RALLY, CAP_S - 200, 4_000, 9_000) },
    ));
    v.push((
        "profile_cap_reached",
        Frame {
            recording: 0,
            rec_stop: REC_DURATION_LIMIT,
            rec_s: CAP_S,
            elapsed_s: CAP_S + 1_800,
            ..profiling(LABEL_RALLY, CAP_S, 4_000, 9_000)
        },
    ));
    v.push((
        "profile_write_failed",
        Frame { recording: 0, rec_stop: REC_SINK_ERROR, ..profiling(LABEL_RALLY, 400, 0, 0) },
    ));

    // An hour and a half is the cap, so the clock has to survive three digits
    // of minutes and an hours field.
    v.push(("profile_long", profiling(LABEL_RALLY, 4_233, 2_398, 2_001)));

    v.push((
        "paused",
        Frame { screen: SCREEN_PAUSED, recording: 0, ..profiling(LABEL_REST, 1_100, 300, 20) },
    ));

    for (name, pick) in [
        ("label_rally", LABEL_RALLY),
        ("label_off_court", LABEL_OFF_COURT),
        ("label_idle", LABEL_IDLE),
    ] {
        v.push((
            name,
            Frame {
                screen: SCREEN_LABEL,
                label: LABEL_RALLY,
                label_pick: pick,
                ..Frame::default()
            },
        ));
    }

    v.push((
        "saved",
        Frame {
            screen: SCREEN_SAVED,
            saved_ok: 1,
            rec_s: 4_233,
            rec_kb: 18_200,
            markers: 87,
            rec_stop: REC_NONE,
            ..Frame::default()
        },
    ));
    v.push((
        "saved_capped",
        Frame {
            screen: SCREEN_SAVED,
            saved_ok: 1,
            rec_s: CAP_S,
            rec_kb: CAP_KB,
            markers: 87,
            rec_stop: REC_DURATION_LIMIT,
            ..Frame::default()
        },
    ));
    v.push((
        "saved_failed",
        Frame { screen: SCREEN_SAVED, saved_ok: 0, rec_s: 300, rec_kb: 1_290, markers: 4,
                rec_stop: REC_SINK_ERROR, ..Frame::default() },
    ));
    v.push((
        "discarded",
        Frame { screen: SCREEN_DISCARDED, rec_s: 120, rec_kb: 516, ..Frame::default() },
    ));

    v
}
