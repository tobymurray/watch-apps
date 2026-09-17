//! The SquashLab Service's half of [`effortkit`], as a C ABI.
//!
//! The Service owns the clock, the sensor and the filesystem; this owns the
//! arithmetic. The detector is [`effortkit::shot`] and so is the reasoning.
//!
//! The point of routing the drill screen's count through here is that the
//! number the wearer compares against their own, on court, and the number a
//! later analysis gets by running the same detector over the same recording,
//! are produced by one implementation. A second implementation for the screen
//! would make the calibration a comparison of three things instead of two.

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

use core::cell::UnsafeCell;

use effortkit::epoch::ImuSample;
use effortkit::shot::{Config, Detector};

#[cfg(not(feature = "std"))]
extern "C" {
    fn squashlab_engine_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic stops the Service silently, and a drill in progress is
/// lost with no record of why.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"squashlab_engine panic";
    unsafe { squashlab_engine_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// Referenced by the pre-built `core` for host targets, which is compiled to
/// unwind even though every profile here aborts. Never called.
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

/// None until the Service has been given a threshold. There is no default,
/// because [`effortkit::shot`] has no calibration and inventing one here would
/// put a number on the glass that nothing measured.
static DETECTOR: Single<Option<Detector>> = Single::new(None);

/// Begin counting with these levels, discarding any drill in progress.
///
/// Returns 1 when the configuration was accepted. A rejected one leaves the
/// detector absent rather than half-configured, so `count` reads 0 and the
/// screen shows a drill counting nothing — which is visible, where a silently
/// substituted threshold would not be.
#[no_mangle]
pub extern "C" fn squashlab_engine_start(
    enter_above: u32,
    exit_below: u32,
    refractory_ms: u32,
) -> u8 {
    match Config::new(enter_above, exit_below, refractory_ms) {
        Some(cfg) => {
            *DETECTOR.get() = Some(Detector::new(cfg));
            1
        }
        None => {
            *DETECTOR.get() = None;
            0
        }
    }
}

/// Feed one sample on the recording's own clock; returns 1 if it completed a
/// shot.
///
/// The caller passes the sensor's timestamp, not a loop-local now: a batch
/// carries ~10 samples and collapsing them onto one instant would move every
/// shot's time and merge the ones inside a refractory window.
#[no_mangle]
pub extern "C" fn squashlab_engine_push(
    t_ms: u32,
    ax: i16,
    ay: i16,
    az: i16,
    gx: i16,
    gy: i16,
    gz: i16,
) -> u8 {
    let s = ImuSample { ax, ay, az, gx, gy, gz };
    match DETECTOR.get() {
        Some(d) => u8::from(d.push(t_ms, &s).is_some()),
        None => 0,
    }
}

/// Shots counted in the current drill; 0 when no detector is configured.
#[no_mangle]
pub extern "C" fn squashlab_engine_count() -> u32 {
    DETECTOR.get().as_ref().map_or(0, Detector::count)
}

/// 1 when a threshold was accepted and counting is live.
#[no_mangle]
pub extern "C" fn squashlab_engine_ready() -> u8 {
    u8::from(DETECTOR.get().is_some())
}

/// Drop an excursion in progress, at the end of a drill.
#[no_mangle]
pub extern "C" fn squashlab_engine_flush() {
    if let Some(d) = DETECTOR.get() {
        d.flush();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The ABI is one process-wide set of statics, so tests cannot overlap.
    fn alone() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
        LOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn push(t: u32, g: i16) -> u8 {
        squashlab_engine_push(t, 0, 0, 4096, g, 0, 0)
    }

    #[test]
    fn a_drill_counts_the_swings_it_saw() {
        let _guard = alone();
        assert_eq!(squashlab_engine_start(16_000, 8_000, 300), 1);
        assert_eq!(squashlab_engine_ready(), 1);
        for i in 0..3 {
            let base = i * 1_000;
            push(base, 20_000);
            push(base + 10, 30_000);
            push(base + 20, 100);
        }
        assert_eq!(squashlab_engine_count(), 3);
    }

    #[test]
    fn a_threshold_without_hysteresis_leaves_the_detector_absent() {
        let _guard = alone();
        assert_eq!(squashlab_engine_start(8_000, 8_000, 300), 0);
        assert_eq!(squashlab_engine_ready(), 0);
        // Pushing at it is harmless and counts nothing, which is what a drill
        // screen showing zero is telling the wearer.
        push(0, 30_000);
        push(10, 100);
        assert_eq!(squashlab_engine_count(), 0);
    }

    #[test]
    fn starting_a_drill_forgets_the_last_one() {
        let _guard = alone();
        squashlab_engine_start(16_000, 8_000, 300);
        push(0, 30_000);
        push(10, 100);
        assert_eq!(squashlab_engine_count(), 1);
        squashlab_engine_start(16_000, 8_000, 300);
        assert_eq!(squashlab_engine_count(), 0);
    }
}
