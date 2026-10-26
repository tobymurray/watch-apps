//! Draws the strings a round watch face shows in each face 0.2 added, on the host, into
//! `Docs/previews/<FACE>.png`: `cargo run --release` here.
use incised_text::{faces::*, Face, Style};
use inscribed_disc::{Abgr2222, Surface};

const STRINGS: [&str; 11] = [
    "Mon 26 Oct",
    "Wed 30 Sept",
    "mer. 30 sept.",
    "Mi., 30. Sept.",
    "März",
    "śr. 30 paź",
    "Ieșire, țintă",
    "100 %",
    "--:--",
    "23.59",
    "23:59",
];

/// A numeric face draws only these: the strings above it can draw, and the other CLDR times.
const NUMBERS: [&str; 7] = ["100 %", "--:--", "23.59", "23:59", "23 h 59", "23-59", "\u{2212}5 %"];

const FACES: [(&str, &Face); 8] = [
    ("POPPINS_SEMIBOLD_22_LATIN", &POPPINS_SEMIBOLD_22_LATIN),
    ("POPPINS_SEMIBOLD_26_LATIN", &POPPINS_SEMIBOLD_26_LATIN),
    ("POPPINS_SEMIBOLD_47_NUMERIC", &POPPINS_SEMIBOLD_47_NUMERIC),
    ("POPPINS_SEMIBOLD_67_NUMERIC", &POPPINS_SEMIBOLD_67_NUMERIC),
    ("ATKINSON_BOLD_23_LATIN", &ATKINSON_BOLD_23_LATIN),
    ("ATKINSON_BOLD_28_LATIN", &ATKINSON_BOLD_28_LATIN),
    ("ATKINSON_BOLD_52_NUMERIC", &ATKINSON_BOLD_52_NUMERIC),
    ("ATKINSON_BOLD_72_NUMERIC", &ATKINSON_BOLD_72_NUMERIC),
];

/// Each panel pixel is drawn as a SCALE × SCALE block so the 2bpp levels stay visible.
const SCALE: usize = 2;
const MARGIN: i32 = 8;

fn main() {
    let out = concat!(env!("CARGO_MANIFEST_DIR"), "/../../previews");
    std::fs::create_dir_all(out).unwrap();
    for (name, face) in FACES {
        let strings: &[&str] = if face.covers("a") { &STRINGS } else { &NUMBERS };
        let width = strings.iter().map(|s| face.measure(s).advance).max().unwrap() + 2 * MARGIN;
        let height = strings.len() as i32 * face.line_height() + 2 * MARGIN;
        let (w, h) = (width as u32, height as u32);
        let mut frame = vec![Abgr2222::BLACK.0; (w * h) as usize];
        let mut surface = Surface::<Abgr2222>::rect(&mut frame, w, h).unwrap();
        for (i, text) in strings.iter().enumerate() {
            let top = MARGIN + i as i32 * face.line_height();
            face.draw_top(&mut surface, text, MARGIN, top, Style::left(Abgr2222::WHITE, Abgr2222::BLACK)).unwrap();
        }
        let grey: Vec<u8> = frame.iter().map(|&b| (b & 3) * 85).collect();
        let png = png(&grey, w as usize, h as usize);
        std::fs::write(format!("{out}/{name}.png"), png).unwrap();
    }
}

fn png(grey: &[u8], w: usize, h: usize) -> Vec<u8> {
    let (sw, sh) = (w * SCALE, h * SCALE);
    let pixels: Vec<u8> = (0..sw * sh).map(|i| grey[(i / sw / SCALE) * w + (i % sw) / SCALE]).collect();
    let mut out = Vec::new();
    let mut encoder = png::Encoder::new(&mut out, sw as u32, sh as u32);
    encoder.set_color(png::ColorType::Grayscale);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.set_compression(png::Compression::Best);
    encoder.write_header().unwrap().write_image_data(&pixels).unwrap();
    out
}
