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

#[cfg(feature = "t1")]
#[no_mangle]
pub extern "C" fn tier1(buf: *mut u8, len: usize) {
    use panelkit::{dither, draw};
    let s = unsafe { core::slice::from_raw_parts_mut(buf, len) };
    if let Some(mut fb) = Surface::<Abgr2222>::round(s, 240, 240) {
        draw::fill_arc(&mut fb, 119.5, 119.5, 100.0, 118.0, 30.0, 300.0, Abgr2222::WHITE);
        draw::fill_circle(&mut fb, 120, 120, 30, Abgr2222::GREY);
        let mut cov = [0.0f32; 240];
        draw::accumulate_coverage(&mut cov, 1.25, 9.75);
        for x in 0..240 {
            let c = dither::dither_rgb((x * 255 / 239) as u8, 128, 64, x, 10);
            fb.fill_rect(x, 10, 1, 8, c);
        }
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

#[cfg(feature = "t3")]
#[no_mangle]
pub extern "C" fn tier3(button: u8, len: usize, now_ms: u32) -> u32 {
    use panelkit::nav::{Anim, Bindings, Button, Focus, Role, Stack};
    let mut stack: Stack<u8, 4> = Stack::new(0);
    let b = Bindings::default();
    let mut f = Focus::new();
    if let Some(btn) = Button::from_u8(button) {
        match f.apply(&b, btn, len) {
            Role::Select => {
                stack.push(1);
            }
            Role::Back => {
                stack.pop();
            }
            _ => {}
        }
    }
    let a = Anim::starting(0, 400);
    (f.index() as u32) + panelkit::nav::ease_in_out(a.progress(now_ms)) as u32 + stack.depth() as u32
}
