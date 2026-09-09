//! One PNG per scene plus a contact sheet, through the same `render()` the
//! watch calls, and through PanelKit's decode and disc mask so the image cannot
//! show a pixel the renderer was not allowed to draw.
//!
//! This file used to carry its own `inside_bezel`, testing `dx² + dy² <= 240²`
//! where the panel's rule is `239²` -- so it showed 436 pixels the watch does
//! not light, which is the wrong direction for a preview to be wrong in.
//!
//!   cargo run --features preview --bin preview [-- <out-dir>]

use panelkit::preview::{sheet_from_frames, to_rgba, write_png, Bezel, Options};
use spin_gui::scenes::scenes;

const W: u32 = 240;
const H: u32 = 240;

fn main() {
    let dir = std::env::args().nth(1).unwrap_or_else(|| "/tmp/spin_gui_preview".to_string());
    std::fs::create_dir_all(&dir).expect("cannot create the output directory");

    // 240x240 with black corners, which is what the five montages in
    // Spin/Docs were assembled from and what Spin/README.md's "blacks out
    // everything outside the round bezel" describes. The kit's own default is
    // transparent at scale 2; matching the checked-in artefacts matters more
    // here than the kit's default does, because the README documents this
    // command as the way to regenerate them.
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

    // Six across fits the catalogue on a sheet that reads at arm's length; the
    // order is the catalogue's, so it reads against scenes() beside it. Named
    // apart from Docs/screens.png, which is the README's curated four-panel
    // banner and a different artefact -- a 43-cell sheet under a caption
    // promising four named screens is a worse hero image whatever it contains.
    let sheet = format!("{dir}/contact-sheet.png");
    let (w, h) = sheet_from_frames(&sheet, &frames, W, H, 6, Options { scale: 1, bezel: Bezel::Transparent })
        .expect("cannot write the contact sheet");
    println!("wrote {sheet} ({w}x{h}, {} scenes)", frames.len());
}
