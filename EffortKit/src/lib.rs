//! Heart-rate recovery, session structure and the record that outlives the
//! session, for any activity app on this watch. Design record and schema
//! contract: `README.md`.
//!
//! Layered in the order they depend on each other. [`hr`] is what a heart-rate
//! reading is; [`window`] measures the fall across one and is fed by a caller
//! that decides when effort ceased; [`record`], [`json`] and [`history`] are
//! the bounded cross-app log; [`profile`], [`session`] and [`baseline`] are the
//! app's own interior record and what normal looks like across sessions;
//! [`epoch`] and [`segment`] reduce a raw IMU stream to the cessations a
//! segmenting app feeds [`window`] with.
//!
//! WHAT OPENS A WINDOW IS AN INPUT, NOT A COMPONENT. Spin calls
//! [`window::Detector::cease`] from a button and Squash calls it from
//! [`segment`]; the measurement does not know which, and an app links only the
//! producer it uses. Measured on the watch toolchain: carrying the modules a
//! consumer never calls costs it 24 bytes of flash and nothing in RAM; see
//! `README.md` for the whole table.
//!
//! NO SDK TYPES, NO CLOCK, NO FILESYSTEM. Everything here is a pure function of
//! what it is handed, which is what lets `cargo test` cover it without a
//! kernel. The app's Service owns the seconds and the file; each app's shim
//! under `<App>/Software/Libs/rust/` owns the C ABI.

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

pub mod baseline;
pub mod epoch;
pub mod history;
pub mod hr;
pub mod json;
pub mod load;
pub mod profile;
pub mod record;
pub mod segment;
pub mod session;
pub mod window;

#[cfg(feature = "std")]
pub mod fixture;

/// Where a tuned number came from, carried by every calibration so that a
/// threshold and its evidence cannot be separated by a refactor.
///
/// Two kinds, because they are not the same kind of claim and a reader has to
/// be able to tell them apart. A segmentation level is [`Provenance::Measured`]
/// — nobody can know what accelerometer magnitude means "rally" without
/// recording it. A measurement interval is [`Provenance::Defined`] — HRR60's
/// 60 seconds is what the quantity *is*, fixed by the literature, and a local
/// recording cannot set it because a recording suggesting 47 s would not have
/// measured a better HRR60, it would have measured something else.
///
/// There is no third variant and no `Default`: a number reaching a wearer is
/// one of these two or it does not exist.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Provenance {
    /// A number this repository measured, and the recordings that set it.
    Measured {
        /// Recording ids from `Squash/Docs/RECORDING-PROTOCOL.md`, e.g. `"M1,M2"`.
        recordings: &'static str,
        /// ISO date the numbers were derived, so a stale calibration is visible.
        measured_on: &'static str,
        /// Where the derivation is written up.
        method: &'static str,
    },
    /// A number a published protocol defines, and the quantity it defines.
    Defined {
        /// The paper or standard, enough to find it.
        citation: &'static str,
        /// The quantity it fixes, e.g. `"HRR60: the fall over 60 s after cessation"`.
        defines: &'static str,
    },
}

impl Provenance {
    /// True when a recording in this repository set the number.
    pub const fn is_measured(&self) -> bool {
        matches!(self, Provenance::Measured { .. })
    }
}

/// Why a metric has no value to report.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Unavailable {
    /// No recording has set the thresholds this metric needs.
    NotCalibrated,
    /// Fewer sessions than the baseline needs before it will compare anything.
    WarmingUp {
        /// Sessions admitted so far.
        have: u16,
        /// Sessions required.
        need: u16,
    },
    /// The session did not meet the criteria for contributing a measurement.
    NoQualifyingWindow,
    /// The heart-rate stream was absent or too broken to measure across.
    HeartRateUnusable,
}

// -- The ABI's shape ----------------------------------------------------------

/// FNV-1a offset basis, exposed so each app's shim hashes its own layout the
/// same way its C++ header does.
pub const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
/// FNV-1a prime, as above.
pub const FNV_PRIME: u32 = 0x0100_0193;

/// One FNV-1a step over the low byte of a size or offset.
///
/// Every shim walks its own structs with this and its C++ header walks the same
/// values with the same function, so a struct that drifts on either side is a
/// refused start-up rather than a file whose numbers are in the wrong fields.
pub const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}
