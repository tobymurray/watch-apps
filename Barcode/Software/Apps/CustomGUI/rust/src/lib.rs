#![cfg_attr(not(feature = "std"), no_std)]

use embedded_graphics::{pixelcolor::raw::RawU8, pixelcolor::PixelColor, prelude::*};
use textkit::{faces, Align, Canvas, Face};

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

// -- Text: TextKit's pre-rendered Poppins atlases. Why these faces and not
// others, and what the port replaced, is in Barcode/README.md and Docs/TEXT.md.

/// The preferred id face: the TouchGFX build's SemiBold 20, whose bezel fit the
/// README measured string by string.
static LARGE: &Face = &faces::SEMIBOLD_20_ASCII;
/// Split id lines, the caption and every prompt line: the TouchGFX build's
/// Regular 18, with Latin-1 and Latin Extended-A for a name with a diacritic.
static SMALL: &Face = &faces::REGULAR_18_LATIN;

/// Widest a one-line id may be in the preferred face; wider steps down.
const ID_LARGE_MAX: i32 = 186;

/// Baselines, kept where the u8g2 build drew them so nothing moves: each face
/// used to sit with its top at the box top, and these are those tops plus the
/// scaled ascents (19 for the large face, 14 for the small).
const ID_BASELINE: i32 = ID_Y + 19;
const ID_SMALL_BASELINE: i32 = ID_Y + 14;
const ID_LINE1_BASELINE: i32 = ID_LINE1_Y + 14;
const ID_LINE2_BASELINE: i32 = ID_LINE2_Y + 14;
const CAPTION_BASELINE: i32 = CAPTION_Y + 14;
const PROMPT_BASELINE: i32 = PROMPT_TOP + 14;

fn text_width(face: &Face, s: &str) -> i32 {
    face.measure(s).advance
}

/// Centred in a box the way every TouchGFX TextArea this app had was.
fn draw_centered(fb: &mut FrameBuf, face: &Face, s: &str, box_x: i32, box_w: i32, baseline: i32) {
    let mut canvas = Canvas::round(fb.buf, fb.w, fb.h);
    face.draw(&mut canvas, s, box_x + box_w / 2, baseline, Align::Center, WHITE.0);
}

fn draw_prompt(fb: &mut FrameBuf, frame: &Frame) {
    let mut lines = [""; PROMPT_MAX_LINES];
    let n = SMALL.wrap(frame.message_str(), PROMPT_W, &mut lines).min(PROMPT_MAX_LINES);
    for (i, line) in lines[..n].iter().enumerate() {
        draw_centered(fb, SMALL, line, PROMPT_X, PROMPT_W, PROMPT_BASELINE + i as i32 * PROMPT_LINE_H);
    }
}

/// Coverage of BLACK ink on a white background (Code128's bars: more
/// coverage = closer to black). `None` at level 0 leaves the white backing box
/// untouched rather than drawing white-on-white.
fn ink_gray_for_level(level: i32) -> Option<Abgr2222> {
    match level {
        0 => None,
        1 => Some(Abgr2222::rgb(170, 170, 170)),
        2 => Some(Abgr2222::rgb(85, 85, 85)),
        _ => Some(BLACK),
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
    draw_centered(fb, SMALL, name, CAPTION_X, CAPTION_W, CAPTION_BASELINE);
    let _ = CAPTION_H;
}

/// The three-tier id layout TouchGFX's layOutId() used: prefer the bold face
/// if its width clears the small face's own column *and* it fits under its
/// own ink budget; otherwise draw at the small face, splitting across two
/// lines by character count -- never by width -- only when even the small
/// face does not fit one line.
fn draw_id(fb: &mut FrameBuf, id: &str) {
    if text_width(SMALL, id) <= ID_W {
        if text_width(LARGE, id) <= ID_LARGE_MAX {
            draw_centered(fb, LARGE, id, ID_X, ID_W, ID_BASELINE);
        } else {
            draw_centered(fb, SMALL, id, ID_X, ID_W, ID_SMALL_BASELINE);
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
    // Each line is centered in its own box independently, the same way two
    // TouchGFX TextAreas each center their own content rather than the pair
    // being centered as a block.
    draw_centered(fb, SMALL, line1, ID_X, ID_W, ID_LINE1_BASELINE);
    draw_centered(fb, SMALL, line2, ID_X, ID_W, ID_LINE2_BASELINE);
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

    const PROMPTS: [&str; 8] = [
        "No codes yet. Set one in the UNA app, or write input.json.",
        "input.json has no usable code",
        "No codes set yet. Open the UNA app and enter your ID",
        "That ID cannot be drawn: 1-16 plain characters",
        "ITF needs an even count of digits, 2 to 16",
        "ITF only draws digits 0-9",
        "That ID starts or ends with a space, remove it",
        "Unknown format. Set it to Code128, QRCode or ITF.",
    ];

    #[test]
    fn every_prompt_wraps_into_the_box_without_splitting_or_losing_a_word() {
        for text in PROMPTS {
            let mut lines = [""; PROMPT_MAX_LINES];
            let n = SMALL.wrap(text, PROMPT_W, &mut lines);
            assert!(n <= PROMPT_MAX_LINES, "{text:?} needs {n} lines");
            let mut words = Vec::new();
            for line in &lines[..n] {
                assert!(text_width(SMALL, line) <= PROMPT_W, "line {line:?} exceeds {PROMPT_W}px");
                words.extend(line.split(' '));
            }
            assert_eq!(words, text.split(' ').collect::<Vec<_>>(), "{text:?} lost or split a word");
        }
    }

    /// The README's measured table, in Poppins' own metrics: 186 is the widest
    /// id that fits the large face, and GYMWORLD12345678 is why the split exists.
    #[test]
    fn id_layout_steps_down_at_the_touchgfx_thresholds() {
        assert!(text_width(LARGE, "0123456789ABCD") <= ID_LARGE_MAX);
        assert!(text_width(LARGE, "0123456789ABCDE") > ID_LARGE_MAX);
        assert!(text_width(SMALL, "0123456789ABCDE") <= ID_W);
        assert!(text_width(SMALL, "0123456789ABCDEF") <= ID_W);
        assert!(text_width(SMALL, "GYMWORLD12345678") > ID_W, "the third tier has a reason to exist");
        assert!(text_width(SMALL, "WWWWWWWW") <= ID_W, "the widest half of a split id fits its row");
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


    /// Text is drawn from 2bpp atlases, so glyph edges carry the panel's two
    /// intermediate greys; all-or-nothing here means a face or a blit regressed.
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
        let has_intermediate = buf[id_row_start..id_row_end].iter().any(|&b| b == dim || b == mid);
        assert!(has_intermediate, "id text should have anti-aliased edge pixels, not just black/white");
    }

    /// A missing glyph draws as a box, so the only place to find one would be
    /// a wrist. Ids and names are printable ASCII by the encoders' own rules;
    /// the prompts are literals; a name with a diacritic is what the Latin
    /// face is for.
    #[test]
    fn every_string_this_app_can_draw_has_a_glyph() {
        let ascii: String = (32u8..=126).map(|b| b as char).collect();
        assert!(LARGE.covers(&ascii));
        assert!(SMALL.covers(&ascii));
        for text in PROMPTS {
            assert!(SMALL.covers(text), "{text:?}");
        }
        assert!(SMALL.covers("José Zoë Müller"));
    }

    /// The panel is round and the buffer is square; the id row sits low, where
    /// the chord narrows fast, and a box inset from the buffer's edge reached
    /// the watch twice before anyone measured. Every screen, every tier.
    #[test]
    fn nothing_is_drawn_outside_the_bezel() {
        let n = (PANEL_W * PANEL_H) as usize;
        let mut qr = empty_frame();
        qr.kind = KIND_QR;
        qr.matrix_size = 25;
        qr.matrix_bits.iter_mut().for_each(|b| *b = 0b1010_0101);
        str_into(&mut qr.id, "GYMWORLD12345678");
        let mut with_name = linear_frame(KIND_CODE128, &CODE128_MAXLEN, CODE128_MAXLEN_TOTAL, "0123456789ABCD");
        str_into(&mut with_name.name, "Toby Murray");
        with_name.count = 6;
        with_name.index = 2;
        let frames = [
            linear_frame(KIND_CODE128, &CODE128_A1234, CODE128_A1234_TOTAL, "A1234"),
            with_name,
            linear_frame(KIND_CODE128, &CODE128_MAXLEN, CODE128_MAXLEN_TOTAL, "0123456789ABCDEF"),
            linear_frame(KIND_CODE128, &CODE128_MAXLEN, CODE128_MAXLEN_TOTAL, "GYMWORLD12345678"),
            linear_frame(KIND_CODE128, &CODE128_MAXLEN, CODE128_MAXLEN_TOTAL, "WWWWWWWWWWWWWWWW"),
            linear_frame(KIND_ITF, &ITF_123456, ITF_123456_TOTAL, "123456"),
            qr,
            prompt_frame(PROMPTS[0]),
            prompt_frame(PROMPTS[7]),
        ];
        for frame in &frames {
            let mut buf = vec![0u8; n];
            render(&mut buf, PANEL_W as u32, PANEL_H as u32, frame);
            for y in 0..PANEL_H {
                for x in 0..PANEL_W {
                    let lit = (2 * x - 239).pow(2) + (2 * y - 239).pow(2) <= 239 * 239;
                    assert!(lit || buf[(y * PANEL_W + x) as usize] == BLACK.0, "kind {} id {:?}: ({x},{y}) lit behind the bezel", frame.kind, frame.id_str());
                }
            }
        }
    }
}
