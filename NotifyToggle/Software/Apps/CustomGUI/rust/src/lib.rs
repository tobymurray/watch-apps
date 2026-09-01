#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(not(feature = "std"))]
use core::fmt::Write as _;

use embedded_graphics::{
    mono_font::{
        ascii::{FONT_6X10, FONT_9X15_BOLD},
        MonoTextStyle,
    },
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, CornerRadii, PrimitiveStyle, PrimitiveStyleBuilder, Rectangle, RoundedRectangle},
    text::{Alignment, Text},
};

#[cfg(not(feature = "std"))]
extern "C" {
    fn notify_toggle_host_panic(msg: *const u8, len: u32);
}

#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    let mut msg = Buf::<192>::new();
    if let Some(loc) = info.location() {
        let _ = write!(msg, "{}:{}: ", loc.file(), loc.line());
    }
    let _ = write!(msg, "{}", info.message());

    let s = msg.as_str();
    unsafe { notify_toggle_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// Mirrors `notify_toggle_state` (`notify_toggle_gui.h`) field for field.
/// This is a read-only view of the real watch-wide notifications flag, not
/// app state of its own -- `known` says whether `enabled` is actually
/// trustworthy (see the C header for why that can be false).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    pub enabled: u8,
    pub known: u8,
    pub _pad: [u8; 2],
}

impl State {
    fn is_enabled(&self) -> bool {
        self.enabled != 0
    }

    fn is_known(&self) -> bool {
        self.known != 0
    }
}

const ALPHA_SHIFT: u8 = 6;
const BLUE_SHIFT: u8 = 4;
const GREEN_SHIFT: u8 = 2;
const RED_SHIFT: u8 = 0;
const CHANNEL_MASK: u8 = 0b11;
const CHANNEL_BITS: u8 = 2;
const ALPHA_OPAQUE: u8 = 0b11;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Abgr2222(pub u8);

const fn keep_high_bits(channel: u8) -> u8 {
    (channel >> (8 - CHANNEL_BITS)) & CHANNEL_MASK
}

impl Abgr2222 {
    pub const fn from_levels(r2: u8, g2: u8, b2: u8) -> Self {
        Abgr2222(
            (ALPHA_OPAQUE << ALPHA_SHIFT)
                | ((b2 & CHANNEL_MASK) << BLUE_SHIFT)
                | ((g2 & CHANNEL_MASK) << GREEN_SHIFT)
                | ((r2 & CHANNEL_MASK) << RED_SHIFT),
        )
    }

    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Abgr2222::from_levels(keep_high_bits(r), keep_high_bits(g), keep_high_bits(b))
    }

    pub const BLACK: Abgr2222 = Abgr2222::rgb(0, 0, 0);
    pub const WHITE: Abgr2222 = Abgr2222::rgb(255, 255, 255);
    pub const GREEN: Abgr2222 = Abgr2222::rgb(0, 255, 0);
    pub const GRAY: Abgr2222 = Abgr2222::rgb(128, 128, 128);
    pub const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
}

impl Default for Abgr2222 {
    fn default() -> Self {
        Abgr2222::BLACK
    }
}

impl PixelColor for Abgr2222 {
    type Raw = RawU8;
}

// Every label on this screen is bright text on the black ground: RustGuiPoc's
// hardware runs found dark thin glyphs on a light fill drop out on this panel
// (Docs/FINDINGS.md), so light-on-dark is a rule here, not a preference.
const GROUND: Abgr2222 = Abgr2222::BLACK;
const HEADING: Abgr2222 = Abgr2222::WHITE;
const CHROME: Abgr2222 = Abgr2222::GRAY;
const ON_ACCENT: Abgr2222 = Abgr2222::GREEN;
const OFF_ACCENT: Abgr2222 = Abgr2222::GRAY;
const UNKNOWN_ACCENT: Abgr2222 = Abgr2222::AMBER;
const KNOB: Abgr2222 = Abgr2222::WHITE;

struct FrameBuf<'a> {
    buf: &'a mut [u8],
    w: u32,
    h: u32,
}

impl OriginDimensions for FrameBuf<'_> {
    fn size(&self) -> Size {
        Size::new(self.w, self.h)
    }
}

impl DrawTarget for FrameBuf<'_> {
    type Color = Abgr2222;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        let (w, h) = (self.w as i32, self.h as i32);
        for Pixel(coord, color) in pixels {
            if coord.x >= 0 && coord.y >= 0 && coord.x < w && coord.y < h {
                let idx = (coord.y as u32 * self.w + coord.x as u32) as usize;
                self.buf[idx] = color.0;
            }
        }
        Ok(())
    }
}

#[cfg(not(feature = "std"))]
struct Buf<const N: usize> {
    b: [u8; N],
    n: usize,
}

#[cfg(not(feature = "std"))]
impl<const N: usize> Buf<N> {
    fn new() -> Self {
        Buf { b: [0; N], n: 0 }
    }

    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.b[..self.n]).unwrap_or("")
    }
}

#[cfg(not(feature = "std"))]
impl<const N: usize> core::fmt::Write for Buf<N> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &c in s.as_bytes() {
            if self.n >= N {
                return Err(core::fmt::Error);
            }
            self.b[self.n] = c;
            self.n += 1;
        }
        Ok(())
    }
}

// Layout: one screen, nothing to configure, so every position is a literal
// rather than something computed from panel geometry -- this app only ever
// draws on the 240x240 round panel every UNA Watch has.
const PANEL_CX: i32 = 120;
const TITLE_BASELINE_Y: i32 = 46;

const PILL_W: i32 = 140;
const PILL_H: i32 = 64;
const PILL_X: i32 = PANEL_CX - PILL_W / 2;
const PILL_Y: i32 = 98;
const PILL_RADIUS: i32 = PILL_H / 2;
const PILL_STROKE_W: u32 = 3;

const KNOB_RADIUS: i32 = 24;
const KNOB_INSET: i32 = 8;
const KNOB_CY: i32 = PILL_Y + PILL_H / 2;
const KNOB_OFF_CX: i32 = PILL_X + KNOB_INSET + KNOB_RADIUS;
const KNOB_ON_CX: i32 = PILL_X + PILL_W - KNOB_INSET - KNOB_RADIUS;
// Centred: neither ON's nor OFF's position, so an unknown reading never
// looks like a confident answer that happens to be wrong.
const KNOB_UNKNOWN_CX: i32 = PANEL_CX;

const LABEL_BASELINE_Y: i32 = PILL_Y + PILL_H + 32;
const FOOTER_BASELINE_Y: i32 = 222;

fn draw_circle(fb: &mut FrameBuf, cx: i32, cy: i32, radius: i32, style: PrimitiveStyle<Abgr2222>) {
    Circle::new(Point::new(cx - radius, cy - radius), (radius as u32) * 2)
        .into_styled(style)
        .draw(fb)
        .ok();
}

fn text(fb: &mut FrameBuf, s: &str, at: Point, color: Abgr2222, align: Alignment) {
    Text::with_alignment(s, at, MonoTextStyle::new(&FONT_9X15_BOLD, color), align)
        .draw(fb)
        .ok();
}

fn small_text(fb: &mut FrameBuf, s: &str, at: Point, color: Abgr2222, align: Alignment) {
    Text::with_alignment(s, at, MonoTextStyle::new(&FONT_6X10, color), align)
        .draw(fb)
        .ok();
}

fn draw_toggle(fb: &mut FrameBuf, state: &State) {
    let (accent, knob_cx) = if !state.is_known() {
        (UNKNOWN_ACCENT, KNOB_UNKNOWN_CX)
    } else if state.is_enabled() {
        (ON_ACCENT, KNOB_ON_CX)
    } else {
        (OFF_ACCENT, KNOB_OFF_CX)
    };

    let pill = RoundedRectangle::new(
        Rectangle::new(Point::new(PILL_X, PILL_Y), Size::new(PILL_W as u32, PILL_H as u32)),
        CornerRadii::new(Size::new(PILL_RADIUS as u32, PILL_RADIUS as u32)),
    );
    let pill_style = PrimitiveStyleBuilder::new()
        .fill_color(accent)
        .stroke_color(HEADING)
        .stroke_width(PILL_STROKE_W)
        .build();
    pill.into_styled(pill_style).draw(fb).ok();

    draw_circle(fb, knob_cx, KNOB_CY, KNOB_RADIUS, PrimitiveStyle::with_fill(KNOB));
}

fn draw(fb: &mut FrameBuf, state: &State) {
    text(fb, "NOTIFICATIONS", Point::new(PANEL_CX, TITLE_BASELINE_Y), HEADING, Alignment::Center);

    draw_toggle(fb, state);

    let (label, label_color) = if !state.is_known() {
        ("?", UNKNOWN_ACCENT)
    } else if state.is_enabled() {
        ("ON", ON_ACCENT)
    } else {
        ("OFF", OFF_ACCENT)
    };
    text(fb, label, Point::new(PANEL_CX, LABEL_BASELINE_Y), label_color, Alignment::Center);

    // R1 always attempts a fresh read-and-toggle (Gui.cpp re-reads the real
    // file before deciding what to write), so the hint stays the same even
    // from the unknown state -- it is "try again", not "disabled".
    small_text(
        fb,
        "R1 TOGGLE  R2 BACK",
        Point::new(PANEL_CX, FOOTER_BASELINE_Y),
        CHROME,
        Alignment::Center,
    );
}

pub fn render(buf: &mut [u8], width: u32, height: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height };
    fb.buf.fill(GROUND.0);
    draw(&mut fb, state);
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `notify_toggle_abi::fingerprint()`
/// in notify_toggle_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, core::mem::offset_of!(State, enabled));
    let h = fnv1a(h, core::mem::offset_of!(State, known));
    fnv1a(h, core::mem::offset_of!(State, _pad))
}

/// Lets the caller confirm it was linked against the archive it thinks it was.
/// The compile-time assertions below cannot do this: a stale archive and a newer
/// header each satisfy their own, having been compiled at different times.
#[no_mangle]
pub extern "C" fn notify_toggle_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

// Per field, because a size check passes when two fields are swapped.
// notify_toggle_gui.h asserts the same offsets, so a hand edit to either
// declaration breaks a build.
const _: () = assert!(core::mem::size_of::<State>() == 4);
const _: () = assert!(core::mem::align_of::<State>() == 1);
const _: () = assert!(core::mem::offset_of!(State, enabled) == 0);
const _: () = assert!(core::mem::offset_of!(State, known) == 1);
const _: () = assert!(core::mem::offset_of!(State, _pad) == 2);

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `notify_toggle_state`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn notify_toggle_render(
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
    const C_STRUCT_SIZE: usize = 4;
    const C_STRUCT_ALIGN: usize = 1;

    fn on() -> State {
        State { enabled: 1, known: 1, _pad: [0; 2] }
    }

    fn off() -> State {
        State { enabled: 0, known: 1, _pad: [0; 2] }
    }

    fn unknown() -> State {
        State { enabled: 0, known: 0, _pad: [0; 2] }
    }

    #[test]
    fn state_layout_matches_c() {
        assert_eq!(core::mem::size_of::<State>(), C_STRUCT_SIZE);
        assert_eq!(core::mem::align_of::<State>(), C_STRUCT_ALIGN);
    }

    #[test]
    fn abi_fingerprint_is_stable() {
        // Pinned so a change to either fingerprint implementation shows up
        // here rather than as a refusal to start on a watch.
        assert_eq!(notify_toggle_abi_fingerprint(), abi_fingerprint());
    }

    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0xAAu8; (W * H) as usize - 1];
        render(&mut buf, W, H, &on());
        assert!(buf.iter().all(|&b| b == 0xAA));
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        let mut buf = vec![0xAAu8; n + 64];
        render(&mut buf, W, H, &on());
        assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran the framebuffer");
    }

    #[test]
    fn render_is_deterministic() {
        let n = (W * H) as usize;
        let mut a = vec![0u8; n];
        let mut b = vec![0u8; n];
        render(&mut a, W, H, &on());
        render(&mut b, W, H, &on());
        assert_eq!(a, b);
    }

    /// The one behaviour this whole app exists to have: flipping `enabled`
    /// visibly moves the knob. Sampled at the two knob centres rather than
    /// diffed wholesale, so the test states what must differ instead of just
    /// asserting "something changed".
    #[test]
    fn knob_moves_side_with_state() {
        let n = (W * H) as usize;
        let mut buf_on = vec![0u8; n];
        let mut buf_off = vec![0u8; n];
        render(&mut buf_on, W, H, &on());
        render(&mut buf_off, W, H, &off());

        let px = |buf: &[u8], x: i32, y: i32| buf[(y as u32 * W + x as u32) as usize];

        assert_eq!(px(&buf_on, KNOB_ON_CX, KNOB_CY), KNOB.0, "ON: knob should be on the right");
        assert_ne!(px(&buf_on, KNOB_OFF_CX, KNOB_CY), KNOB.0, "ON: left side should not be the knob");

        assert_eq!(px(&buf_off, KNOB_OFF_CX, KNOB_CY), KNOB.0, "OFF: knob should be on the left");
        assert_ne!(px(&buf_off, KNOB_ON_CX, KNOB_CY), KNOB.0, "OFF: right side should not be the knob");
    }

    #[test]
    fn pill_fill_reflects_state() {
        let n = (W * H) as usize;
        let mut buf_on = vec![0u8; n];
        let mut buf_off = vec![0u8; n];
        render(&mut buf_on, W, H, &on());
        render(&mut buf_off, W, H, &off());

        // Sampled just inside the pill's top edge, away from the knob, so this
        // reads the fill colour rather than the knob or the stroke.
        let sample_x = PANEL_CX;
        let sample_y = PILL_Y + 6;
        let idx = (sample_y as u32 * W + sample_x as u32) as usize;

        assert_eq!(buf_on[idx], ON_ACCENT.0);
        assert_eq!(buf_off[idx], OFF_ACCENT.0);
    }

    /// `known == 0` must not be drawn as either a confident ON or a confident
    /// OFF: distinct fill colour, and the knob sits centred rather than at
    /// either side's position.
    #[test]
    fn unknown_state_is_visually_distinct_from_on_and_off() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        render(&mut buf, W, H, &unknown());

        let px = |x: i32, y: i32| buf[(y as u32 * W + x as u32) as usize];

        assert_eq!(px(KNOB_UNKNOWN_CX, KNOB_CY), KNOB.0, "knob should be centred");
        assert_ne!(px(KNOB_ON_CX, KNOB_CY), KNOB.0, "not drawn at the ON position");
        assert_ne!(px(KNOB_OFF_CX, KNOB_CY), KNOB.0, "not drawn at the OFF position");

        let fill = px(PANEL_CX, PILL_Y + 6);
        assert_eq!(fill, UNKNOWN_ACCENT.0);
        assert_ne!(fill, ON_ACCENT.0);
        assert_ne!(fill, OFF_ACCENT.0);
    }
}
