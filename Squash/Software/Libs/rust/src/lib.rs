//! The Squash Service's half of [`effortkit`], as a C ABI.
//!
//! The Service owns the clock, the sensor and the filesystem; this owns the
//! arithmetic. Everything here is a thin shell — the features are
//! [`effortkit::epoch`] and so is the reasoning, in that crate's `README.md`.
//!
//! The point of routing the live display through here rather than computing
//! something similar in C++ is that the bars on the watch and the distributions
//! `phase-a` prints off the recording are then the same numbers, produced by one
//! implementation. A second implementation for the screen would be a second
//! thing to be wrong.
//!
//! This shim ships no calibration. [`effortkit::segment`] still reports
//! `NotCalibrated`, so nothing here decides what a rally is — the wearer does,
//! with a button, and that is what the labels are for.

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

use core::cell::UnsafeCell;

use effortkit::epoch::{EpochAccumulator, EpochFeatures, ImuSample};

#[cfg(not(feature = "std"))]
extern "C" {
    fn squash_engine_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic stops the Service silently, and a recording in progress
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
        // SAFETY: see above.
        unsafe { &mut *self.0.get() }
    }
}

static ACC: Single<EpochAccumulator> = Single::new(EpochAccumulator::new());
static LAST: Single<Epoch> = Single::new(Epoch::ZERO);

// ------------------------------------------------------------------ the ABI

/// One completed epoch, in the units the screen draws rather than the floats
/// the features are computed in.
///
/// Mirrors `squash_epoch` in `squash_engine.h`; the fingerprint below is what
/// checks it.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Epoch {
    /// Mean gyroscope vector magnitude, raw LSB, rounded.
    pub gyro_mag: u32,
    /// Variance of accelerometer magnitude, LSB^2 / 1000. Scaled because the
    /// raw figure reaches 2.5e8 on a real stroke and the screen wants a number
    /// it can ladder, not one it has to shorten.
    pub accel_var_k: u32,
    /// Epoch index from the recording's origin.
    pub index: u32,
    /// Samples that actually arrived; 100 is nominal at the rate Squash
    /// subscribes at.
    pub samples: u16,
    /// Percent of samples with any accelerometer axis railed.
    pub sat_accel_pct: u8,
    /// Percent of samples with any gyroscope axis railed.
    pub sat_gyro_pct: u8,
}

impl Epoch {
    const ZERO: Epoch = Epoch {
        gyro_mag: 0,
        accel_var_k: 0,
        index: 0,
        samples: 0,
        sat_accel_pct: 0,
        sat_gyro_pct: 0,
    };

    fn from_features(e: &EpochFeatures) -> Epoch {
        Epoch {
            gyro_mag: round_u32(e.gyro_mag_mean),
            accel_var_k: round_u32(e.accel_mag_var / 1000.0),
            index: e.index,
            samples: e.samples,
            sat_accel_pct: pct(e.accel_sat_frac),
            sat_gyro_pct: pct(e.gyro_sat_frac),
        }
    }
}

/// Saturating, because a NaN or a negative from a degenerate epoch must not
/// wrap into a huge reading on the screen.
fn round_u32(v: f32) -> u32 {
    if v.is_nan() || v <= 0.0 {
        return 0;
    }
    if v >= u32::MAX as f32 {
        return u32::MAX;
    }
    (v + 0.5) as u32
}

fn pct(frac: f32) -> u8 {
    if frac.is_nan() || frac <= 0.0 {
        return 0;
    }
    let v = frac * 100.0 + 0.5;
    if v >= 100.0 {
        100
    } else {
        v as u8
    }
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `squash_engine_abi::fingerprint()`.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Epoch>());
    let h = fnv1a(h, core::mem::align_of::<Epoch>());
    let h = fnv1a(h, core::mem::offset_of!(Epoch, gyro_mag));
    let h = fnv1a(h, core::mem::offset_of!(Epoch, accel_var_k));
    let h = fnv1a(h, core::mem::offset_of!(Epoch, index));
    let h = fnv1a(h, core::mem::offset_of!(Epoch, samples));
    let h = fnv1a(h, core::mem::offset_of!(Epoch, sat_accel_pct));
    fnv1a(h, core::mem::offset_of!(Epoch, sat_gyro_pct))
}

/// The layout both sides must agree on; the Service refuses to start otherwise.
#[no_mangle]
pub extern "C" fn squash_engine_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Epoch>() == 16);
const _: () = assert!(core::mem::align_of::<Epoch>() == 4);

/// Begin a recording. Drops any epoch in progress, so a session started after
/// another one cannot inherit its partial window.
#[no_mangle]
pub extern "C" fn squash_engine_reset() {
    *ACC.get() = EpochAccumulator::new();
    *LAST.get() = Epoch::ZERO;
}

/// Feed one sample on the recording's own clock.
///
/// Returns 1 when this sample completed an epoch, at which point
/// [`squash_engine_last_epoch`] has the new features; 0 otherwise. The caller
/// passes the sensor's timestamp, not a loop-local now: a batch carries ~10
/// samples and collapsing them onto one instant would change every feature.
#[no_mangle]
pub extern "C" fn squash_engine_push(
    t_ms: u32,
    ax: i16,
    ay: i16,
    az: i16,
    gx: i16,
    gy: i16,
    gz: i16,
) -> u8 {
    let s = ImuSample { ax, ay, az, gx, gy, gz };
    match ACC.get().push(t_ms, &s) {
        Some(e) => {
            *LAST.get() = Epoch::from_features(&e);
            1
        }
        None => 0,
    }
}

/// Close out the epoch in progress, if it has any samples.
///
/// Returns 1 when a final epoch was produced. Called when a recording ends, so
/// the last partial second is not silently dropped from the display.
#[no_mangle]
pub extern "C" fn squash_engine_flush() -> u8 {
    match ACC.get().flush() {
        Some(e) => {
            *LAST.get() = Epoch::from_features(&e);
            1
        }
        None => 0,
    }
}

/// Copy the most recently completed epoch out.
///
/// # Safety
/// `out` must point to a writable `squash_epoch`.
#[no_mangle]
pub unsafe extern "C" fn squash_engine_last_epoch(out: *mut Epoch) {
    if out.is_null() {
        return;
    }
    *out = *LAST.get();
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The ABI is one process-wide set of statics, so tests cannot overlap.
    fn alone() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
        LOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// Rail on every axis, which both ranges reach during a real stroke.
    const RAILED: ImuSample =
        ImuSample { ax: i16::MAX, ay: 0, az: 0, gx: i16::MAX, gy: 0, gz: 0 };

    fn push(t_ms: u32, s: &ImuSample) -> u8 {
        squash_engine_push(t_ms, s.ax, s.ay, s.az, s.gx, s.gy, s.gz)
    }

    fn last() -> Epoch {
        let mut e = Epoch::ZERO;
        unsafe { squash_engine_last_epoch(&mut e) };
        e
    }

    #[test]
    fn an_epoch_closes_and_reports_its_sample_count() {
        let _guard = alone();
        squash_engine_reset();
        let still = ImuSample { ax: 0, ay: 0, az: 4096, gx: 0, gy: 0, gz: 0 };
        let mut closed = 0;
        for i in 0..100 {
            closed += push(i * 10, &still) as u32;
        }
        // The 100th sample sits at t=990, still inside the first epoch; the
        // epoch closes on the first sample of the next one.
        assert_eq!(closed, 0);
        assert_eq!(push(1_000, &still), 1);
        assert_eq!(last().samples, 100);
        assert_eq!(last().index, 0);
    }

    #[test]
    fn saturation_reaches_the_caller_as_a_percentage() {
        let _guard = alone();
        squash_engine_reset();
        for i in 0..100 {
            push(i * 10, &RAILED);
        }
        push(1_000, &RAILED);
        assert_eq!(last().sat_accel_pct, 100);
        assert_eq!(last().sat_gyro_pct, 100);
    }

    #[test]
    fn reset_drops_the_epoch_in_progress() {
        let _guard = alone();
        squash_engine_reset();
        let still = ImuSample { ax: 0, ay: 0, az: 4096, gx: 0, gy: 0, gz: 0 };
        for i in 0..50 {
            push(i * 10, &still);
        }
        squash_engine_reset();
        assert_eq!(last().samples, 0);
        // A fresh accumulator starts its clock again, so the next epoch is 0.
        for i in 0..100 {
            push(i * 10, &still);
        }
        assert_eq!(push(1_000, &still), 1);
        assert_eq!(last().index, 0);
    }

    #[test]
    fn flush_produces_the_partial_epoch_and_then_has_nothing_left() {
        let _guard = alone();
        squash_engine_reset();
        let still = ImuSample { ax: 0, ay: 0, az: 4096, gx: 0, gy: 0, gz: 0 };
        for i in 0..30 {
            push(i * 10, &still);
        }
        assert_eq!(squash_engine_flush(), 1);
        assert_eq!(last().samples, 30);
        assert_eq!(squash_engine_flush(), 0);
    }

    #[test]
    fn a_degenerate_feature_reads_as_zero_rather_than_wrapping() {
        assert_eq!(round_u32(f32::NAN), 0);
        assert_eq!(round_u32(-1.0), 0);
        assert_eq!(round_u32(f32::INFINITY), u32::MAX);
        assert_eq!(pct(f32::NAN), 0);
        assert_eq!(pct(-0.5), 0);
        assert_eq!(pct(2.0), 100);
    }
}
