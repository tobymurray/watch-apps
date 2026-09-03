//! What the detector will and will not measure.
//!
//! Every test here pins one gate. Reverting the gate it names must fail it;
//! that was checked by reverting each one in turn.

use trainkit::record::*;
use trainkit::recovery::*;

/// A wearer with a 190 bpm maximum, so 80% is 152.
const MAX_HR: u8 = 190;

/// A rider on the bike: `hold` seconds of steady effort at `bpm`, ending at
/// `utc`.
struct Ride {
    det: Detector,
    utc: i64,
    active_s: u32,
    /// The sensor every second claims to come from, unless a test changes it.
    src: u8,
}

impl Ride {
    fn new(max_hr: u8) -> Self {
        let mut det = Detector::new();
        det.start(max_hr);
        Ride {
            det,
            utc: 1_700_000_000,
            active_s: 0,
            src: HR_SOURCE_EXTERNAL,
        }
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
            last = self
                .det
                .second(self.utc, bpm, trusted, self.src, self.active_s);
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
            last = self
                .det
                .second(self.utc, bpm, trusted, self.src, self.active_s);
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
    assert_eq!(r.det.cease(TRIGGER_PAUSE), Step::Nothing);
    assert_eq!(r.paused(70, decay(170.0, 90.0, 55.0)), Step::Completed);

    let m = r.det.take().expect("a measurement");
    assert_eq!(m.trigger, TRIGGER_PAUSE);
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
    // The curve falls all the way along, which is what a recovery looks like.
    for w in m.curve.windows(2) {
        assert!(w[0] > w[1], "curve is not monotonic: {:?}", m.curve);
    }
}

#[test]
fn a_second_pause_in_the_same_ride_is_measured_too() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    assert_eq!(r.paused(70, decay(170.0, 90.0, 55.0)), Step::Completed);
    r.det.take().unwrap();

    assert_eq!(r.det.resume(), Step::Nothing);
    r.effort(600, 172.0);
    r.det.cease(TRIGGER_PAUSE);
    assert_eq!(r.paused(70, decay(172.0, 90.0, 50.0)), Step::Completed);
    assert!(r.det.take().is_some());
}

#[test]
fn a_watch_with_no_maximum_measures_nothing() {
    let mut r = Ride::new(0);
    r.effort(600, 170.0);
    assert_eq!(r.det.cease(TRIGGER_PAUSE), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_NO_MAX_HR);
    assert!(r.det.take().is_none());
}

#[test]
fn the_first_minute_of_a_ride_measures_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(MIN_EFFORT_S - 1, 170.0);
    assert_eq!(r.det.cease(TRIGGER_PAUSE), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_TOO_SHORT);
}

#[test]
fn effort_is_the_current_bout_not_the_whole_ride() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    r.paused(5, |_| (160.0, true));
    r.det.resume();

    // Ten minutes are behind us, but only ten seconds of it since resuming.
    r.effort(10, 170.0);
    assert_eq!(r.det.cease(TRIGGER_PAUSE), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_TOO_SHORT);
}

#[test]
fn an_easy_pause_measures_nothing() {
    let mut r = Ride::new(MAX_HR);
    // 80% of 190 is 152; 140 is a shoelace, not an effort.
    r.effort(600, 140.0);
    assert_eq!(r.det.cease(TRIGGER_PAUSE), Step::Nothing);
    assert_eq!(r.paused(3, |_| (140.0, true)), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_TOO_EASY);
}

#[test]
fn exactly_the_intensity_floor_is_measured() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 152.0); // 80.0% of 190
    r.det.cease(TRIGGER_PAUSE);
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
    r.det.cease(TRIGGER_PAUSE);
    assert_eq!(r.paused(3, |_| (154.0, true)), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_ALREADY_FALLING);
}

#[test]
fn ordinary_drift_is_not_a_fall() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    // Spin measured 0.50 bpm between consecutive readings; this is ten times
    // that and must still be effort rather than recovery.
    r.run(30, |i| (170.0 - (i as f32) * 0.2, true));
    r.det.cease(TRIGGER_PAUSE);
    assert_eq!(r.paused(70, decay(164.0, 90.0, 55.0)), Step::Completed);
}

#[test]
fn a_dropout_in_the_window_is_discarded_not_interpolated() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    let fall = decay(170.0, 90.0, 55.0);
    // Eight untrusted seconds out of 61 is past the 90% the window needs.
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, !(20..28).contains(&i))
    });
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_DROPOUT);
    assert!(r.det.take().is_none());
}

#[test]
fn a_dropout_the_size_spin_measured_is_survivable() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    let fall = decay(170.0, 90.0, 55.0);
    // Two seconds untrusted, which is what a real ride does several times a
    // minute; see HrHold.hpp.
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
    r.det.cease(TRIGGER_PAUSE);
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
    r.det.cease(TRIGGER_PAUSE);
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
    r.det.cease(TRIGGER_PAUSE);
    let fall = decay(170.0, 90.0, 55.0);
    let step = r.paused(70, |i| {
        let (bpm, _) = fall(i);
        (bpm, i < 59)
    });
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_NO_ENDPOINT);
}

#[test]
fn resuming_the_ride_ends_the_window() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    r.paused(30, decay(170.0, 90.0, 55.0));
    assert_eq!(r.det.resume(), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_EFFORT_RESUMED);
    assert!(r.det.take().is_none());
}

#[test]
fn ending_the_ride_ends_the_window() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    r.paused(30, decay(170.0, 90.0, 55.0));
    assert_eq!(r.det.end(), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_RIDE_ENDED);
}

#[test]
fn no_trusted_baseline_measures_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    assert_eq!(r.paused(5, |_| (170.0, false)), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_NO_BASELINE);
}

#[test]
fn a_ride_of_untrusted_seconds_has_no_history_to_judge_by() {
    let mut r = Ride::new(MAX_HR);
    // Trusted enough to be a ride, but the last 30 seconds are mostly gone.
    r.effort(600, 170.0);
    r.run(30, |i| (170.0, i < 5));
    assert_eq!(r.det.cease(TRIGGER_PAUSE), Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_NO_BASELINE_HISTORY);
}

#[test]
fn a_stalled_tick_is_seconds_that_passed_not_seconds_that_did_not() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);

    // The Service misses the baseline second entirely and comes back four
    // seconds later, well down the curve.
    r.utc += 4;
    let step = r.det.second(r.utc, 150.0, true, r.src, r.active_s);
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_NO_BASELINE);
}

#[test]
fn a_stall_inside_the_window_costs_the_seconds_it_took() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    r.paused(20, decay(170.0, 90.0, 55.0));

    // Ten seconds the Service never served: they are untrusted seconds, so the
    // window falls under the 90% it needs rather than closing early.
    r.utc += 10;
    let mut step = r.det.second(r.utc, 130.0, true, r.src, r.active_s);
    let mut guard = 0;
    while step == Step::Nothing && guard < 60 {
        r.utc += 1;
        guard += 1;
        step = r.det.second(r.utc, 125.0, true, r.src, r.active_s);
    }
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_DROPOUT);
}

#[test]
fn a_clock_that_repeats_a_second_changes_nothing() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    r.paused(30, decay(170.0, 90.0, 55.0));

    let before = r.det.last_discard();
    // The same second again, and one before it.
    assert_eq!(
        r.det.second(r.utc, 140.0, true, r.src, r.active_s),
        Step::Nothing
    );
    assert_eq!(
        r.det.second(r.utc - 5, 140.0, true, r.src, r.active_s),
        Step::Nothing
    );
    assert_eq!(r.det.last_discard(), before);

    // And the window still closes on time.
    assert_eq!(r.paused(40, decay(140.0, 90.0, 55.0)), Step::Completed);
    assert_eq!(r.det.take().unwrap().window_s, 60);
}

#[test]
fn a_window_that_changes_sensor_is_discarded() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    let fall = decay(170.0, 90.0, 55.0);

    // Twenty seconds in, the kernel prefers the wrist. Measured over 34 minutes
    // of pulled recordings: this happens to 14% of 60 s windows, and the two
    // sensors disagree by a 95th-percentile 16 bpm -- the size of the whole
    // measurement.
    let mut step = Step::Nothing;
    for i in 0..70 {
        r.utc += 1;
        r.src = if i < 20 {
            HR_SOURCE_EXTERNAL
        } else {
            HR_SOURCE_OPTICAL
        };
        let (bpm, _) = fall(i);
        step = r.det.second(r.utc, bpm, true, r.src, r.active_s);
        if step != Step::Nothing {
            break;
        }
    }
    assert_eq!(step, Step::Discarded);
    assert_eq!(r.det.last_discard(), DISCARD_SOURCE_CHANGED);
    assert!(r.det.take().is_none());
}

#[test]
fn a_measurement_names_the_sensor_it_came_from() {
    let mut r = Ride::new(MAX_HR);
    r.src = HR_SOURCE_OPTICAL;
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    assert_eq!(r.paused(70, decay(170.0, 90.0, 55.0)), Step::Completed);
    assert_eq!(r.det.take().unwrap().source, HR_SOURCE_OPTICAL);
}

#[test]
fn an_untrusted_second_does_not_count_as_a_sensor_change() {
    // An untrusted reading carries no source worth believing, so it must not
    // end a window that is otherwise on one sensor throughout.
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    let fall = decay(170.0, 90.0, 55.0);

    let mut step = Step::Nothing;
    for i in 0..70 {
        r.utc += 1;
        let untrusted = (20..22).contains(&i);
        r.src = if untrusted {
            HR_SOURCE_NONE
        } else {
            HR_SOURCE_EXTERNAL
        };
        let (bpm, _) = fall(i);
        step = r.det.second(r.utc, bpm, !untrusted, r.src, r.active_s);
        if step != Step::Nothing {
            break;
        }
    }
    assert_eq!(step, Step::Completed);
    assert_eq!(r.det.take().unwrap().source, HR_SOURCE_EXTERNAL);
}

#[test]
fn a_lap_is_not_a_cessation() {
    // Nothing in the Service calls cease() for a lap, and the reason is that a
    // lap does not mean the wearer stopped: the trigger exists so a reader can
    // tell them apart if one ever does.
    assert_ne!(TRIGGER_LAP, TRIGGER_PAUSE);
    assert_ne!(TRIGGER_STOP, TRIGGER_PAUSE);
}

#[test]
fn a_measurement_is_handed_out_once() {
    let mut r = Ride::new(MAX_HR);
    r.effort(600, 170.0);
    r.det.cease(TRIGGER_PAUSE);
    r.paused(70, decay(170.0, 90.0, 55.0));
    assert!(r.det.take().is_some());
    assert!(r.det.take().is_none());
}
