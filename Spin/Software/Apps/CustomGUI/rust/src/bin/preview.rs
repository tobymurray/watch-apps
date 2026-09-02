//! One PNG per scene, through the same `render()` the watch calls. Needs a PNG
//! encoder and nothing else, so it runs anywhere. Blacks out the corners
//! outside the round bezel, which the device simulator does not.
//!
//!   cargo run --features preview --bin preview [-- <out-dir>]

use png::{BitDepth, ColorType, Encoder};
use spin_gui::scenes::scenes;

const W: u32 = 240;
const H: u32 = 240;
/// ABGR2222 back to 8-bit RGB: each 2-bit channel is one of 0/85/170/255.
const CHANNEL_LEVELS: u8 = 85;

fn decode(byte: u8) -> [u8; 3] {
    let expand = |two_bit: u8| two_bit * CHANNEL_LEVELS;
    [expand(byte & 0b11), expand((byte >> 2) & 0b11), expand((byte >> 4) & 0b11)]
}

fn inside_bezel(x: u32, y: u32) -> bool {
    let dx = 2 * x as i32 - (W as i32 - 1);
    let dy = 2 * y as i32 - (H as i32 - 1);
    dx * dx + dy * dy <= (W as i32) * (W as i32)
}

fn main() {
    let dir = std::env::args().nth(1).unwrap_or_else(|| "/tmp/spin_gui_preview".to_string());
    std::fs::create_dir_all(&dir).expect("cannot create the output directory");

    for (name, frame) in scenes() {
        let mut buf = vec![0u8; (W * H) as usize];
        spin_gui::render(&mut buf, W, H, &frame);

        let mut rgb = vec![0u8; (W * H * 3) as usize];
        for y in 0..H {
            for x in 0..W {
                let c = if inside_bezel(x, y) { decode(buf[(y * W + x) as usize]) } else { [0; 3] };
                let i = ((y * W + x) * 3) as usize;
                rgb[i..i + 3].copy_from_slice(&c);
            }
        }

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

        println!("wrote {path}");
    }
}
