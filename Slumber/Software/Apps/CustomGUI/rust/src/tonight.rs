//! The evening screen: tonight's usual bedtime, on a 24-hour dial with midnight at the top.

use inscribed_disc::{Abgr2222, Surface};

use crate::night::{
    clock, draw_band, duration, on_ring, rest_ink, text, Rgb, DETAIL_FACE, GROUND, KIND_SO_FAR, MAIN_INK,
    MUTED_INK, ON_TIME_MIN, REST_FACE, REST_IN, REST_OUT, REST_TRACK, REST_Y, RHYTHM_INK, RHYTHM_Y, TIMES_Y,
    TITLE_FACE, TITLE_INK, TITLE_Y, TRACK_W, UNKNOWN,
};
use crate::{Line, State, SCREEN_NIGHT, SCREEN_TONIGHT};

const DAY_MIN: i32 = 1440;
const NOW_R: f32 = 5.0;
const TICK_RADIUS: f32 = 97.0;
const TICK_R: f32 = 1.5;
const TICK_INK: Abgr2222 = Abgr2222::from_levels(1, 1, 1);

/// The app opens on this screen from this long before tonight's usual bedtime...
const OPENS_BEFORE_MIN: i32 = 4 * 60;
/// ...to this long after it.
const OPENS_AFTER_MIN: i32 = 2 * 60;

fn known(state: &State) -> bool {
    state.tonight_bed_min != UNKNOWN && state.tonight_wake_min != UNKNOWN
}

fn ahead(from: u16, to: u16) -> i32 {
    (to as i32 - from as i32).rem_euclid(DAY_MIN)
}

/// The screen the app opens on: this one in the hours around tonight's usual bedtime, unless a night is under way.
pub fn opening_screen(state: &State) -> u8 {
    let evening = state.have_status != 0
        && known(state)
        && state.now_min != UNKNOWN
        && state.night_kind != KIND_SO_FAR
        && (ahead(state.now_min, state.tonight_bed_min) <= OPENS_BEFORE_MIN
            || ahead(state.tonight_bed_min, state.now_min) <= OPENS_AFTER_MIN);
    if evening {
        SCREEN_TONIGHT
    } else {
        SCREEN_NIGHT
    }
}

/// "in 2h10", "about now", "45m past": now against the usual bedtime.
pub fn countdown(line: &mut Line, now: u16, bed: u16, wake: u16) {
    let since = ahead(bed, now);
    let to = ahead(now, bed);
    let asleep_window = ahead(bed, wake);
    let (minutes, past) = if since <= asleep_window { (since, true) } else { (to, false) };
    if minutes <= ON_TIME_MIN {
        line.push("about now");
        return;
    }
    if !past {
        line.push("in ");
    }
    if minutes < 60 {
        line.num(minutes as u32, 1).push("m");
    } else {
        duration(line, minutes as u32);
    }
    if past {
        line.push(" past");
    }
}

/// The lines the screen shows, top to bottom.
pub struct Words {
    pub title: Line,
    pub bed: Line,
    pub wake: Line,
    pub countdown: Line,
}

pub fn words(state: &State) -> Words {
    let mut w = Words { title: Line::new(), bed: Line::new(), wake: Line::new(), countdown: Line::new() };
    w.title.push("Tonight");
    if !known(state) {
        w.bed.push("learning");
        match state.nights_to_go {
            0 => w.wake.push("wear it to bed"),
            1 => w.wake.push("1 more night"),
            n => w.wake.num(n as u32, 1).push(" more nights"),
        };
        return w;
    }
    clock(&mut w.bed, state.tonight_bed_min);
    w.wake.push("wake ");
    clock(&mut w.wake, state.tonight_wake_min);
    if state.now_min != UNKNOWN {
        countdown(&mut w.countdown, state.now_min, state.tonight_bed_min, state.tonight_wake_min);
    }
    w
}

fn at(minute: u16) -> f32 {
    minute as f32 / DAY_MIN as f32
}

/// The usual night as an arc on the Rest band, capped round at both ends; the rest of the band is its track.
fn dial(state: &State) -> impl Fn(f32, f32, f32) -> Option<Rgb> {
    let (from, len) = if known(state) {
        (at(state.tonight_bed_min), ahead(state.tonight_bed_min, state.tonight_wake_min) as f32 / DAY_MIN as f32)
    } else {
        (0.0, 0.0)
    };
    let mid = (REST_IN + REST_OUT) / 2.0;
    let half = (REST_OUT - REST_IN) / 2.0;
    move |dx, dy, t| {
        let r2 = dx * dx + dy * dy;
        if len > 0.0 {
            let cap = |a: f32| {
                let (cx, cy) = on_ring(mid, a);
                (dx - cx) * (dx - cx) + (dy - cy) * (dy - cy) <= half * half
            };
            let along = if t >= from { t - from } else { t - from + 1.0 };
            if r2 >= REST_IN * REST_IN && r2 <= REST_OUT * REST_OUT && along <= len {
                return Some(rest_ink(along / len));
            }
            if cap(from) {
                return Some(rest_ink(0.0));
            }
            if cap(from + len) {
                return Some(rest_ink(1.0));
            }
        }
        let (lo, hi) = (mid - TRACK_W / 2.0, mid + TRACK_W / 2.0);
        (r2 >= lo * lo && r2 <= hi * hi).then_some(REST_TRACK)
    }
}

fn disc(fb: &mut Surface<Abgr2222>, cx: f32, cy: f32, r: f32, ink: Abgr2222) {
    for y in (cy - r - 1.0) as i32..=(cy + r + 1.0) as i32 {
        for x in (cx - r - 1.0) as i32..=(cx + r + 1.0) as i32 {
            let (ex, ey) = (x as f32 - cx, y as f32 - cy);
            if ex * ex + ey * ey <= r * r && fb.is_lit(x, y) {
                fb.fill_rect(x, y, 1, 1, ink);
            }
        }
    }
}

pub fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    draw_band(fb, REST_IN - 1.0, REST_OUT + 1.0, dial(state));
    for quarter in 0..4 {
        let (x, y) = on_ring(TICK_RADIUS, quarter as f32 / 4.0);
        disc(fb, 119.5 + x, 119.5 + y, TICK_R, TICK_INK);
    }
    if state.now_min != UNKNOWN {
        let (x, y) = on_ring((REST_IN + REST_OUT) / 2.0, at(state.now_min));
        disc(fb, 119.5 + x, 119.5 + y, NOW_R + 1.5, GROUND);
        disc(fb, 119.5 + x, 119.5 + y, NOW_R, MAIN_INK);
    }

    let w = words(state);
    text(fb, TITLE_FACE, &w.title, TITLE_Y, TITLE_INK);
    let main = if known(state) { MAIN_INK } else { MUTED_INK };
    text(fb, REST_FACE, &w.bed, REST_Y, main);
    text(fb, DETAIL_FACE, &w.wake, TIMES_Y, main);
    text(fb, DETAIL_FACE, &w.countdown, RHYTHM_Y, RHYTHM_INK);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn say(now: u16, bed: u16, wake: u16) -> Line {
        let mut l = Line::new();
        countdown(&mut l, now, bed, wake);
        l
    }

    #[test]
    fn the_countdown_reads_toward_bed_and_past_it() {
        let (bed, wake) = (23 * 60 + 15, 7 * 60 + 5);
        assert_eq!(say(21 * 60 + 5, bed, wake).as_str(), "in 2h10");
        assert_eq!(say(22 * 60 + 30, bed, wake).as_str(), "in 45m");
        assert_eq!(say(23 * 60, bed, wake).as_str(), "about now");
        assert_eq!(say(23 * 60 + 45, bed, wake).as_str(), "about now");
        assert_eq!(say(0, bed, wake).as_str(), "45m past");
        assert_eq!(say(3 * 60 + 15, bed, wake).as_str(), "4h00 past");
        assert_eq!(say(10 * 60, bed, wake).as_str(), "in 13h15");
    }

    #[test]
    fn a_usual_after_midnight_counts_across_it() {
        assert_eq!(say(23 * 60, 30, 8 * 60).as_str(), "in 1h30");
        assert_eq!(say(2 * 60, 30, 8 * 60).as_str(), "1h30 past");
    }
}
