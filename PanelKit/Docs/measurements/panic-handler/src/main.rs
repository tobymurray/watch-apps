//! What does a panic handler that says where it panicked actually cost?
//!
//! Three handlers over the identical workload, linked into a real ELF so the
//! answer includes what each drags out of `core` — which an archive-level
//! measurement misses entirely, because `core::fmt` lives in `core`'s own
//! object and the linker takes it or does not.
//!
//!   `literal`   sends `b"panic"`. What Spin and Barcode shipped.
//!   `location`  sends `file:line`. No message, so no `Display` machinery.
//!   `full`      sends `file:line: message`. What PanelKit has.
#![no_std]
#![no_main]

use panelkit::{color::Abgr2222, surface::Surface};

/// A workload with a real panic reachable from it, so the handler cannot be
/// proved unreachable and deleted.
#[no_mangle]
pub extern "C" fn _start() -> ! {
    let mut fb = [0u8; 240 * 240];
    let mut s = Surface::<Abgr2222>::round(&mut fb, 240, 240).unwrap();
    s.clear(Abgr2222::BLACK);
    s.fill_rect(20, 100, 200, 40, Abgr2222::WHITE);
    // Indexing the framebuffer through a value the compiler cannot bound: a
    // bounds-check panic is the realistic one on this platform.
    let n = s.bytes()[core::hint::black_box(57_599)];
    let extra = s.bytes()[core::hint::black_box(n as usize)];
    core::hint::black_box(extra);
    loop {}
}

#[cfg(feature = "literal")]
#[panic_handler]
fn on_panic(_info: &core::panic::PanicInfo) -> ! {
    let s = b"panic";
    unsafe { panelkit_host_panic(s.as_ptr(), s.len() as u32) }
}

#[cfg(feature = "location")]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    // `file()` is a `&'static str` and `line()` a u32, so this needs an integer
    // formatter but never `Display` for an arbitrary payload.
    let mut buf = [0u8; 192];
    let mut n = 0;
    if let Some(loc) = info.location() {
        let f = loc.file().as_bytes();
        let take = f.len().min(buf.len() - 12);
        buf[..take].copy_from_slice(&f[..take]);
        n = take;
        buf[n] = b':';
        n += 1;
        let mut line = loc.line();
        let mut digits = [0u8; 10];
        let mut d = 0;
        loop {
            digits[d] = b'0' + (line % 10) as u8;
            d += 1;
            line /= 10;
            if line == 0 {
                break;
            }
        }
        for i in 0..d {
            buf[n] = digits[d - 1 - i];
            n += 1;
        }
    }
    unsafe { panelkit_host_panic(buf.as_ptr(), n as u32) }
}

#[cfg(feature = "full")]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    use core::fmt::Write as _;
    let mut msg = panelkit::panic::Buf::<192>::new();
    if let Some(loc) = info.location() {
        let _ = write!(msg, "{}:{}: ", loc.file(), loc.line());
    }
    let _ = write!(msg, "{}", info.message());
    let s = msg.as_str();
    unsafe { panelkit_host_panic(s.as_ptr(), s.len() as u32) }
}

/// Stands in for the C++ shell's trampoline, which logs and exits. Kept as
/// small as possible so it does not itself colour the comparison.
#[no_mangle]
pub extern "C" fn panelkit_host_panic(msg: *const u8, len: u32) -> ! {
    unsafe {
        core::ptr::write_volatile(0x2008_0000 as *mut u32, len);
        core::ptr::write_volatile(0x2008_0004 as *mut u32, msg as u32);
    }
    loop {}
}

/// `location`, with every operation that could itself panic removed: iterator
/// zips instead of indexing, no `copy_from_slice`. If a cheap location handler
/// exists, this is it.
#[cfg(feature = "location_min")]
#[panic_handler]
fn on_panic(info: &core::panic::PanicInfo) -> ! {
    let mut buf = [0u8; 192];
    let mut n = 0usize;
    if let Some(loc) = info.location() {
        for (dst, src) in buf.iter_mut().zip(loc.file().as_bytes()) {
            *dst = *src;
            n += 1;
        }
        let mut digits = [0u8; 10];
        let mut d = 0usize;
        let mut line = loc.line();
        loop {
            for (i, slot) in digits.iter_mut().enumerate() {
                if i == d {
                    *slot = b'0' + (line % 10) as u8;
                }
            }
            d += 1;
            line /= 10;
            if line == 0 || d == 10 {
                break;
            }
        }
        for (dst, src) in buf.iter_mut().skip(n).zip(digits.iter().take(d).rev()) {
            *dst = *src;
            n += 1;
        }
    }
    unsafe { panelkit_host_panic(buf.as_ptr(), n as u32) }
}
