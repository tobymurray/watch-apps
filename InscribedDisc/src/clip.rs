//! A disc clip over somebody else's `DrawTarget`, at any colour depth.

use embedded_graphics::{prelude::*, primitives::Rectangle};

use crate::geometry;

/// Clips every draw to the inscribed disc of the parent's bounding box.
///
/// The clip is [`geometry::is_lit`], so it paints exactly what
/// [`Surface`](crate::surface::Surface) paints.
///
/// MEASURED, linked `.text` on `thumbv8m.main-none-eabihf` at
/// `opt-level = "z"`: 444 bytes for a plain rect-clipped target, 704 for this
/// wrapper over it, 772 for `Surface::round`. Frame time, median of nine
/// interleaved trials on an aarch64 host: 1.18–1.19× `Surface::round`, against
/// 7.4–8.6× for the same clip with `fill_solid` left at the default. Re-run
/// `InscribedDisc/Docs/measurements/disc-wrapper`.
///
/// The panel is taken to start at the parent's origin, so a parent whose
/// `bounding_box()` does not begin at `(0, 0)` is clipped about the wrong
/// centre.
///
/// `clear` paints the glass and leaves the bezel holding whatever was there;
/// clear the parent directly when the whole buffer goes to the display.
pub struct DiscClipped<'a, T> {
    parent: &'a mut T,
    w: i32,
    h: i32,
}

impl<'a, T: DrawTarget> DiscClipped<'a, T> {
    /// Wraps `parent`, taking the panel size from its bounding box.
    pub fn new(parent: &'a mut T) -> Self {
        let size = parent.bounding_box().size;
        DiscClipped { parent, w: size.width as i32, h: size.height as i32 }
    }
}

impl<T: DrawTarget> Dimensions for DiscClipped<'_, T> {
    fn bounding_box(&self) -> Rectangle {
        self.parent.bounding_box()
    }
}

impl<T: DrawTarget> DrawTarget for DiscClipped<'_, T> {
    type Color = T::Color;
    type Error = T::Error;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        // Copied out so the filter does not borrow `self` alongside `self.parent`.
        let (w, h) = (self.w, self.h);
        self.parent
            .draw_iter(pixels.into_iter().filter(|Pixel(p, _)| geometry::is_lit(p.x, p.y, w, h)))
    }

    /// One `fill_solid` a row rather than a pixel at a time: MEASURED at
    /// 1.18–1.19× `Surface::round` against 7.4–8.6× without it. Re-run
    /// `InscribedDisc/Docs/measurements/disc-wrapper`'s `bench` example.
    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        let (w, h) = (self.w, self.h);
        let Some(bottom_right) = area.bottom_right() else {
            return Ok(());
        };
        let x0 = area.top_left.x.max(0);
        let x1 = (bottom_right.x + 1).min(w);
        for row in area.top_left.y.max(0)..(bottom_right.y + 1).min(h) {
            if let Some((a, z)) = geometry::lit_span(x0, x1, row, w, h) {
                let span = Rectangle::new(Point::new(a, row), Size::new((z - a) as u32, 1));
                self.parent.fill_solid(&span, color)?;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use embedded_graphics::{
        pixelcolor::{BinaryColor, Gray8, Rgb565},
        primitives::{Circle, Line, PrimitiveStyle, Triangle},
    };
    use embedded_graphics_framebuf::FrameBuf;

    use super::*;
    use crate::surface::Surface;

    const W: u32 = 240;
    const H: u32 = 240;

    // `Gray8` keeps these tests off the `abgr2222` feature.
    const GROUND: Gray8 = Gray8::new(0x00);
    const BAND: Gray8 = Gray8::new(0xFF);
    const TOP: Gray8 = Gray8::new(0xAA);
    const RING: Gray8 = Gray8::new(0x80);
    const DIAGONAL: Gray8 = Gray8::new(0x55);
    const WEDGE: Gray8 = Gray8::new(0x30);
    const DOT: Gray8 = Gray8::new(0x11);

    /// A plain rectangular target.
    struct Plain<'a> {
        buf: &'a mut [u8],
        w: i32,
        h: i32,
    }

    impl OriginDimensions for Plain<'_> {
        fn size(&self) -> Size {
            Size::new(self.w as u32, self.h as u32)
        }
    }

    impl DrawTarget for Plain<'_> {
        type Color = Gray8;
        type Error = core::convert::Infallible;

        fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(
            &mut self,
            pixels: I,
        ) -> Result<(), Self::Error> {
            for Pixel(p, c) in pixels {
                if p.x >= 0 && p.y >= 0 && p.x < self.w && p.y < self.h {
                    self.buf[(p.y * self.w + p.x) as usize] = c.luma();
                }
            }
            Ok(())
        }

        fn fill_solid(&mut self, area: &Rectangle, c: Self::Color) -> Result<(), Self::Error> {
            let Some(bottom_right) = area.bottom_right() else {
                return Ok(());
            };
            let (x0, x1) = (area.top_left.x.max(0), (bottom_right.x + 1).min(self.w));
            if x0 >= x1 {
                return Ok(());
            }
            for row in area.top_left.y.max(0)..(bottom_right.y + 1).min(self.h) {
                let a = (row * self.w + x0) as usize;
                let z = (row * self.w + x1) as usize;
                self.buf[a..z].fill(c.luma());
            }
            Ok(())
        }
    }

    /// A scene that straddles the rim on every row: a full-width band, a ring,
    /// a diagonal past both corners, a triangle to the bottom edge, and 240
    /// single pixels.
    fn scene<D: DrawTarget<Color = Gray8>>(d: &mut D) {
        let _ = Rectangle::new(Point::new(0, 100), Size::new(240, 40))
            .into_styled(PrimitiveStyle::with_fill(BAND))
            .draw(d);
        let _ = Rectangle::new(Point::new(0, 0), Size::new(240, 20))
            .into_styled(PrimitiveStyle::with_fill(TOP))
            .draw(d);
        let _ = Circle::new(Point::new(20, 20), 200)
            .into_styled(PrimitiveStyle::with_stroke(RING, 3))
            .draw(d);
        let _ = Line::new(Point::new(-20, -20), Point::new(260, 260))
            .into_styled(PrimitiveStyle::with_stroke(DIAGONAL, 1))
            .draw(d);
        let _ = Triangle::new(Point::new(0, 239), Point::new(239, 239), Point::new(120, 60))
            .into_styled(PrimitiveStyle::with_fill(WEDGE))
            .draw(d);
        for y in 0..H as i32 {
            let _ = d.draw_iter([Pixel(Point::new(y, y), DOT)]);
        }
    }

    #[test]
    fn the_wrapper_paints_exactly_what_the_surface_paints() {
        let mut via_surface = vec![0u8; (W * H) as usize];
        {
            let mut s = Surface::<Gray8>::round(&mut via_surface, W, H).unwrap();
            s.clear(GROUND);
            scene(&mut s);
        }

        let mut via_wrapper = vec![0u8; (W * H) as usize];
        {
            let mut p = Plain { buf: &mut via_wrapper, w: W as i32, h: H as i32 };
            // Cleared directly, so the bezel holds the ground colour too.
            p.buf.fill(GROUND.luma());
            scene(&mut DiscClipped::new(&mut p));
        }

        let differing = via_surface.iter().zip(&via_wrapper).filter(|(a, b)| a != b).count();
        assert_eq!(differing, 0, "{differing} of {} bytes differ", via_surface.len());
    }

    #[test]
    fn clear_paints_the_glass_and_leaves_the_bezel() {
        const PRIOR: u8 = 0x7F;
        let mut buf = vec![PRIOR; (W * H) as usize];
        {
            let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
            let _ = DiscClipped::new(&mut p).clear(GROUND);
        }
        let (mut bezel_kept, mut glass_cleared) = (0, 0);
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                let byte = buf[(y * W as i32 + x) as usize];
                if geometry::is_lit(x, y, W as i32, H as i32) {
                    glass_cleared += (byte == GROUND.luma()) as u32;
                } else {
                    bezel_kept += (byte == PRIOR) as u32;
                }
            }
        }
        assert_eq!(glass_cleared, 44_808, "the glass was not fully cleared");
        assert_eq!(bezel_kept, 12_792, "clear reached behind the bezel");
    }

    #[test]
    fn a_non_square_panel_clips_to_the_same_disc_as_the_surface() {
        const W2: u32 = 240;
        const H2: u32 = 160;
        let mut via_surface = vec![0u8; (W2 * H2) as usize];
        {
            let mut s = Surface::<Gray8>::round(&mut via_surface, W2, H2).unwrap();
            s.clear(GROUND);
            let _ = Rectangle::new(Point::new(0, 0), Size::new(W2, H2))
                .into_styled(PrimitiveStyle::with_fill(BAND))
                .draw(&mut s);
        }
        let mut via_wrapper = vec![0u8; (W2 * H2) as usize];
        {
            let mut p = Plain { buf: &mut via_wrapper, w: W2 as i32, h: H2 as i32 };
            p.buf.fill(GROUND.luma());
            let _ = Rectangle::new(Point::new(0, 0), Size::new(W2, H2))
                .into_styled(PrimitiveStyle::with_fill(BAND))
                .draw(&mut DiscClipped::new(&mut p));
        }
        assert_eq!(via_surface, via_wrapper);
        assert!(via_wrapper.iter().any(|&b| b == GROUND.luma()), "nothing was clipped");
    }

    #[test]
    fn a_failing_parent_fails_through_the_wrapper() {
        struct Failing;
        impl OriginDimensions for Failing {
            fn size(&self) -> Size {
                Size::new(W, H)
            }
        }
        impl DrawTarget for Failing {
            type Color = Gray8;
            type Error = ();
            fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(
                &mut self,
                pixels: I,
            ) -> Result<(), Self::Error> {
                // Fails only if a pixel survived the clip.
                if pixels.into_iter().count() > 0 { Err(()) } else { Ok(()) }
            }
            fn fill_solid(&mut self, _: &Rectangle, _: Self::Color) -> Result<(), Self::Error> {
                Err(())
            }
        }

        let mut parent = Failing;
        let mut d = DiscClipped::new(&mut parent);
        assert_eq!(d.fill_solid(&Rectangle::new(Point::new(100, 100), Size::new(4, 4)), BAND), Err(()));
        assert_eq!(d.draw_iter([Pixel(Point::new(120, 120), BAND)]), Err(()));
        // A pixel behind the bezel never reaches the parent, so it cannot fail.
        assert_eq!(d.draw_iter([Pixel(Point::new(0, 0), BAND)]), Ok(()));
    }

    #[test]
    fn the_bounding_box_is_the_parents() {
        let mut buf = vec![0u8; (W * H) as usize];
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        let parent = p.bounding_box();
        assert_eq!(DiscClipped::new(&mut p).bounding_box(), parent);
    }

    #[test]
    fn a_round_rgb565_panel_is_disc_clipped() {
        let mut data = [Rgb565::BLACK; 240 * 240];
        let mut fb = FrameBuf::new(&mut data, 240, 240);
        {
            // A 4px inset: inside the buffer, outside the glass at the corners.
            let _ = Rectangle::new(Point::new(4, 4), Size::new(232, 232))
                .into_styled(PrimitiveStyle::with_fill(Rgb565::WHITE))
                .draw(&mut DiscClipped::new(&mut fb));
        }
        let (glass, bezel) = tally(240, |x, y| data[(y * 240 + x) as usize] == Rgb565::WHITE);
        assert_eq!(bezel, 0, "painted behind the bezel on an Rgb565 panel");
        assert!(glass > 40_000, "only {glass} lit pixels painted");
    }

    #[test]
    fn a_round_1bpp_panel_is_disc_clipped() {
        let mut data = [BinaryColor::Off; 128 * 128];
        let mut fb = FrameBuf::new(&mut data, 128, 128);
        {
            let _ = Rectangle::new(Point::new(0, 0), Size::new(128, 128))
                .into_styled(PrimitiveStyle::with_fill(BinaryColor::On))
                .draw(&mut DiscClipped::new(&mut fb));
        }
        let (glass, bezel) = tally(128, |x, y| data[(y * 128 + x) as usize] == BinaryColor::On);
        assert_eq!(bezel, 0);
        assert!(glass > 12_000, "only {glass} lit pixels painted");
    }

    /// Painted pixels on the glass and behind the bezel, on a square panel.
    fn tally(side: i32, painted: impl Fn(i32, i32) -> bool) -> (u32, u32) {
        let (mut glass, mut bezel) = (0, 0);
        for y in 0..side {
            for x in 0..side {
                if !painted(x, y) {
                    continue;
                }
                if geometry::is_lit(x, y, side, side) {
                    glass += 1;
                } else {
                    bezel += 1;
                }
            }
        }
        (glass, bezel)
    }
}
