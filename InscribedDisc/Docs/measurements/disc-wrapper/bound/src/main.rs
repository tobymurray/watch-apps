//! Does spelling the bound as `PixelColor<Raw = RawU8>` instead of a custom
//! `to_byte`/`from_byte` trait cost anything? Identical bodies, two bounds.
#![no_std]
#![no_main]

use embedded_graphics::{pixelcolor::raw::{RawData, RawU8}, prelude::*, primitives::Rectangle};

#[derive(Copy, Clone, PartialEq, Eq, Debug, Default)]
pub struct Abgr2222(pub u8);
impl PixelColor for Abgr2222 { type Raw = RawU8; }
impl From<RawU8> for Abgr2222 { fn from(r: RawU8) -> Self { Abgr2222(r.into_inner()) } }
impl From<Abgr2222> for RawU8 { fn from(c: Abgr2222) -> Self { RawU8::new(c.0) } }

// A: InscribedDisc as it stands -- a custom trait each colour type implements.
#[cfg(feature = "bytecolor")]
pub trait Byte: PixelColor + Copy {
    fn to_byte(self) -> u8;
    fn from_byte(b: u8) -> Self;
}
#[cfg(feature = "bytecolor")]
impl Byte for Abgr2222 {
    #[inline] fn to_byte(self) -> u8 { self.0 }
    #[inline] fn from_byte(b: u8) -> Self { Abgr2222(b) }
}

// B: the same trait as a blanket alias over what embedded-graphics already
// says, so no colour type anywhere implements anything.
#[cfg(feature = "rawu8")]
pub trait Byte: PixelColor<Raw = RawU8> + From<RawU8> + Copy
where
    RawU8: From<Self>,
{
    #[inline]
    fn to_byte(self) -> u8 { RawU8::from(self).into_inner() }
    #[inline]
    fn from_byte(b: u8) -> Self { Self::from(RawU8::new(b)) }
}
#[cfg(feature = "rawu8")]
impl<C> Byte for C
where
    C: PixelColor<Raw = RawU8> + From<RawU8> + Copy,
    RawU8: From<C>,
{
}

pub struct Surface<'a, C> { buf: &'a mut [u8], w: i32, h: i32, _c: core::marker::PhantomData<C> }

fn is_lit(x: i32, y: i32, w: i32, h: i32) -> bool {
    if x < 0 || y < 0 || x >= w || y >= h { return false; }
    let (dx, dy, d) = (2 * x - (w - 1), 2 * y - (h - 1), if w < h { w - 1 } else { h - 1 });
    dx * dx + dy * dy <= d * d
}

macro_rules! body {
    ($bound:tt, $to:expr, $from:expr) => {
        impl<'a, C> Surface<'a, C> where C: $bound, RawU8: From<C> {
            pub fn round(buf: &'a mut [u8], w: u32, h: u32) -> Option<Self> {
                let n = (w as usize).checked_mul(h as usize)?;
                if w == 0 || h == 0 || buf.len() < n { return None; }
                Some(Surface { buf: &mut buf[..n], w: w as i32, h: h as i32, _c: core::marker::PhantomData })
            }
            #[inline] fn byte(c: C) -> u8 { $to(c) }
            pub fn color_at(&self, x: i32, y: i32) -> Option<C> {
                if x < 0 || y < 0 || x >= self.w || y >= self.h { return None; }
                let b = self.buf[(y * self.w + x) as usize];
                Some($from(b))
            }
            pub fn clear(&mut self, c: C) { self.buf.fill(Self::byte(c)); }
            pub fn fill_rect(&mut self, x: i32, y: i32, w: i32, h: i32, c: C) {
                if w <= 0 || h <= 0 { return; }
                let b = Self::byte(c);
                let (x0, y0) = (x.max(0), y.max(0));
                let (x1, y1) = ((x + w).min(self.w), (y + h).min(self.h));
                for row in y0..y1 { for col in x0..x1 {
                    if is_lit(col, row, self.w, self.h) {
                        self.buf[(row * self.w + col) as usize] = b;
                    }
                }}
            }
            pub fn bytes(&self) -> &[u8] { self.buf }
        }
        impl<C> OriginDimensions for Surface<'_, C> {
            fn size(&self) -> Size { Size::new(self.w as u32, self.h as u32) }
        }
        impl<C> DrawTarget for Surface<'_, C> where C: $bound, RawU8: From<C> {
            type Color = C;
            type Error = core::convert::Infallible;
            fn draw_iter<I: IntoIterator<Item = Pixel<C>>>(&mut self, px: I) -> Result<(), Self::Error> {
                for Pixel(p, c) in px {
                    if is_lit(p.x, p.y, self.w, self.h) {
                        self.buf[(p.y * self.w + p.x) as usize] = Self::byte(c);
                    }
                }
                Ok(())
            }
            fn fill_solid(&mut self, a: &Rectangle, c: C) -> Result<(), Self::Error> {
                self.fill_rect(a.top_left.x, a.top_left.y, a.size.width as i32, a.size.height as i32, c);
                Ok(())
            }
        }
    };
}

// One body, both bounds: only how `Byte` is satisfied differs.
body!(Byte, |c: C| c.to_byte(), |b: u8| C::from_byte(b));

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let mut fb = [0u8; 240 * 240];
    let mut s = Surface::<Abgr2222>::round(&mut fb, 240, 240).unwrap();
    s.clear(Abgr2222(0));
    s.fill_rect(20, 100, 200, 40, Abgr2222(0xFF));
    let _ = s.draw_iter([Pixel(Point::new(core::hint::black_box(7), 7), Abgr2222(0x0F))]);
    core::hint::black_box(s.color_at(core::hint::black_box(9), 9));
    core::hint::black_box(s.bytes());
    loop {}
}

#[panic_handler]
fn p(_: &core::panic::PanicInfo) -> ! { loop {} }
