//! How much a disc clip costs a frame, four ways, on the same drawing.
//!
//! Not a device measurement: aarch64 host, so treat the ratios and not the
//! absolute microseconds as the result.
use discwrapper::{disc::{DiscClipped, DiscNaive}, plain::Plain};
use embedded_graphics::{prelude::*, primitives::{Circle, PrimitiveStyle, Rectangle}};
use inscribed_disc::{color::Abgr2222, surface::Surface};
use std::time::Instant;

const W: u32 = 240;
const H: u32 = 240;

/// A frame shaped like a real one: a full-panel ground, three bands that
/// straddle the rim, a ring, and a scatter of single pixels.
fn frame<D: DrawTarget<Color = Abgr2222>>(d: &mut D) {
    let _ = Rectangle::new(Point::new(0, 0), Size::new(W, H))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::BLACK)).draw(d);
    for (y, c) in [(30, Abgr2222::GREY), (100, Abgr2222::WHITE), (196, Abgr2222::DARK_GREY)] {
        let _ = Rectangle::new(Point::new(0, y), Size::new(W, 36))
            .into_styled(PrimitiveStyle::with_fill(c)).draw(d);
    }
    let _ = Circle::new(Point::new(6, 6), 228)
        .into_styled(PrimitiveStyle::with_stroke(Abgr2222::AMBER, 6)).draw(d);
    for i in 0..600i32 {
        let _ = d.draw_iter([Pixel(Point::new((i * 7) % 240, (i * 13) % 240), Abgr2222::CYAN)]);
    }
}

/// Nine interleaved trials, reporting the median: a single pass of this bench
/// moved Surface::round from 22.4 to 32.5 us between runs.
fn time(name: &str, reps: u32, mut f: impl FnMut()) -> f64 {
    let mut us = Vec::new();
    for _ in 0..9 {
        f();
        let t = Instant::now();
        for _ in 0..reps { f(); }
        us.push(t.elapsed().as_secs_f64() * 1e6 / reps as f64);
    }
    us.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let med = us[4];
    println!("{name:<34} median {med:>7.1}  min {:>7.1}  max {:>7.1} us/frame", us[0], us[8]);
    med
}

fn main() {
    let reps = 500;
    let mut buf = vec![0u8; (W * H) as usize];

    let floor = time("Plain, rect clip (floor)", reps, || {
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        frame(&mut p);
    });
    let surf = time("inscribed-disc Surface::round", reps, || {
        let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
        frame(&mut s);
    });
    let promoted = time("inscribed_disc::DiscClipped", reps, || {
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        let mut d = inscribed_disc::clip::DiscClipped::new(&mut p);
        frame(&mut d);
    });
    let wrap = time("DiscClipped (row fast path)", reps, || {
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        let mut d = DiscClipped::new(&mut p);
        frame(&mut d);
    });
    let naive = time("DiscNaive (filter only)", reps, || {
        let mut p = Plain { buf: &mut buf, w: W as i32, h: H as i32 };
        let mut d = DiscNaive::new(&mut p);
        frame(&mut d);
    });
    let srect = time("Surface::rect (no disc clip)", reps, || {
        let mut s = Surface::<Abgr2222>::rect(&mut buf, W, H).unwrap();
        frame(&mut s);
    });

    println!();
    println!("vs Surface::round:  promoted {:.2}x  wrapper {:.2}x  naive {:.2}x",
             promoted / surf, wrap / surf, naive / surf);
    println!("vs rect clip floor: Surface {:.2}x  wrapper {:.2}x  naive {:.2}x  Surface::rect {:.2}x",
             surf / floor, wrap / floor, naive / floor, srect / floor);
}
