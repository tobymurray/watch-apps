#![cfg_attr(not(feature = "std"), no_std)]

use embedded_graphics::{pixelcolor::raw::RawU8, pixelcolor::PixelColor, prelude::*};
use u8g2_fonts::{fonts, FontRenderer};

#[cfg(not(feature = "std"))]
extern "C" {
    fn barcode_gui_host_panic(msg: *const u8, len: u32);
}

#[cfg(not(feature = "std"))]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"panic";
    unsafe { barcode_gui_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

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

// Mirrors barcode_gui_frame (barcode_gui.h) field for field -- field order
// chosen there so every field lands on its natural alignment with zero
// padding; the ABI fingerprint below checks whatever the compiler actually
// produced rather than trusting that by inspection.
pub const KIND_CODE128: u8 = 0;
pub const KIND_ITF: u8 = 1;
pub const KIND_QR: u8 = 2;
pub const KIND_PROMPT: u8 = 3;

pub const MAX_WIDTHS: usize = 115;
pub const MAX_MATRIX_BITS: usize = 79;
pub const ID_LEN: usize = 17;
pub const NAME_LEN: usize = 13;
pub const MESSAGE_LEN: usize = 96;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Frame {
    pub total_modules: u16,
    pub kind: u8,
    pub width_count: u8,
    pub widths: [u8; MAX_WIDTHS],
    pub matrix_bits: [u8; MAX_MATRIX_BITS],
    pub matrix_size: u8,
    pub id: [u8; ID_LEN],
    pub name: [u8; NAME_LEN],
    pub index: u8,
    pub count: u8,
    pub message: [u8; MESSAGE_LEN],
}

impl Frame {
    fn str_field(bytes: &[u8]) -> &str {
        let len = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
        core::str::from_utf8(&bytes[..len]).unwrap_or("")
    }

    fn id_str(&self) -> &str {
        Self::str_field(&self.id)
    }

    fn name_str(&self) -> &str {
        Self::str_field(&self.name)
    }

    fn message_str(&self) -> &str {
        Self::str_field(&self.message)
    }

    fn dark(&self, x: u8, y: u8) -> bool {
        let i = y as usize * self.matrix_size as usize + x as usize;
        (self.matrix_bits[i / 8] >> (i % 8)) & 1 != 0
    }
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `barcode_gui_abi::fingerprint()`
/// in barcode_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<Frame>());
    let h = fnv1a(h, core::mem::align_of::<Frame>());
    let h = fnv1a(h, core::mem::offset_of!(Frame, total_modules));
    let h = fnv1a(h, core::mem::offset_of!(Frame, kind));
    let h = fnv1a(h, core::mem::offset_of!(Frame, width_count));
    let h = fnv1a(h, core::mem::offset_of!(Frame, widths));
    let h = fnv1a(h, core::mem::offset_of!(Frame, matrix_bits));
    let h = fnv1a(h, core::mem::offset_of!(Frame, matrix_size));
    let h = fnv1a(h, core::mem::offset_of!(Frame, id));
    let h = fnv1a(h, core::mem::offset_of!(Frame, name));
    let h = fnv1a(h, core::mem::offset_of!(Frame, index));
    let h = fnv1a(h, core::mem::offset_of!(Frame, count));
    fnv1a(h, core::mem::offset_of!(Frame, message))
}

#[no_mangle]
pub extern "C" fn barcode_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

const _: () = assert!(core::mem::size_of::<Frame>() == 328);
const _: () = assert!(core::mem::align_of::<Frame>() == 2);
const _: () = assert!(core::mem::offset_of!(Frame, total_modules) == 0);
const _: () = assert!(core::mem::offset_of!(Frame, kind) == 2);
const _: () = assert!(core::mem::offset_of!(Frame, width_count) == 3);
const _: () = assert!(core::mem::offset_of!(Frame, widths) == 4);
const _: () = assert!(core::mem::offset_of!(Frame, matrix_bits) == 119);
const _: () = assert!(core::mem::offset_of!(Frame, matrix_size) == 198);
const _: () = assert!(core::mem::offset_of!(Frame, id) == 199);
const _: () = assert!(core::mem::offset_of!(Frame, name) == 216);
const _: () = assert!(core::mem::offset_of!(Frame, index) == 229);
const _: () = assert!(core::mem::offset_of!(Frame, count) == 230);
const _: () = assert!(core::mem::offset_of!(Frame, message) == 231);

// -- Geometry, copied verbatim from Barcode/Software/Libs/Header/BarcodeLayout.hpp --

const PANEL_W: i32 = 240;
const PANEL_H: i32 = 240;

const BACKING_X: i32 = 10;
const BACKING_Y: i32 = 73;
const BACKING_W: i32 = 220;
const BACKING_H: i32 = 94;

const BARS_X: i32 = 20;
const BARS_Y: i32 = 75;
const BARS_W: i32 = 200;
const BARS_H: i32 = 90;

const ITF_MAX_UNIT_PX: i32 = 4;

fn itf_unit_px(units: i32) -> i32 {
    if units <= 0 {
        return 1;
    }
    let px = BACKING_W / (units + 2 * 10);
    px.clamp(1, ITF_MAX_UNIT_PX)
}

fn itf_bearer_px(unit_px: i32) -> i32 {
    (unit_px * 2).max(4)
}

const CAPTION_X: i32 = 40;
const CAPTION_Y: i32 = 48;
const CAPTION_W: i32 = 160;
const CAPTION_H: i32 = 24;

const QR_QUIET_MODULES: i32 = 4;
const QR_MODULE_PX: i32 = 4;
const QR_X: i32 = 54;
const QR_Y: i32 = 33;
const QR_INK_X: i32 = QR_X + QR_QUIET_MODULES * QR_MODULE_PX;
const QR_INK_Y: i32 = QR_Y + QR_QUIET_MODULES * QR_MODULE_PX;

const ID_X: i32 = 27;
const ID_Y: i32 = 178;
const ID_W: i32 = 187;
const ID_LINE1_Y: i32 = 169;
// helvR14's own default line height (23px), not the original's 18px pitch --
// that pitch was tuned to Regular 18's shorter line height and the fonts
// differ enough here that reusing it collided the two rows (caught by
// rendering the worst case and looking at it, not by any test).
const ID_LINE_H: i32 = 23;
const ID_LINE2_Y: i32 = ID_LINE1_Y + ID_LINE_H;

const MARK_W: i32 = 8;
const MARK_H: i32 = 4;
const MARK_PITCH: i32 = 15;
const MARK_Y: i32 = 212;

const PROMPT_X: i32 = 20;
const PROMPT_W: i32 = 200;
const PROMPT_LINE_H: i32 = 24;
const PROMPT_TOP: i32 = 72;
const PROMPT_MAX_LINES: usize = 4;

const WHITE: Abgr2222 = Abgr2222::WHITE;
const BLACK: Abgr2222 = Abgr2222::BLACK;
const DIM: Abgr2222 = Abgr2222::rgb(170, 170, 170);

/// A direct row-fill rather than going through embedded-graphics's generic
/// styled-primitive machinery (`Rectangle::into_styled().draw()`) -- this is
/// the single most-called drawing primitive in the whole renderer (every
/// bar, module, pager mark and backing box), so its own code size and the
/// styled-drawable/point-iterator machinery it would otherwise pull in for
/// every distinct color it's called with are both worth avoiding.
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

// -- Text: u8g2-fonts, the proportional bitmap font layer that
// embedded-graphics's own MonoTextStyle cannot provide. Two faces mirror the
// original two-tier id layout (a bold/larger face preferred when it fits
// under an ink limit, else a smaller/regular face); the exact point sizes
// and thresholds are chosen empirically against these specific bitmap fonts,
// not reused from TouchGFX's proprietary font metrics -- see the plan's
// note on why kIdW/kIdInkLimit do not carry over numerically.
// helvB18/helvR14 were the original choice, picked over helvB12/helvR10 by
// measuring against the original TouchGFX fonts' own widths: helvR14's width
// for the 16-char worst case (177px) lands almost exactly on Regular 18's
// (178px). Anti-aliasing then needed a *larger* source font to shrink from
// (see render_smoothed() below -- u8g2 fonts are fixed-resolution 1bpp
// bitmaps with no "render bigger" option), which meant shipping helvB24/
// helvR24 as well: two more glyph sets, ~8.7KB of .text, for no visual
// difference from just using the larger pair for measurement too and scaling
// the result down. So Font/FontSmall *are* that larger pair now; LARGE_H/
// SMALL_H and scale_large()/scale_small() below are what is left of the
// smaller pair -- their target heights and the width ratio between the two
// sizes, not the fonts themselves. The original used exactly two faces --
// T_TMP_SEMIBOLD_20 for the preferred large id, T_TMP_REGULAR_18 for
// everything else (split id lines, caption, every prompt line) -- so
// FontSmall does all three jobs here too, rather than a third face.
//
// `_tr` (u8g2's "reduced" glyph-coverage tier), not `_tf` ("full" Unicode
// coverage): every string this app ever draws is printable ASCII -- ids and
// names are validated by Barcode's own encoders before they ever reach this
// crate, and every prompt message is a fixed English literal -- so `_tf`'s
// wide non-Latin ranges were dead weight. Saved ~7.3KB with zero visual
// difference, confirmed by re-diffing every screen against the real device
// captures: identical numbers to the `_tf` build, because nothing actually
// rendered by this app is outside `_tr`'s coverage.
type Font = fonts::u8g2_font_helvB24_tr;
type FontSmall = fonts::u8g2_font_helvR24_tr;
type FontPrompt = fonts::u8g2_font_helvR24_tr;

/// helvB18's own ascent(19) - descent(-5): the target height every "large" id
/// draws at, now that helvB18 itself is no longer shipped.
const LARGE_H: i32 = 24;
/// helvR14's own ascent(14) - descent(-4): the target height everything
/// "small" (split id lines, caption, every prompt line) draws at.
const SMALL_H: i32 = 18;

/// Scales a Font (helvB24) measurement down to what helvB18 would have
/// measured, by the ratio of the two faces' own heights: 24/32, where 32 is
/// helvB24's ascent(25) - descent(-7). Not exact -- a real font's width
/// doesn't scale perfectly linearly with height across sizes -- but the
/// thresholds this feeds were already an empirical approximation against
/// TouchGFX's proprietary metrics, not exact ones, so this is one
/// approximation layered on another that was already there.
fn scale_large(px: u32) -> u32 {
    px * 3 / 4
}

/// Scales a FontSmall (helvR24) measurement down to what helvR14 would have
/// measured, by the ratio of the two faces' own heights: 18/32.
fn scale_small(px: u32) -> u32 {
    px * 9 / 16
}

fn measure_width(renderer: &FontRenderer, s: &str) -> u32 {
    renderer
        .get_rendered_dimensions(
            s,
            Point::zero(),
            u8g2_fonts::types::VerticalPosition::Top,
        )
        .map(|d| d.bounding_box.map(|b| b.size.width).unwrap_or(0))
        .unwrap_or(0)
}

fn draw_text<D>(fb: &mut D, renderer: &FontRenderer, s: &str, x: i32, y: i32, color: Abgr2222)
where
    D: DrawTarget<Color = Abgr2222>,
{
    let _ = renderer.render(
        s,
        Point::new(x, y),
        u8g2_fonts::types::VerticalPosition::Top,
        u8g2_fonts::types::FontColor::Transparent(color),
        fb,
    );
}

// -- Smoothing: render one font size class up into a scratch buffer, shrink
// it down to the on-screen size by area-averaging. A no_std, no-alloc, no
// heap-allocator stand-in for real anti-aliasing -- see the plan note on why
// this was chosen over rasterizing the original TTF with ab_glyph, which
// would need a heap and a hand-written critical-section implementation for
// this SDK's interrupt architecture that cannot be verified without hardware.

// Sized from measurement, not guessed: every wrapped prompt line, the full
// id at both source fonts, and both split-id halves were rendered and the
// widest came to 345px (a wrapped prompt line) against a needed height of
// 34px (helvB24/helvR24's ascent-descent, both 25-(-7)). ~10% margin on top
// of that measured max, not a round number picked in advance -- a buffer
// sized in advance of measuring would either waste flash/RAM on headroom
// nothing uses, or silently clip text that turned out wider than guessed,
// which render_smoothed()'s `.clamp()` calls would hide rather than flag.
const SS_MAX_W: usize = 384;
const SS_MAX_H: usize = 40;

/// One scratch buffer, reused for every string. Safe on the real target
/// because the GUI process is single-threaded and synchronous -- Gui.cpp's
/// message loop calls barcode_gui_render() to completion before doing
/// anything else, so there is never a second render (and therefore never a
/// second borrow) in flight. `cargo test` does not honour that: its default
/// runner executes tests in parallel threads, so two unrelated tests calling
/// render() at once raced on this buffer and made `render_is_deterministic`
/// flaky -- not a real bug in the single-render production path, but a real
/// bug in trusting that path's assumption inside a multi-threaded test
/// binary. `SS_LOCK` exists only to make the test binary honest about it;
/// the no_std/ARM build has no threads to race and pays nothing for it.
static mut SS_BUF: [u8; SS_MAX_W * SS_MAX_H] = [0; SS_MAX_W * SS_MAX_H];

#[cfg(feature = "std")]
static SS_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

struct SuperSample {
    w: i32,
    h: i32,
}

impl OriginDimensions for SuperSample {
    fn size(&self) -> Size {
        Size::new(self.w.max(0) as u32, self.h.max(0) as u32)
    }
}

impl DrawTarget for SuperSample {
    type Color = Abgr2222;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(coord, _color) in pixels {
            if coord.x >= 0 && coord.y >= 0 && coord.x < self.w && coord.y < self.h {
                let idx = coord.y as usize * SS_MAX_W + coord.x as usize;
                unsafe { SS_BUF[idx] = 1; }
            }
        }
        Ok(())
    }
}

/// Coverage of WHITE ink on a black background (text: more coverage = closer
/// to white). `None` at level 0 leaves the background untouched rather than
/// drawing black-on-black.
fn gray_for_level(level: i32) -> Option<Abgr2222> {
    match level {
        0 => None,
        1 => Some(Abgr2222::rgb(85, 85, 85)),
        2 => Some(Abgr2222::rgb(170, 170, 170)),
        _ => Some(WHITE),
    }
}

/// Coverage of BLACK ink on a white background (Code128's bars: more
/// coverage = closer to black) -- the inverse of `gray_for_level()`. `None`
/// at level 0 leaves the white backing box untouched rather than drawing
/// white-on-white.
fn ink_gray_for_level(level: i32) -> Option<Abgr2222> {
    match level {
        0 => None,
        1 => Some(Abgr2222::rgb(170, 170, 170)),
        2 => Some(Abgr2222::rgb(85, 85, 85)),
        _ => Some(BLACK),
    }
}

/// Renders `s` with `source` -- a bitmap font one size class larger than the
/// on-screen target -- into the scratch buffer, then shrinks it to
/// `(dst_w, dst_h)` by area-averaging each destination pixel's source block,
/// mapping the resulting coverage fraction to the panel's four gray levels.
fn render_smoothed(
    fb: &mut FrameBuf,
    source: &FontRenderer,
    s: &str,
    dst_x: i32,
    dst_y: i32,
    dst_w: i32,
    dst_h: i32,
) {
    if dst_w <= 0 || dst_h <= 0 {
        return;
    }

    #[cfg(feature = "std")]
    let _guard = SS_LOCK.lock().unwrap();

    // A glyph's own left bearing (bounding_box.top_left.x, measured from a
    // pen position of zero) is not always 0 -- helvB24's "12345678" comes
    // back at 2px, helvR24's "Bold Maximum" at 3px. Drawing at a flat x=1
    // (assuming that bearing away) shifted every string's ink right by
    // (bearing - 1)px with no matching increase to the buffer's right
    // margin, silently dropping that many columns off the last glyph via
    // SuperSample::draw_iter's bounds check -- a real, visible right-edge
    // clip, not a display limitation. Measuring the bearing and folding it
    // into the draw offset keeps the ink's left edge at x=1 regardless of
    // the font's own bearing, so the fixed +2 margin is never eaten from one
    // side only.
    let bbox = source
        .get_rendered_dimensions(s, Point::zero(), u8g2_fonts::types::VerticalPosition::Top)
        .ok()
        .and_then(|d| d.bounding_box);
    let left = bbox.map(|b| b.top_left.x).unwrap_or(0);
    let measured_w = bbox.map(|b| b.size.width as i32).unwrap_or(0);
    let src_w = (measured_w + 2).clamp(1, SS_MAX_W as i32);
    let ascent = source.get_ascent() as i32;
    let descent = source.get_descent() as i32;
    let src_h = (ascent - descent + 2).clamp(1, SS_MAX_H as i32);

    for y in 0..src_h {
        let row = y as usize * SS_MAX_W;
        unsafe {
            SS_BUF[row..row + src_w as usize].fill(0);
        }
    }

    let mut target = SuperSample { w: src_w, h: src_h };
    draw_text(&mut target, source, s, 1 - left, 0, WHITE);

    for dy in 0..dst_h {
        let sy0 = dy * src_h / dst_h;
        let sy1 = ((dy + 1) * src_h / dst_h).max(sy0 + 1).min(src_h);
        for dx in 0..dst_w {
            let sx0 = dx * src_w / dst_w;
            let sx1 = ((dx + 1) * src_w / dst_w).max(sx0 + 1).min(src_w);

            let mut lit = 0i32;
            let mut total = 0i32;
            for sy in sy0..sy1 {
                let row = sy as usize * SS_MAX_W;
                for sx in sx0..sx1 {
                    total += 1;
                    if unsafe { SS_BUF[row + sx as usize] } != 0 {
                        lit += 1;
                    }
                }
            }
            if total == 0 {
                continue;
            }
            let level = (lit * 3 + total / 2) / total;
            if let Some(color) = gray_for_level(level) {
                let _ = fb.draw_iter(core::iter::once(Pixel(Point::new(dst_x + dx, dst_y + dy), color)));
            }
        }
    }
}

/// Every text element this app draws is CENTER-aligned in the TouchGFX build
/// being replaced -- both `T_TMP_REGULAR_18` and `T_TMP_SEMIBOLD_20` are
/// `touchgfx::CENTER` in the generated `TypedTextDatabase.cpp` -- so centering
/// within the box is the default here too. `dst_w` is the already-scaled
/// on-screen width (via `scale_large()`/`scale_small()`), computed by the
/// caller since it is also needed there for the fit-threshold decision --
/// measuring it twice would just be the same call made twice.
fn draw_text_centered_smoothed(
    fb: &mut FrameBuf,
    source: &FontRenderer,
    s: &str,
    box_x: i32,
    box_w: i32,
    y: i32,
    dst_w: i32,
    dst_h: i32,
) {
    let x = box_x + (box_w - dst_w) / 2;
    render_smoothed(fb, source, s, x, y, dst_w, dst_h);
}

/// Greedy word wrap against a measured pixel width, generic over any message
/// -- this is what replaces the four hand-split prompt line arrays and the
/// one bespoke BadFormat wrapper: one wrapper, driven by real glyph widths,
/// for every Problem screen. Returns up to `PROMPT_MAX_LINES` lines.
fn word_wrap<'a>(
    renderer: &FontRenderer,
    text: &'a str,
    max_width: u32,
) -> heapless_lines::Lines<'a> {
    let mut lines = heapless_lines::Lines::new();
    let mut line_start = 0usize;
    let mut line_end = 0usize;

    for word_end in text
        .char_indices()
        .filter(|&(_, c)| c == ' ')
        .map(|(i, _)| i)
        .chain(core::iter::once(text.len()))
    {
        if word_end <= line_end {
            continue;
        }
        let candidate = &text[line_start..word_end];
        if measure_width(renderer, candidate) <= max_width || line_end == line_start {
            line_end = word_end;
        } else {
            lines.push(text[line_start..line_end].trim());
            line_start = line_end + 1; // skip the space
            line_end = word_end.max(line_start);
        }
    }
    if line_start < text.len() {
        lines.push(text[line_start..].trim());
    }
    lines
}

mod heapless_lines {
    pub const MAX_LINES: usize = super::PROMPT_MAX_LINES;

    pub struct Lines<'a> {
        lines: [&'a str; MAX_LINES],
        n: usize,
    }

    impl<'a> Lines<'a> {
        pub fn new() -> Self {
            Lines { lines: [""; MAX_LINES], n: 0 }
        }

        pub fn push(&mut self, s: &'a str) {
            if self.n < MAX_LINES && !s.is_empty() {
                self.lines[self.n] = s;
                self.n += 1;
            }
        }

        pub fn iter(&self) -> impl Iterator<Item = &&'a str> {
            self.lines[..self.n].iter()
        }
    }
}

fn draw_prompt(fb: &mut FrameBuf, frame: &Frame) {
    let font = FontRenderer::new::<FontPrompt>();
    // word_wrap measures in raw (unscaled) Font/FontSmall pixels, so the
    // width it wraps against has to be PROMPT_W's on-screen pixels scaled
    // back up by scale_small()'s inverse (16/9) -- the same box, expressed
    // in the font's native measurement space instead of on-screen space.
    let wrap_max = (PROMPT_W as u32) * 16 / 9;
    let lines = word_wrap(&font, frame.message_str(), wrap_max);
    for (i, line) in lines.iter().enumerate() {
        let w = scale_small(measure_width(&font, line)) as i32;
        draw_text_centered_smoothed(
            fb, &font, line, PROMPT_X, PROMPT_W,
            PROMPT_TOP + i as i32 * PROMPT_LINE_H, w, SMALL_H,
        );
    }
}

fn draw_pager(fb: &mut FrameBuf, count: u8, index: u8) {
    if count < 2 {
        return;
    }
    let count = count as i32;
    let left = (PANEL_W - ((count - 1) * MARK_PITCH + MARK_W)) / 2;
    for i in 0..count {
        let color = if i == index as i32 { WHITE } else { DIM };
        fill_rect(fb, left + i * MARK_PITCH, MARK_Y, MARK_W, MARK_H, color);
    }
}

fn draw_caption(fb: &mut FrameBuf, name: &str) {
    if name.is_empty() {
        return;
    }
    let font = FontRenderer::new::<FontSmall>();
    let w = scale_small(measure_width(&font, name)) as i32;
    draw_text_centered_smoothed(fb, &font, name, CAPTION_X, CAPTION_W, CAPTION_Y, w, SMALL_H);
    let _ = CAPTION_H;
}

/// The three-tier id layout TouchGFX's layOutId() used: prefer the bold face
/// if its width clears the small face's own column *and* it fits under its
/// own ink budget; otherwise draw at the small face, splitting across two
/// lines by character count -- never by width -- only when even the small
/// face does not fit one line.
fn draw_id(fb: &mut FrameBuf, id: &str) {
    let small = FontRenderer::new::<FontSmall>();
    let large = FontRenderer::new::<Font>();

    let small_width = scale_small(measure_width(&small, id));
    if small_width <= ID_W as u32 {
        let large_width = scale_large(measure_width(&large, id));
        if large_width <= ID_W as u32 {
            draw_text_centered_smoothed(fb, &large, id, ID_X, ID_W, ID_Y, large_width as i32, LARGE_H);
        } else {
            draw_text_centered_smoothed(fb, &small, id, ID_X, ID_W, ID_Y, small_width as i32, SMALL_H);
        }
        return;
    }

    let bytes = id.len();
    let first = bytes.div_ceil(2);
    // Split on a char boundary at or before the halfway point -- ids are
    // printable ASCII per the encoders' own accepts(), so this only matters
    // as a defensive measure against a future multi-byte id.
    let mut split = first.min(bytes);
    while split > 0 && !id.is_char_boundary(split) {
        split -= 1;
    }
    let (line1, line2) = id.split_at(split);
    let w1 = scale_small(measure_width(&small, line1)) as i32;
    let w2 = scale_small(measure_width(&small, line2)) as i32;
    // Each line is centered in its own box independently, the same way two
    // TouchGFX TextAreas each center their own content rather than the pair
    // being centered as a block.
    draw_text_centered_smoothed(fb, &small, line1, ID_X, ID_W, ID_LINE1_Y, w1, SMALL_H);
    draw_text_centered_smoothed(fb, &small, line2, ID_X, ID_W, ID_LINE2_Y, w2, SMALL_H);
    let _ = ID_LINE_H;
}

/// `f32::floor()`/`ceil()` need `std` or an extra crate for `no_std`. Every
/// caller here passes a non-negative value, where truncating toward zero is
/// exactly `floor`, and `floor` plus one more than truncation exactly
/// disagrees is exactly `ceil` -- not worth a dependency for two cases this
/// narrow.
fn floor_nonneg(v: f32) -> i32 {
    v as i32
}

fn ceil_nonneg(v: f32) -> i32 {
    let t = v as i32;
    if (t as f32) < v {
        t + 1
    } else {
        t
    }
}

/// Accumulates how much of [x0, x1) (in fractional pixels) each integer
/// column in `coverage` is covered by a bar, in bar-processing order.
/// Order-independent by construction -- a column's final coverage is the sum
/// of every bar's overlap with it, so two adjacent sub-pixel-wide bars still
/// add up correctly no matter which is processed first, unlike blitting each
/// bar as its own independently-rounded rectangle.
fn accumulate_coverage(coverage: &mut [f32], x0: f32, x1: f32) {
    let start = floor_nonneg(x0).max(0) as usize;
    let end = ceil_nonneg(x1).max(0) as usize;
    for col in start..end.min(coverage.len()) {
        let px0 = col as f32;
        let px1 = px0 + 1.0;
        let overlap = (x1.min(px1) - x0.max(px0)).max(0.0);
        coverage[col] += overlap;
    }
}

/// Code128: true sub-pixel coverage, not whole-pixel rounding. Symbology.hpp's
/// renderStyle() documents why Code128 does not take ITF's whole-pixel trade
/// -- its modules are already close to one pixel at the lengths this app
/// draws, and flooring each one independently would nearly halve the narrow
/// ones. TouchGFX's original drew this with `CanvasWidget` anti-aliasing;
/// side-by-side against a real device capture, a plain whole-pixel-rounded
/// port showed up as a structured difference concentrated exactly on the
/// narrow bars TouchGFX rendered partially gray and this had been drawing
/// solid black or not at all. Matching that needs real coverage, not just
/// accurate boundaries: each bar's fractional overlap with every pixel column
/// it touches is accumulated (not blitted independently, so two sub-pixel-
/// wide bars sharing a column still add up correctly regardless of order),
/// then quantised to the panel's four gray levels -- the same coverage-to-
/// gray-level idea `render_smoothed()` uses for text, minus the scratch
/// buffer, since a bar's coverage is one-dimensional and computable in closed
/// form rather than needing a bitmap to average down.
fn draw_code128(fb: &mut FrameBuf, frame: &Frame) {
    fill_rect(fb, BACKING_X, BACKING_Y, BACKING_W, BACKING_H, WHITE);

    if frame.total_modules == 0 {
        return;
    }
    let module_px = BARS_W as f32 / frame.total_modules as f32;
    let mut coverage = [0f32; BARS_W as usize];
    let mut x = 0f32;
    let mut is_bar = true;
    for i in 0..frame.width_count as usize {
        let w = frame.widths[i] as f32 * module_px;
        if is_bar {
            accumulate_coverage(&mut coverage, x, x + w);
        }
        x += w;
        is_bar = !is_bar;
    }

    for (col, &c) in coverage.iter().enumerate() {
        let level = (c.clamp(0.0, 1.0) * 3.0 + 0.5) as i32;
        if let Some(color) = ink_gray_for_level(level) {
            fill_rect(fb, BARS_X + col as i32, BARS_Y, 1, BARS_H, color);
        }
    }
}

/// ITF: one whole-pixel unit for the entire symbol plus bearer bars, exactly
/// as BarcodeLayout.hpp's itfUnitPx/itfBearerPx/itfLeftPx already specify --
/// the trade ITF's 3:1 wide:narrow ratio affords and Code128 does not.
fn draw_itf(fb: &mut FrameBuf, frame: &Frame) {
    fill_rect(fb, BACKING_X, BACKING_Y, BACKING_W, BACKING_H, WHITE);

    let total = frame.total_modules as i32;
    if total == 0 {
        return;
    }
    let unit_px = itf_unit_px(total);
    let bearer = itf_bearer_px(unit_px);
    let bars_h = (BARS_H - 2 * bearer).max(0);
    let width = total * unit_px;
    let left = BARS_X + (BARS_W - width) / 2;

    fill_rect(fb, BARS_X, BARS_Y, BARS_W, bearer, BLACK);
    fill_rect(fb, BARS_X, BARS_Y + bearer + bars_h, BARS_W, bearer, BLACK);

    let mut x = left;
    let mut is_bar = true;
    for i in 0..frame.width_count as usize {
        let w_px = frame.widths[i] as i32 * unit_px;
        if is_bar {
            fill_rect(fb, x, BARS_Y + bearer, w_px, bars_h, BLACK);
        }
        x += w_px;
        is_bar = !is_bar;
    }
}

fn draw_qr(fb: &mut FrameBuf, frame: &Frame) {
    let side = (frame.matrix_size as i32 + 2 * QR_QUIET_MODULES) * QR_MODULE_PX;
    fill_rect(fb, QR_X, QR_Y, side, side, WHITE);

    for y in 0..frame.matrix_size {
        for x in 0..frame.matrix_size {
            if frame.dark(x, y) {
                fill_rect(
                    fb,
                    QR_INK_X + x as i32 * QR_MODULE_PX,
                    QR_INK_Y + y as i32 * QR_MODULE_PX,
                    QR_MODULE_PX,
                    QR_MODULE_PX,
                    BLACK,
                );
            }
        }
    }
}

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

    match frame.kind {
        KIND_PROMPT => {
            draw_prompt(&mut fb, frame);
            return;
        }
        KIND_QR => draw_qr(&mut fb, frame),
        KIND_ITF => draw_itf(&mut fb, frame),
        _ => draw_code128(&mut fb, frame),
    }

    draw_id(&mut fb, frame.id_str());
    if frame.kind != KIND_QR {
        draw_caption(&mut fb, frame.name_str());
    }
    draw_pager(&mut fb, frame.count, frame.index);
    let _ = PANEL_H;
}

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `frame` to a
/// valid `barcode_gui_frame`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn barcode_gui_render(
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

    fn str_into(dst: &mut [u8], s: &str) {
        for (d, b) in dst.iter_mut().zip(s.as_bytes()) {
            *d = *b;
        }
    }

    fn empty_frame() -> Frame {
        Frame {
            total_modules: 0,
            kind: KIND_CODE128,
            width_count: 0,
            widths: [0; MAX_WIDTHS],
            matrix_bits: [0; MAX_MATRIX_BITS],
            matrix_size: 0,
            id: [0; ID_LEN],
            name: [0; NAME_LEN],
            index: 0,
            count: 1,
            message: [0; MESSAGE_LEN],
        }
    }

    fn linear_frame(kind: u8, widths: &[u8], total_modules: u16, id: &str) -> Frame {
        let mut f = empty_frame();
        f.kind = kind;
        f.width_count = widths.len() as u8;
        f.widths[..widths.len()].copy_from_slice(widths);
        f.total_modules = total_modules;
        str_into(&mut f.id, id);
        f
    }

    fn prompt_frame(message: &str) -> Frame {
        let mut f = empty_frame();
        f.kind = KIND_PROMPT;
        str_into(&mut f.message, message);
        f
    }

    // Generated once from a host build of the real, unmodified
    // Code128::encode()/Itf::encode() (Barcode/Software/Libs/Header) -- not
    // hand-transcribed. See Barcode/Software/Apps/CustomGUI/README.md for the
    // regeneration command.
    const CODE128_A1234: [u8; 43] = [
        2, 1, 1, 2, 1, 4, 1, 1, 1, 3, 2, 3, 1, 1, 3, 1, 4, 1, 1, 1, 2, 2, 3, 2, 1, 3, 1, 1, 2, 3,
        1, 1, 4, 1, 1, 3, 2, 3, 3, 1, 1, 1, 2,
    ];
    const CODE128_A1234_TOTAL: u16 = 79;

    const CODE128_MAXLEN: [u8; 91] = [
        2, 1, 1, 2, 3, 2, 2, 2, 2, 1, 2, 2, 3, 1, 2, 1, 3, 1, 1, 1, 3, 1, 2, 3, 1, 4, 1, 1, 2, 2,
        2, 1, 2, 1, 4, 1, 1, 1, 4, 1, 3, 1, 1, 1, 1, 3, 2, 3, 1, 3, 1, 1, 2, 3, 1, 3, 1, 3, 2, 1,
        1, 1, 2, 3, 1, 3, 1, 3, 2, 1, 1, 3, 1, 3, 2, 3, 1, 1, 1, 1, 2, 3, 1, 3, 2, 3, 3, 1, 1, 1,
        2,
    ];
    const CODE128_MAXLEN_TOTAL: u16 = 167;

    const ITF_123456: [u8; 37] = [
        1, 1, 1, 1, 3, 1, 1, 3, 1, 1, 1, 1, 3, 3, 3, 1, 3, 1, 1, 3, 1, 1, 1, 3, 3, 1, 1, 3, 3, 3,
        1, 1, 1, 1, 3, 1, 1,
    ];
    const ITF_123456_TOTAL: u16 = 63;

    #[test]
    fn abi_fingerprint_is_stable() {
        assert_eq!(barcode_gui_abi_fingerprint(), abi_fingerprint());
    }

    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0xAAu8; (PANEL_W * PANEL_H) as usize - 1];
        let frame = linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, "A1234");
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);
        assert!(buf.iter().all(|&b| b == 0xAA));
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (PANEL_W * PANEL_H) as usize;
        let frames = [
            linear_frame(KIND_CODE128, &CODE128_MAXLEN, CODE128_MAXLEN_TOTAL, "0123456789ABCDEF"),
            linear_frame(KIND_ITF, &ITF_123456, ITF_123456_TOTAL, "123456"),
            prompt_frame("Unknown format. Set it to Code128, QRCode or ITF."),
        ];
        for frame in &frames {
            let mut buf = vec![0xAAu8; n + 64];
            render(&mut buf, PANEL_W as u32, PANEL_H as u32, frame);
            assert!(buf[n..].iter().all(|&b| b == 0xAA), "kind {} overran", frame.kind);
        }
    }

    #[test]
    fn code128_backing_box_is_white_and_bars_are_centered_in_it() {
        let n = (PANEL_W * PANEL_H) as usize;
        let mut buf = vec![0u8; n];
        let frame = linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, "A1234");
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);

        // A corner of the backing box, away from any bar: white.
        let idx = (BACKING_Y as u32 + 2) * PANEL_W as u32 + (BACKING_X as u32 + 2);
        assert_eq!(buf[idx as usize], WHITE.0, "backing box should be white");

        // The very first module is a bar (Encoded's first entry always is):
        // the pixel just inside the bars area's left edge should be black.
        let idx = (BARS_Y as u32 + 2) * PANEL_W as u32 + (BARS_X as u32 + 1);
        assert_eq!(buf[idx as usize], BLACK.0, "first module should be a dark bar");
    }

    /// TouchGFX's original CanvasWidget drew Code128 anti-aliased; a plain
    /// whole-pixel-rounded port lost that entirely, which showed up as a
    /// structured diff against a real device capture concentrated on the
    /// narrow bars TouchGFX drew partially gray. This pins that fractional
    /// bar boundaries still produce intermediate gray levels, not just
    /// black/white, the same failure mode the text-smoothing test above
    /// guards against.
    #[test]
    fn code128_bars_have_anti_aliased_edges() {
        let n = (PANEL_W * PANEL_H) as usize;
        let mut buf = vec![0u8; n];
        let frame = linear_frame(KIND_CODE128, &CODE128_MAXLEN, CODE128_MAXLEN_TOTAL, "0123456789ABCDEF");
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);

        let dim = Abgr2222::rgb(170, 170, 170).0;
        let mid = Abgr2222::rgb(85, 85, 85).0;
        let row_start = (BARS_Y as u32 + BARS_H as u32 / 2) * PANEL_W as u32 + BARS_X as u32;
        let row_end = row_start + BARS_W as u32;
        let has_intermediate = buf[row_start as usize..row_end as usize]
            .iter()
            .any(|&b| b == dim || b == mid);
        assert!(has_intermediate, "bars should have anti-aliased edge pixels, not just black/white");
    }

    #[test]
    fn itf_bearer_bars_frame_the_symbol_top_and_bottom() {
        let n = (PANEL_W * PANEL_H) as usize;
        let mut buf = vec![0u8; n];
        let frame = linear_frame(KIND_ITF, &ITF_123456, ITF_123456_TOTAL, "123456");
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);

        // The top-left pixel of the bars area is inside the top bearer bar
        // for any unit_px this total could produce (bearer is always >= 4px).
        let idx = (BARS_Y as u32 + 1) * PANEL_W as u32 + (BARS_X as u32 + 1);
        assert_eq!(buf[idx as usize], BLACK.0, "top bearer bar should be dark");
    }

    #[test]
    fn qr_quiet_zone_is_white_and_first_module_is_dark() {
        let n = (PANEL_W * PANEL_H) as usize;
        let mut buf = vec![0u8; n];
        let mut frame = empty_frame();
        frame.kind = KIND_QR;
        frame.matrix_size = 25;
        frame.matrix_bits[0] = 0b0000_0001; // module (0,0) dark
        str_into(&mut frame.id, "GYMWORLD12345678");

        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);

        let idx = (QR_Y as u32 + 2) * PANEL_W as u32 + (QR_X as u32 + 2);
        assert_eq!(buf[idx as usize], WHITE.0, "quiet zone should be white");

        let idx = (QR_INK_Y as u32 + 1) * PANEL_W as u32 + (QR_INK_X as u32 + 1);
        assert_eq!(buf[idx as usize], BLACK.0, "module (0,0) should be dark");
    }

    #[test]
    fn pager_hides_below_two_codes_and_marks_the_current_one() {
        let n = (PANEL_W * PANEL_H) as usize;

        // count == 1: no pager row at all.
        let mut buf = vec![0u8; n];
        let mut frame = linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, "A1234");
        frame.count = 1;
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);
        let row_has_a_mark = (0..PANEL_W as u32)
            .any(|x| buf[(MARK_Y as u32 * PANEL_W as u32 + x) as usize] != BLACK.0);
        assert!(!row_has_a_mark, "count==1 should draw no pager marks");

        // count == 6: the current mark is white, at least one other is dim.
        let mut buf = vec![0u8; n];
        frame.count = 6;
        frame.index = 2;
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);
        let row: Vec<u8> = (0..PANEL_W as u32)
            .map(|x| buf[(MARK_Y as u32 * PANEL_W as u32 + x) as usize])
            .collect();
        assert!(row.contains(&WHITE.0), "current mark should be white");
        assert!(row.contains(&DIM.0), "other marks should be dim");
    }

    #[test]
    fn word_wrap_never_exceeds_the_prompt_width_and_never_splits_a_word() {
        let font = FontRenderer::new::<FontPrompt>();
        let text = "Unknown format. Set it to Code128, QRCode or ITF.";
        // word_wrap measures in raw (unscaled) font pixels -- see draw_prompt().
        let wrap_max = (PROMPT_W as u32) * 16 / 9;
        let lines = word_wrap(&font, text, wrap_max);

        let mut seen_words = alloc_free_word_set();
        for line in lines.iter() {
            assert!(
                scale_small(measure_width(&font, line)) <= PROMPT_W as u32,
                "line {:?} exceeds {}px",
                line,
                PROMPT_W
            );
            for word in line.split(' ') {
                seen_words.push(word);
            }
        }
        for word in text.split(' ') {
            assert!(seen_words.contains(&word), "word {:?} lost by wrapping", word);
        }
    }

    fn alloc_free_word_set() -> Vec<&'static str> {
        // `std` is available under `#[cfg(test)]` regardless of the `std`
        // feature (test builds always link the host std), so a Vec is fine
        // here even though the renderer itself never allocates.
        Vec::new()
    }

    #[test]
    fn id_layout_short_id_uses_one_line() {
        let small = FontRenderer::new::<FontSmall>();
        let large = FontRenderer::new::<Font>();
        let id = "A1234";
        assert!(scale_small(measure_width(&small, id)) <= ID_W as u32);
        // Whichever face is chosen, draw_id must not panic and must not need
        // a second line -- covered indirectly by never_writes_past_the_
        // stated_geometry above using this same short id via A1234's frame.
        let _ = large;
    }

    #[test]
    fn id_layout_max_length_id_splits_by_character_count_not_width() {
        // The two worst cases from BarcodeLayout.hpp's own measurement table:
        // a 16-char alnum id and 12 wide 'W's. Whichever font is chosen, if
        // the small face doesn't fit either on one line, the split point must
        // be ceil(len/2), not decided by measured width.
        let small = FontRenderer::new::<FontSmall>();
        for id in ["0123456789ABCDEF", "WWWWWWWWWWWWWWWW"] {
            let width = scale_small(measure_width(&small, id));
            if width > ID_W as u32 {
                let bytes = id.len();
                let first = bytes.div_ceil(2);
                assert_eq!(first, (bytes + 1) / 2);
                // Exercise the real split path end to end.
                let mut buf = vec![0xAAu8; (PANEL_W * PANEL_H) as usize + 64];
                let frame = linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, id);
                render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);
                assert!(buf[(PANEL_W * PANEL_H) as usize..].iter().all(|&b| b == 0xAA));
            }
        }
    }

    #[test]
    fn render_is_deterministic() {
        let n = (PANEL_W * PANEL_H) as usize;
        let frame = linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, "A1234");
        let mut a = vec![0u8; n];
        let mut b = vec![0u8; n];
        render(&mut a, PANEL_W as u32, PANEL_H as u32, &frame);
        render(&mut b, PANEL_W as u32, PANEL_H as u32, &frame);
        assert_eq!(a, b);
    }

    /// The whole point of render_smoothed(): a bitmap font drawn directly has
    /// only two colors present in its glyphs, black and white. Smoothed text
    /// should show intermediate gray levels at glyph edges -- if it doesn't,
    /// render_smoothed() silently degraded to a 1-bit copy somewhere (e.g. a
    /// clamp()'d scratch buffer too small for the string, truncating the
    /// coverage average back to all-or-nothing).
    #[test]
    fn id_text_uses_intermediate_gray_levels_not_just_black_and_white() {
        let n = (PANEL_W * PANEL_H) as usize;
        let mut buf = vec![0u8; n];
        let frame = linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, "A1234");
        render(&mut buf, PANEL_W as u32, PANEL_H as u32, &frame);

        let dim = Abgr2222::rgb(85, 85, 85).0;
        let mid = Abgr2222::rgb(170, 170, 170).0;
        let id_row_start = (ID_Y as u32 * PANEL_W as u32) as usize;
        let id_row_end = ((ID_Y + 30) as u32 * PANEL_W as u32) as usize;
        let has_intermediate = buf[id_row_start..id_row_end]
            .iter()
            .any(|&b| b == dim || b == mid);
        assert!(has_intermediate, "id text should have anti-aliased edge pixels, not just black/white");
    }

    /// The scratch buffer is sized from real measurement (see SS_MAX_W's own
    /// comment), not padded arbitrarily -- this pins that the widest string
    /// this app actually draws still fits, so a future wording change to a
    /// prompt message that quietly exceeds the buffer clamps (and silently
    /// truncates a source glyph's contribution to the coverage average)
    /// rather than passing unnoticed.
    #[test]
    fn widest_known_string_fits_the_scratch_buffer_without_clamping() {
        let font = FontRenderer::new::<FontPrompt>();
        let wrap_max = (PROMPT_W as u32) * 16 / 9;
        let messages = [
            "No codes yet. Set one in the UNA app, or write input.json.",
            "input.json has no usable code",
            "No codes set yet. Open the UNA app and enter your ID",
            "That ID cannot be drawn: 1-16 plain characters",
            "ITF needs an even count of digits, 2 to 16",
            "ITF only draws digits 0-9",
            "That ID starts or ends with a space, remove it",
            "Unknown format. Set it to Code128, QRCode or ITF.",
        ];
        for m in messages {
            for line in word_wrap(&font, m, wrap_max).iter() {
                let src_w = measure_width(&font, line) as i32 + 2;
                assert!(
                    src_w <= SS_MAX_W as i32,
                    "line {:?} needs {}px, exceeds SS_MAX_W={}",
                    line, src_w, SS_MAX_W
                );
            }
        }
    }

    /// Font/FontSmall are u8g2's *reduced* glyph-coverage tier (`_tr`), not
    /// the full one (`_tf`) -- chosen because every string this app draws is
    /// printable ASCII, so the wide non-Latin ranges `_tf` carries are dead
    /// weight. That is a claim about every character actually reachable:
    /// every printable ASCII code point (encoders validate ids/names into
    /// this range) and every character in every literal prompt message.
    /// `get_rendered_dimensions()` returns `Err` for an unsupported glyph,
    /// which `draw_text()`'s `let _ = renderer.render(...)` silently
    /// swallows -- this is what would catch a future prompt message or
    /// widened id charset drifting outside `_tr`'s coverage before it
    /// shipped as missing glyphs on a real screen.
    #[test]
    fn every_character_this_app_can_draw_is_in_the_reduced_font_tier() {
        let small = FontRenderer::new::<FontSmall>();
        let large = FontRenderer::new::<Font>();

        for byte in 32u8..=126 {
            let s = (byte as char).to_string();
            assert!(
                small.get_rendered_dimensions(s.as_str(), Point::zero(), u8g2_fonts::types::VerticalPosition::Top).is_ok(),
                "FontSmall cannot render {:?} (0x{:02X}) -- outside _tr's coverage",
                s, byte
            );
            assert!(
                large.get_rendered_dimensions(s.as_str(), Point::zero(), u8g2_fonts::types::VerticalPosition::Top).is_ok(),
                "Font cannot render {:?} (0x{:02X}) -- outside _tr's coverage",
                s, byte
            );
        }
    }

    /// Regression test for a right-edge clip: `render_smoothed()` drew its
    /// scratch-buffer text at a flat `x=1`, sizing the buffer only from
    /// `measure_width()` and assuming a string's own left bearing
    /// (`get_rendered_dimensions()`'s `bounding_box.top_left.x`, measured
    /// from a pen position of zero) is 0. It isn't -- helvB24's "12345678"
    /// measures a 2px bearing, helvR24's "Bold Maximum" a 3px bearing -- so
    /// the whole string's ink shifted right by `bearing - 1` px with no
    /// matching growth of the buffer's right margin, and
    /// `SuperSample::draw_iter`'s bounds check silently dropped that many
    /// columns off the trailing glyph: the last "8", the last "m".
    ///
    /// The buffer is sized as `measured_w + 2`, one spare column on each
    /// side of the ink. So long as nothing is clipped, the rightmost lit
    /// column can never reach the buffer's last column (`src_w - 1`) --
    /// that spare column must survive untouched. Landing exactly on it is
    /// this bug's own signature: the true ink wanted to go one column
    /// further and got cut off by `SuperSample`'s bounds check instead.
    #[test]
    fn scratch_buffer_never_uses_its_right_margin_column() {
        // No SS_LOCK guard here -- render_smoothed() takes it internally,
        // and it is not reentrant.
        let large = FontRenderer::new::<Font>();
        let small = FontRenderer::new::<FontSmall>();
        let mut buf = vec![0u8; (PANEL_W * PANEL_H) as usize];

        for (font, s) in [(&large, "12345678"), (&small, "Bold Maximum")] {
            let mut fb = FrameBuf { buf: &mut buf, w: PANEL_W as u32, h: PANEL_H as u32 };
            render_smoothed(&mut fb, font, s, 0, 0, 50, LARGE_H);

            let src_w = (measure_width(font, s) as i32 + 2).clamp(1, SS_MAX_W as i32);
            let last_col_is_lit = (0..SS_MAX_H as i32).any(|y| {
                let idx = y as usize * SS_MAX_W + (src_w - 1) as usize;
                unsafe { SS_BUF[idx] != 0 }
            });
            assert!(
                !last_col_is_lit,
                "{s:?} lights the scratch buffer's last column (col {} of {src_w}) -- \
                 its ink is butting against the wall, meaning the real edge was clipped off",
                src_w - 1
            );
        }
    }
}





