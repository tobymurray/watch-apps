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
use effortkit::recovery::{Calibration as RecoveryCalibration, HrSample, HrSource, RecoveryAnalyser};
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
    recovery: RecoveryAnalyser,
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
    /// This type is 12 656 bytes and the Service's stack is 10 240. Building
    /// one as a value and moving it into place is a stack overflow, and it is
    /// not a subtle one: the watch took a `STKOF` UsageFault
    /// (`CFSR=0x00100000`) in `Squash.SRV` the first time an activity was
    /// started, on the line after the three recording sinks were opened. Host
    /// tests cannot see it -- a host thread has an 8 MB stack -- which is why
    /// `a_session_starts_within_the_services_stack` runs on a 10 KiB one.
    const fn new() -> Self {
        Self {
            running: false,
            epochs: EpochAccumulator::new(),
            segmenter: Segmenter::new(SegmentCalibration::Absent),
            recovery: RecoveryAnalyser::new(RecoveryCalibration::Absent),
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

    /// Ready this session for a new activity, touching nothing large.
    fn reset(&mut self) {
        self.epochs = EpochAccumulator::new();
        self.segmenter.reset(SegmentCalibration::Absent);
        self.recovery.reset(RecoveryCalibration::Absent);
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
    /// Seconds a trusted heart rate was available.
    pub hr_covered_s: u32,
    /// Rallies the segmenter found; zero when uncalibrated.
    pub rally_count: u32,
    /// Seconds in rallies.
    pub rally_s: u32,
    /// Seconds resting between rallies.
    pub rest_s: u32,
    /// Seconds off court.
    pub off_court_s: u32,
    /// Mean trusted heart rate, bpm.
    pub hr_mean: f32,
    /// Highest trusted heart rate, bpm.
    pub hr_max: f32,
    /// Mean fall across qualifying rests between rallies, bpm.
    pub recovery_short_mean: f32,
    /// Mean fall across qualifying off-court rests, bpm.
    pub recovery_long_mean: f32,
    /// Qualifying rests between rallies.
    pub recovery_short_n: u16,
    /// Qualifying off-court rests.
    pub recovery_long_n: u16,
    /// 0 unknown, 1 optical, 2 external.
    pub hr_source: u8,
    /// Zero when no calibration existed, in which case every segmentation and
    /// recovery field above is zero because nothing ran, not because nothing
    /// happened.
    pub segmented: u8,
    /// Recovery windows discarded, all reasons together.
    pub discarded_windows: u16,
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
            s.recovery.on_state(state, e.start_ms());
        }
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
    s.recovery.on_hr(&HrSample { t_ms, bpm, trust, source });

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
        if s.hr_count > 0 {
            r.hr_mean = s.hr_sum / s.hr_count as f32;
            r.hr_max = s.hr_max;
            r.hr_covered_s = s.hr_covered_ms / 1000;
            r.hr_source = if s.external >= s.optical && s.external > 0 {
                2
            } else if s.optical > 0 {
                1
            } else {
                0
            };
        }
        if let Ok(seg) = s.segmenter.finish_in_place() {
            r.segmented = 1;
            r.rally_count = seg.rally_count;
            r.rally_s = seg.rally_epochs;
            r.rest_s = seg.rest_epochs;
            r.off_court_s = seg.off_court_epochs;
        }
        if let Ok(rec) = s.recovery.finish_in_place() {
            use effortkit::recovery::WindowKind;
            r.recovery_short_mean = rec.mean_drop(WindowKind::BetweenRallies).unwrap_or(0.0);
            r.recovery_short_n = rec.count(WindowKind::BetweenRallies);
            r.recovery_long_mean = rec.mean_drop(WindowKind::OffCourt).unwrap_or(0.0);
            r.recovery_long_n = rec.count(WindowKind::OffCourt);
            r.discarded_windows = rec.discarded.total();
        }
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
    SessionRecord {
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
        recovery_short_mean: r.recovery_short_mean,
        recovery_short_n: r.recovery_short_n,
        recovery_long_mean: r.recovery_long_mean,
        recovery_long_n: r.recovery_long_n,
    }
}

// ------------------------------------------------------------------ ABI guard

const fn fnv1a(h: u32, v: usize) -> u32 {
    (h ^ v as u32).wrapping_mul(16_777_619)
}

/// The value [`squash_engine_abi_fingerprint`] returns for this layout.
///
/// `SquashEngine.hpp` asserts the same number at start-up. Changing it is a
/// deliberate act: it means the C++ side must change too.
pub const ABI_FINGERPRINT: u32 = 3_384_192_379;

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
    fnv1a(h, METRICS.len())
}

const _: () = assert!(core::mem::size_of::<SquashSessionRecord>() == 52);
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
        for i in 0..6u32 {
            squash_engine_on_hr(i * 1000, 140.0 + i as f32, 2, 2);
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
        squash_engine_on_hr(0, 140.0, 2, 2);
        squash_engine_on_hr(1000, 40.0, 0, 2);
        let r = finish(1, 60);
        assert_eq!(r.hr_mean, 140.0);
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
    fn a_session_is_too_large_for_that_stack_which_is_why_it_is_static() {
        let _alone = alone();
        // Not a limit being enforced -- a measurement being kept. If this ever
        // fails, Session now fits the stack and the static is only an
        // optimisation; the comment on Session::new needs revising, not this.
        assert!(
            core::mem::size_of::<Session>() > SERVICE_STACK_BYTES,
            "Session is {} bytes against a {}-byte stack",
            core::mem::size_of::<Session>(),
            SERVICE_STACK_BYTES
        );
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
