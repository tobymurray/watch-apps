//! The C ABI's shape, as the compiler actually produced it.
//!
//! `include/trainkit.h` restates every one of these numbers as a
//! `static_assert`, and the fingerprint below is what catches a header and an
//! archive that disagree at run time rather than at the first wrong file.

use trainkit::record::{Recovery, Session};

extern "C" {
    fn trainkit_abi_fingerprint() -> u32;
    fn trainkit_detector_bytes() -> u32;
    fn trainkit_detector_align() -> u32;
    fn trainkit_history_bytes() -> u32;
    fn trainkit_history_align() -> u32;
    fn trainkit_max_store_bytes() -> u32;
}

#[test]
fn the_structs_are_where_the_header_says() {
    assert_eq!(core::mem::size_of::<Recovery>(), 20);
    assert_eq!(core::mem::align_of::<Recovery>(), 4);
    assert_eq!(core::mem::offset_of!(Recovery, at_active_s), 0);
    assert_eq!(core::mem::offset_of!(Recovery, hr0), 4);
    assert_eq!(core::mem::offset_of!(Recovery, hr_end), 5);
    assert_eq!(core::mem::offset_of!(Recovery, window_s), 6);
    assert_eq!(core::mem::offset_of!(Recovery, trusted_s), 7);
    assert_eq!(core::mem::offset_of!(Recovery, hr0_pct_max), 8);
    assert_eq!(core::mem::offset_of!(Recovery, trigger), 9);
    assert_eq!(core::mem::offset_of!(Recovery, curve), 10);
    assert_eq!(core::mem::offset_of!(Recovery, reserved), 17);

    assert_eq!(core::mem::size_of::<Session>(), 92);
    assert_eq!(core::mem::align_of::<Session>(), 4);
    assert_eq!(core::mem::offset_of!(Session, start_utc), 0);
    assert_eq!(core::mem::offset_of!(Session, active_s), 4);
    assert_eq!(core::mem::offset_of!(Session, elapsed_s), 8);
    assert_eq!(core::mem::offset_of!(Session, kcal), 12);
    assert_eq!(core::mem::offset_of!(Session, work_kj), 14);
    assert_eq!(core::mem::offset_of!(Session, zone_s), 16);
    assert_eq!(core::mem::offset_of!(Session, zone_floor), 34);
    assert_eq!(core::mem::offset_of!(Session, hr_avg), 42);
    assert_eq!(core::mem::offset_of!(Session, hr_max), 43);
    assert_eq!(core::mem::offset_of!(Session, hr_max_setting), 44);
    assert_eq!(core::mem::offset_of!(Session, weight_kg), 45);
    assert_eq!(core::mem::offset_of!(Session, zone_count), 46);
    assert_eq!(core::mem::offset_of!(Session, recovery_count), 47);
    assert_eq!(core::mem::offset_of!(Session, recoveries_dropped), 48);
    assert_eq!(core::mem::offset_of!(Session, reserved), 49);
    assert_eq!(core::mem::offset_of!(Session, recoveries), 52);
}

#[test]
fn the_opaque_storage_fits_what_the_header_reserves() {
    // trainkit.h sizes both blobs in uint64_t, so these are the numbers the
    // header's TRAINKIT_*_WORDS must cover.
    unsafe {
        assert!(
            trainkit_detector_bytes() <= 8 * 24,
            "{}",
            trainkit_detector_bytes()
        );
        assert!(trainkit_detector_align() <= 8);
        assert!(
            trainkit_history_bytes() <= 8 * 250,
            "{}",
            trainkit_history_bytes()
        );
        assert!(trainkit_history_align() <= 8);
        assert_eq!(trainkit_max_store_bytes(), 16 * 1024);
    }
}

/// Printed so the header's constant can be updated from a run rather than from
/// arithmetic done by hand.
#[test]
fn the_fingerprint_is_stable() {
    let fp = unsafe { trainkit_abi_fingerprint() };
    println!("trainkit ABI fingerprint: 0x{fp:08X}");
    println!(
        "detector {} bytes / align {}, history {} bytes / align {}",
        unsafe { trainkit_detector_bytes() },
        unsafe { trainkit_detector_align() },
        unsafe { trainkit_history_bytes() },
        unsafe { trainkit_history_align() },
    );
    assert_ne!(fp, 0);
}
