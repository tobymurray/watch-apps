//! Heart-rate recovery, training load and the shared session log, for any
//! activity app on this watch. Design record and schema contract:
//! `TrainKit/README.md`.
//!
//! WHY THIS IS A CRATE AND NOT A FILE IN ONE APP. Spin, Squash, RunMap, HikeMap
//! and BikeMap all connect `HEART_RATE_EX`, all run a pause, and all end a
//! session; the only thing that differs is the sport. Recovery detection and
//! the log's schema are the same arithmetic in all five, and a second copy is a
//! second thing to keep in step -- the argument `Spin/README.md` already makes
//! for reading the watch's own zones rather than restating them.
//!
//! NO SDK TYPES, NO CLOCK, NO FILESYSTEM. Everything here is a pure function of
//! what it is handed, which is what lets `cargo test` cover it without a
//! kernel. The app's Service owns the seconds and the file; see
//! `include/trainkit.h` for the boundary.

// `cargo test --features std`: the crate is no_std for the watch, and a test
// binary cannot link one whose panics do not unwind.
#![cfg_attr(not(feature = "std"), no_std)]

pub mod history;
pub mod json;
pub mod load;
pub mod record;
pub mod recovery;

mod ffi;

pub use history::{History, Load};
pub use record::{Recovery, Session};
pub use recovery::{Detector, Step};

#[cfg(not(feature = "std"))]
extern "C" {
    fn trainkit_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic is silent, and the caller cannot tell a log that was
/// not written from one that was.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"panic";
    unsafe { trainkit_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

// -- The C ABI's shape -------------------------------------------------------
// A stale libtrainkit.a linked against a changed struct is otherwise silent
// until it writes a file whose numbers are in the wrong fields.

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `trainkit_abi::fingerprint()`
/// in `include/trainkit.h`.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Recovery>());
    let h = fnv1a(h, core::mem::align_of::<Recovery>());
    let h = fnv1a(h, core::mem::offset_of!(Recovery, at_active_s));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, hr0));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, hr_end));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, window_s));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, trusted_s));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, hr0_pct_max));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, trigger));
    let h = fnv1a(h, core::mem::offset_of!(Recovery, curve));
    let h = fnv1a(h, core::mem::size_of::<Session>());
    let h = fnv1a(h, core::mem::align_of::<Session>());
    let h = fnv1a(h, core::mem::offset_of!(Session, start_utc));
    let h = fnv1a(h, core::mem::offset_of!(Session, active_s));
    let h = fnv1a(h, core::mem::offset_of!(Session, elapsed_s));
    let h = fnv1a(h, core::mem::offset_of!(Session, kcal));
    let h = fnv1a(h, core::mem::offset_of!(Session, work_kj));
    let h = fnv1a(h, core::mem::offset_of!(Session, zone_s));
    let h = fnv1a(h, core::mem::offset_of!(Session, zone_floor));
    let h = fnv1a(h, core::mem::offset_of!(Session, hr_avg));
    let h = fnv1a(h, core::mem::offset_of!(Session, hr_max));
    let h = fnv1a(h, core::mem::offset_of!(Session, hr_max_setting));
    let h = fnv1a(h, core::mem::offset_of!(Session, weight_kg));
    let h = fnv1a(h, core::mem::offset_of!(Session, zone_count));
    let h = fnv1a(h, core::mem::offset_of!(Session, recovery_count));
    let h = fnv1a(h, core::mem::offset_of!(Session, recoveries_dropped));
    fnv1a(h, core::mem::offset_of!(Session, recoveries))
}
