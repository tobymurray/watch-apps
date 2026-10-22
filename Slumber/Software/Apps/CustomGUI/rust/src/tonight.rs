//! The evening screen: tonight's usual night on the 24-hour dial, and how far off bedtime is.

use inscribed_disc::{Abgr2222, Surface};

use crate::dial::{self, Span};
use crate::night::{
    clock, duration, rhythm_ink, text, Rgb, DETAIL_FACE, GROUND, KIND_SO_FAR, MAIN_INK, MUTED_INK, ON_TIME_MIN,
    REST_FACE, REST_Y, RHYTHM_INK, RHYTHM_Y, TIMES_Y, TITLE_FACE, TITLE_INK, TITLE_Y, UNKNOWN,
};
use crate::{Line, State, SCREEN_NIGHT, SCREEN_TONIGHT};

const DAY_MIN: i32 = 1440;
const NOW_R: f32 = 5.0;

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

pub fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    let usual = known(state).then_some((
        Span { from_min: state.tonight_bed_min, to_min: state.tonight_wake_min },
        rhythm_ink as fn(f32) -> Rgb,
    ));
    dial::draw(fb, usual, None);
    if state.now_min != UNKNOWN {
        dial::mark(fb, state.now_min, NOW_R, MAIN_INK, GROUND);
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
