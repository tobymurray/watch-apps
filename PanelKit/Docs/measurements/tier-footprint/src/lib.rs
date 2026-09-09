//! What an app's archive gains per tier of the kit, on the real target.
#![no_std]

use panelkit::{color::Abgr2222, surface::Surface};

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

#[cfg(feature = "t2")]
#[no_mangle]
pub extern "C" fn tier2(buf: *mut u8, len: usize) {
    use panelkit::widgets::{Marks, Pill};
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    if let Some(mut fb) = Surface::<Abgr2222>::round(s, 240, 240) {
        Marks::dots(198, Abgr2222::WHITE, Abgr2222::GREY).draw(&mut fb, 5, 2);
        Pill { x: 90, y: 110, width: 60, height: 24, on: Abgr2222::GREEN, off: Abgr2222::BLACK, track: Abgr2222::DARK_GREY }
            .draw(&mut fb, true);
    }
}
