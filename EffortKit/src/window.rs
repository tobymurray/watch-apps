//! The fall in heart rate across a window, measured only when it can mean
//! something.
//!
//! WHAT OPENS A WINDOW IS THE CALLER'S BUSINESS. This file is told that effort
//! ceased and told when it restarted; it does not know whether a button or a
//! segmenter said so. Spin's Service calls [`Detector::cease`] from a pause and
//! Squash's from [`crate::segment`], and neither needs the other's producer.
//!
//! WHY A PAUSE IS WHERE A CYCLING APP MEASURES. An activity Service's
//! `stopTrack()` ends by releasing the heart-rate sensor, so there is no heart
//! rate after a session ends and the textbook "measure for 60 s after
//! cessation" cannot be run there. A pause costs nothing: effort has stopped,
//! the sensor is still connected, the tick keeps running, and a ride stays
//! paused through the post-ride screens, so the end of a ride is a pause too.
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
//! MEASURED ELSEWHERE, and the reason the gates are where they are: Barak OF,
//! Ovcin ZB, Jakovljevic DG, Lozanov-Crvenkovic Z, Brodie DA, Grujic NG, "Heart
//! rate recovery after submaximal exercise in four different recovery protocols
//! in male athletes and non-athletes", J Sports Sci Med 2011;10(2):369-375,
//! Table 2, fitted the 5-minute recovery to a first-order exponential and got a
//! time constant of 52.5 (14.6) s seated inactive against 74.1 (24.0) s seated
//! *actively* pedalling in the same athletes -- 41% slower for moving rather
//! than sitting -- and 32.0 (9.1) s lying supine. Their exercise was 5 minutes
//! at 80% of individual peak heart rate.
//!
//! Falsified by any of those papers being misread; every number above is in the
//! cited table or abstract and can be checked against it.

use crate::hr::{clamp_bpm, pct_of, Bpm, HrSource, SourcePolicy, UNTRUSTED};
use crate::record::{Recovery, CURVE_POINTS, CURVE_STEP_S};
use crate::{Provenance, Unavailable};

/// Seconds of history the "was it already falling" test looks back over.
pub const PRE_WINDOW_S: usize = 30;

/// Trusted readings needed in that history.
///
/// MEASURED: 5.3% of seconds are untrusted across the pulled recordings, so
/// about 1.6 of 30 is normal and 10 missing is not.
pub const PRE_MIN_TRUSTED: usize = 20;

/// How long after cessation a trusted reading may still serve as the baseline.
///
/// Past this the measurement is discarded rather than started late: a baseline
/// two seconds down the curve already understates the fall.
pub const BASELINE_GRACE_S: i64 = 2;

/// How much past the stated window a trusted endpoint may still be taken.
///
/// [`Recovery::window_s`] records the second actually used, so a stretched
/// window is visible rather than rounded away.
pub const WINDOW_GRACE_S: i64 = 2;

/// What kind of cessation opened a window, and the wire code for it.
///
/// A reader needs this because the kinds are not comparable with each other: a
/// rest between rallies and the end of a ride are different measurements and
/// averaging them would be averaging two quantities.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum WindowKind {
    /// The wearer paused the session.
    Pause = 1,
    /// A lap was marked. A lap does not imply effort ceased, so an app should
    /// only produce this if it knows something this crate does not.
    Lap = 2,
    /// The session stopped. Reachable only where the sensor outlives the stop.
    Stop = 3,
    /// A rest between rallies, on court.
    BetweenRallies = 4,
    /// A whole game or longer spent off court.
    OffCourt = 5,
}

/// Every window kind, for iterating a calibration.
pub const ALL_KINDS: [WindowKind; 5] = [
    WindowKind::Pause,
    WindowKind::Lap,
    WindowKind::Stop,
    WindowKind::BetweenRallies,
    WindowKind::OffCourt,
];

impl WindowKind {
    /// The wire code for this kind.
    pub const fn code(self) -> u8 {
        self as u8
    }

    /// A kind from its wire code.
    pub const fn from_code(v: u8) -> Option<Self> {
        match v {
            1 => Some(WindowKind::Pause),
            2 => Some(WindowKind::Lap),
            3 => Some(WindowKind::Stop),
            4 => Some(WindowKind::BetweenRallies),
            5 => Some(WindowKind::OffCourt),
            _ => None,
        }
    }

    /// Name, for reports and for the log's own spelling.
    pub const fn name(self) -> &'static str {
        match self {
            WindowKind::Pause => "pause",
            WindowKind::Lap => "lap",
            WindowKind::Stop => "stop",
            WindowKind::BetweenRallies => "between_rallies",
            WindowKind::OffCourt => "off_court",
        }
    }

    const fn index(self) -> usize {
        self as usize - 1
    }
}

/// How the fall is expressed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Formulation {
    /// An absolute fall in bpm over a fixed number of seconds.
    FixedWindowDrop {
        /// Seconds from the baseline reading to the reading that ends the window.
        window_s: u16,
    },
    /// The signal's own smoothing time constant is comparable to the interval
    /// being measured across, so no fall measured over one is physiology.
    ///
    /// A calibration carrying this discards every window of that kind and
    /// counts them, rather than reporting a number that is the filter settling.
    ///
    /// MEASURED, and it does not yet apply to this watch: the claim that the
    /// kernel smooths the rate rests on consecutive readings differing by 0.50
    /// and 0.18 bpm, which are *mean* steps and not step sizes. Across all six
    /// pulled recordings the smallest non-zero step is exactly 1 bpm and 0 of
    /// 691 are smaller, so no sub-bpm smoothing is visible. What is still
    /// unmeasured is the step response: `phase-a` reports no labelled
    /// transition out of effort carried enough heart rate to measure settling
    /// across. Re-derive with `cargo run --bin phase-a -- Squash/Tests/pulled/*/imu_*.csv`.
    NotMeasurableOnThisHardware,
}

/// One tuned number, inseparable from where it came from.
///
/// A single [`Provenance`] over a whole calibration would be a lie: the
/// interval a measurement is defined over comes from a paper, while the
/// fraction of seconds that must be trusted comes from this repository's own
/// recordings. Wrapping each number individually keeps the type honest about
/// which is which.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Gate<T> {
    /// The number.
    pub value: T,
    /// Where it came from.
    pub provenance: Provenance,
}

impl<T> Gate<T> {
    /// A number a published protocol defines.
    pub const fn defined(value: T, citation: &'static str, defines: &'static str) -> Self {
        Self { value, provenance: Provenance::Defined { citation, defines } }
    }

    /// A number a recording in this repository set.
    pub const fn measured(
        value: T,
        recordings: &'static str,
        measured_on: &'static str,
        method: &'static str,
    ) -> Self {
        Self { value, provenance: Provenance::Measured { recordings, measured_on, method } }
    }
}

/// The numbers that govern one kind of window.
///
/// No `Default` and no public fields: every number arrives inside a [`Gate`]
/// carrying its own [`Provenance`], so no threshold can be separated from its
/// evidence by a refactor and no two numbers can share a justification that
/// covers only one of them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Thresholds {
    formulation: Formulation,
    formulation_provenance: Provenance,
    source_policy: SourcePolicy,
    min_trusted_pct: Gate<u32>,
    min_effort_s: Option<Gate<u32>>,
    min_hr0_pct_max: Option<Gate<u8>>,
    max_pre_fall_bpm: Option<Gate<u8>>,
}

impl Thresholds {
    /// Build a calibration for one window kind.
    ///
    /// The three optional gates are the ones that only mean something for a
    /// whole-session recovery: a rest between rallies has no preceding bout to
    /// be long enough, no intensity floor anybody has measured, and a heart
    /// rate that is legitimately already falling. Passing `None` says the gate
    /// does not apply rather than that it passed.
    ///
    /// `None` is returned for a `min_trusted_pct` above 100, which is a
    /// calibration no window could ever satisfy.
    pub const fn new(
        formulation: Formulation,
        formulation_provenance: Provenance,
        source_policy: SourcePolicy,
        min_trusted_pct: Gate<u32>,
        min_effort_s: Option<Gate<u32>>,
        min_hr0_pct_max: Option<Gate<u8>>,
        max_pre_fall_bpm: Option<Gate<u8>>,
    ) -> Option<Self> {
        if min_trusted_pct.value > 100 {
            return None;
        }
        Some(Self {
            formulation,
            formulation_provenance,
            source_policy,
            min_trusted_pct,
            min_effort_s,
            min_hr0_pct_max,
            max_pre_fall_bpm,
        })
    }

    /// Where the interval this measurement is defined over came from.
    pub const fn provenance(&self) -> Provenance {
        self.formulation_provenance
    }

    /// How the fall is expressed.
    pub const fn formulation(&self) -> Formulation {
        self.formulation
    }

    /// Seconds the window spans, or 0 when it is not measurable here.
    pub const fn window_s(&self) -> u16 {
        match self.formulation {
            Formulation::FixedWindowDrop { window_s } => window_s,
            Formulation::NotMeasurableOnThisHardware => 0,
        }
    }
}

/// HRR60 after a whole-session cessation, on the terms the literature defines
/// and this repository's recordings admit.
///
/// This is the one calibration that ships without a local recording, and the
/// reason is the [`Provenance`] split: the 60 seconds is what the quantity *is*
/// rather than a number tuned to this hardware, so refusing to report it until
/// a local recording "set" it would be a category error. Every gate below says
/// for itself whether a paper defines it or a recording here measured it.
///
/// A segmentation threshold is the opposite case and has no equivalent
/// constant: nobody can know what accelerometer magnitude means "rally"
/// without recording it, so [`crate::segment`] still ships uncalibrated.
pub const HRR60: Thresholds = Thresholds {
    formulation: Formulation::FixedWindowDrop { window_s: 60 },
    formulation_provenance: Provenance::Defined {
        citation: "Cole et al., N Engl J Med 1999;341:1351-1357",
        defines: "HRR60: the fall in bpm over the 60 s after effort ceases",
    },
    source_policy: SourcePolicy::EitherWithSourceRecorded,
    // 5.3% of seconds are untrusted across 34 minutes of pulled recordings, in
    // runs of median length 1 and maximum 4 -- so 90% allows six untrusted
    // seconds in 61 and no single dropout has ever been long enough to trip it.
    min_trusted_pct: Gate::measured(
        90,
        "Squash/Tests/pulled",
        "2026-09-03",
        "Tools/hr_analyse.py: untrusted fraction and run lengths",
    ),
    // "At least 3-4 min of exercise are generally required for HR to reach a
    // steady state during submaximal exercise." A statement about steady state
    // rather than recovery, so this is the weakest gate here; it exists to stop
    // the first minute of a session producing a measurement.
    min_effort_s: Some(Gate::defined(
        180,
        "Buchheit, Front Physiol 2014;5:73",
        "the exercise duration a submaximal HR plateau needs",
    )),
    // Barak et al.'s own protocol was 5 min at 80% of individual peak heart
    // rate, and their time constants are what this measurement is read against.
    // The 70% that circulates for this traces to fitness writing rather than to
    // a primary study, so it is not used.
    min_hr0_pct_max: Some(Gate::defined(
        80,
        "Barak et al., J Sports Sci Med 2011;10(2):369-375",
        "the exercise intensity the reference time constants were measured at",
    )),
    // At Barak et al.'s 52.5 s seated-inactive time constant a 70 bpm reserve
    // falls 30 bpm in the first 30 s, while steady effort moves by a fraction
    // of that: 0.49 bpm between consecutive readings across the pulled
    // recordings. 10 sits between the two with room on both sides.
    max_pre_fall_bpm: Some(Gate::measured(
        10,
        "Squash/Tests/pulled",
        "2026-09-03",
        "Tools/hr_analyse.py: consecutive-step distribution against Barak's tau",
    )),
};

/// The window kinds an app measures, and on what terms.
///
/// A kind with no thresholds is one this app does not measure; a cessation of
/// that kind is counted `not_calibrated` rather than silently ignored, so a
/// session that measured nothing can say why.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Calibration {
    kinds: [Option<Thresholds>; ALL_KINDS.len()],
}

impl Default for Calibration {
    fn default() -> Self {
        Self::absent()
    }
}

impl Calibration {
    /// Nothing is measured, and every cessation says so.
    pub const fn absent() -> Self {
        Self { kinds: [None; ALL_KINDS.len()] }
    }

    /// Measure one kind of window on the given terms.
    pub const fn with(mut self, kind: WindowKind, t: Thresholds) -> Self {
        self.kinds[kind.index()] = Some(t);
        self
    }

    /// The terms for one kind, if this app measures it.
    pub const fn for_kind(&self, kind: WindowKind) -> Option<Thresholds> {
        self.kinds[kind.index()]
    }

    /// True when no kind is measured at all.
    pub fn is_absent(&self) -> bool {
        self.kinds.iter().all(|k| k.is_none())
    }
}

/// Why windows produced nothing, counted by reason.
///
/// An output in its own right rather than a debug field. A diagnostic log
/// answers "what happened in this session, second by second" and is deleted;
/// this answers "why does this session carry no number" and is a permanent
/// property of the session. The desk check of 2026-09-03 is the argument: every
/// window in both runs was discarded `no_max_hr`, and the only record of that
/// was a text log the field test instructs you to delete.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Discarded {
    /// This app does not measure this kind of window.
    pub not_calibrated: u16,
    /// The calibration says this kind is not measurable on this hardware.
    pub not_measurable: u16,
    /// The watch has no maximum heart rate, so there is no intensity to record
    /// the measurement against.
    pub no_max_hr: u16,
    /// Not enough uninterrupted effort before the window opened.
    pub too_short: u16,
    /// Heart rate at cessation was below the intensity the measurement needs.
    pub too_easy: u16,
    /// Heart rate was already falling when the window opened.
    pub already_falling: u16,
    /// Too few trusted readings before the window to tell whether it was falling.
    pub no_baseline_history: u16,
    /// No trusted reading at the moment effort ceased.
    pub no_baseline: u16,
    /// Too many untrusted seconds inside the window.
    pub dropout: u16,
    /// No trusted reading at the end of the window.
    pub no_endpoint: u16,
    /// Effort restarted before the window closed.
    pub effort_resumed: u16,
    /// The session ended before the window closed.
    pub session_ended: u16,
    /// The kernel switched sensors part-way through.
    pub source_changed: u16,
    /// The source was not one the policy accepts.
    pub source_not_accepted: u16,
}

impl Discarded {
    /// No window discarded yet, usable in a `const`.
    pub const NONE: Self = Self {
        not_calibrated: 0,
        not_measurable: 0,
        no_max_hr: 0,
        too_short: 0,
        too_easy: 0,
        already_falling: 0,
        no_baseline_history: 0,
        no_baseline: 0,
        dropout: 0,
        no_endpoint: 0,
        effort_resumed: 0,
        session_ended: 0,
        source_changed: 0,
        source_not_accepted: 0,
    };

    /// Windows discarded for any reason.
    pub const fn total(&self) -> u16 {
        self.not_calibrated
            .saturating_add(self.not_measurable)
            .saturating_add(self.no_max_hr)
            .saturating_add(self.too_short)
            .saturating_add(self.too_easy)
            .saturating_add(self.already_falling)
            .saturating_add(self.no_baseline_history)
            .saturating_add(self.no_baseline)
            .saturating_add(self.dropout)
            .saturating_add(self.no_endpoint)
            .saturating_add(self.effort_resumed)
            .saturating_add(self.session_ended)
            .saturating_add(self.source_changed)
            .saturating_add(self.source_not_accepted)
    }

    /// True when nothing has been discarded.
    pub const fn is_none(&self) -> bool {
        self.total() == 0
    }

    fn bump(&mut self, r: Reason) {
        let slot = match r {
            Reason::NotCalibrated => &mut self.not_calibrated,
            Reason::NotMeasurable => &mut self.not_measurable,
            Reason::NoMaxHr => &mut self.no_max_hr,
            Reason::TooShort => &mut self.too_short,
            Reason::TooEasy => &mut self.too_easy,
            Reason::AlreadyFalling => &mut self.already_falling,
            Reason::NoBaselineHistory => &mut self.no_baseline_history,
            Reason::NoBaseline => &mut self.no_baseline,
            Reason::Dropout => &mut self.dropout,
            Reason::NoEndpoint => &mut self.no_endpoint,
            Reason::EffortResumed => &mut self.effort_resumed,
            Reason::SessionEnded => &mut self.session_ended,
            Reason::SourceChanged => &mut self.source_changed,
            Reason::SourceNotAccepted => &mut self.source_not_accepted,
        };
        *slot = slot.saturating_add(1);
    }
}

/// Why one window produced nothing.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Reason {
    /// This app does not measure this kind of window.
    NotCalibrated = 1,
    /// Not measurable on this hardware.
    NotMeasurable = 2,
    /// The watch has no maximum heart rate.
    NoMaxHr = 3,
    /// Not enough uninterrupted effort first.
    TooShort = 4,
    /// Below the intensity the measurement needs.
    TooEasy = 5,
    /// Already falling when the window opened.
    AlreadyFalling = 6,
    /// Too little trusted history to tell.
    NoBaselineHistory = 7,
    /// No trusted reading at cessation.
    NoBaseline = 8,
    /// Too many untrusted seconds inside.
    Dropout = 9,
    /// No trusted reading at the end.
    NoEndpoint = 10,
    /// Effort restarted first.
    EffortResumed = 11,
    /// The session ended first.
    SessionEnded = 12,
    /// The sensor changed part-way through.
    SourceChanged = 13,
    /// The source is not one the policy accepts.
    SourceNotAccepted = 14,
}

impl Reason {
    /// The wire code for this reason.
    pub const fn code(self) -> u8 {
        self as u8
    }

    /// Name, so a reason and its spelling cannot drift apart.
    pub const fn name(self) -> &'static str {
        match self {
            Reason::NotCalibrated => "not_calibrated",
            Reason::NotMeasurable => "not_measurable",
            Reason::NoMaxHr => "no_max_hr",
            Reason::TooShort => "too_short",
            Reason::TooEasy => "too_easy",
            Reason::AlreadyFalling => "already_falling",
            Reason::NoBaselineHistory => "no_baseline_history",
            Reason::NoBaseline => "no_baseline",
            Reason::Dropout => "dropout",
            Reason::NoEndpoint => "no_endpoint",
            Reason::EffortResumed => "effort_resumed",
            Reason::SessionEnded => "session_ended",
            Reason::SourceChanged => "source_changed",
            Reason::SourceNotAccepted => "source_not_accepted",
        }
    }
}

/// What one second did to the detector.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Step {
    /// Nothing to report.
    Nothing,
    /// A measurement is waiting in [`Detector::take`].
    Completed,
    /// A window ended with nothing; [`Detector::last_discard`] says why.
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
/// calls, for the reason `Spin/Software/Libs/Header/SecondsAccrual.hpp` exists:
/// a tick the Service was too busy to serve on time is a real second that went
/// past, and counting calls would quietly shorten every window it happened in.
pub struct Detector {
    calibration: Calibration,
    state: State,

    /// bpm for each of the last [`PRE_WINDOW_S`] seconds; 0 = not trusted.
    pre: [Bpm; PRE_WINDOW_S],
    pre_head: usize,
    pre_filled: usize,

    last_utc: i64,
    has_last_utc: bool,
    last_active_s: u32,
    /// Active seconds at the last resume, so effort is the current bout.
    bout_start_active_s: u32,

    max_hr: Bpm,

    arm_utc: i64,
    kind: WindowKind,
    /// The terms the open window is being measured on; set by `cease`.
    active: Option<Thresholds>,
    /// Highest trusted reading in the 30 s before cessation, snapped at cease.
    pre_peak: Bpm,

    base_utc: i64,
    base_active_s: u32,
    base_bpm: Bpm,
    /// The sensor the baseline came from; every later reading must match it.
    base_src: HrSource,
    trusted_s: u32,
    curve: [Bpm; CURVE_POINTS],

    result: Recovery,
    has_result: bool,
    discard: Option<Reason>,
    discarded: Discarded,
}

impl Detector {
    /// A detector that measures whatever `calibration` says it measures.
    pub const fn new(calibration: Calibration) -> Self {
        Detector {
            calibration,
            state: State::Idle,
            pre: [UNTRUSTED; PRE_WINDOW_S],
            pre_head: 0,
            pre_filled: 0,
            last_utc: 0,
            has_last_utc: false,
            last_active_s: 0,
            bout_start_active_s: 0,
            max_hr: 0,
            arm_utc: 0,
            kind: WindowKind::Pause,
            active: None,
            pre_peak: 0,
            base_utc: 0,
            base_active_s: 0,
            base_bpm: 0,
            base_src: HrSource::Unknown,
            trusted_s: 0,
            curve: [UNTRUSTED; CURVE_POINTS],
            result: Recovery::EMPTY,
            has_result: false,
            discard: None,
            discarded: Discarded::NONE,
        }
    }

    /// Begin a session. `max_hr` is the top of the watch's own threshold
    /// ladder, which is its maximum heart rate and not a zone floor; 0 when it
    /// has none.
    pub fn start(&mut self, max_hr: Bpm) {
        let calibration = self.calibration;
        *self = Detector::new(calibration);
        self.max_hr = max_hr;
    }

    /// Effort has ceased.
    pub fn cease(&mut self, kind: WindowKind) -> Step {
        if self.state != State::Idle || !self.has_last_utc {
            return Step::Nothing;
        }

        let Some(t) = self.calibration.for_kind(kind) else {
            return self.give_up(Reason::NotCalibrated);
        };
        if matches!(t.formulation, Formulation::NotMeasurableOnThisHardware) {
            return self.give_up(Reason::NotMeasurable);
        }
        if t.min_hr0_pct_max.is_some() && self.max_hr == 0 {
            return self.give_up(Reason::NoMaxHr);
        }
        if let Some(min) = t.min_effort_s.map(|g| g.value) {
            if self.last_active_s.saturating_sub(self.bout_start_active_s) < min {
                return self.give_up(Reason::TooShort);
            }
        }
        if t.max_pre_fall_bpm.is_some() {
            let (trusted, _) = self.pre_summary();
            if self.pre_filled < PRE_WINDOW_S || trusted < PRE_MIN_TRUSTED {
                return self.give_up(Reason::NoBaselineHistory);
            }
        }

        let (_, peak) = self.pre_summary();
        self.state = State::Arming;
        self.arm_utc = self.last_utc;
        self.kind = kind;
        self.active = Some(t);
        self.pre_peak = peak;
        self.discard = None;
        Step::Nothing
    }

    /// Effort has restarted, which ends any window in progress.
    pub fn resume(&mut self) -> Step {
        self.bout_start_active_s = self.last_active_s;
        if self.state == State::Idle {
            return Step::Nothing;
        }
        self.give_up(Reason::EffortResumed)
    }

    /// The session is over and the sensor is about to go.
    pub fn end(&mut self) -> Step {
        if self.state == State::Idle {
            return Step::Nothing;
        }
        self.give_up(Reason::SessionEnded)
    }

    /// One second of the session.
    ///
    /// `bpm` is the arbitrated reading and is ignored unless `trusted`;
    /// `source` is the sensor the kernel chose; `active_s` is the session's
    /// unpaused seconds so far.
    pub fn second(
        &mut self,
        utc: i64,
        bpm: f32,
        trusted: bool,
        source: HrSource,
        active_s: u32,
    ) -> Step {
        // A repeated or backward clock is a bug elsewhere, and folding it in
        // would shorten a window rather than leave the problem where it is.
        if self.has_last_utc && utc <= self.last_utc {
            return Step::Nothing;
        }

        let sample = if trusted { clamp_bpm(bpm) } else { UNTRUSTED };
        self.push_pre(utc, sample);
        self.last_utc = utc;
        self.has_last_utc = true;
        self.last_active_s = active_s;

        match self.state {
            State::Idle => Step::Nothing,
            State::Arming => self.arming_second(utc, sample, source, active_s),
            State::Open => self.open_second(utc, sample, source),
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

    /// Why the last window produced nothing.
    pub const fn last_discard(&self) -> Option<Reason> {
        self.discard
    }

    /// Every window this session discarded, by reason.
    pub const fn discarded(&self) -> Discarded {
        self.discarded
    }

    /// The terms this detector measures on, or why it measures nothing.
    pub fn calibration(&self) -> Result<Calibration, Unavailable> {
        if self.calibration.is_absent() {
            return Err(Unavailable::NotCalibrated);
        }
        Ok(self.calibration)
    }

    // -- Internals ------------------------------------------------------------

    fn arming_second(&mut self, utc: i64, sample: Bpm, source: HrSource, active_s: u32) -> Step {
        let Some(t) = self.active else {
            return Step::Nothing;
        };
        let lag = utc - self.arm_utc;
        if sample == UNTRUSTED {
            if lag >= BASELINE_GRACE_S {
                return self.give_up(Reason::NoBaseline);
            }
            return Step::Nothing;
        }
        // A tick the Service was too busy to serve can land the first trusted
        // reading well down the curve, where it is no longer the baseline.
        if lag > BASELINE_GRACE_S {
            return self.give_up(Reason::NoBaseline);
        }
        if !t.source_policy.accepts(source) {
            return self.give_up(Reason::SourceNotAccepted);
        }
        if let Some(pct) = t.min_hr0_pct_max.map(|g| g.value) {
            if (sample as u32) * 100 < (pct as u32) * (self.max_hr as u32) {
                return self.give_up(Reason::TooEasy);
            }
        }
        // MEASURED, off the two curves of Spin's Ride A on 2026-09-03: with
        // both gates configured, the band where this one fires rather than
        // TooEasy is only (pre_peak - max_pre_fall_bpm) down to
        // min_hr0_pct_max * max_hr. At that ride's numbers -- peak 161, a
        // 10 bpm fall and 80% of a 184 maximum -- the band is 151 to 147.2,
        // 3.8 bpm wide, about four seconds of a real recovery curve. A late
        // press lands in TooEasy far more often than here. Re-measure by
        // walking the `curve` of any recorded window.
        if let Some(fall) = t.max_pre_fall_bpm.map(|g| g.value) {
            if self.pre_peak.saturating_sub(sample) > fall {
                return self.give_up(Reason::AlreadyFalling);
            }
        }

        self.state = State::Open;
        self.base_utc = utc;
        self.base_active_s = active_s;
        self.base_bpm = sample;
        self.base_src = source;
        self.trusted_s = 1;
        self.curve = [UNTRUSTED; CURVE_POINTS];
        self.curve[0] = sample;
        Step::Nothing
    }

    fn open_second(&mut self, utc: i64, sample: Bpm, source: HrSource) -> Step {
        let Some(t) = self.active else {
            return Step::Nothing;
        };
        let window_s = t.window_s() as i64;
        let dt = utc - self.base_utc;
        if dt > window_s + WINDOW_GRACE_S {
            return self.give_up(Reason::NoEndpoint);
        }
        if sample == UNTRUSTED {
            return Step::Nothing;
        }
        // MEASURED: 14% of 60 s windows in the pulled recordings begin and end
        // on different sensors, and where both report at once the two disagree
        // by a median of 2 bpm and a 95th percentile of 16 -- against falls of
        // 8 to 20 bpm. Those windows averaged -2.1 bpm, an apparent rise. A
        // window spanning a switch measures the gap between two instruments as
        // well as the fall, so it is not a measurement of the fall.
        if source != self.base_src {
            return self.give_up(Reason::SourceChanged);
        }

        self.trusted_s += 1;

        // At or just past a curve mark, first trusted reading wins, so the
        // curve is samples that were taken rather than points interpolated
        // between them.
        let slot = (dt / CURVE_STEP_S as i64) as usize;
        if dt % CURVE_STEP_S as i64 <= 2 && slot < CURVE_POINTS && self.curve[slot] == UNTRUSTED {
            self.curve[slot] = sample;
        }

        if dt < window_s {
            return Step::Nothing;
        }

        let seconds = (dt + 1) as u32;
        if self.trusted_s * 100 < t.min_trusted_pct.value * seconds {
            return self.give_up(Reason::Dropout);
        }

        self.result = Recovery {
            at_active_s: self.base_active_s,
            hr0: self.base_bpm,
            hr_end: sample,
            window_s: dt.min(255) as u8,
            trusted_s: self.trusted_s.min(255) as u8,
            hr0_pct_max: pct_of(self.base_bpm, self.max_hr),
            kind: self.kind.code(),
            curve: self.curve,
            source: self.base_src.code(),
            reserved: [0; 2],
        };
        self.has_result = true;
        self.state = State::Idle;
        self.discard = None;
        Step::Completed
    }

    fn give_up(&mut self, reason: Reason) -> Step {
        self.state = State::Idle;
        self.discard = Some(reason);
        self.discarded.bump(reason);
        Step::Discarded
    }

    fn push_pre(&mut self, utc: i64, sample: Bpm) {
        if self.has_last_utc {
            // Seconds the Service never served are seconds with no reading, not
            // seconds that did not happen.
            let gap = (utc - self.last_utc).min(PRE_WINDOW_S as i64) - 1;
            for _ in 0..gap {
                self.push_one(UNTRUSTED);
            }
        }
        self.push_one(sample);
    }

    fn push_one(&mut self, sample: Bpm) {
        self.pre[self.pre_head] = sample;
        self.pre_head = (self.pre_head + 1) % PRE_WINDOW_S;
        if self.pre_filled < PRE_WINDOW_S {
            self.pre_filled += 1;
        }
    }

    /// Trusted readings in the ring, and the highest of them.
    fn pre_summary(&self) -> (usize, Bpm) {
        let mut count = 0;
        let mut peak = 0;
        for i in 0..self.pre_filled {
            let v = self.pre[i];
            if v != UNTRUSTED {
                count += 1;
                if v > peak {
                    peak = v;
                }
            }
        }
        (count, peak)
    }
}
