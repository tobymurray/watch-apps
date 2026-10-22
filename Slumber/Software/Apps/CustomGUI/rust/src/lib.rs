#![cfg_attr(not(feature = "std"), no_std)]

use inscribed_disc::{Abgr2222, Surface};
use incised_text::{faces, Face, Style};

#[cfg(feature = "device")]
use panickit as _;

mod atlas;
mod night;

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
    pub _pad: [u8; 1],
}

pub const SCREEN_NIGHT: u8 = 0;
pub const SCREEN_PICKER: u8 = 1;

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
            _pad: [0; 1],
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
    if state.screen == SCREEN_PICKER {
        draw_picker(&mut fb, state);
    } else {
        night::draw(&mut fb, state);
    }
}

pub const BUTTON_L1: u8 = 0;
pub const BUTTON_L2: u8 = 1;
pub const BUTTON_OTHER: u8 = 2;

/// The presses that open the picker from the night screen.
pub const SEQUENCE: [u8; 4] = [BUTTON_L1, BUTTON_L1, BUTTON_L2, BUTTON_L2];
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
    fnv1a(h, core::mem::offset_of!(State, _pad))
}

#[no_mangle]
pub extern "C" fn slumber_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<State>() == 40);
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
            ..State::default()
        }
    }

    /// The night screen in every state that changes what it says or fills.
    fn nights() -> Vec<(&'static str, State)> {
        let s = slept();
        vec![
            ("starting", State::default()),
            ("settled", s),
            ("short", State { rest_min: 3 * 60 + 30, rest_fill: 500, rhythm_fill: 1000, rhythm_offset_min: -12, ..s }),
            ("full", State { rest_min: 7 * 60, rest_fill: 1000, ..s }),
            ("long", State { rest_min: 13 * 60 + 59, onset_min: 20 * 60 + 59, wake_min: 10 * 60 + 58, rest_fill: 1997, ..s }),
            ("past-two-laps", State { rest_min: 23 * 60 + 59, rest_fill: 3427, ..s }),
            ("late", State { rhythm_fill: 0, rhythm_offset_min: 23 * 60 + 59, ..s }),
            ("early", State { rhythm_fill: 400, rhythm_offset_min: -(2 * 60 + 5), ..s }),
            ("learning", State { rhythm_fill: night::UNKNOWN, ..s }),
            ("so-far", State { night_kind: night::KIND_SO_FAR, ..s }),
            ("empty", State { rest_min: 0, rest_fill: 0, rhythm_fill: 0, ..s }),
            ("off-wrist", State { night_kind: night::KIND_OFF_WRIST, ..s }),
            ("none-usual", State { night_kind: night::KIND_NONE, ..s }),
            ("none", State { night_kind: night::KIND_NONE, usual_bed_min: night::UNKNOWN, ..s }),
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
    fn a_night_is_drawn_on_the_rings() {
        let empty = frame(&State { night_kind: night::KIND_NONE, ..slept() });
        let full = frame(&slept());
        let ring_changed = (0..H as i32)
            .flat_map(|y| (0..W as i32).map(move |x| (x, y)))
            .filter(|&(x, y)| {
                let r = (((x as f32 - 119.5).powi(2) + (y as f32 - 119.5).powi(2)) as f32).sqrt();
                r > 91.0 && empty[(y * W as i32 + x) as usize] != full[(y * W as i32 + x) as usize]
            })
            .count();
        assert!(ring_changed > 5000, "{ring_changed} ring pixels changed");
    }

    #[test]
    fn the_sequence_opens_only_in_order_and_in_time() {
        let run = |presses: &[(u8, u32)]| presses.iter().fold(0u8, |p, &(b, gap)| sequence_step(p, b, gap));
        let n = SEQUENCE.len() as u8;
        assert_eq!(run(&[(BUTTON_L1, 0), (BUTTON_L1, 400), (BUTTON_L2, 400), (BUTTON_L2, 400)]), n);
        assert_eq!(run(&[(BUTTON_L1, 0), (BUTTON_L1, 400), (BUTTON_L1, 400), (BUTTON_L2, 400), (BUTTON_L2, 400)]), n);
        assert_eq!(run(&[(BUTTON_L1, 0), (BUTTON_L2, 400), (BUTTON_L2, 400)]), 0);
        assert_eq!(run(&[(BUTTON_L1, 0), (BUTTON_L1, 400), (BUTTON_L2, 400), (BUTTON_L2, 2000)]), 0);
        assert_eq!(run(&[(BUTTON_L1, 0), (BUTTON_L1, 400), (BUTTON_OTHER, 400), (BUTTON_L2, 400)]), 0);
    }

    /// Writes each night state as a PNG when SLUMBER_PREVIEW_DIR is set.
    #[test]
    fn previews() {
        let Ok(dir) = std::env::var("SLUMBER_PREVIEW_DIR") else {
            return;
        };
        let opts = inscribed_disc::preview::Options { scale: 2, ..Default::default() };
        for (name, st) in nights().into_iter().chain([("picker", reported())]) {
            let rgba = inscribed_disc::preview::to_rgba(&frame(&st), W, H, opts);
            inscribed_disc::preview::write_png(format!("{dir}/{name}.png"), &rgba, W * 2, H * 2).unwrap();
        }
    }
}
