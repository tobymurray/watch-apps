//! One PNG per scene, through the same `render()` the watch calls. Needs a PNG
//! encoder and nothing else, so it runs anywhere.
//!
//! It blacks out the corners outside the round bezel, which the device
//! simulator does not: the simulator draws the full 240x240 square with no
//! bezel, so a clipping mistake is invisible there and reached the watch twice
//! in Barcode before it was caught. An unmasked screenshot is not evidence
//! about this panel.
//!
//!   cargo run --features preview --bin preview [-- <out-dir>]

use haptic_lab_gui::scenes::scenes;
use png::{BitDepth, ColorType, Encoder};

const W: u32 = 240;
const H: u32 = 240;
/// ABGR2222 back to 8-bit RGB: each 2-bit channel is one of 0/85/170/255.
const CHANNEL_LEVELS: u8 = 85;

fn decode(byte: u8) -> [u8; 3] {
    let expand = |two_bit: u8| two_bit * CHANNEL_LEVELS;
    [expand(byte & 0b11), expand((byte >> 2) & 0b11), expand((byte >> 4) & 0b11)]
}

/// `BarcodeLayout::pixelIsLit`'s rule: a pixel is glass only if its centre is
/// within 119.5 dot pitches of the centre.
fn inside_bezel(x: u32, y: u32) -> bool {
    let dx = 2 * x as i32 - (W as i32 - 1);
    let dy = 2 * y as i32 - (H as i32 - 1);
    let d = W.min(H) as i32 - 1;
    dx * dx + dy * dy <= d * d
}

fn main() {
    let dir = std::env::args().nth(1).unwrap_or_else(|| "/tmp/haptic_lab_preview".to_string());
    std::fs::create_dir_all(&dir).expect("cannot create the output directory");

    let mut outside_total = 0usize;
    for (name, frame) in scenes() {
        let mut buf = vec![0u8; (W * H) as usize];
        haptic_lab_gui::render(&mut buf, W, H, &frame);

        let mut outside = 0usize;
        let mut rgb = vec![0u8; (W * H * 3) as usize];
        for y in 0..H {
            for x in 0..W {
                let raw = buf[(y * W + x) as usize];
                let lit = inside_bezel(x, y);
                if !lit && raw != 0xC0 {
                    outside += 1;
                }
                let c = if lit { decode(raw) } else { [0; 3] };
                let i = ((y * W + x) * 3) as usize;
                rgb[i..i + 3].copy_from_slice(&c);
            }
        }
        outside_total += outside;

        let path = format!("{dir}/{name}.png");
        let file = std::fs::File::create(&path).expect("cannot create the PNG");
        let mut encoder = Encoder::new(std::io::BufWriter::new(file), W, H);
        encoder.set_color(ColorType::Rgb);
        encoder.set_depth(BitDepth::Eight);
        encoder
            .write_header()
            .expect("cannot write the PNG header")
            .write_image_data(&rgb)
            .expect("cannot write the PNG body");

        println!("wrote {path} ({outside} px outside the bezel)");
    }

    // The figure that has to be quoted next to a screenshot in a document.
    println!("ink outside the bezel, all scenes: {outside_total} px");
}
