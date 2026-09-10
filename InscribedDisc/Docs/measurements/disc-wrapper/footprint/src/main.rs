//! What the disc clip costs, three ways, linked into a real ELF.
#![no_std]
#![no_main]

use embedded_graphics::{prelude::*, primitives::{PrimitiveStyle, Rectangle}};
use inscribed_disc::color::Abgr2222;

pub mod plain;
#[cfg(any(feature = "wrapper", feature = "naive"))]
pub mod disc;

/// The same drawing in every variant, so only the clip differs.
fn work<D: DrawTarget<Color = Abgr2222>>(d: &mut D) {
    let _ = Rectangle::new(Point::new(20, 100), Size::new(200, 40))
        .into_styled(PrimitiveStyle::with_fill(Abgr2222::WHITE))
        .draw(d);
    let _ = d.draw_iter([Pixel(Point::new(core::hint::black_box(7), 7), Abgr2222::RED)]);
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    let mut fb = [0u8; 240 * 240];

    #[cfg(feature = "surface")]
    {
        use inscribed_disc::surface::Surface;
        let mut s = Surface::<Abgr2222>::round(&mut fb, 240, 240).unwrap();
        s.clear(Abgr2222::BLACK);
        work(&mut s);
        core::hint::black_box(s.bytes());
    }
    #[cfg(feature = "plain")]
    {
        fb.fill(0);
        let mut p = plain::Plain { buf: &mut fb, w: 240, h: 240 };
        work(&mut p);
        core::hint::black_box(&p.buf[0]);
    }
    #[cfg(feature = "wrapper")]
    {
        fb.fill(0);
        let mut p = plain::Plain { buf: &mut fb, w: 240, h: 240 };
        {
            let mut d = disc::DiscClipped::new(&mut p);
            work(&mut d);
        }
        core::hint::black_box(&p.buf[0]);
    }
    #[cfg(feature = "promoted")]
    {
        fb.fill(0);
        let mut p = plain::Plain { buf: &mut fb, w: 240, h: 240 };
        {
            let mut d = inscribed_disc::clip::DiscClipped::new(&mut p);
            work(&mut d);
        }
        core::hint::black_box(&p.buf[0]);
    }
    #[cfg(feature = "naive")]
    {
        fb.fill(0);
        let mut p = plain::Plain { buf: &mut fb, w: 240, h: 240 };
        {
            let mut d = disc::DiscNaive::new(&mut p);
            work(&mut d);
        }
        core::hint::black_box(&p.buf[0]);
    }
    loop {}
}

#[panic_handler]
fn p(_: &core::panic::PanicInfo) -> ! { loop {} }
