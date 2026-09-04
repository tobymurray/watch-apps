//! What the types a Service holds actually cost it.
//!
//! Not limits being enforced — measurements being kept, because the Service
//! that holds them has a 10 KiB stack and no allocator, and the last time one
//! of these grew unnoticed it took the watch with it.

use effortkit::window::{Calibration, Detector, Thresholds};
use effortkit::Provenance;

/// The Squash Service's stack, from `una_app_build_service`'s default.
const SERVICE_STACK_BYTES: usize = 10 * 1024;

#[test]
fn a_detector_stays_small_enough_to_hold_several_of() {
    let n = core::mem::size_of::<Detector>();
    // A calibration carries a provenance for every one of its numbers, so it is
    // an order of magnitude larger than the state machine that reads it. The
    // detector borrows one rather than owning it; holding it by value made the
    // detector 1,976 bytes on a 64-bit host and overflowed the Service's stack
    // in `squash_engine`'s own start-up test.
    assert!(
        n < 512,
        "Detector is {n} bytes; it borrows its calibration, so it should not \
         be near Calibration's {}",
        core::mem::size_of::<Calibration>()
    );
    assert!(
        n * 8 < SERVICE_STACK_BYTES,
        "a Service could not hold several detectors: {n} bytes each"
    );
}

#[test]
fn a_calibration_is_static_metadata_and_may_be_large() {
    // Nothing copies one per session, so its size is a fact rather than a cost.
    // Recorded to make the ratio above meaningful.
    assert!(core::mem::size_of::<Provenance>() > 0);
    assert!(core::mem::size_of::<Thresholds>() > core::mem::size_of::<Provenance>());
    assert!(core::mem::size_of::<Calibration>() > core::mem::size_of::<Thresholds>());
}
