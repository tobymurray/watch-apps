use discwrapper::{disc::DiscClipped, plain::Plain};
use embedded_graphics::{
    prelude::*,
    primitives::{Circle, Line, PrimitiveStyle, Rectangle, Triangle},
};
use inscribed_disc::{color::Abgr2222, surface::Surface};

const W: u32 = 240;
const H: u32 = 240;

/// Draw the same scene through InscribedDisc's Surface and through the wrapper over
/// a plain rectangular target, and compare every byte.
fn scene_via_surface() -> Vec<u8> {
    let mut buf = vec![0u8; (W * H) as usize];
    {
        let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
        s.clear(Abgr2222::BLACK);
        draw(&mut s);
    }
    buf
}

fn scene_via_wrapper() -> Vec<u8> {
    let mut buf = vec![0u8; (W * H) as usize];
    {
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        // clear() is not a DrawTarget method on Surface's terms; the wrapper's
        // parent is cleared directly, as the whole buffer goes to the display.
        p.buf.fill(Abgr2222::BLACK.0);
        let mut d = DiscClipped::new(&mut p);
        draw(&mut d);
    }
    buf
}

fn draw<D: DrawTarget<Color = Abgr2222>>(d: &mut D) {
    // A rectangle that straddles the rim on both sides.
    let _ = Rectangle::new(Point::new(0, 100), Size::new(240, 40))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::WHITE))
        .draw(d);
    // Full-screen fill: every row straddles.
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

#[test]
fn the_wrapper_paints_exactly_what_the_surface_paints() {
    let a = scene_via_surface();
    let b = scene_via_wrapper();
    let diff = a.iter().zip(&b).filter(|(x, y)| x != y).count();
    assert_eq!(diff, 0, "{diff} of {} bytes differ", a.len());
}

#[test]
fn the_wrapper_lights_nothing_behind_the_bezel() {
    let b = scene_via_wrapper();
    for y in 0..H as i32 {
        for x in 0..W as i32 {
            if !inscribed_disc::geometry::is_lit(x, y, W as i32, H as i32) {
                assert_eq!(b[(y * W as i32 + x) as usize], Abgr2222::BLACK.0, "({x},{y})");
            }
        }
    }
}

/// The claim the crate rests on: without the wrapper, an inset box is not inset
/// from the glass.
#[test]
fn an_unclipped_target_paints_behind_the_bezel() {
    let mut buf = vec![0u8; (W * H) as usize];
    {
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        let _ = Rectangle::new(Point::new(4, 4), Size::new(232, 232))
            .into_styled(PrimitiveStyle::with_fill(Abgr2222::WHITE))
            .draw(&mut p);
    }
    let dark = (0..H as i32)
        .flat_map(|y| (0..W as i32).map(move |x| (x, y)))
        .filter(|&(x, y)| !inscribed_disc::geometry::is_lit(x, y, W as i32, H as i32))
        .filter(|&(x, y)| buf[(y * W as i32 + x) as usize] != Abgr2222::BLACK.0)
        .count();
    assert!(dark > 0, "a 4px inset drew nothing behind the bezel");
    println!("a 4px-inset fill on a rect target paints {dark} pixels the glass never shows");
}
