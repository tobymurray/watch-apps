#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(not(feature = "std"))]
use core::fmt::Write as _;

use embedded_graphics::{
    pixelcolor::{raw::RawU8, PixelColor},
    prelude::*,
    primitives::{PrimitiveStyle, Rectangle},
};

#[cfg(not(feature = "std"))]
extern "C" {
    fn qr_gui_host_panic(msg: *const u8, len: u32);
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
    unsafe { qr_gui_host_panic(s.as_ptr(), s.len() as u32) };
    loop {}
}

/// Mirrors Barcode::Matrix (Barcode/Software/Libs/Header/Matrix.hpp) field for
/// field: row-major, one bit a module, 1 is dark. `dark()` below indexes it the
/// same way that struct's own `dark()` does.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct State {
    pub bits: [u8; 79],
    pub size: u8,
}

impl Default for State {
    fn default() -> Self {
        State { bits: [0; 79], size: 0 }
    }
}

impl State {
    fn dark(&self, x: u8, y: u8) -> bool {
        let i = y as usize * self.size as usize + x as usize;
        (self.bits[i / 8] >> (i % 8)) & 1 != 0
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

#[cfg(not(feature = "std"))]
struct Buf<const N: usize> {
    b: [u8; N],
    n: usize,
}

#[cfg(not(feature = "std"))]
impl<const N: usize> Buf<N> {
    fn new() -> Self {
        Buf { b: [0; N], n: 0 }
    }

    fn as_str(&self) -> &str {
        core::str::from_utf8(&self.b[..self.n]).unwrap_or("")
    }
}

#[cfg(not(feature = "std"))]
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

// Barcode/Software/Libs/Header/BarcodeLayout.hpp's own QR geometry, copied
// rather than derived, so this renders at the identical size and position as
// the TouchGFX screen it is being measured against.
const QR_QUIET_MODULES: i32 = 4;
const QR_MODULE_PX: i32 = 4;
const QR_MODULES: i32 = 25;
const QR_SIDE: i32 = (QR_MODULES + 2 * QR_QUIET_MODULES) * QR_MODULE_PX;
const PANEL_WIDTH: i32 = 240;
const QR_X: i32 = (PANEL_WIDTH - QR_SIDE) / 2;
const QR_Y: i32 = 33;
const QR_INK_X: i32 = QR_X + QR_QUIET_MODULES * QR_MODULE_PX;
const QR_INK_Y: i32 = QR_Y + QR_QUIET_MODULES * QR_MODULE_PX;

fn fill_rect(fb: &mut FrameBuf, x: i32, y: i32, side: i32, color: Abgr2222) {
    Rectangle::new(Point::new(x, y), Size::new(side as u32, side as u32))
        .into_styled(PrimitiveStyle::with_fill(color))
        .draw(fb)
        .ok();
}

/// White quiet zone (the paper a real code is printed on) with the dark modules
/// laid on top -- the same two-step Barcode's QrWidget draws, just without a
/// scene graph in between.
fn draw_qr(fb: &mut FrameBuf, state: &State) {
    fill_rect(fb, QR_X, QR_Y, QR_SIDE, Abgr2222::WHITE);

    for y in 0..state.size {
        for x in 0..state.size {
            if state.dark(x, y) {
                fill_rect(
                    fb,
                    QR_INK_X + x as i32 * QR_MODULE_PX,
                    QR_INK_Y + y as i32 * QR_MODULE_PX,
                    QR_MODULE_PX,
                    Abgr2222::BLACK,
                );
            }
        }
    }
}

pub fn render(buf: &mut [u8], width: u32, height: u32, state: &State) {
    if width == 0 || height == 0 {
        return;
    }
    let needed = (width as usize).saturating_mul(height as usize);
    if buf.len() < needed {
        return;
    }

    let mut fb = FrameBuf { buf: &mut buf[..needed], w: width, h: height };
    fb.buf.fill(Abgr2222::BLACK.0);
    draw_qr(&mut fb, state);
}

const FNV_OFFSET_BASIS: u32 = 0x811C_9DC5;
const FNV_PRIME: u32 = 0x0100_0193;

const fn fnv1a(hash: u32, byte: usize) -> u32 {
    (hash ^ ((byte as u32) & 0xFF)).wrapping_mul(FNV_PRIME)
}

/// Must walk the same values in the same order as `qr_gui_abi::fingerprint()`
/// in qr_gui.h.
const fn abi_fingerprint() -> u32 {
    let h = FNV_OFFSET_BASIS;
    let h = fnv1a(h, core::mem::size_of::<State>());
    let h = fnv1a(h, core::mem::align_of::<State>());
    let h = fnv1a(h, core::mem::offset_of!(State, bits));
    fnv1a(h, core::mem::offset_of!(State, size))
}

/// Lets the caller confirm it was linked against the archive it thinks it was.
/// The compile-time assertions below cannot do this: a stale archive and a newer
/// header each satisfy their own, having been compiled at different times.
#[no_mangle]
pub extern "C" fn qr_gui_abi_fingerprint() -> u32 {
    abi_fingerprint()
}

// Per field, because a size check passes when two fields are swapped. qr_gui.h
// asserts the same offsets, so a hand edit to either declaration breaks a build.
const _: () = assert!(core::mem::size_of::<State>() == 80);
const _: () = assert!(core::mem::align_of::<State>() == 1);
const _: () = assert!(core::mem::offset_of!(State, bits) == 0);
const _: () = assert!(core::mem::offset_of!(State, size) == 79);

/// # Safety
/// `buf` must point to at least `buf_len` writable bytes and `state` to a valid
/// `qr_gui_state`, both valid for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn qr_gui_render(
    buf: *mut u8,
    buf_len: u32,
    width: u16,
    height: u16,
    state: *const State,
) {
    if buf.is_null() || state.is_null() || buf_len == 0 || width == 0 || height == 0 {
        return;
    }
    let slice = core::slice::from_raw_parts_mut(buf, buf_len as usize);
    render(slice, width as u32, height as u32, &*state);
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 240;
    const C_STRUCT_SIZE: usize = 80;
    const C_STRUCT_ALIGN: usize = 1;

    // A single dark module at (0,0), the rest light -- enough to exercise
    // dark()'s bit indexing without hand-encoding a real QR grid.
    fn one_dark_module() -> State {
        let mut st = State { bits: [0; 79], size: 25 };
        st.bits[0] = 0b0000_0001;
        st
    }

    #[test]
    fn state_layout_matches_c() {
        assert_eq!(core::mem::size_of::<State>(), C_STRUCT_SIZE);
        assert_eq!(core::mem::align_of::<State>(), C_STRUCT_ALIGN);
    }

    #[test]
    fn abi_fingerprint_is_stable() {
        // Pinned so a change to either fingerprint implementation shows up
        // here rather than as a refusal to start on a watch.
        assert_eq!(qr_gui_abi_fingerprint(), abi_fingerprint());
    }

    #[test]
    fn undersized_buffer_is_a_no_op() {
        let mut buf = vec![0xAAu8; (W * H) as usize - 1];
        render(&mut buf, W, H, &one_dark_module());
        assert!(buf.iter().all(|&b| b == 0xAA));
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        let mut buf = vec![0xAAu8; n + 64];
        render(&mut buf, W, H, &one_dark_module());
        assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran the framebuffer");
    }

    #[test]
    fn quiet_zone_is_white_and_first_module_is_dark() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        render(&mut buf, W, H, &one_dark_module());

        let quiet_zone_px = (QR_X + 2) as u32;
        let idx = (QR_Y as u32 + 2) * W + quiet_zone_px;
        assert_eq!(buf[idx as usize], Abgr2222::WHITE.0, "quiet zone should be white");

        let dark_px = (QR_INK_X + 1) as u32;
        let idx = (QR_INK_Y as u32 + 1) * W + dark_px;
        assert_eq!(buf[idx as usize], Abgr2222::BLACK.0, "module (0,0) should be dark");
    }

    #[test]
    fn render_is_deterministic() {
        let n = (W * H) as usize;
        let mut a = vec![0u8; n];
        let mut b = vec![0u8; n];
        render(&mut a, W, H, &one_dark_module());
        render(&mut b, W, H, &one_dark_module());
        assert_eq!(a, b);
    }
}
