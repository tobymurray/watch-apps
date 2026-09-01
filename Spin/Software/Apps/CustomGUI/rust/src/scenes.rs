//! The frames worth looking at, shared by `preview`, `sim` and the unit tests,
//! so "every screen still draws" is checked against the list a human reviews.

use crate::{
    Frame, HR_EXTERNAL, HR_NONE, HR_OPTICAL, SCREEN_CONFIRM_DISCARD, SCREEN_DISCARDED,
    SCREEN_ENTER_WORK, SCREEN_PAUSED, SCREEN_READY, SCREEN_RIDING, SCREEN_SAVED,
    STRAP_ABSENT, STRAP_CONNECTED, STRAP_SEARCHING,
};

fn ready(strap: u8) -> Frame {
    Frame { screen: SCREEN_READY, strap, ..Frame::default() }
}

fn ready_with_target(strap: u8, target_minutes: u16) -> Frame {
    Frame { target_minutes, has_zones: 1, zone_count: 5, ..ready(strap) }
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

fn in_zone(mut f: Frame, zone: u8) -> Frame {
    f.hr_zone = zone;
    f.zone_count = 5;
    f.has_zones = 1;
    f.hr_zone_fraction = 128; // mid-zone unless a scene says otherwise
    f
}

fn zones(mut f: Frame, count: u8, zone: u8) -> Frame {
    f.zone_count = count;
    f.hr_zone = zone;
    f.has_zones = 1;
    f.hr_zone_fraction = 128;
    f
}

fn at(mut f: Frame, zone: u8, fraction: u8) -> Frame {
    f = in_zone(f, zone);
    f.hr_zone_fraction = fraction;
    f
}

/// `work_estimate_kj` of 0 draws no reference row.
fn enter_work(work_kj: u16, work_estimate_kj: u16) -> Frame {
    Frame { screen: SCREEN_ENTER_WORK, work_kj, work_estimate_kj, ..Frame::default() }
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
        // The clock at its narrowest.
        ("riding_start", riding(SCREEN_RIDING, 7, 0, HR_NONE)),
        ("riding_wrist", riding(SCREEN_RIDING, 754, 118, HR_OPTICAL)),
        ("riding_strap", riding(SCREEN_RIDING, 754, 142, HR_EXTERNAL)),
        // Every zone the dial can show, plus below zone 1.
        ("zone_below", in_zone(riding(SCREEN_RIDING, 92, 88, HR_EXTERNAL), 0)),
        ("zone_1", in_zone(riding(SCREEN_RIDING, 312, 101, HR_EXTERNAL), 1)),
        ("zone_3", in_zone(riding(SCREEN_RIDING, 1204, 138, HR_EXTERNAL), 3)),
        ("zone_5", in_zone(riding(SCREEN_RIDING, 2610, 179, HR_EXTERNAL), 5)),
        // Both ends of a zone and the middle: 93 and 109 bpm both sit in zone
        // 1 and used to look identical.
        ("needle_low_in_zone", at(riding(SCREEN_RIDING, 300, 93, HR_EXTERNAL), 1, 6)),
        ("needle_high_in_zone", at(riding(SCREEN_RIDING, 340, 109, HR_EXTERNAL), 1, 249)),
        ("needle_zone_4_mid", at(riding(SCREEN_RIDING, 1500, 157, HR_EXTERNAL), 4, 128)),
        // Every dial size the app offers, so each ladder can be looked at.
        ("zones_3_of_3", zones(riding(SCREEN_RIDING, 900, 168, HR_EXTERNAL), 3, 3)),
        ("zones_4_of_7", zones(riding(SCREEN_RIDING, 900, 141, HR_EXTERNAL), 7, 4)),
        ("zones_6_of_8", zones(riding(SCREEN_RIDING, 900, 155, HR_EXTERNAL), 8, 6)),
        ("zones_2_of_2", zones(riding(SCREEN_RIDING, 900, 120, HR_EXTERNAL), 2, 2)),
        // A strap that dropped out mid-ride.
        ("riding_dropout", riding(SCREEN_RIDING, 1830, 0, HR_NONE)),
        ("riding_three_digit_hr", riding(SCREEN_RIDING, 2400, 187, HR_EXTERNAL)),
        // The arc in the bottom gap, partly filled.
        (
            "riding_target_half",
            Frame { target_minutes: 45, hr_zone: 3, has_zones: 1,
                    ..riding(SCREEN_RIDING, 1250, 144, HR_EXTERNAL) },
        ),
        // Where the clock grows a field and drops a size.
        ("riding_one_hour", riding(SCREEN_RIDING, 3725, 133, HR_EXTERNAL)),
        ("riding_long", riding(SCREEN_RIDING, 36_061, 121, HR_EXTERNAL)),
        ("ready_target", ready_with_target(STRAP_CONNECTED, 45)),
        // One banner slot, and the pause has to win it.
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
        ("discard_confirm", Frame { screen: SCREEN_CONFIRM_DISCARD, ..Frame::default() }),
        ("discarded", Frame { screen: SCREEN_DISCARDED, ..Frame::default() }),
        ("enter_work_zero", enter_work(0, 390)),
        // No heart rate means no calorie estimate, so no reference row.
        ("enter_work_no_estimate", enter_work(0, 0)),
        ("enter_work_typical", enter_work(430, 390)),
        // Both places at their limit: the widest number the screen can hold.
        ("enter_work_max", enter_work(1990, 1720)),
        ("saved", saved(2712, 141, 402, true)),
        ("saved_kj", Frame { energy_is_kj: 1, ..saved(2712, 141, 1682, true) }),
        ("saved_no_hr", saved(2712, 0, 61, true)),
        ("saved_long", saved(7325, 128, 1043, true)),
        ("saved_failed", saved(2712, 141, 402, false)),
    ]
}
