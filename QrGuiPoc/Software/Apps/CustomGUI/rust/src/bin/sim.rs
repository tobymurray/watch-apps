use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window};
use qr_gui::State;

const W: u32 = 240;
const H: u32 = 240;
const CHANNEL_LEVELS: u8 = 85;
const DISPLAY_SCALE: u32 = 2;

fn decode(byte: u8) -> Rgb888 {
    let expand = |two_bit: u8| two_bit * CHANNEL_LEVELS;
    Rgb888::new(
        expand(byte & 0b11),
        expand((byte >> 2) & 0b11),
        expand((byte >> 4) & 0b11),
    )
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

/// The same payload Barcode.hpp's Docs/QR.md uses as its canonical example --
/// what Gui.cpp hardcodes on the watch, encoded here with the identical
/// Qr::encode() the C++ side calls, via the qr_gui_state ABI struct rather than
/// Barcode::Matrix directly (this sim only depends on the crate under test).
fn encode_gymworld() -> State {
    // Encoded offline against Barcode::Qr::encode("GYMWORLD12345678", ...) with
    // encodeWithMask() forced to the mask that encode() itself selects, then
    // dumped bit for bit -- see Barcode/Software/Libs/Tests for the encoder this
    // mirrors. Kept as a literal here so this sim has no dependency on Barcode's
    // headers, matching the ABI boundary the real build crosses.
    include!("../gymworld_state.rs")
}

fn main() {
    let output = OutputSettingsBuilder::new().scale(DISPLAY_SCALE).build();
    let mut display = SimulatorDisplay::<Rgb888>::new(Size::new(W, H));
    let mut window = Window::new("QrGuiPoc sim (240x240, ABGR2222, round)", &output);

    let mut buf = vec![0u8; (W * H) as usize];
    let state = encode_gymworld();
    qr_gui::render(&mut buf, W, H, &state);
    blit(&mut display, &buf);

    loop {
        window.update(&display);
        if window.events().any(|e| e == SimulatorEvent::Quit) {
            return;
        }
        std::thread::sleep(std::time::Duration::from_millis(33));
    }
}
