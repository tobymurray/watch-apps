//! Every screen as raw ABGR2222 bytes. Temporary instrument; not part of the app.
use notify_toggle_gui::{render, State};

fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    std::fs::create_dir_all(&dir).unwrap();
    let (w, h) = (240u32, 240u32);
    for enabled in 0..2u8 {
        for known in 0..2u8 {
            for status in 0..7u8 {
                let st = State { enabled, known, status, _pad: [0; 1] };
                let mut buf = vec![0u8; (w * h) as usize];
                render(&mut buf, w, h, &st);
                std::fs::write(format!("{dir}/enabled{enabled}_known{known}_status{status}.bin"), &buf).unwrap();
            }
        }
    }
}
