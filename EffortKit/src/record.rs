//! What one session and one recovery measurement are, on the wire.
//!
//! These two structs are the cross-app contract. Each app's shim mirrors them
//! field for field in its own C header and hashes both layouts into a
//! fingerprint; the JSON schema in [`crate::history`] is these fields spelled
//! out, and `README.md` documents it as a table with units for whatever reads
//! the file next.
//!
//! Everything here is an *input*. Nothing is a conclusion about a person, and
//! the one derived number stored (`hr0_pct_max`) is stored because a reader
//! comparing two measurements needs the intensity they were taken at, not
//! because it means anything alone.

/// Zone buckets: `[0]` is time below zone 1 and `[1..=N]` the zones, so the
/// most buckets is one more than the most zones the watch's ladder reaches.
pub const MAX_ZONE_BUCKETS: usize = 9;

/// Eight is where the kernel's own threshold table stops.
pub const MAX_ZONES: usize = 8;

/// Recovery measurements the cross-app log keeps per session.
///
/// Two, and the *newest* are kept. The end-of-session cessation is the one that
/// happens every session, so it is the one comparable across them. A session's
/// full interior — every window it measured — lives in the app's own profile,
/// not here; see `README.md` § "Two files, two jobs".
pub const MAX_RECOVERIES: usize = 2;

/// Heart rate sampled every [`CURVE_STEP_S`] seconds across the window.
pub const CURVE_POINTS: usize = 7;

/// Seconds between curve points.
///
/// A fixed grid rather than a fraction of the window, so slot `i` is the same
/// number of seconds after cessation whatever the window length, and a 30 s
/// window and a 60 s window can be laid against each other. A window shorter
/// than `CURVE_STEP_S * (CURVE_POINTS - 1)` simply leaves the later slots empty.
pub const CURVE_STEP_S: u32 = 10;

/// One heart-rate recovery measurement, with the context that makes it
/// comparable to another one.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Recovery {
    /// Active seconds into the session that effort ceased.
    pub at_active_s: u32,
    /// bpm at cessation.
    pub hr0: u8,
    /// bpm at the end of the window.
    pub hr_end: u8,
    /// Seconds actually spanned; not always the calibration's window length.
    pub window_s: u8,
    /// Seconds inside the window the sensor was believed.
    pub trusted_s: u8,
    /// `hr0` as a percentage of the watch's maximum; 0 when it has none.
    pub hr0_pct_max: u8,
    /// Which cessation opened it, one of [`crate::window::WindowKind`]'s codes.
    pub kind: u8,
    /// bpm at 0, 10, ... seconds from `hr0`; 0 where no trusted reading landed.
    ///
    /// The input any later curve fit would need, so a derivation can change
    /// without orphaning the history. 0 is a hole, not a reading of zero.
    pub curve: [u8; CURVE_POINTS],
    /// Which sensor every reading came from; constant across the window by
    /// construction, because a switch discards the measurement.
    pub source: u8,
    /// Padding, so the struct's size is not a compiler's choice.
    pub reserved: [u8; 2],
}

impl Recovery {
    /// A measurement with nothing in it, usable in a `const`.
    pub const EMPTY: Self = Self {
        at_active_s: 0,
        hr0: 0,
        hr_end: 0,
        window_s: 0,
        trusted_s: 0,
        hr0_pct_max: 0,
        kind: 0,
        curve: [0; CURVE_POINTS],
        source: 0,
        reserved: [0; 2],
    };

    /// The fall in bpm. Saturating, because a window that ended higher than it
    /// started is a rise and this type carries falls.
    pub const fn drop_bpm(&self) -> u8 {
        self.hr0.saturating_sub(self.hr_end)
    }
}

impl Default for Recovery {
    fn default() -> Self {
        Self::EMPTY
    }
}

/// One session, as the cross-app log records it.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Session {
    /// Unix seconds the session started, and the entry's identity.
    pub start_utc: u32,
    /// Unpaused seconds.
    pub active_s: u32,
    /// Wall clock, start to stop.
    pub elapsed_s: u32,
    /// kcal, active.
    pub kcal: u16,
    /// kJ; 0 = nobody said, and the file omits the field rather than writing 0.
    pub work_kj: u16,
    /// `[0]` is time below zone 1, `[i]` time in zone `i`.
    pub zone_s: [u16; MAX_ZONE_BUCKETS],
    /// bpm; `[i]` is the lowest heart rate in zone `i+1`.
    pub zone_floor: [u8; MAX_ZONES],
    /// bpm, over the session.
    pub hr_avg: u8,
    /// bpm, over the session.
    pub hr_max: u8,
    /// bpm the watch calls the wearer's maximum; 0 = it has none.
    pub hr_max_setting: u8,
    /// kg the calorie model used.
    pub weight_kg: u8,
    /// Zones the ladder had; 0 = none set.
    pub zone_count: u8,
    /// Filled entries of `recoveries`.
    pub recovery_count: u8,
    /// Measured, but did not fit in `recoveries`.
    pub recoveries_dropped: u8,
    /// Padding, so the struct's size is not a compiler's choice.
    pub reserved: [u8; 3],
    /// The newest measurements, oldest first.
    pub recoveries: [Recovery; MAX_RECOVERIES],
    /// Why windows produced nothing, by reason.
    ///
    /// Part of the record rather than a diagnostic: a session that measured
    /// nothing should be able to say why a year later, and a text log that the
    /// field test tells you to delete cannot.
    pub discarded: DiscardCounts,
}

/// Per-reason discard counts, on the wire.
///
/// Mirrors [`crate::window::Discarded`] field for field. Kept as its own
/// `#[repr(C)]` struct so a shim can hash one offset rather than fourteen.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct DiscardCounts {
    /// This app does not measure that kind of window.
    pub not_calibrated: u16,
    /// Not measurable on this hardware.
    pub not_measurable: u16,
    /// The watch has no maximum heart rate.
    pub no_max_hr: u16,
    /// Not enough uninterrupted effort first.
    pub too_short: u16,
    /// Below the intensity the measurement needs.
    pub too_easy: u16,
    /// Already falling when the window opened.
    pub already_falling: u16,
    /// Too little trusted history to tell.
    pub no_baseline_history: u16,
    /// No trusted reading at cessation.
    pub no_baseline: u16,
    /// Too many untrusted seconds inside.
    pub dropout: u16,
    /// No trusted reading at the end.
    pub no_endpoint: u16,
    /// Effort restarted first.
    pub effort_resumed: u16,
    /// The session ended first.
    pub session_ended: u16,
    /// The sensor changed part-way through.
    pub source_changed: u16,
    /// The source is not one the policy accepts.
    pub source_not_accepted: u16,
}

impl DiscardCounts {
    /// Nothing discarded, usable in a `const`.
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
}

impl From<crate::window::Discarded> for DiscardCounts {
    fn from(d: crate::window::Discarded) -> Self {
        Self {
            not_calibrated: d.not_calibrated,
            not_measurable: d.not_measurable,
            no_max_hr: d.no_max_hr,
            too_short: d.too_short,
            too_easy: d.too_easy,
            already_falling: d.already_falling,
            no_baseline_history: d.no_baseline_history,
            no_baseline: d.no_baseline,
            dropout: d.dropout,
            no_endpoint: d.no_endpoint,
            effort_resumed: d.effort_resumed,
            session_ended: d.session_ended,
            source_changed: d.source_changed,
            source_not_accepted: d.source_not_accepted,
        }
    }
}

impl Session {
    /// A session with nothing in it, usable in a `const`.
    pub const EMPTY: Self = Self {
        start_utc: 0,
        active_s: 0,
        elapsed_s: 0,
        kcal: 0,
        work_kj: 0,
        zone_s: [0; MAX_ZONE_BUCKETS],
        zone_floor: [0; MAX_ZONES],
        hr_avg: 0,
        hr_max: 0,
        hr_max_setting: 0,
        weight_kg: 0,
        zone_count: 0,
        recovery_count: 0,
        recoveries_dropped: 0,
        reserved: [0; 3],
        recoveries: [Recovery::EMPTY; MAX_RECOVERIES],
        discarded: DiscardCounts::NONE,
    };

    /// The measurements this session actually carries.
    pub fn recoveries(&self) -> &[Recovery] {
        let n = (self.recovery_count as usize).min(MAX_RECOVERIES);
        &self.recoveries[..n]
    }

    /// Add a measurement, keeping the newest [`MAX_RECOVERIES`].
    ///
    /// The oldest is dropped and counted, because the cessation that happens
    /// every session is the last one and that is the comparable one.
    pub fn add_recovery(&mut self, r: Recovery) {
        let n = self.recovery_count as usize;
        if n < MAX_RECOVERIES {
            self.recoveries[n] = r;
            self.recovery_count = (n + 1) as u8;
        } else {
            self.recoveries.copy_within(1.., 0);
            self.recoveries[MAX_RECOVERIES - 1] = r;
            self.recoveries_dropped = self.recoveries_dropped.saturating_add(1);
        }
    }
}

impl Default for Session {
    fn default() -> Self {
        Self::EMPTY
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rec(hr0: u8) -> Recovery {
        Recovery { hr0, hr_end: hr0 - 10, ..Recovery::EMPTY }
    }

    #[test]
    fn the_newest_measurements_are_the_ones_kept() {
        let mut s = Session::EMPTY;
        for hr0 in [170, 171, 172] {
            s.add_recovery(rec(hr0));
        }
        assert_eq!(s.recovery_count as usize, MAX_RECOVERIES);
        assert_eq!(s.recoveries_dropped, 1);
        assert_eq!(s.recoveries()[0].hr0, 171, "the oldest went, not the newest");
        assert_eq!(s.recoveries()[1].hr0, 172);
    }

    #[test]
    fn a_window_that_ended_higher_reports_no_fall_rather_than_wrapping() {
        let r = Recovery { hr0: 100, hr_end: 116, ..Recovery::EMPTY };
        assert_eq!(r.drop_bpm(), 0);
    }
}
