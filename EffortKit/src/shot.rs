//! Candidate racquet impacts in the raw IMU stream.
//!
//! A shot is a discrete event, which makes it a different problem from
//! [`crate::segment`]'s continuous states and an easier one: a shot either
//! happened or it did not, and a drill of known length is exact ground truth
//! for how many there were. Segmentation has neither property — this
//! repository measured rally-against-rest at 17.9% held-out error with the
//! ground truth itself ambiguous, because walking to serve and walking to hit
//! are the same movement.
//!
//! **Nothing here is calibrated.** [`Config`] has no default and cannot be
//! inferred: the threshold that counts a knock-up correctly is unknown until a
//! drill with a counted number of shots has been recorded against it. What is
//! measured, on the 183-second labelled warm-up in
//! `Squash/Tests/pulled/20260916-v0.6.103-2s-and-3s`:
//!
//! | gyro threshold | shots found | per minute |
//! |---|---|---|
//! | 488 dps | 117 | 38.4 |
//! | 732 dps | 67 | 22.0 |
//! | 976 dps | 52 | 17.1 |
//! | 1220 dps | 45 | 14.8 |
//! | 1524 dps | 41 | 13.5 |
//!
//! The count climbs steeply below 732 dps and flattens above 976, which is the
//! shape of noise entering at the bottom and a stable population of real
//! impacts above it. A knock-up is roughly one shot every two to three seconds,
//! so the truth is somewhere in that band — and which end is a question only a
//! counted drill answers.

use crate::epoch::ImuSample;

/// What counts as an impact. Every field has to come from somewhere; see the
/// module docs for what is measured and what is not.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Config {
    /// Gyroscope vector magnitude, raw LSB, above which an excursion begins.
    ///
    /// Raw LSB rather than dps so the caller never converts: the recordings are
    /// raw and a conversion is a place to be wrong. 16.4 LSB per dps.
    pub enter_above: u32,
    /// Magnitude below which the excursion ends and the shot is emitted.
    ///
    /// Strictly below `enter_above`, so a reading sitting on the threshold
    /// cannot open and close a shot on alternate samples.
    pub exit_below: u32,
    /// Milliseconds after a shot during which another cannot start.
    ///
    /// A racquet swing is one event however many samples it spans. Squash's
    /// fastest exchange is still hundreds of milliseconds apart, so this is a
    /// guard against one swing counting twice rather than a limit on play.
    pub refractory_ms: u32,
}

impl Config {
    /// Build a configuration, refusing one whose thresholds cannot hysteresise.
    ///
    /// Returns `None` rather than clamping: a caller that asked for equal
    /// levels has a bug, and a detector that silently chatters on the boundary
    /// would count one shot many times.
    pub fn new(enter_above: u32, exit_below: u32, refractory_ms: u32) -> Option<Config> {
        if exit_below >= enter_above || refractory_ms == 0 {
            return None;
        }
        Some(Config { enter_above, exit_below, refractory_ms })
    }
}

/// One detected impact.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Shot {
    /// When the excursion peaked, on the recording's clock. The peak rather
    /// than the crossing: the crossing is where the swing got fast enough to
    /// notice, the peak is the closest thing in this signal to the ball.
    pub t_ms: u32,
    /// Largest gyroscope magnitude during the excursion, raw LSB.
    ///
    /// Saturates with the sensor. Both ranges rail during real squash — 2.6% to
    /// 3.9% of epochs carry a railed gyroscope axis across four sessions — so a
    /// peak at the rail is a lower bound on a stroke, never its speed.
    pub peak_gyro: u32,
    /// How long the excursion lasted, milliseconds.
    pub duration_ms: u32,
}

/// Emits a [`Shot`] per excursion above [`Config::enter_above`].
#[derive(Clone, Copy, Debug)]
pub struct Detector {
    config: Config,
    in_shot: bool,
    peak: u32,
    peak_t: u32,
    started_t: u32,
    last_emit_t: u32,
    have_emitted: bool,
    count: u32,
}

impl Detector {
    /// A detector that has seen nothing yet.
    pub fn new(config: Config) -> Detector {
        Detector {
            config,
            in_shot: false,
            peak: 0,
            peak_t: 0,
            started_t: 0,
            last_emit_t: 0,
            have_emitted: false,
            count: 0,
        }
    }

    /// Shots emitted so far. What a drill screen shows.
    #[must_use]
    pub fn count(&self) -> u32 {
        self.count
    }

    /// Feed one sample; returns a shot when an excursion just ended.
    ///
    /// Emitting on the fall rather than the rise costs the display the width of
    /// one swing and buys the peak, which a rising edge cannot know yet.
    pub fn push(&mut self, t_ms: u32, sample: &ImuSample) -> Option<Shot> {
        let mag = gyro_magnitude(sample);

        if !self.in_shot {
            if mag <= self.config.enter_above {
                return None;
            }
            // Unsigned subtraction, correct across the 32-bit tick wrap.
            if self.have_emitted && t_ms.wrapping_sub(self.last_emit_t) < self.config.refractory_ms
            {
                return None;
            }
            self.in_shot = true;
            self.peak = mag;
            self.peak_t = t_ms;
            self.started_t = t_ms;
            return None;
        }

        if mag > self.peak {
            self.peak = mag;
            self.peak_t = t_ms;
        }

        if mag >= self.config.exit_below {
            return None;
        }

        self.in_shot = false;
        self.last_emit_t = self.peak_t;
        self.have_emitted = true;
        self.count += 1;
        Some(Shot {
            t_ms: self.peak_t,
            peak_gyro: self.peak,
            duration_ms: t_ms.wrapping_sub(self.started_t),
        })
    }

    /// Drop any excursion in progress, for a recording that ended mid-swing.
    ///
    /// Not emitted: an excursion whose end was never seen has no peak that can
    /// be trusted to be one.
    pub fn flush(&mut self) {
        self.in_shot = false;
    }
}

/// Gyroscope vector magnitude in raw LSB.
///
/// Integer throughout: a squared magnitude of three i16s reaches 3.2e9, which
/// overflows u32 by a whisker, so the sum is taken in u64 and the root is
/// integer. No float, because the caller is a Service tick on a watch.
fn gyro_magnitude(s: &ImuSample) -> u32 {
    let sq = (s.gx as i64 * s.gx as i64 + s.gy as i64 * s.gy as i64 + s.gz as i64 * s.gz as i64)
        as u64;
    isqrt(sq)
}

fn isqrt(v: u64) -> u32 {
    if v == 0 {
        return 0;
    }
    let mut x = v;
    let mut y = x.div_ceil(2);
    while y < x {
        x = y;
        y = (x + v / x) / 2;
    }
    x as u32
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cfg() -> Config {
        Config::new(16_000, 8_000, 300).unwrap()
    }

    fn spin(g: i16) -> ImuSample {
        ImuSample { ax: 0, ay: 0, az: 4096, gx: g, gy: 0, gz: 0 }
    }

    #[test]
    fn a_configuration_without_hysteresis_is_refused() {
        assert!(Config::new(16_000, 16_000, 300).is_none());
        assert!(Config::new(8_000, 16_000, 300).is_none());
        assert!(Config::new(16_000, 8_000, 0).is_none());
        assert!(Config::new(16_000, 8_000, 300).is_some());
    }

    #[test]
    fn one_excursion_is_one_shot_emitted_at_its_peak() {
        let mut d = Detector::new(cfg());
        assert_eq!(d.push(0, &spin(1_000)), None);
        assert_eq!(d.push(10, &spin(20_000)), None); // enters
        assert_eq!(d.push(20, &spin(30_000)), None); // peak
        assert_eq!(d.push(30, &spin(18_000)), None); // still above exit
        let shot = d.push(40, &spin(100)).expect("excursion ended");
        assert_eq!(shot.t_ms, 20, "emitted at the peak, not the crossing");
        assert_eq!(shot.peak_gyro, 30_000);
        assert_eq!(shot.duration_ms, 30);
        assert_eq!(d.count(), 1);
    }

    #[test]
    fn a_second_swing_inside_the_refractory_window_is_not_a_second_shot() {
        let mut d = Detector::new(cfg());
        d.push(0, &spin(20_000));
        d.push(10, &spin(30_000));
        d.push(20, &spin(100)); // emits, peak at t=10
        // 200 ms later is inside the 300 ms window.
        d.push(200, &spin(30_000));
        d.push(210, &spin(100));
        assert_eq!(d.count(), 1);
        // 400 ms after the peak is outside it.
        d.push(410, &spin(30_000));
        assert_eq!(d.push(420, &spin(100)).is_some(), true);
        assert_eq!(d.count(), 2);
    }

    #[test]
    fn hysteresis_keeps_one_swing_from_chattering_into_many() {
        let mut d = Detector::new(cfg());
        d.push(0, &spin(20_000));
        // Dithering between the two levels must not close and reopen the shot.
        for (i, g) in [17_000, 9_000, 17_000, 9_000, 20_000].iter().enumerate() {
            d.push(10 + i as u32 * 10, &spin(*g));
        }
        assert_eq!(d.count(), 0, "still inside one excursion");
        d.push(100, &spin(100));
        assert_eq!(d.count(), 1);
    }

    #[test]
    fn an_excursion_that_never_ends_is_not_counted() {
        let mut d = Detector::new(cfg());
        d.push(0, &spin(30_000));
        d.push(10, &spin(30_000));
        d.flush();
        assert_eq!(d.count(), 0);
    }

    #[test]
    fn magnitude_is_the_vector_not_one_axis() {
        // Three axes at 10000 each: sqrt(3)*10000 = 17320, above enter_above,
        // where any single axis alone would not be.
        let s = ImuSample { ax: 0, ay: 0, az: 0, gx: 10_000, gy: 10_000, gz: 10_000 };
        assert_eq!(gyro_magnitude(&s), 17_320);
        let mut d = Detector::new(cfg());
        assert_eq!(d.push(0, &s), None);
        assert!(d.push(10, &spin(0)).is_some());
    }

    #[test]
    fn magnitude_survives_every_axis_railed() {
        // The corner of the cube, which real squash reaches: no overflow, and
        // the root is the true magnitude.
        let s = ImuSample {
            ax: 0,
            ay: 0,
            az: 0,
            gx: i16::MAX,
            gy: i16::MAX,
            gz: i16::MAX,
        };
        assert_eq!(gyro_magnitude(&s), 56_754);
    }
}
