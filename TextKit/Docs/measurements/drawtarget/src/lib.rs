//! What an app's archive gains from the crate, on the real target.
#![no_std]

use inscribed_disc::{Abgr2222, Surface};
use textkit::{faces, Face, Style};

#[panic_handler]
fn p(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn probe(buf: *mut u8, len: usize, text: *const u8, tlen: usize) -> i32 {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    let t = unsafe { core::str::from_utf8_unchecked(core::slice::from_raw_parts(text, tlen)) };
    let Some(mut c) = Surface::<Abgr2222>::round(s, 240, 240) else {
        return 0;
    };
    let f: &Face = &faces::SEMIBOLD_18_ASCII;
    let white = Style::centered(Abgr2222::WHITE, Abgr2222::BLACK);
    let _ = f.draw(&mut c, t, 120, 100, white);
    let _ = f.draw_top(&mut c, t, 10, 140, Style::left(Abgr2222::WHITE, Abgr2222::BLACK));
    let mut lines = [""; 4];
    let n = f.wrap(t, 200, &mut lines);
    let picked = textkit::pick(&[&faces::REGULAR_12_ASCII, &faces::SEMIBOLD_18_ASCII], t, 100);
    n as i32 + f.measure(t).advance + picked.map_or(0, |p| p.px as i32)
}
