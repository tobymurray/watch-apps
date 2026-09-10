//! A disc clip over somebody else's `DrawTarget`.
//!
//! [`Surface`](crate::surface::Surface) is bounded on
//! [`ByteColor`](crate::surface::ByteColor), so it serves byte-per-pixel panels
//! only. This wrapper has no colour bound at all: it clips whatever the parent
//! draws, so an `Rgb565` or `BinaryColor` round panel gets the same clip.

use embedded_graphics::{prelude::*, primitives::Rectangle};

use crate::geometry;

/// Clips every draw to the inscribed disc of the parent's bounding box.
///
/// The clip is [`geometry::is_lit`], the same predicate
/// [`Surface`](crate::surface::Surface) uses, so the two paint identical
/// pixels; `the_wrapper_paints_exactly_what_the_surface_paints` holds them
/// together.
///
/// MEASURED, linked `.text` on `thumbv8m.main-none-eabihf` at
/// `opt-level = "z"`: 444 bytes for a plain rect-clipped target, 704 for this
/// wrapper over it, 772 for `Surface::round`. Frame time, median of nine
/// interleaved trials on an aarch64 host: 1.18–1.19× `Surface::round`, against
/// 7.4–8.6× for the same clip with `fill_solid` left at the default. Re-run
/// `InscribedDisc/Docs/measurements/disc-wrapper`.
///
/// Two things to know before using it. The panel is taken to start at the
/// parent's origin, so a parent whose `bounding_box()` does not begin at
/// `(0, 0)` is clipped about the wrong centre. And `clear` arrives through
/// `fill_solid`, so it paints the glass and leaves the bezel holding whatever
/// was there — clear the parent directly when the whole buffer goes to the
/// display.
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
        // The clip is pulled into locals before `self.parent` is touched, which
        // is what makes the borrow check pass.
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
        pixelcolor::{BinaryColor, Rgb565},
        primitives::{Circle, Line, PrimitiveStyle, Triangle},
    };
    use embedded_graphics_framebuf::FrameBuf;

    use super::*;
    use crate::{color::Abgr2222, surface::Surface};

    const W: u32 = 240;
    const H: u32 = 240;

    /// A plain rectangular target, standing in for one an adopter already has.
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
        type Color = Abgr2222;
        type Error = core::convert::Infallible;

        fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(
            &mut self,
            pixels: I,
        ) -> Result<(), Self::Error> {
            for Pixel(p, c) in pixels {
                if p.x >= 0 && p.y >= 0 && p.x < self.w && p.y < self.h {
                    self.buf[(p.y * self.w + p.x) as usize] = c.0;
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
                self.buf[a..z].fill(c.0);
            }
            Ok(())
        }
    }

    /// A scene that straddles the rim on every row: a full-width band, a ring,
    /// a diagonal past both corners, a triangle to the bottom edge, and 240
    /// single pixels.
    fn scene<D: DrawTarget<Color = Abgr2222>>(d: &mut D) {
        let _ = Rectangle::new(Point::new(0, 100), Size::new(240, 40))
            .into_styled(PrimitiveStyle::with_fill(Abgr2222::WHITE))
            .draw(d);
        let _ = Rectangle::new(Point::new(0, 0), Size::new(240, 20))
            .into_styled(PrimitiveStyle::with_fill(Abgr2222::GREY))
            .draw(d);
        let _ = Circle::new(Point::new(20, 20), 200)
            .into_styled(PrimitiveStyle::with_stroke(Abgr2222::RED, 3))
            .draw(d);
        let _ = Line::new(Point::new(-20, -20), Point::new(260, 260))
            .into_styled(PrimitiveStyle::with_stroke(Abgr2222::CYAN, 1))
            .draw(d);
        let _ = Triangle::new(Point::new(0, 239), Point::new(239, 239), Point::new(120, 60))
            .into_styled(PrimitiveStyle::with_fill(Abgr2222::AMBER))
            .draw(d);
        for y in 0..H as i32 {
            let _ = d.draw_iter([Pixel(Point::new(y, y), Abgr2222::YELLOW)]);
        }
    }

    /// The guard that keeps the two clips one clip: both go through `geometry`,
    /// and this is what says so in pixels rather than in prose.
    #[test]
    fn the_wrapper_paints_exactly_what_the_surface_paints() {
        let mut via_surface = vec![0u8; (W * H) as usize];
        {
            let mut s = Surface::<Abgr2222>::round(&mut via_surface, W, H).unwrap();
            s.clear(Abgr2222::BLACK);
            scene(&mut s);
        }

        let mut via_wrapper = vec![0u8; (W * H) as usize];
        {
            let mut p = Plain { buf: &mut via_wrapper, w: W as i32, h: H as i32 };
            // The parent is cleared directly: the whole buffer goes to the
            // display, so the bezel holds the ground colour too.
            p.buf.fill(Abgr2222::BLACK.0);
            scene(&mut DiscClipped::new(&mut p));
        }

        let differing = via_surface.iter().zip(&via_wrapper).filter(|(a, b)| a != b).count();
        assert_eq!(differing, 0, "{differing} of {} bytes differ", via_surface.len());
    }

    /// A round GC9A01 module is `Rgb565`, which `Surface` refuses to compile
    /// against.
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

    /// `sharp-memory-display` drives the round LS010B7DH04 as `BinaryColor`,
    /// which is not a byte a pixel either.
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
