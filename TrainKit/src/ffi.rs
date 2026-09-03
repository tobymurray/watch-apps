//! The C ABI a Service calls. Mirrored by `include/trainkit.h`.
//!
//! `Detector` and `History` cross as opaque storage the caller owns, because
//! neither has a field a C++ Service has any business touching -- the caller
//! only has to make the bytes exist, aligned, and the `*_bytes()` and
//! `*_align()` functions here are what it checks that against before it starts.
//! `Session` and `Recovery` cross as themselves, field for field, because
//! filling one in is the whole point.

use core::ffi::c_void;

use crate::history::{History, Load, MAX_STORE_BYTES};
use crate::record::{Recovery, Session};
use crate::recovery::{Detector, Step};

// -- What `history_load` reported --------------------------------------------

pub const LOAD_OK: i32 = 0;
pub const LOAD_NEWER: i32 = 1;
pub const LOAD_UNREADABLE: i32 = 2;

// -- What one second did -----------------------------------------------------

pub const STEP_NOTHING: u8 = 0;
pub const STEP_COMPLETED: u8 = 1;
pub const STEP_DISCARDED: u8 = 2;

#[no_mangle]
pub extern "C" fn trainkit_abi_fingerprint() -> u32 {
    crate::abi_fingerprint()
}

#[no_mangle]
pub extern "C" fn trainkit_max_store_bytes() -> u32 {
    MAX_STORE_BYTES as u32
}

#[no_mangle]
pub extern "C" fn trainkit_detector_bytes() -> u32 {
    core::mem::size_of::<Detector>() as u32
}

#[no_mangle]
pub extern "C" fn trainkit_detector_align() -> u32 {
    core::mem::align_of::<Detector>() as u32
}

#[no_mangle]
pub extern "C" fn trainkit_history_bytes() -> u32 {
    core::mem::size_of::<History>() as u32
}

#[no_mangle]
pub extern "C" fn trainkit_history_align() -> u32 {
    core::mem::align_of::<History>() as u32
}

// -- Recovery ----------------------------------------------------------------

/// # Safety
/// `d` must be writable storage of at least `trainkit_detector_bytes()`,
/// aligned to `trainkit_detector_align()`.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_start(d: *mut c_void, max_hr: u8) {
    if d.is_null() {
        return;
    }
    // A write rather than a reference: the storage is whatever the caller had,
    // and nothing has constructed a Detector in it yet.
    core::ptr::write(d as *mut Detector, {
        let mut det = Detector::new();
        det.start(max_hr);
        det
    });
}

/// # Safety
/// `d` must have been passed to `trainkit_recovery_start` first.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_second(
    d: *mut c_void,
    utc: i64,
    bpm: f32,
    trusted: u8,
    active_s: u32,
) -> u8 {
    match (d as *mut Detector).as_mut() {
        Some(det) => step_code(det.second(utc, bpm, trusted != 0, active_s)),
        None => STEP_NOTHING,
    }
}

/// # Safety
/// See `trainkit_recovery_second`.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_cease(d: *mut c_void, trigger: u8) -> u8 {
    match (d as *mut Detector).as_mut() {
        Some(det) => step_code(det.cease(trigger)),
        None => STEP_NOTHING,
    }
}

/// # Safety
/// See `trainkit_recovery_second`.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_resume(d: *mut c_void) -> u8 {
    match (d as *mut Detector).as_mut() {
        Some(det) => step_code(det.resume()),
        None => STEP_NOTHING,
    }
}

/// # Safety
/// See `trainkit_recovery_second`.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_end(d: *mut c_void) -> u8 {
    match (d as *mut Detector).as_mut() {
        Some(det) => step_code(det.end()),
        None => STEP_NOTHING,
    }
}

/// Hand back the completed measurement, once.
///
/// # Safety
/// `out` must be writable; see `trainkit_recovery_second` for `d`.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_take(d: *mut c_void, out: *mut Recovery) -> u8 {
    let det = match (d as *mut Detector).as_mut() {
        Some(det) => det,
        None => return 0,
    };
    match det.take() {
        Some(r) if !out.is_null() => {
            *out = r;
            1
        }
        _ => 0,
    }
}

/// # Safety
/// See `trainkit_recovery_second`.
#[no_mangle]
pub unsafe extern "C" fn trainkit_recovery_last_discard(d: *const c_void) -> u8 {
    match (d as *const Detector).as_ref() {
        Some(det) => det.last_discard(),
        None => 0,
    }
}

fn step_code(s: Step) -> u8 {
    match s {
        Step::Nothing => STEP_NOTHING,
        Step::Completed => STEP_COMPLETED,
        Step::Discarded => STEP_DISCARDED,
    }
}

// -- The shared log ----------------------------------------------------------

/// Construct a log in the caller's storage and name its writer.
///
/// `app` and `sport` are NUL-terminated ASCII; anything outside
/// `[A-Za-z0-9_-]` is dropped rather than escaped.
///
/// # Safety
/// `h` must be writable storage of at least `trainkit_history_bytes()`, aligned
/// to `trainkit_history_align()`. `app` and `sport` must be NUL-terminated or
/// null.
#[no_mangle]
pub unsafe extern "C" fn trainkit_history_init(h: *mut c_void, app: *const u8, sport: *const u8) {
    if h.is_null() {
        return;
    }
    core::ptr::write(h as *mut History, History::new());
    if let Some(hist) = (h as *mut History).as_mut() {
        hist.name(cbytes(app), cbytes(sport));
    }
}

/// Read a store file's bytes; `len` 0 is an absent file, which is not an error.
///
/// # Safety
/// `h` must have been passed to `trainkit_history_init`. `buf` must be readable
/// for `len` bytes.
#[no_mangle]
pub unsafe extern "C" fn trainkit_history_load(h: *mut c_void, buf: *const u8, len: u32) -> i32 {
    let hist = match (h as *mut History).as_mut() {
        Some(hist) => hist,
        None => return LOAD_UNREADABLE,
    };
    let bytes: &[u8] = if buf.is_null() || len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(buf, len as usize)
    };
    match hist.load(bytes) {
        Load::Ok => LOAD_OK,
        Load::Newer => LOAD_NEWER,
        Load::Unreadable => LOAD_UNREADABLE,
    }
}

/// # Safety
/// `h` must have been passed to `trainkit_history_init`; `s` must be readable.
#[no_mangle]
pub unsafe extern "C" fn trainkit_history_add(h: *mut c_void, s: *const Session) {
    if let (Some(hist), Some(session)) = ((h as *mut History).as_mut(), s.as_ref()) {
        hist.add(session);
    }
}

/// Serialise, dropping the oldest entries until the result fits `cap`.
///
/// @return the bytes written, or -1 when not even one session fits, which
///         leaves the caller to write nothing rather than a truncated file.
///
/// # Safety
/// `h` must have been passed to `trainkit_history_init`. `buf` must be writable
/// for `cap` bytes.
#[no_mangle]
pub unsafe extern "C" fn trainkit_history_save(h: *mut c_void, buf: *mut u8, cap: u32) -> i32 {
    let hist = match (h as *mut History).as_mut() {
        Some(hist) => hist,
        None => return -1,
    };
    if buf.is_null() || cap == 0 {
        return -1;
    }
    let out = core::slice::from_raw_parts_mut(buf, cap as usize);
    match hist.save(out) {
        Some(n) => n as i32,
        None => -1,
    }
}

// -- Load arithmetic, for whatever reads the log -----------------------------

/// Edwards' TRIMP in minute-weights, or -1 when this ladder is not the one the
/// weights are defined over.
///
/// # Safety
/// `s` must be readable.
#[no_mangle]
pub unsafe extern "C" fn trainkit_edwards_trimp(s: *const Session) -> i32 {
    match s.as_ref().and_then(crate::load::edwards_trimp) {
        Some(t) => t.min(i32::MAX as u32) as i32,
        None => -1,
    }
}

#[no_mangle]
pub extern "C" fn trainkit_avg_power_w(work_kj: u16, active_s: u32) -> u16 {
    crate::load::avg_power_w(work_kj, active_s)
}

#[no_mangle]
pub extern "C" fn trainkit_efficiency_factor_x1000(work_kj: u16, active_s: u32, hr_avg: u8) -> u32 {
    crate::load::efficiency_factor_x1000(work_kj, active_s, hr_avg)
}

/// The bytes of a NUL-terminated name, bounded by what the header can hold.
///
/// # Safety
/// `p` must be NUL-terminated, or null.
unsafe fn cbytes<'a>(p: *const u8) -> &'a [u8] {
    if p.is_null() {
        return &[];
    }
    let mut n = 0usize;
    while n < crate::history::MAX_NAME_LEN && *p.add(n) != 0 {
        n += 1;
    }
    core::slice::from_raw_parts(p, n)
}
