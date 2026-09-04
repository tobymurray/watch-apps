//! One session, as the thing a baseline is built out of.
//!
//! The record stores what was measured rather than what was concluded, so a
//! later change to how a figure is derived can be applied to the history
//! instead of orphaning it. That is why the recovery figures here are counts
//! and sums of *windows that qualified* rather than a mean: a mean is a
//! conclusion about windows that are no longer present, and changing the window
//! criteria would orphan every one ever stored.

use crate::hr::HrSource;
use crate::record::Recovery;
use crate::window::WindowKind;

/// Recovery windows one session *persists in full*.
///
/// MEASURED: a window with its curve serialises to about 150 bytes, so twenty
/// sessions of sixteen would be some 48 KB against the 16 KiB the profile is
/// capped at — see `the_widest_profile_fits_its_cap`. Four fits with room.
///
/// The count and the mean are **not** bounded by this. Every qualifying window
/// updates [`SessionRecord`]'s per-kind tally whether or not its detail is
/// kept, so a mean is over all of them and `windows_dropped` says how much
/// detail went. What the bound costs is the ability to re-derive a mean under
/// *new* criteria from more than the four kept.
pub const MAX_SESSION_WINDOWS: usize = 4;

/// Window kinds a tally has a slot for; matches [`crate::window::ALL_KINDS`].
const KINDS: usize = 5;

/// What one session measured.
///
/// Every duration is whole seconds and every heart rate is bpm; the fixed-point
/// scaling that reaches the profile file lives in [`crate::profile`], not here.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SessionRecord {
    /// When the session started, Unix seconds, and the record's identity.
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
    /// False when no calibration was available, in which case every
    /// segmentation field below is zero because nothing was segmented rather
    /// than because nothing happened.
    pub segmented: bool,
    /// Rallies the segmenter found.
    pub rally_count: u32,
    /// Seconds spent in rallies.
    pub rally_s: u32,
    /// Seconds spent resting between rallies.
    pub rest_s: u32,
    /// Seconds spent off court.
    pub off_court_s: u32,
    /// The windows that qualified, newest last, up to [`MAX_SESSION_WINDOWS`].
    pub windows: [Recovery; MAX_SESSION_WINDOWS],
    /// Filled entries of `windows`.
    pub window_count: u8,
    /// Qualified, but their detail did not fit. They still count and still
    /// contribute to the mean.
    pub windows_dropped: u16,
    /// Total fall in bpm across every qualifying window of each kind.
    ///
    /// Indexed by [`crate::window::WindowKind`]'s code minus one. A sum and a
    /// count rather than a mean, so the mean is derived on read.
    pub drop_sum: [u32; KINDS],
    /// Qualifying windows of each kind, however many had their detail kept.
    pub drop_n: [u16; KINDS],
}

impl Default for SessionRecord {
    fn default() -> Self {
        Self::EMPTY
    }
}

impl SessionRecord {
    /// A record with nothing in it, usable in a `const`.
    pub const EMPTY: Self = Self {
        started_utc: 0,
        active_s: 0,
        hr_mean: 0.0,
        hr_max: 0.0,
        hr_covered_s: 0,
        hr_source: HrSource::Unknown,
        segmented: false,
        rally_count: 0,
        rally_s: 0,
        rest_s: 0,
        off_court_s: 0,
        windows: [Recovery::EMPTY; MAX_SESSION_WINDOWS],
        window_count: 0,
        windows_dropped: 0,
        drop_sum: [0; KINDS],
        drop_n: [0; KINDS],
    };

    /// The windows whose detail this session carries.
    pub fn windows(&self) -> &[Recovery] {
        let n = (self.window_count as usize).min(MAX_SESSION_WINDOWS);
        &self.windows[..n]
    }

    /// Count one qualifying window, keeping its detail if there is room.
    ///
    /// The tally is updated either way, so dropping detail costs inspection
    /// and never costs the count or the mean.
    pub fn add_window(&mut self, r: Recovery) {
        if let Some(i) = kind_index(r.kind) {
            self.drop_sum[i] = self.drop_sum[i].saturating_add(r.drop_bpm() as u32);
            self.drop_n[i] = self.drop_n[i].saturating_add(1);
        }
        let n = self.window_count as usize;
        if n < MAX_SESSION_WINDOWS {
            self.windows[n] = r;
            self.window_count = (n + 1) as u8;
        } else {
            self.windows_dropped = self.windows_dropped.saturating_add(1);
        }
    }

    /// Windows of one kind that qualified, including any whose detail went.
    pub fn window_count_of(&self, kind: WindowKind) -> u16 {
        kind_index(kind.code()).map_or(0, |i| self.drop_n[i])
    }

    /// Mean fall across every qualifying window of one kind, bpm.
    ///
    /// `None` when there were none, which is a different answer from zero.
    /// Derived from a sum and a count rather than stored, so changing how it is
    /// computed applies to the whole history instead of orphaning it.
    pub fn mean_drop(&self, kind: WindowKind) -> Option<f32> {
        let i = kind_index(kind.code())?;
        let n = self.drop_n[i];
        (n > 0).then(|| self.drop_sum[i] as f32 / n as f32)
    }
}

/// A kind's slot in the tally, or `None` for a code no build of this knows.
const fn kind_index(code: u8) -> Option<usize> {
    if code >= 1 && (code as usize) <= KINDS {
        Some(code as usize - 1)
    } else {
        None
    }
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
    /// Seconds of rally per minute of on-court time.
    RallyRate,
    /// Rally seconds over rest seconds.
    WorkRestRatio,
    /// Mean fall across rests between rallies.
    RecoveryShort,
    /// Mean fall across off-court rests.
    RecoveryLong,
    /// Mean fall across whole-session cessations, which is what a
    /// non-segmenting app produces.
    RecoveryPause,
}

/// Whether a metric may be compared between sessions, and on what terms.
///
/// A reader merging several apps' logs needs this: a total is comparable only
/// within one session, while a level or a rate crosses them. The distinction is
/// the schema's, not a convention a reader has to know.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    /// A level, comparable across sessions.
    Level,
    /// A rate, with session length divided out, comparable across sessions.
    Rate,
    /// A ratio, comparable across sessions.
    Ratio,
    /// A total, comparable within a session only.
    Total,
}

/// Every metric, in the order a shim's C enum indexes them.
pub const ALL_METRICS: [Metric; 8] = [
    Metric::HrMean,
    Metric::HrMax,
    Metric::RallyCount,
    Metric::RallyRate,
    Metric::WorkRestRatio,
    Metric::RecoveryShort,
    Metric::RecoveryLong,
    Metric::RecoveryPause,
];

impl Metric {
    /// True when the metric needs a segmented session.
    pub const fn needs_segmentation(&self) -> bool {
        !matches!(
            self,
            Metric::HrMean | Metric::HrMax | Metric::RecoveryPause
        )
    }

    /// Whether this may be compared between sessions.
    pub const fn kind(&self) -> Kind {
        match self {
            Metric::HrMean
            | Metric::HrMax
            | Metric::RecoveryShort
            | Metric::RecoveryLong
            | Metric::RecoveryPause => Kind::Level,
            Metric::RallyRate => Kind::Rate,
            Metric::WorkRestRatio => Kind::Ratio,
            Metric::RallyCount => Kind::Total,
        }
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
            Metric::RecoveryPause => "recovery_pause",
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
            Metric::RecoveryShort => self.mean_drop(WindowKind::BetweenRallies),
            Metric::RecoveryLong => self.mean_drop(WindowKind::OffCourt),
            Metric::RecoveryPause => self.mean_drop(WindowKind::Pause),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn window(kind: WindowKind, hr0: u8, hr_end: u8) -> Recovery {
        Recovery { kind: kind.code(), hr0, hr_end, ..Recovery::EMPTY }
    }

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
            ..SessionRecord::EMPTY
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
    fn an_unsegmented_session_still_votes_on_heart_rate_and_on_a_pause() {
        let s = SessionRecord { segmented: false, rally_count: 0, ..long_session() };
        assert!(s.admits(Metric::HrMean).is_ok());
        assert!(s.admits(Metric::RecoveryPause).is_ok());
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

    #[test]
    fn short_and_long_rests_are_never_averaged_together() {
        let mut s = long_session();
        s.add_window(window(WindowKind::BetweenRallies, 180, 170));
        s.add_window(window(WindowKind::OffCourt, 175, 100));
        assert_eq!(s.value(Metric::RecoveryShort), Some(10.0));
        assert_eq!(s.value(Metric::RecoveryLong), Some(75.0));
    }

    #[test]
    fn a_kind_with_no_window_reports_none_rather_than_zero() {
        let mut s = long_session();
        s.add_window(window(WindowKind::BetweenRallies, 180, 170));
        assert_eq!(s.value(Metric::RecoveryLong), None);
    }

    #[test]
    fn the_mean_is_derived_from_the_windows_so_it_can_be_rederived() {
        let mut s = long_session();
        s.add_window(window(WindowKind::Pause, 170, 150));
        s.add_window(window(WindowKind::Pause, 160, 150));
        assert_eq!(s.value(Metric::RecoveryPause), Some(15.0));
        // The inputs are still there, which is the point: a change to what
        // qualifies re-derives rather than orphaning the history.
        assert_eq!(s.windows().len(), 2);
        assert_eq!(s.windows()[0].hr0, 170);
    }

    #[test]
    fn a_total_is_marked_as_one_so_a_reader_knows_not_to_cross_sessions() {
        assert_eq!(Metric::RallyCount.kind(), Kind::Total);
        assert_eq!(Metric::RallyRate.kind(), Kind::Rate);
        assert_eq!(Metric::HrMean.kind(), Kind::Level);
        assert_eq!(Metric::WorkRestRatio.kind(), Kind::Ratio);
    }
}
