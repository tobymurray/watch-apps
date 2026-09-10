//! One PNG per scene plus a contact sheet, through the same `render()` the
//! watch calls and InscribedDisc's own disc mask.
//!
//!   cargo run --features preview --bin preview [-- <out-dir>]

use inscribed_disc::preview::{sheet_from_frames, to_rgba, write_png, Bezel, Options};
use spin_gui::scenes::scenes;

const W: u32 = 240;
const H: u32 = 240;

fn main() {
    let dir = std::env::args().nth(1).unwrap_or_else(|| "/tmp/spin_gui_preview".to_string());
    std::fs::create_dir_all(&dir).expect("cannot create the output directory");

    // 240x240 with black corners: the montages in Docs/ were assembled from
    // this output, so changing it stops them regenerating.
    let opts = Options { scale: 1, bezel: Bezel::Fill([0, 0, 0]) };
    let mut frames = Vec::new();

    for (name, frame) in scenes() {
        let mut buf = vec![0u8; (W * H) as usize];
        spin_gui::render(&mut buf, W, H, &frame);

        let rgba = to_rgba(&buf, W, H, opts);
        let path = format!("{dir}/{name}.png");
        write_png(&path, &rgba, W * opts.scale, H * opts.scale).expect("cannot write the PNG");
        println!("wrote {path}");
        frames.push(buf);
    }

    // Named apart from Docs/screens.png, which is a curated four-panel banner
    // and a different artefact.
    let sheet = format!("{dir}/contact-sheet.png");
    let (w, h) = sheet_from_frames(&sheet, &frames, W, H, 6, Options { scale: 1, bezel: Bezel::Transparent })
        .expect("cannot write the contact sheet");
    println!("wrote {sheet} ({w}x{h}, {} scenes)", frames.len());
}
