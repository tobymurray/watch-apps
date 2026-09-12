use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window};
use inscribed_disc::color;
use inscribed_disc::geometry;
use qr_gui::State;

/// The crate decodes the panel's byte; the simulator wants its colour type.
fn to_rgb888([r, g, b]: [u8; 3]) -> Rgb888 {
    Rgb888::new(r, g, b)
}

const W: u32 = 240;
const H: u32 = 240;
const DISPLAY_SCALE: u32 = 2;

fn blit(display: &mut SimulatorDisplay<Rgb888>, buf: &[u8]) {
    let pixels = (0..H).flat_map(move |y| {
        (0..W).map(move |x| {
            let color = if geometry::is_lit(x as i32, y as i32, W as i32, H as i32) {
                to_rgb888(color::decode(buf[(y * W + x) as usize]))
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
