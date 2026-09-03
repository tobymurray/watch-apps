//! How far the heart rate fell across a rest, and whether that fall is a
//! measurement at all.
//!
//! Two things separate this from a naive HRR: rests between rallies and whole
//! games spent off court are kept apart rather than averaged, and a window that
//! did not meet its criteria is discarded and counted rather than reported as a
//! small number. The count of discarded windows is an output in its own right.
//!
//! Like [`crate::segment`], nothing here is tuned. The window length, the
//! tolerated gap and whether wrist optical is acceptable at all are exactly
//! what Phase A1 exists to decide, so [`Calibration::Absent`] is the only state
//! the watch build can reach today.

use crate::segment::ActivityState;
use crate::{Provenance, Unavailable};

/// Where a heart rate came from, mirroring `HEART_RATE_EX`'s own source field.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum HrSource {
    /// The watch did not say.
    #[default]
    Unknown,
    /// The watch's own wrist PPG.
    Optical,
    /// A paired chest strap.
    External,
}

/// One heart-rate reading.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HrSample {
    /// Milliseconds on the session clock.
    pub t_ms: u32,
    /// Beats per minute.
    pub bpm: f32,
    /// The watch's own 0-3 confidence; 0 is untrusted.
    pub trust: u8,
    /// Which sensor produced it.
    pub source: HrSource,
}

/// Which sources a recovery figure may be built from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SourcePolicy {
    /// Only a chest strap.
    ExternalOnly,
    /// Either, with the source recorded on every window so a reader can filter.
    EitherWithSourceRecorded,
}

/// How the fall is expressed.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Formulation {
    /// An absolute fall in bpm over a fixed number of seconds.
    FixedWindowDrop {
        /// Seconds from the start of the rest to the reading that ends the window.
        window_s: u16,
    },
    /// A1 found the signal's own smoothing time constant comparable to the
    /// rests being measured across, so no fall measured over one is physiology.
    NotMeasurableOnThisHardware,
}

/// The numbers a recording set.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Thresholds {
    provenance: Provenance,
    short: Formulation,
    long: Formulation,
    max_gap_ms: u32,
    min_trusted_samples: u16,
    min_trust: u8,
    source_policy: SourcePolicy,
}

impl Thresholds {
    /// Build a calibration from measured values.
    ///
    /// `short` governs rests between rallies and `long` whole games off court;
    /// they are separate because they are different measurements and must never
    /// be averaged together.
    pub fn from_recordings(
        provenance: Provenance,
        short: Formulation,
        long: Formulation,
        max_gap_ms: u32,
        min_trusted_samples: u16,
        min_trust: u8,
        source_policy: SourcePolicy,
    ) -> Option<Self> {
        if min_trusted_samples == 0 || min_trust == 0 {
            return None;
        }
        Some(Self { provenance, short, long, max_gap_ms, min_trusted_samples, min_trust, source_policy })
    }

    /// The recordings these numbers came from.
    pub const fn provenance(&self) -> Provenance {
        self.provenance
    }
}

/// Whether the analyser has anything to work with.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Calibration {
    /// No recording has set the window or the source policy.
    Absent,
    /// Thresholds a named recording set.
    Measured(Thresholds),
}

/// Which kind of rest a window covered.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WindowKind {
    /// A rest between rallies, on court.
    BetweenRallies,
    /// A whole game or longer spent off court.
    OffCourt,
}

/// Why a window contributed nothing.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Discarded {
    /// The rest ended before the window did.
    pub too_short: u16,
    /// A gap in the heart-rate stream longer than the calibration allows.
    pub hr_gap: u16,
    /// Fewer trusted readings across the window than required.
    pub too_few_trusted: u16,
    /// The source was not one the policy accepts.
    pub source_not_accepted: u16,
    /// No heart rate arrived at all.
    pub no_heart_rate: u16,
    /// The calibration says this kind of window is not measurable here.
    pub not_measurable: u16,
}

impl Discarded {
    /// Windows discarded for any reason.
    pub const fn total(&self) -> u16 {
        self.too_short
            + self.hr_gap
            + self.too_few_trusted
            + self.source_not_accepted
            + self.no_heart_rate
            + self.not_measurable
    }
}

/// One measured fall.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RecoveryWindow {
    /// Which kind of rest it covered.
    pub kind: WindowKind,
    /// Start of the rest on the session clock, ms.
    pub start_ms: u32,
    /// Seconds from the start reading to the end reading.
    pub window_s: u16,
    /// Heart rate when the rest began, bpm.
    pub bpm_at_start: f32,
    /// Heart rate at the end of the window, bpm.
    pub bpm_at_end: f32,
    /// Trusted readings across the window.
    pub trusted_samples: u16,
    /// Where the readings came from.
    pub source: HrSource,
}

impl RecoveryWindow {
    /// The fall in bpm; negative when the heart rate rose.
    pub fn drop_bpm(&self) -> f32 {
        self.bpm_at_start - self.bpm_at_end
    }
}

/// Ceiling on stored windows.
///
/// Sized above [`crate::segment::MAX_RALLIES`] is pointless — there is at most
/// one rest per rally — so this matches the rally ceiling's order and the count
/// keeps rising past it.
pub const MAX_WINDOWS: usize = 256;

/// Whether recovery got better or worse across the session.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Trend {
    /// Mean fall across the first half of the qualifying windows, bpm.
    pub first_half_mean: f32,
    /// Mean fall across the second half, bpm.
    pub second_half_mean: f32,
    /// Windows the trend was computed over.
    pub windows: u16,
}

impl Trend {
    /// Second half minus first: negative means recovery got worse.
    pub fn change_bpm(&self) -> f32 {
        self.second_half_mean - self.first_half_mean
    }
}

/// Fewest qualifying windows before a within-session trend is reported.
///
/// Four is the smallest count that gives two windows a side, so neither half's
/// mean is a single measurement.
pub const MIN_WINDOWS_FOR_TREND: u16 = 4;

/// What a session's rests measured.
#[derive(Clone, Debug)]
pub struct SessionRecovery {
    /// Windows that met their criteria.
    windows: [Option<RecoveryWindow>; MAX_WINDOWS],
    stored: usize,
    /// Windows that did not, by reason.
    pub discarded: Discarded,
    /// True when detail was dropped because [`MAX_WINDOWS`] was reached.
    pub windows_truncated: bool,
}

impl SessionRecovery {
    /// Every qualifying window, in the order they happened.
    pub fn windows(&self) -> impl Iterator<Item = &RecoveryWindow> {
        self.windows[..self.stored].iter().flatten()
    }

    /// Mean fall across the qualifying windows of one kind.
    ///
    /// `None` when there were none, which is a different answer from zero.
    pub fn mean_drop(&self, kind: WindowKind) -> Option<f32> {
        let mut sum = 0.0;
        let mut n = 0u16;
        for w in self.windows().filter(|w| w.kind == kind) {
            sum += w.drop_bpm();
            n += 1;
        }
        (n > 0).then(|| sum / n as f32)
    }

    /// Qualifying windows of one kind.
    pub fn count(&self, kind: WindowKind) -> u16 {
        self.windows().filter(|w| w.kind == kind).count() as u16
    }

    /// Whether recovery between rallies changed across the session.
    ///
    /// Only rests between rallies, and only when there are at least
    /// [`MIN_WINDOWS_FOR_TREND`] of them: a trend over three windows is a line
    /// through noise.
    pub fn trend(&self) -> Result<Trend, Unavailable> {
        let n = self.count(WindowKind::BetweenRallies);
        if n < MIN_WINDOWS_FOR_TREND {
            return Err(Unavailable::NoQualifyingWindow);
        }
        let half = (n / 2) as usize;
        let mut first = 0.0f32;
        let mut second = 0.0f32;
        for (i, w) in self.windows().filter(|w| w.kind == WindowKind::BetweenRallies).enumerate() {
            if i < half {
                first += w.drop_bpm();
            } else {
                second += w.drop_bpm();
            }
        }
        Ok(Trend {
            first_half_mean: first / half as f32,
            second_half_mean: second / (n as usize - half) as f32,
            windows: n,
        })
    }
}

/// Measures recovery across the rests a segmenter identified.
///
/// Holds no history: the heart rate at the start of a window is captured when
/// the window opens, so nothing has to be buffered to look back at it.
pub struct RecoveryAnalyser {
    calibration: Calibration,
    state: ActivityState,
    pending: Option<Pending>,
    out: SessionRecovery,
}

#[derive(Clone, Copy, Debug)]
struct Pending {
    kind: WindowKind,
    start_ms: u32,
    window_s: u16,
    bpm_at_start: Option<f32>,
    bpm_at_end: Option<f32>,
    last_trusted_ms: u32,
    trusted: u16,
    source: HrSource,
    gap_seen: bool,
    source_rejected: bool,
    closed: bool,
}

impl RecoveryAnalyser {
    /// An analyser for one session.
    pub fn new(calibration: Calibration) -> Self {
        Self {
            calibration,
            state: ActivityState::OffCourt,
            pending: None,
            out: SessionRecovery {
                windows: [None; MAX_WINDOWS],
                stored: 0,
                discarded: Discarded::default(),
                windows_truncated: false,
            },
        }
    }

    /// Tell the analyser what the segmenter thinks the wearer is doing.
    ///
    /// Called on every transition, with the millisecond it happened.
    pub fn on_state(&mut self, state: ActivityState, t_ms: u32) {
        let Calibration::Measured(t) = self.calibration else {
            return;
        };
        if state == self.state {
            return;
        }
        self.settle();
        self.state = state;

        let (kind, formulation) = match state {
            ActivityState::Rest => (WindowKind::BetweenRallies, t.short),
            ActivityState::OffCourt => (WindowKind::OffCourt, t.long),
            ActivityState::Rally => return,
        };

        match formulation {
            Formulation::NotMeasurableOnThisHardware => {
                self.out.discarded.not_measurable += 1;
            }
            Formulation::FixedWindowDrop { window_s } => {
                self.pending = Some(Pending {
                    kind,
                    start_ms: t_ms,
                    window_s,
                    bpm_at_start: None,
                    bpm_at_end: None,
                    last_trusted_ms: t_ms,
                    trusted: 0,
                    source: HrSource::Unknown,
                    gap_seen: false,
                    source_rejected: false,
                    closed: false,
                });
            }
        }
    }

    /// Feed one heart-rate reading.
    pub fn on_hr(&mut self, s: &HrSample) {
        let Calibration::Measured(t) = self.calibration else {
            return;
        };
        let Some(p) = self.pending.as_mut() else {
            return;
        };
        if p.closed || s.t_ms < p.start_ms {
            return;
        }

        let end_ms = p.start_ms + p.window_s as u32 * 1000;
        if s.t_ms > end_ms {
            p.closed = true;
            return;
        }

        if s.trust < t.min_trust {
            return;
        }
        if matches!(t.source_policy, SourcePolicy::ExternalOnly) && s.source != HrSource::External {
            p.source_rejected = true;
            return;
        }

        if s.t_ms.wrapping_sub(p.last_trusted_ms) > t.max_gap_ms {
            p.gap_seen = true;
        }
        p.last_trusted_ms = s.t_ms;
        p.trusted = p.trusted.saturating_add(1);
        p.source = s.source;
        if p.bpm_at_start.is_none() {
            p.bpm_at_start = Some(s.bpm);
        }
        p.bpm_at_end = Some(s.bpm);

        if s.t_ms == end_ms {
            p.closed = true;
        }
    }

    /// Close the session and report what its rests measured.
    pub fn finish(mut self) -> Result<SessionRecovery, Unavailable> {
        if matches!(self.calibration, Calibration::Absent) {
            return Err(Unavailable::NotCalibrated);
        }
        self.settle();
        Ok(self.out)
    }

    fn settle(&mut self) {
        let Calibration::Measured(t) = self.calibration else {
            return;
        };
        let Some(p) = self.pending.take() else {
            return;
        };
        let d = &mut self.out.discarded;

        if p.trusted == 0 {
            if p.source_rejected {
                d.source_not_accepted += 1;
            } else {
                d.no_heart_rate += 1;
            }
            return;
        }
        if !p.closed {
            d.too_short += 1;
            return;
        }
        if p.gap_seen {
            d.hr_gap += 1;
            return;
        }
        if p.trusted < t.min_trusted_samples {
            d.too_few_trusted += 1;
            return;
        }
        let (Some(start), Some(end)) = (p.bpm_at_start, p.bpm_at_end) else {
            d.no_heart_rate += 1;
            return;
        };
        if p.source_rejected {
            d.source_not_accepted += 1;
            return;
        }

        let w = RecoveryWindow {
            kind: p.kind,
            start_ms: p.start_ms,
            window_s: p.window_s,
            bpm_at_start: start,
            bpm_at_end: end,
            trusted_samples: p.trusted,
            source: p.source,
        };
        if self.out.stored < MAX_WINDOWS {
            self.out.windows[self.out.stored] = Some(w);
            self.out.stored += 1;
        } else {
            self.out.windows_truncated = true;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::epoch::UNMEASURED;

    fn thresholds(policy: SourcePolicy) -> Thresholds {
        Thresholds::from_recordings(
            UNMEASURED,
            Formulation::FixedWindowDrop { window_s: 30 },
            Formulation::FixedWindowDrop { window_s: 120 },
            3000,
            20,
            1,
            policy,
        )
        .expect("the counts are non-zero")
    }

    /// Feeds one reading a second from `at_ms`, falling linearly to `to_bpm`.
    fn feed(a: &mut RecoveryAnalyser, from_ms: u32, secs: u32, from_bpm: f32, to_bpm: f32, source: HrSource) {
        for i in 0..=secs {
            let f = i as f32 / secs as f32;
            a.on_hr(&HrSample {
                t_ms: from_ms + i * 1000,
                bpm: from_bpm + (to_bpm - from_bpm) * f,
                trust: 2,
                source,
            });
        }
    }

    #[test]
    fn without_a_calibration_nothing_is_reported() {
        let mut a = RecoveryAnalyser::new(Calibration::Absent);
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 60, 180.0, 120.0, HrSource::External);
        assert_eq!(a.finish().unwrap_err(), Unavailable::NotCalibrated);
    }

    #[test]
    fn a_qualifying_rest_reports_the_fall_over_the_stated_window() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rally, 0);
        a.on_state(ActivityState::Rest, 10_000);
        feed(&mut a, 10_000, 30, 180.0, 150.0, HrSource::External);
        a.on_state(ActivityState::Rally, 45_000);
        let out = a.finish().unwrap();
        let w = out.windows().next().expect("the window qualified");
        assert_eq!(w.kind, WindowKind::BetweenRallies);
        assert_eq!(w.window_s, 30);
        assert_eq!(w.bpm_at_start, 180.0);
        assert_eq!(w.bpm_at_end, 150.0);
        assert_eq!(w.drop_bpm(), 30.0);
        assert_eq!(out.discarded.total(), 0);
    }

    #[test]
    fn a_rest_shorter_than_the_window_is_discarded_not_reported_small() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 12, 180.0, 172.0, HrSource::External);
        a.on_state(ActivityState::Rally, 12_000);
        let out = a.finish().unwrap();
        assert_eq!(out.windows().count(), 0);
        assert_eq!(out.discarded.too_short, 1);
    }

    #[test]
    fn a_gap_in_the_stream_discards_the_window_rather_than_bridging_it() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 5, 180.0, 175.0, HrSource::External);
        // Ten seconds of nothing, past the three the calibration allows.
        feed(&mut a, 15_000, 15, 165.0, 150.0, HrSource::External);
        a.on_state(ActivityState::Rally, 40_000);
        let out = a.finish().unwrap();
        assert_eq!(out.windows().count(), 0);
        assert_eq!(out.discarded.hr_gap, 1);
    }

    #[test]
    fn optical_is_discarded_under_an_external_only_policy() {
        let mut a =
            RecoveryAnalyser::new(Calibration::Measured(thresholds(SourcePolicy::ExternalOnly)));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 30, 180.0, 150.0, HrSource::Optical);
        a.on_state(ActivityState::Rally, 40_000);
        let out = a.finish().unwrap();
        assert_eq!(out.windows().count(), 0);
        assert_eq!(out.discarded.source_not_accepted, 1);
    }

    #[test]
    fn a_window_records_the_source_it_came_from() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 30, 170.0, 145.0, HrSource::Optical);
        a.on_state(ActivityState::Rally, 40_000);
        let out = a.finish().unwrap();
        assert_eq!(out.windows().next().unwrap().source, HrSource::Optical);
    }

    #[test]
    fn untrusted_readings_do_not_count_towards_the_window() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        for i in 0..=30u32 {
            a.on_hr(&HrSample {
                t_ms: i * 1000,
                bpm: 180.0 - i as f32,
                trust: if i % 2 == 0 { 2 } else { 0 },
                source: HrSource::External,
            });
        }
        a.on_state(ActivityState::Rally, 40_000);
        let out = a.finish().unwrap();
        assert_eq!(out.windows().count(), 0, "sixteen trusted is under the twenty required");
        assert_eq!(out.discarded.too_few_trusted, 1);
    }

    #[test]
    fn a_rest_with_no_heart_rate_at_all_is_counted_as_such() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        a.on_state(ActivityState::Rally, 60_000);
        let out = a.finish().unwrap();
        assert_eq!(out.discarded.no_heart_rate, 1);
    }

    #[test]
    fn short_and_long_rests_are_never_averaged_together() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 30, 180.0, 170.0, HrSource::External);
        a.on_state(ActivityState::Rally, 40_000);
        a.on_state(ActivityState::OffCourt, 50_000);
        feed(&mut a, 50_000, 120, 175.0, 100.0, HrSource::External);
        a.on_state(ActivityState::Rally, 200_000);
        let out = a.finish().unwrap();
        assert_eq!(out.mean_drop(WindowKind::BetweenRallies), Some(10.0));
        assert_eq!(out.mean_drop(WindowKind::OffCourt), Some(75.0));
        assert_eq!(out.count(WindowKind::BetweenRallies), 1);
        assert_eq!(out.count(WindowKind::OffCourt), 1);
    }

    #[test]
    fn a_kind_with_no_window_reports_none_rather_than_zero() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 30, 180.0, 170.0, HrSource::External);
        a.on_state(ActivityState::Rally, 40_000);
        let out = a.finish().unwrap();
        assert_eq!(out.mean_drop(WindowKind::OffCourt), None);
    }

    #[test]
    fn a_calibration_saying_the_hardware_cannot_measure_it_reports_no_number() {
        let t = Thresholds::from_recordings(
            UNMEASURED,
            Formulation::NotMeasurableOnThisHardware,
            Formulation::FixedWindowDrop { window_s: 120 },
            3000,
            20,
            1,
            SourcePolicy::EitherWithSourceRecorded,
        )
        .unwrap();
        let mut a = RecoveryAnalyser::new(Calibration::Measured(t));
        a.on_state(ActivityState::Rest, 0);
        feed(&mut a, 0, 60, 180.0, 140.0, HrSource::External);
        a.on_state(ActivityState::Rally, 70_000);
        let out = a.finish().unwrap();
        assert_eq!(out.windows().count(), 0);
        assert_eq!(out.discarded.not_measurable, 1);
    }

    #[test]
    fn no_trend_from_fewer_windows_than_the_stated_minimum() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        for i in 0..3u32 {
            let base = i * 100_000;
            a.on_state(ActivityState::Rest, base);
            feed(&mut a, base, 30, 180.0, 160.0, HrSource::External);
            a.on_state(ActivityState::Rally, base + 40_000);
        }
        let out = a.finish().unwrap();
        assert_eq!(out.trend().unwrap_err(), Unavailable::NoQualifyingWindow);
    }

    #[test]
    fn a_session_whose_recovery_degrades_reports_a_negative_change() {
        let mut a = RecoveryAnalyser::new(Calibration::Measured(thresholds(
            SourcePolicy::EitherWithSourceRecorded,
        )));
        for (i, drop) in [30.0f32, 28.0, 12.0, 10.0].iter().enumerate() {
            let base = i as u32 * 100_000;
            a.on_state(ActivityState::Rest, base);
            feed(&mut a, base, 30, 180.0, 180.0 - drop, HrSource::External);
            a.on_state(ActivityState::Rally, base + 40_000);
        }
        let out = a.finish().unwrap();
        let t = out.trend().expect("four windows is enough");
        assert_eq!(t.windows, 4);
        assert_eq!(t.first_half_mean, 29.0);
        assert_eq!(t.second_half_mean, 11.0);
        assert_eq!(t.change_bpm(), -18.0);
    }
}
