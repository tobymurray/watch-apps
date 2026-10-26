#![cfg_attr(not(feature = "std"), no_std)]

use inscribed_disc::{Abgr2222, Surface};
use incised_text::{faces, Face, Style};

#[cfg(feature = "device")]
use panickit as _;

mod atlas;
mod dial;
mod night;
mod tonight;
mod week;

/// Mirrors `slumber_state` (`slumber_gui.h`) field for field.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct State {
    pub service_up_min: u32,
    pub rows_today: u32,
    pub write_fails: u32,
    pub mark_ago_min: u32,
    pub have_status: u8,
    pub selected: u8,
    pub last_mark: u8,
    pub acc_ok: u8,
    pub hr_ok: u8,
    pub just_logged: u8,
    pub screen: u8,
    pub night_kind: u8,
    pub rest_min: u16,
    pub onset_min: u16,
    pub wake_min: u16,
    pub rest_fill: u16,
    pub rhythm_fill: u16,
    pub rhythm_offset_min: i16,
    pub usual_bed_min: u16,
    pub restless: u8,
    pub week_newest_weekday: u8,
    pub week_bed: [i16; WEEK_NIGHTS],
    pub week_wake: [i16; WEEK_NIGHTS],
    pub week_usual_bed: i16,
    pub week_usual_wake: i16,
    pub tonight_bed_min: u16,
    pub tonight_wake_min: u16,
    pub now_min: u16,
    pub nights_to_go: u8,
    pub times_vary: u8,
    pub scored_bed_min: u16,
    pub scored_wake_min: u16,
}

pub const WEEK_NIGHTS: usize = 7;
pub const NO_BAR: i16 = -1;

pub const SCREEN_NIGHT: u8 = 0;
pub const SCREEN_PICKER: u8 = 1;
pub const SCREEN_WEEK: u8 = 2;
pub const SCREEN_TONIGHT: u8 = 3;

/// The screens L1 and L2 page through, in order; the picker is not one of them.
pub const PAGES: [u8; 3] = [SCREEN_NIGHT, SCREEN_WEEK, SCREEN_TONIGHT];

/// The page `step` pages on from `screen`, wrapping both ways as the stock apps' faces do.
fn page_from(screen: u8, step: usize) -> u8 {
    let at = PAGES.iter().position(|&p| p == screen).unwrap_or(0);
    PAGES[(at + step) % PAGES.len()]
}

/// The page L2 goes to from `screen`.
pub fn page_after(screen: u8) -> u8 {
    page_from(screen, 1)
}

/// The page L1 goes to from `screen`.
pub fn page_before(screen: u8) -> u8 {
    page_from(screen, PAGES.len() - 1)
}

#[no_mangle]
pub extern "C" fn slumber_page_after(screen: u8) -> u8 {
    page_after(screen)
}

#[no_mangle]
pub extern "C" fn slumber_page_before(screen: u8) -> u8 {
    page_before(screen)
}

/// # Safety
/// `state` must point to a valid `slumber_state` for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn slumber_opening_screen(state: *const State) -> u8 {
    if state.is_null() {
        return SCREEN_NIGHT;
    }
    tonight::opening_screen(&*state)
}

/// As far below the middle as the top numeral is above it.
const DOTS_Y: i32 = 205;
const DOT_STEP: i32 = 12;
const DOT_R: i32 = 2;
const DOT_INK: Abgr2222 = Abgr2222::from_levels(1, 1, 1);

/// One dot a page, the current one white.
fn draw_dots(fb: &mut Surface<Abgr2222>, screen: u8) {
    let first = 120 - (PAGES.len() as i32 - 1) * DOT_STEP / 2;
    for (i, &page) in PAGES.iter().enumerate() {
        let cx = first + i as i32 * DOT_STEP;
        let ink = if page == screen { Abgr2222::WHITE } else { DOT_INK };
        for dy in -DOT_R..=DOT_R {
            for dx in -DOT_R..=DOT_R {
                if dx * dx + dy * dy <= DOT_R * DOT_R + 1 {
                    fb.fill_rect(cx + dx, DOTS_Y + dy, 1, 1, ink);
                }
            }
        }
    }
}

pub const NO_MARK: u8 = 0xFF;
pub const NO_MARK_AGE: u32 = 0xFFFF_FFFF;

impl Default for State {
    fn default() -> Self {
        State {
            service_up_min: 0,
            rows_today: 0,
            write_fails: 0,
            mark_ago_min: NO_MARK_AGE,
            have_status: 0,
            selected: 0,
            last_mark: NO_MARK,
            acc_ok: 0,
            hr_ok: 0,
            just_logged: 0,
            screen: SCREEN_NIGHT,
            night_kind: night::KIND_NONE,
            rest_min: 0,
            onset_min: 0,
            wake_min: 0,
            rest_fill: 0,
            rhythm_fill: night::UNKNOWN,
            rhythm_offset_min: 0,
            usual_bed_min: night::UNKNOWN,
            restless: 0,
            week_newest_weekday: 0,
            week_bed: [NO_BAR; WEEK_NIGHTS],
            week_wake: [NO_BAR; WEEK_NIGHTS],
            week_usual_bed: NO_BAR,
            week_usual_wake: NO_BAR,
            tonight_bed_min: night::UNKNOWN,
            tonight_wake_min: night::UNKNOWN,
            now_min: night::UNKNOWN,
            nights_to_go: 0,
            times_vary: 0,
            scored_bed_min: night::UNKNOWN,
            scored_wake_min: night::UNKNOWN,
        }
    }
}

/// Every label a mark has had; the code written beside each mark is its index,
/// so append and never reorder.
pub const MARKS: [&str; 8] = [
    "Lights out",
    "Awake in bed",
    "Out of bed",
    "Sitting, TV",
    "Sitting, other",
    "Moving about",
    "Watch off",
    "Up and about",
];

/// The codes the picker offers, in the order it offers them.
pub const PICKER: [u8; 4] = [0, 1, 7, 6];

// Bright on dark only: dark glyphs on a light fill drop out on this panel
// (RustGuiPoc/Docs/FINDINGS.md).
const GROUND: Abgr2222 = Abgr2222::BLACK;
const TITLE_INK: Abgr2222 = Abgr2222::from_levels(2, 2, 3);
const PICK_INK: Abgr2222 = Abgr2222::WHITE;
const NEIGHBOUR_INK: Abgr2222 = Abgr2222::GREY;
const DONE_INK: Abgr2222 = Abgr2222::GREEN;
const BODY_INK: Abgr2222 = Abgr2222::WHITE;
const HINT_INK: Abgr2222 = Abgr2222::GREY;
const WARN_INK: Abgr2222 = Abgr2222::AMBER;

const PANEL_CX: i32 = 120;
const TITLE_BASELINE_Y: i32 = 40;
const PREV_BASELINE_Y: i32 = 76;
const PICK_BASELINE_Y: i32 = 110;
const NEXT_BASELINE_Y: i32 = 142;
const MARK_BASELINE_Y: i32 = 170;
const RECORDER_BASELINE_Y: i32 = 190;
const HINT_BASELINE_Y: i32 = 210;

static TITLE_FACE: &Face = &faces::POPPINS_SEMIBOLD_18_ASCII;
static PICK_FACE: &Face = &faces::POPPINS_SEMIBOLD_20_ASCII;
static NEIGHBOUR_FACE: &Face = &faces::POPPINS_REGULAR_14_ASCII;
static STATUS_FACE: &Face = &faces::POPPINS_REGULAR_14_ASCII;
static HINT_FACE: &Face = &faces::POPPINS_REGULAR_12_ASCII;

const TITLE: &str = "SLUMBER";
const WAITING: &str = "waiting for service";
const HINT: &str = "L1/L2 PICK  R1 LOG";

/// A fixed buffer for one line; anything past its end is dropped.
///
/// Built without `core::fmt`: its integer formatting reaches a table through a
/// MOVW/MOVT pair, and the SDK's packer relocates only `R_ARM_ABS32`, so on the
/// watch every number came out as garbage and the line drew nothing.
pub(crate) struct Line {
    buf: [u8; 40],
    len: usize,
}

impl Line {
    pub(crate) fn new() -> Self {
        Line { buf: [0; 40], len: 0 }
    }

    /// Non-ASCII becomes `?`, which keeps every byte in `buf` ASCII.
    pub(crate) fn push(&mut self, s: &str) -> &mut Self {
        for &b in s.as_bytes() {
            if self.len == self.buf.len() {
                break;
            }
            self.buf[self.len] = if b.is_ascii() { b } else { b'?' };
            self.len += 1;
        }
        self
    }

    pub(crate) fn num(&mut self, value: u32, min_digits: usize) -> &mut Self {
        let mut digits = [0u8; 10];
        let mut n = 0;
        let mut v = value;
        while n < digits.len() && (v != 0 || n < min_digits.max(1)) {
            digits[n] = b'0' + (v % 10) as u8;
            v /= 10;
            n += 1;
        }
        while n > 0 {
            n -= 1;
            if self.len == self.buf.len() {
                break;
            }
            self.buf[self.len] = digits[n];
            self.len += 1;
        }
        self
    }

    // Not `from_utf8`, which also reaches a table through MOVW/MOVT on entry.
    pub(crate) fn as_str(&self) -> &str {
        // SAFETY: `push` and `num` write only ASCII bytes.
        unsafe { core::str::from_utf8_unchecked(&self.buf[..self.len]) }
    }
}

fn text(fb: &mut Surface<Abgr2222>, face: &Face, s: &str, baseline: i32, ink: Abgr2222) {
    face.draw(fb, s, PANEL_CX, baseline, Style::centered(ink, GROUND)).ok();
}

fn label(index: u8) -> &'static str {
    MARKS.get(index as usize).copied().unwrap_or("?")
}

/// The code at picker position `selected`.
fn picked(selected: u8) -> u8 {
    PICKER[selected as usize % PICKER.len()]
}

fn neighbour(selected: u8, step: i32) -> u8 {
    let n = PICKER.len() as i32;
    ((selected as i32 + step).rem_euclid(n)) as u8
}

/// What was marked last and how long ago, or that it just went in.
fn mark_line(state: &State) -> (Line, Abgr2222) {
    let mut line = Line::new();
    if state.just_logged != 0 {
        line.push("LOGGED");
        return (line, DONE_INK);
    }
    if state.last_mark == NO_MARK || state.mark_ago_min == NO_MARK_AGE {
        line.push("nothing marked yet");
        return (line, BODY_INK);
    }
    line.push(label(state.last_mark)).push("  ");
    let ago = state.mark_ago_min;
    if ago < 60 {
        line.num(ago, 1).push("m ago");
    } else {
        line.num(ago / 60, 1).push("h").num(ago % 60, 2).push(" ago");
    }
    (line, BODY_INK)
}

/// Whether rows are going to disk with both signals in them.
fn recorder_line(state: &State) -> (Line, Abgr2222) {
    let mut line = Line::new();
    if state.have_status == 0 {
        line.push(WAITING);
        return (line, WARN_INK);
    }
    if state.write_fails != 0 {
        line.push("WRITE FAILS ").num(state.write_fails, 1);
        return (line, WARN_INK);
    }
    line.push("rows ").num(state.rows_today, 1);
    line.push(if state.acc_ok != 0 { "  acc ok" } else { "  NO ACC" });
    line.push(if state.hr_ok != 0 { "  hr ok" } else { "  no hr" });
    let ink = if state.acc_ok != 0 { BODY_INK } else { WARN_INK };
    (line, ink)
}

fn draw_picker(fb: &mut Surface<Abgr2222>, state: &State) {
    text(fb, TITLE_FACE, TITLE, TITLE_BASELINE_Y, TITLE_INK);

    let sel = state.selected;
    text(fb, NEIGHBOUR_FACE, label(picked(neighbour(sel, -1))), PREV_BASELINE_Y, NEIGHBOUR_INK);
    let pick_ink = if state.just_logged != 0 { DONE_INK } else { PICK_INK };
    text(fb, PICK_FACE, label(picked(sel)), PICK_BASELINE_Y, pick_ink);
    text(fb, NEIGHBOUR_FACE, label(picked(neighbour(sel, 1))), NEXT_BASELINE_Y, NEIGHBOUR_INK);

    let (mark, mark_ink) = mark_line(state);
    text(fb, STATUS_FACE, mark.as_str(), MARK_BASELINE_Y, mark_ink);
    let (rec, rec_ink) = recorder_line(state);
    text(fb, HINT_FACE, rec.as_str(), RECORDER_BASELINE_Y, rec_ink);

    text(fb, HINT_FACE, HINT, HINT_BASELINE_Y, HINT_INK);
}

pub fn render(buf: &mut [u8], width: u32, height: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let Some(mut fb) = Surface::<Abgr2222>::round(buf, width, height) else {
        return;
    };
    fb.clear(GROUND);
    match state.screen {
        SCREEN_PICKER => {
            draw_picker(&mut fb, state);
            return;
        }
        SCREEN_WEEK => week::draw(&mut fb, state),
        SCREEN_TONIGHT => tonight::draw(&mut fb, state),
        _ => night::draw(&mut fb, state),
    }
    draw_dots(&mut fb, state.screen);
}

pub const BUTTON_L1: u8 = 0;
pub const BUTTON_L2: u8 = 1;
pub const BUTTON_OTHER: u8 = 2;
pub const BUTTON_R1: u8 = 3;

/// The presses that open the picker from any page: R1, which does nothing else there, so paging never opens it.
pub const SEQUENCE: [u8; 3] = [BUTTON_R1, BUTTON_R1, BUTTON_R1];
/// A longer pause between presses starts the sequence again.
pub const SEQUENCE_GAP_MS: u32 = 1500;

/// How much of SEQUENCE the presses so far end with, after `button` follows `progress` of it.
pub fn sequence_step(progress: u8, button: u8, gap_ms: u32) -> u8 {
    let done = if gap_ms > SEQUENCE_GAP_MS { 0 } else { (progress as usize).min(SEQUENCE.len() - 1) };
    let mut seen = [0u8; SEQUENCE.len()];
    seen[..done].copy_from_slice(&SEQUENCE[..done]);
    seen[done] = button;
    let pressed = &seen[..=done];
    (0..=pressed.len())
        .rev()
        .find(|&k| k <= SEQUENCE.len() && pressed[pressed.len() - k..] == SEQUENCE[..k])
        .unwrap_or(0) as u8
}

#[no_mangle]
pub extern "C" fn slumber_sequence_step(progress: u8, button: u8, gap_ms: u32) -> u8 {
    sequence_step(progress, button, gap_ms)
}

#[no_mangle]
pub extern "C" fn slumber_sequence_len() -> u8 {
    SEQUENCE.len() as u8
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `slumber_abi::fingerprint()`
/// in slumber_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, core::mem::offset_of!(State, service_up_min));
    let h = fnv1a(h, core::mem::offset_of!(State, rows_today));
    let h = fnv1a(h, core::mem::offset_of!(State, write_fails));
    let h = fnv1a(h, core::mem::offset_of!(State, mark_ago_min));
    let h = fnv1a(h, core::mem::offset_of!(State, have_status));
    let h = fnv1a(h, core::mem::offset_of!(State, selected));
    let h = fnv1a(h, core::mem::offset_of!(State, last_mark));
    let h = fnv1a(h, core::mem::offset_of!(State, acc_ok));
    let h = fnv1a(h, core::mem::offset_of!(State, hr_ok));
    let h = fnv1a(h, core::mem::offset_of!(State, just_logged));
    let h = fnv1a(h, core::mem::offset_of!(State, screen));
    let h = fnv1a(h, core::mem::offset_of!(State, night_kind));
    let h = fnv1a(h, core::mem::offset_of!(State, rest_min));
    let h = fnv1a(h, core::mem::offset_of!(State, onset_min));
    let h = fnv1a(h, core::mem::offset_of!(State, wake_min));
    let h = fnv1a(h, core::mem::offset_of!(State, rest_fill));
    let h = fnv1a(h, core::mem::offset_of!(State, rhythm_fill));
    let h = fnv1a(h, core::mem::offset_of!(State, rhythm_offset_min));
    let h = fnv1a(h, core::mem::offset_of!(State, usual_bed_min));
    let h = fnv1a(h, core::mem::offset_of!(State, restless));
    let h = fnv1a(h, core::mem::offset_of!(State, week_newest_weekday));
    let h = fnv1a(h, core::mem::offset_of!(State, week_bed));
    let h = fnv1a(h, core::mem::offset_of!(State, week_wake));
    let h = fnv1a(h, core::mem::offset_of!(State, week_usual_bed));
    let h = fnv1a(h, core::mem::offset_of!(State, week_usual_wake));
    let h = fnv1a(h, core::mem::offset_of!(State, tonight_bed_min));
    let h = fnv1a(h, core::mem::offset_of!(State, tonight_wake_min));
    let h = fnv1a(h, core::mem::offset_of!(State, now_min));
    let h = fnv1a(h, core::mem::offset_of!(State, nights_to_go));
    let h = fnv1a(h, core::mem::offset_of!(State, times_vary));
    let h = fnv1a(h, core::mem::offset_of!(State, scored_bed_min));
    fnv1a(h, core::mem::offset_of!(State, scored_wake_min))
}

#[no_mangle]
pub extern "C" fn slumber_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<State>() == 84);
const _: () = assert!(core::mem::align_of::<State>() == 4);
const _: () = assert!(core::mem::offset_of!(State, rows_today) == 4);
const _: () = assert!(core::mem::offset_of!(State, write_fails) == 8);
const _: () = assert!(core::mem::offset_of!(State, mark_ago_min) == 12);
const _: () = assert!(core::mem::offset_of!(State, have_status) == 16);
const _: () = assert!(core::mem::offset_of!(State, selected) == 17);
const _: () = assert!(core::mem::offset_of!(State, last_mark) == 18);
const _: () = assert!(core::mem::offset_of!(State, acc_ok) == 19);
const _: () = assert!(core::mem::offset_of!(State, hr_ok) == 20);
const _: () = assert!(core::mem::offset_of!(State, just_logged) == 21);
const _: () = assert!(core::mem::offset_of!(State, screen) == 22);
const _: () = assert!(core::mem::offset_of!(State, night_kind) == 23);
const _: () = assert!(core::mem::offset_of!(State, rest_min) == 24);
const _: () = assert!(core::mem::offset_of!(State, rest_fill) == 30);
const _: () = assert!(core::mem::offset_of!(State, rhythm_offset_min) == 34);
const _: () = assert!(core::mem::offset_of!(State, usual_bed_min) == 36);
const _: () = assert!(core::mem::offset_of!(State, restless) == 38);
const _: () = assert!(core::mem::offset_of!(State, week_bed) == 40);
const _: () = assert!(core::mem::offset_of!(State, week_wake) == 54);
const _: () = assert!(core::mem::offset_of!(State, week_usual_bed) == 68);
const _: () = assert!(core::mem::offset_of!(State, tonight_bed_min) == 72);
const _: () = assert!(core::mem::offset_of!(State, now_min) == 76);
const _: () = assert!(core::mem::offset_of!(State, nights_to_go) == 78);
const _: () = assert!(core::mem::offset_of!(State, times_vary) == 79);
const _: () = assert!(core::mem::offset_of!(State, scored_bed_min) == 80);
const _: () = assert!(core::mem::offset_of!(State, scored_wake_min) == 82);

#[no_mangle]
pub extern "C" fn slumber_mark_count() -> u32 {
    PICKER.len() as u32
}

#[no_mangle]
pub extern "C" fn slumber_mark_code(index: u32) -> u32 {
    picked((index % PICKER.len() as u32) as u8) as u32
}

/// Copies at most `cap - 1` bytes and a NUL; returns the bytes copied.
pub fn copy_label(code: u32, out: &mut [u8]) -> usize {
    let Some(src) = MARKS.get(code as usize) else {
        if let Some(first) = out.first_mut() {
            *first = 0;
        }
        return 0;
    };
    if out.is_empty() {
        return 0;
    }
    let n = src.len().min(out.len() - 1);
    out[..n].copy_from_slice(&src.as_bytes()[..n]);
    out[n] = 0;
    n
}

/// # Safety
/// `out` must point to at least `cap` writable bytes.
#[no_mangle]
pub unsafe extern "C" fn slumber_mark_label(code: u32, out: *mut u8, cap: u32) -> u32 {
    if out.is_null() || cap == 0 {
        return 0;
    }
    copy_label(code, core::slice::from_raw_parts_mut(out, cap as usize)) as u32
}

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `slumber_state`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn slumber_render(
    buf: *mut u8,
    buf_len: u32,
    width: u16,
    height: u16,
    state: *const State,
) {
    if buf.is_null() || state.is_null() || buf_len == 0 || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, buf_len as usize);
    render(slice, width as u32, height as u32, &*state);
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;

    fn reported() -> State {
        State {
            service_up_min: 9 * 60 + 59,
            rows_today: 2880,
            write_fails: 0,
            mark_ago_min: 23 * 60 + 59,
            have_status: 1,
            selected: 2,
            last_mark: 4,
            acc_ok: 1,
            hr_ok: 1,
            just_logged: 0,
            screen: SCREEN_PICKER,
            ..State::default()
        }
    }

    /// Last night as the night screen gets it: 7h42 from 23:29, 38 min late.
    fn slept() -> State {
        State {
            have_status: 1,
            screen: SCREEN_NIGHT,
            night_kind: night::KIND_SETTLED,
            rest_min: 7 * 60 + 42,
            onset_min: 23 * 60 + 29,
            wake_min: 7 * 60 + 30,
            rest_fill: 1100,
            rhythm_fill: 860,
            rhythm_offset_min: 38,
            usual_bed_min: 22 * 60 + 51,
            scored_bed_min: 22 * 60 + 51,
            scored_wake_min: 6 * 60 + 55,
            week_usual_bed: 22 * 60 + 51 - 720,
            week_usual_wake: 6 * 60 + 55 + 720,
            ..State::default()
        }
    }

    /// The night screen in every state that changes what it says or fills.
    fn nights() -> Vec<(&'static str, State)> {
        let s = slept();
        vec![
            ("starting", State::default()),
            ("settled", s),
            ("short", State { rest_min: 3 * 60 + 30, wake_min: 3 * 60 + 5, rest_fill: 500, rhythm_fill: 1000, rhythm_offset_min: -12, ..s }),
            ("full", State { rest_min: 7 * 60, rest_fill: 1000, ..s }),
            ("long", State { rest_min: 13 * 60 + 59, onset_min: 20 * 60 + 59, wake_min: 10 * 60 + 58, rest_fill: 1997, ..s }),
            ("past-two-laps", State { rest_min: 23 * 60 + 59, rest_fill: 3427, ..s }),
            ("late", State { rhythm_fill: 0, rhythm_offset_min: 23 * 60 + 59, ..s }),
            ("early", State { rhythm_fill: 400, rhythm_offset_min: -(2 * 60 + 5), ..s }),
            ("learning", State { rhythm_fill: night::UNKNOWN, scored_bed_min: night::UNKNOWN, scored_wake_min: night::UNKNOWN, ..s }),
            ("times-vary", State { rhythm_fill: night::RHYTHM_VARIES, scored_bed_min: night::UNKNOWN, scored_wake_min: night::UNKNOWN, ..s }),
            ("lie-in", State { rest_min: 9 * 60 + 15, onset_min: 50, wake_min: 10 * 60 + 40, rhythm_offset_min: 119, ..s }),
            ("so-far", State { night_kind: night::KIND_SO_FAR, ..s }),
            ("empty", State { rest_min: 0, rest_fill: 0, rhythm_fill: 0, ..s }),
            ("off-wrist", State { night_kind: night::KIND_OFF_WRIST, ..s }),
            ("none-usual", State { night_kind: night::KIND_NONE, ..s }),
            ("none", State { night_kind: night::KIND_NONE, usual_bed_min: night::UNKNOWN, ..s }),
        ]
    }

    /// Minutes after noon for a local clock time.
    fn hm(h: i32, m: i32) -> i16 {
        ((h * 60 + m + 720) % 1440) as i16
    }

    /// A week from (bed, wake) clock pairs, oldest first; `None` is a night not slept on the wrist.
    fn week_of(nights: [Option<((i32, i32), (i32, i32))>; WEEK_NIGHTS], usual: Option<((i32, i32), (i32, i32))>) -> State {
        let mut st = State {
            have_status: 1,
            screen: SCREEN_WEEK,
            week_newest_weekday: 3,
            week_usual_bed: NO_BAR,
            week_usual_wake: NO_BAR,
            ..slept()
        };
        let span = |((bh, bm), (wh, wm)): ((i32, i32), (i32, i32))| {
            let (bed, wake) = (hm(bh, bm), hm(wh, wm));
            (bed, if wake < bed { wake + 1440 } else { wake })
        };
        for (i, n) in nights.into_iter().enumerate() {
            if let Some(n) = n {
                (st.week_bed[i], st.week_wake[i]) = span(n);
            }
        }
        if let Some(u) = usual {
            (st.week_usual_bed, st.week_usual_wake) = span(u);
        }
        st
    }

    /// The week screen in every state that changes what it says or how it scales.
    fn weeks() -> Vec<(&'static str, State)> {
        let usual = Some(((23, 10), (7, 5)));
        vec![
            ("week", week_of([
                Some(((23, 0), (7, 0))), Some(((23, 40), (7, 10))), None, Some(((22, 50), (6, 55))),
                Some(((0, 55), (8, 30))), Some(((1, 30), (10, 15))), Some(((23, 15), (7, 20))),
            ], usual)),
            ("week-steady", week_of([Some(((23, 5), (7, 0))); WEEK_NIGHTS], usual)),
            ("week-noon", week_of([
                None, None, Some(((4, 0), (13, 30))), Some(((23, 0), (7, 0))),
                Some(((20, 30), (5, 0))), None, Some(((23, 30), (7, 30))),
            ], usual)),
            ("week-learning", week_of([None, None, None, None, None, Some(((23, 0), (7, 0))), Some(((23, 30), (7, 15)))], None)),
            ("week-empty", week_of([None; WEEK_NIGHTS], None)),
            ("week-times-vary", State { times_vary: 1, ..week_of([
                Some(((22, 30), (6, 30))), Some(((22, 30), (6, 30))), Some(((8, 30), (15, 30))), Some(((8, 30), (15, 30))),
                Some(((8, 30), (15, 30))), Some(((22, 30), (6, 30))), Some(((22, 30), (6, 30)))], None) }),
        ]
    }

    /// The evening screen in every state that changes what it says or draws.
    fn tonights() -> Vec<(&'static str, State)> {
        let s = State {
            screen: SCREEN_TONIGHT,
            tonight_bed_min: 23 * 60 + 15,
            tonight_wake_min: 7 * 60 + 5,
            now_min: 21 * 60 + 5,
            ..slept()
        };
        vec![
            ("tonight", s),
            ("tonight-now", State { now_min: 23 * 60 + 20, ..s }),
            ("tonight-past", State { now_min: 1 * 60 + 40, ..s }),
            ("tonight-late-owl", State { tonight_bed_min: 2 * 60 + 30, tonight_wake_min: 11 * 60 + 45, now_min: 23 * 60 + 59, ..s }),
            ("tonight-no-clock", State { now_min: night::UNKNOWN, ..s }),
            ("tonight-learning", State { tonight_bed_min: night::UNKNOWN, tonight_wake_min: night::UNKNOWN, nights_to_go: 2, ..s }),
            ("tonight-one-more", State { tonight_bed_min: night::UNKNOWN, tonight_wake_min: night::UNKNOWN, nights_to_go: 1, ..s }),
            ("tonight-unworn", State { tonight_bed_min: night::UNKNOWN, tonight_wake_min: night::UNKNOWN, nights_to_go: 0, ..s }),
            ("tonight-times-vary", State { tonight_bed_min: night::UNKNOWN, tonight_wake_min: night::UNKNOWN, times_vary: 1, ..s }),
        ]
    }

    /// Every state that changes a line's text or ink, at its widest.
    fn variants() -> Vec<State> {
        let mut out = vec![State::default(), reported()];
        for sel in 0..PICKER.len() as u8 {
            out.push(State { selected: sel, last_mark: picked(sel), ..reported() });
        }
        for code in 0..MARKS.len() as u8 {
            out.push(State { last_mark: code, ..reported() });
        }
        out.push(State { just_logged: 1, ..reported() });
        out.push(State { write_fails: 4_294_967_295, ..reported() });
        out.push(State { acc_ok: 0, hr_ok: 0, rows_today: 4_294_967_295, ..reported() });
        out.push(State { mark_ago_min: 7, ..reported() });
        out.extend(nights().into_iter().map(|(_, st)| st));
        out.extend(weeks().into_iter().map(|(_, st)| st));
        out.extend(tonights().into_iter().map(|(_, st)| st));
        out
    }

    fn frame(state: &State) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, state);
        buf
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        let mut buf = vec![0xAAu8; n + 64];
        render(&mut buf, W, H, &reported());
        assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran the framebuffer");
    }

    #[test]
    fn every_string_has_a_glyph_in_its_face() {
        assert!(TITLE_FACE.covers(TITLE));
        assert!(HINT_FACE.covers(HINT));
        for m in MARKS {
            assert!(PICK_FACE.covers(m), "{m}");
            assert!(NEIGHBOUR_FACE.covers(m), "{m}");
        }
        for st in variants() {
            assert!(STATUS_FACE.covers(mark_line(&st).0.as_str()));
            assert!(HINT_FACE.covers(recorder_line(&st).0.as_str()));
        }
    }

    /// The panel is round and the buffer square, so nothing may land behind
    /// the bezel in any state.
    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        for state in variants() {
            let buf = frame(&state);
            for y in 0..H as i32 {
                for x in 0..W as i32 {
                    let lit = (2 * x - 239).pow(2) + (2 * y - 239).pow(2) <= 239 * 239;
                    assert!(
                        lit || buf[(y * W as i32 + x) as usize] == GROUND.0,
                        "({x},{y}) lit behind the bezel in {state:?}"
                    );
                }
            }
        }
    }

    #[test]
    fn waiting_and_reported_draw_differently() {
        assert_ne!(frame(&State::default()), frame(&reported()));
    }

    #[test]
    fn the_picker_wraps_both_ways() {
        assert_eq!(neighbour(0, -1) as usize, PICKER.len() - 1);
        assert_eq!(neighbour(PICKER.len() as u8 - 1, 1), 0);
    }

    #[test]
    fn the_picker_offers_four_and_keeps_the_old_codes() {
        let offered: Vec<&str> =
            (0..slumber_mark_count()).map(|i| label(slumber_mark_code(i) as u8)).collect();
        assert_eq!(offered, ["Lights out", "Awake in bed", "Up and about", "Watch off"]);
        assert_eq!(MARKS[3], "Sitting, TV");
    }

    #[test]
    fn mark_ages_read_as_minutes_then_hours() {
        let at = |m| mark_line(&State { mark_ago_min: m, last_mark: 3, ..reported() }).0;
        assert_eq!(at(0).as_str(), "Sitting, TV  0m ago");
        assert_eq!(at(59).as_str(), "Sitting, TV  59m ago");
        assert_eq!(at(65).as_str(), "Sitting, TV  1h05 ago");
    }

    #[test]
    fn the_recorder_line_says_what_is_missing() {
        assert_eq!(recorder_line(&reported()).0.as_str(), "rows 2880  acc ok  hr ok");
        let st = State { acc_ok: 0, hr_ok: 0, ..reported() };
        assert_eq!(recorder_line(&st).0.as_str(), "rows 2880  NO ACC  no hr");
        let st = State { write_fails: 3, ..reported() };
        assert_eq!(recorder_line(&st).0.as_str(), "WRITE FAILS 3");
    }

    #[test]
    fn labels_copy_whole_and_terminated() {
        let mut out = [0xAAu8; 24];
        assert_eq!(copy_label(4, &mut out), "Sitting, other".len());
        assert_eq!(&out[..15], b"Sitting, other\0");
        let mut tiny = [0xAAu8; 4];
        assert_eq!(copy_label(0, &mut tiny), 3);
        assert_eq!(&tiny, b"Lig\0");
        assert_eq!(copy_label(99, &mut out), 0);
        assert_eq!(out[0], 0);
    }

    /// The service's Mark message carries 24 bytes including the NUL.
    #[test]
    fn every_label_fits_the_mark_message() {
        for m in MARKS {
            assert!(m.len() < 24 && m.is_ascii() && !m.contains('\n'), "{m}");
        }
    }

    #[test]
    fn non_ascii_never_reaches_the_buffer() {
        let mut line = Line::new();
        line.push("caf\u{e9} \u{1F319}");
        assert!(line.buf[..line.len].is_ascii());
        assert_eq!(line.as_str(), "caf?? ????");
    }

    #[test]
    fn no_line_is_cut_short_by_its_buffer() {
        for st in variants() {
            for line in [mark_line(&st).0, recorder_line(&st).0] {
                assert!(line.len < line.buf.len(), "{:?} filled its buffer", line.as_str());
            }
        }
    }

    /// Every line of the night screen sits inside the Rhythm ring, at the rows it is drawn on.
    #[test]
    fn night_text_clears_the_rings() {
        let r = night::TEXT_RADIUS;
        for (name, st) in nights() {
            let w = night::words(&st);
            for (face, line, y) in [
                (night::TITLE_FACE, &w.title, night::TITLE_Y),
                (night::REST_FACE, &w.rest, night::REST_Y),
                (night::DETAIL_FACE, &w.times, night::TIMES_Y),
                (night::DETAIL_FACE, &w.rhythm, night::RHYTHM_Y),
            ] {
                let m = face.measure(line.as_str());
                let half = (m.ink_right - m.ink_left) as f32 / 2.0 + 1.0;
                for row in [y - face.ascent as i32, y + face.descent as i32] {
                    let d = row as f32 - 119.5;
                    let room = (r * r - d * d).max(0.0).sqrt();
                    assert!(half <= room, "{name}: {:?} needs {half} at row {row}, has {room}", line.as_str());
                }
            }
        }
    }

    #[test]
    fn night_lines_say_what_happened() {
        let w = night::words(&slept());
        assert_eq!(w.title.as_str(), "Last night");
        assert_eq!(w.rest.as_str(), "7h42");
        assert_eq!(w.times.as_str(), "23:29-07:30");
        assert_eq!(w.rhythm.as_str(), "38m late");
        let none = night::words(&State { night_kind: night::KIND_NONE, ..slept() });
        assert_eq!(none.times.as_str(), "usually 22:51");
    }

    #[test]
    fn an_unscored_night_says_why_from_its_own_nights() {
        let unscored = State { rhythm_fill: night::UNKNOWN, scored_bed_min: night::UNKNOWN, scored_wake_min: night::UNKNOWN, ..slept() };
        assert_eq!(night::words(&State { times_vary: 1, ..unscored }).rhythm.as_str(), "learning");
        assert_eq!(night::words(&State { rhythm_fill: night::RHYTHM_VARIES, ..unscored }).rhythm.as_str(), "times vary");
    }

    /// The pixel on the middle of the dial's band at `minute` past midnight, `along` pixels clockwise of it.
    fn band_pixel(buf: &[u8], minute: u16, along: f32) -> u8 {
        let t = minute as f32 / 1440.0 * core::f32::consts::TAU + along / 110.5;
        let (x, y) = (119.5 + 110.5 * t.sin(), 119.5 - 110.5 * t.cos());
        buf[(y.round() as i32 * W as i32 + x.round() as i32) as usize]
    }

    fn on_band(buf: &[u8], minute: u16) -> u8 {
        band_pixel(buf, minute, 0.0)
    }

    /// Last night 23:29-07:30 lights the band through 03:20, the middle of an hour, and not at 15:20.
    #[test]
    fn a_night_is_drawn_where_it_fell_on_the_clock() {
        let lit = frame(&slept());
        let none = frame(&State { night_kind: night::KIND_NONE, ..slept() });
        assert_ne!(on_band(&lit, 3 * 60 + 20), GROUND.0);
        assert_eq!(on_band(&lit, 15 * 60 + 20), GROUND.0);
        assert_eq!(on_band(&none, 3 * 60 + 20), GROUND.0);
    }

    /// Each hour a night crosses is cut back to the ground colour.
    #[test]
    fn a_night_is_notched_at_each_hour() {
        let lit = frame(&slept());
        for h in [0u16, 1, 2, 5, 7] {
            let cut = [-1.0, -0.5, 0.0, 0.5, 1.0].iter().any(|&a| band_pixel(&lit, h * 60, a) == GROUND.0);
            assert!(cut, "{h}:00 is not notched");
            assert_ne!(on_band(&lit, h * 60 + 30), GROUND.0, "{h}:30");
        }
    }

    /// Lines on a ringless screen clear the bezel by a pixel at the rows they are drawn on.
    fn fits_the_glass(face: &Face, s: &str, x: i32, y: i32) -> bool {
        let m = face.measure(s);
        let (left, right) = (x as f32 - (m.ink_right - m.ink_left) as f32 / 2.0 - 1.0, x as f32 + (m.ink_right - m.ink_left) as f32 / 2.0 + 1.0);
        [y - face.ascent as i32, y + face.descent as i32].iter().all(|&row| {
            let d = row as f32 - 119.5;
            let room = (118.5f32 * 118.5 - d * d).max(0.0).sqrt();
            left >= 119.5 - room && right <= 119.5 + room
        })
    }

    #[test]
    fn week_text_fits_the_glass() {
        for (name, st) in weeks() {
            let (summary, _) = week::summary(&st);
            assert!(week::SUMMARY_FACE.covers(summary.as_str()), "{name}");
            assert!(fits_the_glass(week::SUMMARY_FACE, summary.as_str(), 120, week::SUMMARY_Y), "{name}: {:?}", summary.as_str());
            assert!(fits_the_glass(week::TITLE_FACE, "Past week", 120, week::TITLE_Y));
            for i in 0..WEEK_NIGHTS {
                assert!(fits_the_glass(week::DAY_FACE, week::day(&st, i), week::column_x(i), week::DAY_Y), "{name} day {i}");
            }
        }
    }

    #[test]
    fn the_week_counts_bedtimes_near_the_usual() {
        let say = |st: &State| week::summary(st).0.as_str().to_owned();
        let w = weeks();
        assert_eq!(say(&w[0].1), "4 of 6 on time");
        assert_eq!(say(&w[1].1), "7 of 7 on time");
        assert_eq!(say(&w[3].1), "learning");
        assert_eq!(say(&w[4].1), "No nights yet");
        assert_eq!(say(&w[5].1), "times vary");
    }

    #[test]
    fn the_week_letters_end_on_the_newest_evening() {
        let st = State { week_newest_weekday: 3, ..State::default() };
        let letters: String = (0..WEEK_NIGHTS).map(|i| week::day(&st, i)).collect();
        assert_eq!(letters, "TFSSMTW");
    }

    #[test]
    fn every_bar_lies_inside_the_plot() {
        for (name, st) in weeks() {
            let (lo, hi) = week::span(&st);
            for i in 0..WEEK_NIGHTS {
                if st.week_bed[i] != NO_BAR {
                    assert!(lo <= st.week_bed[i] as i32 && st.week_wake[i] as i32 <= hi, "{name} night {i}");
                }
            }
        }
    }

    #[test]
    fn l1_and_l2_page_both_ways_and_wrap() {
        assert_eq!(page_after(SCREEN_NIGHT), SCREEN_WEEK);
        assert_eq!(page_after(SCREEN_WEEK), SCREEN_TONIGHT);
        assert_eq!(page_after(SCREEN_TONIGHT), SCREEN_NIGHT);
        for &p in &PAGES {
            assert_eq!(page_before(page_after(p)), p);
        }
        assert_eq!(page_before(SCREEN_NIGHT), SCREEN_TONIGHT);
        assert!(!PAGES.contains(&SCREEN_PICKER));
    }

    /// Every line of the evening screen sits inside the dial, at the rows it is drawn on.
    #[test]
    fn tonight_text_clears_the_dial() {
        let r = night::TEXT_RADIUS;
        for (name, st) in tonights() {
            let w = tonight::words(&st);
            for (face, line, y) in [
                (night::TITLE_FACE, &w.title, night::TITLE_Y),
                (night::REST_FACE, &w.bed, night::REST_Y),
                (night::DETAIL_FACE, &w.wake, night::TIMES_Y),
                (night::DETAIL_FACE, &w.countdown, night::RHYTHM_Y),
            ] {
                assert!(face.covers(line.as_str()), "{name}: {:?}", line.as_str());
                let m = face.measure(line.as_str());
                let half = (m.ink_right - m.ink_left) as f32 / 2.0 + 1.0;
                for row in [y - face.ascent as i32, y + face.descent as i32] {
                    let d = row as f32 - 119.5;
                    let room = (r * r - d * d).max(0.0).sqrt();
                    assert!(half <= room, "{name}: {:?} needs {half} at row {row}, has {room}", line.as_str());
                }
            }
        }
    }

    #[test]
    fn tonight_says_the_usual_and_how_far_off_it_is() {
        let t = tonights();
        let w = tonight::words(&t[0].1);
        assert_eq!((w.bed.as_str(), w.wake.as_str(), w.countdown.as_str()), ("23:15", "wake 07:05", "in 2h10"));
        assert_eq!(tonight::words(&t[5].1).wake.as_str(), "2 more nights");
        assert_eq!(tonight::words(&t[6].1).wake.as_str(), "1 more night");
        assert_eq!(tonight::words(&t[8].1).bed.as_str(), "no usual");
        assert_eq!(tonight::words(&t[8].1).wake.as_str(), "times vary");
        assert_eq!(tonight::words(&t[4].1).countdown.as_str(), "");
    }

    #[test]
    fn the_app_opens_on_tonight_only_around_the_usual_bedtime() {
        let t = tonights()[0].1;
        let at = |now: u16| tonight::opening_screen(&State { now_min: now, ..t });
        assert_eq!(at(19 * 60 + 15), SCREEN_TONIGHT);
        assert_eq!(at(19 * 60 + 14), SCREEN_NIGHT);
        assert_eq!(at(1 * 60 + 15), SCREEN_TONIGHT);
        assert_eq!(at(1 * 60 + 16), SCREEN_NIGHT);
        assert_eq!(at(9 * 60), SCREEN_NIGHT);
        assert_eq!(tonight::opening_screen(&State { night_kind: night::KIND_SO_FAR, now_min: 23 * 60, ..t }), SCREEN_NIGHT);
        assert_eq!(tonight::opening_screen(&State { now_min: night::UNKNOWN, ..t }), SCREEN_NIGHT);
        assert_eq!(tonight::opening_screen(&tonights()[5].1), SCREEN_NIGHT);
        assert_eq!(tonight::opening_screen(&State::default()), SCREEN_NIGHT);
    }

    /// Each numeral's ink is centred on its place on the dial, to the pixel, and clear of the usual arc.
    #[test]
    fn numerals_sit_centred_inside_the_usual_arc() {
        for (label, minute) in dial::NUMERALS {
            let mut buf = vec![0u8; (W * H) as usize];
            let mut fb = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
            fb.clear(GROUND);
            let (x, baseline) = dial::numeral_at(minute);
            dial::NUMERAL_FACE.draw(&mut fb, label, x, baseline, Style::centered(Abgr2222::WHITE, GROUND)).ok();
            let lit: Vec<(i32, i32)> = (0..H as i32)
                .flat_map(|y| (0..W as i32).map(move |x| (x, y)))
                .filter(|&(x, y)| buf[(y * W as i32 + x) as usize] != GROUND.0)
                .collect();
            let (x0, x1) = (lit.iter().map(|p| p.0).min().unwrap(), lit.iter().map(|p| p.0).max().unwrap());
            let (y0, y1) = (lit.iter().map(|p| p.1).min().unwrap(), lit.iter().map(|p| p.1).max().unwrap());
            let t = minute as f32 / 1440.0 * core::f32::consts::TAU;
            let (cx, cy) = (119.5 + dial::NUMERAL_RADIUS * t.sin(), 119.5 - dial::NUMERAL_RADIUS * t.cos());
            assert!(((x0 + x1) as f32 / 2.0 - cx).abs() <= 1.0, "{label} centred at x {} not {cx}", (x0 + x1) as f32 / 2.0);
            assert!(((y0 + y1) as f32 / 2.0 - cy).abs() <= 1.0, "{label} centred at y {} not {cy}", (y0 + y1) as f32 / 2.0);
            for &(x, y) in &lit {
                let (dx, dy) = (x as f32 - 119.5, y as f32 - 119.5);
                assert!(dx * dx + dy * dy < 98.0 * 98.0, "{label} reaches the usual arc at ({x},{y})");
            }
        }
    }

    /// No numeral shares a pixel box with any line of the night or evening screen, in any state.
    #[test]
    fn numerals_never_touch_the_text() {
        let line_box = |face: &Face, s: &str, y: i32| {
            let m = face.measure(s);
            let half = (m.ink_right - m.ink_left + 1) / 2 + 1;
            (120 - half, 120 + half, y - face.ascent as i32, y + face.descent as i32)
        };
        let numeral_boxes: Vec<_> = dial::NUMERALS
            .iter()
            .map(|&(label, minute)| {
                let (x, baseline) = dial::numeral_at(minute);
                let m = dial::NUMERAL_FACE.measure(label);
                let half = (m.ink_right - m.ink_left + 1) / 2 + 1;
                (x - half, x + half, baseline - 14, baseline)
            })
            .collect();
        let mut lines: Vec<(String, (i32, i32, i32, i32))> = Vec::new();
        for (name, st) in nights() {
            let w = night::words(&st);
            for (face, l, y) in [
                (night::TITLE_FACE, &w.title, night::TITLE_Y),
                (night::REST_FACE, &w.rest, night::REST_Y),
                (night::DETAIL_FACE, &w.times, night::TIMES_Y),
                (night::DETAIL_FACE, &w.rhythm, night::RHYTHM_Y),
            ] {
                if !l.as_str().is_empty() {
                    lines.push((format!("{name}: {:?}", l.as_str()), line_box(face, l.as_str(), y)));
                }
            }
        }
        for (name, st) in tonights() {
            let w = tonight::words(&st);
            for (face, l, y) in [
                (night::TITLE_FACE, &w.title, night::TITLE_Y),
                (night::REST_FACE, &w.bed, night::REST_Y),
                (night::DETAIL_FACE, &w.wake, night::TIMES_Y),
                (night::DETAIL_FACE, &w.countdown, night::RHYTHM_Y),
            ] {
                if !l.as_str().is_empty() {
                    lines.push((format!("{name}: {:?}", l.as_str()), line_box(face, l.as_str(), y)));
                }
            }
        }
        for (what, (l0, l1, t0, t1)) in &lines {
            for (n0, n1, m0, m1) in &numeral_boxes {
                let apart = l1 < n0 || n1 < l0 || t1 < m0 || m1 < t0;
                assert!(apart, "{what} touches a numeral at x {n0}..{n1}, rows {m0}..{m1}");
            }
        }
    }

    /// The text block of the night screen is centred on the panel, to within a pixel.
    #[test]
    fn the_night_text_is_centred_on_the_panel() {
        let top = night::TITLE_Y - 14;
        let buf = frame(&slept());
        let rows: Vec<i32> = (top - 4..=night::RHYTHM_Y + 8)
            .filter(|&y| (60..180).any(|x| buf[(y * W as i32 + x) as usize] != GROUND.0))
            .collect();
        let middle = (rows[0] + rows[rows.len() - 1]) as f32 / 2.0;
        assert!((middle - 119.5).abs() <= 1.0, "text rows {}..{} centre on {middle}", rows[0], rows[rows.len() - 1]);
    }

    #[test]
    fn the_sequence_opens_only_in_order_and_in_time() {
        let run = |presses: &[(u8, u32)]| presses.iter().fold(0u8, |p, &(b, gap)| sequence_step(p, b, gap));
        let n = SEQUENCE.len() as u8;
        assert_eq!(run(&[(BUTTON_R1, 0), (BUTTON_R1, 400), (BUTTON_R1, 400)]), n);
        assert_eq!(run(&[(BUTTON_L2, 0), (BUTTON_R1, 300), (BUTTON_R1, 400), (BUTTON_R1, 400)]), n);
        assert_eq!(run(&[(BUTTON_R1, 0), (BUTTON_R1, 400)]), 2);
        assert_eq!(run(&[(BUTTON_R1, 0), (BUTTON_R1, 400), (BUTTON_R1, 2000)]), 1);
        assert_eq!(run(&[(BUTTON_R1, 0), (BUTTON_L1, 400), (BUTTON_R1, 400)]), 1);
    }

    /// Paging, however fast and in whatever order, never opens the picker.
    #[test]
    fn paging_never_opens_the_picker() {
        for presses in 0u32..(1 << 10) {
            let p = (0..10).fold(0u8, |p, i| {
                let b = if presses >> i & 1 == 0 { BUTTON_L1 } else { BUTTON_L2 };
                let q = sequence_step(p, b, 100);
                assert_ne!(q, SEQUENCE.len() as u8);
                q
            });
            assert_eq!(p, 0);
        }
    }

    /// Writes each night state as a PNG when SLUMBER_PREVIEW_DIR is set.
    #[test]
    fn previews() {
        let Ok(dir) = std::env::var("SLUMBER_PREVIEW_DIR") else {
            return;
        };
        let opts = inscribed_disc::preview::Options { scale: 2, ..Default::default() };
        for (name, st) in nights().into_iter().chain(weeks()).chain(tonights()).chain([("picker", reported())]) {
            let rgba = inscribed_disc::preview::to_rgba(&frame(&st), W, H, opts);
            inscribed_disc::preview::write_png(format!("{dir}/{name}.png"), &rgba, W * 2, H * 2).unwrap();
        }
    }
}
