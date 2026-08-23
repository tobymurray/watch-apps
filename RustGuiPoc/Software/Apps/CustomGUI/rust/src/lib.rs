//! no_std Rust frontend core for the UNA Watch CustomGUI PoC.
//!
//! This crate owns *rendering only*. It draws with `embedded-graphics` into a
//! caller-provided 8bpp **ABGR2222** framebuffer and hands nothing back but
//! pixels. All watch plumbing — querying the display, pushing the buffer over
//! the kernel message bus, input, lifecycle, and reading the sensor — lives
//! outside: in the C++ shim (`Gui.cpp`) and the Service half.
//!
//! [`render`] is a **pure function of `(buffer, geometry, screen, state)`**.
//! That is the point of the design, not a stylistic preference: the device, the
//! host simulator and the tests all drive this one renderer through the one
//! [`State`] struct, so none of them can drift from the others and none has a
//! private path to the truth.
//!
//! The layout is **round-display aware**: the physical panel shows only a
//! circular region of the rectangular framebuffer, so all content is kept inside
//! the inscribed square (the largest axis-aligned box that fits in the circle).
#![cfg_attr(not(feature = "std"), no_std)]

use core::fmt::Write as _;

use embedded_graphics::{
    mono_font::{ascii::FONT_9X15_BOLD, MonoTextStyle},
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle},
    text::{Alignment, Text},
};

// -----------------------------------------------------------------------------
// Panic handler (device/no_std only; the host sim and tests use std's)
// -----------------------------------------------------------------------------
// NOTE: `loop {}` freezes the GUI thread silently, which is the wrong behaviour
// for anything but a PoC — a real app should route this to the SDK logger and
// then exit. Left deliberately minimal, and deliberately flagged.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

// -----------------------------------------------------------------------------
// State — everything the UI is allowed to know
// -----------------------------------------------------------------------------

/// Mirror of `poc_gui_state` in `poc_gui.h`. Field order and types must match.
///
/// The renderer never decides whether data is trustworthy; `valid` is computed
/// by the shim, which owns the clock. This crate's job is to *respect* it.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    /// Acceleration in g. Meaningless unless `valid != 0`.
    pub accel_x: f32,
    pub accel_y: f32,
    pub accel_z: f32,
    /// Milliseconds since the newest sample arrived.
    pub sample_age_ms: u32,
    /// Samples received this session.
    pub samples: u32,
    /// Frames rendered this session.
    pub frames: u32,
    /// 0 = never sampled, or too stale to display.
    pub valid: u8,
    pub _pad: [u8; 3],
}

impl State {
    fn is_live(&self) -> bool {
        self.valid != 0
    }
}

// -----------------------------------------------------------------------------
// ABGR2222 — the watch's 8-bits-per-pixel packed color
// -----------------------------------------------------------------------------
// One byte per pixel. From MSB: A[7:6] B[5:4] G[3:2] R[1:0], 2 bits per channel.
// NOTE: RequestDisplayConfig reports colorDepth=6 — that is the count of
// *displayed color* bits (3 channels x 2), NOT the storage width. Storage is a
// full 8bpp byte; the top 2 (alpha) bits must be 0b11 (opaque), which rgb() OR's
// in unconditionally. Do not "shrink" this to 6bpp.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Abgr2222(pub u8);

impl Abgr2222 {
    /// Pack 8-bit RGB into ABGR2222, alpha forced opaque (3).
    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        let a2 = 0b11u8;
        let r2 = (r >> 6) & 0b11;
        let g2 = (g >> 6) & 0b11;
        let b2 = (b >> 6) & 0b11;
        Abgr2222((a2 << 6) | (b2 << 4) | (g2 << 2) | r2)
    }

    pub const BLACK: Abgr2222 = Abgr2222::rgb(0, 0, 0);
    pub const WHITE: Abgr2222 = Abgr2222::rgb(255, 255, 255);
    pub const CYAN: Abgr2222 = Abgr2222::rgb(0, 255, 255);
    pub const YELLOW: Abgr2222 = Abgr2222::rgb(255, 255, 0);
    pub const GRAY: Abgr2222 = Abgr2222::rgb(128, 128, 128);
}

impl Default for Abgr2222 {
    fn default() -> Self {
        Abgr2222::BLACK
    }
}

impl PixelColor for Abgr2222 {
    type Raw = RawU8;
}

// -----------------------------------------------------------------------------
// Framebuffer as an embedded-graphics DrawTarget
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Fixed-capacity string formatting (no alloc)
// -----------------------------------------------------------------------------
/// A tiny `core::fmt::Write` sink over a stack array, so labels can be built with
/// `write!` without an allocator. Values are formatted as integers on purpose:
/// integer formatting is a fraction of the code size of float formatting, and
/// milli-g is the right resolution for a 240px panel anyway.
struct Buf<const N: usize> {
    b: [u8; N],
    n: usize,
}

impl<const N: usize> Buf<N> {
    fn new() -> Self {
        Buf { b: [0; N], n: 0 }
    }
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.b[..self.n]).unwrap_or("")
    }
}

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

/// g -> milli-g, saturating. Keeps all display maths in integers.
fn to_mg(g: f32) -> i32 {
    let v = g * 1000.0;
    if v > 9999.0 {
        9999
    } else if v < -9999.0 {
        -9999
    } else {
        v as i32
    }
}

// -----------------------------------------------------------------------------
// Round-display geometry
// -----------------------------------------------------------------------------
/// The circular panel's center, radius, and the half-side of the inscribed
/// square (the largest box guaranteed fully visible). Everything important is
/// laid out within `cx±half, cy±half`.
struct Geom {
    cx: i32,
    cy: i32,
    r: i32,
    half: i32,
}

fn geom(fb: &FrameBuf) -> Geom {
    let w = fb.w as i32;
    let h = fb.h as i32;
    let r = w.min(h) / 2;
    Geom {
        cx: w / 2,
        cy: h / 2,
        r,
        half: r * 181 / 256, // r / sqrt(2) ~= 0.707 r
    }
}

/// Faint rim so the round edge / clipped region is visible on-device.
fn draw_rim(fb: &mut FrameBuf, g: &Geom) {
    Circle::new(Point::new(g.cx - g.r + 1, g.cy - g.r + 1), (g.r as u32 - 1) * 2)
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::GRAY, 1))
        .draw(fb)
        .ok();
}

// Panel legibility rule (verified on-device): BRIGHT text on the DARK background
// renders crisply, but DARK thin glyphs on a LIGHT fill drop out. Every label
// here is therefore bright-on-black.
fn text(fb: &mut FrameBuf, s: &str, at: Point, color: Abgr2222, align: Alignment) {
    Text::with_alignment(s, at, MonoTextStyle::new(&FONT_9X15_BOLD, color), align)
        .draw(fb)
        .ok();
}

fn title(fb: &mut FrameBuf, g: &Geom, s: &str) {
    text(fb, s, Point::new(g.cx, g.cy - g.half + 16), Abgr2222::CYAN, Alignment::Center);
}

/// Which screen you are on, as dots rather than a line of text. The button
/// mapping lives in the README: a watch UI that has to print its own key
/// bindings on every screen is spending its scarcest resource — vertical space
/// inside a circle — on documentation.
fn page_dots(fb: &mut FrameBuf, g: &Geom, current: u32) {
    const D: i32 = 6;
    const GAP: i32 = 14;
    let n = SCREEN_COUNT as i32;
    let y = g.cy + g.half - 7;
    let x0 = g.cx - ((n - 1) * GAP) / 2;
    for i in 0..n {
        let c = Circle::new(Point::new(x0 + i * GAP - D / 2, y - D / 2), D as u32);
        if i == current as i32 % n {
            c.into_styled(PrimitiveStyle::with_fill(Abgr2222::WHITE)).draw(fb).ok();
        } else {
            c.into_styled(PrimitiveStyle::with_stroke(Abgr2222::GRAY, 1)).draw(fb).ok();
        }
    }
}

// -----------------------------------------------------------------------------
// Screen 0 — live accelerometer
// -----------------------------------------------------------------------------
// A bubble-level: the dot's offset from centre is the wrist's tilt. Chosen over
// a numeric-only readout because it is *self-evidently live* — tilt the watch and
// the dot moves, which demonstrates the whole sensor -> Service -> IPC -> Rust ->
// panel path in one gesture, with no strap, no fix and no warm-up.
fn draw_accel(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    draw_rim(fb, &g);
    title(fb, &g, "ACCEL mg");

    let (bx, by, br) = (g.cx, g.cy - 16, 40i32);

    // Reference ring + crosshair: drawn whether or not there is data, so a stale
    // screen reads as "no reading" rather than "app broken".
    Circle::new(Point::new(bx - br, by - br), (br as u32) * 2)
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::GRAY, 1))
        .draw(fb)
        .ok();
    Line::new(Point::new(bx - 4, by), Point::new(bx + 4, by))
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::GRAY, 1))
        .draw(fb)
        .ok();
    Line::new(Point::new(bx, by - 4), Point::new(bx, by + 4))
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::GRAY, 1))
        .draw(fb)
        .ok();

    if !st.is_live() {
        text(fb, "NO DATA", Point::new(g.cx, g.cy + 52), Abgr2222::YELLOW, Alignment::Center);
        page_dots(fb, &g, 0);
        return;
    }

    // Bubble: 1 g of tilt on an axis = the full ring radius. Screen y is inverted
    // relative to the accelerometer's y, so it is negated for a natural feel.
    let off = |acc: f32| -> i32 {
        let v = acc * br as f32;
        let lim = br as f32;
        let c = if v > lim {
            lim
        } else if v < -lim {
            -lim
        } else {
            v
        };
        c as i32
    };
    let dx = off(st.accel_x);
    let dy = -off(st.accel_y);
    Circle::new(Point::new(bx + dx - 6, by + dy - 6), 12)
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::YELLOW))
        .draw(fb)
        .ok();

    let mut l1 = Buf::<24>::new();
    let _ = write!(l1, "X{:>5} Y{:>5}", to_mg(st.accel_x), to_mg(st.accel_y));
    text(fb, l1.as_str(), Point::new(g.cx, g.cy + 48), Abgr2222::WHITE, Alignment::Center);

    let mut l2 = Buf::<24>::new();
    let _ = write!(l2, "Z{:>5}", to_mg(st.accel_z));
    text(fb, l2.as_str(), Point::new(g.cx, g.cy + 66), Abgr2222::WHITE, Alignment::Center);

    page_dots(fb, &g, 0);
}

// -----------------------------------------------------------------------------
// Screen 1 — diagnostics
// -----------------------------------------------------------------------------
// Real numbers about the render loop and the data path, which is what you
// actually want while bringing up a rendering stack — and unlike a shapes demo,
// nothing here is invented.
fn draw_diag(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    draw_rim(fb, &g);
    title(fb, &g, "DIAG");

    let x = g.cx - g.half + 6;
    let mut y = g.cy - 26;

    let row = |fb: &mut FrameBuf, s: &str, yy: i32| {
        text(fb, s, Point::new(x, yy), Abgr2222::WHITE, Alignment::Left);
    };

    let mut b = Buf::<24>::new();
    let _ = write!(b, "FRAMES{:>7}", st.frames);
    row(fb, b.as_str(), y);
    y += 18;

    let mut b = Buf::<24>::new();
    let _ = write!(b, "SAMPLE{:>7}", st.samples);
    row(fb, b.as_str(), y);
    y += 18;

    let mut b = Buf::<24>::new();
    let _ = write!(b, "AGE{:>8}ms", st.sample_age_ms);
    row(fb, b.as_str(), y);
    y += 18;

    let state = if st.is_live() {
        "LIVE"
    } else if st.samples > 0 {
        "STALE"
    } else {
        "NONE"
    };
    let mut b = Buf::<24>::new();
    let _ = write!(b, "STATE{:>8}", state);
    row(fb, b.as_str(), y);

    page_dots(fb, &g, 1);
}

// -----------------------------------------------------------------------------
// Rendering entry point — the single source of truth for the device (via the
// C ABI below), the host simulator, and the tests.
// -----------------------------------------------------------------------------
const SCREEN_COUNT: u32 = 2;

/// Number of selectable screens.
pub const fn screen_count() -> u32 {
    SCREEN_COUNT
}

/// Render one frame into `buf`, an 8bpp ABGR2222 framebuffer of at least
/// `width * height` bytes. Does nothing at all if the buffer is too small for
/// the stated geometry — the caller cannot make this function overrun.
pub fn render(buf: &mut [u8], width: u32, height: u32, screen: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height };
    fb.buf.fill(Abgr2222::BLACK.0);
    match screen % SCREEN_COUNT {
        0 => draw_accel(&mut fb, state),
        _ => draw_diag(&mut fb, state),
    }
}

// -----------------------------------------------------------------------------
// C ABI — the seam the C++ shim (Gui.cpp) calls on-device
// -----------------------------------------------------------------------------

/// C ABI wrapper for [`screen_count`].
#[no_mangle]
pub extern "C" fn poc_gui_screen_count() -> u32 {
    screen_count()
}

/// C ABI wrapper for [`render`].
///
/// `buf_len` is passed explicitly rather than inferred from `width * height`, so
/// the size invariant is enforced where the writing happens instead of being a
/// promise the caller makes and the callee trusts.
///
/// # Safety
/// `buf` must point to at least `buf_len` writable bytes, and `state` to a valid
/// `poc_gui_state`; both must stay valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn poc_gui_render(
    buf: *mut u8,
    buf_len: u32,
    width: u16,
    height: u16,
    screen: u32,
    state: *const State,
) {
    if buf.is_null() || state.is_null() || buf_len == 0 || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, buf_len as usize);
    render(slice, width as u32, height as u32, screen, &*state);
}

// -----------------------------------------------------------------------------
// Tests (host: `cargo test --features std`)
// -----------------------------------------------------------------------------
#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;

    fn live() -> State {
        State { accel_x: 0.25, accel_y: -0.5, accel_z: 0.9, sample_age_ms: 40,
                samples: 123, frames: 456, valid: 1, _pad: [0; 3] }
    }

    /// The C struct is 28 bytes; a mismatch here is a silently corrupt ABI.
    #[test]
    fn state_layout_matches_c() {
        assert_eq!(core::mem::size_of::<State>(), 28);
        assert_eq!(core::mem::align_of::<State>(), 4);
    }

    /// A buffer too small for the stated geometry must be left untouched, not
    /// partially painted and not overrun.
    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0xAAu8; (W * H) as usize - 1];
        render(&mut buf, W, H, 0, &live());
        assert!(buf.iter().all(|&b| b == 0xAA));
    }

    /// Every screen must stay inside width*height even when handed a longer
    /// buffer — the device passes the whole 240x240 array regardless of config.
    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        for screen in 0..screen_count() {
            let mut buf = vec![0xAAu8; n + 64];
            render(&mut buf, W, H, screen, &live());
            assert!(buf[n..].iter().all(|&b| b == 0xAA), "screen {screen} overran");
        }
    }

    /// Stale data must not render as live data. This is the behaviour the whole
    /// valid/age plumbing exists to produce, so it is worth pinning.
    #[test]
    fn stale_state_renders_differently() {
        let n = (W * H) as usize;
        let mut a = vec![0u8; n];
        let mut b = vec![0u8; n];
        render(&mut a, W, H, 0, &live());
        render(&mut b, W, H, 0, &State { valid: 0, ..live() });
        assert_ne!(a, b);
    }

    /// Sensor values are untrusted input: a driver glitch can hand us NaN or a
    /// wild magnitude. Neither may escape the framebuffer or panic the GUI
    /// thread, whose panic handler is an infinite loop.
    #[test]
    fn hostile_sensor_values_stay_in_bounds() {
        let n = (W * H) as usize;
        let nasty = [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1.0e30, -1.0e30, 0.0];
        for &v in &nasty {
            for screen in 0..screen_count() {
                let mut buf = vec![0xAAu8; n + 64];
                let st = State { accel_x: v, accel_y: v, accel_z: v, ..live() };
                render(&mut buf, W, H, screen, &st);
                assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran on {v} screen {screen}");
            }
        }
    }

    /// Same state in, same pixels out — the property the simulator's fidelity
    /// claim rests on.
    #[test]
    fn render_is_deterministic() {
        let n = (W * H) as usize;
        let mut a = vec![0u8; n];
        let mut b = vec![0u8; n];
        render(&mut a, W, H, 0, &live());
        render(&mut b, W, H, 0, &live());
        assert_eq!(a, b);
    }
}
