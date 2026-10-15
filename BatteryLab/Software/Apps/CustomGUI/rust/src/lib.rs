#![cfg_attr(not(feature = "std"), no_std)]

use inscribed_disc::{Abgr2222, Surface};
use textkit::{faces, Face, Style};

#[cfg(feature = "device")]
use panickit as _;

/// Mirrors `batterylab_state` (`batterylab_gui.h`) field for field.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct State {
    pub pct_x1000: i32,
    pub mv: i32,
    pub ma_x1000: i32,
    pub avg_ma_x1000: i32,
    pub mah_x1000: i32,
    pub design_mah_x1000: i32,
    pub connected: i32,
    pub charging: i32,
    pub lines: u32,
    pub write_fails: u32,
    pub service_up_min: u32,
    pub have_status: u8,
    pub _pad: [u8; 3],
}

pub const NONE: i32 = i32::MIN;

impl Default for State {
    fn default() -> Self {
        State {
            pct_x1000: NONE,
            mv: NONE,
            ma_x1000: NONE,
            avg_ma_x1000: NONE,
            mah_x1000: NONE,
            design_mah_x1000: NONE,
            connected: NONE,
            charging: NONE,
            lines: 0,
            write_fails: 0,
            service_up_min: 0,
            have_status: 0,
            _pad: [0; 3],
        }
    }
}

// Bright on dark only: dark glyphs on a light fill drop out on this panel
// (RustGuiPoc/Docs/FINDINGS.md).
const GROUND: Abgr2222 = Abgr2222::BLACK;
const TITLE_INK: Abgr2222 = Abgr2222::from_levels(2, 2, 3);
const BODY_INK: Abgr2222 = Abgr2222::WHITE;
const LABEL_INK: Abgr2222 = Abgr2222::GREY;
const WARN_INK: Abgr2222 = Abgr2222::AMBER;

const PANEL_CX: i32 = 120;

static TITLE_FACE: &Face = &faces::SEMIBOLD_18_ASCII;
static BIG_FACE: &Face = &faces::SEMIBOLD_24_ASCII;
static BODY_FACE: &Face = &faces::REGULAR_14_ASCII;
static FOOT_FACE: &Face = &faces::REGULAR_12_ASCII;

const TITLE: &str = "BATTERY LAB";
const WAITING: &str = "waiting for service";

/// A fixed buffer for one line; anything past its end is dropped.
///
/// Built without `core::fmt`, whose integer formatting reads garbage on the
/// watch (Slumber's `Line` says why).
struct Line {
    buf: [u8; 40],
    len: usize,
}

impl Line {
    fn new() -> Self {
        Line { buf: [0; 40], len: 0 }
    }

    fn push(&mut self, s: &str) -> &mut Self {
        for &b in s.as_bytes() {
            if self.len == self.buf.len() {
                break;
            }
            self.buf[self.len] = if b.is_ascii() { b } else { b'?' };
            self.len += 1;
        }
        self
    }

    fn num(&mut self, value: u32, min_digits: usize) -> &mut Self {
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

    /// A value held in thousandths, shown to `places` decimals (truncated); `--` for `NONE`.
    fn milli(&mut self, value: i32, places: u32) -> &mut Self {
        if value == NONE {
            return self.push("--");
        }
        if value < 0 {
            self.push("-");
        }
        let v = value.unsigned_abs();
        self.num(v / 1000, 1);
        if places > 0 {
            let frac = (v % 1000) / 10u32.pow(3 - places.min(3));
            self.push(".").num(frac, places.min(3) as usize);
        }
        self
    }

    fn int(&mut self, value: i32) -> &mut Self {
        if value == NONE {
            return self.push("--");
        }
        if value < 0 {
            self.push("-");
        }
        self.num(value.unsigned_abs(), 1)
    }

    // Not `from_utf8`, which also reaches a table through MOVW/MOVT on entry.
    fn as_str(&self) -> &str {
        // SAFETY: `push` and `num` write only ASCII bytes.
        unsafe { core::str::from_utf8_unchecked(&self.buf[..self.len]) }
    }
}

fn text(fb: &mut Surface<Abgr2222>, face: &Face, s: &str, baseline: i32, ink: Abgr2222) {
    face.draw(fb, s, PANEL_CX, baseline, Style::centered(ink, GROUND)).ok();
}

/// Each line's text, face, baseline and ink, top to bottom.
fn lines(state: &State) -> [(Line, &'static Face, i32, Abgr2222); 7] {
    let mut pct = Line::new();
    pct.milli(state.pct_x1000, 1).push(" %");

    let mut left = Line::new();
    left.push("left ").milli(state.mah_x1000, 3).push(" mAh");

    let mut design = Line::new();
    design.push("design ").milli(state.design_mah_x1000, 3).push(" mAh");

    let mut volts = Line::new();
    volts.int(state.mv).push(" mV");

    let mut amps = Line::new();
    amps.milli(state.ma_x1000, 3).push(" / ").milli(state.avg_ma_x1000, 3).push(" mA");

    let mut usb = Line::new();
    usb.push("usb ").int(state.connected).push("  charging ").int(state.charging);

    let mut foot = Line::new();
    let foot_ink = if state.have_status == 0 {
        foot.push(WAITING);
        WARN_INK
    } else if state.write_fails != 0 {
        foot.push("WRITE FAILS ").num(state.write_fails, 1);
        WARN_INK
    } else {
        foot.push("rows ").num(state.lines, 1).push("  up ").num(state.service_up_min / 60, 1).push("h");
        LABEL_INK
    };

    [
        (pct, BIG_FACE, 78, BODY_INK),
        (left, BODY_FACE, 104, BODY_INK),
        (design, BODY_FACE, 124, BODY_INK),
        (volts, BODY_FACE, 144, BODY_INK),
        (amps, BODY_FACE, 164, BODY_INK),
        (usb, BODY_FACE, 184, BODY_INK),
        (foot, FOOT_FACE, 204, foot_ink),
    ]
}

fn draw(fb: &mut Surface<Abgr2222>, state: &State) {
    text(fb, TITLE_FACE, TITLE, 44, TITLE_INK);
    for (line, face, baseline, ink) in lines(state) {
        text(fb, face, line.as_str(), baseline, ink);
    }
}

pub fn render(buf: &mut [u8], width: u32, height: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let Some(mut fb) = Surface::<Abgr2222>::round(buf, width, height) else {
        return;
    };
    fb.clear(GROUND);
    draw(&mut fb, state);
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `batterylab_abi::fingerprint()`
/// in batterylab_gui.h.
const fn abi_fingerprint() -> u32 {
    use core::mem::offset_of;
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, offset_of!(State, pct_x1000));
    let h = fnv1a(h, offset_of!(State, mv));
    let h = fnv1a(h, offset_of!(State, ma_x1000));
    let h = fnv1a(h, offset_of!(State, avg_ma_x1000));
    let h = fnv1a(h, offset_of!(State, mah_x1000));
    let h = fnv1a(h, offset_of!(State, design_mah_x1000));
    let h = fnv1a(h, offset_of!(State, connected));
    let h = fnv1a(h, offset_of!(State, charging));
    let h = fnv1a(h, offset_of!(State, lines));
    let h = fnv1a(h, offset_of!(State, write_fails));
    let h = fnv1a(h, offset_of!(State, service_up_min));
    let h = fnv1a(h, offset_of!(State, have_status));
    fnv1a(h, offset_of!(State, _pad))
}

#[no_mangle]
pub extern "C" fn batterylab_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<State>() == 48);
const _: () = assert!(core::mem::align_of::<State>() == 4);
const _: () = assert!(core::mem::offset_of!(State, lines) == 32);
const _: () = assert!(core::mem::offset_of!(State, have_status) == 44);

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `batterylab_state`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn batterylab_render(
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
            pct_x1000: 99_900,
            mv: 4379,
            ma_x1000: -1_400,
            avg_ma_x1000: -1_512,
            mah_x1000: 204_000,
            design_mah_x1000: 148_000,
            connected: 0,
            charging: 0,
            lines: 17_280,
            write_fails: 0,
            service_up_min: 23 * 60,
            have_status: 1,
            _pad: [0; 3],
        }
    }

    /// Every state that changes a line's text or ink, at its widest.
    fn variants() -> Vec<State> {
        vec![
            State::default(),
            reported(),
            State { write_fails: u32::MAX, ..reported() },
            State {
                pct_x1000: 100_000,
                mv: -2_147_483_647,
                ma_x1000: -2_147_483_647,
                avg_ma_x1000: -2_147_483_647,
                mah_x1000: -2_147_483_647,
                design_mah_x1000: -2_147_483_647,
                connected: -2_147_483_647,
                charging: -2_147_483_647,
                lines: u32::MAX,
                service_up_min: u32::MAX,
                ..reported()
            },
        ]
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
        for st in variants() {
            for (line, face, _, _) in lines(&st) {
                assert!(face.covers(line.as_str()), "{:?}", line.as_str());
            }
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
    fn values_read_as_the_service_scaled_them() {
        let l = lines(&reported());
        assert_eq!(l[0].0.as_str(), "99.9 %");
        assert_eq!(l[1].0.as_str(), "left 204.000 mAh");
        assert_eq!(l[2].0.as_str(), "design 148.000 mAh");
        assert_eq!(l[3].0.as_str(), "4379 mV");
        assert_eq!(l[4].0.as_str(), "-1.400 / -1.512 mA");
        assert_eq!(l[5].0.as_str(), "usb 0  charging 0");
        assert_eq!(l[6].0.as_str(), "rows 17280  up 23h");
    }

    #[test]
    fn a_field_no_message_carried_reads_as_dashes() {
        let l = lines(&State { have_status: 1, ..State::default() });
        assert_eq!(l[1].0.as_str(), "left -- mAh");
        assert_eq!(l[4].0.as_str(), "-- / -- mA");
    }

    #[test]
    fn fractions_keep_their_leading_zeros() {
        let mut l = Line::new();
        l.milli(-5, 3);
        assert_eq!(l.as_str(), "-0.005");
        let mut l = Line::new();
        l.milli(100_050, 1);
        assert_eq!(l.as_str(), "100.0");
    }

    #[test]
    fn no_line_is_cut_short_by_its_buffer() {
        for st in variants() {
            for (line, _, _, _) in lines(&st) {
                assert!(line.len < line.buf.len(), "{:?} filled its buffer", line.as_str());
            }
        }
    }
}
