//! The arcs on the rim beside the buttons that do something on the screen up, as the stock
//! apps mark them.

use inscribed_disc::{Abgr2222, Surface};

use crate::night::{dither, sin_turns, turns};

pub const L1: usize = 0;
pub const L2: usize = 1;
pub const R1: usize = 2;
pub const R2: usize = 3;

/// The stock `Buttons` container's arcs (the SDK's Running example, `ButtonsBase.cpp`), in
/// degrees clockwise from 12 o'clock, in the order L1, L2, R1, R2.
const ARCS: [(f32, f32); 4] = [(291.0, 308.0), (232.0, 249.0), (52.0, 69.0), (111.0, 128.0)];
const RADIUS: f32 = 113.0;
const HALF_W: f32 = 3.0;
/// The stock arcs' neutral white, #C0C0C0, at the panel's nearest level.
const INK: [f32; 3] = [2.0, 2.0, 2.0];
const CENTRE: f32 = 119.5;
const SUB: [f32; 4] = [-0.375, -0.125, 0.125, 0.375];

/// The pixels arc `(from, to)` can touch, as `(x0, y0, x1, y1)`.
fn bounds((from, to): (f32, f32)) -> (i32, i32, i32, i32) {
    let (mut x0, mut y0, mut x1, mut y1) = (i32::MAX, i32::MAX, i32::MIN, i32::MIN);
    let mut deg = from;
    while deg <= to + 1.0 {
        let t = deg.min(to) / 360.0;
        for r in [RADIUS - HALF_W - 1.0, RADIUS + HALF_W + 1.0] {
            let x = (CENTRE + r * sin_turns(t)) as i32;
            let y = (CENTRE - r * sin_turns(t + 0.25)) as i32;
            (x0, y0, x1, y1) = (x0.min(x), y0.min(y), x1.max(x), y1.max(y));
        }
        deg += 1.0;
    }
    (x0 - 1, y0 - 1, x1 + 1, y1 + 1)
}

/// Draws the arc beside each button in `lit`, antialiased.
pub fn draw(fb: &mut Surface<Abgr2222>, lit: [bool; 4]) {
    for (k, &(from, to)) in ARCS.iter().enumerate() {
        if !lit[k] {
            continue;
        }
        let (x0, y0, x1, y1) = bounds((from, to));
        for y in y0..=y1 {
            for x in x0..=x1 {
                let mut hit = 0;
                for sy in SUB {
                    for sx in SUB {
                        let (dx, dy) = (x as f32 + sx - CENTRE, y as f32 + sy - CENTRE);
                        let r2 = dx * dx + dy * dy;
                        let on_band = r2 >= (RADIUS - HALF_W) * (RADIUS - HALF_W) && r2 <= (RADIUS + HALF_W) * (RADIUS + HALF_W);
                        let deg = turns(dx, dy) * 360.0;
                        hit += u32::from(on_band && deg >= from && deg <= to);
                    }
                }
                if hit == 0 || !fb.is_lit(x, y) {
                    continue;
                }
                let c = hit as f32 / 16.0;
                fb.fill_rect(x, y, 1, 1, dither([INK[0] * c, INK[1] * c, INK[2] * c], x, y));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_arc_lies_inside_its_bounds() {
        for &(from, to) in &ARCS {
            let (x0, y0, x1, y1) = bounds((from, to));
            let mut deg = from;
            while deg <= to {
                for r in [RADIUS - HALF_W, RADIUS, RADIUS + HALF_W] {
                    let x = CENTRE + r * sin_turns(deg / 360.0);
                    let y = CENTRE - r * sin_turns(deg / 360.0 + 0.25);
                    assert!(x0 as f32 <= x && x <= x1 as f32 + 1.0 && y0 as f32 <= y && y <= y1 as f32 + 1.0, "{from}-{to} at {deg}");
                }
                deg += 0.25;
            }
        }
    }

    #[test]
    fn r1s_arc_is_at_two_oclock() {
        let (x0, y0, x1, y1) = bounds(ARCS[R1]);
        assert!(x0 > 120 && y1 < 120, "{x0},{y0} {x1},{y1}");
    }
}
