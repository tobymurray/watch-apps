//! What one heart-rate reading is, and what makes it untrusted.
//!
//! MEASURED, over 34 minutes of `HEART_RATE_EX` pulled from this watch
//! (`Squash/Tests/pulled`, 2,021 samples, six sessions): every value is a whole
//! bpm — the arbitrated stream, the optical stream and the external stream all
//! carry `*_x100` values exactly divisible by 100 — consecutive readings differ
//! by a mean of 0.49 bpm with a median of 0, and 65% of steps are exactly zero.
//! The stream is integer-quantised and mostly repeats. Re-derive with
//! `Tools/hr_analyse.py`.
//!
//! That is why a fall is stored as a difference of two whole bpm and why the
//! curve is stored as evidence rather than as a promise that a time constant
//! fitted from it would mean anything: a fall of 8 to 20 bpm is 8 to 20
//! quantisation steps.

/// Where a heart rate came from, mirroring `HEART_RATE_EX`'s own source field.
///
/// The values are the kernel's, carried through unchanged.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum HrSource {
    /// The watch did not say.
    #[default]
    Unknown = 0,
    /// The watch's own wrist PPG.
    Optical = 1,
    /// A paired chest strap.
    External = 2,
}

impl HrSource {
    /// The kernel's own code for this source.
    pub const fn code(self) -> u8 {
        self as u8
    }

    /// A source from the kernel's code; anything unrecognised is `Unknown`.
    pub const fn from_code(v: u8) -> Self {
        match v {
            1 => HrSource::Optical,
            2 => HrSource::External,
            _ => HrSource::Unknown,
        }
    }
}

/// One heart-rate reading as a recording or a sensor callback delivers it.
///
/// The detector is fed whole seconds rather than these, because it is keyed on
/// the session's own clock; this is what a *recording* holds and what the
/// analysis tools read.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct HrSample {
    /// Milliseconds on the session clock.
    pub t_ms: u32,
    /// Beats per minute.
    pub bpm: f32,
    /// The watch's own 0-3 confidence; 0 is untrusted.
    pub trust: u8,
    /// Which sensor produced it.
    pub source: HrSource,
}

/// Which sources a recovery figure may be built from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SourcePolicy {
    /// Only a chest strap.
    ///
    /// Wrist optical during a racket sport is the adversarial case — grip
    /// tension, impact shock and the watch moving on the wrist all corrupt it,
    /// and a smoothed corrupted signal still looks smooth.
    ExternalOnly,
    /// Either, with the source recorded on every window so a reader can filter.
    EitherWithSourceRecorded,
}

impl SourcePolicy {
    /// Whether a reading from this source may contribute.
    pub const fn accepts(self, source: HrSource) -> bool {
        match self {
            SourcePolicy::ExternalOnly => matches!(source, HrSource::External),
            SourcePolicy::EitherWithSourceRecorded => true,
        }
    }
}

/// The bpm a reading carries once it has been believed or discarded.
///
/// Zero is the one value that means "no trusted reading", everywhere in this
/// crate. It is not a heart rate of zero and nothing ever treats it as one.
pub type Bpm = u8;

/// No trusted reading.
pub const UNTRUSTED: Bpm = 0;

/// Round a reading to the whole bpm the stream is quantised to, or [`UNTRUSTED`].
///
/// NaN falls through both comparisons and lands on [`UNTRUSTED`], which is the
/// only safe answer: a NaN that reaches a mean poisons it, and a poisoned mean
/// reaching the wire saturates to a plain `0` that a reader cannot tell from a
/// measured zero. That is a silent, permanent, locally-plausible wrong number,
/// which is the class of defect this crate exists to refuse.
pub fn clamp_bpm(bpm: f32) -> Bpm {
    if bpm >= 254.5 {
        255
    } else if bpm >= 0.5 {
        (bpm + 0.5) as Bpm
    } else {
        UNTRUSTED
    }
}

/// `value` as a percentage of `of`, saturating; 0 when `of` is 0.
pub fn pct_of(value: u8, of: u8) -> u8 {
    if of == 0 {
        return 0;
    }
    let pct = ((value as u32) * 100 + (of as u32) / 2) / (of as u32);
    pct.min(255) as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_nan_reading_is_untrusted_rather_than_a_number() {
        assert_eq!(clamp_bpm(f32::NAN), UNTRUSTED);
    }

    #[test]
    fn an_infinite_reading_saturates_rather_than_wrapping() {
        assert_eq!(clamp_bpm(f32::INFINITY), 255);
        assert_eq!(clamp_bpm(f32::NEG_INFINITY), UNTRUSTED);
    }

    #[test]
    fn a_reading_rounds_to_the_whole_bpm_the_stream_carries() {
        assert_eq!(clamp_bpm(65.4), 65);
        assert_eq!(clamp_bpm(65.5), 66);
        assert_eq!(clamp_bpm(0.4), UNTRUSTED);
    }

    #[test]
    fn external_only_refuses_the_wrist_and_the_unknown() {
        let p = SourcePolicy::ExternalOnly;
        assert!(p.accepts(HrSource::External));
        assert!(!p.accepts(HrSource::Optical));
        assert!(!p.accepts(HrSource::Unknown));
    }

    #[test]
    fn a_source_survives_the_kernels_code_unchanged() {
        for s in [HrSource::Unknown, HrSource::Optical, HrSource::External] {
            assert_eq!(HrSource::from_code(s.code()), s);
        }
        assert_eq!(HrSource::from_code(9), HrSource::Unknown);
    }
}
