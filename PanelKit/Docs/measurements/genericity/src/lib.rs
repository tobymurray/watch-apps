//! Does generic-over-`PixelColor` cost more `.text` on thumbv8m.main-none-eabihf
//! than the concrete `Abgr2222` path the apps use today?
//!
//! The same four primitives -- the ones tier 0 and tier 1 of the kit would
//! actually contain -- written three ways, with only the abstraction differing:
//!
//!   `concrete`     one colour type, direct byte writes, row `fill` fast path
//!   `generic`      `DrawTarget`-generic, instantiated at one colour type
//!   `generic_two`  the same generic code, instantiated at two colour types
//!
//! `generic` against `concrete` is the cost of the abstraction; `generic_two`
//! against `generic` is the cost of the publishable claim, which is what a
//! second adopter's colour type would add.
#![no_std]

use embedded_graphics::{
    pixelcolor::{raw::RawU8, PixelColor, Rgb565},
    prelude::*,
    primitives::Rectangle,
};

#[panic_handler]
fn on_panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Abgr2222(pub u8);

impl PixelColor for Abgr2222 {
    type Raw = RawU8;
}

pub const W: i32 = 240;
pub const H: i32 = 240;

// ---------------------------------------------------------------------------
// Concrete: one colour type, bytes written straight into the framebuffer.
// ---------------------------------------------------------------------------

#[cfg(feature = "concrete")]
pub struct Surface<'a> {
    pub buf: &'a mut [u8],
    pub w: i32,
    pub h: i32,
}

#[cfg(feature = "concrete")]
impl Surface<'_> {
    fn is_lit(&self, x: i32, y: i32) -> bool {
        let dx = 2 * x - (self.w - 1);
        let dy = 2 * y - (self.h - 1);
        let d = self.w.min(self.h) - 1;
        dx * dx + dy * dy <= d * d
    }
}

#[cfg(feature = "concrete")]
pub mod concrete {
    use super::*;

    pub fn fill_rect(s: &mut Surface, x: i32, y: i32, w: i32, h: i32, c: Abgr2222) {
        if w <= 0 || h <= 0 {
            return;
        }
        let x0 = x.max(0);
        let y0 = y.max(0);
        let x1 = (x + w).min(s.w);
        let y1 = (y + h).min(s.h);
        if x0 >= x1 || y0 >= y1 {
            return;
        }
        for row in y0..y1 {
            let a = (row * s.w + x0) as usize;
            let b = (row * s.w + x1) as usize;
            s.buf[a..b].fill(c.0);
        }
    }

    pub fn fill_disc_rect(s: &mut Surface, x: i32, y: i32, w: i32, h: i32, c: Abgr2222) {
        for row in y.max(0)..(y + h).min(s.h) {
            for col in x.max(0)..(x + w).min(s.w) {
                if s.is_lit(col, row) {
                    s.buf[(row * s.w + col) as usize] = c.0;
                }
            }
        }
    }

    pub fn marks(s: &mut Surface, n: i32, index: i32, pitch: i32, mw: i32, mh: i32, y: i32, on: Abgr2222, off: Abgr2222) {
        let left = s.w / 2 - ((n - 1) * pitch) / 2 - mw / 2;
        for i in 0..n {
            let c = if i == index { on } else { off };
            fill_rect(s, left + i * pitch, y, mw, mh, c);
        }
    }

    pub fn arc(s: &mut Surface, cx: i32, cy: i32, r: i32, thick: i32, from: i32, to: i32, c: Abgr2222) {
        let mut a = from;
        while a < to {
            let (sin, cos) = sin_cos(a);
            for t in 0..thick {
                let rr = r - t;
                let px = cx + (cos * rr as f32) as i32;
                let py = cy + (sin * rr as f32) as i32;
                if px >= 0 && py >= 0 && px < s.w && py < s.h && s.is_lit(px, py) {
                    s.buf[(py * s.w + px) as usize] = c.0;
                }
            }
            a += 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Generic: the identical algorithms over any `DrawTarget`.
// ---------------------------------------------------------------------------

#[cfg(any(feature = "generic", feature = "generic_two", feature = "generic_fast"))]
pub mod generic {
    use super::*;

    pub fn fill_rect<D: DrawTarget>(d: &mut D, x: i32, y: i32, w: i32, h: i32, c: D::Color) {
        if w <= 0 || h <= 0 {
            return;
        }
        let _ = d.fill_solid(
            &Rectangle::new(Point::new(x, y), Size::new(w as u32, h as u32)),
            c,
        );
    }

    pub fn fill_disc_rect<D: DrawTarget>(d: &mut D, x: i32, y: i32, w: i32, h: i32, c: D::Color) {
        let sz = d.bounding_box().size;
        let (sw, sh) = (sz.width as i32, sz.height as i32);
        let lit = |px: i32, py: i32| {
            let dx = 2 * px - (sw - 1);
            let dy = 2 * py - (sh - 1);
            let dd = sw.min(sh) - 1;
            dx * dx + dy * dy <= dd * dd
        };
        for row in y.max(0)..(y + h).min(sh) {
            for col in x.max(0)..(x + w).min(sw) {
                if lit(col, row) {
                    let _ = d.draw_iter(core::iter::once(Pixel(Point::new(col, row), c)));
                }
            }
        }
    }

    pub fn marks<D: DrawTarget>(d: &mut D, n: i32, index: i32, pitch: i32, mw: i32, mh: i32, y: i32, on: D::Color, off: D::Color) {
        let sw = d.bounding_box().size.width as i32;
        let left = sw / 2 - ((n - 1) * pitch) / 2 - mw / 2;
        for i in 0..n {
            let c = if i == index { on } else { off };
            fill_rect(d, left + i * pitch, y, mw, mh, c);
        }
    }

    pub fn arc<D: DrawTarget>(d: &mut D, cx: i32, cy: i32, r: i32, thick: i32, from: i32, to: i32, c: D::Color) {
        let sz = d.bounding_box().size;
        let (sw, sh) = (sz.width as i32, sz.height as i32);
        let lit = |px: i32, py: i32| {
            let dx = 2 * px - (sw - 1);
            let dy = 2 * py - (sh - 1);
            let dd = sw.min(sh) - 1;
            dx * dx + dy * dy <= dd * dd
        };
        let mut a = from;
        while a < to {
            let (sin, cos) = sin_cos(a);
            for t in 0..thick {
                let rr = r - t;
                let px = cx + (cos * rr as f32) as i32;
                let py = cy + (sin * rr as f32) as i32;
                if px >= 0 && py >= 0 && px < sw && py < sh && lit(px, py) {
                    let _ = d.draw_iter(core::iter::once(Pixel(Point::new(px, py), c)));
                }
            }
            a += 1;
        }
    }
}

/// A tiny fixed-cost trig so the arc's own maths is identical in all three
/// variants and does not itself differ between them.
#[allow(dead_code)]
fn sin_cos(deg: i32) -> (f32, f32) {
    let rad = deg as f32 * 0.017_453_292;
    // Two terms is plenty for a size experiment and pulls in no libm.
    let x = rad - 6.283_185 * ((rad * 0.159_154_94) as i32 as f32);
    let s = x - x * x * x / 6.0;
    let c = 1.0 - x * x / 2.0;
    (s, c)
}

// ---------------------------------------------------------------------------
// A framebuffer `DrawTarget`, so the generic path has something to run on.
// ---------------------------------------------------------------------------

#[cfg(any(feature = "generic", feature = "generic_two", feature = "generic_fast"))]
pub struct FrameBuf<'a, C> {
    pub buf: &'a mut [u8],
    pub w: u32,
    pub h: u32,
    pub _c: core::marker::PhantomData<C>,
}

#[cfg(any(feature = "generic", feature = "generic_two", feature = "generic_fast"))]
impl<C> OriginDimensions for FrameBuf<'_, C> {
    fn size(&self) -> Size {
        Size::new(self.w, self.h)
    }
}

#[cfg(any(feature = "generic", feature = "generic_two", feature = "generic_fast"))]
impl<C: PixelColor + Into<u8>> DrawTarget for FrameBuf<'_, C> {
    type Color = C;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        let (w, h) = (self.w as i32, self.h as i32);
        for Pixel(coord, color) in pixels {
            if coord.x >= 0 && coord.y >= 0 && coord.x < w && coord.y < h {
                let idx = (coord.y as u32 * self.w + coord.x as u32) as usize;
                self.buf[idx] = color.into();
            }
        }
        Ok(())
    }

    /// The row fill the concrete path uses, reached through the generic API:
    /// `fill_solid`'s default walks `fill_contiguous` a pixel at a time.
    #[cfg(feature = "generic_fast")]
    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        let b: u8 = color.into();
        let x0 = area.top_left.x.max(0);
        let y0 = area.top_left.y.max(0);
        let x1 = (area.top_left.x + area.size.width as i32).min(self.w as i32);
        let y1 = (area.top_left.y + area.size.height as i32).min(self.h as i32);
        if x0 >= x1 || y0 >= y1 {
            return Ok(());
        }
        for row in y0..y1 {
            let a = (row * self.w as i32 + x0) as usize;
            let z = (row * self.w as i32 + x1) as usize;
            self.buf[a..z].fill(b);
        }
        Ok(())
    }
}

impl From<Abgr2222> for u8 {
    fn from(c: Abgr2222) -> u8 {
        c.0
    }
}

/// Only so `generic_two` has a second, genuinely different colour type to
/// monomorphise at; a real second adopter's would be no simpler.
#[cfg(feature = "generic_two")]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Gray4(pub u8);
#[cfg(feature = "generic_two")]
impl PixelColor for Gray4 {
    type Raw = RawU8;
}
#[cfg(feature = "generic_two")]
impl From<Gray4> for u8 {
    fn from(c: Gray4) -> u8 {
        c.0 * 85
    }
}

// ---------------------------------------------------------------------------
// Entry points, so nothing above is dead code.
// ---------------------------------------------------------------------------

#[cfg(feature = "concrete")]
#[no_mangle]
pub extern "C" fn draw(buf: *mut u8, len: usize) {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    let mut surf = Surface { buf: s, w: W, h: H };
    concrete::fill_rect(&mut surf, 10, 10, 100, 40, Abgr2222(0xFF));
    concrete::fill_disc_rect(&mut surf, 0, 0, W, 30, Abgr2222(0xC0));
    concrete::marks(&mut surf, 5, 2, 14, 6, 6, 198, Abgr2222(0xFF), Abgr2222(0xD5));
    concrete::arc(&mut surf, 120, 120, 110, 6, 30, 330, Abgr2222(0xFC));
}

#[cfg(feature = "generic")]
#[no_mangle]
pub extern "C" fn draw(buf: *mut u8, len: usize) {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    let mut fb = FrameBuf::<Abgr2222> { buf: s, w: W as u32, h: H as u32, _c: core::marker::PhantomData };
    generic::fill_rect(&mut fb, 10, 10, 100, 40, Abgr2222(0xFF));
    generic::fill_disc_rect(&mut fb, 0, 0, W, 30, Abgr2222(0xC0));
    generic::marks(&mut fb, 5, 2, 14, 6, 6, 198, Abgr2222(0xFF), Abgr2222(0xD5));
    generic::arc(&mut fb, 120, 120, 110, 6, 30, 330, Abgr2222(0xFC));
}

#[cfg(feature = "generic_two")]
#[no_mangle]
pub extern "C" fn draw(buf: *mut u8, len: usize) {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    let mut fb = FrameBuf::<Abgr2222> { buf: s, w: W as u32, h: H as u32, _c: core::marker::PhantomData };
    generic::fill_rect(&mut fb, 10, 10, 100, 40, Abgr2222(0xFF));
    generic::fill_disc_rect(&mut fb, 0, 0, W, 30, Abgr2222(0xC0));
    generic::marks(&mut fb, 5, 2, 14, 6, 6, 198, Abgr2222(0xFF), Abgr2222(0xD5));
    generic::arc(&mut fb, 120, 120, 110, 6, 30, 330, Abgr2222(0xFC));
}

#[cfg(feature = "generic_two")]
#[no_mangle]
pub extern "C" fn draw_second(buf: *mut u8, len: usize) {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    let mut fb = FrameBuf::<Gray4> { buf: s, w: W as u32, h: H as u32, _c: core::marker::PhantomData };
    generic::fill_rect(&mut fb, 10, 10, 100, 40, Gray4(3));
    generic::fill_disc_rect(&mut fb, 0, 0, W, 30, Gray4(0));
    generic::marks(&mut fb, 5, 2, 14, 6, 6, 198, Gray4(3), Gray4(2));
    generic::arc(&mut fb, 120, 120, 110, 6, 30, 330, Gray4(3));
}

#[cfg(feature = "generic_fast")]
#[no_mangle]
pub extern "C" fn draw(buf: *mut u8, len: usize) {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    let mut fb = FrameBuf::<Abgr2222> { buf: s, w: W as u32, h: H as u32, _c: core::marker::PhantomData };
    generic::fill_rect(&mut fb, 10, 10, 100, 40, Abgr2222(0xFF));
    generic::fill_disc_rect(&mut fb, 0, 0, W, 30, Abgr2222(0xC0));
    generic::marks(&mut fb, 5, 2, 14, 6, 6, 198, Abgr2222(0xFF), Abgr2222(0xD5));
    generic::arc(&mut fb, 120, 120, 110, 6, 30, 330, Abgr2222(0xFC));
}

/// The `Rgb565` instantiation an off-watch adopter would actually reach for,
/// kept out of the default build so it costs the watch nothing.
#[cfg(feature = "generic_two")]
#[no_mangle]
pub extern "C" fn rgb565_is_reachable() -> u16 {
    Rgb565::new(1, 2, 3).into_storage()
}
