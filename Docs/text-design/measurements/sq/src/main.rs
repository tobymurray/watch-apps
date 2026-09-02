//! Status-quo renderer, lifted from Spin's lib.rs: render from a u8g2 1bpp face one
//! size class up, area-average to dst_h, quantise coverage to 0..3. Writes a PGM of
//! the string with levels 0/85/170/255 on black, and prints width / cap-height metrics.
//!   sq <face> <dst_h> <text> <out.pgm>
use embedded_graphics::{pixelcolor::raw::RawU8, pixelcolor::PixelColor, prelude::*};
use u8g2_fonts::{fonts, types::{FontColor, HorizontalAlignment, VerticalPosition}, FontRenderer};

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
struct C(u8);
impl PixelColor for C { type Raw = RawU8; }

const SS_MAX_W: usize = 1024;
const SS_MAX_H: usize = 80;
static mut SS_BUF: [u8; SS_MAX_W * SS_MAX_H] = [0; SS_MAX_W * SS_MAX_H];

struct SuperSample { w: i32, h: i32 }
impl OriginDimensions for SuperSample { fn size(&self) -> Size { Size::new(self.w as u32, self.h as u32) } }
impl DrawTarget for SuperSample {
    type Color = C; type Error = core::convert::Infallible;
    fn draw_iter<I: IntoIterator<Item = Pixel<C>>>(&mut self, pixels: I) -> Result<(), Self::Error> {
        for Pixel(p, _) in pixels {
            if p.x >= 0 && p.y >= 0 && p.x < self.w && p.y < self.h {
                unsafe { SS_BUF[p.y as usize * SS_MAX_W + p.x as usize] = 1 };
            }
        }
        Ok(())
    }
}

fn face(name: &str) -> FontRenderer {
    match name {
        "helvB24_tr" => FontRenderer::new::<fonts::u8g2_font_helvB24_tr>(),
        "helvR24_tr" => FontRenderer::new::<fonts::u8g2_font_helvR24_tr>(),
        "fub49_tn" => FontRenderer::new::<fonts::u8g2_font_fub49_tn>(),
        "helvR18_tr" => FontRenderer::new::<fonts::u8g2_font_helvR18_tr>(),
        "helvB18_tr" => FontRenderer::new::<fonts::u8g2_font_helvB18_tr>(),
        _ => panic!("unknown face"),
    }
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let font = face(&a[1]);
    let dst_h: i32 = a[2].parse().unwrap();
    let s = &a[3];
    let out = &a[4];
    let raw = a.get(5).map(|x| x == "raw").unwrap_or(false);

    let src_h = font.get_ascent() as i32 - font.get_descent() as i32;
    let bbox = font.get_rendered_dimensions(s.as_str(), Point::zero(), VerticalPosition::Top).ok().and_then(|d| d.bounding_box);
    let left = bbox.map(|b| b.top_left.x).unwrap_or(0);
    let measured_w = bbox.map(|b| b.size.width as i32).unwrap_or(0);
    let dst_w = measured_w * dst_h / src_h; // Face::width
    let src_w = (measured_w + 2).min(SS_MAX_W as i32);
    let src_hh = (src_h + 2).min(SS_MAX_H as i32);
    unsafe { SS_BUF.iter_mut().for_each(|b| *b = 0) };
    let mut t = SuperSample { w: src_w, h: src_hh };
    let _ = font.render_aligned(s.as_str(), Point::new(1 - left, 0), VerticalPosition::Top, HorizontalAlignment::Left, FontColor::Transparent(C(3)), &mut t);

    let (w, h, img): (i32, i32, Vec<u8>) = if raw {
        let mut img = vec![0u8; (src_w * src_hh) as usize];
        for y in 0..src_hh { for x in 0..src_w { img[(y * src_w + x) as usize] = if unsafe { SS_BUF[y as usize * SS_MAX_W + x as usize] } != 0 { 255 } else { 0 }; } }
        (src_w, src_hh, img)
    } else {
        let mut img = vec![0u8; (dst_w * dst_h) as usize];
        for dy in 0..dst_h {
            let sy0 = dy * src_hh / dst_h;
            let sy1 = ((dy + 1) * src_hh / dst_h).max(sy0 + 1).min(src_hh);
            for dx in 0..dst_w {
                let sx0 = dx * src_w / dst_w;
                let sx1 = ((dx + 1) * src_w / dst_w).max(sx0 + 1).min(src_w);
                let (mut lit, mut total) = (0i32, 0i32);
                for sy in sy0..sy1 { for sx in sx0..sx1 { total += 1; if unsafe { SS_BUF[sy as usize * SS_MAX_W + sx as usize] } != 0 { lit += 1; } } }
                let level = if total == 0 { 0 } else { (lit * 3 + total / 2) / total };
                img[(dy * dst_w + dx) as usize] = (level * 85) as u8;
            }
        }
        (dst_w, dst_h, img)
    };
    // cap height: rows with any ink, for a capital-only string, in the *shrunk* image
    let rows_inked: Vec<i32> = (0..h).filter(|&y| (0..w).any(|x| img[(y * w + x) as usize] != 0)).collect();
    let ink_h = rows_inked.last().map(|l| l - rows_inked[0] + 1).unwrap_or(0);
    println!("face={} src_h={} dst_h={} measured_w={} dst_w={} ink_rows={} first_ink_row={:?}", a[1], src_h, dst_h, measured_w, dst_w, ink_h, rows_inked.first());
    let mut f = std::fs::File::create(out).unwrap();
    use std::io::Write;
    write!(f, "P5\n{} {}\n255\n", w, h).unwrap();
    f.write_all(&img).unwrap();
}
