//! Host preview: the same `render()` the watch calls, decoded back to RGB and
//! masked to the round bezel. Needs SDL2; `preview` writes PNGs without it.
//!
//!   cargo run --features sim --bin sim   # TAB cycles the scenes

use embedded_graphics::{pixelcolor::Rgb888, pixelcolor::RgbColor, prelude::*};
use embedded_graphics_simulator::{OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window};
use spin_gui::scenes::scenes;

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
            let color = if inside_bezel(x, y) {
                decode(buf[(y * W + x) as usize])
            } else {
                Rgb888::BLACK
            };
            Pixel(Point::new(x as i32, y as i32), color)
        })
    });
    display.draw_iter(pixels).unwrap();
}

fn main() {
    let output = OutputSettingsBuilder::new().scale(DISPLAY_SCALE).build();
    let mut display = SimulatorDisplay::<Rgb888>::new(Size::new(W, H));
    let mut window = Window::new("Spin sim (240x240, ABGR2222, round)", &output);

    let scenes = scenes();
    let mut idx = 0usize;
    let mut buf = vec![0u8; (W * H) as usize];
    spin_gui::render(&mut buf, W, H, &scenes[idx].1);
    blit(&mut display, &buf);
    println!("TAB = next scene ({})", scenes[idx].0);

    loop {
        for event in window.events() {
            match event {
                SimulatorEvent::Quit => return,
                SimulatorEvent::KeyDown { keycode, .. } if format!("{keycode:?}") == "Tab" => {
                    idx = (idx + 1) % scenes.len();
                    spin_gui::render(&mut buf, W, H, &scenes[idx].1);
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
