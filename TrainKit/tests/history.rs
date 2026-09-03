//! The shared log: what it writes, what it reads back, and what it does at its
//! bounds.

use trainkit::history::*;
use trainkit::load::*;
use trainkit::record::*;

fn watch_ladder(max_hr: u8) -> [u8; MAX_ZONES] {
    // The watch's own ladder, which is also Edwards': 50/60/70/80/90% of
    // maximum. See ZoneLadder.hpp.
    let mut f = [0u8; MAX_ZONES];
    for (i, slot) in f.iter_mut().take(5).enumerate() {
        *slot = (((max_hr as u32) * (50 + 10 * i as u32) + 50) / 100) as u8;
    }
    f
}

fn a_ride(start_utc: u32) -> Session {
    Session {
        start_utc,
        active_s: 2700,
        elapsed_s: 2760,
        kcal: 480,
        work_kj: 430,
        zone_s: [120, 300, 900, 1000, 300, 80, 0, 0, 0],
        zone_floor: watch_ladder(190),
        hr_avg: 142,
        hr_max: 178,
        hr_max_setting: 190,
        weight_kg: 75,
        zone_count: 5,
        recovery_count: 1,
        recoveries_dropped: 0,
        recoveries: [
            Recovery {
                at_active_s: 2640,
                hr0: 170,
                hr_end: 117,
                window_s: 60,
                trusted_s: 61,
                hr0_pct_max: 89,
                trigger: TRIGGER_PAUSE,
                curve: [170, 157, 146, 137, 130, 123, 117],
                reserved: [0; 3],
            },
            Recovery::default(),
        ],
        ..Session::default()
    }
}

fn store(h: &mut History) -> String {
    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("it fits");
    String::from_utf8(buf[..n].to_vec()).expect("ascii")
}

#[test]
fn a_ride_survives_a_round_trip() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&a_ride(1_756_800_000));
    let text = store(&mut h);

    let mut back = History::new();
    assert_eq!(back.load(text.as_bytes()), Load::Ok);
    assert_eq!(back.sessions().len(), 1);
    assert_eq!(back.sessions()[0], a_ride(1_756_800_000));
}

#[test]
fn the_header_says_who_wrote_it_and_what_is_missing() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&a_ride(1_756_800_000));
    let text = store(&mut h);
    assert!(text.starts_with("{\"version\":1,"), "{text}");
    assert!(text.contains("\"app\":\"Spin\""));
    assert!(text.contains("\"sport\":\"indoor_cycling\""));
    assert!(text.contains("\"kept\":1"));
    assert!(text.contains("\"dropped\":0"));
}

#[test]
fn work_nobody_entered_is_absent_rather_than_zero() {
    let mut s = a_ride(1);
    s.work_kj = 0;
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&s);
    let text = store(&mut h);
    assert!(!text.contains("work_kj"), "{text}");

    let mut back = History::new();
    back.load(text.as_bytes());
    assert_eq!(back.sessions()[0].work_kj, 0);
}

#[test]
fn edwards_trimp_is_written_for_the_ladder_it_is_defined_over() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&a_ride(1));
    let text = store(&mut h);
    // 300*1 + 900*2 + 1000*3 + 300*4 + 80*5 = 6700 second-weights, over 60.
    assert_eq!(edwards_trimp(&a_ride(1)), Some(112));
    assert!(text.contains("\"edwards_trimp\":112"), "{text}");
}

#[test]
fn edwards_trimp_is_absent_for_a_ladder_it_is_not() {
    let mut s = a_ride(1);
    s.zone_count = 3;
    s.zone_floor = [120, 150, 170, 0, 0, 0, 0, 0];
    assert_eq!(edwards_trimp(&s), None);

    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&s);
    assert!(!store(&mut h).contains("edwards_trimp"));
}

#[test]
fn a_hand_entered_five_zone_ladder_is_not_edwards() {
    let mut s = a_ride(1);
    // A published polarised model at five zones, not 50/60/70/80/90.
    s.zone_floor = [110, 132, 150, 162, 174, 0, 0, 0];
    assert_eq!(edwards_trimp(&s), None);
}

#[test]
fn a_floor_one_bpm_off_is_still_edwards() {
    let mut s = a_ride(1);
    s.zone_floor[2] += 1;
    assert!(edwards_trimp(&s).is_some());
    s.zone_floor[2] += 1;
    assert_eq!(edwards_trimp(&s), None);
}

#[test]
fn time_below_zone_one_carries_no_weight() {
    let mut s = a_ride(1);
    let with = edwards_trimp(&s).unwrap();
    s.zone_s[0] += 3600;
    assert_eq!(edwards_trimp(&s), Some(with));
}

#[test]
fn the_oldest_ride_goes_when_the_entry_cap_is_reached() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    for i in 0..(MAX_SESSIONS as u32 + 5) {
        h.add(&a_ride(1_000 + i));
    }
    assert_eq!(h.sessions().len(), MAX_SESSIONS);
    assert_eq!(h.dropped(), 5);
    assert_eq!(h.sessions()[0].start_utc, 1_005);
    assert_eq!(
        h.sessions()[MAX_SESSIONS - 1].start_utc,
        1_000 + MAX_SESSIONS as u32 + 4
    );
}

#[test]
fn the_dropped_count_survives_a_round_trip() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    for i in 0..(MAX_SESSIONS as u32 + 5) {
        h.add(&a_ride(1_000 + i));
    }
    let text = store(&mut h);

    let mut back = History::new();
    back.load(text.as_bytes());
    assert_eq!(back.dropped(), 5);

    back.add(&a_ride(9_999));
    assert_eq!(back.dropped(), 6);
}

#[test]
fn the_same_ride_recorded_twice_is_one_entry() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&a_ride(1_756_800_000));
    let mut again = a_ride(1_756_800_000);
    again.work_kj = 500;
    h.add(&again);

    assert_eq!(h.sessions().len(), 1);
    assert_eq!(h.sessions()[0].work_kj, 500);
    assert_eq!(h.dropped(), 0);
}

/// The bound that actually holds, since the byte cap is the one flash cares
/// about. Nothing here may be relaxed without re-running this.
#[test]
fn the_widest_possible_log_fits_the_buffer() {
    let mut h = History::new();
    h.name(b"indoor_cycling_x", b"indoor_cycling_x");
    let wide = Session {
        start_utc: u32::MAX,
        active_s: u32::MAX,
        elapsed_s: u32::MAX,
        kcal: u16::MAX,
        work_kj: u16::MAX,
        zone_s: [u16::MAX; MAX_ZONE_BUCKETS],
        zone_floor: [255; MAX_ZONES],
        hr_avg: 255,
        hr_max: 255,
        hr_max_setting: 255,
        weight_kg: 255,
        zone_count: 8,
        recovery_count: MAX_RECOVERIES as u8,
        recoveries_dropped: 255,
        reserved: [0; 3],
        recoveries: [Recovery {
            at_active_s: u32::MAX,
            hr0: 255,
            hr_end: 255,
            window_s: 255,
            trusted_s: 255,
            hr0_pct_max: 255,
            trigger: TRIGGER_PAUSE,
            curve: [255; CURVE_POINTS],
            reserved: [0; 3],
        }; MAX_RECOVERIES],
    };
    for i in 0..MAX_SESSIONS {
        let mut s = wide;
        s.start_utc = u32::MAX - i as u32;
        h.add(&s);
    }

    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h
        .save(&mut buf)
        .expect("the entry cap must fit the byte cap");
    assert_eq!(h.sessions().len(), MAX_SESSIONS, "nothing was evicted");
    // Measured, not argued. Re-run after changing any cap or adding a field;
    // at 24 sessions this was 16,084 of 16,384, which is why the cap is 20.
    assert_eq!(n, 13_420, "the widest log's size moved");
    assert!(n < MAX_STORE_BYTES, "{n} bytes of {MAX_STORE_BYTES}");
}

/// The number that decides whether the cap is generous, as opposed to the
/// pathological one above.
#[test]
fn an_ordinary_log_is_measured_too() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    for i in 0..MAX_SESSIONS as u32 {
        h.add(&a_ride(1_756_800_000 + i * 86_400));
    }
    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).unwrap();
    assert_eq!(n, 9_046, "the ordinary log's size moved");
}

#[test]
fn a_buffer_too_small_drops_the_oldest_and_keeps_the_newest() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    for i in 0..MAX_SESSIONS as u32 {
        h.add(&a_ride(1_000 + i));
    }
    // Room for a handful of entries, not all of them.
    let mut small = [0u8; 1200];
    let n = h.save(&mut small).expect("some entries fit");
    assert!(n <= small.len());
    assert!(h.sessions().len() < MAX_SESSIONS);
    assert_eq!(
        h.sessions().last().unwrap().start_utc,
        1_000 + MAX_SESSIONS as u32 - 1,
        "the newest ride must survive"
    );
    assert!(h.dropped() > 0);
}

#[test]
fn a_buffer_that_cannot_hold_one_ride_writes_nothing() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&a_ride(1));
    let mut tiny = [0u8; 32];
    assert_eq!(h.save(&mut tiny), None);
}

#[test]
fn an_absent_file_is_an_empty_log_not_an_error() {
    let mut h = History::new();
    assert_eq!(h.load(&[]), Load::Ok);
    assert_eq!(h.sessions().len(), 0);
}

#[test]
fn a_newer_schema_is_refused_rather_than_overwritten() {
    let mut h = History::new();
    assert_eq!(h.load(br#"{"version":2,"sessions":[]}"#), Load::Newer);
    assert_eq!(h.sessions().len(), 0);
}

#[test]
fn rubbish_is_unreadable_rather_than_half_read() {
    let mut h = History::new();
    for bad in [
        &b"not json at all"[..],
        &b"{}"[..],
        &br#"{"version":1}"#[..],
        &br#"{"sessions":[]}"#[..],
        &br#"{"version":"one","sessions":[]}"#[..],
    ] {
        assert_eq!(
            h.load(bad),
            Load::Unreadable,
            "{:?}",
            core::str::from_utf8(bad)
        );
        assert_eq!(h.sessions().len(), 0);
    }
}

#[test]
fn a_file_past_the_cap_is_not_read_into_memory() {
    let mut h = History::new();
    let huge = vec![b'{'; MAX_STORE_BYTES + 1];
    assert_eq!(h.load(&huge), Load::Unreadable);
}

#[test]
fn a_failed_load_leaves_the_name_alone() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.load(b"not json");
    h.add(&a_ride(1));
    assert!(store(&mut h).contains("\"app\":\"Spin\""));
}

#[test]
fn a_brace_inside_a_string_does_not_confuse_the_parser() {
    let mut h = History::new();
    assert_eq!(
        h.load(br#"{"version":1,"app":"a{b}c[","dropped":7,"sessions":[]}"#),
        Load::Ok
    );
    assert_eq!(h.dropped(), 7);
}

#[test]
fn a_session_with_more_recoveries_than_fit_counts_the_rest() {
    let json = br#"{"version":1,"sessions":[{"start_utc":5,"recoveries":[
        {"hr0":170,"hr_end":130},{"hr0":168,"hr_end":128},{"hr0":166,"hr_end":126}]}]}"#;
    let mut h = History::new();
    assert_eq!(h.load(json), Load::Ok);
    let s = h.sessions()[0];
    assert_eq!(s.recovery_count as usize, MAX_RECOVERIES);
    assert_eq!(s.recoveries_dropped, 1);
}

#[test]
fn average_power_matches_the_fit_files_own_arithmetic() {
    // ActivityWriter::averageWatts: joules over active seconds, rounded.
    assert_eq!(avg_power_w(430, 2700), 159);
    assert_eq!(avg_power_w(0, 2700), 0);
    assert_eq!(avg_power_w(430, 0), 0);
}

#[test]
fn efficiency_factor_is_absent_rather_than_imputed() {
    assert_eq!(efficiency_factor_x1000(430, 2700, 142), 1120);
    assert_eq!(efficiency_factor_x1000(0, 2700, 142), 0);
    assert_eq!(efficiency_factor_x1000(430, 2700, 0), 0);
}
