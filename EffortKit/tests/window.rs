//! What the detector will and will not measure.
//!
//! Every test here pins one gate. Reverting the gate it names must fail it.

use effortkit::hr::{HrSource, SourcePolicy};
use effortkit::window::*;

/// A wearer with a 190 bpm maximum, so 80% is 152.
const MAX_HR: u8 = 190;

/// The kernel's top confidence, which is what a healthy reading carries.
const FULL_TRUST: u8 = 3;

/// The shipping whole-session calibration, which is what Spin links.
static SPIN: Calibration = Calibration::absent().with(WindowKind::Pause, HRR60);

/// A wearer on the bike: seconds of steady effort, then a pause.
struct Ride {
    det: Detector,
    utc: i64,
    active_s: u32,
    /// The sensor every second claims to come from, unless a test changes it.
    src: HrSource,
}

impl Ride {
    fn new(max_hr: u8) -> Self {
        Self::borrowing(&SPIN, max_hr)
    }

    /// Leaks, because a detector borrows its calibration for 'static and a test
    /// builds one at run time. A watch build has a `static` to point at.
    fn with(calibration: Calibration, max_hr: u8) -> Self {
        Self::borrowing(Box::leak(Box::new(calibration)), max_hr)
    }

    fn borrowing(calibration: &'static Calibration, max_hr: u8) -> Self {
        let mut det = Detector::new(calibration);
        det.start(max_hr);
        Ride { det, utc: 1_700_000_000, active_s: 0, src: HrSource::External }
    }

    /// `n` seconds of trusted effort at `bpm`.
    fn effort(&mut self, n: u32, bpm: f32) -> Step {
        self.run(n, |_| (bpm, true))
    }

    /// `n` seconds of the caller's choosing; the closure gets the second index.
    fn run(&mut self, n: u32, mut f: impl FnMut(u32) -> (f32, bool)) -> Step {
        let mut last = Step::Nothing;
        for i in 0..n {
            self.utc += 1;
            self.active_s += 1;
            let (bpm, trusted) = f(i);
            let trust = if trusted { FULL_TRUST } else { 0 };
            last = self.det.second(self.utc, bpm, trust, self.src, self.active_s);
            if last != Step::Nothing {
                return last;
            }
        }
        last
    }

    /// Seconds while paused: the clock runs, active time does not.
    fn paused(&mut self, n: u32, mut f: impl FnMut(u32) -> (f32, bool)) -> Step {
        let mut last = Step::Nothing;
        for i in 0..n {
            self.utc += 1;
            let (bpm, trusted) = f(i);
            let trust = if trusted { FULL_TRUST } else { 0 };
            last = self.det.second(self.utc, bpm, trust, self.src, self.active_s);
            if last != Step::Nothing {
                return last;
            }
        }
        last
    }
}

/// A first-order fall, which is the shape Barak et al. fitted; `i` 0 is the
/// second effort ceased, so the baseline is `start` exactly.
fn decay(start: f32, asymptote: f32, tau: f32) -> impl Fn(u32) -> (f32, bool) {
    move |i| {
        let t = i as f32;
        (asymptote + (start - asymptote) * (-t / tau).exp(), true)
    }
}

#[test]
fn a_pause_after_hard_effort_is_measured() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Nothing);
    assert_eq!(r.paused(70, decay(170.0, 90.0, 55.0)), Step::Completed);

    let m = r.det.take().expect("a measurement");
    assert_eq!(m.kind, WindowKind::Pause.code());
    assert_eq!(m.hr0, 170);
    assert_eq!(m.window_s, 60);
    assert_eq!(m.trusted_s, 61);
    // 170 as a percentage of 190.
    assert_eq!(m.hr0_pct_max, 89);
    // 90 + 80 * e^-(60/55) = 116.9 -> 117, so 53 bpm recovered.
    assert_eq!(m.hr_end, 117);
    assert_eq!(m.drop_bpm(), 53);
    assert_eq!(m.curve[0], 170);
    assert_eq!(m.curve[6], m.hr_end);
    for w in m.curve.windows(2) {
        assert!(w[0] > w[1], "curve is not monotonic: {:?}", m.curve);
    }
}

#[test]
fn a_second_pause_in_the_same_session_is_measured_too() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(70, decay(170.0, 90.0, 55.0)), Step::Completed);
    r.det.take().unwrap();

    assert_eq!(r.det.resume(), Step::Nothing);
    r.effort(600, 172.0);
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(70, decay(172.0, 90.0, 50.0)), Step::Completed);
    assert!(r.det.take().is_some());
}

#[test]
fn a_watch_with_no_maximum_measures_nothing() {
    let mut r = Ride::new(0);
    r.effort(600, 170.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NoMaxHr));
    assert!(r.det.take().is_none());
    assert_eq!(r.det.discarded().no_max_hr, 1);
}

#[test]
fn the_first_minutes_of_a_session_measure_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(179, 170.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::TooShort));
}

#[test]
fn effort_is_the_current_bout_not_the_whole_session() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(5, |_| (160.0, true));
    r.det.resume();

    // Ten minutes are behind us, but only ten seconds of it since resuming.
    r.effort(10, 170.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::TooShort));
}

#[test]
fn an_easy_pause_measures_nothing() {
    let mut r = Ride::new(MAX_HR);
    // 80% of 190 is 152; 140 is a shoelace, not an effort.
    r.effort(600, 140.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Nothing);
    assert_eq!(r.paused(3, |_| (140.0, true)), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::TooEasy));
}

#[test]
fn exactly_the_intensity_floor_is_measured() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 152.0); // 80.0% of 190
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(70, decay(152.0, 90.0, 55.0)), Step::Completed);
    assert_eq!(r.det.take().unwrap().hr0_pct_max, 80);
}

#[test]
fn a_heart_rate_already_falling_measures_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 178.0);
    // Half a minute of soft-pedalling, still above the intensity floor, so only
    // the falling gate can catch this.
    r.run(30, |i| (178.0 - (i as f32) * 0.8, true));
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(3, |_| (154.0, true)), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::AlreadyFalling));
}

#[test]
fn ordinary_drift_is_not_a_fall() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    // The pulled recordings measured 0.49 bpm between consecutive readings;
    // this is ten times that and must still be effort rather than recovery.
    r.run(30, |i| (170.0 - (i as f32) * 0.2, true));
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(70, decay(164.0, 90.0, 55.0)), Step::Completed);
}

#[test]
fn a_dropout_in_the_window_is_discarded_not_interpolated() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);
    // Eight untrusted seconds out of 61 is past the 90% the window needs.
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, !(20..28).contains(&i))
    });
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::Dropout));
    assert!(r.det.take().is_none());
}

#[test]
fn a_dropout_the_size_the_recordings_measured_is_survivable() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);
    // Two seconds untrusted, which is what a real session does several times a
    // minute at the measured 5.3%.
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, !(20..22).contains(&i))
    });
    assert_eq!(step, Step::Completed);
    let m = r.det.take().unwrap();
    assert_eq!(m.trusted_s, 59);
    // The ten-second mark was missed by two, which is inside the grace, so the
    // point is a reading taken two seconds late rather than a guess.
    assert_ne!(m.curve[2], 0);
}

#[test]
fn a_gap_across_a_curve_mark_leaves_a_hole_rather_than_a_guess() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, !(20..24).contains(&i))
    });
    assert_eq!(step, Step::Completed);
    let m = r.det.take().unwrap();
    assert_eq!(m.curve[2], 0, "curve: {:?}", m.curve);
    assert_ne!(m.curve[3], 0);
    assert_eq!(m.trusted_s, 57);
}

#[test]
fn an_untrusted_endpoint_stretches_the_window_and_says_so() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, i != 60)
    });
    assert_eq!(step, Step::Completed);
    let m = r.det.take().unwrap();
    assert_eq!(m.window_s, 61);
    assert_eq!(m.trusted_s, 61);
}

#[test]
fn an_endpoint_that_never_arrives_is_discarded() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, i < 59)
    });
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NoEndpoint));
}

#[test]
fn resuming_effort_ends_the_window() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(30, decay(170.0, 90.0, 55.0));
    assert_eq!(r.det.resume(), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::EffortResumed));
    assert!(r.det.take().is_none());
}

#[test]
fn ending_the_session_ends_the_window() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(30, decay(170.0, 90.0, 55.0));
    assert_eq!(r.det.end(), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::SessionEnded));
}

#[test]
fn no_trusted_baseline_measures_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(5, |_| (170.0, false)), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NoBaseline));
}

#[test]
fn a_session_of_untrusted_seconds_has_no_history_to_judge_by() {
    let mut r = Ride::new(MAX_HR);
    // Trusted enough to be a session, but the last 30 seconds are mostly gone.
    r.effort(600, 170.0);
    r.run(30, |i| (170.0, i < 5));
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NoBaselineHistory));
}

#[test]
fn a_stalled_tick_is_seconds_that_passed_not_seconds_that_did_not() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);

    // The Service misses the baseline second entirely and comes back four
    // seconds later, well down the curve.
    r.utc += 4;
    let step = r.det.second(r.utc, 150.0, FULL_TRUST, r.src, r.active_s);
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NoBaseline));
}

#[test]
fn a_stall_inside_the_window_costs_the_seconds_it_took() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(20, decay(170.0, 90.0, 55.0));

    // Ten seconds the Service never served: they are untrusted seconds, so the
    // window falls under the 90% it needs rather than closing early.
    r.utc += 10;
    let mut step = r.det.second(r.utc, 130.0, FULL_TRUST, r.src, r.active_s);
    let mut guard = 0;
    while step == Step::Nothing && guard < 60 {
        r.utc += 1;
        guard += 1;
        step = r.det.second(r.utc, 125.0, FULL_TRUST, r.src, r.active_s);
    }
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::Dropout));
}

#[test]
fn a_clock_that_repeats_a_second_changes_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(30, decay(170.0, 90.0, 55.0));

    let before = r.det.last_discard();
    // The same second again, and one before it.
    assert_eq!(r.det.second(r.utc, 140.0, FULL_TRUST, r.src, r.active_s), Step::Nothing);
    assert_eq!(r.det.second(r.utc - 5, 140.0, FULL_TRUST, r.src, r.active_s), Step::Nothing);
    assert_eq!(r.det.last_discard(), before);

    // And the window still closes on time.
    assert_eq!(r.paused(40, decay(140.0, 90.0, 55.0)), Step::Completed);
    assert_eq!(r.det.take().unwrap().window_s, 60);
}

#[test]
fn a_window_that_changes_sensor_is_discarded() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);

    // Twenty seconds in, the kernel prefers the wrist. MEASURED over 34 minutes
    // of pulled recordings: this happens to 14% of 60 s windows, and the two
    // sensors disagree by a 95th-percentile 16 bpm -- the size of the whole
    // measurement. Those windows averaged -2.1 bpm, an apparent rise.
    let mut step = Step::Nothing;
    for i in 0..70 {
        r.utc += 1;
        r.src = if i < 20 { HrSource::External } else { HrSource::Optical };
        let (bpm, _) = fall(i);
        step = r.det.second(r.utc, bpm, FULL_TRUST, r.src, r.active_s);
        if step != Step::Nothing {
            break;
        }
    }
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::SourceChanged));
    assert!(r.det.take().is_none());
}

#[test]
fn a_measurement_names_the_sensor_it_came_from() {
    let mut r = Ride::new(MAX_HR);
    r.src = HrSource::Optical;
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(70, decay(170.0, 90.0, 55.0)), Step::Completed);
    assert_eq!(r.det.take().unwrap().source, HrSource::Optical.code());
}

#[test]
fn an_untrusted_second_does_not_count_as_a_sensor_change() {
    // An untrusted reading carries no source worth believing, so it must not
    // end a window that is otherwise on one sensor throughout.
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);

    let mut step = Step::Nothing;
    for i in 0..70 {
        r.utc += 1;
        let untrusted = (20..22).contains(&i);
        r.src = if untrusted { HrSource::Unknown } else { HrSource::External };
        let (bpm, _) = fall(i);
        step = r.det.second(r.utc, bpm, if untrusted { 0 } else { FULL_TRUST }, r.src, r.active_s);
        if step != Step::Nothing {
            break;
        }
    }
    assert_eq!(step, Step::Completed);
    assert_eq!(r.det.take().unwrap().source, HrSource::External.code());
}

#[test]
fn a_measurement_is_handed_out_once() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(70, decay(170.0, 90.0, 55.0));
    assert!(r.det.take().is_some());
    assert!(r.det.take().is_none());
}

// -- What the merge added ------------------------------------------------------

#[test]
fn a_nan_reading_cannot_reach_a_measurement() {
    // A NaN arriving trusted must be treated as no reading at all. Left
    // unhandled it propagates into the fall, then into any mean, and reaches a
    // file as a plain 0 that no reader can tell from a measured zero.
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    let fall = decay(170.0, 90.0, 55.0);
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (if i == 30 { f32::NAN } else { bpm }, true)
    });
    assert_eq!(step, Step::Completed, "one NaN is one untrusted second, not a failure");
    let m = r.det.take().unwrap();
    assert_eq!(m.trusted_s, 60, "the NaN second was not counted as trusted");
    assert!(m.hr0 > 0 && m.hr_end > 0);
    assert_eq!(m.curve[0], m.hr0, "the baseline point is the baseline");
    // The NaN fell on the 30 s curve mark, so the grace filled it one second
    // late from a real reading -- the same path a dropout takes, not a guess.
    assert_ne!(m.curve[3], 0);
    assert!(m.curve[3] < m.curve[2], "still falling: {:?}", m.curve);
}

#[test]
fn a_kind_this_app_does_not_measure_is_counted_rather_than_ignored() {
    // Spin measures a pause and nothing else. A cessation of another kind must
    // say so, not vanish: a session that measured nothing has to be able to
    // explain itself a year later.
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    assert_eq!(r.det.cease(WindowKind::OffCourt), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NotCalibrated));
    assert_eq!(r.det.discarded().not_calibrated, 1);
}

#[test]
fn a_kind_the_hardware_cannot_measure_reports_no_number() {
    let t = Thresholds::new(
        Formulation::NotMeasurableOnThisHardware,
        effortkit::Provenance::Defined { citation: "A1", defines: "nothing measurable" },
        SourcePolicy::EitherWithSourceRecorded,
        Gate::defined(90, "A1", "trusted fraction"),
        None,
        None,
        None,
    )
    .unwrap();
    let mut r = Ride::with(Calibration::absent().with(WindowKind::Pause, t), MAX_HR);
    r.effort(600, 170.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NotMeasurable));
    assert_eq!(r.det.discarded().not_measurable, 1);
}

#[test]
fn a_rest_between_rallies_needs_no_intensity_or_effort_gate() {
    // The gates that only mean something for a whole-session recovery are
    // optional, so a 30 s rest can be measured without a maximum heart rate
    // and without three minutes of preceding effort.
    let t = Thresholds::new(
        Formulation::FixedWindowDrop { window_s: 30 },
        effortkit::Provenance::Measured {
            recordings: "none",
            measured_on: "never",
            method: "exercises the machine, means nothing",
        },
        SourcePolicy::EitherWithSourceRecorded,
        Gate::defined(90, "test", "trusted fraction"),
        None,
        None,
        None,
    )
    .unwrap();
    let mut r = Ride::with(Calibration::absent().with(WindowKind::BetweenRallies, t), 0);
    r.effort(40, 170.0);
    assert_eq!(r.det.cease(WindowKind::BetweenRallies), Step::Nothing);
    assert_eq!(r.paused(35, decay(170.0, 120.0, 40.0)), Step::Completed);
    let m = r.det.take().expect("a measurement");
    assert_eq!(m.window_s, 30);
    assert_eq!(m.kind, WindowKind::BetweenRallies.code());
    // No maximum was set, so there is no intensity to express it against.
    assert_eq!(m.hr0_pct_max, 0);
}

#[test]
fn a_source_the_policy_refuses_is_counted_as_such() {
    let t = Thresholds::new(
        Formulation::FixedWindowDrop { window_s: 60 },
        effortkit::Provenance::Defined { citation: "Cole", defines: "HRR60" },
        SourcePolicy::ExternalOnly,
        Gate::defined(90, "test", "trusted fraction"),
        None,
        None,
        None,
    )
    .unwrap();
    let mut r = Ride::with(Calibration::absent().with(WindowKind::Pause, t), MAX_HR);
    r.src = HrSource::Optical;
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    assert_eq!(r.paused(3, |_| (170.0, true)), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::SourceNotAccepted));
}

#[test]
fn discards_accumulate_across_a_session_by_reason() {
    let mut r = Ride::new(MAX_HR);
    // Too early.
    r.effort(60, 170.0);
    r.det.cease(WindowKind::Pause);
    r.det.resume();
    // Long enough, but too easy.
    r.effort(600, 140.0);
    r.det.cease(WindowKind::Pause);
    r.paused(3, |_| (140.0, true));
    r.det.resume();
    // Long enough and hard enough, but effort restarts.
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);
    r.paused(10, decay(170.0, 90.0, 55.0));
    r.det.resume();

    let d = r.det.discarded();
    assert_eq!(d.too_short, 1);
    assert_eq!(d.too_easy, 1);
    assert_eq!(d.effort_resumed, 1);
    assert_eq!(d.total(), 3);
}

#[test]
fn the_shipping_calibration_says_where_each_of_its_numbers_came_from() {
    use effortkit::Provenance;
    // The interval is what the quantity is, so a paper defines it.
    assert!(matches!(HRR60.provenance(), Provenance::Defined { .. }));
    assert!(!HRR60.provenance().is_measured());
    assert_eq!(HRR60.window_s(), 60);
}

#[test]
fn a_calibration_no_window_could_satisfy_is_refused() {
    assert!(Thresholds::new(
        Formulation::FixedWindowDrop { window_s: 60 },
        effortkit::Provenance::Defined { citation: "c", defines: "d" },
        SourcePolicy::EitherWithSourceRecorded,
        Gate::defined(101, "c", "impossible"),
        None,
        None,
        None,
    )
    .is_none());
}

#[test]
fn an_uncalibrated_detector_says_so_rather_than_measuring() {
    let mut r = Ride::with(Calibration::absent(), MAX_HR);
    assert_eq!(r.det.calibration(), Err(effortkit::Unavailable::NotCalibrated));
    r.effort(600, 170.0);
    assert_eq!(r.det.cease(WindowKind::Pause), Step::Discarded);
    assert_eq!(r.det.last_discard(), Some(Reason::NotCalibrated));
}

#[test]
fn every_discard_reason_has_its_own_name_and_code() {
    let all = [
        Reason::NotCalibrated,
        Reason::NotMeasurable,
        Reason::NoMaxHr,
        Reason::TooShort,
        Reason::TooEasy,
        Reason::AlreadyFalling,
        Reason::NoBaselineHistory,
        Reason::NoBaseline,
        Reason::Dropout,
        Reason::NoEndpoint,
        Reason::EffortResumed,
        Reason::SessionEnded,
        Reason::SourceChanged,
        Reason::SourceNotAccepted,
    ];
    for (i, a) in all.iter().enumerate() {
        for b in &all[i + 1..] {
            assert_ne!(a.code(), b.code(), "{} and {} share a code", a.name(), b.name());
            assert_ne!(a.name(), b.name(), "two reasons share a name");
        }
    }
}

#[test]
fn a_confidence_floor_keeps_a_low_trust_excursion_out_of_the_curve() {
    // MEASURED, from Spin's Session 2 of 2026-09-03: a five-second excursion of
    // up to 15 bpm, entirely at trust=1, bracketed by trust=3 readings on a
    // smoothly falling signal, reached the stored curve. The fall itself was
    // untouched -- that is hr0 minus hr_end -- but `trusted_s` counted every
    // one of those seconds, which is a stronger claim than the sensor made.
    let cal: &'static Calibration = Box::leak(Box::new(
        Calibration::absent()
            .with(WindowKind::Pause, HRR60)
            .requiring_trust(Gate::measured(
                2,
                "Spin/Docs/RECOVERY-FIELD-RESULTS.md, Session 2",
                "2026-09-03",
                "a 15 bpm one-second move at trust=1 on a falling signal",
            )),
    ));
    let mut r = Ride::borrowing(cal, MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(WindowKind::Pause);

    let fall = decay(170.0, 90.0, 55.0);
    let step = r.paused(70, |i| {
        // Seconds 30 to 34 are the excursion: badly wrong, and the kernel says
        // so with a low confidence.
        if (30..35).contains(&i) {
            (185.0, true)
        } else {
            fall(i)
        }
    });
    // The harness gives every "trusted" second full confidence, so re-run the
    // excursion at the confidence the sensor actually reported.
    assert_eq!(step, Step::Completed);
    let m = r.det.take().expect("a measurement");
    assert_eq!(m.trusted_s, 61, "at full confidence every second counts");

    // Now the same window with the excursion at trust=1, under a floor of 2.
    let mut r2 = Ride::borrowing(cal, MAX_HR);
    r2.effort(600, 170.0);
    r2.det.cease(WindowKind::Pause);
    let mut step2 = Step::Nothing;
    for i in 0..70u32 {
        r2.utc += 1;
        let (bpm, trust) = if (30..35).contains(&i) { (185.0, 1) } else { (fall(i).0, 3) };
        step2 = r2.det.second(r2.utc, bpm, trust, r2.src, r2.active_s);
        if step2 != Step::Nothing {
            break;
        }
    }
    assert_eq!(step2, Step::Completed, "five untrusted of 61 is inside the 90% gate");
    let m2 = r2.det.take().expect("a measurement");
    assert_eq!(m2.trusted_s, 56, "the five low-confidence seconds did not count");
    assert_eq!(m2.curve[3], 0, "and the excursion left a hole rather than a reading");
    assert_eq!(m2.drop_bpm(), m.drop_bpm(), "the fall itself is unaffected either way");
}

#[test]
fn the_shipping_floor_believes_whatever_the_kernel_stands_behind() {
    // Unchanged behaviour, and deliberately so: the field results measured that
    // at a real intensity trust=1 is 9% of paused seconds against 24% at a
    // synthetic one, and the largest one-second move is 3 bpm against 15. The
    // gates only run above 80% of a real maximum, so the bad regime is one they
    // never see. The floor is a knob because that was measured, not turned.
    assert_eq!(TRUST_ANY.value, 1);
    assert_eq!(SPIN.min_trust().value, 1);
}
