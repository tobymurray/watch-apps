//! Writes one raw framebuffer per screen to a directory, so the screens can be
//! looked at where SDL is not available:
//!
//!     cargo run --example dump --features std -- /tmp/frames
//!
//! Each file is 240*240 bytes, one ABGR2222 pixel each, exactly what
//! `render()` hands the kernel. `Tools/frames_to_png.py` turns them into
//! images.
use std::fs;
use std::path::PathBuf;

use unit_toggle_gui::{render, State};

const W: u32 = 240;
const H: u32 = 240;

fn main() {
    let dir = PathBuf::from(
        std::env::args().nth(1).expect("usage: dump <output directory>"),
    );
    fs::create_dir_all(&dir).expect("could not create the output directory");

    let screens: [(&str, State); 7] = [
        ("metric", State { imperial: 0, known: 1, status: 0, _pad: [0; 1] }),
        ("imperial", State { imperial: 1, known: 1, status: 0, _pad: [0; 1] }),
        ("live-only", State { imperial: 1, known: 1, status: 4, _pad: [0; 1] }),
        ("not-saved", State { imperial: 1, known: 1, status: 3, _pad: [0; 1] }),
        ("unsupported", State { imperial: 1, known: 1, status: 1, _pad: [0; 1] }),
        ("unreadable", State { imperial: 0, known: 0, status: 2, _pad: [0; 1] }),
        ("no-settings", State { imperial: 0, known: 0, status: 5, _pad: [0; 1] }),
    ];

    let mut buf = vec![0u8; (W * H) as usize];
    for (name, state) in screens {
        render(&mut buf, W, H, &state);
        let path = dir.join(format!("{name}.raw"));
        fs::write(&path, &buf).expect("could not write a frame");
        println!("{}", path.display());
    }
}
