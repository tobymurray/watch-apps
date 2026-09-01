//! The frames worth looking at, in one place.
//!
//! Shared by the PNG dump (`preview`), the interactive window (`sim`) and the
//! unit tests, so "every screen still draws" is checked against the same list
//! a human reviews rather than a second list that can drift from it.

use crate::{
    Frame, HR_EXTERNAL, HR_NONE, HR_OPTICAL, SCREEN_CONFIRM_DISCARD, SCREEN_DISCARDED,
    SCREEN_PAUSED, SCREEN_READY, SCREEN_RIDING, SCREEN_SAVED, STRAP_ABSENT,
    STRAP_CONNECTED, STRAP_SEARCHING,
};

fn ready(strap: u8) -> Frame {
    Frame { screen: SCREEN_READY, strap, ..Frame::default() }
}

fn ready_with_target(strap: u8, target_minutes: u16) -> Frame {
    Frame { target_minutes, has_zones: 1, ..ready(strap) }
}

fn riding(screen: u8, elapsed_s: u32, hr_bpm: u16, hr_source: u8) -> Frame {
    Frame {
        screen,
        elapsed_s,
        hr_bpm,
        hr_source,
        strap: if hr_source == HR_EXTERNAL { STRAP_CONNECTED } else { STRAP_ABSENT },
        ..Frame::default()
    }
}

/// The same ride on a watch whose wearer has set their zone thresholds.
fn in_zone(mut f: Frame, zone: u8) -> Frame {
    f.hr_zone = zone;
    f.has_zones = 1;
    f
}

fn saved(elapsed_s: u32, avg_hr_bpm: u16, energy: u16, ok: bool) -> Frame {
    Frame {
        screen: SCREEN_SAVED,
        elapsed_s,
        avg_hr_bpm,
        energy,
        saved_ok: ok as u8,
        ..Frame::default()
    }
}

pub fn scenes() -> Vec<(&'static str, Frame)> {
    vec![
        ("ready_no_strap", ready(STRAP_ABSENT)),
        ("ready_searching", ready(STRAP_SEARCHING)),
        ("ready_strap", ready(STRAP_CONNECTED)),
        // The first minute, when the clock is at its narrowest.
        ("riding_start", riding(SCREEN_RIDING, 7, 0, HR_NONE)),
        ("riding_wrist", riding(SCREEN_RIDING, 754, 118, HR_OPTICAL)),
        ("riding_strap", riding(SCREEN_RIDING, 754, 142, HR_EXTERNAL)),
        // With zones set, every zone the bar can show, plus below zone 1.
        ("zone_below", in_zone(riding(SCREEN_RIDING, 92, 88, HR_EXTERNAL), 0)),
        ("zone_1", in_zone(riding(SCREEN_RIDING, 312, 101, HR_EXTERNAL), 1)),
        ("zone_3", in_zone(riding(SCREEN_RIDING, 1204, 138, HR_EXTERNAL), 3)),
        ("zone_5", in_zone(riding(SCREEN_RIDING, 2610, 179, HR_EXTERNAL), 5)),
        // A strap that dropped out mid-ride: the clock keeps going, the beat
        // does not, and the screen has to say so rather than hold the number.
        ("riding_dropout", riding(SCREEN_RIDING, 1830, 0, HR_NONE)),
        ("riding_three_digit_hr", riding(SCREEN_RIDING, 2400, 187, HR_EXTERNAL)),
        // A target part-way through, so the arc in the bottom gap is partly filled.
        (
            "riding_target_half",
            Frame { target_minutes: 45, hr_zone: 3, has_zones: 1,
                    ..riding(SCREEN_RIDING, 1250, 144, HR_EXTERNAL) },
        ),
        // The hour boundary, where the clock grows a field and drops a size.
        ("riding_one_hour", riding(SCREEN_RIDING, 3725, 133, HR_EXTERNAL)),
        ("riding_long", riding(SCREEN_RIDING, 36_061, 121, HR_EXTERNAL)),
        ("ready_target", ready_with_target(STRAP_CONNECTED, 45)),
        // The target met, and then the same ride paused: one banner slot, and
        // the pause has to win it.
        (
            "riding_target_met",
            Frame { target_minutes: 30, target_reached: 1, hr_zone: 4, has_zones: 1,
                    ..riding(SCREEN_RIDING, 1801, 148, HR_EXTERNAL) },
        ),
        (
            "paused_after_target",
            Frame { target_minutes: 30, target_reached: 1, hr_zone: 2, has_zones: 1,
                    ..riding(SCREEN_PAUSED, 1860, 112, HR_EXTERNAL) },
        ),
        ("paused", riding(SCREEN_PAUSED, 912, 96, HR_EXTERNAL)),
        ("paused_no_hr", riding(SCREEN_PAUSED, 912, 0, HR_NONE)),
        // Holding L1 on the paused screen: the ring fills, and letting go
        // before it is full cancels.
        ("discard_hold_start", Frame { screen: SCREEN_CONFIRM_DISCARD, hold_pct: 0,
                                       ..Frame::default() }),
        ("discard_hold_half", Frame { screen: SCREEN_CONFIRM_DISCARD, hold_pct: 55,
                                      ..Frame::default() }),
        ("discard_hold_full", Frame { screen: SCREEN_CONFIRM_DISCARD, hold_pct: 100,
                                      ..Frame::default() }),
        ("discarded", Frame { screen: SCREEN_DISCARDED, ..Frame::default() }),
        ("saved", saved(2712, 141, 402, true)),
        ("saved_kj", Frame { energy_is_kj: 1, ..saved(2712, 141, 1682, true) }),
        ("saved_no_hr", saved(2712, 0, 61, true)),
        ("saved_long", saved(7325, 128, 1043, true)),
        ("saved_failed", saved(2712, 141, 402, false)),
    ]
}
