#![cfg_attr(not(feature = "std"), no_std)]

use core::fmt::Write as _;

use embedded_graphics::{
    mono_font::{ascii::FONT_9X15_BOLD, MonoTextStyle},
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle},
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
    pub sample_age_ms: u32,
    pub sample_age_max_ms: u32,
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
    pub const fn rgb(r: u8, g: u8, b: u8) -> Self {
        Abgr2222(
            (ALPHA_OPAQUE << ALPHA_SHIFT)
                | (keep_high_bits(b) << BLUE_SHIFT)
                | (keep_high_bits(g) << GREEN_SHIFT)
                | (keep_high_bits(r) << RED_SHIFT),
        )
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
    draw_rim(fb, &g);
    title(fb, &g, "ACCEL mg");

    let (bubble_cx, bubble_cy) = (g.cx, g.cy - 16);
    draw_bubble_reference(fb, bubble_cx, bubble_cy);

    if !st.is_live() {
        let at = Point::new(g.cx, g.cy + 52);
        text(fb, "NO DATA", at, BRIGHT_WARNING, Alignment::Center);
        page_dots(fb, &g, 0);
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

    page_dots(fb, &g, 0);
}

const DIAG_ROW_HEIGHT: i32 = 18;

fn draw_diag(fb: &mut FrameBuf, st: &State) {
    let g = geom(fb);
    draw_rim(fb, &g);
    title(fb, &g, "DIAG");

    let x = g.cx - g.inscribed_half + 6;
    let mut y = g.cy - 35;
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
    let _ = write!(b, "PEAK{:>7}ms", st.sample_age_max_ms);
    row(fb, b.as_str());

    let mut b = Buf::<24>::new();
    let _ = write!(b, "STATE{:>8}", st.status_label());
    row(fb, b.as_str());

    page_dots(fb, &g, 1);
}

const SCREEN_COUNT: u32 = 2;

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
        _ => draw_diag(&mut fb, state),
    }
}

#[no_mangle]
pub extern "C" fn poc_gui_screen_count() -> u32 {
    screen_count()
}

/// Lets the caller confirm it was linked against the archive it thinks it was:
/// a stale one disagrees here instead of silently reading every field at the
/// wrong offset.
#[no_mangle]
pub extern "C" fn poc_gui_state_size() -> u32 {
    core::mem::size_of::<State>() as u32
}

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
            sample_age_ms: 40,
            sample_age_max_ms: 812,
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
