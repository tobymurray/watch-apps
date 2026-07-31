//! no_std Rust frontend core for the UNA Watch CustomGUI PoC.
//!
//! This crate owns *rendering only*. It draws with `embedded-graphics` into a
//! caller-provided 8bpp **ABGR2222** framebuffer and hands nothing back but
//! pixels. All watch plumbing — querying the display, pushing the buffer over
//! the kernel message bus, input, lifecycle — lives in the C++ shim (`Gui.cpp`),
//! which calls the `extern "C"` entry points at the bottom of this file.
//!
//! Why this split: the app<->kernel ABI is C++ (vtables, mangled names), which
//! Rust can't consume directly. A thin C ABI seam keeps the interesting UI work
//! in Rust while the C++ shim satisfies the SDK's `Gui { Gui(kernel); run(); }`
//! contract.
#![no_std]

use core::fmt::Write;

use embedded_graphics::{
    mono_font::{ascii::FONT_6X10, ascii::FONT_9X15_BOLD, MonoTextStyle},
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, PrimitiveStyleBuilder, Rectangle},
    text::{Alignment, Text},
};

// -----------------------------------------------------------------------------
// Panic handler
// -----------------------------------------------------------------------------
// panic = "abort" in Cargo.toml means no unwinder, so a no-op handler is all the
// language item requires. A real build would route this to the kernel logger.
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

// -----------------------------------------------------------------------------
// ABGR2222 — the watch's 8-bits-per-pixel packed color
// -----------------------------------------------------------------------------
// One byte per pixel. From MSB: A[7:6] B[5:4] G[3:2] R[1:0], 2 bits per channel.
// The panel is effectively a few-bit reflective memory LCD, so 2 bits/channel is
// all the color it can show — embedded-graphics' blocky output is a good match.
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
/// Borrows the caller's raw buffer (`width * height` bytes, row-major, 1 B/px).
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
        // Only ASCII is written below, so this is always valid UTF-8.
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
// Screens
// -----------------------------------------------------------------------------
const SCREEN_COUNT: u32 = 2;

fn draw_home(fb: &mut FrameBuf, frame: u32) {
    let w = fb.w as i32;
    let h = fb.h as i32;

    // Header band.
    Rectangle::new(Point::new(0, 0), Size::new(fb.w, 22))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::BLUE))
        .draw(fb)
        .ok();
    Text::with_alignment(
        "UNA · Rust UI",
        Point::new(w / 2, 15),
        MonoTextStyle::new(&FONT_9X15_BOLD, Abgr2222::WHITE),
        Alignment::Center,
    )
    .draw(fb)
    .ok();

    // A "clock" derived from the frame counter — no real time source in the PoC,
    // just proof the render loop is live and animating.
    let secs = frame / 30; // ~30 ticks/sec, assumed
    let mut t = Buf::new();
    let _ = write!(t, "{:02}:{:02}:{:02}", (secs / 3600) % 24, (secs / 60) % 60, secs % 60);
    Text::with_alignment(
        t.as_str(),
        Point::new(w / 2, h / 2),
        MonoTextStyle::new(&FONT_9X15_BOLD, Abgr2222::CYAN),
        Alignment::Center,
    )
    .draw(fb)
    .ok();

    // Animated marker orbiting so a still screenshot still shows motion frame-to-frame.
    let steps = 60u32;
    let phase = frame % steps;
    let cx = w / 2;
    let cy = h / 2 + 34;
    let x = cx - 40 + (80 * phase as i32 / steps as i32);
    Circle::new(Point::new(x - 4, cy - 4), 8)
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::YELLOW))
        .draw(fb)
        .ok();

    footer(fb, "SW2: next screen  (1/2)");
}

fn draw_shapes(fb: &mut FrameBuf, frame: u32) {
    let w = fb.w as i32;

    Rectangle::new(Point::new(0, 0), Size::new(fb.w, 22))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::GREEN))
        .draw(fb)
        .ok();
    Text::with_alignment(
        "embedded-graphics",
        Point::new(w / 2, 15),
        MonoTextStyle::new(&FONT_9X15_BOLD, Abgr2222::BLACK),
        Alignment::Center,
    )
    .draw(fb)
    .ok();

    let outline = PrimitiveStyleBuilder::new()
        .stroke_color(Abgr2222::WHITE)
        .stroke_width(2)
        .build();

    Rectangle::new(Point::new(20, 40), Size::new(50, 50))
        .into_styled(outline)
        .draw(fb)
        .ok();
    Circle::new(Point::new(90, 40), 50)
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::RED))
        .draw(fb)
        .ok();
    Line::new(Point::new(20, 110), Point::new(w - 20, 110))
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::CYAN, 3))
        .draw(fb)
        .ok();

    // A progress bar that fills with the frame counter.
    let bar_w = (w - 40) as u32;
    let fill = (frame % 100) * bar_w / 100;
    Rectangle::new(Point::new(20, 130), Size::new(bar_w, 12))
        .into_styled(outline)
        .draw(fb)
        .ok();
    Rectangle::new(Point::new(20, 130), Size::new(fill, 12))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::YELLOW))
        .draw(fb)
        .ok();

    footer(fb, "SW2: next screen  (2/2)");
}

fn footer(fb: &mut FrameBuf, msg: &str) {
    let h = fb.h as i32;
    Text::with_alignment(
        msg,
        Point::new(fb.w as i32 / 2, h - 8),
        MonoTextStyle::new(&FONT_6X10, Abgr2222::GRAY),
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
/// bytes). `screen` selects which UI is shown; `frame` is a monotonic counter
/// for animation.
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

    // Clear.
    fb.buf.fill(Abgr2222::BLACK.0);

    match screen % SCREEN_COUNT {
        0 => draw_home(&mut fb, frame),
        _ => draw_shapes(&mut fb, frame),
    }
}
