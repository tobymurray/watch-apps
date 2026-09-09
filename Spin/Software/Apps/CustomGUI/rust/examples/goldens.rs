//! Byte hash of every scene, so a refactor can be held to producing the
//! identical frames. Temporary instrument; not part of the app.
use spin_gui::{render, scenes};

fn main() {
    let (w, h) = (240u32, 240u32);
    let mut buf = vec![0u8; (w * h) as usize];
    for (name, frame) in scenes::scenes() {
        buf.iter_mut().for_each(|b| *b = 0);
        render(&mut buf, w, h, &frame);
        let mut hash: u32 = 0x811C_9DC5;
        for &b in &buf {
            hash = (hash ^ b as u32).wrapping_mul(0x0100_0193);
        }
        println!("{hash:08x}  {name}");
    }
}
