//! The week screen: the seven evenings before tonight as floating bars, bedtime to wake.

use inscribed_disc::{Abgr2222, Surface};
use incised_text::{faces, Face};

use crate::night::{dither, minutes, rest_ink, text, GROUND, MAIN_INK, MUTED_INK, REST_INK, RHYTHM_INK, TITLE_INK, UNKNOWN};
use crate::{Line, State, NO_BAR, SCREEN_WEEK, WEEK_NIGHTS};

pub static TITLE_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;
pub static SUMMARY_FACE: &Face = &faces::POPPINS_SEMIBOLD_20_ASCII;
pub static DAY_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;
/// Measured: a 10 h night's "10h22 22:05-08:50" is 192 px in the summary's 20 px face, past
/// the 190 the glass has at that row, and 177 in this one. Re-measure with `Face::measure`.
pub static PICK_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;

pub const TITLE_Y: i32 = 46;
pub const SUMMARY_Y: i32 = 70;
/// Lower than the summary, where the glass is wider, so a 10 h night clears the L1 arc.
pub const PICK_Y: i32 = 73;
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
const NAMES: [&str; 7] = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];

/// What is left of a bar's colour while another night is picked.
const UNPICKED_LIT: f32 = 0.35;
const UNDERLINE_Y: i32 = DAY_Y + 6;
const UNDERLINE_HALF_W: i32 = 6;

pub fn column_x(i: usize) -> i32 {
    120 + (i as i32 - (WEEK_NIGHTS as i32 - 1) / 2) * COLUMN_STEP
}

fn weekday(state: &State, i: usize) -> usize {
    let back = (WEEK_NIGHTS - 1 - i) as i32;
    (state.week_newest_weekday as i32 - back).rem_euclid(7) as usize
}

/// The weekday letter under bar `i`, given the weekday of the newest.
pub fn day(state: &State, i: usize) -> &'static str {
    DAYS[weekday(state, i)]
}

/// The night picked on the week, if the week is up and one is.
pub fn picked(state: &State) -> Option<usize> {
    (state.screen == SCREEN_WEEK && (state.week_pick as usize) < WEEK_NIGHTS).then_some(state.week_pick as usize)
}

pub fn picking(state: &State) -> bool {
    picked(state).is_some()
}

/// The picked night's weekday, in place of the title.
pub fn pick_title(state: &State, i: usize) -> &'static str {
    NAMES[weekday(state, i)]
}

/// Between the picked night's rest and its times, in place of a space.
const PICK_GAP: i32 = 6;

/// The picked night's rest and times, "7h42" and "23:15-07:20"; `None` if it has no bar.
pub fn pick_parts(state: &State, i: usize) -> Option<(Line, Line)> {
    if state.week_bed[i] == NO_BAR {
        return None;
    }
    let clock = |m: i16| ((m as i32 + DAY_MIN / 2).rem_euclid(DAY_MIN)) as u16;
    let mut rest = Line::new();
    minutes(&mut rest, state.week_rest[i] as u32);
    let mut times = Line::new();
    crate::night::clock(&mut times, clock(state.week_bed[i]));
    times.push("-");
    crate::night::clock(&mut times, clock(state.week_wake[i]));
    Some((rest, times))
}

/// Where the rest and the times start their ink, and where the times' ink ends, so the two
/// centre together.
pub fn pick_layout(rest: &Line, times: &Line) -> (i32, i32, i32) {
    let (r, t) = (PICK_FACE.measure(rest.as_str()), PICK_FACE.measure(times.as_str()));
    let (wr, wt) = ((r.ink_right - r.ink_left) as i32, (t.ink_right - t.ink_left) as i32);
    let left = 120 - (wr + PICK_GAP + wt) / 2;
    (left, left + wr + PICK_GAP, left + wr + PICK_GAP + wt)
}

/// The rest in the Rest colour and the times in white, as the night screen has them.
fn draw_pick(fb: &mut Surface<Abgr2222>, state: &State, i: usize) {
    let Some((rest, times)) = pick_parts(state, i) else {
        let mut none = Line::new();
        none.push("no night");
        text(fb, PICK_FACE, &none, PICK_Y, MUTED_INK);
        return;
    };
    let (left, times_at, _) = pick_layout(&rest, &times);
    let (r, t) = (PICK_FACE.measure(rest.as_str()), PICK_FACE.measure(times.as_str()));
    let style = |ink| incised_text::Style::left(ink, GROUND);
    PICK_FACE.draw(fb, rest.as_str(), left - r.ink_left as i32, PICK_Y, style(REST_INK)).ok();
    PICK_FACE.draw(fb, times.as_str(), times_at - t.ink_left as i32, PICK_Y, style(MAIN_INK)).ok();
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
fn bar(fb: &mut Surface<Abgr2222>, cx: i32, top: f32, bottom: f32, lit: f32) {
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
            let k = lit * hit as f32 / 4.0;
            fb.fill_rect(x, y, 1, 1, dither([c[0] * k, c[1] * k, c[2] * k], x, y));
        }
    }
}

pub fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    let pick = picked(state);
    let mut title = Line::new();
    title.push(pick.map_or("Past week", |i| pick_title(state, i)));
    text(fb, TITLE_FACE, &title, TITLE_Y, TITLE_INK);
    match pick {
        Some(i) => draw_pick(fb, state, i),
        None => {
            let (line, ink) = summary(state);
            text(fb, SUMMARY_FACE, &line, SUMMARY_Y, ink);
        }
    }

    let span = span(state);
    if nights(state).next().is_some() {
        if let Some((bed, wake)) = usual(state) {
            dashed(fb, row(bed, span) as i32);
            dashed(fb, row(wake, span) as i32);
        }
        for (i, bed, wake) in nights(state) {
            let lit = if pick.is_some_and(|p| p != i) { UNPICKED_LIT } else { 1.0 };
            bar(fb, column_x(i), row(bed, span), row(wake, span), lit);
        }
    }

    let bright = pick.unwrap_or(WEEK_NIGHTS - 1);
    for i in 0..WEEK_NIGHTS {
        let mut letter = Line::new();
        letter.push(day(state, i));
        let ink = if i == bright { MAIN_INK } else { MUTED_INK };
        DAY_FACE.draw(fb, letter.as_str(), column_x(i), DAY_Y, incised_text::Style::centered(ink, GROUND)).ok();
    }
    if let Some(i) = pick {
        fb.fill_rect(column_x(i) - UNDERLINE_HALF_W, UNDERLINE_Y, 2 * UNDERLINE_HALF_W + 1, 2, MAIN_INK);
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
