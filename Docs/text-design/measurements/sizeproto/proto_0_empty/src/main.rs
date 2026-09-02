#![no_std]
#![no_main]
static mut FB: [u8; 240 * 240] = [0; 240 * 240];
#[panic_handler] fn p(_: &core::panic::PanicInfo) -> ! { loop {} }
#[no_mangle] pub extern "C" fn _start() -> ! {
    let fb = unsafe { &mut *core::ptr::addr_of_mut!(FB) };
    let s = unsafe { core::ptr::read_volatile(&TEXT) };
    for (i, b) in s.as_bytes().iter().enumerate() { fb[i] = *b; }
    unsafe { core::ptr::write_volatile(fb.as_mut_ptr(), 1) };
    loop {}
}
static TEXT: &str = "GYMWORLD12345678 José 12:34";
