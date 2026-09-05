//! The Spin Service's half of [`effortkit`], as a C ABI.
//!
//! The Service owns the clock, the sensor and the filesystem; this owns the
//! arithmetic. Everything here is a thin shell — the engine is in `EffortKit`
//! and so is the reasoning, in its `README.md`.
//!
//! Unlike Squash's shim, this one ships a calibration: [`effortkit::window::HRR60`]
//! is defined by the literature rather than measured here, so Spin reports on
//! its first ride. See the crate's README on why that distinction is a type.

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

use core::cell::UnsafeCell;

use effortkit::history::{History, Load, MAX_STORE_BYTES};
use effortkit::hr::HrSource;
use effortkit::record::{DiscardCounts, Recovery, Session};
use effortkit::window::{Calibration, Detector, Reason, Step, WindowKind, HRR60};

#[cfg(not(feature = "std"))]
extern "C" {
    fn spin_engine_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic stops the Service silently, and a ride in progress is
/// lost with no record of why.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"spin_engine panic";
    unsafe { spin_engine_host_panic(s.as_ptr(), s.len() as u32) };
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

/// What Spin measures: the whole-session cessation, on the terms the literature
/// defines. Borrowed by the detector rather than copied into it.
static CALIBRATION: Calibration = Calibration::absent().with(WindowKind::Pause, HRR60);

/// Live for the life of the process, in BSS rather than on any stack.
static DETECTOR: Single<Detector> = Single::new(Detector::new(&CALIBRATION));

/// The cross-app log, which is 20 sessions and therefore far too large to build
/// on the Service's 10 KiB stack.
static HISTORY: Single<History> = Single::new(History::new());

/// The Service's stack, from `una_app_build_service`'s default of `10*1024`.
pub const SERVICE_STACK_BYTES: usize = 10 * 1024;

// ------------------------------------------------------------------ recovery

/// Nothing to report.
pub const STEP_NOTHING: u8 = 0;
/// A measurement is waiting in [`spin_engine_take`].
pub const STEP_COMPLETED: u8 = 1;
/// A window ended with nothing; [`spin_engine_last_discard`] says why.
pub const STEP_DISCARDED: u8 = 2;

const fn step_code(s: Step) -> u8 {
    match s {
        Step::Nothing => STEP_NOTHING,
        Step::Completed => STEP_COMPLETED,
        Step::Discarded => STEP_DISCARDED,
    }
}

/// Begin a ride. `max_hr` is the top of the watch's own threshold ladder, which
/// is its maximum heart rate and not a zone floor; 0 when it has none.
#[no_mangle]
pub extern "C" fn spin_engine_start(max_hr: u8) {
    DETECTOR.get().start(max_hr);
}

/// One second of the ride, keyed on UTC rather than on the number of calls: a
/// tick the Service was too busy to serve is a second that still went past.
///
/// `trust` is the kernel's own 0-3 confidence, passed through rather than
/// collapsed, so the calibration decides how far the sensor is believed.
#[no_mangle]
pub extern "C" fn spin_engine_second(
    utc: i64,
    bpm: f32,
    trust: u8,
    source: u8,
    active_s: u32,
) -> u8 {
    step_code(DETECTOR.get().second(utc, bpm, trust, HrSource::from_code(source), active_s))
}

/// Effort has ceased, because the wearer paused.
#[no_mangle]
pub extern "C" fn spin_engine_cease() -> u8 {
    step_code(DETECTOR.get().cease(WindowKind::Pause))
}

/// Effort has restarted, which ends any window in progress.
#[no_mangle]
pub extern "C" fn spin_engine_resume() -> u8 {
    step_code(DETECTOR.get().resume())
}

/// The ride is over and the sensor is about to go.
#[no_mangle]
pub extern "C" fn spin_engine_end() -> u8 {
    step_code(DETECTOR.get().end())
}

/// Hand back a completed measurement, once. Returns 0 when there is none.
///
/// # Safety
/// `out` must point at a writable `SpinRecovery`.
#[no_mangle]
pub unsafe extern "C" fn spin_engine_take(out: *mut Recovery) -> u8 {
    if out.is_null() {
        return 0;
    }
    match DETECTOR.get().take() {
        Some(r) => {
            *out = r;
            1
        }
        None => 0,
    }
}

/// Why the last window produced nothing, or 0 when it produced something.
#[no_mangle]
pub extern "C" fn spin_engine_last_discard() -> u8 {
    DETECTOR.get().last_discard().map_or(0, |r| r.code())
}

/// Every window this ride discarded, by reason.
///
/// # Safety
/// `out` must point at a writable `SpinDiscardCounts`.
#[no_mangle]
pub unsafe extern "C" fn spin_engine_discarded(out: *mut DiscardCounts) {
    if out.is_null() {
        return;
    }
    *out = DETECTOR.get().discarded().into();
}

/// The name of a discard reason, NUL-terminated and static.
///
/// Here so a reason and its spelling cannot drift apart.
#[no_mangle]
pub extern "C" fn spin_engine_discard_name(reason: u8) -> *const u8 {
    // Every arm is a literal with an explicit NUL, so the pointer is valid for
    // the life of the process and needs no allocation.
    let s: &'static [u8] = match reason {
        r if r == Reason::NotCalibrated.code() => b"not_calibrated\0",
        r if r == Reason::NotMeasurable.code() => b"not_measurable\0",
        r if r == Reason::NoMaxHr.code() => b"no_max_hr\0",
        r if r == Reason::TooShort.code() => b"too_short\0",
        r if r == Reason::TooEasy.code() => b"too_easy\0",
        r if r == Reason::AlreadyFalling.code() => b"already_falling\0",
        r if r == Reason::NoBaselineHistory.code() => b"no_baseline_history\0",
        r if r == Reason::NoBaseline.code() => b"no_baseline\0",
        r if r == Reason::Dropout.code() => b"dropout\0",
        r if r == Reason::NoEndpoint.code() => b"no_endpoint\0",
        r if r == Reason::EffortResumed.code() => b"effort_resumed\0",
        r if r == Reason::SessionEnded.code() => b"session_ended\0",
        r if r == Reason::SourceChanged.code() => b"source_changed\0",
        r if r == Reason::SourceNotAccepted.code() => b"source_not_accepted\0",
        _ => b"none\0",
    };
    s.as_ptr()
}

// ------------------------------------------------------------------ the log

/// Name the log's writer, so a reader merging several apps can tell them apart.
///
/// # Safety
/// `app` and `sport` must point at `app_len` and `sport_len` readable bytes.
#[no_mangle]
pub unsafe extern "C" fn spin_history_init(
    app: *const u8,
    app_len: u32,
    sport: *const u8,
    sport_len: u32,
) {
    if app.is_null() || sport.is_null() {
        return;
    }
    let a = core::slice::from_raw_parts(app, app_len as usize);
    let s = core::slice::from_raw_parts(sport, sport_len as usize);
    HISTORY.get().name(a, s);
}

/// Parsed, or there was nothing there to parse.
pub const LOAD_OK: i32 = 0;
/// A schema this build does not write, and not to be overwritten.
pub const LOAD_NEWER: i32 = 1;
/// Not JSON this understands; keep it as evidence and start fresh.
pub const LOAD_UNREADABLE: i32 = 2;

/// Read a store file.
///
/// # Safety
/// `buf` must point at `len` readable bytes, or be null for an absent file.
#[no_mangle]
pub unsafe extern "C" fn spin_history_load(buf: *const u8, len: u32) -> i32 {
    let bytes: &[u8] = if buf.is_null() {
        &[]
    } else {
        core::slice::from_raw_parts(buf, len as usize)
    };
    match HISTORY.get().load(bytes) {
        Load::Ok => LOAD_OK,
        Load::Newer => LOAD_NEWER,
        Load::Unreadable => LOAD_UNREADABLE,
    }
}

/// Add a session, replacing any entry that already claims the same start.
///
/// # Safety
/// `s` must point at a readable `SpinSessionRecord`.
#[no_mangle]
pub unsafe extern "C" fn spin_history_add(s: *const Session) {
    if s.is_null() {
        return;
    }
    HISTORY.get().add(&*s);
}

/// Serialise, dropping the oldest entries until the result fits `cap`.
///
/// Returns the bytes written, or -1 when not even one session fits.
///
/// # Safety
/// `buf` must point at `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn spin_history_save(buf: *mut u8, cap: u32) -> i32 {
    if buf.is_null() {
        return -1;
    }
    let out = core::slice::from_raw_parts_mut(buf, cap as usize);
    HISTORY.get().save(out).map_or(-1, |n| n as i32)
}

/// The largest file this will write or read.
#[no_mangle]
pub extern "C" fn spin_history_max_bytes() -> u32 {
    MAX_STORE_BYTES as u32
}

// ------------------------------------------------------------------ load

/// Edwards' TRIMP in minute-weights, or -1 for a ladder the weights are not
/// defined over.
///
/// # Safety
/// `s` must point at a readable `SpinSessionRecord`.
#[no_mangle]
pub unsafe extern "C" fn spin_edwards_trimp(s: *const Session) -> i32 {
    if s.is_null() {
        return -1;
    }
    effortkit::load::edwards_trimp(&*s).map_or(-1, |t| t as i32)
}

// ------------------------------------------------------------------ ABI guard

/// A hash of every offset the C++ side reads, so a struct that drifts is a
/// runtime assertion rather than a silently misread field.
///
/// `SpinEngine.hpp` walks the same values with the same function over the
/// offsets *its* compiler produced, so a drift on either side is caught.
#[no_mangle]
pub extern "C" fn spin_engine_abi_fingerprint() -> u32 {
    use core::mem::{align_of, offset_of, size_of};
    let h = effortkit::fnv1a(effortkit::FNV_OFFSET_BASIS, size_of::<Recovery>());
    let h = effortkit::fnv1a(h, align_of::<Recovery>());
    let h = effortkit::fnv1a(h, offset_of!(Recovery, at_active_s));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, hr0));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, hr_end));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, window_s));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, trusted_s));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, hr0_pct_max));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, kind));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, curve));
    let h = effortkit::fnv1a(h, offset_of!(Recovery, source));
    let h = effortkit::fnv1a(h, size_of::<Session>());
    let h = effortkit::fnv1a(h, align_of::<Session>());
    let h = effortkit::fnv1a(h, offset_of!(Session, start_utc));
    let h = effortkit::fnv1a(h, offset_of!(Session, active_s));
    let h = effortkit::fnv1a(h, offset_of!(Session, elapsed_s));
    let h = effortkit::fnv1a(h, offset_of!(Session, kcal));
    let h = effortkit::fnv1a(h, offset_of!(Session, work_kj));
    let h = effortkit::fnv1a(h, offset_of!(Session, zone_s));
    let h = effortkit::fnv1a(h, offset_of!(Session, zone_floor));
    let h = effortkit::fnv1a(h, offset_of!(Session, hr_avg));
    let h = effortkit::fnv1a(h, offset_of!(Session, hr_max));
    let h = effortkit::fnv1a(h, offset_of!(Session, hr_max_setting));
    let h = effortkit::fnv1a(h, offset_of!(Session, weight_kg));
    let h = effortkit::fnv1a(h, offset_of!(Session, zone_count));
    let h = effortkit::fnv1a(h, offset_of!(Session, recovery_count));
    let h = effortkit::fnv1a(h, offset_of!(Session, recoveries_dropped));
    let h = effortkit::fnv1a(h, offset_of!(Session, recoveries));
    let h = effortkit::fnv1a(h, offset_of!(Session, discarded));
    effortkit::fnv1a(h, size_of::<DiscardCounts>())
}

#[cfg(test)]
mod tests {
    use super::*;
    use effortkit::record::MAX_RECOVERIES;

    /// The ABI is one process-wide set of statics, so tests cannot overlap.
    fn alone() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
        LOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn ride(det_utc: &mut i64, seconds: u32, bpm: f32, active: &mut u32) {
        for _ in 0..seconds {
            *det_utc += 1;
            *active += 1;
            spin_engine_second(*det_utc, bpm, 3, 2, *active);
        }
    }

    #[test]
    fn a_pause_after_hard_effort_is_measured_across_the_abi() {
        let _alone = alone();
        spin_engine_start(190);
        let mut utc = 1_700_000_000i64;
        let mut active = 0u32;
        ride(&mut utc, 600, 170.0, &mut active);
        assert_eq!(spin_engine_cease(), STEP_NOTHING);

        let mut step = STEP_NOTHING;
        for i in 0..70u32 {
            utc += 1;
            let bpm = 90.0 + 80.0 * (-(i as f32) / 55.0).exp();
            step = spin_engine_second(utc, bpm, 3, 2, active);
            if step != STEP_NOTHING {
                break;
            }
        }
        assert_eq!(step, STEP_COMPLETED);

        let mut m = Recovery::EMPTY;
        assert_eq!(unsafe { spin_engine_take(&mut m) }, 1);
        assert_eq!(m.hr0, 170);
        assert_eq!(m.window_s, 60);
        assert_eq!(m.kind, WindowKind::Pause.code());
        assert_eq!(unsafe { spin_engine_take(&mut m) }, 0, "handed out once");
    }

    #[test]
    fn a_watch_with_no_maximum_says_why_rather_than_measuring() {
        let _alone = alone();
        spin_engine_start(0);
        let mut utc = 1_700_000_000i64;
        let mut active = 0u32;
        ride(&mut utc, 600, 170.0, &mut active);
        assert_eq!(spin_engine_cease(), STEP_DISCARDED);
        assert_eq!(spin_engine_last_discard(), Reason::NoMaxHr.code());

        let mut d = DiscardCounts::NONE;
        unsafe { spin_engine_discarded(&mut d) };
        assert_eq!(d.no_max_hr, 1);
        assert_eq!(d.total(), 1);
    }

    #[test]
    fn every_discard_reason_has_a_name_across_the_abi() {
        let _alone = alone();
        for code in 1..=14u8 {
            let p = spin_engine_discard_name(code);
            let s = unsafe { core::ffi::CStr::from_ptr(p as *const core::ffi::c_char) };
            assert!(!s.to_bytes().is_empty(), "reason {code} has no name");
            assert_ne!(s.to_bytes(), b"none", "reason {code} fell through");
        }
    }

    #[test]
    fn the_log_round_trips_through_its_own_bytes() {
        let _alone = alone();
        unsafe { spin_history_load(core::ptr::null(), 0) };
        spin_history_init_str("Spin", "indoor_cycling");

        let mut s = Session::EMPTY;
        s.start_utc = 1_788_483_397;
        s.active_s = 1142;
        s.hr_avg = 116;
        s.hr_max_setting = 184;
        s.zone_count = 5;
        s.zone_floor = [92, 110, 129, 147, 166, 0, 0, 0];
        s.zone_s = [162, 321, 341, 173, 145, 0, 0, 0, 0];
        unsafe { spin_history_add(&s) };

        let mut buf = [0u8; 16 * 1024];
        let n = unsafe { spin_history_save(buf.as_mut_ptr(), buf.len() as u32) };
        assert!(n > 0);
        assert_eq!(unsafe { spin_history_load(buf.as_ptr(), n as u32) }, LOAD_OK);
        assert!(unsafe { spin_edwards_trimp(&s) } >= 0, "a five-zone ladder has one");
    }

    #[test]
    fn a_newer_schema_is_refused() {
        let _alone = alone();
        let newer = br#"{"version":99,"app":"Spin","sessions":[]}"#;
        assert_eq!(
            unsafe { spin_history_load(newer.as_ptr(), newer.len() as u32) },
            LOAD_NEWER
        );
    }

    #[test]
    fn the_cap_keeps_the_newest_and_counts_the_rest() {
        // MEASURED on hardware: Spin's Session 2 of 2026-09-03 ran three
        // windows and the log kept `at_active_s` 591 and 889, dropping 261.
        let _alone = alone();
        let mut s = Session::EMPTY;
        for at in [261u32, 591, 889] {
            s.add_recovery(Recovery { at_active_s: at, ..Recovery::EMPTY });
        }
        assert_eq!(s.recovery_count as usize, MAX_RECOVERIES);
        assert_eq!(s.recoveries_dropped, 1);
        assert_eq!(s.recoveries()[0].at_active_s, 591);
        assert_eq!(s.recoveries()[1].at_active_s, 889);
    }

    #[test]
    fn the_whole_abi_path_fits_the_services_stack() {
        let _alone = alone();
        std::thread::Builder::new()
            .stack_size(SERVICE_STACK_BYTES)
            .spawn(|| {
                spin_engine_start(190);
                let mut utc = 1_700_000_000i64;
                for i in 0..300u32 {
                    utc += 1;
                    spin_engine_second(utc, 150.0, 3, 2, i);
                }
                spin_engine_cease();
                let mut buf = [0u8; 256];
                unsafe { spin_history_save(buf.as_mut_ptr(), buf.len() as u32) }
            })
            .expect("a thread with the Service's stack size")
            .join()
            .expect("the whole ABI path must fit the Service's stack");
    }

    fn spin_history_init_str(app: &str, sport: &str) {
        unsafe {
            spin_history_init(
                app.as_ptr(),
                app.len() as u32,
                sport.as_ptr(),
                sport.len() as u32,
            )
        }
    }
}
