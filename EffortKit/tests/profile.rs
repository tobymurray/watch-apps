//! The app's own interior record: what it keeps, what it refuses, and the
//! baselines derived from it.

use effortkit::baseline::MIN_SESSIONS_FOR_COMPARISON;
use effortkit::hr::HrSource;
use effortkit::profile::*;
use effortkit::record::Recovery;
use effortkit::session::*;
use effortkit::window::WindowKind;
use effortkit::Unavailable;

fn window(kind: WindowKind, hr0: u8, hr_end: u8) -> Recovery {
    Recovery {
        kind: kind.code(),
        at_active_s: 900,
        hr0,
        hr_end,
        window_s: 60,
        trusted_s: 61,
        hr0_pct_max: 88,
        curve: [hr0, hr0 - 1, hr0 - 4, hr0 - 7, hr0 - 12, hr0 - 15, hr_end],
        source: HrSource::Optical.code(),
        reserved: [0; 2],
    }
}

fn session(utc: u32) -> SessionRecord {
    let mut s = SessionRecord {
        started_utc: utc,
        active_s: 3600,
        hr_mean: 142.5,
        hr_max: 181.25,
        hr_covered_s: 3500,
        hr_source: HrSource::External,
        segmented: true,
        rally_count: 42,
        rally_s: 900,
        rest_s: 1200,
        off_court_s: 1500,
        ..SessionRecord::EMPTY
    };
    s.add_window(window(WindowKind::BetweenRallies, 170, 158));
    s.add_window(window(WindowKind::OffCourt, 175, 120));
    s
}

#[test]
fn a_session_survives_a_round_trip_with_its_windows() {
    let mut p = Profile::empty();
    p.record(session(1_788_483_397));

    let mut buf = [0u8; MAX_BYTES];
    let n = p.write_json(&mut buf).expect("it fits");
    let (back, load) = Profile::parse_json(&buf[..n]);
    assert_eq!(load, Load::Ok);
    assert_eq!(back.len(), 1);
    let s = back.sessions()[0];
    assert_eq!(s.started_utc, 1_788_483_397);
    assert_eq!(s.hr_mean, 142.5);
    assert_eq!(s.hr_max, 181.25);
    assert_eq!(s.windows().len(), 2);
    // Everything but the curve, which this file deliberately does not carry:
    // seven numbers per window, wanted only for a fit this hardware cannot
    // support, and the cross-app log already keeps one per measurement.
    let stripped = |w: Recovery| Recovery { curve: [0; 7], ..w };
    assert_eq!(s.windows()[0], stripped(window(WindowKind::BetweenRallies, 170, 158)));
    assert_eq!(s.windows()[1], stripped(window(WindowKind::OffCourt, 175, 120)));
    assert_eq!(s.windows()[0].curve, [0; 7], "no curve in the interior file");
}

#[test]
fn the_windows_are_stored_so_a_mean_can_be_rederived_rather_than_orphaned() {
    // The whole reason the file keeps windows instead of the means it could
    // have stored: change what qualifies and the history re-derives.
    let mut p = Profile::empty();
    p.record(session(1));
    let mut buf = [0u8; MAX_BYTES];
    let n = p.write_json(&mut buf).expect("it fits");
    let (back, _) = Profile::parse_json(&buf[..n]);
    let s = back.sessions()[0];

    assert_eq!(s.value(Metric::RecoveryShort), Some(12.0));
    assert_eq!(s.value(Metric::RecoveryLong), Some(55.0));
    // And the inputs those came from are still there.
    assert_eq!(s.windows()[0].hr0, 170);
    assert_eq!(s.windows()[0].hr_end, 158);
    assert_eq!(s.window_count_of(WindowKind::BetweenRallies), 1);
}

#[test]
fn a_mean_counts_every_qualifying_window_even_when_its_detail_did_not_fit() {
    // The bound is on inspection, never on the count or the mean: a session
    // with more rests than the file keeps rows for must still report the right
    // number, or the bound would silently change the measurement.
    let mut s = SessionRecord { active_s: 3600, hr_covered_s: 3500, ..SessionRecord::EMPTY };
    for i in 0..(MAX_SESSION_WINDOWS as u8 + 6) {
        s.add_window(window(WindowKind::BetweenRallies, 180, 180 - 10 - i));
    }
    assert_eq!(s.windows().len(), MAX_SESSION_WINDOWS, "detail is bounded");
    assert_eq!(s.windows_dropped, 6, "and it says how much went");
    assert_eq!(s.window_count_of(WindowKind::BetweenRallies), 10, "the count is not");

    let mut p = Profile::empty();
    p.record(s);
    let mut buf = [0u8; MAX_BYTES];
    let n = p.write_json(&mut buf).expect("it fits");
    let (back, _) = Profile::parse_json(&buf[..n]);
    let got = back.sessions()[0];
    assert_eq!(got.window_count_of(WindowKind::BetweenRallies), 10);
    assert_eq!(got.mean_drop(WindowKind::BetweenRallies), s.mean_drop(WindowKind::BetweenRallies));
}

#[test]
fn a_session_recorded_twice_replaces_itself_rather_than_voting_twice() {
    let mut p = Profile::empty();
    p.record(session(1_788_483_397));
    let mut again = session(1_788_483_397);
    again.hr_mean = 99.0;
    p.record(again);

    assert_eq!(p.len(), 1, "a retried write must not vote twice in a baseline");
    assert_eq!(p.sessions()[0].hr_mean, 99.0);
}

#[test]
fn the_oldest_session_falls_out_at_the_cap() {
    let mut p = Profile::empty();
    for i in 0..(MAX_SESSIONS as u32 + 3) {
        p.record(session(1_000 + i));
    }
    assert_eq!(p.len(), MAX_SESSIONS);
    assert_eq!(p.sessions()[0].started_utc, 1_003);
}

#[test]
fn every_way_of_being_wrong_yields_an_empty_profile_and_says_which() {
    assert_eq!(Profile::parse_json(b"").1, Load::Absent);
    assert_eq!(Profile::parse_json(b"not json").1, Load::Malformed);
    assert_eq!(Profile::parse_json(br#"{"schema":99,"sessions":[]}"#).1, Load::UnknownSchema);
    let (p, _) = Profile::parse_json(br#"{"schema":99,"sessions":[{"utc":1}]}"#);
    assert!(p.is_empty(), "a schema this build does not know is refused whole");
}

#[test]
fn a_buffer_too_small_writes_nothing_rather_than_a_truncated_file() {
    let mut p = Profile::empty();
    p.record(session(1));
    let mut tiny = [0u8; 32];
    assert_eq!(p.write_json(&mut tiny), Err(BufferTooSmall));
}

#[test]
fn a_baseline_is_built_only_from_the_sessions_admitted_to_it() {
    let mut p = Profile::empty();
    // Five that may vote, and one drill session that may not vote on rallies.
    for i in 0..MIN_SESSIONS_FOR_COMPARISON as u32 {
        p.record(session(1_000 + i));
    }
    let mut drill = session(2_000);
    drill.rally_count = 2;
    p.record(drill);

    let hr = p.baseline_of(Metric::HrMean);
    assert_eq!(hr.sessions(), 6, "every session votes on heart rate");

    let rallies = p.baseline_of(Metric::RallyCount);
    assert_eq!(rallies.sessions(), 5, "the drill session does not vote on rallies");
}

#[test]
fn no_comparison_is_offered_before_the_warm_up_threshold() {
    let mut p = Profile::empty();
    p.record(session(1));
    let b = p.baseline_of(Metric::HrMean);
    assert_eq!(
        b.compare(140.0),
        Err(Unavailable::WarmingUp { have: 1, need: MIN_SESSIONS_FOR_COMPARISON })
    );
}

#[test]
fn a_session_too_short_to_vote_is_still_recorded_and_still_readable() {
    let mut p = Profile::empty();
    let mut short = session(1);
    short.active_s = 300;
    p.record(short);

    assert_eq!(p.len(), 1, "it happened, so it is recorded");
    assert_eq!(p.baseline_of(Metric::HrMean).sessions(), 0, "it does not vote");
}

/// The cap that decides, measured rather than argued.
#[test]
fn the_widest_profile_fits_its_cap() {
    let mut p = Profile::empty();
    let mut wide = SessionRecord {
        started_utc: u32::MAX,
        active_s: u32::MAX,
        hr_mean: 655.35,
        hr_max: 655.35,
        hr_covered_s: u32::MAX,
        hr_source: HrSource::External,
        segmented: true,
        rally_count: u32::MAX,
        rally_s: u32::MAX,
        rest_s: u32::MAX,
        off_court_s: u32::MAX,
        windows_dropped: u16::MAX,
        ..SessionRecord::EMPTY
    };
    for _ in 0..MAX_SESSION_WINDOWS {
        wide.add_window(Recovery {
            at_active_s: u32::MAX,
            hr0: 255,
            hr_end: 255,
            window_s: 255,
            trusted_s: 255,
            hr0_pct_max: 255,
            kind: WindowKind::OffCourt.code(),
            curve: [255; 7],
            source: HrSource::External.code(),
            reserved: [0; 2],
        });
    }
    for i in 0..MAX_SESSIONS {
        let mut s = wide;
        s.started_utc = u32::MAX - i as u32;
        p.record(s);
    }

    let mut buf = [0u8; MAX_BYTES];
    let n = p.write_json(&mut buf).expect("the entry cap must fit the byte cap");
    // MEASURED. Re-run after changing any cap or adding a field.
    assert_eq!(n, 15_685, "the widest profile's size moved");
    assert!(n < MAX_BYTES, "{n} bytes of {MAX_BYTES}");
}

#[test]
fn an_ordinary_profile_of_twenty_sessions_is_comfortable() {
    let mut p = Profile::empty();
    for i in 0..MAX_SESSIONS {
        p.record(session(1_788_000_000 + i as u32));
    }
    let mut buf = [0u8; MAX_BYTES];
    let n = p.write_json(&mut buf).expect("it fits");
    assert_eq!(n, 9_705, "the ordinary profile's size moved");
}
