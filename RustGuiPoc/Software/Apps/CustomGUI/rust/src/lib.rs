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

const SCREEN_COUNT: u32 = 3;

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
        _ => draw_dither(&mut fb, state),
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
