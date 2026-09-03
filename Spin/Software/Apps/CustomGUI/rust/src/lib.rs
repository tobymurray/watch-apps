//! The Spin app's renderer, as a pure function of one `Frame`. Layout
//! reasoning: `Spin/README.md`.
//!
//! PANEL. 240x240, round, 8bpp `ABGR2222` — four levels a channel, so every
//! colour here is one of 0/85/170/255 and anything else is quantised on the
//! way to the glass. The corners of the square buffer sit behind the bezel.
//! Falsified by a different display; `nothing_is_drawn_outside_the_bezel`
//! is what holds the round part.

// `cargo test --features std`: the crate is no_std, and a test binary cannot
// link one whose panics do not unwind.
#![cfg_attr(not(feature = "std"), no_std)]

use embedded_graphics::{pixelcolor::raw::RawU8, pixelcolor::PixelColor, prelude::*};
#[cfg(not(feature = "std"))]
use micromath::F32Ext;
use textkit::{faces, Align, Canvas, Face};

#[cfg(not(feature = "std"))]
extern "C" {
    fn spin_gui_host_panic(msg: *const u8, len: u32);
}

/// Without this a panic hangs the GUI thread silently, and the only way out is
/// a reboot with a ride in progress.
#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"panic";
    unsafe { spin_gui_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// The frames worth looking at. Host-only: the watch is handed frames.
#[cfg(feature = "std")]
pub mod scenes;

pub mod work;

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
/// The dimmest usable grey: 85 is the only other non-black level and it washes
/// out in daylight on this reflective panel.
const DIM: Abgr2222 = Abgr2222::rgb(170, 170, 170);
/// Heart rate, and only heart rate. Nothing else on any screen is red.
const RED: Abgr2222 = Abgr2222::rgb(255, 0, 0);
/// Held / not-what-you-wanted. Paused, and a ride that failed to save.
const AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
/// Most zones the dial will draw, matching the ceiling of the kernel's own
/// threshold table (`RequestSystemSettings::skMaxHearRateTh`).
pub const MAX_ZONES: usize = 8;

/// The colour ladder, cool to warm, eight stops.
const HUE_GREY: Abgr2222 = Abgr2222::rgb(170, 170, 170);
const HUE_BLUE: Abgr2222 = Abgr2222::rgb(0, 170, 255);
const HUE_CYAN: Abgr2222 = Abgr2222::rgb(0, 255, 255);
const HUE_GREEN: Abgr2222 = Abgr2222::rgb(0, 255, 0);
const HUE_YELLOW: Abgr2222 = Abgr2222::rgb(255, 255, 0);
const HUE_AMBER: Abgr2222 = Abgr2222::rgb(255, 170, 0);
const HUE_ORANGE: Abgr2222 = Abgr2222::rgb(255, 85, 0);
const HUE_RED: Abgr2222 = Abgr2222::rgb(255, 0, 0);

/// Which hues a dial of N zones uses, written out per count rather than sampled
/// by formula; every row runs grey to red. With four levels a channel these are
/// not approximations of nicer colours, they are the colours.
const ZONE_HUES: [&[Abgr2222]; MAX_ZONES + 1] = [
    &[],                                                        // 0 - no dial
    &[HUE_RED],                                                 // 1 - not offered
    &[HUE_GREY, HUE_RED],                                       // 2
    &[HUE_GREY, HUE_AMBER, HUE_RED],                            // 3  polarised
    &[HUE_GREY, HUE_GREEN, HUE_AMBER, HUE_RED],                 // 4
    &[HUE_GREY, HUE_BLUE, HUE_GREEN, HUE_AMBER, HUE_RED],       // 5  the classic
    &[HUE_GREY, HUE_BLUE, HUE_GREEN, HUE_YELLOW, HUE_AMBER, HUE_RED],            // 6
    &[HUE_GREY, HUE_BLUE, HUE_CYAN, HUE_GREEN, HUE_YELLOW, HUE_AMBER, HUE_RED],  // 7
    &[HUE_GREY, HUE_BLUE, HUE_CYAN, HUE_GREEN, HUE_YELLOW, HUE_AMBER, HUE_ORANGE,
      HUE_RED],                                                                  // 8
];

/// Dims a hue one level on every channel it uses, so an inactive segment reads
/// as the same zone, quieter, rather than as a different colour.
fn dim(c: Abgr2222) -> Abgr2222 {
    let step = |shift: u8| {
        let v = (c.0 >> shift) & CHANNEL_MASK;
        if v > 0 { v - 1 } else { 0 }
    };
    Abgr2222::from_levels(step(RED_SHIFT), step(GREEN_SHIFT), step(BLUE_SHIFT))
}

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

/// A direct row-fill: `Rectangle::into_styled().draw()` would pull in the
/// point-iterator layer once per distinct colour it is called with.
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
// Mirrors spin_gui_frame (spin_gui.h); the fingerprint below is what checks it.

pub const SCREEN_READY: u8 = 0;
pub const SCREEN_RIDING: u8 = 1;
pub const SCREEN_PAUSED: u8 = 2;
pub const SCREEN_SAVED: u8 = 3;
pub const SCREEN_CONFIRM_DISCARD: u8 = 4;
pub const SCREEN_DISCARDED: u8 = 5;
pub const SCREEN_ENTER_WORK: u8 = 6;

pub const STRAP_ABSENT: u8 = 0;
pub const STRAP_SEARCHING: u8 = 1;
pub const STRAP_CONNECTED: u8 = 2;

pub const HR_NONE: u8 = 0;
pub const HR_OPTICAL: u8 = 1;
pub const HR_EXTERNAL: u8 = 2;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Frame {
    pub elapsed_s: u32,
    pub hr_bpm: u16,
    pub avg_hr_bpm: u16,
    pub target_minutes: u16,
    pub energy: u16,
    /// kJ; 0 = nothing said yet.
    pub work_kj: u16,
    /// kJ the calorie model suggests; 0 = draw no reference.
    pub work_estimate_kj: u16,
    pub screen: u8,
    pub strap: u8,
    pub hr_source: u8,
    pub saved_ok: u8,
    pub target_reached: u8,
    pub hr_zone: u8,
    pub zone_count: u8,
    pub has_zones: u8,
    pub energy_is_kj: u8,
    pub hr_zone_fraction: u8,
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `spin_gui_abi::fingerprint()`.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Frame>());
    let h = fnv1a(h, core::mem::align_of::<Frame>());
    let h = fnv1a(h, core::mem::offset_of!(Frame, elapsed_s));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_bpm));
    let h = fnv1a(h, core::mem::offset_of!(Frame, avg_hr_bpm));
    let h = fnv1a(h, core::mem::offset_of!(Frame, target_minutes));
    let h = fnv1a(h, core::mem::offset_of!(Frame, energy));
    let h = fnv1a(h, core::mem::offset_of!(Frame, work_kj));
    let h = fnv1a(h, core::mem::offset_of!(Frame, work_estimate_kj));
    let h = fnv1a(h, core::mem::offset_of!(Frame, screen));
    let h = fnv1a(h, core::mem::offset_of!(Frame, strap));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_source));
    let h = fnv1a(h, core::mem::offset_of!(Frame, saved_ok));
    let h = fnv1a(h, core::mem::offset_of!(Frame, target_reached));
    let h = fnv1a(h, core::mem::offset_of!(Frame, hr_zone));
    let h = fnv1a(h, core::mem::offset_of!(Frame, zone_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, has_zones));
    let h = fnv1a(h, core::mem::offset_of!(Frame, energy_is_kj));
    fnv1a(h, core::mem::offset_of!(Frame, hr_zone_fraction))
}

#[no_mangle]
pub extern "C" fn spin_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Frame>() == 28);
const _: () = assert!(core::mem::align_of::<Frame>() == 4);
const _: () = assert!(core::mem::offset_of!(Frame, elapsed_s) == 0);
const _: () = assert!(core::mem::offset_of!(Frame, hr_bpm) == 4);
const _: () = assert!(core::mem::offset_of!(Frame, avg_hr_bpm) == 6);
const _: () = assert!(core::mem::offset_of!(Frame, target_minutes) == 8);
const _: () = assert!(core::mem::offset_of!(Frame, energy) == 10);
const _: () = assert!(core::mem::offset_of!(Frame, work_kj) == 12);
const _: () = assert!(core::mem::offset_of!(Frame, work_estimate_kj) == 14);
const _: () = assert!(core::mem::offset_of!(Frame, screen) == 16);
const _: () = assert!(core::mem::offset_of!(Frame, strap) == 17);
const _: () = assert!(core::mem::offset_of!(Frame, hr_source) == 18);
const _: () = assert!(core::mem::offset_of!(Frame, saved_ok) == 19);
const _: () = assert!(core::mem::offset_of!(Frame, target_reached) == 20);
const _: () = assert!(core::mem::offset_of!(Frame, hr_zone) == 21);
const _: () = assert!(core::mem::offset_of!(Frame, zone_count) == 22);
const _: () = assert!(core::mem::offset_of!(Frame, has_zones) == 23);
const _: () = assert!(core::mem::offset_of!(Frame, energy_is_kj) == 24);
const _: () = assert!(core::mem::offset_of!(Frame, hr_zone_fraction) == 25);

// -- Geometry ----------------------------------------------------------------

const PANEL_W: i32 = 240;
const PANEL_H: i32 = 240;
const CENTER_X: i32 = PANEL_W / 2;

// -- Button hints -----------------------------------------------------------
// HARDWARE: the four buttons are at the corners of the bezel, so a hint belongs
// on its button's diagonal. Falsified by a differently laid out watch.
// Why words appear on some and not others: Spin/README.md.

/// Clockwise from twelve o'clock, matching the bezel: L1 top-left, R1
/// top-right, L2 bottom-left, R2 bottom-right (CommandMessages.hpp).
const BUTTON_L1_DEG: f32 = -45.0;
const BUTTON_R1_DEG: f32 = 45.0;
const BUTTON_R2_DEG: f32 = 135.0;
const BUTTON_L2_DEG: f32 = 225.0;

/// The mark: inboard of the zone ring, so the two never touch.
const TICK_INNER: f32 = 98.0;
const TICK_OUTER: f32 = 105.0;
const TICK_SWEEP_DEG: f32 = 17.0;

/// Gap between a mark and its word, measured from the mark's edge rather than
/// from the centre: on the diagonal the clock's corner and the mark are a few
/// pixels apart, and a radius lands on one or the other.
const LABEL_GAP: i32 = 8;

const HEADING_Y: i32 = 50;

fn polar(radius: f32, deg: f32) -> (i32, i32) {
    let a = deg * DEG_TO_RAD;
    (
        (RING_CX + radius * a.sin() + 0.5) as i32,
        (RING_CY - radius * a.cos() + 0.5) as i32,
    )
}

/// The mark alone: this button does something.
fn draw_button_tick(fb: &mut FrameBuf, deg: f32, color: Abgr2222) {
    fill_arc(fb, TICK_INNER, TICK_OUTER, deg - TICK_SWEEP_DEG / 2.0, TICK_SWEEP_DEG, color);
}

/// The mark plus what it does. The word grows away from its own edge -- right
/// buttons right-aligned, left buttons left-aligned -- so it always reads as
/// hanging off that side rather than floating in the middle.
/// Where a word hangs off its mark: x, the alignment, and the mark's own row.
///
/// Both ends of the arc are evaluated, because which one is nearer depends on
/// the quadrant; assuming it runs the word straight through the mark.
fn hint_anchor(deg: f32) -> (i32, Align, i32) {
    let (ax, _) = polar(TICK_INNER, deg - TICK_SWEEP_DEG / 2.0);
    let (bx, _) = polar(TICK_INNER, deg + TICK_SWEEP_DEG / 2.0);
    let (_, ty) = polar(TICK_INNER, deg);
    if (deg * DEG_TO_RAD).sin() > 0.0 {
        (ax.min(bx) - LABEL_GAP, Align::Right, ty)
    } else {
        (ax.max(bx) + LABEL_GAP, Align::Left, ty)
    }
}

fn draw_button_hint(fb: &mut FrameBuf, deg: f32, text: &str, color: Abgr2222) {
    draw_button_tick(fb, deg, color);
    let label = label_face();
    let (x, align, ty) = hint_anchor(deg);
    // Lifted half a line so the word straddles the mark.
    draw_text(fb, &label, text, x, ty - 6, align, color);
}

/// A hint in the answer face. Only the discard screen uses it.
fn draw_button_answer(fb: &mut FrameBuf, deg: f32, text: &str, color: Abgr2222) {
    draw_button_tick(fb, deg, color);
    let answer = bold_face(ANSWER_H);
    let (x, align, ty) = hint_anchor(deg);
    draw_text(fb, &answer, text, x, ty - 9, align, color);
}

const CLOCK_Y: i32 = 70;
/// Identical on both riding screens, so the number does not jump on a pause.
const HR_ROW_Y: i32 = 138;
/// All four buttons are labelled on the entry screen, leaving about forty
/// pixels between "+100" and "SAVE" -- not a heading's worth -- so the whole
/// block sits below the top hint row.
const ENTER_WORK_HEADING_Y: i32 = 66;
const ENTER_WORK_VALUE_Y: i32 = 90;
const ENTER_WORK_ESTIMATE_Y: i32 = 148;

/// Not on the hint row: a centred word there collides with the right-hand
/// label, and "PAUSED"/"RESUME" are the pair that do it.
const PAUSED_BANNER_Y: i32 = 116;

// -- Fonts -------------------------------------------------------------------
// TextKit's pre-rendered Poppins atlases. Which faces and why: Spin/README.md
// "Text", and Docs/TEXT.md at the repo root for the measurements.

static LABEL: &Face = &faces::REGULAR_16_LATIN;
static HEADING: &Face = &faces::SEMIBOLD_18_ASCII;
static ANSWER: &Face = &faces::SEMIBOLD_24_ANSWERS;
static TITLE: &Face = &faces::SEMIBOLD_32_TITLE;
static NUMBER: &Face = &faces::SEMIBOLD_27_CLOCK;
static CLOCK_M: &Face = &faces::SEMIBOLD_36_CLOCK;
static CLOCK_L: &Face = &faces::SEMIBOLD_49_CLOCK;
static CLOCK_XL: &Face = &faces::SEMIBOLD_60_CLOCK;

/// The heights the screens ask for, as the u8g2 build named them; each picks
/// the Poppins face whose capital height matches, so no layout constant moved.
const CLOCK_XL_H: i32 = 54;
const CLOCK_L_H: i32 = 44;
const CLOCK_M_H: i32 = 32;
const NUMBER_H: i32 = 25;
const TITLE_H: i32 = 32;
const HEADING_H: i32 = 18;
/// The discard answers, and only those. Bold and half again the label size,
/// because they are the one place where a misread costs the wearer their ride.
const ANSWER_H: i32 = 24;

/// Widest the clock may draw: the round panel's chord at the clock's own rows,
/// not the full 240.
const CLOCK_MAX_W: u32 = 218;

fn number_face(dst_h: i32) -> &'static Face {
    match dst_h {
        CLOCK_XL_H => CLOCK_XL,
        CLOCK_L_H => CLOCK_L,
        CLOCK_M_H => CLOCK_M,
        _ => NUMBER,
    }
}

fn bold_face(dst_h: i32) -> &'static Face {
    match dst_h {
        TITLE_H => TITLE,
        ANSWER_H => ANSWER,
        _ => HEADING,
    }
}

fn label_face() -> &'static Face {
    LABEL
}

/// Poppins' capital height is 0.7 em, so a screen that names a top gets its
/// capitals starting there, as they did when the u8g2 faces were placed by top.
fn cap_height(face: &Face) -> i32 {
    (face.px as i32 * 7 + 5) / 10
}

fn text_width(face: &Face, s: &str) -> u32 {
    face.measure(s).advance.max(0) as u32
}

fn draw_text(fb: &mut FrameBuf, face: &Face, s: &str, x: i32, top: i32, align: Align, color: Abgr2222) {
    let mut canvas = Canvas::round(fb.buf, fb.w, fb.h);
    face.draw(&mut canvas, s, x, top + cap_height(face), align, color.0);
}

fn draw_centered(fb: &mut FrameBuf, face: &Face, s: &str, y: i32, color: Abgr2222) {
    draw_text(fb, face, s, CENTER_X, y, Align::Center, color);
}

// -- Number formatting -------------------------------------------------------
// No allocator, so every string is built into a caller-owned buffer.

/// Writes `value` as decimal into the end of `buf`, returning just the digits.
/// Unpadded; the one caller that needs padding does it itself.
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
// Hand-drawn because `_tr` has no heart glyph. Bit 0 of each row is leftmost.

const HEART_W: i32 = 15;
const HEART_H: i32 = 13;
/// Explicit because the font renderer measures ink, not advance: a space inside
/// a string contributes no width it will report.
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

/// Red from the strap, white from the wrist, dim with no beat -- the colour is
/// the whole legend, and nothing else on any screen is red.
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
        // Not an error: the wrist sensor is a heart rate monitor too.
        _ => "WRIST SENSOR",
    }
}

fn draw_ready(fb: &mut FrameBuf, frame: &Frame) {
    let title = bold_face(TITLE_H);
    let label = label_face();

    // Centred between the hint rows, so it starts higher when a target line has
    // to fit; a fixed top runs the target case into the EXIT hint.
    let has_target = frame.target_minutes > 0;
    let top = if has_target { 70 } else { 84 };

    draw_centered(fb, &title, "SPIN", top, WHITE);

    // A rule the width of the word, so the strap line does not read as a
    // subtitle of the app name.
    let w = text_width(&title, "SPIN") as i32;
    fill_rect(fb, CENTER_X - w / 2, top + 38, w, 2, DIM);

    draw_zone_ring(fb, 0, frame.zone_count, frame.has_zones != 0);
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
        Align::Left,
        if connected { WHITE } else { DIM },
    );

    // Only when a target is set: a "no target" row would be a permanent line
    // about a feature most rides do not use.
    if has_target {
        let y = top + 78;
        let mut buf = [0u8; 12];
        let minutes = u32_to_str(frame.target_minutes as u32, &mut buf);
        let label_w = text_width(&label, "TARGET") as i32;
        let value_w = text_width(&label, minutes) as i32;
        let unit_w = text_width(&label, "MIN") as i32;
        let left = CENTER_X - (label_w + WORD_GAP + value_w + WORD_GAP + unit_w) / 2;
        let value_x = left + label_w + WORD_GAP;
        draw_text(fb, &label, "TARGET", left, y, Align::Left, DIM);
        draw_text(fb, &label, minutes, value_x, y, Align::Left, WHITE);
        draw_text(fb, &label, "MIN", value_x + value_w + WORD_GAP, y,
                  Align::Left, DIM);
    }

    draw_button_hint(fb, BUTTON_R1_DEG, "START", WHITE);
    draw_button_hint(fb, BUTTON_R2_DEG, "EXIT", DIM);
}

/// Draws the clock at the largest face that fits, returning the height it used.
fn render_clock(fb: &mut FrameBuf, text: &str, y: i32, color: Abgr2222) -> i32 {
    for h in [CLOCK_XL_H, CLOCK_L_H] {
        let face = number_face(h);
        if text_width(&face, text) <= CLOCK_MAX_W {
            draw_centered(fb, &face, text, y, color);
            return h;
        }
    }
    let face = number_face(CLOCK_M_H);
    draw_centered(fb, &face, text, y, color);
    CLOCK_M_H
}

fn draw_riding(fb: &mut FrameBuf, frame: &Frame) {
    let label = label_face();
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
    draw_zone_ring(fb, frame.hr_zone, frame.zone_count, frame.has_zones != 0);
    draw_zone_needle(fb, frame.hr_zone, frame.hr_zone_fraction, frame.zone_count,
                     frame.has_zones != 0);
    draw_target_arc(fb, frame.elapsed_s, frame.target_minutes, frame.target_reached != 0);

    if paused {
        // Resume gets the mark alone: "SAVE" and "RESUME" do not both fit on
        // this band, with nothing left to trim.
        draw_button_hint(fb, BUTTON_L1_DEG, "SAVE", AMBER);
        draw_button_tick(fb, BUTTON_R1_DEG, WHITE);
        draw_button_hint(fb, BUTTON_L2_DEG, "DISCARD", RED);
    } else {
        // Two live buttons now, doing different things. R1 keeps the mark
        // alone: pausing is unchanged and already learned, and a word there
        // costs the clock its space. R2 is the new one, so it is the one that
        // needs naming -- the labelled button is the unfamiliar button.
        draw_button_tick(fb, BUTTON_R1_DEG, WHITE);
        draw_button_hint(fb, BUTTON_R2_DEG, "LAP", DIM);
    }
}

// -- The zone ring ----------------------------------------------------------

/// Centre in the half-pixel sense: a 240-wide row has its middle between pixel
/// 119 and 120, and a ring drawn about 120.0 shows a half-pixel lopsided rim.
const RING_CX: f32 = (PANEL_W as f32 - 1.0) / 2.0;
const RING_CY: f32 = (PANEL_H as f32 - 1.0) / 2.0;

const RING_OUTER: f32 = 116.0;
/// The active segment grows inward to RING_INNER_ON, giving the lit zone weight
/// as well as brightness -- on a reflective panel in bad light colour alone is
/// thin.
const RING_INNER_OFF: f32 = 107.0;
const RING_INNER_ON: f32 = 100.0;

/// Clockwise from twelve o'clock, leaving the bottom 90 degrees open -- which
/// is where the button labels live.
const RING_START_DEG: f32 = -135.0;
const RING_SWEEP_DEG: f32 = 270.0;
/// Gap between segments, in degrees, so five arcs read as five.
const RING_GAP_DEG: f32 = 3.0;

/// The scale's open bottom, less a margin so the target arc never touches the
/// zone arcs it sits between.
const TARGET_START_DEG: f32 = 223.0;
const TARGET_SWEEP_DEG: f32 = 86.0;
/// Thinner than the zone arcs and inside them: a second reading, not a rival.
const TARGET_OUTER: f32 = 114.0;
const TARGET_INNER: f32 = 108.0;

const DEG_TO_RAD: f32 = core::f32::consts::PI / 180.0;

/// Sample pitch along the arc and along the radius. The samples are a grid of
/// this pitch over the pixel grid at an arbitrary rotation, so the limit is the
/// diagonal, not 1.0: past 1/sqrt(2) ~= 0.707 a pixel can fall between four
/// samples and never be painted.
///
/// MEASURED: 0.85 leaves 95 unpainted pixels in the ring, 0.75 leaves 4, 0.70
/// is clean -- the geometry, exactly. 0.65 is that with margin. Re-measure by
/// filling an arc and counting unpainted pixels inside it.
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
    // Sized so the outer edge, where runs are furthest apart, moves by
    // ARC_ANGULAR_PX.
    let step = ARC_ANGULAR_PX / r_outer;
    let start = start_deg * DEG_TO_RAD;
    let end = start + sweep_deg * DEG_TO_RAD;

    let mut a = start;
    while a <= end {
        // Zero is twelve o'clock, clockwise; panel y grows downward, hence -cos.
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

/// Longer than the ring is thick so most of it lies over black whatever colour
/// the segment under it is, and outboard of TICK_OUTER so it can never share
/// pixels with a button mark -- zone 4's arc runs through R1's corner.
///
/// 119, not 120: the bezel is the circle of radius 120, and a needle drawn to
/// it loses its tip to the glass.
const NEEDLE_INNER: f32 = 106.0;
const NEEDLE_OUTER: f32 = 119.0;
const NEEDLE_SWEEP_DEG: f32 = 2.2;
const NEEDLE_SLOT_DEG: f32 = 5.4;

/// Position within the lit segment, so the needle cannot drift into the gap
/// between two of them the way a whole-ring position does.
fn zone_needle_deg(zone: u8, fraction: u8, count: u8) -> f32 {
    let n = count as f32;
    let segment = (RING_SWEEP_DEG - RING_GAP_DEG * (n - 1.0)) / n;
    let start = RING_START_DEG + (segment + RING_GAP_DEG) * (zone - 1) as f32;
    start + segment * (fraction as f32) / 255.0
}

fn draw_zone_needle(fb: &mut FrameBuf, zone: u8, fraction: u8, count: u8, has_zones: bool) {
    // The same bounds draw_zone_ring uses, so a needle can never appear on a
    // dial that was not drawn. Below zone 1 there is nowhere honest to point.
    if !has_zones || count < 2 || count as usize > MAX_ZONES || zone < 1 || zone > count {
        return;
    }
    let deg = zone_needle_deg(zone, fraction, count);
    fill_arc(fb, NEEDLE_INNER, NEEDLE_OUTER, deg - NEEDLE_SLOT_DEG / 2.0,
             NEEDLE_SLOT_DEG, BLACK);
    fill_arc(fb, NEEDLE_INNER, NEEDLE_OUTER, deg - NEEDLE_SWEEP_DEG / 2.0,
             NEEDLE_SWEEP_DEG, WHITE);
}

/// The zone arcs, `count` of them. Drawn whenever thresholds are set, whatever
/// zone the wearer is in; zone 0 lights none of them.
fn draw_zone_ring(fb: &mut FrameBuf, zone: u8, count: u8, has_zones: bool) {
    if !has_zones || count < 2 || count as usize > MAX_ZONES {
        return;
    }
    let hues = ZONE_HUES[count as usize];

    let n = count as f32;
    let segment = (RING_SWEEP_DEG - RING_GAP_DEG * (n - 1.0)) / n;

    for i in 0..count {
        let start = RING_START_DEG + (segment + RING_GAP_DEG) * i as f32;
        let hue = hues[i as usize];
        let active = zone == i + 1;
        let (inner, color) = if active { (RING_INNER_ON, hue) } else { (RING_INNER_OFF, dim(hue)) };
        fill_arc(fb, inner, RING_OUTER, start, segment, color);
    }
}

/// Progress toward the target, in the opening the zone scale leaves at the
/// bottom, growing left to right to meet where the zones end. Nothing is drawn
/// without a target, which is what keeps the ring a speedometer.
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
                 remaining, dim(HUE_GREY));
    }
    if swept > 0.0 {
        fill_arc(fb, TARGET_INNER, TARGET_OUTER, TARGET_START_DEG - swept, swept, WHITE);
    }
}

/// Heart, number, unit, centred as one group so the row stays put when the bpm
/// goes from two digits to three.
fn draw_hr_row(fb: &mut FrameBuf, bpm: u16, hr_source: u8, y: i32) {
    let number = number_face(NUMBER_H);
    let label = label_face();

    let has_beat = bpm > 0;
    let mut buf = [0u8; 12];
    // Dashes, not a zero: only one of "no reading" and "a reading of zero" can
    // happen, and they must not look alike.
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
        draw_text(fb, &number, text, text_x, y - 2, Align::Left, WHITE);
    } else {
        draw_text(fb, &label, text, text_x, y + 4, Align::Left, DIM);
    }
    draw_text(
        fb,
        &label,
        "BPM",
        text_x + number_w + WORD_GAP,
        y + 8,
        Align::Left,
        DIM,
    );
}

fn draw_saved(fb: &mut FrameBuf, frame: &Frame) {
    let heading = bold_face(HEADING_H);
    let label = label_face();

    // saved_ok is ActivityWriter::stop()'s durability contract, so this word is
    // a fact about the filesystem rather than a reassurance.
    let ok = frame.saved_ok != 0;
    draw_centered(
        fb,
        &heading,
        if ok { "SAVED" } else { "NOT SAVED" },
        HEADING_Y,
        if ok { WHITE } else { AMBER },
    );

    let mut buf = [0u8; 12];
    let text = format_duration(frame.elapsed_s, &mut buf);
    render_clock(fb, text, CLOCK_Y, WHITE);

    // Two rows: three digits and a unit each do not share a line at this size.
    if frame.avg_hr_bpm > 0 {
        let mut hr_buf = [0u8; 12];
        let hr = u32_to_str(frame.avg_hr_bpm as u32, &mut hr_buf);
        draw_value_row(fb, &label, "AVG", hr, "BPM", 132, WHITE);
    } else {
        draw_centered(fb, &label, "NO HEART RATE", 132, DIM);
    }

    // Always drawn: 0 is a real answer for a very short ride, and an absent row
    // would read as a broken estimate rather than a small one.
    let mut energy_buf = [0u8; 12];
    let energy = u32_to_str(frame.energy as u32, &mut energy_buf);
    let unit = if frame.energy_is_kj != 0 { "KJ" } else { "KCAL" };
    draw_value_row(fb, &label, "", energy, unit, 156, WHITE);

    draw_button_hint(fb, BUTTON_R2_DEG, "DONE", WHITE);
}

/// `prefix value unit`, centred as one group, the words around the value dim.
/// Gaps are explicit because the renderer measures ink, not advance.
fn draw_value_row(
    fb: &mut FrameBuf,
    label: &Face,
    prefix: &str,
    value: &str,
    unit: &str,
    y: i32,
    value_color: Abgr2222,
) {
    let prefix_w = if prefix.is_empty() { 0 } else { text_width(label, prefix) as i32 };
    let value_w = text_width(label, value) as i32;
    let unit_w = text_width(label, unit) as i32;

    let lead = if prefix.is_empty() { 0 } else { prefix_w + WORD_GAP };
    let left = CENTER_X - (lead + value_w + WORD_GAP + unit_w) / 2;

    if !prefix.is_empty() {
        draw_text(fb, label, prefix, left, y, Align::Left, DIM);
    }
    let value_x = left + lead;
    draw_text(fb, label, value, value_x, y, Align::Left, value_color);
    draw_text(fb, label, unit, value_x + value_w + WORD_GAP, y,
              Align::Left, DIM);
}

/// Asks before destroying a ride, with both answers on labelled buttons.
///
/// HARDWARE, and why this is not a press-and-hold: Spin sets
/// `enMusicControl = true` in `Service::setCapabilities()`, the system claims
/// the long press to open its overlay, and `HOLD_1S` never reaches the app. The
/// first version of this screen could only be left by that event, so it could
/// not be left at all. Falsified by that setting changing, and only re-testable
/// on the watch.
fn draw_confirm_discard(fb: &mut FrameBuf) {
    let heading = bold_face(HEADING_H);
    let label = label_face();

    // Unbroken, so it cannot be mistaken for the zone dial at a glance.
    fill_arc(fb, RING_INNER_OFF, RING_OUTER, 0.0, 360.0, RED);

    draw_centered(fb, &heading, "DISCARD", 92, WHITE);
    draw_centered(fb, &label, "THIS RIDE?", 122, DIM);

    draw_button_answer(fb, BUTTON_R1_DEG, "YES", RED);
    draw_button_answer(fb, BUTTON_R2_DEG, "NO", WHITE);
}

/// "+100" / "+10", built from the step the button adds, so the glass cannot
/// drift from the arithmetic.
fn plus_label(step: u16, buf: &mut [u8; 12]) -> &str {
    let mut digits = [0u8; 12];
    let d = u32_to_str(step as u32, &mut digits);
    buf[0] = b'+';
    let n = if d.len() < buf.len() - 1 { d.len() } else { buf.len() - 1 };
    buf[1..1 + n].copy_from_slice(&d.as_bytes()[..n]);
    core::str::from_utf8(&buf[..1 + n]).unwrap_or("+")
}

/// The bike console's kilojoules, built with two buttons on the way to saving.
/// SKIP is as bright as SAVE, and the estimate is beside the field and never in
/// it; both are argued in `Spin/README.md` and `work.rs`.
fn draw_enter_work(fb: &mut FrameBuf, frame: &Frame) {
    let heading = bold_face(HEADING_H);
    let label = label_face();
    let value_font = number_face(CLOCK_XL_H);

    // Named for where the number comes from, so it cannot be confused with the
    // app's own calorie figure.
    draw_centered(fb, &heading, "BIKE KJ", ENTER_WORK_HEADING_Y, WHITE);

    let mut buf = [0u8; 12];
    let value = u32_to_str(frame.work_kj as u32, &mut buf);
    draw_centered(fb, &value_font, value, ENTER_WORK_VALUE_Y, WHITE);

    if frame.work_estimate_kj > 0 {
        let mut est_buf = [0u8; 12];
        let est = u32_to_str(frame.work_estimate_kj as u32, &mut est_buf);
        draw_value_row(fb, &label, "ESTIMATE", est, "KJ", ENTER_WORK_ESTIMATE_Y, DIM);
    }

    let mut hundreds_buf = [0u8; 12];
    draw_button_hint(fb, BUTTON_L1_DEG, plus_label(work::HUNDREDS_STEP, &mut hundreds_buf),
                     WHITE);
    let mut tens_buf = [0u8; 12];
    draw_button_hint(fb, BUTTON_L2_DEG, plus_label(work::TENS_STEP, &mut tens_buf), WHITE);

    draw_button_hint(fb, BUTTON_R1_DEG, "SAVE", WHITE);
    draw_button_hint(fb, BUTTON_R2_DEG, "SKIP", WHITE);
}

/// Not "saved" and not an error: the wearer asked for this.
fn draw_discarded(fb: &mut FrameBuf) {
    let heading = bold_face(HEADING_H);
    let label = label_face();

    draw_centered(fb, &heading, "DISCARDED", 96, AMBER);
    draw_centered(fb, &label, "NOTHING WAS SAVED", 126, DIM);
    draw_button_hint(fb, BUTTON_R2_DEG, "DONE", WHITE);
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
        SCREEN_CONFIRM_DISCARD => draw_confirm_discard(&mut fb),
        SCREEN_DISCARDED => draw_discarded(&mut fb),
        SCREEN_ENTER_WORK => draw_enter_work(&mut fb, frame),
        // Default rather than an arm of its own: an out-of-range screen byte is
        // a bug upstream, and READY loses the wearer the least.
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
        // The hour boundary, where the minute field starts being padded.
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
        // Otherwise the longest ride of the year is the one that draws off the
        // edge.
        let heights = [CLOCK_XL_H, CLOCK_L_H, CLOCK_M_H];
        for seconds in [0u32, 59, 60, 3599, 3600, 35999, 36000, 359_999, u32::MAX] {
            let mut buf = [0u8; 12];
            let text = format_duration(seconds, &mut buf);
            let chosen = heights
                .iter()
                .find(|h| text_width(&number_face(**h), text) <= CLOCK_MAX_W)
                .unwrap_or(&CLOCK_M_H);
            assert!(
                text_width(&number_face(*chosen), text) <= CLOCK_MAX_W,
                "{text} does not fit even at the smallest height"
            );
        }
    }

    /// The panel is a circle inscribed in the 240x240 buffer.
    fn inside_bezel(x: u32, y: u32) -> bool {
        let dx = 2 * x as i32 - (W as i32 - 1);
        let dy = 2 * y as i32 - (H as i32 - 1);
        dx * dx + dy * dy <= (W as i32) * (W as i32)
    }

    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        // Every scene, because this is a layout invariant and the scenes are
        // the layouts. Button hints inset from the buffer's edge rather than
        // the glass's lost their last glyph on the watch.
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
        // A layout that shifted would make a dropout look like a new reading.
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

    /// Just the rows the banner occupies: the target arc legitimately differs
    /// between these cases, and a whole-frame compare would call that a banner.
    fn rows(buf: &[u8], y0: u32, y1: u32) -> Vec<u8> {
        buf[(y0 * W) as usize..(y1 * W) as usize].to_vec()
    }

    fn banner_rows(buf: &[u8]) -> Vec<u8> {
        rows(buf, PAUSED_BANNER_Y as u32, PAUSED_BANNER_Y as u32 + 16)
    }

    #[test]
    fn a_pause_outranks_the_target_for_the_one_banner_slot() {
        // A wearer who pauses after passing the target needs to be told the
        // clock is stopped, not congratulated again.
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
        // The renderer must not conclude "reached" from the clock passing the
        // target: same elapsed, same target, different flag.
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
        let base = {
            let mut f = frame(SCREEN_RIDING);
            f.target_minutes = 30;
            f.has_zones = 1;
            f.zone_count = 5;
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

        // Over the arc's rows only: elapsed_s also drives the clock.
        let over = Frame { elapsed_s: 9999, target_reached: 1, ..base };
        assert_eq!(rows(&draw(&full), 200, 240), rows(&draw(&over), 200, 240));
    }

    #[test]
    fn no_target_leaves_the_ring_open() {
        let mut f = frame(SCREEN_RIDING);
        f.has_zones = 1;
        f.zone_count = 5;
        f.hr_zone = 3;
        f.elapsed_s = 600;
        let with_target = Frame { target_minutes: 30, ..f };
        assert_ne!(draw(&f), draw(&with_target));
    }

    #[test]
    fn no_zones_set_draws_no_dial_at_all() {
        // A dial with nothing lit would say "below zone 1" when it means
        // "no zones set".
        let none = Frame { elapsed_s: 300, hr_bpm: 120, ..frame(SCREEN_RIDING) };
        let below_zone_1 = Frame { has_zones: 1, zone_count: 5, hr_zone: 0, ..none };
        assert_ne!(draw(&none), draw(&below_zone_1),
                   "a wearer with zones set should see the dial even below zone 1");
    }

    #[test]
    fn each_zone_lights_a_different_segment() {
        let base = Frame { elapsed_s: 900, hr_bpm: 140, has_zones: 1, zone_count: 5,
                           hr_zone_fraction: 128, ..frame(SCREEN_RIDING) };
        let mut seen: Vec<Vec<u8>> = Vec::new();
        for zone in 0..=5u8 {
            let drawn = draw(&Frame { hr_zone: zone, ..base });
            assert!(!seen.contains(&drawn), "zone {zone} draws like another zone");
            seen.push(drawn);
        }
    }

    #[test]
    fn every_dial_size_draws_and_reads_bottom_to_top() {
        // A ladder running cool to cool loses "red means hard" at any length.
        let mut seen: Vec<Vec<u8>> = Vec::new();
        for count in 2..=MAX_ZONES as u8 {
            let base = Frame { has_zones: 1, zone_count: count, hr_zone_fraction: 128,
                               elapsed_s: 600, ..frame(SCREEN_RIDING) };
            let bottom = draw(&Frame { hr_zone: 1, ..base });
            let top = draw(&Frame { hr_zone: count, ..base });
            assert!(lit_pixels(&bottom) > 500, "{count} zones drew almost nothing");
            assert_ne!(bottom, top, "{count} zones: top and bottom look the same");
            assert!(!seen.contains(&bottom), "{count} zones draws like another count");
            seen.push(bottom);
        }
    }

    #[test]
    fn a_count_outside_the_supported_range_draws_no_dial() {
        // Rather than indexing off the end of ZONE_HUES.
        for count in [1u8, 9, 200] {
            let f = Frame { has_zones: 1, zone_count: count, hr_zone: 1,
                            ..frame(SCREEN_RIDING) };
            assert_eq!(draw(&f), draw(&Frame { has_zones: 0, ..f }),
                       "count {count} drew something");
        }
    }

    #[test]
    fn the_needle_stays_inside_its_own_segment_at_any_count() {
        // Why the fraction is per-zone: equal slices with gaps between them, so
        // a whole-scale position drifts out of its segment by the last one.
        for count in 2..=MAX_ZONES as u8 {
            let segment =
                (RING_SWEEP_DEG - RING_GAP_DEG * (count as f32 - 1.0)) / count as f32;
            for zone in 1..=count {
                let start = RING_START_DEG + (segment + RING_GAP_DEG) * (zone - 1) as f32;
                for frac in [0u8, 128, 255] {
                    let deg = zone_needle_deg(zone, frac, count);
                    assert!(deg >= start - 0.01 && deg <= start + segment + 0.01,
                            "count {count} zone {zone} frac {frac}: {deg} is outside its segment");
                }
            }
        }
    }

    #[test]
    fn the_energy_unit_changes_the_word_not_the_number() {
        let kcal = Frame { elapsed_s: 2712, avg_hr_bpm: 141, energy: 402, saved_ok: 1,
                           ..frame(SCREEN_SAVED) };
        assert_ne!(draw(&kcal), draw(&Frame { energy_is_kj: 1, ..kcal }));
    }

    #[test]
    fn zero_energy_is_still_drawn() {
        let zero = Frame { elapsed_s: 12, saved_ok: 1, ..frame(SCREEN_SAVED) };
        assert_ne!(draw(&zero), draw(&Frame { energy: 5, ..zero }));
        assert!(lit_pixels(&draw(&zero)) > 500);
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
    fn the_confirm_screen_offers_a_way_out() {
        // The bug this replaced: the screen could only be left by an event the
        // system was intercepting, so it could not be left at all.
        let buf = draw(&frame(SCREEN_CONFIRM_DISCARD));
        assert!(lit_pixels(&buf) > 500);

        let mut riding = frame(SCREEN_RIDING);
        riding.has_zones = 1;
        riding.zone_count = 5;
        riding.hr_zone = 3;
        assert_ne!(buf, draw(&riding));
    }

    #[test]
    fn a_discarded_ride_does_not_read_as_a_failed_save() {
        let discarded = frame(SCREEN_DISCARDED);
        let mut failed = frame(SCREEN_SAVED);
        failed.elapsed_s = 2712;
        failed.saved_ok = 0;
        assert_ne!(draw(&discarded), draw(&failed));
    }

    #[test]
    fn the_entry_screen_offers_both_ways_off_it() {
        // Both endings are CLICKs on labelled buttons, and both are drawn.
        let buf = draw(&frame(SCREEN_ENTER_WORK));
        assert!(lit_pixels(&buf) > 500);

        assert_ne!(buf, draw(&frame(SCREEN_CONFIRM_DISCARD)));
        assert_ne!(buf, draw(&frame(SCREEN_SAVED)));
    }

    #[test]
    fn the_entry_screen_shows_the_number_being_built() {
        // Or the wearer cannot tell that a click landed.
        let mut seen: Vec<Vec<u8>> = Vec::new();
        let mut v = 0u16;
        for _ in 0..work::TENS_PLACES {
            let drawn = draw(&Frame { work_kj: v, ..frame(SCREEN_ENTER_WORK) });
            assert!(!seen.contains(&drawn), "{v} kJ draws like another value");
            seen.push(drawn);
            v = work::add_tens(v);
        }
    }

    #[test]
    fn the_button_labels_are_the_steps_the_buttons_take() {
        // What the wearer is currently promised on the glass.
        let mut buf = [0u8; 12];
        assert_eq!(plus_label(work::HUNDREDS_STEP, &mut buf), "+100");
        let mut buf = [0u8; 12];
        assert_eq!(plus_label(work::TENS_STEP, &mut buf), "+10");
    }

    #[test]
    fn the_estimate_is_drawn_beside_the_value_and_never_as_it() {
        // The reference must not be mistakable for the field.
        let none = Frame { work_kj: 0, work_estimate_kj: 0, ..frame(SCREEN_ENTER_WORK) };
        let with_ref = Frame { work_estimate_kj: 390, ..none };
        assert_ne!(draw(&none), draw(&with_ref), "the reference should be drawn");

        let entered = Frame { work_kj: 390, work_estimate_kj: 0, ..none };
        assert_ne!(draw(&entered), draw(&with_ref),
                   "390 entered and 390 suggested must not look the same");
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
        // Every scene, not one frame: the discard answers are drawn with a
        // hand-made halo one level down, and a colour that missed the panel's
        // levels would be quantised on the way to the glass -- so the test
        // would not be looking at what the watch draws.
        for (name, frame) in scenes::scenes() {
            for byte in draw(&frame) {
                assert_eq!(byte >> 6, 0b11, "{name}: pixel 0x{byte:02X} is not opaque");
            }
        }
    }

    #[test]
    fn the_discard_answers_are_bolder_than_an_ordinary_hint() {
        // They are the one place a misread costs the wearer their ride.
        assert!(text_width(&bold_face(ANSWER_H), "YES") > text_width(&label_face(), "YES"),
                "the answers should be drawn larger than an ordinary label");

        // Part-covered RED appears nowhere else on this screen -- the ring and
        // the mark are both full red -- so any shade of it is the smoothing or
        // nothing. A part-covered white would prove nothing here, since
        // "THIS RIDE?" is drawn in DIM already.
        let buf = draw(&frame(SCREEN_CONFIRM_DISCARD));
        let partial: Vec<u8> = [1u8, 2].iter().map(|l| textkit::shade(RED.0, BLACK.0, *l)).collect();
        assert!(buf.iter().any(|b| partial.contains(b)),
                "no part-covered red: the discard answers are not being smoothed");
    }
}
