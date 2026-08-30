use barcode_gui::{Frame, KIND_CODE128, KIND_ITF, KIND_PROMPT, KIND_QR};
use embedded_graphics::{pixelcolor::Rgb888, pixelcolor::RgbColor, prelude::*};
use embedded_graphics_simulator::{OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window};

const W: u32 = 240;
const H: u32 = 240;
const CHANNEL_LEVELS: u8 = 85;
const DISPLAY_SCALE: u32 = 2;

fn decode(byte: u8) -> Rgb888 {
    let expand = |two_bit: u8| two_bit * CHANNEL_LEVELS;
    Rgb888::new(expand(byte & 0b11), expand((byte >> 2) & 0b11), expand((byte >> 4) & 0b11))
}

fn inside_bezel(x: u32, y: u32) -> bool {
    let dx = 2 * x as i32 - (W as i32 - 1);
    let dy = 2 * y as i32 - (H as i32 - 1);
    dx * dx + dy * dy <= (W as i32) * (W as i32)
}

fn blit(display: &mut SimulatorDisplay<Rgb888>, buf: &[u8]) {
    let pixels = (0..H).flat_map(move |y| {
        (0..W).map(move |x| {
            let color = if inside_bezel(x, y) { decode(buf[(y * W + x) as usize]) } else { Rgb888::BLACK };
            Pixel(Point::new(x as i32, y as i32), color)
        })
    });
    display.draw_iter(pixels).unwrap();
}

fn str_into(dst: &mut [u8], s: &str) {
    for (d, b) in dst.iter_mut().zip(s.as_bytes()) {
        *d = *b;
    }
}

fn empty_frame() -> Frame {
    Frame {
        total_modules: 0,
        kind: KIND_CODE128,
        width_count: 0,
        widths: [0; barcode_gui::MAX_WIDTHS],
        matrix_bits: [0; barcode_gui::MAX_MATRIX_BITS],
        matrix_size: 0,
        id: [0; barcode_gui::ID_LEN],
        name: [0; barcode_gui::NAME_LEN],
        index: 0,
        count: 1,
        message: [0; barcode_gui::MESSAGE_LEN],
    }
}

fn linear(kind: u8, widths: &[u8], total_modules: u16, id: &str, name: &str, index: u8, count: u8) -> Frame {
    let mut f = empty_frame();
    f.kind = kind;
    f.width_count = widths.len() as u8;
    f.widths[..widths.len()].copy_from_slice(widths);
    f.total_modules = total_modules;
    str_into(&mut f.id, id);
    str_into(&mut f.name, name);
    f.index = index;
    f.count = count;
    f
}

fn qr(bits: &[u8; 79], size: u8, id: &str) -> Frame {
    let mut f = empty_frame();
    f.kind = KIND_QR;
    f.matrix_bits = *bits;
    f.matrix_size = size;
    str_into(&mut f.id, id);
    f
}

fn prompt(message: &str) -> Frame {
    let mut f = empty_frame();
    f.kind = KIND_PROMPT;
    str_into(&mut f.message, message);
    f
}

// Real encoder output, generated once from a host build of the unmodified
// Barcode::Code128/Itf/Qr encoders -- see the module doc in lib.rs's test
// module and QrGuiPoc's own gymworld_state.rs for the same pattern.
const CODE128_A1234: [u8; 43] = [
    2, 1, 1, 2, 1, 4, 1, 1, 1, 3, 2, 3, 1, 1, 3, 1, 4, 1, 1, 1, 2, 2, 3, 2, 1, 3, 1, 1, 2, 3, 1,
    1, 4, 1, 1, 3, 2, 3, 3, 1, 1, 1, 2,
];
const CODE128_MAXLEN: [u8; 91] = [
    2, 1, 1, 2, 3, 2, 2, 2, 2, 1, 2, 2, 3, 1, 2, 1, 3, 1, 1, 1, 3, 1, 2, 3, 1, 4, 1, 1, 2, 2, 2,
    1, 2, 1, 4, 1, 1, 1, 4, 1, 3, 1, 1, 1, 1, 3, 2, 3, 1, 3, 1, 1, 2, 3, 1, 3, 1, 3, 2, 1, 1, 1,
    2, 3, 1, 3, 1, 3, 2, 1, 1, 3, 1, 3, 2, 3, 1, 1, 1, 1, 2, 3, 1, 3, 2, 3, 3, 1, 1, 1, 2,
];
const ITF_123456: [u8; 37] = [
    1, 1, 1, 1, 3, 1, 1, 3, 1, 1, 1, 1, 3, 3, 3, 1, 3, 1, 1, 3, 1, 1, 1, 3, 3, 1, 1, 3, 3, 3, 1,
    1, 1, 1, 3, 1, 1,
];
const ITF_MAXLEN: [u8; 87] = [
    1, 1, 1, 1, 3, 1, 1, 3, 1, 1, 1, 1, 3, 3, 3, 1, 3, 1, 1, 3, 1, 1, 1, 3, 3, 1, 1, 3, 3, 3, 1,
    1, 1, 1, 1, 3, 1, 1, 1, 1, 3, 3, 3, 1, 1, 1, 3, 1, 1, 3, 3, 3, 1, 1, 3, 1, 1, 3, 1, 1, 1, 1,
    3, 3, 3, 1, 3, 1, 1, 3, 1, 1, 1, 3, 3, 1, 1, 3, 3, 3, 1, 1, 1, 1, 3, 1, 1,
];
include!("../gymworld_bits.rs");

fn scenes() -> Vec<(&'static str, Frame)> {
    vec![
        ("code128_short", linear(KIND_CODE128, &CODE128_A1234, 79, "A1234", "Alice", 0, 3)),
        ("code128_max", linear(KIND_CODE128, &CODE128_MAXLEN, 167, "0123456789ABCDEF", "Bob", 1, 3)),
        ("itf_short", linear(KIND_ITF, &ITF_123456, 63, "123456", "", 2, 3)),
        ("itf_max", linear(KIND_ITF, &ITF_MAXLEN, 153, "1234567890123456", "Carol Longnm", 0, 1)),
        ("qr", qr(&GYMWORLD_BITS, 25, "GYMWORLD12345678")),
        ("prompt_noconfig", prompt("No codes yet. Set one in the UNA app, or write input.json.")),
        ("prompt_novalue", prompt("input.json has no usable code")),
        ("prompt_notset", prompt("No codes set yet. Open the UNA app and enter your ID")),
        ("prompt_badvalue", prompt("That ID cannot be drawn: 1-16 plain characters")),
        ("prompt_baddigitcount", prompt("ITF needs an even count of digits, 2 to 16")),
        ("prompt_badcharacters", prompt("ITF only draws digits 0-9")),
        ("prompt_badwhitespace", prompt("That ID starts or ends with a space, remove it")),
        ("prompt_badformat", prompt("Unknown format. Set it to Code128, QRCode or ITF.")),
        ("pager_six", linear(KIND_CODE128, &CODE128_A1234, 79, "A1234", "", 4, 6)),
        ("id_split_worst_case", linear(KIND_CODE128, &CODE128_A1234, 79, "WWWWWWWWWWWWWWWW", "", 0, 1)),
    ]
}

fn dump_pngs() {
    use std::io::Write;
    std::fs::create_dir_all("/tmp/barcode_gui_preview").unwrap();
    for (name, frame) in scenes() {
        let mut buf = vec![0u8; (W * H) as usize];
        barcode_gui::render(&mut buf, W, H, &frame);

        let mut png_data = Vec::new();
        {
            let mut encoder = png::Encoder::new(&mut png_data, W, H);
            encoder.set_color(png::ColorType::Rgb);
            encoder.set_depth(png::BitDepth::Eight);
            let mut writer = encoder.write_header().unwrap();
            let mut rgb = vec![0u8; (W * H * 3) as usize];
            for y in 0..H {
                for x in 0..W {
                    let c = if inside_bezel(x, y) { decode(buf[(y * W + x) as usize]) } else { Rgb888::BLACK };
                    let i = ((y * W + x) * 3) as usize;
                    rgb[i] = c.r();
                    rgb[i + 1] = c.g();
                    rgb[i + 2] = c.b();
                }
            }
            writer.write_image_data(&rgb).unwrap();
        }
        let path = format!("/tmp/barcode_gui_preview/{name}.png");
        let mut f = std::fs::File::create(&path).unwrap();
        f.write_all(&png_data).unwrap();
        println!("wrote {path}");
    }
}

fn main() {
    if std::env::args().any(|a| a == "--dump") {
        dump_pngs();
        return;
    }

    let output = OutputSettingsBuilder::new().scale(DISPLAY_SCALE).build();
    let mut display = SimulatorDisplay::<Rgb888>::new(Size::new(W, H));
    let mut window = Window::new("Barcode sim (240x240, ABGR2222, round)", &output);

    let scenes = scenes();
    let mut idx = 0usize;
    let mut buf = vec![0u8; (W * H) as usize];
    barcode_gui::render(&mut buf, W, H, &scenes[idx].1);
    blit(&mut display, &buf);
    println!("TAB = next scene ({})", scenes[idx].0);

    loop {
        for event in window.events() {
            match event {
                SimulatorEvent::Quit => return,
                SimulatorEvent::KeyDown { keycode, .. } if format!("{keycode:?}") == "Tab" => {
                    idx = (idx + 1) % scenes.len();
                    barcode_gui::render(&mut buf, W, H, &scenes[idx].1);
                    blit(&mut display, &buf);
                    println!("scene: {}", scenes[idx].0);
                }
                _ => {}
            }
        }
        window.update(&display);
        std::thread::sleep(std::time::Duration::from_millis(33));
    }
}
