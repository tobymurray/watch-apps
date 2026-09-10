//! What an app's archive gains from the kit, on the real target.
#![no_std]

use inscribed_disc::{color::Abgr2222, surface::Surface};

#[panic_handler]
fn p(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn tier0(buf: *mut u8, len: usize) {
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    if let Some(mut fb) = Surface::<Abgr2222>::round(s, 240, 240) {
        fb.clear(Abgr2222::BLACK);
        fb.fill_rect(20, 100, 200, 40, Abgr2222::WHITE);
    }
}
