//! The 24-hour dial Last night and Tonight are drawn on: midnight at the top, a tick each hour,
//! and a night as an arc notched at every hour it crosses.

use incised_text::{faces, Face, Style};
use inscribed_disc::{Abgr2222, Surface};

use crate::night::{draw_band, on_ring, Rgb};

/// The night's band, as pixel-centre radii, 2 px in from the glass edge.
pub const BAND_IN: f32 = 104.5;
pub const BAND_OUT: f32 = 116.5;
pub const BAND_MID: f32 = (BAND_IN + BAND_OUT) / 2.0;
/// The usual night, a thin arc just inside the band.
const USUAL_IN: f32 = 99.5;
const USUAL_OUT: f32 = 102.5;
const USUAL_INK: Rgb = [0.0, 3.0, 3.0];

const TICK_INK: Rgb = [2.0, 2.0, 2.0];
/// Half-widths in pixels: an hour tick, a six-hour tick, and the notch an hour cuts in a night.
const TICK_HALF: f32 = 0.7;
const QUARTER_HALF: f32 = 1.1;
const NOTCH_HALF: f32 = 0.8;
/// An hour tick reaches this far either side of the band's middle; a six-hour tick spans the band.
const TICK_REACH: f32 = 4.0;

const DAY_MIN: i32 = 1440;

pub static NUMERAL_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;
const NUMERAL_INK: Abgr2222 = Abgr2222::from_levels(2, 2, 2);
/// Numerals are centred this far from the middle, inside the usual arc; the page dots sit as far below it.
pub const NUMERAL_RADIUS: f32 = 86.0;
/// In NUMERAL_FACE a digit's ink spans the 13 rows above the baseline, so its middle row is 7 above it.
const DIGIT_MIDDLE_ABOVE_BASELINE: f32 = 7.0;
/// Noon is left to the page dots.
pub const NUMERALS: [(&str, u16); 3] = [("00", 0), ("06", 6 * 60), ("18", 18 * 60)];

/// From one minute of the day to another, clockwise, across midnight if need be.
#[derive(Clone, Copy)]
pub struct Span {
    pub from_min: u16,
    pub to_min: u16,
}

impl Span {
    fn start(&self) -> f32 {
        turn(self.from_min)
    }

    fn length(&self) -> f32 {
        (self.to_min as i32 - self.from_min as i32).rem_euclid(DAY_MIN) as f32 / DAY_MIN as f32
    }
}

/// A minute of the day as turns clockwise from midnight at the top.
pub fn turn(minute: u16) -> f32 {
    (minute as i32 % DAY_MIN) as f32 / DAY_MIN as f32
}

fn abs(v: f32) -> f32 {
    if v < 0.0 {
        -v
    } else {
        v
    }
}

/// Whether (dx, dy), at `t` turns and `r2` from the centre, is within `half` pixels of an hour line; and which hour.
fn near_hour(t: f32, r2: f32, half: f32) -> (bool, i32) {
    let h = t * 24.0;
    let nearest = (h + 0.5) as i32;
    let gap = abs(h - nearest as f32) / 24.0 * core::f32::consts::TAU;
    (gap * gap * r2 <= half * half, nearest % 24)
}

fn between(r2: f32, lo: f32, hi: f32) -> bool {
    r2 >= lo * lo && r2 <= hi * hi
}

fn on_tick(t: f32, r2: f32) -> bool {
    let (quarter, hour) = near_hour(t, r2, QUARTER_HALF);
    if hour % 6 == 0 {
        return quarter && between(r2, BAND_IN - 1.0, BAND_OUT + 1.0);
    }
    near_hour(t, r2, TICK_HALF).0 && between(r2, BAND_MID - TICK_REACH, BAND_MID + TICK_REACH)
}

/// What `span` puts at (dx, dy) on the band `lo..hi`, round-capped, coloured `ink` from start to end.
fn arc(dx: f32, dy: f32, t: f32, span: Span, lo: f32, hi: f32, ink: fn(f32) -> Rgb) -> Option<Rgb> {
    let (from, len) = (span.start(), span.length());
    if len <= 0.0 {
        return None;
    }
    let along = if t >= from { t - from } else { t - from + 1.0 };
    if between(dx * dx + dy * dy, lo, hi) && along <= len {
        return Some(ink(along / len));
    }
    let (mid, half) = ((lo + hi) / 2.0, (hi - lo) / 2.0);
    for (at, k) in [(from, 0.0), (from + len, 1.0)] {
        let (cx, cy) = on_ring(mid, at);
        let (ex, ey) = (dx - cx, dy - cy);
        if ex * ex + ey * ey <= half * half {
            return Some(ink(k));
        }
    }
    None
}

fn usual_ink(_: f32) -> Rgb {
    USUAL_INK
}

/// The hour ticks; `night` as a band in `ink`, notched at each hour; `usual` as a thin cyan arc inside it.
pub fn draw(fb: &mut Surface<Abgr2222>, night: Option<(Span, fn(f32) -> Rgb)>, usual: Option<Span>) {
    draw_band(fb, USUAL_IN - 1.0, BAND_OUT + 2.0, |dx, dy, t| {
        let r2 = dx * dx + dy * dy;
        if let Some((span, ink)) = night {
            if let Some(c) = arc(dx, dy, t, span, BAND_IN, BAND_OUT, ink) {
                return Some(if near_hour(t, r2, NOTCH_HALF).0 { [0.0; 3] } else { c });
            }
        }
        if let Some(span) = usual {
            if let Some(c) = arc(dx, dy, t, span, USUAL_IN, USUAL_OUT, usual_ink) {
                return Some(c);
            }
        }
        on_tick(t, r2).then_some(TICK_INK)
    });
}

/// The pixel row and column the numeral for `minute` is centred on, and the baseline that centres it.
pub fn numeral_at(minute: u16) -> (i32, i32) {
    let (x, y) = on_ring(NUMERAL_RADIUS, turn(minute));
    ((119.5 + x + 0.5) as i32, (119.5 + y + DIGIT_MIDDLE_ABOVE_BASELINE + 0.5) as i32)
}

pub fn numerals(fb: &mut Surface<Abgr2222>, ground: Abgr2222) {
    for (label, minute) in NUMERALS {
        let (x, baseline) = numeral_at(minute);
        NUMERAL_FACE.draw(fb, label, x, baseline, Style::centered(NUMERAL_INK, ground)).ok();
    }
}

/// A filled disc centred on the band at `minute`, ringed in the ground colour so it reads over an arc.
pub fn mark(fb: &mut Surface<Abgr2222>, minute: u16, radius: f32, ink: Abgr2222, ground: Abgr2222) {
    let (x, y) = on_ring(BAND_MID, turn(minute));
    let (cx, cy) = (119.5 + x, 119.5 + y);
    for (r, c) in [(radius + 1.5, ground), (radius, ink)] {
        for py in (cy - r - 1.0) as i32..=(cy + r + 1.0) as i32 {
            for px in (cx - r - 1.0) as i32..=(cx + r + 1.0) as i32 {
                let (ex, ey) = (px as f32 - cx, py as f32 - cy);
                if ex * ex + ey * ey <= r * r && fb.is_lit(px, py) {
                    fb.fill_rect(px, py, 1, 1, c);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_span_runs_clockwise_across_midnight() {
        let s = Span { from_min: 23 * 60, to_min: 7 * 60 };
        assert!((s.length() - 8.0 / 24.0).abs() < 1e-6);
        assert!((s.start() - 23.0 / 24.0).abs() < 1e-6);
    }

    #[test]
    fn every_hour_is_a_tick_and_every_sixth_spans_the_band() {
        let r2 = BAND_MID * BAND_MID;
        for h in 0..24 {
            assert!(on_tick(h as f32 / 24.0, r2), "hour {h}");
        }
        let edge = (BAND_IN + 0.5) * (BAND_IN + 0.5);
        assert!(on_tick(0.0, edge) && on_tick(0.25, edge));
        assert!(!on_tick(1.0 / 24.0, edge));
        assert!(!on_tick(0.5 / 24.0, r2));
    }
}
