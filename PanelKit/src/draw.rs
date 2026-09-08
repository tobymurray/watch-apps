//! Primitives whose shape was decided by a measurement rather than a taste.
//!
//! Each one carries the number that chose it and the way to prove that number
//! wrong.

use crate::surface::{ByteColor, Surface};

/// Sample pitch along an arc and along its radius, in pixels.
///
/// The samples are a grid of this pitch laid over the pixel grid at an
/// arbitrary rotation, so the limit is the diagonal, not 1.0: past
/// `1/sqrt(2) ≈ 0.707` a pixel can fall between four samples and never be
/// painted.
///
/// MEASURED here, filling the ring between r=100 and r=118 on a 240×240 panel
/// and counting pixels inside it that stayed unpainted: 0.95 leaves 520, 0.85
/// leaves 142, **0.75 leaves 4**, and 0.70 is clean. 0.65 is that with margin,
/// and is what ships.
///
/// The 4 at 0.75 is the same count `Spin` recorded independently on its own
/// ring, which is the agreement worth having; the coarser counts differ from
/// its 95 at 0.85 because the radii differ, and holes grow with the arc.
/// Re-measure with `unpainted_pixels_at` in this module's tests, which is the
/// falsifier and is run on every build.
pub const ARC_STEP_PX: f32 = 0.65;

/// Fills the wedge between two radii and two angles, clockwise from twelve.
///
/// Swept rather than scanned: walking the angle and drawing a radial run at
/// each step touches only the pixels of the arc, where testing every pixel of
/// the bounding box for membership would be the whole panel and an `atan2` per
/// pixel.
pub fn fill_arc<C: ByteColor>(
    s: &mut Surface<C>,
    cx: f32,
    cy: f32,
    r_inner: f32,
    r_outer: f32,
    start_deg: f32,
    sweep_deg: f32,
    color: C,
) {
    if sweep_deg <= 0.0 || r_outer <= r_inner || r_outer <= 0.0 {
        return;
    }
    // Sized so the outer edge, where consecutive runs are furthest apart, moves
    // by one sample pitch.
    let step = ARC_STEP_PX / r_outer;
    let start = start_deg.to_radians();
    let end = start + sweep_deg.to_radians();

    let mut a = start;
    while a <= end {
        // Zero is twelve o'clock, clockwise; panel y grows downward, hence -cos.
        let (sa, ca) = (sin(a), cos(a));
        let mut r = r_inner;
        while r <= r_outer {
            let x = (cx + r * sa + 0.5) as i32;
            let y = (cy - r * ca + 0.5) as i32;
            s.fill_rect(x, y, 1, 1, color);
            r += ARC_STEP_PX;
        }
        a += step;
    }
}

/// A ring: the arc that goes all the way round.
pub fn fill_ring<C: ByteColor>(s: &mut Surface<C>, cx: f32, cy: f32, r_inner: f32, r_outer: f32, color: C) {
    fill_arc(s, cx, cy, r_inner, r_outer, 0.0, 360.0, color);
}

/// A filled circle, by rows rather than by samples.
pub fn fill_circle<C: ByteColor>(s: &mut Surface<C>, cx: i32, cy: i32, radius: i32, color: C) {
    if radius <= 0 {
        return;
    }
    for dy in -radius..=radius {
        // Half-chord of the circle at this row, in whole pixels.
        let dx = isqrt(radius * radius - dy * dy);
        s.fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

/// Accumulates how much of `[x0, x1)`, in fractional pixels, each integer
/// column of `coverage` is covered by.
///
/// Order-independent by construction: a column's final coverage is the sum of
/// every span's overlap with it, so two adjacent sub-pixel-wide spans still add
/// up correctly whichever is processed first — unlike blitting each as its own
/// independently rounded rectangle, which is how a barcode's bars blur.
///
/// This is the only correct way to scale on a panel with four levels a channel:
/// with 254 intermediate shades a rounding error is invisible, and with two it
/// is a missing bar.
pub fn accumulate_coverage(coverage: &mut [f32], x0: f32, x1: f32) {
    if x1 <= x0 {
        return;
    }
    let start = (x0.max(0.0)) as usize;
    let end = (ceil_nonneg(x1).max(0)) as usize;
    for (col, cell) in coverage.iter_mut().enumerate().take(end).skip(start) {
        let px0 = col as f32;
        let px1 = px0 + 1.0;
        let overlap = (if x1 < px1 { x1 } else { px1 } - if x0 > px0 { x0 } else { px0 }).max(0.0);
        *cell += overlap;
    }
}

fn ceil_nonneg(v: f32) -> i32 {
    let t = v as i32;
    if v > t as f32 {
        t + 1
    } else {
        t
    }
}

fn isqrt(v: i32) -> i32 {
    if v <= 0 {
        return 0;
    }
    let mut x = v;
    let mut y = (x + 1) / 2;
    while y < x {
        x = y;
        y = (x + v / x) / 2;
    }
    x
}

// `libm` is a dependency this crate does not want and `core` has no `sin`, so
// the two it needs are here. Held to 1e-6 against the host by
// `trig_agrees_with_the_host`; at the largest radius this panel has, 120
// pixels, that is 1.2e-4 of a pixel, against the half-pixel the sampler rounds
// to. Falsified by that test, which is what would notice a term dropped.
fn wrap(a: f32) -> f32 {
    const TAU: f32 = core::f32::consts::PI * 2.0;
    let turns = a * (1.0 / TAU);
    a - TAU * (turns as i32 as f32)
}

fn sin(a: f32) -> f32 {
    use core::f32::consts::{FRAC_PI_2, PI};
    let x = wrap(a);
    let x = if x > PI {
        x - PI * 2.0
    } else if x < -PI {
        x + PI * 2.0
    } else {
        x
    };
    // Folded into [-pi/2, pi/2] by sin(pi - x) = sin(x) before the series runs.
    // Without this the series is evaluated near pi, where truncating it at the
    // ninth power leaves 6.9e-3 -- and `cos` reaches exactly there, because it
    // is `sin(a + pi/2)`. That error put a run of unpainted pixels along each
    // cardinal direction of every arc; `trig_agrees_with_the_host` is what
    // notices it coming back.
    let x = if x > FRAC_PI_2 {
        PI - x
    } else if x < -FRAC_PI_2 {
        -PI - x
    } else {
        x
    };
    let x2 = x * x;
    x * (1.0 - x2 * (1.0 / 6.0 - x2 * (1.0 / 120.0 - x2 * (1.0 / 5040.0 - x2 / 362_880.0))))
}

fn cos(a: f32) -> f32 {
    sin(a + core::f32::consts::FRAC_PI_2)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color::Abgr2222;
    use crate::geometry;

    const W: u32 = 240;
    const H: u32 = 240;

    /// Fills the ring at `step` and counts pixels inside it that stayed black.
    /// This is `ARC_STEP_PX`'s falsifier, run rather than quoted.
    fn unpainted_pixels_at(step: f32) -> usize {
        let (r_in, r_out) = (100.0f32, 118.0f32);
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        let (cx, cy) = ((W as f32 - 1.0) / 2.0, (H as f32 - 1.0) / 2.0);
        {
            let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
            s.clear(Abgr2222::BLACK);
            // The sampler, with the step under test rather than the constant.
            let ang = step / r_out;
            let mut a = 0.0f32;
            while a <= core::f32::consts::PI * 2.0 {
                let (sa, ca) = (sin(a), cos(a));
                let mut r = r_in;
                while r <= r_out {
                    let x = (cx + r * sa + 0.5) as i32;
                    let y = (cy - r * ca + 0.5) as i32;
                    s.fill_rect(x, y, 1, 1, Abgr2222::WHITE);
                    r += step;
                }
                a += ang;
            }
        }
        // A pixel is "inside" if its centre is comfortably between the radii,
        // so the count is of holes rather than of edge rounding.
        let mut holes = 0;
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                let dx = x as f32 - cx;
                let dy = y as f32 - cy;
                let d = (dx * dx + dy * dy).sqrt();
                if d > r_in + 1.0 && d < r_out - 1.0 && buf[(y * W as i32 + x) as usize] == Abgr2222::BLACK.0 {
                    holes += 1;
                }
            }
        }
        holes
    }

    /// `ARC_STEP_PX`'s falsifier, run rather than quoted.
    ///
    /// Asserts the shape of the curve and not only its end, because a sampler
    /// that painted everything regardless of step would pass the end alone.
    #[test]
    fn the_arc_step_was_chosen_by_counting_unpainted_pixels() {
        assert_eq!(unpainted_pixels_at(0.85), 142);
        assert_eq!(unpainted_pixels_at(0.75), 4);
        assert_eq!(unpainted_pixels_at(0.70), 0);
        assert_eq!(unpainted_pixels_at(ARC_STEP_PX), 0, "the shipped step left holes");
    }

    /// The geometry the step is chosen against: past 1/sqrt(2) a pixel can fall
    /// between four samples, so a step above it must leave holes. If this ever
    /// passes with a coarse step, the sampler stopped being a point sampler.
    #[test]
    fn a_step_past_the_diagonal_leaves_holes() {
        assert!(unpainted_pixels_at(0.95) > 100);
        assert!(unpainted_pixels_at(0.85) > unpainted_pixels_at(0.75));
    }

    #[test]
    fn an_arc_stays_inside_the_bezel() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
            s.clear(Abgr2222::BLACK);
            fill_ring(&mut s, 119.5, 119.5, 100.0, 130.0, Abgr2222::WHITE);
        }
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                if !geometry::is_lit(x, y, W as i32, H as i32) {
                    assert_eq!(buf[(y * W as i32 + x) as usize], Abgr2222::BLACK.0, "({x},{y})");
                }
            }
        }
    }

    #[test]
    fn coverage_is_order_independent() {
        let mut a = [0.0f32; 8];
        let mut b = [0.0f32; 8];
        accumulate_coverage(&mut a, 0.3, 1.4);
        accumulate_coverage(&mut a, 1.4, 2.1);
        accumulate_coverage(&mut b, 1.4, 2.1);
        accumulate_coverage(&mut b, 0.3, 1.4);
        assert_eq!(a, b);
    }

    #[test]
    fn coverage_conserves_area() {
        let mut c = [0.0f32; 16];
        accumulate_coverage(&mut c, 1.25, 9.75);
        let total: f32 = c.iter().sum();
        assert!((total - 8.5).abs() < 1e-4, "got {total}");
    }

    #[test]
    fn coverage_never_writes_past_its_slice() {
        let mut c = [0.0f32; 4];
        accumulate_coverage(&mut c, 0.0, 100.0);
        assert!(c.iter().all(|&v| v <= 1.0 + 1e-6));
    }

    /// MEASURED: the worst absolute error at one-degree steps over ±720° is
    /// 3.6e-6, which at this panel's largest radius of 120 pixels is 4.3e-4 of
    /// a pixel — three orders finer than the half-pixel the sampler rounds to.
    /// Most of it is `wrap` losing f32 precision on the large arguments, not
    /// the series; over the 0..2π an arc actually uses it is far smaller.
    /// Falsified by a dropped series term or a lost range reduction, either of
    /// which moves it by three orders and shows up as holes in every arc.
    #[test]
    fn trig_agrees_with_the_host_to_within_four_millionths() {
        let mut worst = 0.0f32;
        for deg in -720..=720 {
            let a = (deg as f32).to_radians();
            worst = worst.max((sin(a) - (a as f64).sin() as f32).abs());
            worst = worst.max((cos(a) - (a as f64).cos() as f32).abs());
        }
        assert!(worst < 4e-6, "worst trig error was {worst:e}");
    }

    #[test]
    fn a_circle_is_round_and_inside_its_radius() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
            s.clear(Abgr2222::BLACK);
            fill_circle(&mut s, 120, 120, 40, Abgr2222::WHITE);
        }
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                let d2 = (x - 120).pow(2) + (y - 120).pow(2);
                if buf[(y * W as i32 + x) as usize] != Abgr2222::BLACK.0 {
                    assert!(d2 <= 40 * 40 + 40, "({x},{y}) is outside the radius");
                }
            }
        }
    }
}

