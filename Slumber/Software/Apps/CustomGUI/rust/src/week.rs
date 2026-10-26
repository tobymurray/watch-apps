//! The week screen: the seven evenings before tonight as floating bars, bedtime to wake.

use inscribed_disc::{Abgr2222, Surface};
use incised_text::{faces, Face};

use crate::night::{dither, rest_ink, text, GROUND, MAIN_INK, MUTED_INK, ON_TIME_MIN, RHYTHM_INK, TITLE_INK};
use crate::{Line, State, NO_BAR, WEEK_NIGHTS};

pub static TITLE_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;
pub static SUMMARY_FACE: &Face = &faces::POPPINS_SEMIBOLD_20_ASCII;
pub static DAY_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;

pub const TITLE_Y: i32 = 46;
pub const SUMMARY_Y: i32 = 70;
pub const PLOT_TOP: i32 = 82;
pub const PLOT_BOTTOM: i32 = 164;
pub const DAY_Y: i32 = 186;

const COLUMN_STEP: i32 = 22;
const BAR_HALF_W: f32 = 6.0;
const USUAL_FROM_X: i32 = 40;
const USUAL_TO_X: i32 = 200;
const DASH: i32 = 3;

/// The plot spans at least this much, so a single night does not fill it.
const MIN_SPAN_MIN: i32 = 6 * 60;

const DAYS: [&str; 7] = ["S", "M", "T", "W", "T", "F", "S"];

pub fn column_x(i: usize) -> i32 {
    120 + (i as i32 - (WEEK_NIGHTS as i32 - 1) / 2) * COLUMN_STEP
}

/// The weekday letter under bar `i`, given the weekday of the newest.
pub fn day(state: &State, i: usize) -> &'static str {
    let back = (WEEK_NIGHTS - 1 - i) as i32;
    DAYS[(state.week_newest_weekday as i32 - back).rem_euclid(7) as usize]
}

fn nights(state: &State) -> impl Iterator<Item = (usize, i32, i32)> + '_ {
    (0..WEEK_NIGHTS)
        .filter(move |&i| state.week_bed[i] != NO_BAR)
        .map(move |i| (i, state.week_bed[i] as i32, state.week_wake[i] as i32))
}

fn usual_known(state: &State) -> bool {
    state.week_usual_bed != NO_BAR && state.week_usual_wake != NO_BAR
}

/// Nights in the week, and how many of them began within ON_TIME_MIN of the usual bedtime.
pub fn on_time(state: &State) -> (u32, u32) {
    let usual = state.week_usual_bed as i32;
    nights(state).fold((0, 0), |(n, on), (_, bed, _)| {
        (n + 1, on + u32::from(usual_known(state) && (bed - usual).abs() <= ON_TIME_MIN))
    })
}

/// "5 of 7 on time", or why there is no count yet.
pub fn summary(state: &State) -> (Line, Abgr2222) {
    let mut line = Line::new();
    let (n, on) = on_time(state);
    if n == 0 {
        line.push("No nights yet");
        return (line, MUTED_INK);
    }
    if !usual_known(state) {
        line.push(if state.times_vary != 0 { "times vary" } else { "learning" });
        return (line, MUTED_INK);
    }
    line.num(on, 1).push(" of ").num(n, 1).push(" on time");
    (line, RHYTHM_INK)
}

/// The minutes after noon the plot runs between, whole hours, at least MIN_SPAN_MIN apart.
pub fn span(state: &State) -> (i32, i32) {
    let mut lo = i32::MAX;
    let mut hi = i32::MIN;
    for (_, bed, wake) in nights(state) {
        lo = lo.min(bed);
        hi = hi.max(wake);
    }
    if usual_known(state) {
        lo = lo.min(state.week_usual_bed as i32);
        hi = hi.max(state.week_usual_wake as i32);
    }
    if lo > hi {
        return (0, MIN_SPAN_MIN);
    }
    let mut lo = lo.div_euclid(60) * 60;
    let mut hi = (hi + 59).div_euclid(60) * 60;
    let short = MIN_SPAN_MIN - (hi - lo);
    if short > 0 {
        lo -= (short / 2).div_euclid(60) * 60;
        hi = lo + MIN_SPAN_MIN;
    }
    (lo, hi)
}

fn row(minute: i32, (lo, hi): (i32, i32)) -> f32 {
    PLOT_TOP as f32 + (minute - lo) as f32 * (PLOT_BOTTOM - PLOT_TOP) as f32 / (hi - lo) as f32
}

fn dashed(fb: &mut Surface<Abgr2222>, y: i32) {
    let mut x = USUAL_FROM_X;
    while x <= USUAL_TO_X {
        fb.fill_rect(x, y, DASH.min(USUAL_TO_X - x + 1), 1, RHYTHM_INK);
        x += 2 * DASH;
    }
}

const SUB: [f32; 2] = [-0.25, 0.25];

/// A capsule from row `top` to row `bottom`, in the Rest colours down its length, edges antialiased.
fn bar(fb: &mut Surface<Abgr2222>, cx: i32, top: f32, bottom: f32) {
    let r = BAR_HALF_W;
    let (a, b) = (top + r, (bottom - r).max(top + r));
    for y in (top - 1.0) as i32..=(bottom + 1.0) as i32 {
        for x in cx - r as i32 - 1..=cx + r as i32 + 1 {
            let mut hit = 0;
            for sy in SUB {
                for sx in SUB {
                    let (px, py) = (x as f32 + sx - cx as f32, y as f32 + sy);
                    let d = py.max(a).min(b) - py;
                    hit += u32::from(px * px + d * d <= r * r);
                }
            }
            if hit == 0 || !fb.is_lit(x, y) {
                continue;
            }
            let t = (y - PLOT_TOP) as f32 / (PLOT_BOTTOM - PLOT_TOP) as f32;
            let c = rest_ink(t);
            let k = hit as f32 / 4.0;
            fb.fill_rect(x, y, 1, 1, dither([c[0] * k, c[1] * k, c[2] * k], x, y));
        }
    }
}

pub fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    let mut title = Line::new();
    title.push("Past week");
    text(fb, TITLE_FACE, &title, TITLE_Y, TITLE_INK);
    let (summary, ink) = summary(state);
    text(fb, SUMMARY_FACE, &summary, SUMMARY_Y, ink);

    let span = span(state);
    if on_time(state).0 > 0 {
        if usual_known(state) {
            dashed(fb, row(state.week_usual_bed as i32, span) as i32);
            dashed(fb, row(state.week_usual_wake as i32, span) as i32);
        }
        for (i, bed, wake) in nights(state) {
            bar(fb, column_x(i), row(bed, span), row(wake, span));
        }
    }

    let newest = WEEK_NIGHTS - 1;
    for i in 0..WEEK_NIGHTS {
        let mut letter = Line::new();
        letter.push(day(state, i));
        let ink = if i == newest { MAIN_INK } else { MUTED_INK };
        DAY_FACE.draw(fb, letter.as_str(), column_x(i), DAY_Y, incised_text::Style::centered(ink, GROUND)).ok();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn columns_centre_on_the_panel() {
        assert_eq!(column_x(3), 120);
        assert_eq!(column_x(0) + column_x(6), 240);
    }

    #[test]
    fn the_span_is_whole_hours_and_never_short() {
        let mut st = State::default();
        assert_eq!(span(&st), (0, MIN_SPAN_MIN));
        st.week_bed[6] = 650;
        st.week_wake[6] = 700;
        let (lo, hi) = span(&st);
        assert_eq!((lo % 60, hi % 60, hi - lo), (0, 0, MIN_SPAN_MIN));
        assert!(lo <= 650 && hi >= 700);
    }
}
