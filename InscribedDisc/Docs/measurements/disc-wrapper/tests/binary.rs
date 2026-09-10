//! `sharp-memory-display` drives the round LS012B7DD06 and LS010B7DH04 as
//! `BinaryColor`. Neither `Abgr2222` nor `ByteColor` fits that; the wrapper
//! does not care.
use discwrapper::disc::DiscClipped;
use embedded_graphics::{pixelcolor::BinaryColor, prelude::*, primitives::{PrimitiveStyle, Rectangle}};
use embedded_graphics_framebuf::FrameBuf;

#[test]
fn a_round_1bpp_panel_is_disc_clipped_by_the_wrapper() {
    let mut data = [BinaryColor::Off; 128 * 128];
    let mut fb = FrameBuf::new(&mut data, 128, 128);
    {
        let mut d = DiscClipped::new(&mut fb);
        let _ = Rectangle::new(Point::new(0, 0), Size::new(128, 128))
            .into_styled(PrimitiveStyle::with_fill(BinaryColor::On))
            .draw(&mut d);
    }
    let mut bezel = 0u32;
    let mut glass = 0u32;
    for y in 0..128i32 {
        for x in 0..128i32 {
            let on = data[(y * 128 + x) as usize] == BinaryColor::On;
            if inscribed_disc::geometry::is_lit(x, y, 128, 128) { glass += on as u32 } else { bezel += on as u32 }
        }
    }
    assert_eq!(bezel, 0);
    println!("BinaryColor 128x128 round (LS010B7DH04): {glass} lit, {bezel} behind the bezel");
}
