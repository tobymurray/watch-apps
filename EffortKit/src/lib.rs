//! Session structure for the UNA Watch's activity apps.
//!
//! Four things, in the order they depend on each other: [`epoch`] reduces a raw
//! IMU stream to one feature vector a second, [`segment`] runs a hysteresis
//! state machine over those vectors, [`recovery`] measures heart-rate fall
//! across rest windows the segmenter identified, and [`baseline`] keeps what a
//! session measured so the next one has something to be compared against.
//!
//! The crate refuses to produce a number it cannot defend. Both [`segment`] and
//! [`recovery`] take a calibration that has no default and cannot be
//! constructed without naming the recordings that set it; with none supplied
//! they report [`segment::Unavailable`] rather than a plausible figure. See
//! `README.md` for why that is a type and not a convention.

#![cfg_attr(not(feature = "std"), no_std)]
#![deny(missing_docs)]

pub mod baseline;
pub mod epoch;
pub mod profile;
pub mod recovery;
pub mod segment;
pub mod session;

#[cfg(feature = "std")]
pub mod fixture;

/// Names the recordings a set of tuned numbers came from.
///
/// Carried by every calibration struct so that a threshold and its provenance
/// cannot be separated by a refactor: there is no way to build the one without
/// the other.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Provenance {
    /// Recording ids from `Squash/Docs/RECORDING-PROTOCOL.md`, e.g. `"M1,M2,M3"`.
    pub recordings: &'static str,
    /// ISO date the numbers were derived, so a stale calibration is visible.
    pub measured_on: &'static str,
    /// Where the derivation is written up.
    pub method: &'static str,
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
