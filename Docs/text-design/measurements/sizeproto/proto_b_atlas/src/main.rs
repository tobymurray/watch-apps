#![no_std]
#![no_main]
//! Candidate B: pre-rendered 2bpp atlases, blitted. Regular 18 tier 1, SemiBold 20 tier 0, SemiBold 54 digits.
mod atlas;
#[repr(C)] pub struct Node { pub off: u32, pub cp: u16, pub w: u8, pub h: u8, pub top: i8, pub left: i8, pub adv: u8, pub _pad: u8 }
pub struct Face { pub px: u8, pub ascent: u8, pub descent: u8, pub nodes: &'static [Node], pub data: &'static [u8] }
static mut FB: [u8; 240 * 240] = [0; 240 * 240];
#[panic_handler] fn p(_: &core::panic::PanicInfo) -> ! { loop {} }
impl Face {
    fn find(&self, c: char) -> Option<&Node> { let cp = c as u32; if cp > 0xFFFF { return None; } self.nodes.binary_search_by_key(&(cp as u16), |n| n.cp).ok().map(|i| &self.nodes[i]) }
    fn width(&self, s: &str) -> i32 { s.chars().map(|c| self.find(c).or_else(|| self.find('?')).map(|n| n.adv as i32).unwrap_or(0)).sum() }
    fn draw(&self, fb: &mut [u8], s: &str, x: i32, baseline: i32, color: u8) {
        let mut pen = x;
        for c in s.chars() {
            let Some(n) = self.find(c).or_else(|| self.find('?')) else { continue };
            let mut bit = (n.off as usize) * 4;
            for row in 0..n.h as i32 {
                let y = baseline - n.top as i32 + row;
                for col in 0..n.w as i32 {
                    let lv = (self.data[bit / 4] >> (2 * (bit % 4))) & 3; bit += 1;
                    let px = pen + n.left as i32 + col;
                    if lv != 0 && px >= 0 && y >= 0 && px < 240 && y < 240 { fb[(y * 240 + px) as usize] = shade(color, lv); }
                }
            }
            pen += n.adv as i32;
        }
    }
}
fn shade(color: u8, lv: u8) -> u8 { let ch = |s: u8| ((((color >> s) & 3) as u16 * lv as u16 + 1) / 3) as u8; 0xC0 | ch(0) | (ch(2) << 2) | (ch(4) << 4) }
#[no_mangle] pub extern "C" fn _start() -> ! {
    let fb = unsafe { &mut *core::ptr::addr_of_mut!(FB) };
    let s = unsafe { core::ptr::read_volatile(&TEXT) };
    let w = atlas::REGULAR_18.width(s);
    atlas::REGULAR_18.draw(fb, s, 120 - w / 2, 40, 0xFF);
    atlas::SEMIBOLD_20.draw(fb, s, 20, 80, 0xFF);
    atlas::SEMIBOLD_54.draw(fb, s, 20, 160, 0xFF);
    unsafe { core::ptr::write_volatile(fb.as_mut_ptr(), 1) };
    loop {}
}
static TEXT: &str = "GYMWORLD12345678 José 12:34";
