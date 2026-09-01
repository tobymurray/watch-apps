//! The Spin app's renderer: everything the watch draws during a stationary
//! ride, as a pure function of one `Frame`.
//!
//! No clock, no sensor, no state. `Gui.cpp` fills a `Frame` from whatever the
//! Service last published and calls `render()`; the same call from the host
//! simulator draws the same pixels, which is what makes a screenshot on a
//! laptop evidence about the watch.
//!
//! The framebuffer is the panel's own format: 8bpp `ABGR2222`, four levels a
//! channel. Any grey that is not 0/85/170/255 is quantised on the way to the
//! glass, so the palette below only names colours the panel can actually hold.

// The tests below need `--features std`, same as Barcode's crate: without it
// the lib is no_std and a test binary cannot link against a crate whose
// panics do not unwind. `cargo test --features std` is the invocation.
#![cfg_attr(not(feature = "std"), no_std)]

use embedded_graphics::{pixelcolor::raw::RawU8, pixelcolor::PixelColor, prelude::*};
#[cfg(not(feature = "std"))]
use micromath::F32Ext;
use u8g2_fonts::{
    fonts,
    types::{FontColor, HorizontalAlignment, VerticalPosition},
    FontRenderer,
};

#[cfg(not(feature = "std"))]
extern "C" {
    fn spin_gui_host_panic(msg: *const u8, len: u32);
}

/// A panic in the renderer reaches the SDK logger and stops the app. Without
/// this the GUI thread would hang silently and the only way out would be a
/// reboot -- with a ride in progress and a half-written FIT file.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"panic";
    unsafe { spin_gui_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// The frames worth looking at. Host-only: the watch is handed frames by
/// Gui.cpp and never needs a table of them.
#[cfg(feature = "std")]
pub mod scenes;

// -- Colour ------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Abgr2222(pub u8);

const ALPHA_SHIFT: u8 = 6;
const BLUE_SHIFT: u8 = 4;
const GREEN_SHIFT: u8 = 2;
const RED_SHIFT: u8 = 0;
const CHANNEL_MASK: u8 = 0b11;
const CHANNEL_BITS: u8 = 2;
const ALPHA_OPAQUE: u8 = 0b11;

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
}

impl Default for Abgr2222 {
    fn default() -> Self {
        Abgr2222::BLACK
    }
}

impl PixelColor for Abgr2222 {
    type Raw = RawU8;
}

const WHITE: Abgr2222 = Abgr2222::WHITE;
const BLACK: Abgr2222 = Abgr2222::BLACK;
/// The dimmest grey worth using. 85 is the only other non-black level and it
/// washes out in daylight on this reflective panel, so "secondary" is 170.
const DIM: Abgr2222 = Abgr2222::rgb(170, 170, 170);
/// Heart rate, and only heart rate. Nothing else on any screen is red.
const RED: Abgr2222 = Abgr2222::rgb(255, 0, 0);
/// Held / not-what-you-wanted. Paused, and a ride that failed to save.
const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
/// One hue per heart-rate zone, in the ladder every training app uses: grey,
/// blue, green, amber, red. Each has a dim twin, which is the same hue one
/// level down on every non-zero channel -- so an inactive segment reads as the
/// same zone, quieter, rather than as a different colour.
///
/// Four levels a channel is the whole palette, so these are not approximations
/// of nicer colours: they are the colours.
const ZONE_BRIGHT: [Abgr2222; ZONE_COUNT as usize] = [
    Abgr2222::rgb(170, 170, 170), // 1  grey
    Abgr2222::rgb(0, 170, 255),   // 2  blue
    Abgr2222::rgb(0, 255, 0),     // 3  green
    Abgr2222::rgb(255, 170, 0),   // 4  amber
    Abgr2222::rgb(255, 0, 0),     // 5  red
];
const ZONE_DIM_HUE: [Abgr2222; ZONE_COUNT as usize] = [
    Abgr2222::rgb(85, 85, 85),
    Abgr2222::rgb(0, 85, 170),
    Abgr2222::rgb(0, 170, 0),
    Abgr2222::rgb(170, 85, 0),
    Abgr2222::rgb(170, 0, 0),
];

// -- Framebuffer -------------------------------------------------------------

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

/// A direct row-fill rather than embedded-graphics's styled-primitive
/// machinery: this is the only rectangle primitive the renderer has, and
/// `Rectangle::into_styled().draw()` would pull in the point-iterator layer
/// once per distinct colour it is called with.
fn fill_rect(fb: &mut FrameBuf, x: i32, y: i32, w: i32, h: i32, color: Abgr2222) {
    if w <= 0 || h <= 0 {
        return;
    }
    let fb_w = fb.w as i32;
    let fb_h = fb.h as i32;
    let x0 = x.max(0);
    let y0 = y.max(0);
    let x1 = (x + w).min(fb_w);
    let y1 = (y + h).min(fb_h);
    if x0 >= x1 || y0 >= y1 {
        return;
    }
    for row in y0..y1 {
        let start = (row * fb_w + x0) as usize;
        let end = (row * fb_w + x1) as usize;
        fb.buf[start..end].fill(color.0);
    }
}

// -- The C ABI frame ---------------------------------------------------------
// Mirrors spin_gui_frame (spin_gui.h) field for field. The fingerprint below
// checks whatever the two compilers actually produced rather than trusting
// that this list still matches by inspection.

pub const SCREEN_READY: u8 = 0;
pub const SCREEN_RIDING: u8 = 1;
pub const SCREEN_PAUSED: u8 = 2;
pub const SCREEN_SAVED: u8 = 3;
pub const SCREEN_CONFIRM_DISCARD: u8 = 4;
pub const SCREEN_DISCARDED: u8 = 5;

pub const STRAP_ABSENT: u8 = 0;
pub const STRAP_SEARCHING: u8 = 1;
pub const STRAP_CONNECTED: u8 = 2;

pub const HR_NONE: u8 = 0;
pub const HR_OPTICAL: u8 = 1;
pub const HR_EXTERNAL: u8 = 2;

/// Heart-rate zones the bar draws. Zone 0 (below zone 1) is a state, not a
/// segment: it dims every segment rather than adding a sixth.
pub const ZONE_COUNT: u8 = 5;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Frame {
    pub elapsed_s: u32,
    pub hr_bpm: u16,
    pub avg_hr_bpm: u16,
    pub target_minutes: u16,
    pub energy: u16,
    pub screen: u8,
    pub strap: u8,
    pub hr_source: u8,
    pub saved_ok: u8,
    pub target_reached: u8,
    pub hr_zone: u8,
    pub has_zones: u8,
    pub energy_is_kj: u8,
    pub hold_pct: u8,
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `spin_gui_abi::fingerprint()`
/// in spin_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Frame>());
    let h = fnv1a(h, core::mem::align_of::<Frame>());
    let h = fnv1a(h, core::mem::offset_of!(Frame, elapsed_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_bpm));
    let h = fnv1a(h, core::mem::offset_of!(Frame, avg_hr_bpm));
    let h = fnv1a(h, core::mem::offset_of!(Frame, target_minutes));
    let h = fnv1a(h, core::mem::offset_of!(Frame, energy));
    let h = fnv1a(h, core::mem::offset_of!(Frame, screen));
    let h = fnv1a(h, core::mem::offset_of!(Frame, strap));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_source));
    let h = fnv1a(h, core::mem::offset_of!(Frame, saved_ok));
    let h = fnv1a(h, core::mem::offset_of!(Frame, target_reached));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_zone));
    let h = fnv1a(h, core::mem::offset_of!(Frame, has_zones));
    let h = fnv1a(h, core::mem::offset_of!(Frame, energy_is_kj));
    fnv1a(h, core::mem::offset_of!(Frame, hold_pct))
}

#[no_mangle]
pub extern "C" fn spin_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Frame>() == 24);
const _: () = assert!(core::mem::align_of::<Frame>() == 4);
const _: () = assert!(core::mem::offset_of!(Frame, elapsed_s) == 0);
const _: () = assert!(core::mem::offset_of!(Frame, hr_bpm) == 4);
const _: () = assert!(core::mem::offset_of!(Frame, avg_hr_bpm) == 6);
const _: () = assert!(core::mem::offset_of!(Frame, target_minutes) == 8);
const _: () = assert!(core::mem::offset_of!(Frame, energy) == 10);
const _: () = assert!(core::mem::offset_of!(Frame, screen) == 12);
const _: () = assert!(core::mem::offset_of!(Frame, strap) == 13);
const _: () = assert!(core::mem::offset_of!(Frame, hr_source) == 14);
const _: () = assert!(core::mem::offset_of!(Frame, saved_ok) == 15);
const _: () = assert!(core::mem::offset_of!(Frame, target_reached) == 16);
const _: () = assert!(core::mem::offset_of!(Frame, hr_zone) == 17);
const _: () = assert!(core::mem::offset_of!(Frame, has_zones) == 18);
const _: () = assert!(core::mem::offset_of!(Frame, energy_is_kj) == 19);
const _: () = assert!(core::mem::offset_of!(Frame, hold_pct) == 20);

// -- Geometry ----------------------------------------------------------------

const PANEL_W: i32 = 240;
const PANEL_H: i32 = 240;
const CENTER_X: i32 = PANEL_W / 2;

/// Button labels live above and below the clock, never beside it. The clock is
/// the widest thing on the panel and spans it edge to edge at its own rows, so
/// a label level with the buttons themselves gets drawn straight through --
/// which is what the first layout did.
///
/// The x insets are set by the *round* panel, not by the 240x240 buffer. The
/// half-chord at y=44 is 92px, so a label whose right edge sat at 214 lost its
/// last glyph to the bezel -- invisible in a square simulator and obvious on
/// the glass. `nothing_is_drawn_outside_the_bezel` is the regression test.
// Lower than the bezel alone would need. The paused screen puts two labels on
// this row -- DISCARD on the left button and RESUME on the right -- and at y=44
// the ring's inner edge leaves only ~116px between them, which is not enough
// for both. Ten pixels down the chord is 150px and they fit with room to spare.
const HINT_TOP_Y: i32 = 54;
const HINT_BOTTOM_Y: i32 = 184;
/// Pulled in far enough to clear the zone ring, not just the bezel. At the top
/// row the ring's inner edge is the binding constraint; at the bottom row it is
/// the edge of the ring's 90-degree opening. Both work out to about the same
/// inset, so there is one pair rather than two.
const HINT_LEFT_X: i32 = 48;
const HINT_RIGHT_X: i32 = PANEL_W - 48;

/// The clock's top edge, on every screen that shows one. Chosen so the clock
/// plus the row under it sits centred between the two hint rows.
const CLOCK_Y: i32 = 70;
/// The heart-rate row, on both riding screens. Identical on each so the number
/// does not jump when the clock is paused.
const HR_ROW_Y: i32 = 138;
/// "PAUSED", tucked between the clock and the heart-rate row. Not on the hint
/// row: a centred word there runs into the right-hand button label, and the
/// two longest cases ("PAUSED"/"RESUME") are exactly the pair that collide.
const PAUSED_BANNER_Y: i32 = 116;

// -- Fonts -------------------------------------------------------------------
// `_tn` (digits, ':' and a little punctuation) for every number, `_tr` (the
// reduced ASCII tier) for every label. Nothing this app draws is outside
// either set: the labels are fixed English literals and the numbers are
// formatted here, so the full-Unicode `_tf` tiers would be dead flash.

/// Largest to smallest. `render_clock` walks this list and takes the first
/// that fits the panel, so "1:23" gets the big face and "10:23:45" does not
/// get clipped -- rather than one compromise size that suits neither.
type ClockXl = fonts::u8g2_font_fub42_tn;
type ClockL = fonts::u8g2_font_fub35_tn;
type ClockM = fonts::u8g2_font_fub25_tn;

type NumberFont = fonts::u8g2_font_fub20_tn;
type TitleFont = fonts::u8g2_font_helvB24_tr;
type HeadingFont = fonts::u8g2_font_helvB14_tr;
type LabelFont = fonts::u8g2_font_helvR12_tr;

/// The clock's own drawn height at each size, used to place it by its top
/// edge. u8g2 reports ascent/descent per face; these are the ascent-descent
/// spans of the three faces above.
const CLOCK_H_XL: i32 = 42;
const CLOCK_H_L: i32 = 35;
const CLOCK_H_M: i32 = 25;

/// Widest the clock may draw. The panel is round, so this is the chord at the
/// clock's own rows rather than the full 240.
const CLOCK_MAX_W: u32 = 218;

fn text_width(renderer: &FontRenderer, s: &str) -> u32 {
    renderer
        .get_rendered_dimensions(s, Point::zero(), VerticalPosition::Top)
        .map(|d| d.bounding_box.map(|b| b.size.width).unwrap_or(0))
        .unwrap_or(0)
}

fn draw_text(
    fb: &mut FrameBuf,
    renderer: &FontRenderer,
    s: &str,
    x: i32,
    y: i32,
    align: HorizontalAlignment,
    color: Abgr2222,
) {
    let _ = renderer.render_aligned(
        s,
        Point::new(x, y),
        VerticalPosition::Top,
        align,
        FontColor::Transparent(color),
        fb,
    );
}

fn draw_centered(fb: &mut FrameBuf, renderer: &FontRenderer, s: &str, y: i32, color: Abgr2222) {
    draw_text(fb, renderer, s, CENTER_X, y, HorizontalAlignment::Center, color);
}

// -- Number formatting -------------------------------------------------------
// no_std and no allocator, so every string is built into a caller-owned
// buffer. `core::fmt` would work through `format_args!`, but it drags the
// whole formatting machinery in for what is two loops.

/// Writes `value` as decimal into the end of `buf`, returning just the digits.
/// No padding: the one field that needs it (minutes, and only in the hours
/// form) is padded by its caller, where the rule actually lives.
fn u32_to_str(mut value: u32, buf: &mut [u8; 12]) -> &str {
    let mut i = buf.len();
    loop {
        i -= 1;
        buf[i] = b'0' + (value % 10) as u8;
        value /= 10;
        if value == 0 || i == 0 {
            break;
        }
    }
    core::str::from_utf8(&buf[i..]).unwrap_or("0")
}

/// `M:SS` under an hour, `H:MM:SS` over it -- the shortest form that is still
/// unambiguous, so the clock face can be as large as possible for as long as
/// possible. Saturates rather than wrapping: a ride that somehow ran past 99
/// hours shows 99:59:59, which is visibly wrong, where a wrapped 0:00:03 is
/// not.
pub fn format_duration(seconds: u32, buf: &mut [u8; 12]) -> &str {
    const MAX: u32 = 99 * 3600 + 59 * 60 + 59;
    let s = seconds.min(MAX);
    let (h, m, sec) = (s / 3600, (s % 3600) / 60, s % 60);

    let mut n = 0;
    let push = |buf: &mut [u8; 12], n: &mut usize, byte: u8| {
        if *n < buf.len() {
            buf[*n] = byte;
            *n += 1;
        }
    };

    if h > 0 {
        let mut scratch = [0u8; 12];
        for b in u32_to_str(h, &mut scratch).as_bytes() {
            push(buf, &mut n, *b);
        }
        push(buf, &mut n, b':');
        push(buf, &mut n, b'0' + (m / 10) as u8);
        push(buf, &mut n, b'0' + (m % 10) as u8);
    } else {
        let mut scratch = [0u8; 12];
        for b in u32_to_str(m, &mut scratch).as_bytes() {
            push(buf, &mut n, *b);
        }
    }
    push(buf, &mut n, b':');
    push(buf, &mut n, b'0' + (sec / 10) as u8);
    push(buf, &mut n, b'0' + (sec % 10) as u8);

    core::str::from_utf8(&buf[..n]).unwrap_or("0:00")
}

// -- The heart -------------------------------------------------------------
// Hand-drawn rather than a font glyph: `_tr` has no heart in it, and a second
// symbol font for one 15x13 shape would cost more flash than the shape does.
// Bit 0 of each row is its leftmost pixel.

const HEART_W: i32 = 15;
const HEART_H: i32 = 13;
/// Space between the heart and the text it belongs to, and between words laid
/// out side by side. Both are explicit because the font renderer measures ink,
/// not advance, so a space inside a string is not a gap it will report.
const HEART_GAP: i32 = 8;
const WORD_GAP: i32 = 9;
const HEART: [u16; HEART_H as usize] = [
    0x1C1C, 0x3E3E, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x3FFE, 0x1FFC, 0x0FF8, 0x07F0, 0x03E0,
    0x01C0, 0x0080,
];

fn draw_heart(fb: &mut FrameBuf, x: i32, y: i32, color: Abgr2222) {
    for (row, bits) in HEART.iter().enumerate() {
        for col in 0..HEART_W {
            if (bits >> col) & 1 != 0 {
                fill_rect(fb, x + col, y + row as i32, 1, 1, color);
            }
        }
    }
}

/// Red when the beat came from the chest strap, white from the wrist, dim when
/// there is no beat at all. The colour is the whole legend: a wearer who put a
/// strap on wants to know at a glance that it is the one being believed, and
/// nothing else on any screen is red.
fn heart_color(hr_source: u8, has_beat: bool) -> Abgr2222 {
    if !has_beat {
        DIM
    } else if hr_source == HR_EXTERNAL {
        RED
    } else {
        WHITE
    }
}

// -- Screens -----------------------------------------------------------------

fn strap_text(strap: u8) -> &'static str {
    match strap {
        STRAP_CONNECTED => "STRAP READY",
        STRAP_SEARCHING => "FINDING STRAP",
        // Not an error: the wrist sensor is a heart rate monitor too, and
        // saying so is more use than saying what is missing.
        _ => "WRIST SENSOR",
    }
}

fn draw_ready(fb: &mut FrameBuf, frame: &Frame) {
    let title = FontRenderer::new::<TitleFont>();
    let label = FontRenderer::new::<LabelFont>();

    // The block is centred between the two hint rows, so it starts higher when
    // there is a target line to fit in. Laying it out from a fixed top instead
    // would leave the no-target case sitting high and the target case running
    // into the EXIT hint -- which is what the first version did.
    let has_target = frame.target_minutes > 0;
    let top = if has_target { 70 } else { 84 };

    draw_centered(fb, &title, "SPIN", top, WHITE);

    // A rule under the title, the width of the word. Nothing structural --
    // it just stops the strap line reading as a subtitle of the app name.
    let w = text_width(&title, "SPIN") as i32;
    fill_rect(fb, CENTER_X - w / 2, top + 38, w, 2, DIM);

    draw_zone_ring(fb, 0, frame.has_zones != 0);
    draw_target_arc(fb, 0, frame.target_minutes, false);

    let connected = frame.strap == STRAP_CONNECTED;
    let heart = if connected { RED } else { DIM };
    let text = strap_text(frame.strap);
    let text_w = text_width(&label, text) as i32;
    let group_w = HEART_W + HEART_GAP + text_w;
    let left = CENTER_X - group_w / 2;

    draw_heart(fb, left, top + 54, heart);
    draw_text(
        fb,
        &label,
        text,
        left + HEART_W + HEART_GAP,
        top + 52,
        HorizontalAlignment::Left,
        if connected { WHITE } else { DIM },
    );

    // Only drawn when a target is set. A row saying "no target" would be a
    // permanent line about a feature most rides do not use.
    if has_target {
        let y = top + 78;
        let mut buf = [0u8; 12];
        let minutes = u32_to_str(frame.target_minutes as u32, &mut buf);
        let label_w = text_width(&label, "TARGET") as i32;
        let value_w = text_width(&label, minutes) as i32;
        let unit_w = text_width(&label, "MIN") as i32;
        let left = CENTER_X - (label_w + WORD_GAP + value_w + WORD_GAP + unit_w) / 2;
        let value_x = left + label_w + WORD_GAP;
        draw_text(fb, &label, "TARGET", left, y, HorizontalAlignment::Left, DIM);
        draw_text(fb, &label, minutes, value_x, y, HorizontalAlignment::Left, WHITE);
        draw_text(fb, &label, "MIN", value_x + value_w + WORD_GAP, y,
                  HorizontalAlignment::Left, DIM);
    }

    draw_hint(fb, HINT_RIGHT_X, HINT_TOP_Y, HorizontalAlignment::Right, "START", WHITE);
    draw_hint(fb, HINT_RIGHT_X, HINT_BOTTOM_Y, HorizontalAlignment::Right, "EXIT", DIM);
}

/// Draws the clock at the largest of the three faces that fits, and returns
/// the height it drew at so the caller can place what comes after it.
fn render_clock(fb: &mut FrameBuf, text: &str, y: i32, color: Abgr2222) -> i32 {
    let xl = FontRenderer::new::<ClockXl>();
    if text_width(&xl, text) <= CLOCK_MAX_W {
        draw_centered(fb, &xl, text, y, color);
        return CLOCK_H_XL;
    }
    let l = FontRenderer::new::<ClockL>();
    if text_width(&l, text) <= CLOCK_MAX_W {
        draw_centered(fb, &l, text, y, color);
        return CLOCK_H_L;
    }
    let m = FontRenderer::new::<ClockM>();
    draw_centered(fb, &m, text, y, color);
    CLOCK_H_M
}

fn draw_riding(fb: &mut FrameBuf, frame: &Frame) {
    let label = FontRenderer::new::<LabelFont>();
    let paused = frame.screen == SCREEN_PAUSED;

    let mut buf = [0u8; 12];
    let text = format_duration(frame.elapsed_s, &mut buf);
    // Dimmed while held, so a glance from the saddle tells the clock apart
    // from a running one without reading the banner.
    render_clock(fb, text, CLOCK_Y, if paused { DIM } else { WHITE });

    // One slot, two things that can want it. Paused wins: it is the state the
    // wearer can act on, and the target having been met stays true for the rest
    // of the ride while a pause is the thing happening right now.
    if paused {
        draw_centered(fb, &label, "PAUSED", PAUSED_BANNER_Y, AMBER);
    } else if frame.target_reached != 0 {
        draw_centered(fb, &label, "TARGET MET", PAUSED_BANNER_Y, WHITE);
    }

    draw_hr_row(fb, frame.hr_bpm, frame.hr_source, HR_ROW_Y);
    draw_zone_ring(fb, frame.hr_zone, frame.has_zones != 0);
    draw_target_arc(fb, frame.elapsed_s, frame.target_minutes, frame.target_reached != 0);

    if paused {
        // FINISH and DISCARD on the two left buttons, at opposite ends. Both on
        // the top row ran into RESUME, and putting the destructive one a whole
        // panel away from the one you reach for every ride is worth more than
        // the symmetry anyway.
        draw_hint(fb, HINT_LEFT_X, HINT_TOP_Y, HorizontalAlignment::Left, "FINISH", AMBER);
        draw_hint(fb, HINT_RIGHT_X, HINT_TOP_Y, HorizontalAlignment::Right, "RESUME", WHITE);
        draw_hint(fb, HINT_LEFT_X, HINT_BOTTOM_Y, HorizontalAlignment::Left, "DISCARD", RED);
    } else {
        draw_hint(fb, HINT_RIGHT_X, HINT_TOP_Y, HorizontalAlignment::Right, "PAUSE", WHITE);
    }
}

/// Heart, then the number, then its unit -- centred as one group so the row
/// stays put when the bpm goes from two digits to three.
// -- The zone ring ----------------------------------------------------------
// The zones drawn around the rim, as a speedometer rather than a bar. The panel
// is a circle and this is the one piece of information that is naturally a
// scale, so it gets the perimeter and the middle stays clear for the numbers.

/// Centre of the panel, in the half-pixel sense: a 240-wide row has its middle
/// between pixel 119 and 120, and a ring drawn about 120.0 sits a half pixel
/// off and shows it as a lopsided rim.
const RING_CX: f32 = (PANEL_W as f32 - 1.0) / 2.0;
const RING_CY: f32 = (PANEL_H as f32 - 1.0) / 2.0;

const RING_OUTER: f32 = 116.0;
/// Inactive segments are thinner. The active one grows inward to RING_INNER_ON,
/// so the zone you are in is the one with weight as well as brightness -- two
/// signals, because on a reflective panel in bad light colour alone is thin.
const RING_INNER_OFF: f32 = 107.0;
const RING_INNER_ON: f32 = 100.0;

/// Where the scale starts and stops, measured clockwise from twelve o'clock.
/// A 270-degree sweep with the gap at the bottom: the classic speedometer
/// opening, and it is where the button labels live.
const RING_START_DEG: f32 = -135.0;
const RING_SWEEP_DEG: f32 = 270.0;
/// Gap between segments, in degrees, so five arcs read as five.
const RING_GAP_DEG: f32 = 3.0;

/// The 90 degrees the zone scale leaves open at the bottom, minus a margin at
/// each end so the target arc does not touch the zone arcs it sits between.
const TARGET_START_DEG: f32 = 223.0;
const TARGET_SWEEP_DEG: f32 = 86.0;
/// Thinner than the zone arcs and set inside them: a second reading, not a
/// competing one.
const TARGET_OUTER: f32 = 114.0;
const TARGET_INNER: f32 = 108.0;

const DEG_TO_RAD: f32 = core::f32::consts::PI / 180.0;

/// How far apart consecutive samples are, in pixels, along the arc and along
/// the radius. The samples form a grid of this pitch laid over the pixel grid
/// at an arbitrary rotation, so the limit is not 1.0 but the diagonal: past
/// 1/sqrt(2) ~= 0.707 a pixel can fall between four samples and never be
/// painted.
///
/// Measured rather than reasoned: 0.85 leaves 95 unpainted pixels in the ring,
/// 0.75 leaves 4, and 0.70 is clean -- which lands on the geometry exactly.
/// 0.65 is that with a little margin.
///
/// These were 0.4 and 0.5, which painted every pixel of the ring three to nine
/// times over. `the_hold_ring_has_no_gaps` is what makes tuning them safe: it
/// asserts the ring is solid, not merely present, which is the one thing that
/// goes wrong here and the one thing no other test would notice.
const ARC_ANGULAR_PX: f32 = 0.65;
const ARC_RADIAL_PX: f32 = 0.65;

/// Fills the wedge between two radii and two angles.
///
/// Swept rather than scanned: walking the angle and drawing a radial run at
/// each step touches only the pixels of the arc, where testing every pixel of
/// the bounding box for membership would be the whole 240x240 panel and an
/// atan2 per pixel. The step is sized so that consecutive runs overlap at the
/// outer edge, which is where they are furthest apart.
fn fill_arc(
    fb: &mut FrameBuf,
    r_inner: f32,
    r_outer: f32,
    start_deg: f32,
    sweep_deg: f32,
    color: Abgr2222,
) {
    if sweep_deg <= 0.0 || r_outer <= r_inner {
        return;
    }
    // Radians per step, sized so the outer edge -- where consecutive runs are
    // furthest apart -- moves by ARC_ANGULAR_PX.
    let step = ARC_ANGULAR_PX / r_outer;
    let start = start_deg * DEG_TO_RAD;
    let end = start + sweep_deg * DEG_TO_RAD;

    let mut a = start;
    while a <= end {
        // 0 degrees is twelve o'clock and angles run clockwise, which is how
        // the constants above read; the panel's y grows downward, hence -cos.
        let (sa, ca) = (a.sin(), a.cos());
        let mut r = r_inner;
        while r <= r_outer {
            let x = (RING_CX + r * sa + 0.5) as i32;
            let y = (RING_CY - r * ca + 0.5) as i32;
            fill_rect(fb, x, y, 1, 1, color);
            r += ARC_RADIAL_PX;
        }
        a += step;
    }
}

/// The five zone arcs. Drawn whenever the wearer has thresholds set, whatever
/// zone they are in -- the scale is the point, and a ring that appeared only
/// once you reached zone 1 would be a ring that vanished when you eased off.
///
/// Zone 0 (below zone 1) lights nothing, which is the honest rendering of
/// warming up: the scale is there, you are not on it yet.
fn draw_zone_ring(fb: &mut FrameBuf, zone: u8, has_zones: bool) {
    if !has_zones {
        return;
    }

    let n = ZONE_COUNT as f32;
    let segment = (RING_SWEEP_DEG - RING_GAP_DEG * (n - 1.0)) / n;

    for i in 0..ZONE_COUNT {
        let start = RING_START_DEG + (segment + RING_GAP_DEG) * i as f32;
        let active = zone == i + 1;
        let (inner, color) = if active {
            (RING_INNER_ON, ZONE_BRIGHT[i as usize])
        } else {
            (RING_INNER_OFF, ZONE_DIM_HUE[i as usize])
        };
        fill_arc(fb, inner, RING_OUTER, start, segment, color);
    }
}

/// Progress toward the configured target, drawn in the opening the zone scale
/// leaves at the bottom. Only when a target is set -- with none, the gap stays
/// empty, which is what makes the ring read as a speedometer rather than a
/// closed circle.
///
/// It grows from the bottom-left toward the bottom-right, so the two arcs read
/// as one continuous sweep: the zones run clockwise over the top and finish at
/// the lower right, and this picks up at the lower left and runs to meet them.
fn draw_target_arc(fb: &mut FrameBuf, elapsed_s: u32, target_minutes: u16, reached: bool) {
    if target_minutes == 0 {
        return;
    }

    let target_s = target_minutes as f32 * 60.0;
    let fraction = if reached { 1.0 } else { (elapsed_s as f32 / target_s).min(1.0) };
    let swept = TARGET_SWEEP_DEG * fraction;

    // The two arcs meet rather than overlap: the track covers only what is not
    // yet filled. Drawing it end to end and then painting over half of it costs
    // the whole ring twice for the same picture.
    let remaining = TARGET_SWEEP_DEG - swept;
    if remaining > 0.0 {
        fill_arc(fb, TARGET_INNER, TARGET_OUTER, TARGET_START_DEG - TARGET_SWEEP_DEG,
                 remaining, ZONE_DIM_HUE[0]);
    }
    // Anchored at the bottom-left end and advancing toward the bottom-right, so
    // the filled part always starts in the same place.
    if swept > 0.0 {
        fill_arc(fb, TARGET_INNER, TARGET_OUTER, TARGET_START_DEG - swept, swept, WHITE);
    }
}

fn draw_hr_row(fb: &mut FrameBuf, bpm: u16, hr_source: u8, y: i32) {
    let number = FontRenderer::new::<NumberFont>();
    let label = FontRenderer::new::<LabelFont>();

    let has_beat = bpm > 0;
    let mut buf = [0u8; 12];
    // Three dashes, not a stale number and not a zero: "no reading" and "a
    // reading of zero" have to look different, and only one of them can happen.
    let text = if has_beat {
        u32_to_str(bpm as u32, &mut buf)
    } else {
        "---"
    };

    let number_w = if has_beat {
        text_width(&number, text) as i32
    } else {
        text_width(&label, text) as i32
    };
    let unit_w = text_width(&label, "BPM") as i32;
    let group_w = HEART_W + HEART_GAP + number_w + WORD_GAP + unit_w;
    let left = CENTER_X - group_w / 2;

    draw_heart(fb, left, y + 6, heart_color(hr_source, has_beat));

    let text_x = left + HEART_W + HEART_GAP;
    if has_beat {
        draw_text(fb, &number, text, text_x, y - 2, HorizontalAlignment::Left, WHITE);
    } else {
        draw_text(fb, &label, text, text_x, y + 4, HorizontalAlignment::Left, DIM);
    }
    draw_text(
        fb,
        &label,
        "BPM",
        text_x + number_w + WORD_GAP,
        y + 8,
        HorizontalAlignment::Left,
        DIM,
    );
}

fn draw_saved(fb: &mut FrameBuf, frame: &Frame) {
    let heading = FontRenderer::new::<HeadingFont>();
    let label = FontRenderer::new::<LabelFont>();

    let ok = frame.saved_ok != 0;
    // The Service only claims this once the .fit is flushed and closed, so
    // "SAVED" here is a fact about the filesystem rather than a reassurance.
    draw_centered(
        fb,
        &heading,
        if ok { "SAVED" } else { "NOT SAVED" },
        HINT_TOP_Y,
        if ok { WHITE } else { AMBER },
    );

    let mut buf = [0u8; 12];
    let text = format_duration(frame.elapsed_s, &mut buf);
    render_clock(fb, text, CLOCK_Y, WHITE);

    // Average heart rate, then energy. Two rows rather than one crowded one:
    // three digits and a unit each, and at this size they do not share a line.
    if frame.avg_hr_bpm > 0 {
        let mut hr_buf = [0u8; 12];
        let hr = u32_to_str(frame.avg_hr_bpm as u32, &mut hr_buf);
        draw_value_row(fb, &label, "AVG", hr, "BPM", 132);
    } else {
        draw_centered(fb, &label, "NO HEART RATE", 132, DIM);
    }

    // 0 is a real answer for a ride too short to have burned a whole unit, so
    // the row is always drawn: an absent one would read as a broken estimate
    // rather than a small one.
    let mut energy_buf = [0u8; 12];
    let energy = u32_to_str(frame.energy as u32, &mut energy_buf);
    let unit = if frame.energy_is_kj != 0 { "KJ" } else { "KCAL" };
    draw_value_row(fb, &label, "", energy, unit, 156);

    // Bottom right, not top right: the heading owns the top row here, and
    // "NOT SAVED" is wide enough to reach a label placed beside it. Leaving is
    // the bottom-right button on every screen that offers it anyway.
    draw_hint(fb, HINT_RIGHT_X, HINT_BOTTOM_Y, HorizontalAlignment::Right, "DONE", WHITE);
}

/// `prefix value unit`, centred as one group, the value in white and the words
/// around it dim. Spaced with explicit gaps rather than spaces inside the
/// strings: the renderer measures a string by its ink, so a leading or trailing
/// space contributes nothing to the width and the parts run together.
fn draw_value_row(
    fb: &mut FrameBuf,
    label: &FontRenderer,
    prefix: &str,
    value: &str,
    unit: &str,
    y: i32,
) {
    let prefix_w = if prefix.is_empty() { 0 } else { text_width(label, prefix) as i32 };
    let value_w = text_width(label, value) as i32;
    let unit_w = text_width(label, unit) as i32;

    let lead = if prefix.is_empty() { 0 } else { prefix_w + WORD_GAP };
    let left = CENTER_X - (lead + value_w + WORD_GAP + unit_w) / 2;

    if !prefix.is_empty() {
        draw_text(fb, label, prefix, left, y, HorizontalAlignment::Left, DIM);
    }
    let value_x = left + lead;
    draw_text(fb, label, value, value_x, y, HorizontalAlignment::Left, WHITE);
    draw_text(fb, label, unit, value_x + value_w + WORD_GAP, y,
              HorizontalAlignment::Left, DIM);
}

/// Hold-to-confirm, the way the SDK's own activity apps gate Discard: the ring
/// fills while the button is held and the ride only goes when it is full.
/// Releasing early cancels, which is the whole point -- this is the one action
/// in the app that destroys data, so it should be hard to do by accident and
/// easy to back out of.
///
/// The ring fills clockwise from twelve o'clock, whole-circle rather than the
/// zone scale's 270 degrees, so it cannot be mistaken for the zone display.
fn draw_confirm_discard(fb: &mut FrameBuf, hold_pct: u8) {
    let heading = FontRenderer::new::<HeadingFont>();
    let label = FontRenderer::new::<LabelFont>();

    // Track and fill meet rather than overlap -- see draw_target_arc().
    let swept = 360.0 * (hold_pct.min(100) as f32) / 100.0;
    if swept < 360.0 {
        fill_arc(fb, RING_INNER_OFF, RING_OUTER, swept, 360.0 - swept, ZONE_DIM_HUE[4]);
    }
    if swept > 0.0 {
        fill_arc(fb, RING_INNER_OFF, RING_OUTER, 0.0, swept, RED);
    }

    draw_centered(fb, &heading, "DISCARD", 96, WHITE);
    draw_centered(fb, &label, "KEEP HOLDING", 126, DIM);
}

/// What happened, said plainly. Not "saved" and not an error: the wearer asked
/// for this, and the screen should agree with them rather than apologise.
fn draw_discarded(fb: &mut FrameBuf) {
    let heading = FontRenderer::new::<HeadingFont>();
    let label = FontRenderer::new::<LabelFont>();

    draw_centered(fb, &heading, "DISCARDED", 96, AMBER);
    draw_centered(fb, &label, "NOTHING WAS SAVED", 126, DIM);
    draw_hint(fb, HINT_RIGHT_X, HINT_BOTTOM_Y, HorizontalAlignment::Right, "DONE", WHITE);
}

/// A button label, drawn at the edge the button is on. Only ever drawn for a
/// button that does something on this screen, so a blank edge means that
/// button is inert rather than undocumented.
fn draw_hint(
    fb: &mut FrameBuf,
    x: i32,
    y: i32,
    align: HorizontalAlignment,
    text: &str,
    color: Abgr2222,
) {
    let label = FontRenderer::new::<LabelFont>();
    draw_text(fb, &label, text, x, y, align, color);
}

// -- Entry points ------------------------------------------------------------

pub fn render(buf: &mut [u8], width: u32, height: u32, frame: &Frame) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height };
    fb.buf.fill(BLACK.0);

    match frame.screen {
        SCREEN_RIDING | SCREEN_PAUSED => draw_riding(&mut fb, frame),
        SCREEN_SAVED => draw_saved(&mut fb, frame),
        SCREEN_CONFIRM_DISCARD => draw_confirm_discard(&mut fb, frame.hold_pct),
        SCREEN_DISCARDED => draw_discarded(&mut fb),
        // READY is the default rather than a fourth arm: an out-of-range
        // screen byte is a bug somewhere upstream, and the pre-ride screen is
        // the one that loses the wearer the least.
        _ => draw_ready(&mut fb, frame),
    }

}

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `frame` to a
/// valid `spin_gui_frame`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn spin_gui_render(
    buf: *mut u8,
    buf_len: u32,
    width: u16,
    height: u16,
    frame: *const Frame,
) {
    if buf.is_null() || frame.is_null() || buf_len == 0 || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, buf_len as usize);
    render(slice, width as u32, height as u32, &*frame);
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;

    fn frame(screen: u8) -> Frame {
        Frame { screen, ..Frame::default() }
    }

    fn draw(frame: &Frame) -> Vec<u8> {
        let mut buf = vec![0u8; (W * H) as usize];
        render(&mut buf, W, H, frame);
        buf
    }

    fn lit_pixels(buf: &[u8]) -> usize {
        buf.iter().filter(|&&b| b != BLACK.0).count()
    }

    #[test]
    fn duration_uses_the_shortest_unambiguous_form() {
        let mut buf = [0u8; 12];
        assert_eq!(format_duration(0, &mut buf), "0:00");
        assert_eq!(format_duration(9, &mut buf), "0:09");
        assert_eq!(format_duration(59, &mut buf), "0:59");
        assert_eq!(format_duration(60, &mut buf), "1:00");
        assert_eq!(format_duration(600, &mut buf), "10:00");
        assert_eq!(format_duration(3599, &mut buf), "59:59");
        // The hour boundary is where the minute field starts being padded --
        // "1:0:05" would be unreadable and "1:00:05" is the point of the split.
        assert_eq!(format_duration(3600, &mut buf), "1:00:00");
        assert_eq!(format_duration(3605, &mut buf), "1:00:05");
        assert_eq!(format_duration(3665, &mut buf), "1:01:05");
        assert_eq!(format_duration(36000, &mut buf), "10:00:00");
    }

    #[test]
    fn duration_saturates_rather_than_wrapping() {
        let mut buf = [0u8; 12];
        assert_eq!(format_duration(u32::MAX, &mut buf), "99:59:59");
    }

    #[test]
    fn the_clock_never_exceeds_the_panel() {
        // Every clock string this app can produce, at the face render_clock
        // would pick for it, has to fit inside CLOCK_MAX_W -- otherwise the
        // longest ride of the year is the one that draws off the edge.
        let faces = [
            (FontRenderer::new::<ClockXl>(), "xl"),
            (FontRenderer::new::<ClockL>(), "l"),
            (FontRenderer::new::<ClockM>(), "m"),
        ];
        for seconds in [0u32, 59, 60, 3599, 3600, 35999, 36000, 359_999, u32::MAX] {
            let mut buf = [0u8; 12];
            let text = format_duration(seconds, &mut buf);
            let chosen = faces
                .iter()
                .find(|(f, _)| text_width(f, text) <= CLOCK_MAX_W)
                .unwrap_or(&faces[2]);
            assert!(
                text_width(&chosen.0, text) <= CLOCK_MAX_W,
                "{text} does not fit even at the smallest face"
            );
        }
    }

    /// The panel is a circle inscribed in the 240x240 buffer -- the same test
    /// the preview binary applies when it blacks out the corners.
    fn inside_bezel(x: u32, y: u32) -> bool {
        let dx = 2 * x as i32 - (W as i32 - 1);
        let dy = 2 * y as i32 - (H as i32 - 1);
        dx * dx + dy * dy <= (W as i32) * (W as i32)
    }

    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        // Every scene, because this is a layout invariant and the scenes are
        // the layouts. The first version of the button hints failed this: they
        // were inset from the buffer's edge rather than the glass's, so the
        // last glyph of "START" and "PAUSE" was cut off by the bezel.
        for (name, frame) in scenes::scenes() {
            let buf = draw(&frame);
            for y in 0..H {
                for x in 0..W {
                    if !inside_bezel(x, y) {
                        assert_eq!(
                            buf[(y * W + x) as usize],
                            BLACK.0,
                            "{name} draws at ({x},{y}), outside the round panel"
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn every_screen_draws_something() {
        for screen in [SCREEN_READY, SCREEN_RIDING, SCREEN_PAUSED, SCREEN_SAVED] {
            let mut f = frame(screen);
            f.elapsed_s = 1234;
            f.hr_bpm = 142;
            f.avg_hr_bpm = 138;
            f.saved_ok = 1;
            assert!(lit_pixels(&draw(&f)) > 500, "screen {screen} drew almost nothing");
        }
    }

    #[test]
    fn render_is_a_pure_function_of_the_frame() {
        let mut f = frame(SCREEN_RIDING);
        f.elapsed_s = 754;
        f.hr_bpm = 131;
        f.hr_source = HR_EXTERNAL;
        assert_eq!(draw(&f), draw(&f));
    }

    #[test]
    fn a_missing_beat_is_not_drawn_as_a_number() {
        // The two have to be visibly different: "---" against a dim heart, not
        // a zero and not whatever the last beat was.
        let mut none = frame(SCREEN_RIDING);
        none.elapsed_s = 300;
        let mut zero_bpm = none;
        zero_bpm.hr_bpm = 0;
        assert_eq!(draw(&none), draw(&zero_bpm));

        let mut beating = none;
        beating.hr_bpm = 60;
        assert_ne!(draw(&none), draw(&beating));
    }

    #[test]
    fn the_strap_changes_the_heart_rather_than_the_layout() {
        // Same bpm from the strap and from the wrist: the number sits in the
        // same place, only the heart's colour differs. A layout that shifted
        // would make a dropout look like a different reading.
        let mut wrist = frame(SCREEN_RIDING);
        wrist.hr_bpm = 140;
        wrist.hr_source = HR_OPTICAL;
        let mut strap = wrist;
        strap.hr_source = HR_EXTERNAL;

        let (a, b) = (draw(&wrist), draw(&strap));
        assert_ne!(a, b, "the strap has to look different");

        let differing = a.iter().zip(b.iter()).filter(|(x, y)| x != y).count();
        let heart_area = (HEART_W * HEART_H) as usize;
        assert!(
            differing <= heart_area,
            "{differing} pixels changed; only the heart ({heart_area} px) should have"
        );
    }

    /// Just the rows the one banner slot occupies. The banner tests compare
    /// this rather than the whole frame: the target arc at the bottom of the
    /// ring legitimately differs between these cases, and a whole-frame
    /// comparison would call that a banner change.
    fn rows(buf: &[u8], y0: u32, y1: u32) -> Vec<u8> {
        buf[(y0 * W) as usize..(y1 * W) as usize].to_vec()
    }

    fn banner_rows(buf: &[u8]) -> Vec<u8> {
        rows(buf, PAUSED_BANNER_Y as u32, PAUSED_BANNER_Y as u32 + 16)
    }

    #[test]
    fn a_pause_outranks_the_target_for_the_one_banner_slot() {
        // Both want the same row. A wearer who pauses after passing the target
        // needs to be told the clock is stopped, not congratulated again.
        let mut riding_met = frame(SCREEN_RIDING);
        riding_met.elapsed_s = 1801;
        riding_met.target_minutes = 30;
        riding_met.target_reached = 1;

        let paused_met = Frame { screen: SCREEN_PAUSED, ..riding_met };
        let paused_plain = Frame { target_minutes: 0, target_reached: 0, ..paused_met };

        // Paused says the same thing whether or not the target was met...
        assert_eq!(banner_rows(&draw(&paused_met)), banner_rows(&draw(&paused_plain)));
        // ...and it is not what riding-with-the-target-met says.
        assert_ne!(banner_rows(&draw(&riding_met)), banner_rows(&draw(&paused_met)));
    }

    #[test]
    fn the_banner_never_infers_that_the_target_was_met() {
        // The Service owns "reached" -- it is the flag that fired the buzz --
        // so the renderer must not conclude it from the clock passing the
        // target. Same elapsed, same target, different flag.
        let mut past_it = frame(SCREEN_RIDING);
        past_it.elapsed_s = 4000; // well past 30 minutes
        past_it.target_minutes = 30;
        past_it.hr_bpm = 140;

        let told = Frame { target_reached: 1, ..past_it };

        assert_ne!(banner_rows(&draw(&past_it)), banner_rows(&draw(&told)),
                   "the flag should be what puts TARGET MET on the screen");

        let no_target = Frame { target_minutes: 0, ..past_it };
        assert_eq!(banner_rows(&draw(&past_it)), banner_rows(&draw(&no_target)),
                   "a target that has not been reached should say nothing");
    }

    #[test]
    fn the_target_arc_tracks_progress() {
        // The arc in the ring's opening is the only thing that changes here, so
        // a whole-frame comparison is the right one.
        let base = {
            let mut f = frame(SCREEN_RIDING);
            f.target_minutes = 30;
            f.has_zones = 1;
            f.hr_zone = 3;
            f
        };
        let none = Frame { elapsed_s: 0, ..base };
        let part = Frame { elapsed_s: 900, ..base };   // half
        let full = Frame { elapsed_s: 1800, target_reached: 1, ..base };

        assert_ne!(draw(&none), draw(&part));
        assert_ne!(draw(&part), draw(&full));

        // More elapsed never draws less arc.
        let lit = |f: &Frame| draw(f).iter().filter(|&&b| b != BLACK.0).count();
        assert!(lit(&none) < lit(&part) && lit(&part) < lit(&full));

        // Past the target it stops growing rather than wrapping round. Compared
        // over the arc's own rows only: elapsed_s also drives the clock, and
        // 30:00 and 2:46:39 are legitimately different pictures.
        let over = Frame { elapsed_s: 9999, target_reached: 1, ..base };
        assert_eq!(rows(&draw(&full), 200, 240), rows(&draw(&over), 200, 240));
    }

    #[test]
    fn no_target_leaves_the_ring_open() {
        // The gap at the bottom is what makes it read as a speedometer rather
        // than a closed circle, so nothing draws there without a target.
        let mut f = frame(SCREEN_RIDING);
        f.has_zones = 1;
        f.hr_zone = 3;
        f.elapsed_s = 600;
        let with_target = Frame { target_minutes: 30, ..f };
        assert_ne!(draw(&f), draw(&with_target));
    }

    #[test]
    fn a_failed_save_does_not_read_as_a_saved_one() {
        let mut ok = frame(SCREEN_SAVED);
        ok.elapsed_s = 1800;
        ok.avg_hr_bpm = 140;
        ok.saved_ok = 1;
        let mut failed = ok;
        failed.saved_ok = 0;
        assert_ne!(draw(&ok), draw(&failed));
    }

    #[test]
    fn the_hold_ring_fills_with_the_hold() {
        let at = |pct: u8| {
            let mut f = frame(SCREEN_CONFIRM_DISCARD);
            f.hold_pct = pct;
            draw(&f)
        };
        // Counting bright pixels, not lit ones: the dim track is drawn for the
        // whole circle whatever the progress, so the number of non-black pixels
        // never changes -- only how many of them are filled.
        let filled = |b: &Vec<u8>| b.iter().filter(|&&x| x == RED.0).count();

        // More hold, more ring. Never less.
        assert!(filled(&at(0)) < filled(&at(50)),
                "no progress drawn: {} then {}", filled(&at(0)), filled(&at(50)));
        assert!(filled(&at(50)) < filled(&at(100)));

        // Past full it stops rather than wrapping round for a second lap.
        assert_eq!(at(100), at(255));
    }

    #[test]
    fn the_hold_ring_has_no_gaps() {
        // The guard on ARC_ANGULAR_PX / ARC_RADIAL_PX. Loosening the sampling
        // is only safe while consecutive samples still land on the same pixel
        // or its neighbour; too coarse and the ring grows holes that no other
        // test would notice, because it would still be lit, still be round and
        // still be the right colour.
        //
        // The fully-held discard ring is the case to check: a complete circle,
        // so every angle is covered and any gap is the sampling's fault rather
        // than a segment boundary's.
        let mut f = frame(SCREEN_CONFIRM_DISCARD);
        f.hold_pct = 100;
        let buf = draw(&f);

        // A pixel is "inside the band" only if its whole 1x1 area is, so the
        // outermost and innermost rows are excluded: those legitimately
        // straddle the edge and may or may not be painted.
        let lo = RING_INNER_OFF + 1.0;
        let hi = RING_OUTER - 1.0;
        let mut holes = 0;
        for y in 0..H {
            for x in 0..W {
                let dx = x as f32 - RING_CX;
                let dy = y as f32 - RING_CY;
                let r = (dx * dx + dy * dy).sqrt();
                if r >= lo && r <= hi && buf[(y * W + x) as usize] == BLACK.0 {
                    holes += 1;
                }
            }
        }
        assert_eq!(holes, 0, "{holes} unpainted pixels inside the ring band");
    }

    #[test]
    fn a_discarded_ride_does_not_read_as_a_failed_save() {
        // The wearer asked for one of these and not the other, and the screen
        // has to agree with them rather than apologise.
        let discarded = frame(SCREEN_DISCARDED);
        let mut failed = frame(SCREEN_SAVED);
        failed.elapsed_s = 2712;
        failed.saved_ok = 0;
        assert_ne!(draw(&discarded), draw(&failed));
    }

    #[test]
    fn an_unknown_screen_falls_back_to_ready() {
        assert_eq!(draw(&frame(200)), draw(&frame(SCREEN_READY)));
    }

    #[test]
    fn render_refuses_a_buffer_it_would_overrun() {
        let mut small = vec![0xAAu8; 100];
        render(&mut small, W, H, &frame(SCREEN_RIDING));
        assert!(small.iter().all(|&b| b == 0xAA), "render wrote past the buffer");
    }

    #[test]
    fn every_pixel_is_a_colour_the_panel_can_hold() {
        // ABGR2222 with an opaque alpha: anything else would be quantised on
        // the way to the glass and would not look like what the test saw.
        let mut f = frame(SCREEN_RIDING);
        f.elapsed_s = 3661;
        f.hr_bpm = 155;
        f.hr_source = HR_EXTERNAL;
        for byte in draw(&f) {
            assert_eq!(byte >> 6, 0b11, "pixel 0x{byte:02X} is not opaque");
        }
    }
}
