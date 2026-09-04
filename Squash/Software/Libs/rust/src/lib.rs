//! The Squash Service's half of [`effortkit`], as a C ABI.
//!
//! The Service owns the clock, the sensors and the filesystem; this owns the
//! arithmetic. Everything here is a thin shell — the engine is in `EffortKit`
//! and so is the reasoning, in its `README.md`.
//!
//! **Nothing this returns is displayable today.** There is no calibration
//! constant in this repository, so [`squash_engine_calibration`] reports
//! `CALIBRATION_ABSENT` and the segmentation and recovery fields of a finished
//! session are zero with `segmented` false. The heart-rate fields are real,
//! because they need no threshold.

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

use core::cell::UnsafeCell;

use effortkit::baseline::RollingBaseline;
use effortkit::epoch::{EpochAccumulator, ImuSample};
use effortkit::profile::{Load, Profile};
use effortkit::hr::HrSource;
use effortkit::window::{
    Calibration as RecoveryCalibration, Detector, WindowKind,
};
use effortkit::segment::{Calibration as SegmentCalibration, Segmenter};
use effortkit::session::{Metric, SessionRecord};
use effortkit::Unavailable;

#[cfg(not(feature = "std"))]
extern "C" {
    fn squash_engine_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic stops the Service silently, and a session in progress
/// is lost with no record of why.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"squash_engine panic";
    unsafe { squash_engine_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// Referenced by the pre-built `core` for host targets, which is compiled to
/// unwind even though every profile here aborts. Never called: the panic
/// handler above does not return.
#[cfg(not(feature = "std"))]
#[no_mangle]
pub extern "C" fn rust_eh_personality() {}

// ------------------------------------------------------------------ state
//
// Static rather than heap because the Service has no allocator, and unguarded
// because apps on this platform do not create threads: all Service work happens
// inside one blocking message loop, so there is no second caller to race with.

struct Single<T>(UnsafeCell<T>);

// SAFETY: see above. One thread, one message loop, no reentrancy.
unsafe impl<T> Sync for Single<T> {}

impl<T> Single<T> {
    const fn new(v: T) -> Self {
        Self(UnsafeCell::new(v))
    }

    #[allow(clippy::mut_from_ref)]
    fn get(&self) -> &mut T {
        // SAFETY: as above.
        unsafe { &mut *self.0.get() }
    }
}

struct Session {
    /// False before the first `begin()` and after `finish()`, which is what
    /// `Option<Session>` used to say before holding one by value became a
    /// stack overflow.
    running: bool,
    epochs: EpochAccumulator,
    segmenter: Segmenter,
    recovery: Detector,
    /// The state the segmenter last reported, so a transition is a transition.
    last_state: Option<effortkit::segment::ActivityState>,
    /// What this session's windows measured.
    measured: SessionRecord,
    /// Whether a trusted reading arrived during the second being accumulated.
    hr_trusted_this_s: bool,
    /// The source of that reading.
    hr_source_this_s: HrSource,
    hr_sum: f32,
    hr_count: u32,
    hr_max: f32,
    hr_covered_ms: u32,
    last_hr_ms: u32,
    have_hr: bool,
    optical: u32,
    external: u32,
    last_hr_bpm: f32,
}

impl Session {
    /// `const`, and never called anywhere but the `static` below.
    ///
    /// Building one as a value and moving it into place is a stack overflow,
    /// and it is not a subtle one: the watch took a `STKOF` UsageFault
    /// (`CFSR=0x00100000`) in `Squash.SRV` the first time an activity was
    /// started, on the line after the three recording sinks were opened. Host
    /// tests cannot see it -- a host thread has an 8 MB stack -- which is why
    /// `a_session_starts_within_the_services_stack` runs on a 10 KiB one, and
    /// `a_session_cannot_be_built_on_the_services_stack` keeps the size.
    const fn new() -> Self {
        Self {
            running: false,
            epochs: EpochAccumulator::new(),
            segmenter: Segmenter::new(SegmentCalibration::Absent),
            recovery: Detector::new(RecoveryCalibration::absent()),
            last_state: None,
            measured: SessionRecord::EMPTY,
            hr_trusted_this_s: false,
            hr_source_this_s: HrSource::Unknown,
            hr_sum: 0.0,
            hr_count: 0,
            hr_max: 0.0,
            hr_covered_ms: 0,
            last_hr_ms: 0,
            have_hr: false,
            optical: 0,
            external: 0,
            last_hr_bpm: 0.0,
        }
    }


    /// One epoch has closed, so one second of the session has passed.
    ///
    /// Driven by the IMU clock rather than by heart-rate samples, because the
    /// detector has to see a second in which no reading arrived. A sample-driven
    /// feed cannot: with nothing to fire on it cannot tell a sensor that stopped
    /// from a rest that ended, and reports the first as the second.
    fn tick(&mut self, epoch_index: u32) {
        let bpm = if self.hr_trusted_this_s { self.last_hr_bpm } else { 0.0 };
        self.recovery.second(
            epoch_index as i64,
            bpm,
            self.hr_trusted_this_s,
            self.hr_source_this_s,
            epoch_index,
        );
        self.drain_windows();
        self.hr_trusted_this_s = false;
        self.hr_source_this_s = HrSource::Unknown;
    }

    /// Tell the detector what the segmenter now thinks the wearer is doing.
    ///
    /// A rally is effort, so it resumes; anything else is a cessation of the
    /// kind that state names.
    fn on_activity(&mut self, state: effortkit::segment::ActivityState) {
        use effortkit::segment::ActivityState as A;
        if self.last_state == Some(state) {
            return;
        }
        self.last_state = Some(state);
        match state {
            A::Rally => {
                self.recovery.resume();
            }
            A::Rest => {
                self.recovery.cease(WindowKind::BetweenRallies);
            }
            A::OffCourt => {
                self.recovery.cease(WindowKind::OffCourt);
            }
        }
        self.drain_windows();
    }

    /// Move any completed measurement out of the detector and into the session.
    fn drain_windows(&mut self) {
        while let Some(w) = self.recovery.take() {
            self.measured.add_window(w);
        }
    }

    /// Ready this session for a new activity, touching nothing large.
    fn reset(&mut self) {
        self.epochs = EpochAccumulator::new();
        self.segmenter.reset(SegmentCalibration::Absent);
        self.recovery = Detector::new(RecoveryCalibration::absent());
        self.last_state = None;
        self.measured = SessionRecord::EMPTY;
        self.hr_trusted_this_s = false;
        self.hr_source_this_s = HrSource::Unknown;
        self.hr_sum = 0.0;
        self.hr_count = 0;
        self.hr_max = 0.0;
        self.hr_covered_ms = 0;
        self.last_hr_ms = 0;
        self.have_hr = false;
        self.optical = 0;
        self.external = 0;
        self.last_hr_bpm = 0.0;
        self.running = false;
    }
}

/// Live for the life of the process, in BSS rather than on any stack.
static SESSION: Single<Session> = Single::new(Session::new());

/// The watch Service's stack, from `una_app_build_service`'s default of
/// `10*1024`. Nothing here may put a session-sized object on it.
pub const SERVICE_STACK_BYTES: usize = 10 * 1024;
static PROFILE: Single<Profile> = Single::new(Profile::empty());

// ------------------------------------------------------------------ ABI types

/// What one session measured, as the Service sees it.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SquashSessionRecord {
    /// Unix seconds the session started.
    pub started_utc: u32,
    /// Seconds the activity was running, pauses excluded.
    pub active_s: u32,
    /// Seconds a trusted heart rate was available. Reported whatever the
    /// sample count, unlike the means below, because it is what says whether
    /// they are missing for want of data.
    pub hr_covered_s: u32,
    /// Rallies the segmenter found; zero when uncalibrated.
    pub rally_count: u32,
    /// Seconds in rallies.
    pub rally_s: u32,
    /// Seconds resting between rallies.
    pub rest_s: u32,
    /// Seconds off court.
    pub off_court_s: u32,
    /// Mean trusted heart rate, bpm; 0 below `MIN_TRUSTED_HR_SAMPLES`.
    pub hr_mean: f32,
    /// Highest trusted heart rate, bpm; 0 below `MIN_TRUSTED_HR_SAMPLES`.
    pub hr_max: f32,
    /// Mean fall across qualifying rests between rallies, bpm.
    pub recovery_short_mean: f32,
    /// Mean fall across qualifying off-court rests, bpm.
    pub recovery_long_mean: f32,
    /// Qualifying rests between rallies.
    pub recovery_short_n: u16,
    /// Qualifying off-court rests.
    pub recovery_long_n: u16,
    /// The source that supplied most of the session: 0 unknown, 1 optical,
    /// 2 external. A majority, so it hides a strap that fed part of it —
    /// which is what `hr_external_s` is for.
    pub hr_source: u8,
    /// Zero when no calibration existed, in which case every segmentation and
    /// recovery field above is zero because nothing ran, not because nothing
    /// happened.
    pub segmented: u8,
    /// Recovery windows discarded, all reasons together.
    pub discarded_windows: u16,
    /// Trusted readings that came from a chest strap.
    ///
    /// Separate from `hr_source` because a strap that linked before the session
    /// and dropped during it is a majority-optical session that nonetheless had
    /// a strap: measured on 2026-09-03, external for 118 of 707 readings, which
    /// `hr_source` alone reported as plain optical.
    pub hr_external_readings: u32,
}

/// How one measurement sits against the wearer's own history.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SquashComparison {
    /// The wearer's median for the metric.
    pub median: f32,
    /// Median absolute deviation about it.
    pub mad: f32,
    /// Robust z-score, meaningful only when `has_z` is 1.
    pub z: f32,
    /// Sessions the comparison rests on.
    pub sessions: u16,
    /// 0 when the MAD is zero and there is no spread to measure against.
    pub has_z: u8,
    /// Padding, so the struct's size is not a compiler's choice.
    pub reserved: u8,
}

/// No recording has set any threshold, so nothing is segmented or measured.
pub const CALIBRATION_ABSENT: u32 = 0;
/// A named recording set the thresholds.
pub const CALIBRATION_MEASURED: u32 = 1;

/// The comparison succeeded.
pub const COMPARE_OK: u32 = 0;
/// Fewer sessions than the baseline needs.
pub const COMPARE_WARMING_UP: u32 = 1;
/// No threshold, so the metric has no value to compare.
pub const COMPARE_NOT_CALIBRATED: u32 = 2;
/// No session has contributed a value for this metric.
pub const COMPARE_NO_DATA: u32 = 3;

// ------------------------------------------------------------------ session

/// Start a session. Any session in progress is discarded.
#[no_mangle]
pub extern "C" fn squash_engine_begin() {
    let s = SESSION.get();
    s.reset();
    s.running = true;
}

/// Feed one IMU sample, in raw sensor LSB, on the session's own clock.
///
/// # Safety
/// `axes` must point at six `int16_t`: ax, ay, az, gx, gy, gz.
#[no_mangle]
pub unsafe extern "C" fn squash_engine_on_imu(t_ms: u32, axes: *const i16) {
    let s = SESSION.get();
    if !s.running || axes.is_null() {
        return;
    }
    let a = core::slice::from_raw_parts(axes, 6);
    let sample = ImuSample { ax: a[0], ay: a[1], az: a[2], gx: a[3], gy: a[4], gz: a[5] };
    if let Some(e) = s.epochs.push(t_ms, &sample) {
        let hr = s.have_hr.then_some(s.last_hr_bpm);
        s.segmenter.push(&e, hr);
        if let Ok(state) = s.segmenter.state() {
            s.on_activity(state);
        }
        s.tick(e.index);
    }
}

/// Feed one heart-rate reading on the session's own clock.
#[no_mangle]
pub extern "C" fn squash_engine_on_hr(t_ms: u32, bpm: f32, trust: u8, source: u8) {
    let s = SESSION.get();
    if !s.running {
        return;
    }
    let source = match source {
        1 => HrSource::Optical,
        2 => HrSource::External,
        _ => HrSource::Unknown,
    };
    if trust == 0 {
        return;
    }
    if s.have_hr {
        // Coverage is the span a trusted reading actually spoke for, so a gap
        // in the stream reduces it rather than being counted as covered.
        let gap = t_ms.wrapping_sub(s.last_hr_ms);
        if gap <= MAX_COVERAGE_GAP_MS {
            s.hr_covered_ms = s.hr_covered_ms.saturating_add(gap);
        }
    }
    s.have_hr = true;
    s.hr_trusted_this_s = true;
    s.hr_source_this_s = source;
    s.last_hr_ms = t_ms;
    s.last_hr_bpm = bpm;
    s.hr_sum += bpm;
    s.hr_count += 1;
    if bpm > s.hr_max {
        s.hr_max = bpm;
    }
    match source {
        HrSource::Optical => s.optical += 1,
        HrSource::External => s.external += 1,
        HrSource::Unknown => {}
    }
}

/// Fewest trusted readings before a heart rate is reported at all.
///
/// Matches the gate `Service.cpp` already applies before writing heart rate to
/// the FIT file (`mHrCounter.getCurrent() > 20`), so the two numbers the same
/// app produces about the same session agree. Without it a 5-second activity
/// reported a mean of 68.00 bpm from five readings while the app's own summary
/// said 0 -- measured on the 2026-09-03 smoke recording, and exactly the
/// plausible-looking number this engine exists not to produce.
const MIN_TRUSTED_HR_SAMPLES: u32 = 21;

/// Longest gap a single reading is taken to have spoken for.
///
/// The sensor delivers at 1 Hz, so five seconds is five missed readings — past
/// that the stream is broken rather than sparse, and counting it as covered
/// would let a session whose strap fell off pass the admission gate.
const MAX_COVERAGE_GAP_MS: u32 = 5000;

/// Close the session and fill `out` with what it measured.
///
/// # Safety
/// `out` must point at a writable `SquashSessionRecord`.
#[no_mangle]
pub unsafe extern "C" fn squash_engine_finish(
    started_utc: u32,
    active_s: u32,
    out: *mut SquashSessionRecord,
) {
    if out.is_null() {
        return;
    }
    let mut r = SquashSessionRecord { started_utc, active_s, ..Default::default() };

    let s = SESSION.get();
    if s.running {
        s.running = false;
        if let Some(e) = s.epochs.flush() {
            let hr = s.have_hr.then_some(s.last_hr_bpm);
            s.segmenter.push(&e, hr);
        }
        r.hr_external_readings = s.external;
        if s.hr_count > 0 {
            // Coverage and source are reported whatever the count: they are what
            // say whether a missing mean is missing for want of data, and which
            // sensor was not providing it.
            // Clamped to the active time the caller reports, because coverage
            // greater than the session is not a number: it means the feed was
            // running when the activity was not. The Service gates on ACTIVE
            // to prevent it; this makes the invariant hold even when a caller
            // does not.
            r.hr_covered_s = (s.hr_covered_ms / 1000).min(active_s);
            r.hr_source = if s.external >= s.optical && s.external > 0 {
                2
            } else if s.optical > 0 {
                1
            } else {
                0
            };
        }
        if s.hr_count >= MIN_TRUSTED_HR_SAMPLES {
            r.hr_mean = s.hr_sum / s.hr_count as f32;
            r.hr_max = s.hr_max;
        }
        if let Ok(seg) = s.segmenter.finish_in_place() {
            r.segmented = 1;
            r.rally_count = seg.rally_count;
            r.rally_s = seg.rally_epochs;
            r.rest_s = seg.rest_epochs;
            r.off_court_s = seg.off_court_epochs;
        }
        s.recovery.end();
        s.drain_windows();
        r.recovery_short_mean = s.measured.mean_drop(WindowKind::BetweenRallies).unwrap_or(0.0);
        r.recovery_short_n = s.measured.window_count_of(WindowKind::BetweenRallies);
        r.recovery_long_mean = s.measured.mean_drop(WindowKind::OffCourt).unwrap_or(0.0);
        r.recovery_long_n = s.measured.window_count_of(WindowKind::OffCourt);
        r.discarded_windows = s.recovery.discarded().total();
    }

    *out = r;
}

/// Whether any recording has set the thresholds.
#[no_mangle]
pub extern "C" fn squash_engine_calibration() -> u32 {
    // Both engines are constructed Absent because no calibration constant
    // exists in this repository. When one does, this reports it and the fields
    // above start carrying values.
    CALIBRATION_ABSENT
}

// ------------------------------------------------------------------ profile

/// Read the profile file. Returns a [`Load`] code; never fails.
///
/// # Safety
/// `bytes` must point at `len` readable bytes, or be null with `len` 0.
#[no_mangle]
pub unsafe extern "C" fn squash_profile_load(bytes: *const u8, len: u32) -> u32 {
    let slice: &[u8] =
        if bytes.is_null() { &[] } else { core::slice::from_raw_parts(bytes, len as usize) };
    let (p, load) = Profile::parse_json(slice);
    *PROFILE.get() = p;
    match load {
        Load::Ok => 0,
        Load::Absent => 1,
        Load::UnknownSchema => 2,
        Load::Malformed => 3,
        Load::Truncated => 4,
    }
}

/// Add a session to the profile, dropping the oldest once it is full.
///
/// # Safety
/// `r` must point at a readable `SquashSessionRecord`.
#[no_mangle]
pub unsafe extern "C" fn squash_profile_record(r: *const SquashSessionRecord) {
    if r.is_null() {
        return;
    }
    PROFILE.get().record(to_session(&*r));
}

/// Serialise the profile into `out`, returning the bytes written or -1 when it
/// does not fit.
///
/// Nothing is written unless the whole file fits, so a caller that commits only
/// on a positive result cannot leave a truncated file behind.
///
/// # Safety
/// `out` must point at `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn squash_profile_write(out: *mut u8, cap: u32) -> i32 {
    if out.is_null() {
        return -1;
    }
    let slice = core::slice::from_raw_parts_mut(out, cap as usize);
    match PROFILE.get().write_json(slice) {
        Ok(n) => n as i32,
        Err(_) => -1,
    }
}

/// Sessions currently held.
#[no_mangle]
pub extern "C" fn squash_profile_sessions() -> u32 {
    PROFILE.get().len() as u32
}

/// Place one measurement against the wearer's own history.
///
/// `metric` indexes [`METRICS`]. Returns [`COMPARE_OK`] and fills `out`, or one
/// of the other `COMPARE_*` codes and leaves it untouched.
///
/// # Safety
/// `out` must point at a writable `SquashComparison`.
#[no_mangle]
pub unsafe extern "C" fn squash_profile_compare(
    metric: u8,
    value: f32,
    out: *mut SquashComparison,
) -> u32 {
    let Some(m) = METRICS.get(metric as usize).copied() else {
        return COMPARE_NO_DATA;
    };
    if out.is_null() {
        return COMPARE_NO_DATA;
    }
    if m.needs_segmentation() && squash_engine_calibration() == CALIBRATION_ABSENT {
        return COMPARE_NOT_CALIBRATED;
    }
    let baseline: RollingBaseline = PROFILE.get().baseline_of(m);
    match baseline.compare(value) {
        Ok(c) => {
            *out = SquashComparison {
                median: c.median,
                mad: c.mad,
                z: c.z.unwrap_or(0.0),
                sessions: c.sessions,
                has_z: c.z.is_some() as u8,
                reserved: 0,
            };
            COMPARE_OK
        }
        Err(Unavailable::WarmingUp { .. }) => COMPARE_WARMING_UP,
        Err(Unavailable::NotCalibrated) => COMPARE_NOT_CALIBRATED,
        Err(_) => COMPARE_NO_DATA,
    }
}

/// The metrics `squash_profile_compare` will place, in the order its `metric`
/// argument indexes them. The C++ side's enum must match, and
/// [`squash_engine_abi_fingerprint`] is what catches it when it does not.
pub const METRICS: [Metric; 7] = [
    Metric::HrMean,
    Metric::HrMax,
    Metric::RallyCount,
    Metric::RallyRate,
    Metric::WorkRestRatio,
    Metric::RecoveryShort,
    Metric::RecoveryLong,
];

fn to_session(r: &SquashSessionRecord) -> SessionRecord {
    let mut out = SessionRecord {
        started_utc: r.started_utc,
        active_s: r.active_s,
        hr_mean: r.hr_mean,
        hr_max: r.hr_max,
        hr_covered_s: r.hr_covered_s,
        hr_source: match r.hr_source {
            1 => HrSource::Optical,
            2 => HrSource::External,
            _ => HrSource::Unknown,
        },
        segmented: r.segmented != 0,
        rally_count: r.rally_count,
        rally_s: r.rally_s,
        rest_s: r.rest_s,
        off_court_s: r.off_court_s,
        ..SessionRecord::EMPTY
    };
    // The crate keeps a sum and a count so a mean can be re-derived; the C ABI
    // carries the mean the Service displays. Going back the other way multiplies
    // them out, which is exact for a whole-bpm stream and is the only lossy step
    // on this path -- the per-window detail the record was built from stays in
    // the session that produced it and does not survive the round trip.
    tally(&mut out, WindowKind::BetweenRallies, r.recovery_short_mean, r.recovery_short_n);
    tally(&mut out, WindowKind::OffCourt, r.recovery_long_mean, r.recovery_long_n);
    out
}

fn tally(s: &mut SessionRecord, kind: WindowKind, mean: f32, n: u16) {
    if n == 0 {
        return;
    }
    let i = kind.code() as usize - 1;
    let total = mean * n as f32 + 0.5;
    s.drop_sum[i] = if total > 0.0 { total as u32 } else { 0 };
    s.drop_n[i] = n;
}

// ------------------------------------------------------------------ ABI guard

const fn fnv1a(h: u32, v: usize) -> u32 {
    (h ^ v as u32).wrapping_mul(16_777_619)
}

/// The value [`squash_engine_abi_fingerprint`] returns for this layout.
///
/// `SquashEngine.hpp` asserts the same number at start-up. Changing it is a
/// deliberate act: it means the C++ side must change too.
pub const ABI_FINGERPRINT: u32 = 524_638_087;

/// A hash of every offset the C++ side reads, so a struct that drifts is a
/// runtime assertion rather than a silently misread field.
#[no_mangle]
pub extern "C" fn squash_engine_abi_fingerprint() -> u32 {
    let h = fnv1a(2_166_136_261, core::mem::size_of::<SquashSessionRecord>());
    let h = fnv1a(h, core::mem::size_of::<SquashComparison>());
    let h = fnv1a(h, core::mem::offset_of!(SquashSessionRecord, active_s));
    let h = fnv1a(h, core::mem::offset_of!(SquashSessionRecord, hr_mean));
    let h = fnv1a(h, core::mem::offset_of!(SquashSessionRecord, hr_source));
    let h = fnv1a(h, core::mem::offset_of!(SquashSessionRecord, segmented));
    let h = fnv1a(h, core::mem::offset_of!(SquashComparison, sessions));
    let h = fnv1a(h, core::mem::offset_of!(SquashSessionRecord, hr_external_readings));
    fnv1a(h, METRICS.len())
}

const _: () = assert!(core::mem::size_of::<SquashSessionRecord>() == 56);
const _: () = assert!(core::mem::align_of::<SquashSessionRecord>() == 4);
const _: () = assert!(core::mem::size_of::<SquashComparison>() == 16);

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Mutex, MutexGuard};

    /// The ABI's state is a singleton because the Service is one thread; the
    /// test harness is not, so every test takes this first.
    static SERIALISE: Mutex<()> = Mutex::new(());

    fn alone() -> MutexGuard<'static, ()> {
        SERIALISE.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn imu(az: i16) -> [i16; 6] {
        [0, 0, az, 0, 0, 0]
    }

    fn finish(utc: u32, active_s: u32) -> SquashSessionRecord {
        let mut r = SquashSessionRecord::default();
        unsafe { squash_engine_finish(utc, active_s, &mut r) };
        r
    }

    #[test]
    fn an_uncalibrated_session_reports_heart_rate_and_nothing_segmented() {
        let _alone = alone();
        squash_engine_begin();
        for i in 0..600u32 {
            unsafe { squash_engine_on_imu(i * 10, imu(4096).as_ptr()) };
        }
        for i in 0..30u32 {
            squash_engine_on_hr(i * 1000, 140.0 + (i % 6) as f32, 2, 2);
        }
        let r = finish(1_756_900_000, 600);
        assert_eq!(r.segmented, 0);
        assert_eq!(r.rally_count, 0);
        assert_eq!(r.hr_max, 145.0);
        assert_eq!(r.hr_source, 2);
        assert!(r.hr_mean > 140.0 && r.hr_mean < 145.0);
    }

    #[test]
    fn untrusted_readings_are_left_out_of_the_mean() {
        let _alone = alone();
        squash_engine_begin();
        for i in 0..25u32 {
            squash_engine_on_hr(i * 1000, 140.0, 2, 2);
        }
        squash_engine_on_hr(25_000, 40.0, 0, 2);
        let r = finish(1, 60);
        assert_eq!(r.hr_mean, 140.0);
    }

    /// The 2026-09-03 8-minute recording reported 720 covered seconds against
    /// 498 active ones, because heart rate was fed to the engine while the
    /// activity was paused.
    #[test]
    fn coverage_can_never_exceed_the_active_time() {
        let _alone = alone();
        squash_engine_begin();
        for i in 0..700u32 {
            squash_engine_on_hr(i * 1000, 72.0, 2, 1);
        }
        let r = finish(1_788_450_473, 498);
        assert!(
            r.hr_covered_s <= r.active_s,
            "covered {} s of a {} s session",
            r.hr_covered_s,
            r.active_s
        );
    }

    /// The 2026-09-03 smoke recording: five trusted readings on wrist optical,
    /// which the app's own summary reported as no heart rate at all.
    #[test]
    fn too_few_readings_report_no_heart_rate_rather_than_a_mean_of_five() {
        let _alone = alone();
        squash_engine_begin();
        for (i, bpm) in [70.0f32, 68.0, 68.0, 67.0, 67.0].iter().enumerate() {
            squash_engine_on_hr(i as u32 * 1005, *bpm, 2, 1);
        }
        let r = finish(1_788_447_336, 5);
        assert_eq!(r.hr_mean, 0.0, "five readings is not a mean heart rate");
        assert_eq!(r.hr_max, 0.0);
        assert!(r.hr_covered_s > 0, "coverage still reported, so the gap is visible");
    }

    #[test]
    fn a_gap_in_the_stream_does_not_count_as_covered() {
        let _alone = alone();
        squash_engine_begin();
        for i in 0..10u32 {
            squash_engine_on_hr(i * 1000, 140.0, 2, 2);
        }
        // Two minutes of nothing, then more readings.
        for i in 0..10u32 {
            squash_engine_on_hr(130_000 + i * 1000, 140.0, 2, 2);
        }
        let r = finish(1, 140);
        assert!(r.hr_covered_s < 30, "the gap must not be counted: {}", r.hr_covered_s);
    }

    /// The bug this file's design exists to prevent, exercised on a stack the
    /// size of the one it crashed on.
    ///
    /// A host thread gets 8 MB by default, which is why 69 passing tests said
    /// nothing about a 12 656-byte value being moved through a 10 240-byte
    /// stack. The platform may round the request up -- macOS has a 16 KiB
    /// floor -- so this is a floor on the guarantee, not the exact watch
    /// condition; it still fails outright on a by-value construction.
    #[test]
    fn the_strap_is_visible_even_when_optical_supplied_most_of_the_session() {
        let _alone = alone();
        squash_engine_begin();
        // The 2026-09-03 shape: external for 118 readings, then optical.
        for i in 0..118u32 {
            squash_engine_on_hr(i * 1000, 77.0, 3, 2);
        }
        for i in 118..707u32 {
            squash_engine_on_hr(i * 1000, 72.0, 2, 1);
        }
        let r = finish(1_788_450_473, 498);
        assert_eq!(r.hr_source, 1, "optical supplied the majority");
        assert_eq!(r.hr_external_readings, 118, "and the strap is still on the record");
    }

    #[test]
    fn a_session_starts_within_the_services_stack() {
        let _alone = alone();
        std::thread::Builder::new()
            .stack_size(SERVICE_STACK_BYTES)
            .spawn(|| {
                squash_engine_begin();
                for i in 0..300u32 {
                    unsafe { squash_engine_on_imu(i * 10, imu(4096).as_ptr()) };
                }
                for i in 0..3u32 {
                    squash_engine_on_hr(i * 1000, 140.0, 2, 2);
                }
                let mut r = SquashSessionRecord::default();
                unsafe { squash_engine_finish(1, 3, &mut r) };
                r
            })
            .expect("a thread with the Service's stack size")
            .join()
            .expect("the whole ABI path must fit the Service's stack");
    }

    #[test]
    fn a_session_cannot_be_built_on_the_services_stack() {
        let _alone = alone();
        // Not a limit being enforced -- a measurement being kept. It was
        // 12 656 bytes when the recovery analyser held 256 windows of its own;
        // the shared detector keeps one window and the session record four, so
        // it is 9 536 now and no longer exceeds the stack on its own.
        //
        // It still cannot be built on it. Constructing one as a value and
        // moving it into place needs the source and the destination live at
        // once, which is twice this, and even one copy would leave 704 bytes
        // for every frame beneath it. Hence the const fn and the static.
        let size = core::mem::size_of::<Session>();
        assert!(
            size * 2 > SERVICE_STACK_BYTES,
            "a move into place is {} bytes against a {}-byte stack",
            size * 2,
            SERVICE_STACK_BYTES
        );
        assert!(size > SERVICE_STACK_BYTES / 2, "Session is {size} bytes");
    }

    #[test]
    fn there_is_no_calibration_in_this_repository() {
        let _alone = alone();
        assert_eq!(squash_engine_calibration(), CALIBRATION_ABSENT);
    }

    #[test]
    fn a_profile_warms_up_before_it_compares() {
        let _alone = alone();
        unsafe { squash_profile_load(core::ptr::null(), 0) };
        assert_eq!(squash_profile_sessions(), 0);

        let mut c = SquashComparison::default();
        for i in 0..4u32 {
            let r = SquashSessionRecord {
                started_utc: 1000 + i,
                active_s: 3600,
                hr_covered_s: 3500,
                hr_mean: 140.0 + i as f32,
                hr_max: 180.0,
                hr_source: 2,
                ..Default::default()
            };
            unsafe { squash_profile_record(&r) };
        }
        assert_eq!(unsafe { squash_profile_compare(0, 142.0, &mut c) }, COMPARE_WARMING_UP);

        let r = SquashSessionRecord {
            started_utc: 2000,
            active_s: 3600,
            hr_covered_s: 3500,
            hr_mean: 141.0,
            hr_max: 180.0,
            hr_source: 2,
            ..Default::default()
        };
        unsafe { squash_profile_record(&r) };
        assert_eq!(unsafe { squash_profile_compare(0, 142.0, &mut c) }, COMPARE_OK);
        assert_eq!(c.sessions, 5);
    }

    #[test]
    fn a_rally_metric_is_not_calibrated_rather_than_missing() {
        let _alone = alone();
        unsafe { squash_profile_load(core::ptr::null(), 0) };
        let mut c = SquashComparison::default();
        // Index 2 is Metric::RallyCount.
        assert_eq!(unsafe { squash_profile_compare(2, 10.0, &mut c) }, COMPARE_NOT_CALIBRATED);
    }

    #[test]
    fn the_profile_round_trips_through_its_own_bytes() {
        let _alone = alone();
        unsafe { squash_profile_load(core::ptr::null(), 0) };
        let r = SquashSessionRecord {
            started_utc: 1_756_900_000,
            active_s: 3600,
            hr_covered_s: 3500,
            hr_mean: 142.5,
            hr_max: 181.25,
            hr_source: 2,
            ..Default::default()
        };
        unsafe { squash_profile_record(&r) };

        let mut buf = [0u8; effortkit::profile::MAX_BYTES];
        let n = unsafe { squash_profile_write(buf.as_mut_ptr(), buf.len() as u32) };
        assert!(n > 0);
        assert_eq!(unsafe { squash_profile_load(buf.as_ptr(), n as u32) }, 0);
        assert_eq!(squash_profile_sessions(), 1);
    }

    #[test]
    fn a_buffer_too_small_reports_failure_rather_than_a_partial_file() {
        let _alone = alone();
        unsafe { squash_profile_load(core::ptr::null(), 0) };
        let r = SquashSessionRecord { started_utc: 1, active_s: 60, ..Default::default() };
        unsafe { squash_profile_record(&r) };
        let mut tiny = [0u8; 4];
        assert_eq!(unsafe { squash_profile_write(tiny.as_mut_ptr(), tiny.len() as u32) }, -1);
    }

    #[test]
    fn the_abi_fingerprint_is_the_value_the_cpp_side_asserts() {
        let _alone = alone();
        // Printed by this test; SquashEngine.hpp carries the same number, so a
        // struct that drifts fails at start-up instead of misreading a field.
        assert_eq!(squash_engine_abi_fingerprint(), ABI_FINGERPRINT);
        // Changing it is a deliberate act: the C++ side asserts the same value,
        // so a struct that drifts fails loudly instead of misreading a field.
    }
}
