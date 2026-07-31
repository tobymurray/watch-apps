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
#![no_std]

use core::fmt::Write;

use embedded_graphics::{
    mono_font::{
        ascii::{FONT_10X20, FONT_6X10, FONT_9X15_BOLD},
        MonoTextStyle,
    },
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, PrimitiveStyleBuilder, Rectangle},
    text::{Alignment, Text},
};

// -----------------------------------------------------------------------------
// Panic handler (panic = "abort" -> a no-op handler satisfies the lang item)
// -----------------------------------------------------------------------------
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

// -----------------------------------------------------------------------------
// ABGR2222 — the watch's 8-bits-per-pixel packed color
// -----------------------------------------------------------------------------
// One byte per pixel. From MSB: A[7:6] B[5:4] G[3:2] R[1:0], 2 bits per channel.
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
// Tiny no-alloc string builder (avoids pulling in `alloc` or `heapless`)
// -----------------------------------------------------------------------------
struct Buf {
    data: [u8; 48],
    len: usize,
}
impl Buf {
    fn new() -> Self {
        Buf { data: [0; 48], len: 0 }
    }
    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.data[..self.len]).unwrap_or("")
    }
}
impl Write for Buf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &b in s.as_bytes() {
            if self.len < self.data.len() {
                self.data[self.len] = b;
                self.len += 1;
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

    // Hero clock: big black text on a white block, dead center — unmissable, and
    // a decisive legibility test vs. thin colored glyphs. Derived from the frame
    // counter (no real time source in the PoC).
    let secs = frame / 30; // ~30 ticks/sec, assumed
    let mut t = Buf::new();
    let _ = write!(t, "{:02}:{:02}:{:02}", (secs / 3600) % 24, (secs / 60) % 60, secs % 60);
    let box_w = (g.half as u32 * 2).saturating_sub(6).max(80);
    boxed_text(
        fb,
        Point::new(g.cx, g.cy),
        box_w,
        30,
        Abgr2222::WHITE,
        Abgr2222::BLACK,
        t.as_str(),
        &FONT_10X20,
    );

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
// C ABI — the seam the C++ shim (Gui.cpp) calls
// -----------------------------------------------------------------------------

/// Number of selectable screens, so the shim knows the modulus for cycling.
#[no_mangle]
pub extern "C" fn poc_gui_screen_count() -> u32 {
    SCREEN_COUNT
}

/// Render one frame into `buf` (an 8bpp ABGR2222 framebuffer of `width*height`
/// bytes). `screen` selects which UI is shown; `frame` is a monotonic counter.
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
    let len = width as usize * height as usize;
    let slice = core::slice::from_raw_parts_mut(buf, len);
    let mut fb = FrameBuf { buf: slice, w: width as u32, h: height as u32 };

    fb.buf.fill(Abgr2222::BLACK.0);

    match screen % SCREEN_COUNT {
        0 => draw_home(&mut fb, frame),
        _ => draw_shapes(&mut fb, frame),
    }
}
