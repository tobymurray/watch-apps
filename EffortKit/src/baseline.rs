//! What normal looks like for one wearer, kept across sessions.
//!
//! Median and median absolute deviation over a rolling window, not mean and
//! standard deviation: one two-hour session of threes, one twenty-minute
//! knock-up, or one session where the strap fell off must not redefine normal.
//! Every comparison this produces is against the wearer's own past and nothing
//! else.

use crate::Unavailable;

/// Sessions kept in the rolling window.
///
/// At one to three sessions a week this is roughly two months — long enough
/// that one bad session is outvoted, short enough to follow a training block
/// rather than average it away.
pub const WINDOW: usize = 20;

/// Sessions required before any comparison is offered.
///
/// Below three the median absolute deviation is degenerate; at five a single
/// outlier cannot move the median past its neighbours. Under this the raw
/// measurement is shown and the comparison is reported as
/// [`Unavailable::WarmingUp`].
pub const MIN_SESSIONS_FOR_COMPARISON: u16 = 5;

/// The most one session may move a baseline, as a fraction of its current value.
///
/// A median over a rolling window already resists a single session, so this is
/// the second lock rather than the first. Ten percent means a genuine halving
/// takes at least seven sessions (0.9^7 = 0.48) — longer than any artefact
/// lasts and shorter than a training block, so a real change still arrives.
pub const MAX_BASELINE_STEP_FRAC: f32 = 0.10;

/// The 0.75 quantile of the standard normal, which scales MAD to be comparable
/// with a standard deviation.
const MAD_TO_SIGMA: f32 = 0.674_5;

/// How one measurement sits against the wearer's own history.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Comparison {
    /// The wearer's baseline for this metric: the window's median, moved by at
    /// most [`MAX_BASELINE_STEP_FRAC`] per session.
    pub median: f32,
    /// Median absolute deviation about that median.
    pub mad: f32,
    /// Robust z-score, or `None` when the MAD is zero and no spread exists to
    /// measure against.
    pub z: Option<f32>,
    /// Sessions the comparison rests on.
    pub sessions: u16,
}

/// A rolling, robust baseline for one metric.
#[derive(Clone, Copy, Debug)]
pub struct RollingBaseline {
    values: [f32; WINDOW],
    len: usize,
    next: usize,
    baseline: Option<f32>,
}

impl Default for RollingBaseline {
    fn default() -> Self {
        Self::new()
    }
}

impl RollingBaseline {
    /// An empty baseline.
    pub const fn new() -> Self {
        Self { values: [0.0; WINDOW], len: 0, next: 0, baseline: None }
    }

    /// Sessions currently in the window.
    pub const fn sessions(&self) -> u16 {
        self.len as u16
    }

    /// The values in the window, oldest first is not guaranteed.
    pub fn values(&self) -> &[f32] {
        &self.values[..self.len]
    }

    /// Rebuild from stored values, dropping anything past the window.
    pub fn from_values(values: &[f32]) -> Self {
        let mut b = Self::new();
        for v in values.iter().take(WINDOW) {
            b.values[b.len] = *v;
            b.len += 1;
        }
        b.next = b.len % WINDOW;
        b.baseline = b.raw_median();
        b
    }

    /// Add one session's value, replacing the oldest once the window is full.
    ///
    /// The reported baseline moves by at most [`MAX_BASELINE_STEP_FRAC`] of its
    /// previous value, so one session cannot redefine the wearer even if it
    /// somehow displaced half the window.
    pub fn push(&mut self, value: f32) {
        self.values[self.next] = value;
        self.next = (self.next + 1) % WINDOW;
        if self.len < WINDOW {
            self.len += 1;
        }

        let Some(target) = self.raw_median() else {
            return;
        };
        self.baseline = Some(match self.baseline {
            None => target,
            Some(prev) => {
                let limit = libm::fabsf(prev) * MAX_BASELINE_STEP_FRAC;
                let step = target - prev;
                if libm::fabsf(step) <= limit {
                    target
                } else if step > 0.0 {
                    prev + limit
                } else {
                    prev - limit
                }
            }
        });
    }

    /// The wearer's baseline, bounded by [`MAX_BASELINE_STEP_FRAC`] per session.
    pub const fn baseline(&self) -> Option<f32> {
        self.baseline
    }

    /// The median of the window, ignoring the per-session bound.
    pub fn median(&self) -> Option<f32> {
        self.raw_median()
    }

    /// Median absolute deviation about the median.
    pub fn mad(&self) -> Option<f32> {
        let m = self.raw_median()?;
        let mut d = [0.0f32; WINDOW];
        for (slot, v) in d[..self.len].iter_mut().zip(&self.values[..self.len]) {
            *slot = libm::fabsf(v - m);
        }
        Some(median_of(&mut d[..self.len]))
    }

    /// Place one measurement against the wearer's history.
    ///
    /// Against the bounded baseline rather than the bare median, so both locks
    /// -- the median's own resistance and the per-session step limit -- are in
    /// front of every comparison a wearer is shown.
    pub fn compare(&self, value: f32) -> Result<Comparison, Unavailable> {
        if self.sessions() < MIN_SESSIONS_FOR_COMPARISON {
            return Err(Unavailable::WarmingUp {
                have: self.sessions(),
                need: MIN_SESSIONS_FOR_COMPARISON,
            });
        }
        // The bounded baseline, not the raw median: the per-session step limit
        // exists to stop one session redefining the wearer, and a comparison
        // taken against the raw median is a comparison the limit never reached.
        let median = self
            .baseline
            .or_else(|| self.raw_median())
            .ok_or(Unavailable::NoQualifyingWindow)?;
        let mad = self.mad().unwrap_or(0.0);
        let z = (mad > 0.0).then(|| MAD_TO_SIGMA * (value - median) / mad);
        Ok(Comparison { median, mad, z, sessions: self.sessions() })
    }

    fn raw_median(&self) -> Option<f32> {
        if self.len == 0 {
            return None;
        }
        let mut buf = [0.0f32; WINDOW];
        buf[..self.len].copy_from_slice(&self.values[..self.len]);
        Some(median_of(&mut buf[..self.len]))
    }
}

fn median_of(v: &mut [f32]) -> f32 {
    // Insertion sort: WINDOW is 20, and it needs no scratch space.
    for i in 1..v.len() {
        let mut j = i;
        while j > 0 && v[j - 1] > v[j] {
            v.swap(j - 1, j);
            j -= 1;
        }
    }
    let n = v.len();
    if n % 2 == 1 {
        v[n / 2]
    } else {
        (v[n / 2 - 1] + v[n / 2]) / 2.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn filled(vs: &[f32]) -> RollingBaseline {
        let mut b = RollingBaseline::new();
        for v in vs {
            b.push(*v);
        }
        b
    }

    #[test]
    fn no_comparison_before_the_warm_up_threshold() {
        let b = filled(&[10.0, 11.0, 12.0]);
        assert_eq!(
            b.compare(11.0),
            Err(Unavailable::WarmingUp { have: 3, need: MIN_SESSIONS_FOR_COMPARISON })
        );
    }

    #[test]
    fn a_comparison_arrives_exactly_at_the_threshold() {
        let b = filled(&[10.0, 11.0, 12.0, 13.0, 14.0]);
        let c = b.compare(12.0).expect("five sessions is enough");
        assert_eq!(c.sessions, 5);
        assert_eq!(c.median, 12.0);
    }

    #[test]
    fn one_wild_session_does_not_move_the_median() {
        let steady = filled(&[20.0, 21.0, 19.0, 20.0, 21.0, 20.0, 19.0]);
        let before = steady.median().unwrap();
        let mut after = steady;
        after.push(400.0);
        assert!(
            libm::fabsf(after.median().unwrap() - before) < 1.5,
            "median moved from {before} to {}",
            after.median().unwrap()
        );
    }

    #[test]
    fn the_baseline_step_is_bounded_even_when_the_median_jumps() {
        // A window rebuilt entirely from a far-away value: the median moves at
        // once, the reported baseline may not.
        let mut b = filled(&[100.0; WINDOW]);
        assert_eq!(b.baseline(), Some(100.0));
        for _ in 0..WINDOW {
            b.push(10.0);
        }
        assert_eq!(b.median(), Some(10.0));
        let bounded = b.baseline().unwrap();
        assert!(bounded > 10.0, "the bound must not have been skipped: {bounded}");
    }

    #[test]
    fn the_bound_still_lets_a_real_change_arrive() {
        let mut b = filled(&[100.0; WINDOW]);
        for _ in 0..(WINDOW * 4) {
            b.push(50.0);
        }
        let settled = b.baseline().unwrap();
        assert!(
            libm::fabsf(settled - 50.0) < 1.0,
            "a sustained change must converge, not be blocked: {settled}"
        );
    }

    #[test]
    fn the_window_forgets_the_oldest_session() {
        let mut b = RollingBaseline::new();
        for i in 0..(WINDOW + 5) {
            b.push(i as f32);
        }
        assert_eq!(b.sessions(), WINDOW as u16);
        assert!(b.values().iter().all(|v| *v >= 5.0));
    }

    #[test]
    fn a_flat_history_has_no_spread_to_compare_against() {
        let b = filled(&[30.0; 8]);
        let c = b.compare(45.0).unwrap();
        assert_eq!(c.mad, 0.0);
        assert_eq!(c.z, None);
    }

    #[test]
    fn a_value_above_the_median_scores_positive() {
        let b = filled(&[10.0, 12.0, 14.0, 16.0, 18.0, 20.0, 22.0]);
        let c = b.compare(30.0).unwrap();
        assert!(c.z.unwrap() > 0.0);
    }

    #[test]
    fn a_comparison_is_taken_against_the_bounded_baseline_not_the_raw_median() {
        // Twenty steady sessions, then a window replaced wholesale by a far
        // value. The median moves at once; the baseline may not, and it is the
        // baseline a wearer is compared against.
        let mut b = filled(&[100.0; WINDOW]);
        for _ in 0..WINDOW {
            b.push(10.0);
        }
        assert_eq!(b.median(), Some(10.0));
        let c = b.compare(10.0).expect("the window is full");
        assert_eq!(
            c.median,
            b.baseline().unwrap(),
            "compare() must read the bounded baseline"
        );
        assert!(c.median > 10.0, "the step bound must be in front of the comparison");
    }

    #[test]
    fn rebuilding_from_stored_values_reproduces_the_median() {
        let b = filled(&[3.0, 9.0, 5.0, 7.0, 1.0]);
        let again = RollingBaseline::from_values(b.values());
        assert_eq!(again.median(), b.median());
        assert_eq!(again.sessions(), b.sessions());
    }
}
