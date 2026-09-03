//! One session, as the thing a baseline is built out of.
//!
//! The record stores what was measured rather than what was concluded, so a
//! later change to how a figure is derived can be applied to the history
//! instead of orphaning it.

use crate::recovery::HrSource;

/// What one session measured.
///
/// Every duration is whole seconds and every heart rate is bpm; the fixed-point
/// scaling that reaches the profile file lives in [`crate::profile`], not here.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SessionRecord {
    /// When the session started, Unix seconds.
    pub started_utc: u32,
    /// Seconds the activity was running, excluding pauses.
    pub active_s: u32,
    /// Mean heart rate across trusted readings, bpm.
    pub hr_mean: f32,
    /// Highest trusted heart rate, bpm.
    pub hr_max: f32,
    /// Seconds a trusted heart rate was available.
    pub hr_covered_s: u32,
    /// Which sensor supplied most of the session.
    pub hr_source: HrSource,
    /// False when no calibration was available, in which case every field below
    /// is zero because nothing was segmented rather than because nothing happened.
    pub segmented: bool,
    /// Rallies the segmenter found.
    pub rally_count: u32,
    /// Seconds spent in rallies.
    pub rally_s: u32,
    /// Seconds spent resting between rallies.
    pub rest_s: u32,
    /// Seconds spent off court.
    pub off_court_s: u32,
    /// Mean heart-rate fall across qualifying rests between rallies, bpm.
    pub recovery_short_mean: f32,
    /// Qualifying rests between rallies.
    pub recovery_short_n: u16,
    /// Mean heart-rate fall across qualifying off-court rests, bpm.
    pub recovery_long_mean: f32,
    /// Qualifying off-court rests.
    pub recovery_long_n: u16,
}

/// Shortest session that may contribute to a baseline.
///
/// Ten minutes excludes a knock-up and an activity started and abandoned, both
/// of which are sessions the wearer had but not sessions that say what their
/// normal is.
pub const MIN_ACTIVE_S_TO_VOTE: u32 = 600;

/// Least fraction of the session a trusted heart rate must cover.
///
/// Below this the mean is over a different session from the one recorded, and a
/// strap that fell off halfway is the case this exists for.
pub const MIN_HR_COVERAGE_TO_VOTE: f32 = 0.8;

/// Fewest rallies before a session may vote on a rally-derived baseline.
pub const MIN_RALLIES_TO_VOTE: u32 = 10;

/// Why a session did not contribute to a baseline.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NotAdmitted {
    /// Shorter than [`MIN_ACTIVE_S_TO_VOTE`].
    TooShort,
    /// A trusted heart rate covered less than [`MIN_HR_COVERAGE_TO_VOTE`] of it.
    HeartRateTooSparse,
    /// No calibration, so the session has no rally structure to contribute.
    NotSegmented,
    /// Fewer rallies than [`MIN_RALLIES_TO_VOTE`], so probably a drill session
    /// rather than rally-structured play.
    TooFewRallies,
}

/// Which measurement a baseline tracks.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Metric {
    /// Mean heart rate across the session.
    HrMean,
    /// Highest heart rate reached.
    HrMax,
    /// Rallies in the session.
    RallyCount,
    /// Seconds of rally per minute of on-court time, so a long session and a
    /// short one are comparable.
    RallyRate,
    /// Rally seconds over rest seconds.
    WorkRestRatio,
    /// Mean fall across rests between rallies.
    RecoveryShort,
    /// Mean fall across off-court rests.
    RecoveryLong,
}

impl Metric {
    /// True when the metric needs a segmented session.
    pub const fn needs_segmentation(&self) -> bool {
        !matches!(self, Metric::HrMean | Metric::HrMax)
    }

    /// Name, for reports and for the profile file's own keys.
    pub const fn name(&self) -> &'static str {
        match self {
            Metric::HrMean => "hr_mean",
            Metric::HrMax => "hr_max",
            Metric::RallyCount => "rally_count",
            Metric::RallyRate => "rally_rate",
            Metric::WorkRestRatio => "work_rest_ratio",
            Metric::RecoveryShort => "recovery_short",
            Metric::RecoveryLong => "recovery_long",
        }
    }
}

impl SessionRecord {
    /// Whether this session may contribute to the given metric's baseline.
    ///
    /// A session that may not still gets recorded and shown; it just does not
    /// get to define the wearer.
    pub fn admits(&self, metric: Metric) -> Result<(), NotAdmitted> {
        if self.active_s < MIN_ACTIVE_S_TO_VOTE {
            return Err(NotAdmitted::TooShort);
        }
        if (self.hr_covered_s as f32) < self.active_s as f32 * MIN_HR_COVERAGE_TO_VOTE {
            return Err(NotAdmitted::HeartRateTooSparse);
        }
        if metric.needs_segmentation() {
            if !self.segmented {
                return Err(NotAdmitted::NotSegmented);
            }
            if self.rally_count < MIN_RALLIES_TO_VOTE {
                return Err(NotAdmitted::TooFewRallies);
            }
        }
        Ok(())
    }

    /// The session's value for one metric, or `None` when it has none.
    pub fn value(&self, metric: Metric) -> Option<f32> {
        match metric {
            Metric::HrMean => (self.hr_mean > 0.0).then_some(self.hr_mean),
            Metric::HrMax => (self.hr_max > 0.0).then_some(self.hr_max),
            Metric::RallyCount => self.segmented.then_some(self.rally_count as f32),
            Metric::RallyRate => {
                let on_court = self.rally_s + self.rest_s;
                (self.segmented && on_court > 0)
                    .then(|| self.rally_s as f32 * 60.0 / on_court as f32)
            }
            Metric::WorkRestRatio => (self.segmented && self.rest_s > 0)
                .then(|| self.rally_s as f32 / self.rest_s as f32),
            Metric::RecoveryShort => {
                (self.recovery_short_n > 0).then_some(self.recovery_short_mean)
            }
            Metric::RecoveryLong => (self.recovery_long_n > 0).then_some(self.recovery_long_mean),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn long_session() -> SessionRecord {
        SessionRecord {
            started_utc: 1_756_900_000,
            active_s: 3600,
            hr_mean: 142.0,
            hr_max: 181.0,
            hr_covered_s: 3500,
            hr_source: HrSource::External,
            segmented: true,
            rally_count: 42,
            rally_s: 900,
            rest_s: 1200,
            off_court_s: 1500,
            recovery_short_mean: 12.5,
            recovery_short_n: 8,
            recovery_long_mean: 34.0,
            recovery_long_n: 3,
        }
    }

    #[test]
    fn a_short_session_never_votes() {
        let s = SessionRecord { active_s: 300, ..long_session() };
        assert_eq!(s.admits(Metric::HrMean), Err(NotAdmitted::TooShort));
    }

    #[test]
    fn a_session_whose_strap_fell_off_never_votes() {
        let s = SessionRecord { hr_covered_s: 900, ..long_session() };
        assert_eq!(s.admits(Metric::HrMean), Err(NotAdmitted::HeartRateTooSparse));
    }

    #[test]
    fn an_unsegmented_session_still_votes_on_heart_rate() {
        let s = SessionRecord { segmented: false, rally_count: 0, ..long_session() };
        assert!(s.admits(Metric::HrMean).is_ok());
        assert_eq!(s.admits(Metric::RallyCount), Err(NotAdmitted::NotSegmented));
    }

    #[test]
    fn a_drill_session_does_not_vote_on_rally_structure() {
        let s = SessionRecord { rally_count: 2, ..long_session() };
        assert_eq!(s.admits(Metric::RallyCount), Err(NotAdmitted::TooFewRallies));
        assert!(s.admits(Metric::HrMean).is_ok());
    }

    #[test]
    fn rally_rate_is_a_rate_so_a_long_session_and_a_short_one_compare() {
        let short = SessionRecord { rally_s: 300, rest_s: 300, ..long_session() };
        let long = SessionRecord { rally_s: 1200, rest_s: 1200, ..long_session() };
        assert_eq!(short.value(Metric::RallyRate), long.value(Metric::RallyRate));
    }

    #[test]
    fn an_unsegmented_session_reports_no_rally_value_rather_than_zero() {
        let s = SessionRecord { segmented: false, ..long_session() };
        assert_eq!(s.value(Metric::RallyCount), None);
        assert_eq!(s.value(Metric::WorkRestRatio), None);
    }
}
