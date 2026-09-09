//! Ordered dithering.
//!
//! Four levels a channel bands any gradient. Dithering makes a value between
//! two levels land on the lower one in some pixels and the upper one in others,
//! so the eye averages them.

use crate::color::Abgr2222;

const BAYER_SIZE: usize = 8;
const BAYER_MAX: u32 = 63;
const CHANNEL_LEVELS_MAX: u32 = 3;
const FULL_SCALE: u32 = 255;

/// The recursive 8×8 ordered-dither threshold matrix.
#[rustfmt::skip]
pub const BAYER_8X8: [[u8; BAYER_SIZE]; BAYER_SIZE] = [
    [ 0, 32,  8, 40,  2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44,  4, 36, 14, 46,  6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [ 3, 35, 11, 43,  1, 33,  9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47,  7, 39, 13, 45,  5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
];

/// Quantise 0..=255 to a level by truncation, which is what bands a ramp.
#[inline]
pub const fn quantise_flat(value: u8) -> u8 {
    (value >> 6) & 3
}

/// Quantise with a per-pixel threshold, so a value between two levels lands on
/// the lower one in some pixels and the upper one in others.
#[inline]
pub fn quantise_dithered(value: u8, x: i32, y: i32) -> u8 {
    let threshold = BAYER_8X8[(y.rem_euclid(BAYER_SIZE as i32)) as usize]
        [(x.rem_euclid(BAYER_SIZE as i32)) as usize] as u32;
    let scaled = value as u32 * CHANNEL_LEVELS_MAX + threshold * (FULL_SCALE + 1) / (BAYER_MAX + 1);
    let level = scaled / FULL_SCALE;
    if level > CHANNEL_LEVELS_MAX {
        CHANNEL_LEVELS_MAX as u8
    } else {
        level as u8
    }
}

/// An 8-bit-per-channel colour dithered to what this panel can actually show.
#[inline]
pub fn dither_rgb(r: u8, g: u8, b: u8, x: i32, y: i32) -> Abgr2222 {
    Abgr2222::from_levels(
        quantise_dithered(r, x, y),
        quantise_dithered(g, x, y),
        quantise_dithered(b, x, y),
    )
}

/// The same colour without dithering, for the comparison that proves the point.
#[inline]
pub fn flat_rgb(r: u8, g: u8, b: u8) -> Abgr2222 {
    Abgr2222::from_levels(quantise_flat(r), quantise_flat(g), quantise_flat(b))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// MEASURED: mean absolute error against the ideal, in level units, over a
    /// 240-pixel ramp averaged down each 8-pixel cell. Dithering must be under
    /// *half* truncation's error, not merely under it, or it is not worth the
    /// code. Falsified by a matrix or quantiser that reverses the two.
    #[test]
    fn dithering_beats_flat_quantisation() {
        const W: i32 = 240;
        let mut dithered_err = 0.0f64;
        let mut flat_err = 0.0f64;
        let mut cells = 0.0f64;

        for cell in 0..(W / BAYER_SIZE as i32) {
            let x0 = cell * BAYER_SIZE as i32;
            // The true value at the middle of this cell, on a 0..255 ramp.
            let ideal = ((x0 + BAYER_SIZE as i32 / 2) as f64 / (W - 1) as f64) * 3.0;

            let mut d_sum = 0.0f64;
            let mut f_sum = 0.0f64;
            for dy in 0..BAYER_SIZE as i32 {
                for dx in 0..BAYER_SIZE as i32 {
                    let x = x0 + dx;
                    let v = ((x as f64 / (W - 1) as f64) * 255.0).round() as u8;
                    d_sum += quantise_dithered(v, x, dy) as f64;
                    f_sum += quantise_flat(v) as f64;
                }
            }
            let n = (BAYER_SIZE * BAYER_SIZE) as f64;
            dithered_err += (d_sum / n - ideal).abs();
            flat_err += (f_sum / n - ideal).abs();
            cells += 1.0;
        }

        let d = dithered_err / cells;
        let f = flat_err / cells;
        assert!(d < f, "dithered mean error {d:.4} was not below flat {f:.4}");
        // Not merely better: better by enough to be worth the code.
        assert!(d < f * 0.5, "dithered {d:.4} vs flat {f:.4} is too small a win");
    }

    /// A flat ramp reaches four distinct values; a dithered one reaches more
    /// *average* values, which is the banding gone.
    #[test]
    fn a_flat_ramp_bands_and_a_dithered_one_does_not() {
        const W: i32 = 240;
        let mut flat = [false; 4];
        for x in 0..W {
            let v = ((x as f64 / (W - 1) as f64) * 255.0).round() as u8;
            flat[quantise_flat(v) as usize] = true;
        }
        assert_eq!(flat.iter().filter(|&&s| s).count(), 4, "the ramp has four bands");

        let mut distinct_cell_means = 0;
        let mut last = -1.0f64;
        for cell in 0..(W / BAYER_SIZE as i32) {
            let x0 = cell * BAYER_SIZE as i32;
            let mut sum = 0.0f64;
            for dy in 0..BAYER_SIZE as i32 {
                for dx in 0..BAYER_SIZE as i32 {
                    let x = x0 + dx;
                    let v = ((x as f64 / (W - 1) as f64) * 255.0).round() as u8;
                    sum += quantise_dithered(v, x, dy) as f64;
                }
            }
            let mean = sum / (BAYER_SIZE * BAYER_SIZE) as f64;
            if (mean - last).abs() > 1e-9 {
                distinct_cell_means += 1;
                last = mean;
            }
        }
        assert!(distinct_cell_means > 4, "only {distinct_cell_means} distinct steps");
    }

    #[test]
    fn the_matrix_is_a_permutation_of_0_to_63() {
        let mut seen = [false; 64];
        for row in BAYER_8X8 {
            for v in row {
                assert!(!seen[v as usize], "{v} appears twice");
                seen[v as usize] = true;
            }
        }
        assert!(seen.iter().all(|&s| s));
    }

    #[test]
    fn the_ends_of_the_range_never_dither() {
        for y in 0..8 {
            for x in 0..8 {
                assert_eq!(quantise_dithered(0, x, y), 0);
                assert_eq!(quantise_dithered(255, x, y), 3);
            }
        }
    }

    #[test]
    fn negative_coordinates_stay_in_the_matrix() {
        // `%` on a negative i32 is negative and would index out of bounds.
        assert_eq!(quantise_dithered(128, -1, -1), quantise_dithered(128, 7, 7));
    }
}
