//! Heart-rate recovery: the fall in heart rate over the 60 s after effort
//! ceases, measured only when it can mean something.
//!
//! WHERE THE WINDOW COMES FROM. The sensor is released inside the Service's
//! `stopTrack()`, so there is no heart rate after a ride ends and the textbook
//! "measure for 60 s after cessation" cannot be run there. A **pause** is the
//! one moment that costs nothing: effort has stopped, the sensor is still
//! connected, the tick keeps running, and on the app this was written for the
//! ride stays paused through the post-ride kilojoule screen, so the end of a
//! ride is a pause too. A lap is not a cessation -- nothing tells this code the
//! wearer stopped pedalling at one -- so `TRIGGER_LAP` exists and is never
//! produced.
//!
//! SOURCES, all verified rather than remembered. The 60 s interval and the
//! ">= 12 bpm" reading of it are Cole CR, Blackstone EH, Pashkow FJ, Snader CE,
//! Lauer MS, "Heart-rate recovery immediately after exercise as a predictor of
//! mortality", N Engl J Med 1999;341:1351-1357, whose protocol was a
//! symptom-limited Bruce treadmill test followed by a 2-minute *walking*
//! cool-down: that threshold does not transfer to a wearer sitting still on a
//! bike, and nothing here reports it. Shetler K, Marcus R, Froelicher VF,
//! Vora S, Kalisetti D, Prakash M, Do D, Myers J, "Heart rate recovery:
//! validation and methodologic issues", J Am Coll Cardiol 2001;38(7):1980-1987
//! found 2-minute recovery more prognostic than 1-minute (< 22 bpm, hazard
//! ratio 2.6), on a different cohort again -- which is the point: the number is
//! meaningless detached from its protocol, so this file records the protocol it
//! used beside every number it produces.
//!
//! MEASURED ELSEWHERE, and the reason the gates below are where they are:
//! Barak OF, Ovcin ZB, Jakovljevic DG, Lozanov-Crvenkovic Z, Brodie DA, Grujic
//! NG, "Heart rate recovery after submaximal exercise in four different
//! recovery protocols in male athletes and non-athletes", J Sports Sci Med
//! 2011;10(2):369-375, Table 2, fitted the 5-minute recovery to a first-order
//! exponential and got a time constant of 52.5 (14.6) s seated inactive against
//! 74.1 (24.0) s seated *actively* pedalling in the same athletes -- 41%
//! slower for moving rather than sitting -- and 32.0 (9.1) s lying supine. Their
//! exercise was 5 minutes at 80% of individual peak heart rate.
//!
//! Falsified by any of those papers being misread; every number above is in the
//! cited table or abstract and can be checked against it.

use crate::record::*;

/// The interval the measurement is defined over.
pub const WINDOW_S: i64 = 60;

/// A trusted reading is taken at the first second at or past `WINDOW_S`, and
/// this is how much further it may look. `Recovery::window_s` records which
/// second was actually used, so a stretched window is visible rather than
/// rounded away.
pub const WINDOW_GRACE_S: i64 = 2;

/// How long after cessation a trusted reading may still serve as the baseline.
/// Past this the measurement is discarded rather than started late: a baseline
/// two seconds down the curve already understates the fall.
pub const BASELINE_GRACE_S: i64 = 2;

/// Uninterrupted effort required before a window can open.
///
/// Buchheit M, "Monitoring training status with HR measures: do all roads lead
/// to Rome?", Front Physiol 2014;5:73 gives "at least 3-4 min of exercise are
/// generally required for HR to reach a steady state during submaximal
/// exercise". That is a statement about steady state and not about recovery,
/// so this is the weakest of the gates here; it exists to stop the first minute
/// of a ride producing a measurement.
pub const MIN_EFFORT_S: u32 = 180;

/// Heart rate at cessation, as a percentage of the watch's maximum.
///
/// Matched to Barak et al.'s protocol -- 80% of peak heart rate -- because
/// their time constants are the numbers this feature is calibrated against. The
/// 70% figure that circulates for this threshold traces to fitness writing
/// rather than to a primary study, so it is not used.
pub const MIN_HR0_PCT_MAX: u32 = 80;

/// Seconds of history the "was it already falling" test looks back over.
pub const PRE_WINDOW_S: usize = 30;

/// Trusted readings needed in that history.
///
/// Spin measured 5% of seconds untrusted over two real rides (`HrHold.hpp`), so
/// about 1.5 of 30 is normal and 10 missing is not.
pub const PRE_MIN_TRUSTED: usize = 20;

/// How far below the preceding 30 seconds' peak the baseline may sit.
///
/// Recovery already under way is not subtle: at Barak et al.'s seated-inactive
/// time constant of 52.5 s, a 70 bpm reserve falls 30 bpm in the first 30 s.
/// Steady effort moves by a fraction of that -- Spin measured 0.50 and 0.18 bpm
/// between consecutive readings over two rides. 10 bpm sits between the two
/// with room on both sides; re-measure by differencing the 30 s before each
/// pause in a ride log.
pub const PRE_MAX_FALL_BPM: u8 = 10;

/// Percentage of the window's seconds that must carry a trusted reading.
///
/// At Spin's measured 5% untrusted this rejects roughly one window in a
/// hundred, and it rejects any window with a real gap in it. A dropout is a
/// discarded measurement, never an interpolated one.
pub const MIN_TRUSTED_PCT: u32 = 90;

/// What one second did to the detector.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Step {
    /// Nothing to report.
    Nothing,
    /// A measurement is waiting in `take()`.
    Completed,
    /// A window ended with nothing; `last_discard()` says why.
    Discarded,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum State {
    /// No window; the pre-history ring is still being kept.
    Idle,
    /// Effort has ceased and a trusted baseline is wanted.
    Arming,
    /// The window is running.
    Open,
}

/// Fed one second at a time; hands back a measurement or a reason there is not
/// one.
///
/// Everything is keyed on the UTC second passed in rather than on the number of
/// calls, for the reason `SecondsAccrual.hpp` exists: a tick the Service was
/// too busy to serve on time is a real second that went past, and counting
/// calls would quietly shorten every window it happened in.
pub struct Detector {
    state: State,

    /// bpm for each of the last `PRE_WINDOW_S` seconds; 0 = not trusted.
    pre: [u8; PRE_WINDOW_S],
    pre_head: usize,
    pre_filled: usize,

    last_utc: i64,
    has_last_utc: bool,
    last_active_s: u32,
    /// Active seconds at the last resume, so effort is the current bout.
    bout_start_active_s: u32,

    max_hr: u8,

    arm_utc: i64,
    trigger: u8,
    /// Highest trusted reading in the 30 s before cessation, snapped at cease.
    pre_peak: u8,

    base_utc: i64,
    base_active_s: u32,
    base_bpm: u8,
    trusted_s: u32,
    curve: [u8; CURVE_POINTS],

    result: Recovery,
    has_result: bool,
    discard: u8,
}

impl Default for Detector {
    fn default() -> Self {
        Detector::new()
    }
}

impl Detector {
    pub const fn new() -> Self {
        Detector {
            state: State::Idle,
            pre: [0; PRE_WINDOW_S],
            pre_head: 0,
            pre_filled: 0,
            last_utc: 0,
            has_last_utc: false,
            last_active_s: 0,
            bout_start_active_s: 0,
            max_hr: 0,
            arm_utc: 0,
            trigger: 0,
            pre_peak: 0,
            base_utc: 0,
            base_active_s: 0,
            base_bpm: 0,
            trusted_s: 0,
            curve: [0; CURVE_POINTS],
            result: Recovery {
                at_active_s: 0,
                hr0: 0,
                hr_end: 0,
                window_s: 0,
                trusted_s: 0,
                hr0_pct_max: 0,
                trigger: 0,
                curve: [0; CURVE_POINTS],
                reserved: [0; 3],
            },
            has_result: false,
            discard: DISCARD_NONE,
        }
    }

    /// Begin a ride. `max_hr` is the top of the watch's own threshold ladder,
    /// 0 when it has none.
    pub fn start(&mut self, max_hr: u8) {
        *self = Detector::new();
        self.max_hr = max_hr;
    }

    /// Effort has ceased.
    pub fn cease(&mut self, trigger: u8) -> Step {
        if self.state != State::Idle || !self.has_last_utc {
            return Step::Nothing;
        }

        if self.max_hr == 0 {
            return self.give_up(DISCARD_NO_MAX_HR);
        }
        if self.last_active_s.saturating_sub(self.bout_start_active_s) < MIN_EFFORT_S {
            return self.give_up(DISCARD_TOO_SHORT);
        }

        let (trusted, peak) = self.pre_summary();
        if self.pre_filled < PRE_WINDOW_S || trusted < PRE_MIN_TRUSTED {
            return self.give_up(DISCARD_NO_BASELINE_HISTORY);
        }

        self.state = State::Arming;
        self.arm_utc = self.last_utc;
        self.trigger = trigger;
        self.pre_peak = peak;
        self.discard = DISCARD_NONE;
        Step::Nothing
    }

    /// Effort has restarted, which ends any window in progress.
    pub fn resume(&mut self) -> Step {
        self.bout_start_active_s = self.last_active_s;
        if self.state == State::Idle {
            return Step::Nothing;
        }
        self.give_up(DISCARD_EFFORT_RESUMED)
    }

    /// The ride is over and the sensor is about to go.
    pub fn end(&mut self) -> Step {
        if self.state == State::Idle {
            return Step::Nothing;
        }
        self.give_up(DISCARD_RIDE_ENDED)
    }

    /// One second of the ride.
    ///
    /// @param bpm      the arbitrated reading; ignored unless `trusted`.
    /// @param active_s the ride's unpaused seconds so far.
    pub fn second(&mut self, utc: i64, bpm: f32, trusted: bool, active_s: u32) -> Step {
        // A repeated or backward clock is a bug elsewhere, and folding it in
        // would shorten a window rather than leave the problem where it is.
        if self.has_last_utc && utc <= self.last_utc {
            return Step::Nothing;
        }

        let sample = if trusted { clamp_bpm(bpm) } else { 0 };
        self.push_pre(utc, sample);
        self.last_utc = utc;
        self.has_last_utc = true;
        self.last_active_s = active_s;

        match self.state {
            State::Idle => Step::Nothing,
            State::Arming => self.arming_second(utc, sample, active_s),
            State::Open => self.open_second(utc, sample),
        }
    }

    /// The measurement, once, if there is one.
    pub fn take(&mut self) -> Option<Recovery> {
        if self.has_result {
            self.has_result = false;
            Some(self.result)
        } else {
            None
        }
    }

    /// Why the last window produced nothing; one of the `DISCARD_*` values.
    pub fn last_discard(&self) -> u8 {
        self.discard
    }

    // -- Internals ------------------------------------------------------------

    fn arming_second(&mut self, utc: i64, sample: u8, active_s: u32) -> Step {
        let lag = utc - self.arm_utc;
        if sample == 0 {
            if lag >= BASELINE_GRACE_S {
                return self.give_up(DISCARD_NO_BASELINE);
            }
            return Step::Nothing;
        }
        // A tick the Service was too busy to serve can land the first trusted
        // reading well down the curve, where it is no longer the baseline.
        if lag > BASELINE_GRACE_S {
            return self.give_up(DISCARD_NO_BASELINE);
        }

        if (sample as u32) * 100 < MIN_HR0_PCT_MAX * (self.max_hr as u32) {
            return self.give_up(DISCARD_TOO_EASY);
        }
        if self.pre_peak.saturating_sub(sample) > PRE_MAX_FALL_BPM {
            return self.give_up(DISCARD_ALREADY_FALLING);
        }

        self.state = State::Open;
        self.base_utc = utc;
        self.base_active_s = active_s;
        self.base_bpm = sample;
        self.trusted_s = 1;
        self.curve = [0; CURVE_POINTS];
        self.curve[0] = sample;
        Step::Nothing
    }

    fn open_second(&mut self, utc: i64, sample: u8) -> Step {
        let dt = utc - self.base_utc;
        if dt > WINDOW_S + WINDOW_GRACE_S {
            return self.give_up(DISCARD_NO_ENDPOINT);
        }
        if sample == 0 {
            return Step::Nothing;
        }

        self.trusted_s += 1;

        // At or just past a ten-second mark, first trusted reading wins, so the
        // curve is samples that were taken rather than points interpolated
        // between them.
        let slot = (dt / 10) as usize;
        if dt % 10 <= 2 && slot < CURVE_POINTS && self.curve[slot] == 0 {
            self.curve[slot] = sample;
        }

        if dt < WINDOW_S {
            return Step::Nothing;
        }

        let seconds = (dt + 1) as u32;
        if self.trusted_s * 100 < MIN_TRUSTED_PCT * seconds {
            return self.give_up(DISCARD_DROPOUT);
        }

        self.result = Recovery {
            at_active_s: self.base_active_s,
            hr0: self.base_bpm,
            hr_end: sample,
            window_s: dt as u8,
            trusted_s: self.trusted_s.min(255) as u8,
            hr0_pct_max: pct_of(self.base_bpm, self.max_hr),
            trigger: self.trigger,
            curve: self.curve,
            reserved: [0; 3],
        };
        self.has_result = true;
        self.state = State::Idle;
        self.discard = DISCARD_NONE;
        Step::Completed
    }

    fn give_up(&mut self, reason: u8) -> Step {
        self.state = State::Idle;
        self.discard = reason;
        Step::Discarded
    }

    fn push_pre(&mut self, utc: i64, sample: u8) {
        if self.has_last_utc {
            // Seconds the Service never served are seconds with no reading, not
            // seconds that did not happen.
            let gap = (utc - self.last_utc).min(PRE_WINDOW_S as i64) - 1;
            for _ in 0..gap {
                self.push_one(0);
            }
        }
        self.push_one(sample);
    }

    fn push_one(&mut self, sample: u8) {
        self.pre[self.pre_head] = sample;
        self.pre_head = (self.pre_head + 1) % PRE_WINDOW_S;
        if self.pre_filled < PRE_WINDOW_S {
            self.pre_filled += 1;
        }
    }

    /// Trusted readings in the ring, and the highest of them.
    fn pre_summary(&self) -> (usize, u8) {
        let mut count = 0;
        let mut peak = 0u8;
        for i in 0..self.pre_filled {
            let v = self.pre[i];
            if v != 0 {
                count += 1;
                if v > peak {
                    peak = v;
                }
            }
        }
        (count, peak)
    }
}

fn clamp_bpm(bpm: f32) -> u8 {
    // NaN falls through both comparisons and lands on 0, which reads as "no
    // trusted reading" everywhere else in this file.
    if bpm >= 254.5 {
        255
    } else if bpm >= 0.5 {
        (bpm + 0.5) as u8
    } else {
        0
    }
}

fn pct_of(value: u8, of: u8) -> u8 {
    if of == 0 {
        return 0;
    }
    let pct = ((value as u32) * 100 + (of as u32) / 2) / (of as u32);
    pct.min(255) as u8
}
