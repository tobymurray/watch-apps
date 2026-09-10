//! Does the forty-line wrapper reach the panels `ByteColor` excludes?
//!
//! A round GC9A01 module is `Rgb565` (see `gc9a01-rs`), which
//! `inscribed_disc::Surface` refuses to compile against.

use discwrapper::disc::DiscClipped;
use embedded_graphics::{
    pixelcolor::Rgb565,
    prelude::*,
    primitives::{PrimitiveStyle, Rectangle},
};
use embedded_graphics_framebuf::FrameBuf;

#[test]
fn a_round_rgb565_panel_is_disc_clipped_by_the_wrapper() {
    let mut data = [Rgb565::BLACK; 240 * 240];
    let mut fb = FrameBuf::new(&mut data, 240, 240);
    {
        let mut d = DiscClipped::new(&mut fb);
        // A 4px inset: inside the buffer, outside the glass at the corners.
        let _ = Rectangle::new(Point::new(4, 4), Size::new(232, 232))
            .into_styled(PrimitiveStyle::with_fill(Rgb565::WHITE))
            .draw(&mut d);
    }
    let lit = |x: i32, y: i32| DiscClipped::<FrameBuf<Rgb565, &mut [Rgb565; 57600]>>::lit(240, 240, Point::new(x, y));
    let mut bezel_painted = 0;
    let mut glass_painted = 0;
    for y in 0..240 {
        for x in 0..240 {
            let on = data[(y * 240 + x) as usize] == Rgb565::WHITE;
            if lit(x, y) { glass_painted += on as u32 } else { bezel_painted += on as u32 }
        }
    }
    assert_eq!(bezel_painted, 0, "painted behind the bezel on an Rgb565 panel");
    assert!(glass_painted > 40_000, "only {glass_painted} lit pixels painted");
    println!("Rgb565 240x240 round: {glass_painted} glass pixels painted, {bezel_painted} behind the bezel");
}
