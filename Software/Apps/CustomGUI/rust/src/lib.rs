//! no_std Rust frontend core for the UNA Watch CustomGUI PoC.
//!
//! This crate owns *rendering only*. It draws with `embedded-graphics` into a
//! caller-provided 8bpp **ABGR2222** framebuffer and hands nothing back but
//! pixels. All watch plumbing — querying the display, pushing the buffer over
//! the kernel message bus, input, lifecycle — lives in the C++ shim (`Gui.cpp`),
//! which calls the `extern "C"` entry points at the bottom of this file.
//!
//! The layout is **round-display aware**: the physical panel shows only a
//! circular region of the rectangular framebuffer, so all content is kept inside
//! the inscribed square (the largest axis-aligned box that fits in the circle).
//! A faint rim ring is drawn at the display edge to make the safe area visible.
//!
//! The host simulator (`src/bin/sim.rs`, `--features sim`) links this same crate
//! and calls the same [`render`] function the device does — so the sim is
//! pixel-identical to the device *at the framebuffer level* by construction; the
//! only differences left are what the physical panel does to those bytes.
#![cfg_attr(not(feature = "std"), no_std)]

use embedded_graphics::{
    mono_font::{
        ascii::{FONT_6X10, FONT_9X15_BOLD},
        MonoTextStyle,
    },
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, PrimitiveStyleBuilder, Rectangle},
    text::{Alignment, Text},
};

// -----------------------------------------------------------------------------
// Panic handler (device/no_std only; the host sim uses std's handler)
// -----------------------------------------------------------------------------
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
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
    pub const RED: Abgr2222 = Abgr2222::rgb(255, 0, 0);
    pub const GREEN: Abgr2222 = Abgr2222::rgb(0, 255, 0);
    pub const BLUE: Abgr2222 = Abgr2222::rgb(0, 0, 255);
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

/// A boxed label: filled rectangle with high-contrast text punched in. Text on a
/// solid block reads even when thin glyph strokes alone would wash out.
fn boxed_text(
    fb: &mut FrameBuf,
    center: Point,
    inner_w: u32,
    inner_h: u32,
    fill: Abgr2222,
    fg: Abgr2222,
    text: &str,
    font: &embedded_graphics::mono_font::MonoFont,
) {
    Rectangle::new(
        Point::new(center.x - inner_w as i32 / 2, center.y - inner_h as i32 / 2),
        Size::new(inner_w, inner_h),
    )
    .into_styled(PrimitiveStyle::with_fill(fill))
    .draw(fb)
    .ok();

    // Nudge the baseline to vertically center the glyph in the box.
    let baseline = center.y + font.character_size.height as i32 / 3;
    Text::with_alignment(text, Point::new(center.x, baseline), MonoTextStyle::new(font, fg), Alignment::Center)
        .draw(fb)
        .ok();
}

// -----------------------------------------------------------------------------
// 7-segment clock — digits from filled rectangles (font glyphs don't render on
// this panel, but filled rects provably do)
// -----------------------------------------------------------------------------
// Segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
//    aaa
//   f   b
//    ggg
//   e   c
//    ddd
const SEG: [u8; 10] = [
    0b0111111, // 0: a b c d e f
    0b0000110, // 1: b c
    0b1011011, // 2: a b g e d
    0b1001111, // 3: a b g c d
    0b1100110, // 4: f g b c
    0b1101101, // 5: a f g c d
    0b1111101, // 6: a f g e c d
    0b0000111, // 7: a b c
    0b1111111, // 8: all
    0b1101111, // 9: a b c d f g
];

fn fill_rect(fb: &mut FrameBuf, x: i32, y: i32, w: i32, h: i32, color: Abgr2222) {
    if w > 0 && h > 0 {
        Rectangle::new(Point::new(x, y), Size::new(w as u32, h as u32))
            .into_styled(PrimitiveStyle::with_fill(color))
            .draw(fb)
            .ok();
    }
}

fn draw_digit(fb: &mut FrameBuf, x: i32, y: i32, dw: i32, dh: i32, th: i32, d: u8, color: Abgr2222) {
    let seg = SEG[(d % 10) as usize];
    let hlen = dw - 2 * th;
    let vlen = (dh - 3 * th) / 2;
    let on = |bit: u8| seg & (1 << bit) != 0;
    if on(0) { fill_rect(fb, x + th, y, hlen, th, color); }                       // a
    if on(5) { fill_rect(fb, x, y + th, th, vlen, color); }                       // f
    if on(1) { fill_rect(fb, x + dw - th, y + th, th, vlen, color); }             // b
    if on(6) { fill_rect(fb, x + th, y + th + vlen, hlen, th, color); }           // g
    if on(4) { fill_rect(fb, x, y + 2 * th + vlen, th, vlen, color); }            // e
    if on(2) { fill_rect(fb, x + dw - th, y + 2 * th + vlen, th, vlen, color); }  // c
    if on(3) { fill_rect(fb, x + th, y + 2 * th + 2 * vlen, hlen, th, color); }   // d
}

/// Draw HH:MM:SS centered on (cx, cy) from a seconds count.
fn draw_clock_7seg(fb: &mut FrameBuf, cx: i32, cy: i32, secs: u32, color: Abgr2222) {
    let hh = (secs / 3600) % 24;
    let mm = (secs / 60) % 60;
    let ss = secs % 60;
    let digits = [
        (hh / 10) as u8, (hh % 10) as u8,
        (mm / 10) as u8, (mm % 10) as u8,
        (ss / 10) as u8, (ss % 10) as u8,
    ];

    let (dw, dh, th, colon_w, gap) = (16, 30, 3, 8, 3);
    let total = 6 * dw + 2 * colon_w + 7 * gap;
    let y = cy - dh / 2;
    let mut x = cx - total / 2;

    for (i, d) in digits.iter().enumerate() {
        draw_digit(fb, x, y, dw, dh, th, *d, color);
        x += dw + gap;
        if i == 1 || i == 3 {
            // colon after HH and MM
            let cxp = x + colon_w / 2 - th / 2;
            fill_rect(fb, cxp, y + dh / 3 - th, th, th, color);
            fill_rect(fb, cxp, y + 2 * dh / 3, th, th, color);
            x += colon_w + gap;
        }
    }
}

// -----------------------------------------------------------------------------
// Screens
// -----------------------------------------------------------------------------
const SCREEN_COUNT: u32 = 2;

fn draw_home(fb: &mut FrameBuf, frame: u32) {
    let g = geom(fb);
    draw_rim(fb, &g);

    // Title, centered near the top of the safe square.
    Text::with_alignment(
        "UNA . Rust",
        Point::new(g.cx, g.cy - g.half + 18),
        MonoTextStyle::new(&FONT_9X15_BOLD, Abgr2222::CYAN),
        Alignment::Center,
    )
    .draw(fb)
    .ok();

    // Hero clock, drawn as 7-segment digits built from FILLED RECTANGLES — the
    // one primitive this panel provably displays. Font glyphs (even large black
    // text on a white block) do not render on-device, so we don't use them here.
    let secs = frame / 30; // ~30 ticks/sec, assumed
    draw_clock_7seg(fb, g.cx, g.cy, secs, Abgr2222::WHITE);

    // Animated marker orbiting just below the clock, kept inside the safe square.
    let steps = 60u32;
    let phase = frame % steps;
    let travel = g.half; // total horizontal travel, centered
    let x = g.cx - travel / 2 + (travel * phase as i32 / steps as i32);
    let y = g.cy + 34;
    Circle::new(Point::new(x - 5, y - 5), 10)
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::YELLOW))
        .draw(fb)
        .ok();

    footer(fb, &g, "SW2 = next  (1/2)");
}

fn draw_shapes(fb: &mut FrameBuf, frame: u32) {
    let g = geom(fb);
    draw_rim(fb, &g);

    // Boxed header so text is legible on this screen too.
    boxed_text(
        fb,
        Point::new(g.cx, g.cy - g.half + 16),
        (g.half as u32 * 2).saturating_sub(6),
        22,
        Abgr2222::GREEN,
        Abgr2222::BLACK,
        "embedded-graphics",
        &FONT_6X10,
    );

    let outline = PrimitiveStyleBuilder::new()
        .stroke_color(Abgr2222::WHITE)
        .stroke_width(2)
        .build();

    // Shapes row, inset within the safe square.
    let left = g.cx - g.half + 6;
    let row_y = g.cy - 24;
    Rectangle::new(Point::new(left, row_y), Size::new(40, 40))
        .into_styled(outline)
        .draw(fb)
        .ok();
    Circle::new(Point::new(g.cx - 6, row_y), 40)
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::RED))
        .draw(fb)
        .ok();
    Line::new(Point::new(left, row_y + 52), Point::new(g.cx + g.half - 6, row_y + 52))
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::CYAN, 3))
        .draw(fb)
        .ok();

    // Progress bar filling with the frame counter.
    let bar_w = (g.half as u32 * 2).saturating_sub(12);
    let fill = (frame % 100) * bar_w / 100;
    let bar_y = row_y + 66;
    Rectangle::new(Point::new(g.cx - bar_w as i32 / 2, bar_y), Size::new(bar_w, 12))
        .into_styled(outline)
        .draw(fb)
        .ok();
    Rectangle::new(Point::new(g.cx - bar_w as i32 / 2, bar_y), Size::new(fill, 12))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::YELLOW))
        .draw(fb)
        .ok();

    footer(fb, &g, "SW2 = next  (2/2)");
}

fn footer(fb: &mut FrameBuf, g: &Geom, msg: &str) {
    Text::with_alignment(
        msg,
        Point::new(g.cx, g.cy + g.half - 8),
        MonoTextStyle::new(&FONT_6X10, Abgr2222::WHITE),
        Alignment::Center,
    )
    .draw(fb)
    .ok();
}

// -----------------------------------------------------------------------------
// Rendering entry point — the single source of truth for BOTH the device (via
// the C ABI below) and the host simulator (src/bin/sim.rs).
// -----------------------------------------------------------------------------

/// Number of selectable screens.
pub const fn screen_count() -> u32 {
    SCREEN_COUNT
}

/// Render one frame into `buf`, an 8bpp ABGR2222 framebuffer of at least
/// `width * height` bytes. `screen` selects the UI; `frame` is a monotonic
/// counter driving animation. This is what the device and the sim both call, so
/// they cannot drift.
pub fn render(buf: &mut [u8], width: u32, height: u32, screen: u32, frame: u32) {
    if width == 0 || height == 0 || buf.len() < (width * height) as usize {
        return;
    }
    let mut fb = FrameBuf { buf, w: width, h: height };
    fb.buf.fill(Abgr2222::BLACK.0);
    match screen % SCREEN_COUNT {
        0 => draw_home(&mut fb, frame),
        _ => draw_shapes(&mut fb, frame),
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
/// # Safety
/// `buf` must point to at least `width * height` writable bytes and stay valid
/// for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn poc_gui_render(
    buf: *mut u8,
    width: u16,
    height: u16,
    screen: u32,
    frame: u32,
) {
    if buf.is_null() || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, width as usize * height as usize);
    render(slice, width as u32, height as u32, screen, frame);
}
