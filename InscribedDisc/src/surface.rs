//! The framebuffer, as `embedded-graphics` sees it.
//!
//! One surface owns the clip, so a shape and a glyph drawn into the same bytes
//! cannot disagree about where the glass ends.

use core::marker::PhantomData;

use embedded_graphics::{
    pixelcolor::raw::{RawData, RawU8},
    prelude::*,
    primitives::Rectangle,
};

use crate::geometry;

/// What the panel shows of what is drawn.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Clip {
    /// Only the inscribed disc is glass.
    Disc,
    /// The whole buffer, for a square panel, a test or a host tool.
    Rect,
}

/// A byte-per-pixel framebuffer that clips to the panel.
///
/// MEASURED: generic over the colour costs 66 bytes of `.text` against 884 for
/// the same primitives written concretely, and a second colour type in one
/// binary costs 848 more. Re-run `InscribedDisc/Docs/measurements/genericity`.
pub struct Surface<'a, C> {
    buf: &'a mut [u8],
    w: i32,
    h: i32,
    clip: Clip,
    _color: PhantomData<C>,
}

/// A colour this crate can write into a byte-per-pixel framebuffer.
///
/// Satisfied by any `embedded-graphics` colour whose `Raw` is [`RawU8`] with no
/// impl written anywhere, [`Gray8`](embedded_graphics::pixelcolor::Gray8)
/// included, because that crate's colour macros already generate the two `From`
/// conversions this needs. `Into<RawU8>` is a supertrait rather than a
/// `where RawU8: From<Self>` clause: a where-clause on a trait is not an implied
/// bound, so that spelling leaks the obligation into every downstream signature.
pub trait ByteColor: PixelColor<Raw = RawU8> + From<RawU8> + Into<RawU8> + Copy {
    /// The byte this colour occupies in the framebuffer.
    #[inline]
    fn to_byte(self) -> u8 {
        self.into().into_inner()
    }
    /// The colour that byte encodes.
    #[inline]
    fn from_byte(b: u8) -> Self {
        Self::from(RawU8::new(b))
    }
}

impl<C: PixelColor<Raw = RawU8> + From<RawU8> + Into<RawU8> + Copy> ByteColor for C {}

impl<'a, C: ByteColor> Surface<'a, C> {
    /// A round panel: only the inscribed disc is glass.
    ///
    /// `None` rather than a panic when the buffer is smaller than the geometry
    /// says, because that geometry arrives from outside the app.
    pub fn round(buf: &'a mut [u8], w: u32, h: u32) -> Option<Self> {
        Self::new(buf, w, h, Clip::Disc)
    }

    /// A square target with no bezel.
    pub fn rect(buf: &'a mut [u8], w: u32, h: u32) -> Option<Self> {
        Self::new(buf, w, h, Clip::Rect)
    }

    fn new(buf: &'a mut [u8], w: u32, h: u32, clip: Clip) -> Option<Self> {
        if w == 0 || h == 0 {
            return None;
        }
        let needed = (w as usize).checked_mul(h as usize)?;
        if buf.len() < needed {
            return None;
        }
        Some(Surface { buf: &mut buf[..needed], w: w as i32, h: h as i32, clip, _color: PhantomData })
    }

    /// Whether the panel would show this pixel at all.
    #[inline]
    pub fn is_lit(&self, x: i32, y: i32) -> bool {
        match self.clip {
            Clip::Rect => x >= 0 && y >= 0 && x < self.w && y < self.h,
            Clip::Disc => geometry::is_lit(x, y, self.w, self.h),
        }
    }

    /// The framebuffer's width in pixels, bezel included.
    pub fn width(&self) -> i32 {
        self.w
    }

    /// The framebuffer's height in pixels, bezel included.
    pub fn height(&self) -> i32 {
        self.h
    }

    /// The raw bytes, for a caller pushing them at a display.
    pub fn bytes(&self) -> &[u8] {
        self.buf
    }

    /// The raw bytes, mutably, **bypassing the clip**.
    ///
    /// For a text implementation that rasterises through its own surface type.
    /// An adapter using this must apply [`crate::geometry::is_lit`] itself, and
    /// must have a test that says it does.
    pub fn bytes_mut(&mut self) -> &mut [u8] {
        self.buf
    }

    /// Every pixel, bezel included: the whole buffer is handed to the display,
    /// so the corners must hold the ground colour and not the last frame's.
    pub fn clear(&mut self, color: C) {
        self.buf.fill(color.to_byte());
    }

    #[inline]
    fn put(&mut self, x: i32, y: i32, b: u8) {
        if self.is_lit(x, y) {
            self.buf[(y * self.w + x) as usize] = b;
        }
    }

    /// The byte at a pixel, or `None` outside the buffer.
    pub fn get(&self, x: i32, y: i32) -> Option<u8> {
        if x < 0 || y < 0 || x >= self.w || y >= self.h {
            return None;
        }
        Some(self.buf[(y * self.w + x) as usize])
    }

    /// A rectangle filled a row at a time.
    ///
    /// Not `Rectangle::into_styled().draw()`, which pulls in the point-iterator
    /// layer once per distinct colour it is called with.
    pub fn fill_rect(&mut self, x: i32, y: i32, w: i32, h: i32, color: C) {
        if w <= 0 || h <= 0 {
            return;
        }
        let b = color.to_byte();
        let x0 = x.max(0);
        let y0 = y.max(0);
        let x1 = (x + w).min(self.w);
        let y1 = (y + h).min(self.h);
        if x0 >= x1 || y0 >= y1 {
            return;
        }
        match self.clip {
            Clip::Rect => {
                for row in y0..y1 {
                    let a = (row * self.w + x0) as usize;
                    let z = (row * self.w + x1) as usize;
                    self.buf[a..z].fill(b);
                }
            }
            Clip::Disc => {
                for row in y0..y1 {
                    if let Some((a, z)) = geometry::lit_span(x0, x1, row, self.w, self.h) {
                        let i = (row * self.w + a) as usize;
                        let j = (row * self.w + z) as usize;
                        self.buf[i..j].fill(b);
                    }
                }
            }
        }
    }
}

impl<C> OriginDimensions for Surface<'_, C> {
    fn size(&self) -> Size {
        Size::new(self.w as u32, self.h as u32)
    }
}

impl<C: ByteColor> DrawTarget for Surface<'_, C> {
    type Color = C;
    type Error = core::convert::Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(coord, color) in pixels {
            self.put(coord.x, coord.y, color.to_byte());
        }
        Ok(())
    }

    /// MEASURED: overriding this costs 16 bytes of `.text` *less* than the
    /// default, which walks `fill_contiguous` a pixel at a time. Re-run
    /// `InscribedDisc/Docs/measurements/genericity`.
    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        self.fill_rect(
            area.top_left.x,
            area.top_left.y,
            area.size.width as i32,
            area.size.height as i32,
            color,
        );
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use embedded_graphics::pixelcolor::Gray8;

    use super::*;

    // Gray8, not this crate's own colour: these tests are about the buffer and
    // the clip, and a foreign `RawU8` colour is what says `ByteColor`'s blanket
    // impl reaches one.

    const W: u32 = 240;
    const H: u32 = 240;

    fn surface(buf: &mut [u8]) -> Surface<'_, Gray8> {
        Surface::round(buf, W, H).unwrap()
    }

    #[test]
    fn a_buffer_smaller_than_its_geometry_is_refused() {
        let mut buf = vec![0u8; (W * H) as usize - 1];
        assert!(Surface::<Gray8>::round(&mut buf, W, H).is_none());
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        let mut buf = vec![0xAAu8; n + 64];
        {
            let mut s = surface(&mut buf);
            s.clear(Gray8::BLACK);
            s.fill_rect(-50, -50, 1000, 1000, Gray8::WHITE);
        }
        assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran the framebuffer");
    }

    #[test]
    fn nothing_is_drawn_outside_the_lit_disc() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = surface(&mut buf);
            s.clear(Gray8::BLACK);
            s.fill_rect(0, 0, W as i32, H as i32, Gray8::WHITE);
            // Through the generic path too, not only the fast one.
            let _ = s.draw_iter((0..W as i32).map(|x| Pixel(Point::new(x, 0), Gray8::WHITE)));
        }
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                if !geometry::is_lit(x, y, W as i32, H as i32) {
                    assert_eq!(
                        buf[(y * W as i32 + x) as usize],
                        Gray8::BLACK.to_byte(),
                        "({x},{y}) lit behind the bezel"
                    );
                }
            }
        }
    }

    /// The two paths into the same bytes must agree about the clip, which is
    /// the seam this type exists to close.
    #[test]
    fn fill_rect_and_draw_iter_paint_the_same_pixels() {
        let n = (W * H) as usize;
        let (mut a, mut b) = (vec![0u8; n], vec![0u8; n]);
        {
            let mut s = surface(&mut a);
            s.clear(Gray8::BLACK);
            s.fill_rect(4, 100, 232, 20, Gray8::WHITE);
        }
        {
            let mut s = surface(&mut b);
            s.clear(Gray8::BLACK);
            let px = (100..120).flat_map(|y| (4..236).map(move |x| Pixel(Point::new(x, y), Gray8::WHITE)));
            let _ = s.draw_iter(px);
        }
        assert_eq!(a, b);
    }

    #[test]
    fn fill_solid_goes_through_the_row_fill() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = surface(&mut buf);
            s.clear(Gray8::BLACK);
            let _ = s.fill_solid(
                &Rectangle::new(Point::new(100, 100), Size::new(40, 10)),
                Gray8::WHITE,
            );
        }
        assert_eq!(buf[(100 * 240 + 100) as usize], Gray8::WHITE.to_byte());
        assert_eq!(buf[(109 * 240 + 139) as usize], Gray8::WHITE.to_byte());
        assert_eq!(buf[(110 * 240 + 100) as usize], Gray8::BLACK.to_byte());
    }

    /// `bytes_mut` is documented as bypassing the clip, and §7 of the design
    /// record calls it the one escape hatch. A test that says so is what stops
    /// the documentation drifting into a promise it does not make: a caller
    /// writing through it *can* light the bezel, and must apply
    /// [`geometry::is_lit`] itself.
    #[test]
    fn bytes_mut_bypasses_the_clip() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = surface(&mut buf);
            s.clear(Gray8::BLACK);
            // The top-left corner is bezel: fill_rect refuses it.
            s.fill_rect(0, 0, 1, 1, Gray8::WHITE);
            assert_eq!(s.get(0, 0), Some(Gray8::BLACK.to_byte()), "fill_rect lit the bezel");
            // The same pixel through the raw bytes is not refused.
            s.bytes_mut()[0] = Gray8::WHITE.to_byte();
            assert_eq!(s.get(0, 0), Some(Gray8::WHITE.to_byte()));
        }
        assert_eq!(buf[0], Gray8::WHITE.to_byte());
    }

    /// `bytes` is the whole buffer including the bezel, because that is what
    /// goes to the display.
    #[test]
    fn bytes_is_the_whole_buffer_and_the_size_is_the_geometry() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n + 8];
        let s = surface(&mut buf);
        assert_eq!(s.bytes().len(), n);
        assert_eq!(s.width(), W as i32);
        assert_eq!(s.height(), H as i32);
        assert_eq!(s.size(), Size::new(W, H));
        assert_eq!(s.bounding_box(), Rectangle::new(Point::zero(), Size::new(W, H)));
    }

    /// `get` answers about the buffer, not the glass: a bezel pixel is `Some`,
    /// and only being outside the buffer is `None`.
    #[test]
    fn get_is_none_only_outside_the_buffer() {
        let mut buf = vec![0u8; (W * H) as usize];
        let s = surface(&mut buf);
        assert!(s.get(0, 0).is_some(), "a bezel pixel is still in the buffer");
        assert!(s.get(W as i32 - 1, H as i32 - 1).is_some());
        for (x, y) in [(-1, 0), (0, -1), (W as i32, 0), (0, H as i32)] {
            assert_eq!(s.get(x, y), None, "({x},{y})");
        }
    }

    /// `is_lit` follows the clip the surface was built with, which is the only
    /// difference between the two constructors.
    #[test]
    fn is_lit_follows_the_constructor() {
        let mut buf = vec![0u8; (W * H) as usize];
        {
            let round = Surface::<Gray8>::round(&mut buf, W, H).unwrap();
            assert!(!round.is_lit(0, 0));
            assert!(round.is_lit(120, 120));
            assert!(!round.is_lit(-1, 0));
        }
        let rect = Surface::<Gray8>::rect(&mut buf, W, H).unwrap();
        assert!(rect.is_lit(0, 0));
        assert!(!rect.is_lit(-1, 0));
        assert!(!rect.is_lit(W as i32, 0));
    }

    #[test]
    fn a_rect_surface_has_no_bezel() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = Surface::<Gray8>::rect(&mut buf, W, H).unwrap();
            s.fill_rect(0, 0, W as i32, H as i32, Gray8::WHITE);
        }
        assert_eq!(buf[0], Gray8::WHITE.to_byte());
    }
}
