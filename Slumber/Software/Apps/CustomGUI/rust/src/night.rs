//! The night screen: last night on the 24-hour dial against the usual night it was scored against.

use inscribed_disc::{Abgr2222, Surface};
use incised_text::{faces, Face, Style};

use crate::dial::{self, Span};
use crate::{atlas, Line, State};

pub const KIND_NONE: u8 = 0;
pub const KIND_SETTLED: u8 = 1;
pub const KIND_SO_FAR: u8 = 2;
pub const KIND_OFF_WRIST: u8 = 3;
pub const UNKNOWN: u16 = 0xFFFF;
/// A rhythm_fill for a night whose earlier nights keep no one time.
pub const RHYTHM_VARIES: u16 = 0xFFFE;

/// A bedtime this close to the usual is on time.
pub(crate) const ON_TIME_MIN: i32 = 30;

const CENTRE: f32 = 119.5;
/// Text stays inside this radius, clear of the dial.
pub const TEXT_RADIUS: f32 = 88.0;

/// Channel levels, 0 to 3, as floats so they can be blended before dithering.
pub(crate) type Rgb = [f32; 3];
const VIOLET: Rgb = [1.0, 0.0, 3.0];
const PURPLE: Rgb = [2.0, 0.0, 3.0];
const MAGENTA: Rgb = [3.0, 0.0, 3.0];
const CYAN_DEEP: Rgb = [0.0, 2.0, 3.0];
const CYAN: Rgb = [0.0, 3.0, 3.0];

pub(crate) const GROUND: Abgr2222 = Abgr2222::BLACK;
pub(crate) const TITLE_INK: Abgr2222 = Abgr2222::from_levels(2, 2, 2);
pub(crate) const MAIN_INK: Abgr2222 = Abgr2222::WHITE;
pub(crate) const RHYTHM_INK: Abgr2222 = Abgr2222::from_levels(0, 3, 3);
pub(crate) const MUTED_INK: Abgr2222 = Abgr2222::from_levels(2, 2, 2);
/// The night's length, in the colour of its arc.
pub(crate) const REST_INK: Abgr2222 = Abgr2222::from_levels(3, 0, 3);

pub static TITLE_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;
pub static REST_FACE: &Face = &atlas::POPPINS_SEMIBOLD_24_ASCII;
pub static DETAIL_FACE: &Face = &faces::POPPINS_SEMIBOLD_20_ASCII;

pub const TITLE_Y: i32 = 80;
pub const REST_Y: i32 = 116;
pub const TIMES_Y: i32 = 146;
pub const RHYTHM_Y: i32 = 174;

const BAYER: [[f32; 4]; 4] = [
    [0.5 / 16.0, 8.5 / 16.0, 2.5 / 16.0, 10.5 / 16.0],
    [12.5 / 16.0, 4.5 / 16.0, 14.5 / 16.0, 6.5 / 16.0],
    [3.5 / 16.0, 11.5 / 16.0, 1.5 / 16.0, 9.5 / 16.0],
    [15.5 / 16.0, 7.5 / 16.0, 13.5 / 16.0, 5.5 / 16.0],
];

fn lerp(a: Rgb, b: Rgb, t: f32) -> Rgb {
    [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]
}


fn scale(c: Rgb, k: f32) -> Rgb {
    [c[0] * k, c[1] * k, c[2] * k]
}

pub(crate) fn rest_ink(t: f32) -> Rgb {
    let t = t.clamp(0.0, 1.0) * 2.0;
    if t < 1.0 {
        lerp(VIOLET, PURPLE, t)
    } else {
        lerp(PURPLE, MAGENTA, t - 1.0)
    }
}

pub(crate) fn rhythm_ink(t: f32) -> Rgb {
    lerp(CYAN_DEEP, CYAN, t.clamp(0.0, 1.0))
}

/// atan(z) for |z| <= 1, by the quadratic fit `atan_is_close` holds to.
fn atan_unit(z: f32) -> f32 {
    let a = if z < 0.0 { -z } else { z };
    core::f32::consts::FRAC_PI_4 * z - z * (a - 1.0) * (0.2447 + 0.0663 * a)
}

/// The angle of (dx, dy) in turns, clockwise from 12 o'clock, in [0, 1).
fn turns(dx: f32, dy: f32) -> f32 {
    // atan2(dx, -dy): x is right, y is down.
    let (y, x) = (dx, -dy);
    let (ax, ay) = (if x < 0.0 { -x } else { x }, if y < 0.0 { -y } else { y });
    if ax == 0.0 && ay == 0.0 {
        return 0.0;
    }
    let mut a = if ax >= ay {
        atan_unit(ay / ax)
    } else {
        core::f32::consts::FRAC_PI_2 - atan_unit(ax / ay)
    };
    if x < 0.0 {
        a = core::f32::consts::PI - a;
    }
    if y < 0.0 {
        a = -a;
    }
    let t = a / core::f32::consts::TAU;
    if t < 0.0 {
        t + 1.0
    } else {
        t
    }
}

/// sin of a turn fraction, by its Taylor series after folding into a quarter turn.
fn sin_turns(t: f32) -> f32 {
    let mut f = t - t as i32 as f32;
    if f > 0.5 {
        f -= 1.0;
    } else if f < -0.5 {
        f += 1.0;
    }
    let mut x = f * core::f32::consts::TAU;
    if x > core::f32::consts::FRAC_PI_2 {
        x = core::f32::consts::PI - x;
    } else if x < -core::f32::consts::FRAC_PI_2 {
        x = -core::f32::consts::PI - x;
    }
    let x2 = x * x;
    x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0))))
}

/// The ring's centreline point at `t` turns, as an offset from the centre.
pub(crate) fn on_ring(radius: f32, t: f32) -> (f32, f32) {
    (radius * sin_turns(t), -radius * sin_turns(t + 0.25))
}

/// Ordered dither of continuous levels to the panel's four; README "The screens to come" measured a
/// checkerboard of neighbouring levels landing halfway between them on the glass.
pub(crate) fn dither(c: Rgb, x: i32, y: i32) -> Abgr2222 {
    let d = BAYER[(y & 3) as usize][(x & 3) as usize];
    let q = |v: f32| -> u8 {
        let l = (v + d) as i32;
        l.clamp(0, 3) as u8
    };
    Abgr2222::from_levels(q(c[0]), q(c[1]), q(c[2]))
}

const SUB: [f32; 2] = [-0.25, 0.25];

/// Paints the annulus `lo..hi` from `sample(dx, dy, turns)`, four samples a pixel, dithered.
pub(crate) fn draw_band(fb: &mut Surface<Abgr2222>, lo: f32, hi: f32, sample: impl Fn(f32, f32, f32) -> Option<Rgb>) {
    let (w, h) = (fb.width(), fb.height());
    for y in 0..h {
        let dy = y as f32 - CENTRE;
        if dy * dy > hi * hi {
            continue;
        }
        for x in 0..w {
            let dx = x as f32 - CENTRE;
            let r2 = dx * dx + dy * dy;
            if r2 < lo * lo || r2 > hi * hi || !fb.is_lit(x, y) {
                continue;
            }
            let t = turns(dx, dy);
            let mut sum = [0.0f32; 3];
            for sy in SUB {
                for sx in SUB {
                    if let Some(c) = sample(dx + sx, dy + sy, t) {
                        sum = [sum[0] + c[0], sum[1] + c[1], sum[2] + c[2]];
                    }
                }
            }
            if sum != [0.0; 3] {
                fb.fill_rect(x, y, 1, 1, dither(scale(sum, 0.25), x, y));
            }
        }
    }
}

/// "7h42".
pub fn duration(line: &mut Line, minutes: u32) {
    line.num(minutes / 60, 1).push("h").num(minutes % 60, 2);
}

/// "23:29".
pub fn clock(line: &mut Line, minute_of_day: u16) {
    let m = minute_of_day as u32 % 1440;
    line.num(m / 60, 2).push(":").num(m % 60, 2);
}

/// "on time", "38m late", "1h05 early".
pub fn rhythm_words(line: &mut Line, offset_min: i32) {
    if offset_min.abs() <= ON_TIME_MIN {
        line.push("on time");
        return;
    }
    let a = offset_min.unsigned_abs();
    if a < 60 {
        line.num(a, 1).push("m");
    } else {
        duration(line, a);
    }
    line.push(if offset_min > 0 { " late" } else { " early" });
}

/// The lines the screen shows, top to bottom: title, rest, times, rhythm.
pub struct Words {
    pub title: Line,
    pub rest: Line,
    pub times: Line,
    pub rhythm: Line,
    pub rhythm_known: bool,
    /// The times line is the usual bedtime, in Rhythm's colour.
    pub times_are_usual: bool,
}

pub fn words(state: &State) -> Words {
    let mut w = Words {
        title: Line::new(),
        rest: Line::new(),
        times: Line::new(),
        rhythm: Line::new(),
        rhythm_known: state.rhythm_fill != UNKNOWN && state.rhythm_fill != RHYTHM_VARIES,
        times_are_usual: false,
    };
    if state.have_status == 0 {
        w.title.push("Slumber");
        w.times.push("starting");
        return w;
    }
    match state.night_kind {
        KIND_SETTLED | KIND_SO_FAR => {
            w.title.push(if state.night_kind == KIND_SETTLED { "Last night" } else { "So far" });
            duration(&mut w.rest, state.rest_min as u32);
            clock(&mut w.times, state.onset_min);
            w.times.push("-");
            clock(&mut w.times, state.wake_min);
            if w.rhythm_known {
                rhythm_words(&mut w.rhythm, state.rhythm_offset_min as i32);
            } else {
                w.rhythm.push(if state.rhythm_fill == RHYTHM_VARIES { "times vary" } else { "learning" });
            }
        }
        KIND_OFF_WRIST => {
            w.title.push("No night");
            w.times.push("watch was off");
        }
        _ => {
            w.title.push("No night yet");
            if state.usual_bed_min != UNKNOWN {
                w.times.push("usually ");
                clock(&mut w.times, state.usual_bed_min);
                w.times_are_usual = true;
            } else {
                w.times.push("wear it to bed");
            }
        }
    }
    w
}

pub(crate) fn text(fb: &mut Surface<Abgr2222>, face: &Face, line: &Line, baseline: i32, ink: Abgr2222) {
    face.draw(fb, line.as_str(), 120, baseline, Style::centered(ink, GROUND)).ok();
}

/// The usual the night was scored against or, before there is a night, the latest usual.
fn usual(state: &State, night: bool) -> Option<Span> {
    if night {
        return (state.scored_bed_min != UNKNOWN && state.scored_wake_min != UNKNOWN)
            .then_some(Span { from_min: state.scored_bed_min, to_min: state.scored_wake_min });
    }
    let wake = state.week_usual_wake;
    (state.night_kind == KIND_NONE && state.usual_bed_min != UNKNOWN && wake != crate::NO_BAR)
        .then(|| Span { from_min: state.usual_bed_min, to_min: ((wake as i32 + 720).rem_euclid(1440)) as u16 })
}

pub fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    let night = state.have_status != 0
        && (state.night_kind == KIND_SETTLED || state.night_kind == KIND_SO_FAR);
    let slept = night.then_some((Span { from_min: state.onset_min, to_min: state.wake_min }, rest_ink as fn(f32) -> Rgb));
    dial::draw(fb, slept, if state.have_status != 0 { usual(state, night) } else { None });
    dial::numerals(fb, GROUND);

    let w = words(state);
    text(fb, TITLE_FACE, &w.title, TITLE_Y, TITLE_INK);
    text(fb, REST_FACE, &w.rest, REST_Y, REST_INK);
    let times_ink = if w.times_are_usual {
        RHYTHM_INK
    } else if night {
        MAIN_INK
    } else {
        MUTED_INK
    };
    text(fb, DETAIL_FACE, &w.times, TIMES_Y, times_ink);
    let rhythm_ink = if w.rhythm_known { RHYTHM_INK } else { MUTED_INK };
    text(fb, DETAIL_FACE, &w.rhythm, RHYTHM_Y, rhythm_ink);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turns_run_clockwise_from_twelve() {
        let near = |a: f32, b: f32| (a - b).abs() < 0.001;
        assert!(near(turns(0.0, -1.0), 0.0));
        assert!(near(turns(1.0, 0.0), 0.25));
        assert!(near(turns(0.0, 1.0), 0.5));
        assert!(near(turns(-1.0, 0.0), 0.75));
        assert!(near(turns(1.0, -1.0), 0.125));
    }

    #[test]
    fn atan_is_close() {
        let mut worst = 0.0f32;
        for i in 0..3600 {
            let a = i as f32 / 3600.0;
            let (dx, dy) = ((a * core::f32::consts::TAU).sin(), -(a * core::f32::consts::TAU).cos());
            let mut e = (turns(dx, dy) - a).abs();
            e = e.min(1.0 - e);
            worst = worst.max(e);
        }
        assert!(worst < 0.0003, "worst {worst} turns");
    }

    #[test]
    fn sine_matches_at_the_quarters() {
        for (t, s) in [(0.0, 0.0), (0.25, 1.0), (0.5, 0.0), (0.75, -1.0), (0.125, 0.70710677), (-0.25, -1.0)] {
            assert!((sin_turns(t) - s).abs() < 1e-4, "sin({t}) = {}", sin_turns(t));
        }
    }

    #[test]
    fn rhythm_reads_in_words() {
        let say = |m| {
            let mut l = Line::new();
            rhythm_words(&mut l, m);
            l
        };
        assert_eq!(say(0).as_str(), "on time");
        assert_eq!(say(-30).as_str(), "on time");
        assert_eq!(say(38).as_str(), "38m late");
        assert_eq!(say(-65).as_str(), "1h05 early");
    }
}
