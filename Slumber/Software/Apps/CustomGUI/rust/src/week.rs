//! The week screen: the seven evenings before tonight as floating bars, bedtime to wake.

use inscribed_disc::{Abgr2222, Surface};
use incised_text::{faces, Face};

use crate::night::{dither, minutes, rest_ink, text, GROUND, MAIN_INK, MUTED_INK, RHYTHM_INK, TITLE_INK, UNKNOWN};
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

fn usual_known(state: &State) -> bool {
    state.week_usual_bed != NO_BAR && state.week_usual_wake != NO_BAR
}

const DAY_MIN: i32 = 1440;

/// The bedtime, minutes after noon, that the plot's day starts from: the first after the clock's
/// widest stretch with no bedtime, the usual's included, so bedtimes either side of noon sit together.
fn day_start(state: &State) -> i32 {
    let mut beds = [0i32; WEEK_NIGHTS + 1];
    let mut n = 0;
    for i in 0..WEEK_NIGHTS {
        if state.week_bed[i] != NO_BAR {
            beds[n] = state.week_bed[i] as i32;
            n += 1;
        }
    }
    if usual_known(state) {
        beds[n] = state.week_usual_bed as i32;
        n += 1;
    }
    let beds = &mut beds[..n];
    beds.sort_unstable();
    let mut start = beds.first().copied().unwrap_or(0);
    let mut widest = match n {
        0 => 0,
        _ => beds[0] + DAY_MIN - beds[n - 1],
    };
    for k in 1..n {
        if beds[k] - beds[k - 1] > widest {
            widest = beds[k] - beds[k - 1];
            start = beds[k];
        }
    }
    start
}

/// `(bed, wake)` moved a day on if the bed falls before the plot's day starts.
fn unwrapped(bed: i32, wake: i32, start: i32) -> (i32, i32) {
    if bed < start {
        (bed + DAY_MIN, wake + DAY_MIN)
    } else {
        (bed, wake)
    }
}

/// Each night's column and its bed and wake, unwrapped onto the plot's day.
pub fn nights(state: &State) -> impl Iterator<Item = (usize, i32, i32)> + '_ {
    let start = day_start(state);
    (0..WEEK_NIGHTS).filter(move |&i| state.week_bed[i] != NO_BAR).map(move |i| {
        let (bed, wake) = unwrapped(state.week_bed[i] as i32, state.week_wake[i] as i32, start);
        (i, bed, wake)
    })
}

/// The usual bed and wake, unwrapped onto the plot's day.
fn usual(state: &State) -> Option<(i32, i32)> {
    usual_known(state)
        .then(|| unwrapped(state.week_usual_bed as i32, state.week_usual_wake as i32, day_start(state)))
}

/// "half within 25m": the median distance of the usual's bedtimes from it, or why there is no usual yet.
pub fn summary(state: &State) -> (Line, Abgr2222) {
    let mut line = Line::new();
    if nights(state).next().is_none() {
        line.push("No nights yet");
        return (line, MUTED_INK);
    }
    if !usual_known(state) || state.usual_spread_min == UNKNOWN {
        line.push(if state.times_vary != 0 { "times vary" } else { "learning" });
        return (line, MUTED_INK);
    }
    line.push("half within ");
    minutes(&mut line, state.usual_spread_min as u32);
    (line, RHYTHM_INK)
}

/// The minutes after noon the plot runs between, whole hours, at least MIN_SPAN_MIN apart.
pub fn span(state: &State) -> (i32, i32) {
    let mut lo = i32::MAX;
    let mut hi = i32::MIN;
    for (_, bed, wake) in nights(state).chain(usual(state).map(|(bed, wake)| (0, bed, wake))) {
        lo = lo.min(bed);
        hi = hi.max(wake);
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
    if nights(state).next().is_some() {
        if let Some((bed, wake)) = usual(state) {
            dashed(fb, row(bed, span) as i32);
            dashed(fb, row(wake, span) as i32);
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
