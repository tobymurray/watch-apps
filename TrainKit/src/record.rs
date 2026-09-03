//! What one session and one recovery measurement are, on the wire and across
//! the C ABI.
//!
//! These two structs are the whole contract. `include/trainkit.h` mirrors them
//! field for field and `lib.rs`'s fingerprint is what checks the mirror; the
//! JSON schema in `history.rs` is these fields spelled out, and `README.md`
//! documents it as a table with units for whatever reads the file next.
//!
//! Everything here is an *input*. Nothing is a conclusion about a person, and
//! the one derived number that is stored (`hr0_pct_max`) is stored because a
//! reader comparing two measurements needs the intensity they were taken at,
//! not because it means anything alone.

/// Zone buckets: `[0]` is time below zone 1 and `[1..=N]` the zones, so the
/// most buckets is one more than the most zones the watch's ladder reaches.
pub const MAX_ZONE_BUCKETS: usize = 9;

/// Eight is where the kernel's own threshold table stops.
pub const MAX_ZONES: usize = 8;

/// Recoveries kept per session; see `README.md` for what happens past it.
pub const MAX_RECOVERIES: usize = 2;

/// Heart rate sampled every 10 s across the window, `[0]` being the baseline.
pub const CURVE_POINTS: usize = 7;

// -- Triggers -----------------------------------------------------------------

/// Effort ceased because the wearer paused the ride.
pub const TRIGGER_PAUSE: u8 = 1;
/// Reserved: a lap does not imply effort ceased, so nothing produces this.
pub const TRIGGER_LAP: u8 = 2;
/// Reserved: the sensor is released inside `stopTrack()`, so nothing produces
/// this.
pub const TRIGGER_STOP: u8 = 3;

// -- Why a measurement was thrown away ----------------------------------------

pub const DISCARD_NONE: u8 = 0;
/// The watch has no maximum heart rate, so there is no intensity to record.
pub const DISCARD_NO_MAX_HR: u8 = 1;
/// Not enough uninterrupted effort before the window opened.
pub const DISCARD_TOO_SHORT: u8 = 2;
/// Heart rate at cessation was below the intensity this measurement needs.
pub const DISCARD_TOO_EASY: u8 = 3;
/// Heart rate was already falling when the window opened.
pub const DISCARD_ALREADY_FALLING: u8 = 4;
/// Too few trusted readings before the window to tell whether it was falling.
pub const DISCARD_NO_BASELINE_HISTORY: u8 = 5;
/// No trusted reading at the moment effort ceased.
pub const DISCARD_NO_BASELINE: u8 = 6;
/// Too many untrusted seconds inside the window.
pub const DISCARD_DROPOUT: u8 = 7;
/// No trusted reading at the end of the window.
pub const DISCARD_NO_ENDPOINT: u8 = 8;
/// The wearer started pedalling again before the window closed.
pub const DISCARD_EFFORT_RESUMED: u8 = 9;
/// The ride ended before the window closed.
pub const DISCARD_RIDE_ENDED: u8 = 10;
/// The kernel switched sensors part-way through the window.
///
/// MEASURED, over 34 minutes of `HEART_RATE_EX` pulled from this watch
/// (`Squash/Tests/pulled`): 14% of 60 s windows begin and end on different
/// sensors, and where both report at once the two disagree by a median of 2 bpm
/// and a 95th percentile of 16. The falls being measured are 8 to 20 bpm, so a
/// window spanning a switch is partly a difference between instruments -- those
/// windows averaged -2.1 bpm, an apparent rise. Re-measure by differencing
/// `optical_x100` against `external_x100` in any pulled `_hr.csv`.
pub const DISCARD_SOURCE_CHANGED: u8 = 11;

/// `HeartRateEx::Source`, carried through unchanged from the kernel.
pub const HR_SOURCE_NONE: u8 = 0;
pub const HR_SOURCE_OPTICAL: u8 = 1;
pub const HR_SOURCE_EXTERNAL: u8 = 2;

/// One heart-rate recovery measurement, with the context that makes it
/// comparable to another one.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Recovery {
    /// Active seconds into the ride at which effort ceased.
    pub at_active_s: u32,
    /// bpm at cessation.
    pub hr0: u8,
    /// bpm at the end of the window.
    pub hr_end: u8,
    /// Seconds actually spanned, which is not always 60; see `recovery.rs`.
    pub window_s: u8,
    /// Seconds inside the window the sensor was believed.
    pub trusted_s: u8,
    /// `hr0` as a percentage of the watch's maximum heart rate.
    pub hr0_pct_max: u8,
    /// One of the `TRIGGER_*` values.
    pub trigger: u8,
    /// bpm at 0, 10, ... 60 s from `hr0`; 0 where no trusted reading landed.
    pub curve: [u8; CURVE_POINTS],
    /// Which sensor every reading in the window came from; one of
    /// `HR_SOURCE_*`. Constant across the window by construction -- a switch
    /// discards it -- so one byte describes the whole measurement.
    pub source: u8,
    pub reserved: [u8; 2],
}

impl Recovery {
    /// bpm fallen over `window_s`, which is the measurement itself.
    pub fn drop_bpm(&self) -> u8 {
        self.hr0.saturating_sub(self.hr_end)
    }
}

/// One session, as the shared log records it.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Session {
    /// UTC the ride started, and the identity of the entry.
    pub start_utc: u32,
    /// Unpaused seconds.
    pub active_s: u32,
    /// Wall-clock seconds from start to stop.
    pub elapsed_s: u32,
    /// kcal, active, from the app's own model.
    pub kcal: u16,
    /// kJ; 0 = nobody said.
    pub work_kj: u16,
    /// Seconds in each bucket, indexed the same way `zone_floor` is.
    pub zone_s: [u16; MAX_ZONE_BUCKETS],
    /// bpm; `zone_floor[i]` is the lowest heart rate in zone `i + 1`.
    pub zone_floor: [u8; MAX_ZONES],
    /// bpm over the ride.
    pub hr_avg: u8,
    /// bpm.
    pub hr_max: u8,
    /// bpm the watch calls the wearer's maximum; 0 = it has none.
    pub hr_max_setting: u8,
    /// kg the calorie model used.
    pub weight_kg: u8,
    /// Zones the ladder had, 0 = none set.
    pub zone_count: u8,
    /// Entries of `recoveries` that are filled.
    pub recovery_count: u8,
    /// Measurements that were taken but did not fit; see `README.md`.
    pub recoveries_dropped: u8,
    pub reserved: [u8; 3],
    pub recoveries: [Recovery; MAX_RECOVERIES],
}

impl Default for Session {
    fn default() -> Self {
        Session {
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
            recoveries: [Recovery::default(); MAX_RECOVERIES],
        }
    }
}

impl Session {
    pub fn recoveries(&self) -> &[Recovery] {
        let n = (self.recovery_count as usize).min(MAX_RECOVERIES);
        &self.recoveries[..n]
    }

    /// Buckets that mean something for this ladder, which is one more than the
    /// zones the ride actually had.
    pub fn zone_bucket_count(&self) -> usize {
        if self.zone_count == 0 {
            0
        } else {
            (self.zone_count as usize).min(MAX_ZONES) + 1
        }
    }
}

const _: () = assert!(core::mem::size_of::<Recovery>() == 20);
const _: () = assert!(core::mem::align_of::<Recovery>() == 4);
const _: () = assert!(core::mem::size_of::<Session>() == 92);
const _: () = assert!(core::mem::align_of::<Session>() == 4);
