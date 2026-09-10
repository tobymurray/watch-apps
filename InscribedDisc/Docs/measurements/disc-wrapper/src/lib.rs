pub mod disc;

/// A plain rectangular byte-per-pixel target, standing in for "any existing
/// DrawTarget" the wrapper would be composed over.
pub mod plain {
    use embedded_graphics::{prelude::*, primitives::Rectangle};
    use inscribed_disc::color::Abgr2222;

    pub struct Plain<'a> { pub buf: &'a mut [u8], pub w: i32, pub h: i32 }
    impl OriginDimensions for Plain<'_> {
        fn size(&self) -> Size { Size::new(self.w as u32, self.h as u32) }
    }
    impl DrawTarget for Plain<'_> {
        type Color = Abgr2222;
        type Error = core::convert::Infallible;
        fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(&mut self, px: I) -> Result<(), Self::Error> {
            for Pixel(p, c) in px {
                if p.x >= 0 && p.y >= 0 && p.x < self.w && p.y < self.h {
                    self.buf[(p.y * self.w + p.x) as usize] = c.0;
                }
            }
            Ok(())
        }
        fn fill_solid(&mut self, area: &Rectangle, c: Self::Color) -> Result<(), Self::Error> {
            let Some(br) = area.bottom_right() else { return Ok(()) };
            let (x0, x1) = (area.top_left.x.max(0), (br.x + 1).min(self.w));
            if x0 >= x1 { return Ok(()) }
            for row in area.top_left.y.max(0)..(br.y + 1).min(self.h) {
                let a = (row * self.w + x0) as usize;
                let z = (row * self.w + x1) as usize;
                self.buf[a..z].fill(c.0);
            }
            Ok(())
        }
    }
}
