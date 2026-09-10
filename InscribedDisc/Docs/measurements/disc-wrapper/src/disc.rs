//! A disc-clipped `DrawTarget` wrapper, written to answer: could a competent
//! embedded Rust developer replace InscribedDisc's `Surface` in forty lines?

use embedded_graphics::{prelude::*, primitives::Rectangle};

pub struct DiscClipped<'a, T> {
    parent: &'a mut T,
    w: i32,
    h: i32,
}

fn isqrt(v: i32) -> i32 {
    if v <= 0 { return 0; }
    let (mut x, mut y) = (v, (v + 1) / 2);
    while y < x { x = y; y = (x + v / x) / 2; }
    x
}

impl<'a, T: DrawTarget> DiscClipped<'a, T> {
    pub fn new(parent: &'a mut T) -> Self {
        let s = parent.bounding_box().size;
        Self { parent, w: s.width as i32, h: s.height as i32 }
    }
    pub fn lit(w: i32, h: i32, p: Point) -> bool {
        if p.x < 0 || p.y < 0 || p.x >= w || p.y >= h { return false; }
        let (dx, dy, d) = (2 * p.x - (w - 1), 2 * p.y - (h - 1), w.min(h) - 1);
        dx * dx + dy * dy <= d * d
    }
    /// Leftmost lit column of `row`, or `w` if the row is all bezel.
    fn start(w: i32, h: i32, row: i32) -> i32 {
        let (dy, d) = (2 * row - (h - 1), w.min(h) - 1);
        let r2 = d * d - dy * dy;
        if r2 < 0 { return w; }
        let num = (w - 1) - isqrt(r2);
        let x = if num >= 0 { (num + 1) / 2 } else { 0 };
        if x >= w || !Self::lit(w, h, Point::new(x, row)) { w } else { x }
    }
}

impl<T: DrawTarget> Dimensions for DiscClipped<'_, T> {
    fn bounding_box(&self) -> Rectangle { self.parent.bounding_box() }
}

impl<T: DrawTarget> DrawTarget for DiscClipped<'_, T> {
    type Color = T::Color;
    type Error = T::Error;

    fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(&mut self, pixels: I) -> Result<(), Self::Error> {
        let (w, h) = (self.w, self.h);
        self.parent.draw_iter(pixels.into_iter().filter(|Pixel(p, _)| Self::lit(w, h, *p)))
    }

    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        let (w, h) = (self.w, self.h);
        let Some(br) = area.bottom_right() else { return Ok(()) };
        let (x0, x1) = (area.top_left.x.max(0), (br.x + 1).min(w));
        for row in area.top_left.y.max(0)..(br.y + 1).min(h) {
            // Both ends lit means the whole span is: cheaper than the isqrt,
            // and most rows of most fills take it.
            let (a, z) = if Self::lit(w, h, Point::new(x0, row)) && Self::lit(w, h, Point::new(x1 - 1, row)) {
                (x0, x1)
            } else {
                let s = Self::start(w, h, row);
                if s >= w { continue; }
                (x0.max(s), x1.min(w - s))
            };
            if a < z {
                self.parent.fill_solid(
                    &Rectangle::new(Point::new(a, row), Size::new((z - a) as u32, 1)), color)?;
            }
        }
        Ok(())
    }
}

/// The same clip with no `fill_solid` override: correct, and the "is a naive
/// wrapper fast enough anyway" half of the question.
pub struct DiscNaive<'a, T> { parent: &'a mut T, w: i32, h: i32 }

impl<'a, T: DrawTarget> DiscNaive<'a, T> {
    pub fn new(parent: &'a mut T) -> Self {
        let s = parent.bounding_box().size;
        Self { parent, w: s.width as i32, h: s.height as i32 }
    }
}
impl<T: DrawTarget> Dimensions for DiscNaive<'_, T> {
    fn bounding_box(&self) -> Rectangle { self.parent.bounding_box() }
}
impl<T: DrawTarget> DrawTarget for DiscNaive<'_, T> {
    type Color = T::Color;
    type Error = T::Error;
    fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(&mut self, pixels: I) -> Result<(), Self::Error> {
        let (w, h) = (self.w, self.h);
        self.parent.draw_iter(pixels.into_iter().filter(|Pixel(p, _)| DiscClipped::<T>::lit(w, h, *p)))
    }
}
