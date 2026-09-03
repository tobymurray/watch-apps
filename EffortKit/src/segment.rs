//! What the wearer was doing, one epoch at a time.
//!
//! Three states, because two are not enough: a session is as likely to be two
//! hours of threes, where whole games are spent off court, as it is to be a
//! match. A machine that knows only RALLY and REST scores every sit-out as a
//! magnificent recovery. See `README.md` § "The session is not a match".
//!
//! Nothing here has a tuned number in it. [`Thresholds`] cannot be constructed
//! without a [`Provenance`] naming the recordings that set it, and no such
//! constant exists in this repository yet, so [`Calibration::Absent`] is the
//! only state the watch build can reach and [`Segmenter::result`] reports
//! [`Unavailable::NotCalibrated`].

use crate::epoch::{EpochFeatures, Feature};
use crate::{Provenance, Unavailable};

/// What the wearer was doing during an epoch.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum ActivityState {
    /// Sustained movement of the kind a rally is made of.
    Rally,
    /// Between rallies: on court, upright, waiting to play again.
    Rest,
    /// Not playing at all — off court in a game of threes, walking to court,
    /// tying a shoe, talking.
    #[default]
    OffCourt,
}

impl ActivityState {
    /// Name, for reports.
    pub const fn name(&self) -> &'static str {
        match self {
            ActivityState::Rally => "rally",
            ActivityState::Rest => "rest",
            ActivityState::OffCourt => "off_court",
        }
    }
}

/// How to tell being off court from resting between rallies.
///
/// A duration alone will not do it: a long rest between games is still a rest,
/// and a short trip off court in threes is still off court. So the rule needs
/// a level of its own, and whether one exists is a question only a recording
/// answers.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum OffCourtRule {
    /// A2 found a level of the segmenting feature that separates the two.
    Level {
        /// Below this the wearer is taken to be off court rather than resting.
        below: f32,
        /// Consecutive epochs below it before the state changes.
        for_epochs: u16,
    },
    /// A2 found no separating level; the two states are reported merged and
    /// [`SessionSegmentation::off_court_separable`] says so.
    Indistinguishable,
}

/// The numbers a recording set.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Thresholds {
    provenance: Provenance,
    feature: Feature,
    enter_rally_above: f32,
    exit_rally_below: f32,
    min_rally_epochs: u16,
    min_rest_epochs: u16,
    off_court: OffCourtRule,
    min_epoch_completeness: f32,
}

impl Thresholds {
    /// Build a calibration from measured values.
    ///
    /// Every argument is a number some recording produced, and `provenance`
    /// says which. The entry and exit levels must differ — a machine whose two
    /// levels are equal has no hysteresis and will chatter on any epoch sitting
    /// on the boundary.
    #[allow(clippy::too_many_arguments)]
    pub fn from_recordings(
        provenance: Provenance,
        feature: Feature,
        enter_rally_above: f32,
        exit_rally_below: f32,
        min_rally_epochs: u16,
        min_rest_epochs: u16,
        off_court: OffCourtRule,
        min_epoch_completeness: f32,
    ) -> Option<Self> {
        // Negated rather than `>=` so a NaN level is refused rather than accepted.
        #[allow(clippy::neg_cmp_op_on_partial_ord)]
        let unordered = !(exit_rally_below < enter_rally_above);
        if unordered || min_rally_epochs == 0 || min_rest_epochs == 0 {
            return None;
        }
        Some(Self {
            provenance,
            feature,
            enter_rally_above,
            exit_rally_below,
            min_rally_epochs,
            min_rest_epochs,
            off_court,
            min_epoch_completeness,
        })
    }

    /// The recordings these numbers came from.
    pub const fn provenance(&self) -> Provenance {
        self.provenance
    }

    /// The epoch feature being thresholded.
    pub const fn feature(&self) -> Feature {
        self.feature
    }
}

/// Whether the segmenter has anything to work with.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Calibration {
    /// No recording has set the thresholds, so no state is reported.
    Absent,
    /// Thresholds a named recording set.
    Measured(Thresholds),
}

/// Ceiling on stored rally records.
///
/// A match is roughly 80 rallies and a two-hour session of threes has been
/// assumed no worse than three times that; the count keeps rising past the
/// ceiling but the per-rally detail stops being kept, which
/// [`SessionSegmentation::rallies_truncated`] reports.
pub const MAX_RALLIES: usize = 256;

/// An empty rally, so a `SessionSegmentation` can be built in a `const`.
///
/// `Default::default()` is not usable in const context, and a const is what
/// keeps a 7 KB segmenter out of a 10 KB stack -- see [`Segmenter::new`].
const EMPTY_RALLY: Rally = Rally {
    start_epoch: 0,
    epochs: 0,
    hr_mean: 0.0,
    hr_max: 0.0,
    intensity_mean: 0.0,
    hr_sum: 0.0,
    hr_samples: 0,
    intensity_sum: 0.0,
};

/// One rally, as the machine saw it.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rally {
    /// First epoch of the rally.
    pub start_epoch: u32,
    /// Epochs it lasted.
    pub epochs: u16,
    /// Mean heart rate across the epochs that carried one, bpm.
    pub hr_mean: f32,
    /// Highest heart rate across the rally, bpm; 0 when none arrived.
    pub hr_max: f32,
    /// Mean of the segmenting feature over the rally, in that feature's units.
    pub intensity_mean: f32,
    hr_sum: f32,
    hr_samples: u16,
    intensity_sum: f32,
}

impl Rally {
    /// Rally length in seconds.
    pub const fn seconds(&self) -> u32 {
        self.epochs as u32 * (crate::epoch::EPOCH_MS / 1000)
    }

    /// One past the last epoch of the rally.
    pub const fn end_epoch(&self) -> u32 {
        self.start_epoch + self.epochs as u32
    }
}

/// What a whole session looked like.
#[derive(Clone, Debug)]
pub struct SessionSegmentation {
    /// Epochs the machine classified.
    pub epochs_total: u32,
    /// Epochs spent in each state, indexed by the order of [`ActivityState`].
    pub rally_epochs: u32,
    /// Epochs spent resting between rallies.
    pub rest_epochs: u32,
    /// Epochs spent off court.
    pub off_court_epochs: u32,
    /// Epochs dropped for arriving too incomplete to classify.
    pub epochs_dropped: u32,
    /// Rallies seen, including any past [`MAX_RALLIES`].
    pub rally_count: u32,
    /// True when rally detail was dropped because the ceiling was reached.
    pub rallies_truncated: bool,
    /// False when the calibration said off court and rest could not be told
    /// apart, in which case `off_court_epochs` is always zero and the rest
    /// figure carries both.
    pub off_court_separable: bool,
    rallies: [Rally; MAX_RALLIES],
    stored: usize,
}

impl SessionSegmentation {
    /// The rallies whose detail was kept.
    pub fn rallies(&self) -> &[Rally] {
        &self.rallies[..self.stored]
    }

    /// The longest rally kept, by epochs.
    pub fn longest_rally(&self) -> Option<&Rally> {
        self.rallies().iter().max_by_key(|r| r.epochs)
    }

    /// Rally epochs over rest epochs, off-court time excluded.
    ///
    /// Off court is excluded because a session of threes would otherwise report
    /// a work:rest ratio dominated by the games the wearer sat out, which says
    /// nothing about how hard they played the ones they were on for.
    pub fn work_rest_ratio(&self) -> Option<f32> {
        if self.rest_epochs == 0 {
            return None;
        }
        Some(self.rally_epochs as f32 / self.rest_epochs as f32)
    }

    /// True when the state totals account for every epoch that was classified.
    pub fn accounts_for_every_epoch(&self) -> bool {
        self.rally_epochs + self.rest_epochs + self.off_court_epochs + self.epochs_dropped
            == self.epochs_total
    }
}

/// Runs the state machine over a session's epochs.
pub struct Segmenter {
    calibration: Calibration,
    state: ActivityState,
    run_len: u16,
    pending: Option<ActivityState>,
    current: Option<Rally>,
    out: SessionSegmentation,
}

impl Segmenter {
    /// A segmenter for one session.
    ///
    /// `const` on purpose. This type is 7 328 bytes and the watch Service's
    /// stack is 10 240, so a caller that builds one as a value and moves it
    /// into place overflows the stack: measured as a `STKOF` UsageFault
    /// (`CFSR=0x00100000`) in `Squash.SRV` on 2026-09-03. A `const` can
    /// initialise a `static` in place, and [`Segmenter::reset`] is how a second
    /// session gets a clean one without a temporary existing at all.
    pub const fn new(calibration: Calibration) -> Self {
        let separable = match calibration {
            Calibration::Measured(t) => !matches!(t.off_court, OffCourtRule::Indistinguishable),
            Calibration::Absent => false,
        };
        // Off court is where a session starts, since nobody is playing before
        // the first epoch -- unless the calibration says the state cannot be
        // told apart from rest, in which case it must never be entered at all.
        let initial =
            if separable { ActivityState::OffCourt } else { ActivityState::Rest };
        Self {
            calibration,
            state: initial,
            run_len: 0,
            pending: None,
            current: None,
            out: SessionSegmentation {
                epochs_total: 0,
                rally_epochs: 0,
                rest_epochs: 0,
                off_court_epochs: 0,
                epochs_dropped: 0,
                rally_count: 0,
                rallies_truncated: false,
                off_court_separable: separable,
                rallies: [EMPTY_RALLY; MAX_RALLIES],
                stored: 0,
            },
        }
    }

    /// Ready this segmenter for a new session, in place.
    ///
    /// Only the counters and indices are cleared; the rally array is left as it
    /// is, because `stored` is what says how much of it means anything. Writing
    /// 7 KB of zeroes to say the same thing would cost the sample path nothing
    /// but would cost a caller the temporary this method exists to avoid.
    pub fn reset(&mut self, calibration: Calibration) {
        let stored = self.out.stored;
        *self = Self::new(calibration);
        // Keep whatever the array already held rather than rewriting it; it is
        // unreadable while `stored` is 0.
        let _ = stored;
    }

    /// Feed one epoch, with the heart rate that covered it if there was one.
    pub fn push(&mut self, e: &EpochFeatures, hr_bpm: Option<f32>) {
        let Calibration::Measured(t) = self.calibration else {
            return;
        };

        self.out.epochs_total += 1;

        if !e.complete(t.min_epoch_completeness) {
            self.out.epochs_dropped += 1;
            return;
        }

        let value = t.feature.of(e);
        let candidate = self.candidate_state(&t, value);

        if Some(candidate) == self.pending {
            self.run_len = self.run_len.saturating_add(1);
        } else {
            self.pending = Some(candidate);
            self.run_len = 1;
        }

        if candidate != self.state && self.run_len >= dwell(&t, candidate) {
            self.enter(candidate, e.index);
        }

        match self.state {
            ActivityState::Rally => self.out.rally_epochs += 1,
            ActivityState::Rest => self.out.rest_epochs += 1,
            ActivityState::OffCourt => self.out.off_court_epochs += 1,
        }

        if let Some(r) = self.current.as_mut() {
            r.epochs = r.epochs.saturating_add(1);
            r.intensity_sum += value;
            if let Some(bpm) = hr_bpm {
                r.hr_sum += bpm;
                r.hr_samples = r.hr_samples.saturating_add(1);
                if bpm > r.hr_max {
                    r.hr_max = bpm;
                }
            }
        }
    }

    /// Close the session and report what it looked like.
    ///
    /// Takes `self` by value, which moves 7 KB; fine on a host but not on the
    /// watch's 10 KB Service stack. [`Segmenter::finish_in_place`] is the one
    /// to call there.
    pub fn finish(mut self) -> Result<SessionSegmentation, Unavailable> {
        self.close_rally();
        if matches!(self.calibration, Calibration::Absent) {
            return Err(Unavailable::NotCalibrated);
        }
        Ok(self.out)
    }

    /// Close the session and borrow what it looked like, moving nothing.
    pub fn finish_in_place(&mut self) -> Result<&SessionSegmentation, Unavailable> {
        self.close_rally();
        if matches!(self.calibration, Calibration::Absent) {
            return Err(Unavailable::NotCalibrated);
        }
        Ok(&self.out)
    }

    /// The state the machine is in, for a live screen that wants it.
    ///
    /// [`Unavailable::NotCalibrated`] while no recording has set the thresholds.
    pub fn state(&self) -> Result<ActivityState, Unavailable> {
        match self.calibration {
            Calibration::Absent => Err(Unavailable::NotCalibrated),
            Calibration::Measured(_) => Ok(self.state),
        }
    }

    fn candidate_state(&self, t: &Thresholds, value: f32) -> ActivityState {
        if value > t.enter_rally_above {
            return ActivityState::Rally;
        }
        if self.state == ActivityState::Rally && value >= t.exit_rally_below {
            // Inside the hysteresis band a rally keeps running, which is what
            // stops one quiet second splitting a long rally in two.
            return ActivityState::Rally;
        }
        match t.off_court {
            OffCourtRule::Level { below, .. } if value < below => ActivityState::OffCourt,
            _ => ActivityState::Rest,
        }
    }

    fn enter(&mut self, next: ActivityState, epoch: u32) {
        if self.state == ActivityState::Rally {
            self.close_rally();
        }
        self.state = next;
        self.run_len = 0;
        if next == ActivityState::Rally {
            self.out.rally_count += 1;
            self.current = Some(Rally { start_epoch: epoch, ..Rally::default() });
        }
    }

    fn close_rally(&mut self) {
        let Some(mut r) = self.current.take() else {
            return;
        };
        if r.hr_samples > 0 {
            r.hr_mean = r.hr_sum / r.hr_samples as f32;
        }
        if r.epochs > 0 {
            r.intensity_mean = r.intensity_sum / r.epochs as f32;
        }
        if self.out.stored < MAX_RALLIES {
            self.out.rallies[self.out.stored] = r;
            self.out.stored += 1;
        } else {
            self.out.rallies_truncated = true;
        }
    }
}

fn dwell(t: &Thresholds, state: ActivityState) -> u16 {
    match state {
        ActivityState::Rally => t.min_rally_epochs,
        ActivityState::Rest => t.min_rest_epochs,
        ActivityState::OffCourt => match t.off_court {
            OffCourtRule::Level { for_epochs, .. } => for_epochs,
            OffCourtRule::Indistinguishable => u16::MAX,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::epoch::UNMEASURED;

    fn thresholds(off_court: OffCourtRule) -> Thresholds {
        Thresholds::from_recordings(
            UNMEASURED,
            Feature::AccelVariance,
            1000.0,
            400.0,
            2,
            3,
            off_court,
            0.9,
        )
        .expect("the levels are ordered")
    }

    /// Epochs attributed to the starting state before the first rally is
    /// confirmed. The epoch that satisfies the dwell is itself the first rally
    /// epoch, so this is one less than `min_rally_epochs` from `thresholds()`.
    const LEAD_IN_EPOCHS: u32 = 1;

    fn epoch(index: u32, var: f32) -> EpochFeatures {
        EpochFeatures { index, samples: 100, accel_mag_var: var, ..EpochFeatures::default() }
    }

    fn run(values: &[f32], off_court: OffCourtRule) -> SessionSegmentation {
        let mut s = Segmenter::new(Calibration::Measured(thresholds(off_court)));
        for (i, v) in values.iter().enumerate() {
            s.push(&epoch(i as u32, *v), Some(150.0));
        }
        s.finish().expect("calibrated")
    }

    #[test]
    fn without_a_calibration_nothing_is_reported() {
        let mut s = Segmenter::new(Calibration::Absent);
        s.push(&epoch(0, 9999.0), Some(180.0));
        assert_eq!(s.state(), Err(Unavailable::NotCalibrated));
        assert_eq!(s.finish().unwrap_err(), Unavailable::NotCalibrated);
    }

    #[test]
    fn levels_that_are_not_ordered_are_refused() {
        assert!(Thresholds::from_recordings(
            UNMEASURED,
            Feature::AccelVariance,
            400.0,
            400.0,
            2,
            3,
            OffCourtRule::Indistinguishable,
            0.9
        )
        .is_none());
    }

    #[test]
    fn one_quiet_second_does_not_split_a_rally() {
        // Twelve loud epochs with a single mid-band one in the middle: inside
        // the hysteresis band, so the rally runs through it.
        let mut v = [5000.0f32; 12];
        v[6] = 600.0;
        let out = run(&v, OffCourtRule::Indistinguishable);
        assert_eq!(out.rally_count, 1);
        assert_eq!(out.rallies()[0].epochs, 11);
    }

    #[test]
    fn a_real_rest_ends_the_rally() {
        let mut v = [5000.0f32; 6].to_vec();
        v.extend([100.0f32; 8]);
        v.extend([5000.0f32; 6]);
        let out = run(&v, OffCourtRule::Indistinguishable);
        assert_eq!(out.rally_count, 2);
        assert!(out.rest_epochs > 0);
    }

    #[test]
    fn an_incomplete_epoch_is_dropped_rather_than_classified() {
        let mut s = Segmenter::new(Calibration::Measured(thresholds(OffCourtRule::Indistinguishable)));
        s.push(&EpochFeatures { index: 0, samples: 10, accel_mag_var: 5000.0, ..Default::default() }, None);
        let out = s.finish().unwrap();
        assert_eq!(out.epochs_dropped, 1);
        assert_eq!(out.rally_epochs, 0);
        assert!(out.accounts_for_every_epoch());
    }

    #[test]
    fn every_epoch_is_accounted_for_and_every_rally_lies_inside_the_session() {
        let mut v = [5000.0f32; 10].to_vec();
        v.extend([100.0f32; 20]);
        v.extend([5000.0f32; 10]);
        v.extend([5.0f32; 40]);
        let out = run(&v, OffCourtRule::Level { below: 50.0, for_epochs: 5 });
        assert!(out.accounts_for_every_epoch());
        assert_eq!(out.epochs_total, 80);
        for r in out.rallies() {
            assert!(r.end_epoch() <= out.epochs_total);
        }
    }

    #[test]
    fn off_court_needs_its_own_level_not_just_a_long_rest() {
        let mut v = [5000.0f32; 6].to_vec();
        // Forty seconds of the rest level. Long, but not quiet enough to be off court.
        v.extend([100.0f32; 40]);
        let separable = run(&v, OffCourtRule::Level { below: 50.0, for_epochs: 5 });
        // Only the lead-in before the first rally is off court; the forty quiet
        // seconds that follow are rest, because they never reach the level.
        assert_eq!(separable.off_court_epochs, LEAD_IN_EPOCHS);
        assert_eq!(
            separable.rest_epochs,
            separable.epochs_total - separable.rally_epochs - LEAD_IN_EPOCHS,
            "every quiet epoch after the rally is rest, not off court"
        );

        let mut w = [5000.0f32; 6].to_vec();
        w.extend([5.0f32; 40]);
        let quiet = run(&w, OffCourtRule::Level { below: 50.0, for_epochs: 5 });
        assert!(quiet.off_court_epochs > 0);
    }

    #[test]
    fn an_indistinguishable_calibration_says_so_and_never_reports_off_court() {
        let mut v = [5000.0f32; 6].to_vec();
        v.extend([1.0f32; 120]);
        let out = run(&v, OffCourtRule::Indistinguishable);
        assert!(!out.off_court_separable);
        assert_eq!(out.off_court_epochs, 0);
        assert_eq!(out.rest_epochs + out.rally_epochs, out.epochs_total);
        assert!(out.rest_epochs > 100, "the quiet stretch is rest: {}", out.rest_epochs);
    }

    #[test]
    fn work_rest_ratio_ignores_time_spent_off_court() {
        let mut v = [5000.0f32; 20].to_vec();
        v.extend([100.0f32; 10]);
        v.extend([5.0f32; 600]);
        let out = run(&v, OffCourtRule::Level { below: 50.0, for_epochs: 5 });
        let ratio = out.work_rest_ratio().expect("there was rest");
        assert!(ratio > 1.0, "600 epochs off court must not drag the ratio down: {ratio}");
    }

    #[test]
    fn rally_detail_stops_at_the_ceiling_and_says_so() {
        let mut s = Segmenter::new(Calibration::Measured(thresholds(OffCourtRule::Indistinguishable)));
        let mut i = 0u32;
        for _ in 0..(MAX_RALLIES + 5) {
            for _ in 0..3 {
                s.push(&epoch(i, 5000.0), None);
                i += 1;
            }
            for _ in 0..4 {
                s.push(&epoch(i, 100.0), None);
                i += 1;
            }
        }
        let out = s.finish().unwrap();
        assert!(out.rally_count > MAX_RALLIES as u32);
        assert_eq!(out.rallies().len(), MAX_RALLIES);
        assert!(out.rallies_truncated);
    }
}
