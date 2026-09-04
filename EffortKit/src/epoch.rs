//! One feature vector per epoch, from the raw IMU stream.
//!
//! The features here are candidates, not choices. Which of them separates a
//! rally from a rest is the question Phase A2 exists to answer, so the
//! accumulator computes all of them and the analyser reports their
//! distributions; nothing in this module decides anything.

/// One 6-axis sample in raw sensor LSB, as `SensorDataParser::FusionRaw` delivers it.
///
/// Raw rather than scaled, so a railed axis stays visible as the range limit.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ImuSample {
    /// Accelerometer X, LSB.
    pub ax: i16,
    /// Accelerometer Y, LSB.
    pub ay: i16,
    /// Accelerometer Z, LSB.
    pub az: i16,
    /// Gyroscope X, LSB.
    pub gx: i16,
    /// Gyroscope Y, LSB.
    pub gy: i16,
    /// Gyroscope Z, LSB.
    pub gz: i16,
}

impl ImuSample {
    /// True when any accelerometer axis sits at the end of its 16-bit range.
    ///
    /// The BMI270 reports +/-8 g at 4096 LSB/g, so full scale is exactly the
    /// signed 16-bit range and a railed axis can only read `i16::MAX` or
    /// `i16::MIN`. No tolerance is needed for the same reason.
    pub const fn accel_saturated(&self) -> bool {
        is_railed(self.ax) || is_railed(self.ay) || is_railed(self.az)
    }

    /// True when any gyroscope axis sits at the end of its 16-bit range.
    ///
    /// +/-2000 dps at 16.4 LSB/dps is 32 800 LSB, past the 32 767 the register
    /// can hold, so the gyro rails a little before its nominal range limit.
    pub const fn gyro_saturated(&self) -> bool {
        is_railed(self.gx) || is_railed(self.gy) || is_railed(self.gz)
    }
}

const fn is_railed(v: i16) -> bool {
    v == i16::MAX || v == i16::MIN
}

fn magnitude(x: i16, y: i16, z: i16) -> f32 {
    let (x, y, z) = (x as f32, y as f32, z as f32);
    libm::sqrtf(x * x + y * y + z * z)
}

/// Epoch length.
///
/// Fixed rather than configurable because every downstream dwell time is
/// counted in epochs, and a session recorded at one length would not compare
/// with a baseline built at another. One second sits between the two durations
/// it has to separate: a stroke lasts a few hundred milliseconds and the
/// shortest rest this app cares about is ten seconds.
pub const EPOCH_MS: u32 = 1000;

/// Nominal samples in one epoch at the 100 Hz the app subscribes at.
pub const NOMINAL_SAMPLES_PER_EPOCH: u16 = 100;

/// What one epoch of the IMU stream looked like.
///
/// Every field is in raw sensor LSB or a dimensionless fraction; nothing here
/// is converted to g or dps, so a value that railed is still at the rail.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EpochFeatures {
    /// Epoch index from the recording's origin, so epoch `n` covers
    /// `[n * EPOCH_MS, (n + 1) * EPOCH_MS)`.
    pub index: u32,
    /// Samples that actually arrived.
    pub samples: u16,
    /// Mean accelerometer vector magnitude, LSB.
    pub accel_mag_mean: f32,
    /// Variance of accelerometer vector magnitude, LSB squared.
    pub accel_mag_var: f32,
    /// Mean absolute change in accelerometer magnitude between consecutive
    /// samples, LSB.
    pub accel_jerk_mean: f32,
    /// Mean gyroscope vector magnitude, LSB.
    pub gyro_mag_mean: f32,
    /// Largest gyroscope vector magnitude, LSB.
    pub gyro_mag_max: f32,
    /// Fraction of samples with any accelerometer axis railed.
    pub accel_sat_frac: f32,
    /// Fraction of samples with any gyroscope axis railed.
    pub gyro_sat_frac: f32,
}

impl EpochFeatures {
    /// Start of this epoch on the recording's clock, ms.
    pub const fn start_ms(&self) -> u32 {
        self.index * EPOCH_MS
    }

    /// True when the epoch carries at least `min_frac` of the samples it should.
    ///
    /// An epoch that lost half its samples has a variance computed over the
    /// half that arrived, which is not the same measurement.
    pub fn complete(&self, min_frac: f32) -> bool {
        self.samples as f32 >= NOMINAL_SAMPLES_PER_EPOCH as f32 * min_frac
    }
}

/// Accumulates samples into whole epochs, one feature vector at a time.
///
/// Allocation-free and fixed-size: the running statistics are all incremental,
/// so no epoch's samples are ever held.
#[derive(Clone, Debug, Default)]
pub struct EpochAccumulator {
    index: u32,
    started: bool,
    samples: u16,
    accel_sum: f32,
    accel_m2: f32,
    accel_mean: f32,
    jerk_sum: f32,
    prev_accel_mag: f32,
    has_prev: bool,
    gyro_sum: f32,
    gyro_max: f32,
    accel_sat: u16,
    gyro_sat: u16,
}

impl EpochAccumulator {
    /// A fresh accumulator, positioned before the first epoch.
    ///
    /// `const` so that a caller holding one inside a `static` can initialise it
    /// in place; see [`crate::segment::Segmenter::new`] for why that matters.
    pub const fn new() -> Self {
        Self {
            index: 0,
            started: false,
            samples: 0,
            accel_sum: 0.0,
            accel_m2: 0.0,
            accel_mean: 0.0,
            jerk_sum: 0.0,
            prev_accel_mag: 0.0,
            has_prev: false,
            gyro_sum: 0.0,
            gyro_max: 0.0,
            accel_sat: 0,
            gyro_sat: 0,
        }
    }

    /// Feed one sample.
    ///
    /// `t_ms` is on the recording's clock, where 0 is the first sample.
    /// Returns the completed epoch when this sample belongs to a later one.
    /// Samples must arrive in order; one that goes backwards is dropped, since
    /// reordering would need a buffer this class exists to avoid.
    pub fn push(&mut self, t_ms: u32, sample: &ImuSample) -> Option<EpochFeatures> {
        let index = t_ms / EPOCH_MS;

        if !self.started {
            self.index = index;
            self.started = true;
        }

        if index < self.index {
            return None;
        }

        let completed = if index > self.index {
            let finished = self.finish();
            self.reset_to(index);
            Some(finished)
        } else {
            None
        };

        self.absorb(sample);
        completed
    }

    /// Close the epoch in progress, if any samples have landed in it.
    ///
    /// Called at the end of a recording, where there is no later sample to push
    /// the last epoch out.
    pub fn flush(&mut self) -> Option<EpochFeatures> {
        if !self.started || self.samples == 0 {
            return None;
        }
        let finished = self.finish();
        let next = self.index + 1;
        self.reset_to(next);
        Some(finished)
    }

    fn absorb(&mut self, s: &ImuSample) {
        let accel = magnitude(s.ax, s.ay, s.az);
        let gyro = magnitude(s.gx, s.gy, s.gz);

        self.samples = self.samples.saturating_add(1);
        self.accel_sum += accel;

        let delta = accel - self.accel_mean;
        self.accel_mean += delta / self.samples as f32;
        self.accel_m2 += delta * (accel - self.accel_mean);

        if self.has_prev {
            self.jerk_sum += libm::fabsf(accel - self.prev_accel_mag);
        }
        self.prev_accel_mag = accel;
        self.has_prev = true;

        self.gyro_sum += gyro;
        if gyro > self.gyro_max {
            self.gyro_max = gyro;
        }

        if s.accel_saturated() {
            self.accel_sat += 1;
        }
        if s.gyro_saturated() {
            self.gyro_sat += 1;
        }
    }

    fn finish(&self) -> EpochFeatures {
        let n = self.samples as f32;
        let jerk_pairs = if self.samples > 1 { n - 1.0 } else { 1.0 };
        EpochFeatures {
            index: self.index,
            samples: self.samples,
            accel_mag_mean: if self.samples > 0 { self.accel_sum / n } else { 0.0 },
            accel_mag_var: if self.samples > 1 { self.accel_m2 / (n - 1.0) } else { 0.0 },
            accel_jerk_mean: self.jerk_sum / jerk_pairs,
            gyro_mag_mean: if self.samples > 0 { self.gyro_sum / n } else { 0.0 },
            gyro_mag_max: self.gyro_max,
            accel_sat_frac: if self.samples > 0 { self.accel_sat as f32 / n } else { 0.0 },
            gyro_sat_frac: if self.samples > 0 { self.gyro_sat as f32 / n } else { 0.0 },
        }
    }

    fn reset_to(&mut self, index: u32) {
        let prev_accel_mag = self.prev_accel_mag;
        let has_prev = self.has_prev;
        *self = Self::default();
        self.index = index;
        self.started = true;
        // Carried across the boundary so the first jerk of an epoch is a real
        // difference rather than a discarded one.
        self.prev_accel_mag = prev_accel_mag;
        self.has_prev = has_prev;
    }
}

/// The epoch feature the segmenter thresholds on.
///
/// Which one it should be is [`crate::segment`]'s calibration to name, because
/// only a recording can say; this enum is how that choice is written down.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Feature {
    /// Variance of accelerometer magnitude.
    AccelVariance,
    /// Mean absolute change in accelerometer magnitude between samples.
    AccelJerk,
    /// Mean gyroscope magnitude.
    GyroMean,
    /// Fraction of the epoch with the gyroscope railed.
    GyroSaturation,
}

impl Feature {
    /// Read this feature out of an epoch.
    pub fn of(&self, e: &EpochFeatures) -> f32 {
        match self {
            Feature::AccelVariance => e.accel_mag_var,
            Feature::AccelJerk => e.accel_jerk_mean,
            Feature::GyroMean => e.gyro_mag_mean,
            Feature::GyroSaturation => e.gyro_sat_frac,
        }
    }

    /// Name, for reports and for the analyser's column headings.
    pub const fn name(&self) -> &'static str {
        match self {
            Feature::AccelVariance => "accel_var",
            Feature::AccelJerk => "accel_jerk",
            Feature::GyroMean => "gyro_mean",
            Feature::GyroSaturation => "gyro_sat",
        }
    }
}

/// Every feature the accumulator computes, in report order.
pub const ALL_FEATURES: [Feature; 4] = [
    Feature::AccelVariance,
    Feature::AccelJerk,
    Feature::GyroMean,
    Feature::GyroSaturation,
];

/// A provenance for numbers that came from nowhere.
///
/// Only constructible under test, so a calibration built from it cannot reach
/// the watch build.
#[cfg(any(test, feature = "std"))]
pub const UNMEASURED: crate::Provenance = crate::Provenance::Measured {
    recordings: "none",
    measured_on: "never",
    method: "arbitrary values that exercise the machine and mean nothing",
};

#[cfg(test)]
mod tests {
    use super::*;

    fn still(n: usize) -> impl Iterator<Item = (u32, ImuSample)> {
        // Gravity on +Z at 4096 LSB/g and nothing else moving.
        (0..n).map(|i| (i as u32 * 10, ImuSample { az: 4096, ..Default::default() }))
    }

    #[test]
    fn a_still_epoch_has_no_variance_and_no_jerk() {
        let mut acc = EpochAccumulator::new();
        let mut out = None;
        for (t, s) in still(101) {
            if let Some(e) = acc.push(t, &s) {
                out = Some(e);
            }
        }
        let e = out.expect("the 101st sample crosses into the next epoch");
        assert_eq!(e.index, 0);
        assert_eq!(e.samples, 100);
        assert_eq!(e.accel_mag_mean, 4096.0);
        assert_eq!(e.accel_mag_var, 0.0);
        assert_eq!(e.accel_jerk_mean, 0.0);
        assert_eq!(e.accel_sat_frac, 0.0);
    }

    #[test]
    fn a_railed_axis_is_counted_as_saturated() {
        let mut acc = EpochAccumulator::new();
        for i in 0..100u32 {
            let s = if i < 25 {
                ImuSample { ax: i16::MAX, az: 4096, gx: i16::MIN, ..Default::default() }
            } else {
                ImuSample { az: 4096, ..Default::default() }
            };
            acc.push(i * 10, &s);
        }
        let e = acc.flush().expect("samples landed in the epoch");
        assert_eq!(e.accel_sat_frac, 0.25);
        assert_eq!(e.gyro_sat_frac, 0.25);
    }

    #[test]
    fn variance_grows_with_movement() {
        let mut quiet = EpochAccumulator::new();
        let mut moving = EpochAccumulator::new();
        for i in 0..100u32 {
            quiet.push(i * 10, &ImuSample { az: 4096, ..Default::default() });
            let swing = if i % 2 == 0 { 8000 } else { 1000 };
            moving.push(i * 10, &ImuSample { az: swing, ..Default::default() });
        }
        let q = quiet.flush().unwrap();
        let m = moving.flush().unwrap();
        assert!(m.accel_mag_var > q.accel_mag_var);
        assert!(m.accel_jerk_mean > q.accel_jerk_mean);
    }

    #[test]
    fn a_gap_leaves_the_epoch_short_and_the_index_correct() {
        let mut acc = EpochAccumulator::new();
        acc.push(0, &ImuSample { az: 4096, ..Default::default() });
        // Next sample is three seconds later: epoch 0 closes with one sample and
        // epochs 1 and 2 are never emitted, since nothing landed in them.
        let e = acc.push(3000, &ImuSample { az: 4096, ..Default::default() }).unwrap();
        assert_eq!(e.index, 0);
        assert_eq!(e.samples, 1);
        assert!(!e.complete(0.9));
        let last = acc.flush().unwrap();
        assert_eq!(last.index, 3);
    }

    #[test]
    fn samples_that_go_backwards_are_dropped() {
        let mut acc = EpochAccumulator::new();
        acc.push(1500, &ImuSample { az: 4096, ..Default::default() });
        assert!(acc.push(500, &ImuSample { az: 4096, ..Default::default() }).is_none());
        let e = acc.flush().unwrap();
        assert_eq!(e.index, 1);
        assert_eq!(e.samples, 1);
    }
}
