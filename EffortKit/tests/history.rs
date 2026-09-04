//! The cross-app session log: its schema, its bounds and its refusals.

use effortkit::history::*;
use effortkit::hr::HrSource;
use effortkit::record::*;
use effortkit::window::WindowKind;

fn recovery() -> Recovery {
    Recovery {
        at_active_s: 1200,
        hr0: 161,
        hr_end: 138,
        window_s: 60,
        trusted_s: 61,
        hr0_pct_max: 88,
        kind: WindowKind::Pause.code(),
        curve: [161, 162, 159, 154, 147, 144, 138],
        source: HrSource::Optical.code(),
        reserved: [0; 2],
    }
}

/// A session shaped like Spin's Ride A of 2026-09-03.
fn session(start_utc: u32) -> Session {
    let mut s = Session {
        start_utc,
        active_s: 1142,
        elapsed_s: 1410,
        kcal: 129,
        work_kj: 160,
        hr_avg: 116,
        hr_max: 162,
        hr_max_setting: 184,
        weight_kg: 90,
        zone_count: 5,
        zone_floor: [92, 110, 129, 147, 166, 0, 0, 0],
        zone_s: [162, 321, 341, 173, 145, 0, 0, 0, 0],
        ..Session::EMPTY
    };
    s.add_recovery(recovery());
    s
}

fn round_trip(h: &mut History) -> History {
    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("it fits");
    let mut back = History::new();
    assert_eq!(back.load(&buf[..n]), Load::Ok);
    back
}

#[test]
fn a_session_survives_a_round_trip_field_for_field() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&session(1_788_483_397));

    let back = round_trip(&mut h);
    assert_eq!(back.sessions().len(), 1);
    let s = back.sessions()[0];
    assert_eq!(s, session(1_788_483_397));
    assert_eq!(s.recoveries()[0], recovery());
}

#[test]
fn a_session_recorded_twice_replaces_itself_rather_than_appearing_twice() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&session(1_788_483_397));
    let mut again = session(1_788_483_397);
    again.kcal = 999;
    h.add(&again);

    assert_eq!(h.sessions().len(), 1, "a retried write must not vote twice");
    assert_eq!(h.sessions()[0].kcal, 999);
}

#[test]
fn the_oldest_session_goes_and_the_newest_always_lands() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    for i in 0..(MAX_SESSIONS as u32 + 5) {
        h.add(&session(1_000_000 + i));
    }
    assert_eq!(h.sessions().len(), MAX_SESSIONS);
    assert_eq!(h.dropped(), 5);
    assert_eq!(h.sessions()[0].start_utc, 1_000_005, "the oldest went");
    assert_eq!(
        h.sessions()[MAX_SESSIONS - 1].start_utc,
        1_000_000 + MAX_SESSIONS as u32 + 4,
        "the newest landed"
    );
}

#[test]
fn work_that_nobody_entered_is_absent_rather_than_zero() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    let mut s = session(1);
    s.work_kj = 0;
    h.add(&s);

    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("it fits");
    let text = core::str::from_utf8(&buf[..n]).unwrap();
    assert!(!text.contains("work_kj"), "zero is a claim that no work was done");
}

#[test]
fn a_newer_schema_is_refused_and_nothing_is_read_from_it() {
    let mut h = History::new();
    let newer = br#"{"version":99,"app":"Spin","sessions":[{"start_utc":1}]}"#;
    assert_eq!(h.load(newer), Load::Newer);
    assert_eq!(h.sessions().len(), 0, "a writer that knows more is not clobbered");
}

#[test]
fn something_that_is_not_this_file_is_unreadable_rather_than_empty() {
    let mut h = History::new();
    assert_eq!(h.load(b"not json at all"), Load::Unreadable);
    assert_eq!(h.load(br#"{"sessions":[]}"#), Load::Unreadable, "no version");
    assert_eq!(h.load(b""), Load::Ok, "an absent file is a first session");
}

#[test]
fn a_version_one_file_reads_here_and_simply_carries_no_discard_counts() {
    // Version 2 is a superset of version 1, so the upgrade needs no conversion
    // pass. A v1 session has no counts because it never recorded any, which is
    // not the same as having recorded zero.
    let v1 = br#"{"version":1,"app":"Spin","sport":"indoor_cycling","kept":1,"dropped":0,
      "sessions":[{"start_utc":1788483397,"active_s":1142,"elapsed_s":1410,"hr_avg":116,
      "hr_max":162,"hr_max_setting":184,"weight_kg":90,"kcal":129,"work_kj":160,
      "zone_count":5,"zone_floors":[92,110,129,147,166],"zone_s":[162,321,341,173,145,0],
      "recoveries":[{"at_active_s":1200,"trigger":"pause","hr0":161,"hr_end":138,
      "drop_bpm":23,"window_s":60,"hr0_pct_max":88,"trusted_s":61,"source":"optical",
      "curve":[161,162,159,154,147,144,138]}],"recoveries_dropped":0}]}"#;
    let mut h = History::new();
    assert_eq!(h.load(v1), Load::Ok);
    let s = h.sessions()[0];
    assert_eq!(s.start_utc, 1_788_483_397);
    assert_eq!(s.hr_avg, 116);
    assert_eq!(s.recoveries().len(), 1);
    assert_eq!(s.recoveries()[0].hr0, 161);
    assert_eq!(s.recoveries()[0].drop_bpm(), 23);
    // Version 1 spelled the field "trigger"; it reads as the same kind.
    assert_eq!(s.recoveries()[0].kind, WindowKind::Pause.code());
    assert_eq!(s.discarded.total(), 0, "absent, because v1 recorded none");
}

#[test]
fn the_discard_counts_survive_a_round_trip() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    let mut s = session(1);
    s.discarded = DiscardCounts {
        too_short: 2,
        too_easy: 1,
        source_changed: 3,
        ..DiscardCounts::NONE
    };
    h.add(&s);

    let back = round_trip(&mut h);
    let d = back.sessions()[0].discarded;
    assert_eq!(d.too_short, 2);
    assert_eq!(d.too_easy, 1);
    assert_eq!(d.source_changed, 3);
    assert_eq!(d.total(), 6);
}

#[test]
fn a_session_that_discarded_nothing_costs_no_bytes_for_saying_so() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&session(1));
    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("it fits");
    let text = core::str::from_utf8(&buf[..n]).unwrap();
    assert!(!text.contains("discarded"), "the ordinary session pays nothing");
}

#[test]
fn a_session_that_measured_nothing_can_still_say_why() {
    // The case the desk check of 2026-09-03 produced: every window discarded
    // for want of a maximum heart rate, and a text log the field test tells you
    // to delete was the only record of it.
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    let mut s = Session { start_utc: 1, active_s: 79, ..Session::EMPTY };
    s.discarded.no_max_hr = 1;
    h.add(&s);

    let back = round_trip(&mut h);
    let got = back.sessions()[0];
    assert_eq!(got.recoveries().len(), 0);
    assert_eq!(got.discarded.no_max_hr, 1, "a year later, it still says why");
}

#[test]
fn edwards_trimp_is_emitted_only_for_the_ladder_it_is_defined_over() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    // The watch's own default: 50/60/70/80/90% of 184.
    h.add(&session(1));
    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("it fits");
    assert!(core::str::from_utf8(&buf[..n]).unwrap().contains("edwards_trimp"));

    // A three-zone polarised split is a ladder Edwards never wrote weights for.
    let mut h2 = History::new();
    h2.name(b"Spin", b"indoor_cycling");
    let mut s = session(1);
    s.zone_count = 3;
    s.zone_floor = [120, 150, 170, 0, 0, 0, 0, 0];
    s.zone_s = [100, 200, 300, 400, 0, 0, 0, 0, 0];
    h2.add(&s);
    let n2 = h2.save(&mut buf).expect("it fits");
    assert!(!core::str::from_utf8(&buf[..n2]).unwrap().contains("edwards_trimp"));
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
            kind: WindowKind::Pause.code(),
            curve: [255; CURVE_POINTS],
            source: HrSource::External.code(),
            reserved: [0; 2],
        }; MAX_RECOVERIES],
        discarded: DiscardCounts {
            not_calibrated: u16::MAX,
            not_measurable: u16::MAX,
            no_max_hr: u16::MAX,
            too_short: u16::MAX,
            too_easy: u16::MAX,
            already_falling: u16::MAX,
            no_baseline_history: u16::MAX,
            no_baseline: u16::MAX,
            dropout: u16::MAX,
            no_endpoint: u16::MAX,
            effort_resumed: u16::MAX,
            session_ended: u16::MAX,
            source_changed: u16::MAX,
            source_not_accepted: u16::MAX,
        },
    };
    for i in 0..MAX_SESSIONS {
        let mut s = wide;
        s.start_utc = u32::MAX - i as u32;
        h.add(&s);
    }

    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("the newest session must always land");
    assert!(n <= MAX_STORE_BYTES, "{n} bytes of {MAX_STORE_BYTES}");

    // MEASURED, not argued, and it does NOT reach the entry cap: with every
    // one of the fourteen discard reasons non-zero at once, a session
    // serialises to about 1,040 bytes and only 16 of the 20 fit. Every reason
    // firing in one session is not a reachable state -- a kind that is not
    // calibrated never reaches the gates, and no_max_hr is gated ahead of
    // too_easy -- so this pins the degradation rather than a guarantee.
    // The guarantee is the next test.
    assert_eq!(h.sessions().len(), 16, "the widest log's entry count moved");
    assert_eq!(h.dropped(), 4, "the oldest went, not the newest");
    assert_eq!(
        h.sessions()[h.sessions().len() - 1].start_utc,
        u32::MAX - (MAX_SESSIONS as u32 - 1),
        "the newest session is the one that survived"
    );
}

/// The guarantee: a session as wide as one can actually be still reaches the
/// entry cap, so the byte cap is not what a real wearer ever meets.
#[test]
fn the_widest_reachable_log_still_holds_every_entry() {
    let mut h = History::new();
    h.name(b"indoor_cycling_x", b"indoor_cycling_x");
    let mut wide = Session {
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
            kind: WindowKind::Pause.code(),
            curve: [255; CURVE_POINTS],
            source: HrSource::External.code(),
            reserved: [0; 2],
        }; MAX_RECOVERIES],
        ..Session::EMPTY
    };
    // Five reasons at once is already more than any recorded session has
    // produced; the desk check of 2026-09-03 produced one and Ride A two.
    wide.discarded = DiscardCounts {
        too_short: u16::MAX,
        too_easy: u16::MAX,
        already_falling: u16::MAX,
        dropout: u16::MAX,
        source_changed: u16::MAX,
        ..DiscardCounts::NONE
    };
    for i in 0..MAX_SESSIONS {
        let mut s = wide;
        s.start_utc = u32::MAX - i as u32;
        h.add(&s);
    }

    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("the entry cap must fit the byte cap");
    assert_eq!(h.sessions().len(), MAX_SESSIONS, "nothing was evicted");
    // MEASURED. Re-run after changing any cap or adding a field.
    assert_eq!(n, 16_340, "the widest reachable log's size moved");
    assert!(n < MAX_STORE_BYTES, "{n} bytes of {MAX_STORE_BYTES}");
}

/// The number that decides whether the cap is generous, as opposed to the
/// pathological one above.
#[test]
fn an_ordinary_log_of_twenty_sessions_is_comfortable() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    for i in 0..MAX_SESSIONS {
        h.add(&session(1_788_000_000 + i as u32));
    }
    let mut buf = [0u8; MAX_STORE_BYTES];
    let n = h.save(&mut buf).expect("it fits");
    assert_eq!(n, 9_306, "the ordinary log's size moved");
}

#[test]
fn a_buffer_too_small_for_even_one_session_writes_nothing() {
    let mut h = History::new();
    h.name(b"Spin", b"indoor_cycling");
    h.add(&session(1));
    let mut tiny = [0u8; 16];
    assert!(h.save(&mut tiny).is_none());
}

#[test]
fn a_file_larger_than_the_cap_is_refused_rather_than_parsed() {
    let mut h = History::new();
    let huge = [b'x'; MAX_STORE_BYTES + 1];
    assert_eq!(h.load(&huge), Load::Unreadable);
}

#[test]
fn the_kinds_a_reader_sees_are_the_kinds_that_were_written() {
    let mut h = History::new();
    h.name(b"Squash", b"squash");
    let mut s = Session { start_utc: 1, ..Session::EMPTY };
    s.add_recovery(Recovery { kind: WindowKind::BetweenRallies.code(), ..recovery() });
    s.add_recovery(Recovery { kind: WindowKind::OffCourt.code(), ..recovery() });
    h.add(&s);

    let back = round_trip(&mut h);
    let got = back.sessions()[0];
    assert_eq!(got.recoveries()[0].kind, WindowKind::BetweenRallies.code());
    assert_eq!(got.recoveries()[1].kind, WindowKind::OffCourt.code());
}
