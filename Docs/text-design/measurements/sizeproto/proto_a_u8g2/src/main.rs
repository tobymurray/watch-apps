#![no_std]
#![no_main]
//! Candidate A as shipped: helvB24_tr + helvR24_tr + fub49_tn, supersample-and-shrink into a 384x70 scratch.
use embedded_graphics::{pixelcolor::raw::RawU8, pixelcolor::PixelColor, prelude::*};
use u8g2_fonts::{fonts, types::{FontColor, HorizontalAlignment, VerticalPosition}, FontRenderer};
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)] struct C(u8);
impl PixelColor for C { type Raw = RawU8; }
static mut FB: [u8; 240 * 240] = [0; 240 * 240];
const SS_MAX_W: usize = 384; const SS_MAX_H: usize = 70;
static mut SS_BUF: [u8; SS_MAX_W * SS_MAX_H] = [0; SS_MAX_W * SS_MAX_H];
struct SuperSample { w: i32, h: i32 }
impl OriginDimensions for SuperSample { fn size(&self) -> Size { Size::new(self.w as u32, self.h as u32) } }
impl DrawTarget for SuperSample { type Color = C; type Error = core::convert::Infallible;
    fn draw_iter<I: IntoIterator<Item = Pixel<C>>>(&mut self, pixels: I) -> Result<(), Self::Error> {
        for Pixel(p, _) in pixels { if p.x >= 0 && p.y >= 0 && p.x < self.w && p.y < self.h { unsafe { SS_BUF[p.y as usize * SS_MAX_W + p.x as usize] = 1 }; } } Ok(()) } }
#[panic_handler] fn p(_: &core::panic::PanicInfo) -> ! { loop {} }
fn render_smoothed(fb: &mut [u8], font: &FontRenderer, s: &str, dst_x: i32, dst_y: i32, dst_h: i32) {
    let src_h = font.get_ascent() as i32 - font.get_descent() as i32;
    let bbox = font.get_rendered_dimensions(s, Point::zero(), VerticalPosition::Top).ok().and_then(|d| d.bounding_box);
    let left = bbox.map(|b| b.top_left.x).unwrap_or(0);
    let measured_w = bbox.map(|b| b.size.width as i32).unwrap_or(0);
    let dst_w = measured_w * dst_h / src_h;
    let src_w = (measured_w + 2).clamp(1, SS_MAX_W as i32); let src_hh = (src_h + 2).clamp(1, SS_MAX_H as i32);
    for y in 0..src_hh { let row = y as usize * SS_MAX_W; unsafe { SS_BUF[row..row + src_w as usize].fill(0) }; }
    let mut t = SuperSample { w: src_w, h: src_hh };
    let _ = font.render_aligned(s, Point::new(1 - left, 0), VerticalPosition::Top, HorizontalAlignment::Left, FontColor::Transparent(C(3)), &mut t);
    for dy in 0..dst_h { let sy0 = dy * src_hh / dst_h; let sy1 = ((dy + 1) * src_hh / dst_h).max(sy0 + 1).min(src_hh);
        for dx in 0..dst_w { let sx0 = dx * src_w / dst_w; let sx1 = ((dx + 1) * src_w / dst_w).max(sx0 + 1).min(src_w);
            let (mut lit, mut total) = (0i32, 0i32);
            for sy in sy0..sy1 { for sx in sx0..sx1 { total += 1; if unsafe { SS_BUF[sy as usize * SS_MAX_W + sx as usize] } != 0 { lit += 1; } } }
            if total == 0 { continue; }
            let level = (lit * 3 + total / 2) / total;
            let (x, y) = (dst_x + dx, dst_y + dy);
            if level > 0 && x >= 0 && y >= 0 && x < 240 && y < 240 { fb[(y * 240 + x) as usize] = 0xC0 | (level as u8 * 0x15); } } }
}
#[no_mangle] pub extern "C" fn _start() -> ! {
    let fb = unsafe { &mut *core::ptr::addr_of_mut!(FB) };
    let s = unsafe { core::ptr::read_volatile(&TEXT) };
    let b = FontRenderer::new::<fonts::u8g2_font_helvB24_tr>();
    let r = FontRenderer::new::<fonts::u8g2_font_helvR24_tr>();
    let n = FontRenderer::new::<fonts::u8g2_font_fub49_tn>();
    render_smoothed(fb, &b, s, 20, 20, 24); render_smoothed(fb, &r, s, 20, 60, 18); render_smoothed(fb, &n, s, 20, 100, 54);
    unsafe { core::ptr::write_volatile(fb.as_mut_ptr(), 1) };
    loop {}
}
static TEXT: &str = "GYMWORLD12345678 José 12:34";
