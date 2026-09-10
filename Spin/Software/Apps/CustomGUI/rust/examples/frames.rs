//! Every scene as raw ABGR2222 bytes. Temporary instrument; not part of the app.
use spin_gui::{render, scenes};

fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    std::fs::create_dir_all(&dir).unwrap();
    let (w, h) = (240u32, 240u32);
    for (name, frame) in scenes::scenes() {
        let mut buf = vec![0u8; (w * h) as usize];
        render(&mut buf, w, h, &frame);
        std::fs::write(format!("{dir}/{name}.bin"), &buf).unwrap();
    }
}
