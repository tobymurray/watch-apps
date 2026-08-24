#![cfg_attr(not(feature = "std"), no_std)]

use core::fmt::Write as _;

use embedded_graphics::{
    mono_font::{ascii::FONT_9X15_BOLD, MonoTextStyle},
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, Rectangle},
    text::{Alignment, Text},
};

#[cfg(not(feature = "std"))]
extern "C" {
    fn poc_gui_host_panic(msg: *const u8, len: u32);
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
    unsafe { poc_gui_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    pub accel_x_g: f32,
    pub accel_y_g: f32,
    pub accel_z_g: f32,
    pub uptime_ms: u32,
    pub sample_age_ms: u32,
    pub samples: u32,
    pub frames: u32,
    pub valid: u8,
    pub _pad: [u8; 3],
}

impl State {
    fn is_live(&self) -> bool {
        self.valid != 0
    }

    fn status_label(&self) -> &'static str {
        if self.is_live() {
            "LIVE"
        } else if self.samples > 0 {
            "STALE"
        } else {
            "NONE"
        }
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

const DARK_GROUND: Abgr2222 = Abgr2222::BLACK;
const BRIGHT_HEADING: Abgr2222 = Abgr2222::CYAN;
const BRIGHT_READING: Abgr2222 = Abgr2222::WHITE;
const BRIGHT_WARNING: Abgr2222 = Abgr2222::YELLOW;
const BRIGHT_CHROME: Abgr2222 = Abgr2222::GRAY;

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

const MILLI_G_PER_G: f32 = 1000.0;
const MILLI_G_DISPLAY_LIMIT: i32 = 9999;

fn to_milli_g_clamped(accel_g: f32) -> i32 {
    let milli_g = accel_g * MILLI_G_PER_G;
    let limit = MILLI_G_DISPLAY_LIMIT as f32;
    if milli_g > limit {
        MILLI_G_DISPLAY_LIMIT
    } else if milli_g < -limit {
        -MILLI_G_DISPLAY_LIMIT
    } else {
        milli_g as i32
    }
}

const INV_SQRT2_NUMERATOR: i32 = 181;
const INV_SQRT2_DENOMINATOR: i32 = 256;

fn inscribed_square_half(radius: i32) -> i32 {
    radius * INV_SQRT2_NUMERATOR / INV_SQRT2_DENOMINATOR
}

struct Geom {
    cx: i32,
    cy: i32,
    radius: i32,
    inscribed_half: i32,
}

fn geom(fb: &FrameBuf) -> Geom {
    let w = fb.w as i32;
    let h = fb.h as i32;
    let radius = w.min(h) / 2;
    Geom {
        cx: w / 2,
        cy: h / 2,
        radius,
        inscribed_half: inscribed_square_half(radius),
    }
}

fn stroke(color: Abgr2222) -> PrimitiveStyle<Abgr2222> {
    PrimitiveStyle::with_stroke(color, 1)
}

fn draw_circle(fb: &mut FrameBuf, cx: i32, cy: i32, radius: i32, style: PrimitiveStyle<Abgr2222>) {
    Circle::new(Point::new(cx - radius, cy - radius), (radius as u32) * 2)
        .into_styled(style)
        .draw(fb)
        .ok();
}

fn draw_rim(fb: &mut FrameBuf, g: &Geom) {
    draw_circle(fb, g.cx, g.cy, g.radius - 1, stroke(BRIGHT_CHROME));
}

const HEARTBEAT_STEPS: u32 = 32;
const UNIT_CIRCLE_Q7: [(i8, i8); 32] = [
    (0, -127), (25, -125), (49, -117), (71, -106),
    (90, -90), (106, -71), (117, -49), (125, -25),
    (127, 0), (125, 25), (117, 49), (106, 71),
    (90, 90), (71, 106), (49, 117), (25, 125),
    (0, 127), (-25, 125), (-49, 117), (-71, 106),
    (-90, 90), (-106, 71), (-117, 49), (-125, 25),
    (-127, 0), (-125, -25), (-117, -49), (-106, -71),
    (-90, -90), (-71, -106), (-49, -117), (-25, -125),
];

const Q7_ONE: i32 = 127;
const HEARTBEAT_DOT_RADIUS: i32 = 3;

const HEARTBEAT_STEP_MS: u32 = 100;

/// A marker whose position is a function of the clock, not of the frame count.
///
/// Either grounding proves the loop is running, since a stalled one stops
/// redrawing and the marker freezes. Uptime additionally makes a lap a known
/// duration -- `HEARTBEAT_STEPS * HEARTBEAT_STEP_MS` -- so it can be used to time
/// a run, which a frame-counted marker cannot: timing by it would assume the
/// frame rate it is supposed to reveal. And when frames are dropped it resumes
/// where the clock says rather than at the next position along, so falling behind
/// shows up as a visible jump instead of as a silently slower orbit.
fn draw_heartbeat(fb: &mut FrameBuf, g: &Geom, uptime_ms: u32) {
    let step = uptime_ms / HEARTBEAT_STEP_MS;
    let (sx, sy) = UNIT_CIRCLE_Q7[(step % HEARTBEAT_STEPS) as usize];
    let r = g.radius - 8;
    let x = g.cx + (sx as i32) * r / Q7_ONE;
    let y = g.cy + (sy as i32) * r / Q7_ONE;
    draw_circle(fb, x, y, HEARTBEAT_DOT_RADIUS, PrimitiveStyle::with_fill(BRIGHT_HEADING));
}

/// Everything every screen carries: the rim, proof the loop is running, and which
/// page you are on.
fn chrome(fb: &mut FrameBuf, g: &Geom, st: &State, page: u32) {
    draw_rim(fb, g);
    draw_heartbeat(fb, g, st.uptime_ms);
    page_dots(fb, g, page);
}

fn text(fb: &mut FrameBuf, s: &str, at: Point, color: Abgr2222, align: Alignment) {
    Text::with_alignment(s, at, MonoTextStyle::new(&FONT_9X15_BOLD, color), align)
        .draw(fb)
        .ok();
}

fn title(fb: &mut FrameBuf, g: &Geom, s: &str) {
    let at = Point::new(g.cx, g.cy - g.inscribed_half + 16);
    text(fb, s, at, BRIGHT_HEADING, Alignment::Center);
}

fn page_dots(fb: &mut FrameBuf, g: &Geom, current: u32) {
    const DIAMETER: i32 = 6;
    const SPACING: i32 = 14;
    let n = SCREEN_COUNT as i32;
    let y = g.cy + g.inscribed_half - 7;
    let x0 = g.cx - ((n - 1) * SPACING) / 2;
    for i in 0..n {
        let style = if i == current as i32 % n {
            PrimitiveStyle::with_fill(BRIGHT_READING)
        } else {
            stroke(BRIGHT_CHROME)
        };
        draw_circle(fb, x0 + i * SPACING, y, DIAMETER / 2, style);
    }
}

const BUBBLE_RADIUS: i32 = 40;
const BUBBLE_DOT_RADIUS: i32 = 6;
const CROSSHAIR_ARM: i32 = 4;
const FULL_DEFLECTION_G: f32 = 1.0;

fn draw_bubble_reference(fb: &mut FrameBuf, cx: i32, cy: i32) {
    draw_circle(fb, cx, cy, BUBBLE_RADIUS, stroke(BRIGHT_CHROME));
    Line::new(
        Point::new(cx - CROSSHAIR_ARM, cy),
        Point::new(cx + CROSSHAIR_ARM, cy),
    )
    .into_styled(stroke(BRIGHT_CHROME))
    .draw(fb)
    .ok();
    Line::new(
        Point::new(cx, cy - CROSSHAIR_ARM),
        Point::new(cx, cy + CROSSHAIR_ARM),
    )
    .into_styled(stroke(BRIGHT_CHROME))
    .draw(fb)
    .ok();
}

fn deflection_px(accel_g: f32) -> i32 {
    let limit = BUBBLE_RADIUS as f32;
    let px = accel_g / FULL_DEFLECTION_G * limit;
    if px > limit {
        BUBBLE_RADIUS
    } else if px < -limit {
        -BUBBLE_RADIUS
    } else {
        px as i32
    }
}

fn tilt_to_screen_offset(st: &State) -> (i32, i32) {
    (deflection_px(st.accel_x_g), deflection_px(-st.accel_y_g))
}

fn draw_accel(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    chrome(fb, &g, st, 0);
    title(fb, &g, "ACCEL mg");

    let (bubble_cx, bubble_cy) = (g.cx, g.cy - 16);
    draw_bubble_reference(fb, bubble_cx, bubble_cy);

    if !st.is_live() {
        let at = Point::new(g.cx, g.cy + 52);
        text(fb, "NO DATA", at, BRIGHT_WARNING, Alignment::Center);
        return;
    }

    let (dx, dy) = tilt_to_screen_offset(st);
    draw_circle(
        fb,
        bubble_cx + dx,
        bubble_cy + dy,
        BUBBLE_DOT_RADIUS,
        PrimitiveStyle::with_fill(BRIGHT_WARNING),
    );

    let mut xy = Buf::<24>::new();
    let _ = write!(
        xy,
        "X{:>5} Y{:>5}",
        to_milli_g_clamped(st.accel_x_g),
        to_milli_g_clamped(st.accel_y_g)
    );
    let at = Point::new(g.cx, g.cy + 48);
    text(fb, xy.as_str(), at, BRIGHT_READING, Alignment::Center);

    let mut z = Buf::<24>::new();
    let _ = write!(z, "Z{:>5}", to_milli_g_clamped(st.accel_z_g));
    let at = Point::new(g.cx, g.cy + 66);
    text(fb, z.as_str(), at, BRIGHT_READING, Alignment::Center);
}

const DIAG_ROW_HEIGHT: i32 = 18;

fn draw_diag(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    chrome(fb, &g, st, 1);
    title(fb, &g, "DIAG");

    let x = g.cx - g.inscribed_half + 6;
    let mut y = g.cy - 26;
    let mut row = |fb: &mut FrameBuf, s: &str| {
        text(fb, s, Point::new(x, y), BRIGHT_READING, Alignment::Left);
        y += DIAG_ROW_HEIGHT;
    };

    let mut b = Buf::<24>::new();
    let _ = write!(b, "FRAMES{:>7}", st.frames);
    row(fb, b.as_str());

    let mut b = Buf::<24>::new();
    let _ = write!(b, "SAMPLE{:>7}", st.samples);
    row(fb, b.as_str());

    let mut b = Buf::<24>::new();
    let _ = write!(b, "AGE{:>8}ms", st.sample_age_ms);
    row(fb, b.as_str());

    let mut b = Buf::<24>::new();
    let _ = write!(b, "STATE{:>8}", st.status_label());
    row(fb, b.as_str());
}

// Ordered-dither threshold matrix. Values 0..63 spread so that neighbouring
// pixels round in different directions, trading spatial resolution for apparent
// colour depth -- which is the whole trick on a panel with four levels a channel.
const BAYER_8X8: [[u8; 8]; 8] = [
    [0, 32, 8, 40, 2, 34, 10, 42],
    [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38],
    [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41],
    [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37],
    [63, 31, 55, 23, 61, 29, 53, 21],
];

const CHANNEL_LEVELS_MAX: u32 = 3;
const FULL_SCALE: u32 = 255;
const BAYER_SIZE: usize = 8;
const BAYER_MAX: u32 = 63;

/// Quantise 0..255 to a 2-bit level by truncation: what asking for a colour the
/// panel cannot show gets you, and the reason a ramp becomes four bands.
fn quantise_flat(value: u8) -> u8 {
    keep_high_bits(value)
}

/// Quantise with a per-pixel threshold, so a value between two levels lands on
/// the lower one in some pixels and the upper one in others.
fn quantise_dithered(value: u8, x: i32, y: i32) -> u8 {
    let threshold = BAYER_8X8[(y as usize) % BAYER_SIZE][(x as usize) % BAYER_SIZE] as u32;
    let scaled = value as u32 * CHANNEL_LEVELS_MAX
        + threshold * (FULL_SCALE + 1) / (BAYER_MAX + 1);
    let level = scaled / FULL_SCALE;
    if level > CHANNEL_LEVELS_MAX {
        CHANNEL_LEVELS_MAX as u8
    } else {
        level as u8
    }
}

const GRADIENT_HALF_HEIGHT: i32 = 55;
const SPLIT_GAP: i32 = 2;

fn draw_dither(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    chrome(fb, &g, st, 2);

    let top = g.cy - GRADIENT_HALF_HEIGHT;
    let bottom = g.cy + GRADIENT_HALF_HEIGHT;
    let span = (bottom - top) as u32;
    let left = g.cx - g.inscribed_half;
    let right = g.cx + g.inscribed_half;

    for y in top..bottom {
        // One luminance ramp, drawn twice: the only difference between the
        // halves is how the same value is turned into a pixel.
        let value = (((y - top) as u32 * FULL_SCALE) / span) as u8;
        for x in left..right {
            if (x - g.cx).abs() < SPLIT_GAP {
                continue;
            }
            let level = if x < g.cx {
                quantise_flat(value)
            } else {
                quantise_dithered(value, x, y)
            };
            let idx = (y as u32 * fb.w + x as u32) as usize;
            fb.buf[idx] = Abgr2222::from_levels(level, level, level).0;
        }
    }

    // Both halves are framed because the flat side quantises the top of the ramp
    // to pure black, which without a border reads as a missing gradient rather
    // than as the first of its four bands.
    let frame = Rectangle::new(
        Point::new(g.cx - g.inscribed_half, top),
        Size::new((g.inscribed_half as u32) * 2, span),
    );
    frame.into_styled(stroke(BRIGHT_CHROME)).draw(fb).ok();

    let label_y = g.cy - g.inscribed_half + 16;
    text(fb, "FLAT", Point::new(g.cx - 42, label_y), BRIGHT_READING, Alignment::Center);
    text(fb, "DITHER", Point::new(g.cx + 44, label_y), BRIGHT_HEADING, Alignment::Center);
}


// -- PLASMA --------------------------------------------------------------------
// A per-pixel field, recomputed every frame. The measured budget is ~217 cycles
// per pixel per frame (100 ms tick, 22 ms of it spent pushing), which is enough
// for this and is CPU nothing else on the watch spends.

/// A hue wheel walked along the saturated hull of the colour cube: every entry
/// has one channel at maximum and one at zero.
///
/// Chosen that way because of what MapKit measured about this panel -- at two
/// bits a channel, CyclOSM tiles quantise well and OSM standard washes out. The
/// difference is saturation: colours near the grey axis collapse onto each other
/// with only four levels to land on, and no amount of dithering brings them back.
/// A field that drifted through pastels would wash out the same way, so this one
/// only ever traverses the hull, and dithers *between adjacent hues* rather than
/// per channel.
const HUE_WHEEL_LEN: i32 = 18;
const HUE_WHEEL: [Abgr2222; 18] = [
    Abgr2222::from_levels(3, 1, 0), Abgr2222::from_levels(3, 2, 0), Abgr2222::from_levels(3, 3, 0),
    Abgr2222::from_levels(2, 3, 0), Abgr2222::from_levels(1, 3, 0), Abgr2222::from_levels(0, 3, 0),
    Abgr2222::from_levels(0, 3, 1), Abgr2222::from_levels(0, 3, 2), Abgr2222::from_levels(0, 3, 3),
    Abgr2222::from_levels(0, 2, 3), Abgr2222::from_levels(0, 1, 3), Abgr2222::from_levels(0, 0, 3),
    Abgr2222::from_levels(1, 0, 3), Abgr2222::from_levels(2, 0, 3), Abgr2222::from_levels(3, 0, 3),
    Abgr2222::from_levels(3, 0, 2), Abgr2222::from_levels(3, 0, 1), Abgr2222::from_levels(3, 0, 0),
];

const SINE_U8: [u8; 256] = [
    128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173,
    176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215,
    218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244,
    245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255,
    255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246,
    245, 244, 243, 241, 240, 238, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220,
    218, 215, 213, 211, 208, 206, 203, 201, 198, 196, 193, 190, 188, 185, 182, 179,
    176, 173, 170, 167, 165, 162, 158, 155, 152, 149, 146, 143, 140, 137, 134, 131,
    128, 124, 121, 118, 115, 112, 109, 106, 103, 100,  97,  93,  90,  88,  85,  82,
     79,  76,  73,  70,  67,  65,  62,  59,  57,  54,  52,  49,  47,  44,  42,  40,
     37,  35,  33,  31,  29,  27,  25,  23,  21,  20,  18,  17,  15,  14,  12,  11,
     10,   9,   7,   6,   5,   5,   4,   3,   2,   2,   1,   1,   1,   0,   0,   0,
      0,   0,   0,   0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   9,
     10,  11,  12,  14,  15,  17,  18,  20,  21,  23,  25,  27,  29,  31,  33,  35,
     37,  40,  42,  44,  47,  49,  52,  54,  57,  59,  62,  65,  67,  70,  73,  76,
     79,  82,  85,  88,  90,  93,  97, 100, 103, 106, 109, 112, 115, 118, 121, 124,
];

/// `sqrt(i << SQRT_SHIFT)`, so a radius comes from one shift and one lookup
/// instead of a square root.
const SQRT_SHIFT: u32 = 7;
const SQRT_LUT: [u8; 256] = [
      0,  11,  16,  20,  23,  25,  28,  30,  32,  34,  36,  38,  39,  41,  42,  44,
     45,  47,  48,  49,  51,  52,  53,  54,  55,  57,  58,  59,  60,  61,  62,  63,
     64,  65,  66,  67,  68,  69,  70,  71,  72,  72,  73,  74,  75,  76,  77,  78,
     78,  79,  80,  81,  82,  82,  83,  84,  85,  85,  86,  87,  88,  88,  89,  90,
     91,  91,  92,  93,  93,  94,  95,  95,  96,  97,  97,  98,  99,  99, 100, 101,
    101, 102, 102, 103, 104, 104, 105, 106, 106, 107, 107, 108, 109, 109, 110, 110,
    111, 111, 112, 113, 113, 114, 114, 115, 115, 116, 116, 117, 118, 118, 119, 119,
    120, 120, 121, 121, 122, 122, 123, 123, 124, 124, 125, 125, 126, 126, 127, 127,
    128, 128, 129, 129, 130, 130, 131, 131, 132, 132, 133, 133, 134, 134, 135, 135,
    136, 136, 137, 137, 138, 138, 139, 139, 139, 140, 140, 141, 141, 142, 142, 143,
    143, 144, 144, 144, 145, 145, 146, 146, 147, 147, 148, 148, 148, 149, 149, 150,
    150, 151, 151, 151, 152, 152, 153, 153, 153, 154, 154, 155, 155, 156, 156, 156,
    157, 157, 158, 158, 158, 159, 159, 160, 160, 160, 161, 161, 162, 162, 162, 163,
    163, 164, 164, 164, 165, 165, 166, 166, 166, 167, 167, 167, 168, 168, 169, 169,
    169, 170, 170, 170, 171, 171, 172, 172, 172, 173, 173, 173, 174, 174, 175, 175,
    175, 176, 176, 176, 177, 177, 177, 178, 178, 179, 179, 179, 180, 180, 180, 181,
];

const PLASMA_MS_PER_PHASE: u32 = 40;

/// Angle around the circle as 0..255, by the diamond approximation: one divide,
/// no trig. It is monotonic with true angle but not linear in it, which warps the
/// angular structure slightly -- on a field that reads as character, not error.
fn diamond_angle(dx: i32, dy: i32) -> i32 {
    let (quadrant, num, den) = if dy >= 0 {
        if dx >= 0 { (0, dy, dx + dy) } else { (1, -dx, dy - dx) }
    } else if dx < 0 {
        (2, -dy, -dx - dy)
    } else {
        (3, dx, dx - dy)
    };
    if den == 0 {
        return quadrant * 64;
    }
    (quadrant * 64 + (num * 64) / den) & 0xFF
}

fn sine(phase: i32) -> i32 {
    SINE_U8[(phase & 0xFF) as usize] as i32
}

/// A marker that has to stay readable over an arbitrary colour, so it carries its
/// own dark border rather than relying on the background it lands on.
fn outlined_dot(fb: &mut FrameBuf, cx: i32, cy: i32, radius: i32, color: Abgr2222) {
    draw_circle(fb, cx, cy, radius + 1, PrimitiveStyle::with_fill(DARK_GROUND));
    draw_circle(fb, cx, cy, radius, PrimitiveStyle::with_fill(color));
}

fn draw_plasma(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    let phase = (st.uptime_ms / PLASMA_MS_PER_PHASE) as i32;
    let limit = g.radius * g.radius;
    let w = fb.w as i32;
    let h = fb.h as i32;

    for y in 0..h {
        let dy = y - g.cy;
        for x in 0..w {
            let dx = x - g.cx;
            let dist2 = dx * dx + dy * dy;
            if dist2 > limit {
                continue;
            }
            let r = SQRT_LUT[((dist2 >> SQRT_SHIFT) as usize).min(255)] as i32;
            let a = diamond_angle(dx, dy);

            // Three terms: rings, spokes, and a spiral from mixing the two, so
            // the structure is native to a round display rather than a rectangle
            // with its corners hidden.
            let field = sine(r * 2 + phase)
                + sine(a * 3 - phase)
                + sine(r + a * 2 + phase / 2);

            // Straight to a hue index with a fractional part, then dither between
            // that hue and the next. Six is close enough to (18 * 256) / 766 to
            // spare a divide per pixel.
            let scaled = field * 6;
            let index = scaled >> 8;
            let frac = scaled & 0xFF;
            let threshold =
                (BAYER_8X8[(y as usize) % BAYER_SIZE][(x as usize) % BAYER_SIZE] as i32) * 4;
            let hue = if frac > threshold { index + 1 } else { index };

            let idx = (y as u32 * fb.w + x as u32) as usize;
            fb.buf[idx] = HUE_WHEEL[hue.rem_euclid(HUE_WHEEL_LEN) as usize].0;
        }
    }

    let (sx, sy) = UNIT_CIRCLE_Q7[((st.uptime_ms / HEARTBEAT_STEP_MS) % HEARTBEAT_STEPS) as usize];
    let hr = g.radius - 8;
    outlined_dot(
        fb,
        g.cx + (sx as i32) * hr / Q7_ONE,
        g.cy + (sy as i32) * hr / Q7_ONE,
        HEARTBEAT_DOT_RADIUS,
        BRIGHT_HEADING,
    );

    const DIAMETER: i32 = 6;
    const SPACING: i32 = 14;
    let dots_y = g.cy + g.inscribed_half - 7;
    let x0 = g.cx - ((SCREEN_COUNT as i32 - 1) * SPACING) / 2;
    for i in 0..SCREEN_COUNT as i32 {
        let color = if i == 3 { BRIGHT_READING } else { BRIGHT_CHROME };
        outlined_dot(fb, x0 + i * SPACING, dots_y, DIAMETER / 2, color);
    }
}

const SCREEN_COUNT: u32 = 4;

pub const fn screen_count() -> u32 {
    SCREEN_COUNT
}

pub fn render(buf: &mut [u8], width: u32, height: u32, screen: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height };
    fb.buf.fill(DARK_GROUND.0);
    match screen % SCREEN_COUNT {
        0 => draw_accel(&mut fb, state),
        1 => draw_diag(&mut fb, state),
        2 => draw_dither(&mut fb, state),
        _ => draw_plasma(&mut fb, state),
    }
}

#[no_mangle]
pub extern "C" fn poc_gui_screen_count() -> u32 {
    screen_count()
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `poc_gui_abi::fingerprint()`
/// in poc_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, core::mem::offset_of!(State, accel_x_g));
    let h = fnv1a(h, core::mem::offset_of!(State, accel_y_g));
    let h = fnv1a(h, core::mem::offset_of!(State, accel_z_g));
    let h = fnv1a(h, core::mem::offset_of!(State, uptime_ms));
    let h = fnv1a(h, core::mem::offset_of!(State, sample_age_ms));
    let h = fnv1a(h, core::mem::offset_of!(State, samples));
    let h = fnv1a(h, core::mem::offset_of!(State, frames));
    let h = fnv1a(h, core::mem::offset_of!(State, valid));
    fnv1a(h, core::mem::offset_of!(State, _pad))
}

/// Lets the caller confirm it was linked against the archive it thinks it was.
/// The compile-time assertions below cannot do this: a stale archive and a newer
/// header each satisfy their own, having been compiled at different times.
#[no_mangle]
pub extern "C" fn poc_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

// Per field, because a size check passes when two fields are swapped. poc_gui.h
// asserts the same offsets, so a hand edit to either declaration breaks a build.
const _: () = assert!(core::mem::size_of::<State>() == 32);
const _: () = assert!(core::mem::align_of::<State>() == 4);
const _: () = assert!(core::mem::offset_of!(State, accel_x_g) == 0);
const _: () = assert!(core::mem::offset_of!(State, accel_y_g) == 4);
const _: () = assert!(core::mem::offset_of!(State, accel_z_g) == 8);
const _: () = assert!(core::mem::offset_of!(State, uptime_ms) == 12);
const _: () = assert!(core::mem::offset_of!(State, sample_age_ms) == 16);
const _: () = assert!(core::mem::offset_of!(State, samples) == 20);
const _: () = assert!(core::mem::offset_of!(State, frames) == 24);
const _: () = assert!(core::mem::offset_of!(State, valid) == 28);
const _: () = assert!(core::mem::offset_of!(State, _pad) == 29);

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `poc_gui_state`, both valid for the duration of the call.
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

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;
    const C_STRUCT_SIZE: usize = 32;
    const C_STRUCT_ALIGN: usize = 4;

    fn live() -> State {
        State {
            accel_x_g: 0.25,
            accel_y_g: -0.5,
            accel_z_g: 0.9,
            uptime_ms: 12_345,
            sample_age_ms: 40,
            samples: 123,
            frames: 456,
            valid: 1,
            _pad: [0; 3],
        }
    }

    #[test]
    fn state_layout_matches_c() {
        assert_eq!(core::mem::size_of::<State>(), C_STRUCT_SIZE);
        assert_eq!(core::mem::align_of::<State>(), C_STRUCT_ALIGN);
    }

    /// Pinned so that a change to either fingerprint implementation shows up
    /// here rather than as a refusal to start on a watch.
    #[test]
    fn abi_fingerprint_is_stable() {
        assert_eq!(poc_gui_abi_fingerprint(), 0xEDE6_6FD4);
    }

    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0xAAu8; (W * H) as usize - 1];
        render(&mut buf, W, H, 0, &live());
        assert!(buf.iter().all(|&b| b == 0xAA));
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        for screen in 0..screen_count() {
            let mut buf = vec![0xAAu8; n + 64];
            render(&mut buf, W, H, screen, &live());
            assert!(buf[n..].iter().all(|&b| b == 0xAA), "screen {screen} overran");
        }
    }

    #[test]
    fn stale_state_renders_differently() {
        let n = (W * H) as usize;
        let mut a = vec![0u8; n];
        let mut b = vec![0u8; n];
        render(&mut a, W, H, 0, &live());
        render(&mut b, W, H, 0, &State { valid: 0, ..live() });
        assert_ne!(a, b);
    }

    #[test]
    fn hostile_sensor_values_stay_in_bounds() {
        let n = (W * H) as usize;
        let nasty = [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, 1.0e30, -1.0e30, 0.0];
        for &v in &nasty {
            for screen in 0..screen_count() {
                let mut buf = vec![0xAAu8; n + 64];
                let st = State { accel_x_g: v, accel_y_g: v, accel_z_g: v, ..live() };
                render(&mut buf, W, H, screen, &st);
                assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran on {v} screen {screen}");
            }
        }
    }

    /// The whole claim of the dither screen. Four levels a channel is all the
    /// panel has, so the flat half can only render a ramp as uniform bands --
    /// every row one value. The dithered half mixes two levels within a row,
    /// which is what buys apparent depth the hardware does not have.
    #[test]
    fn dithering_beats_flat_quantisation() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        render(&mut buf, W, H, 2, &live());

        let distinct_in_row = |y: u32, x0: u32, x1: u32| {
            (x0..x1)
                .map(|x| buf[(y * W + x) as usize])
                .collect::<std::collections::BTreeSet<_>>()
                .len()
        };

        let rows: Vec<u32> = (70..170).collect();
        let flat_uniform = rows.iter().filter(|&&y| distinct_in_row(y, 60, 110) == 1).count();
        let dith_mixed = rows.iter().filter(|&&y| distinct_in_row(y, 130, 180) >= 2).count();

        assert_eq!(
            flat_uniform,
            rows.len(),
            "flat half should be uniform per row; {} of {} were not",
            rows.len() - flat_uniform,
            rows.len()
        );
        assert!(
            dith_mixed * 2 > rows.len(),
            "dithered half mixed levels in only {} of {} rows",
            dith_mixed,
            rows.len()
        );
    }

    /// The rule MapKit's panel measurements imply: at four levels a channel,
    /// colours near the grey axis collapse onto each other. Every hue this screen
    /// can emit must therefore sit on the saturated hull -- one channel at
    /// maximum and one at zero -- or it would wash out the way OSM standard tiles
    /// do, and dithering would not save it.
    #[test]
    fn plasma_only_uses_fully_saturated_colours() {
        for (i, c) in HUE_WHEEL.iter().enumerate() {
            let r = c.0 & 0b11;
            let g = (c.0 >> 2) & 0b11;
            let b = (c.0 >> 4) & 0b11;
            let (lo, hi) = ([r, g, b].iter().copied().min().unwrap(),
                            [r, g, b].iter().copied().max().unwrap());
            assert_eq!(lo, 0, "hue {i} has no channel at zero: {r},{g},{b}");
            assert_eq!(hi, 3, "hue {i} has no channel at maximum: {r},{g},{b}");
        }
    }

    /// And that the field actually traverses the wheel rather than sitting in one
    /// corner of it -- the gamut is only used if the picture uses it.
    #[test]
    fn plasma_traverses_the_whole_wheel() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        render(&mut buf, W, H, 3, &live());
        let used: std::collections::BTreeSet<u8> = HUE_WHEEL
            .iter()
            .map(|c| c.0)
            .filter(|v| buf.contains(v))
            .collect();
        assert_eq!(used.len(), HUE_WHEEL.len(), "only {} hues reached the screen", used.len());
    }

    /// The corners fall outside the panel's circle, so the field skips them and
    /// they keep the background `render()` cleared them to. Asserting they are
    /// background rather than untouched, because the clear reaches them first.
    #[test]
    fn plasma_skips_the_corners() {
        let n = (W * H) as usize;
        let mut buf = vec![0xAAu8; n];
        render(&mut buf, W, H, 3, &live());
        for (x, y) in [(0u32, 0u32), (W - 1, 0), (0, H - 1), (W - 1, H - 1)] {
            assert_eq!(
                buf[(y * W + x) as usize],
                DARK_GROUND.0,
                "corner ({x},{y}) got field colour"
            );
        }
    }

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
