//! The framebuffer, as `embedded-graphics` sees it.
//!
//! One surface owns the clip, so a shape and a glyph drawn into the same bytes
//! cannot disagree about where the glass ends. In the apps this crate was
//! distilled from they could: `FrameBuf` clipped to the rectangle and
//! `TextKit::Canvas`, built over the same buffer, clipped to the disc.

use core::marker::PhantomData;

use embedded_graphics::{prelude::*, primitives::Rectangle};

use crate::geometry;

/// Counts how often the disc clip fell back to scanning a row for the lit
/// interval's edge. Test-only, and the whole point of the fast path is that a
/// span already inside the disc never reaches it.
///
/// Thread-local rather than a global: `cargo test` runs tests in parallel, and
/// a shared counter picks up every other test's fills. It read 1,826 that way
/// against a correct fast path, which is a broken instrument reporting a broken
/// fix.
#[cfg(test)]
thread_local! {
    pub(crate) static EDGE_SCANS: core::cell::Cell<usize> = const { core::cell::Cell::new(0) };
}

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
/// Generic over the colour so the crate is not tied to one panel's encoding.
/// Measured on `thumbv8m.main-none-eabihf`: the genericity costs 66 bytes of
/// `.text` over the same primitives written against one concrete colour type,
/// against 884 for the concrete set, and a *second* colour type in the same
/// binary costs 848 more. An app instantiates one, so it pays the 66 and not
/// the 848. Re-measure with `PanelKit/Docs/measurements/genericity`.
pub struct Surface<'a, C> {
    buf: &'a mut [u8],
    w: i32,
    h: i32,
    clip: Clip,
    _color: PhantomData<C>,
}

/// A colour this crate can write into a byte-per-pixel framebuffer.
///
/// One byte a pixel is the whole point: `RequestDisplayUpdate` hands the kernel
/// a raw buffer, and anything wider would have it read past the end.
pub trait ByteColor: PixelColor + Copy {
    /// The byte this colour occupies in the framebuffer.
    fn to_byte(self) -> u8;
    /// The colour that byte encodes.
    fn from_byte(b: u8) -> Self;
}

impl<'a, C: ByteColor> Surface<'a, C> {
    /// A round panel: only the inscribed disc is glass.
    ///
    /// Returns `None` rather than panicking when the buffer is smaller than the
    /// stated geometry, because on this platform that argument arrives from a
    /// kernel message and a renderer that refuses is better than one that
    /// writes past the end.
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
    /// The one escape hatch, and it exists for exactly one job: a text
    /// implementation that rasterises through its own surface type cannot go
    /// through [`DrawTarget`] a glyph at a time and stay affordable. An adapter
    /// that uses this **must** apply [`crate::geometry::is_lit`] itself, and
    /// must have a test that says so — the two clips disagreeing over the same
    /// buffer is the defect this type was made to close, so reopening it here
    /// without a test puts it straight back.
    pub fn bytes_mut(&mut self) -> &mut [u8] {
        self.buf
    }

    /// Every pixel, including the ones behind the bezel: the buffer is handed
    /// to the kernel whole, so the bezel must hold the ground colour and not
    /// whatever was there last frame.
    pub fn clear(&mut self, color: C) {
        self.buf.fill(color.to_byte());
    }

    #[inline]
    fn put(&mut self, x: i32, y: i32, b: u8) {
        if self.is_lit(x, y) {
            self.buf[(y * self.w + x) as usize] = b;
        }
    }

    /// The byte at a pixel, or `None` behind the bezel. For tests and for the
    /// dither, which reads what it is blending toward.
    pub fn get(&self, x: i32, y: i32) -> Option<u8> {
        if x < 0 || y < 0 || x >= self.w || y >= self.h {
            return None;
        }
        Some(self.buf[(y * self.w + x) as usize])
    }

    /// A rectangle filled a row at a time.
    ///
    /// The reason this exists rather than `Rectangle::into_styled().draw()`:
    /// that pulls in the point-iterator layer once per distinct colour it is
    /// called with, which `Spin` recorded and this crate keeps.
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
                    // A row's lit pixels are an interval: `is_lit` depends on x
                    // only through (2x - (w-1))^2, which is a parabola in x. So
                    // if both ends of the span are lit, all of it is, and the
                    // common case costs two tests rather than a scan for the
                    // interval's edge.
                    //
                    // That matters more than it looks: an arc sampler plots
                    // single pixels through this, so "per row" is "per pixel".
                    // MEASURED on the host over Spin's 43 scenes: scanning for
                    // the edge every row costs 128 us a frame against 43 for
                    // the same output, and 363 us against 77 on the worst
                    // frame. Falsified by
                    // Spin/.../examples/frametime.rs, which is what measured it.
                    let (a, z) = if geometry::is_lit(x0, row, self.w, self.h)
                        && geometry::is_lit(x1 - 1, row, self.w, self.h)
                    {
                        (x0, x1)
                    } else {
                        #[cfg(test)]
                        EDGE_SCANS.with(|c| c.set(c.get() + 1));
                        let start = geometry::lit_start(row, self.w, self.h);
                        let end = self.w - start;
                        (x0.max(start), x1.min(end))
                    };
                    if a < z {
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

    /// Overridden because the default walks `fill_contiguous` a pixel at a
    /// time. MEASURED: supplying it costs 16 bytes of `.text` *less* than
    /// leaving the default in place, because the specialised body replaces the
    /// generic machinery rather than adding to it.
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
    use super::*;
    use crate::color::Abgr2222;

    const W: u32 = 240;
    const H: u32 = 240;

    fn surface(buf: &mut [u8]) -> Surface<'_, Abgr2222> {
        Surface::round(buf, W, H).unwrap()
    }

    #[test]
    fn a_buffer_smaller_than_its_geometry_is_refused() {
        let mut buf = vec![0u8; (W * H) as usize - 1];
        assert!(Surface::<Abgr2222>::round(&mut buf, W, H).is_none());
    }

    #[test]
    fn never_writes_past_the_stated_geometry() {
        let n = (W * H) as usize;
        let mut buf = vec![0xAAu8; n + 64];
        {
            let mut s = surface(&mut buf);
            s.clear(Abgr2222::BLACK);
            s.fill_rect(-50, -50, 1000, 1000, Abgr2222::WHITE);
        }
        assert!(buf[n..].iter().all(|&b| b == 0xAA), "overran the framebuffer");
    }

    #[test]
    fn nothing_is_drawn_outside_the_lit_disc() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = surface(&mut buf);
            s.clear(Abgr2222::BLACK);
            s.fill_rect(0, 0, W as i32, H as i32, Abgr2222::WHITE);
            // Through the generic path too, not only the fast one.
            let _ = s.draw_iter((0..W as i32).map(|x| Pixel(Point::new(x, 0), Abgr2222::WHITE)));
        }
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                if !geometry::is_lit(x, y, W as i32, H as i32) {
                    assert_eq!(
                        buf[(y * W as i32 + x) as usize],
                        Abgr2222::BLACK.to_byte(),
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
            s.clear(Abgr2222::BLACK);
            s.fill_rect(4, 100, 232, 20, Abgr2222::WHITE);
        }
        {
            let mut s = surface(&mut b);
            s.clear(Abgr2222::BLACK);
            let px = (100..120).flat_map(|y| (4..236).map(move |x| Pixel(Point::new(x, y), Abgr2222::WHITE)));
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
            s.clear(Abgr2222::BLACK);
            let _ = s.fill_solid(
                &Rectangle::new(Point::new(100, 100), Size::new(40, 10)),
                Abgr2222::WHITE,
            );
        }
        assert_eq!(buf[(100 * 240 + 100) as usize], Abgr2222::WHITE.to_byte());
        assert_eq!(buf[(109 * 240 + 139) as usize], Abgr2222::WHITE.to_byte());
        assert_eq!(buf[(110 * 240 + 100) as usize], Abgr2222::BLACK.to_byte());
    }

    /// A span already inside the disc must never scan the row for its edge.
    ///
    /// This is the fault, stated as a property rather than as a stopwatch: the
    /// first version of this clip scanned every row from x = 0 to find the lit
    /// interval, and because an arc sampler plots single pixels through
    /// `fill_rect`, "per row" meant "per pixel". MEASURED over Spin's 43
    /// scenes: 43 us a frame became 128, and 77 became 363 on the worst frame,
    /// for byte-identical output.
    ///
    /// A timing test was tried first and discarded: the disc/rect ratio is 1.86
    /// broken against 1.42 fixed in a debug build, and 4.56 against 2.75 in
    /// release, so no single threshold separates them in both profiles. Counting
    /// the fallback is exact and profile-independent.
    #[test]
    fn a_span_inside_the_disc_never_scans_for_the_edge() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];

        // Spin's arc: single pixels between r=100 and r=118, all of them well
        // inside the disc's 119.5.
        EDGE_SCANS.with(|c| c.set(0));
        {
            let mut s = surface(&mut buf);
            let (cx, cy) = (119.5f32, 119.5f32);
            let mut a = 0.0f32;
            while a < core::f32::consts::PI * 2.0 {
                let (sa, ca) = (a.sin(), a.cos());
                let mut r = 100.0f32;
                while r <= 118.0 {
                    s.fill_rect((cx + r * sa) as i32, (cy - r * ca) as i32, 1, 1, Abgr2222::WHITE);
                    r += 0.65;
                }
                a += 0.65 / 118.0;
            }
        }
        let inside = EDGE_SCANS.with(|c| c.get());
        assert_eq!(inside, 0, "an arc wholly inside the disc scanned {inside} row edges");

        // The instrument, proven: a span that really does cross the rim must
        // reach the fallback, or the counter above is measuring nothing.
        EDGE_SCANS.with(|c| c.set(0));
        {
            let mut s = surface(&mut buf);
            s.fill_rect(0, 0, W as i32, H as i32, Abgr2222::WHITE);
        }
        let crossing = EDGE_SCANS.with(|c| c.get());
        assert!(crossing > 0, "a full-panel fill took the fast path on every row");
    }

    #[test]
    fn a_rect_surface_has_no_bezel() {
        let n = (W * H) as usize;
        let mut buf = vec![0u8; n];
        {
            let mut s = Surface::<Abgr2222>::rect(&mut buf, W, H).unwrap();
            s.fill_rect(0, 0, W as i32, H as i32, Abgr2222::WHITE);
        }
        assert_eq!(buf[0], Abgr2222::WHITE.to_byte());
    }
}
