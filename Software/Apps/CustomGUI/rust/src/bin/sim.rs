//! Desktop simulator for the RustGuiPoc GUI.
//!
//! Accuracy by construction: this binary links the PoC crate and calls the same
//! [`poc_gui::render`] the device firmware calls, into an identical 240x240
//! ABGR2222 buffer. So the framebuffer is byte-identical to the device's. The
//! sim then only applies the transforms the *physical panel* applies:
//!
//!   1. Color: decode ABGR2222 -> RGB888 using the panel's 2-bits-per-channel
//!      palette (0/85/170/255), so on-screen colors match the device's 64-color
//!      gamut exactly rather than full 24-bit color.
//!   2. Shape: a round mask (black outside the inscribed circle) reproducing the
//!      circular bezel.
//!   3. Panel quirks: an (identity by default) hook, `emulate_panel`, is the one
//!      place to encode device-only behavior once it's characterized against
//!      real hardware (see README "Tightening the sim<->hardware loop").
//!
//! Usage:
//!   cargo run --bin sim --features sim              # live animation
//!   cargo run --bin sim --features sim -- dump.bin  # view a raw device fb dump
//!
//! Controls: any key = next screen; close window = quit.

use std::time::{Duration, Instant};

use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{
    OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window,
};

const W: u32 = 240;
const H: u32 = 240;

/// Decode one ABGR2222 byte to RGB888 using the panel's 2-bit-per-channel gamut.
/// Bit order (verified on-device): B[5:4] G[3:2] R[1:0]; top 2 (alpha) ignored.
fn decode(b: u8) -> Rgb888 {
    let exp = |v: u8| v * 85; // 2-bit -> 8-bit: 0, 85, 170, 255
    Rgb888::new(exp(b & 0b11), exp((b >> 2) & 0b11), exp((b >> 4) & 0b11))
}

/// True if pixel (x, y) falls inside the inscribed circle of the WxH panel.
fn inside_circle(x: u32, y: u32) -> bool {
    let dx = 2 * x as i32 - (W as i32 - 1);
    let dy = 2 * y as i32 - (H as i32 - 1);
    dx * dx + dy * dy <= (W as i32) * (W as i32)
}

/// Device-only panel behavior. Identity today. When we characterize why thin
/// features (e.g. font glyphs) under-render on hardware, encode it HERE, operating
/// on the ABGR2222 buffer before decode — that keeps the sim honest about what
/// the watch will actually show. Calibrate against a real framebuffer dump.
fn emulate_panel(_buf: &mut [u8]) {
    // no-op until calibrated
}

/// Paint one ABGR2222 buffer into the RGB simulator display (decode + round mask).
fn blit(display: &mut SimulatorDisplay<Rgb888>, buf: &[u8]) {
    let pixels = (0..H).flat_map(move |y| {
        (0..W).map(move |x| {
            let color = if inside_circle(x, y) {
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
    let output = OutputSettingsBuilder::new().scale(2).build();
    let mut display = SimulatorDisplay::<Rgb888>::new(Size::new(W, H));
    let mut window = Window::new("RustGuiPoc sim (240x240, 6bpp, round)", &output);

    let mut buf = vec![0u8; (W * H) as usize];

    // Static viewer mode: display a raw device framebuffer dump and exit on close.
    if let Some(path) = std::env::args().nth(1) {
        let data = std::fs::read(&path).unwrap_or_else(|e| panic!("read {path}: {e}"));
        let n = (W * H) as usize;
        assert!(data.len() >= n, "dump too small: {} < {}", data.len(), n);
        buf[..n].copy_from_slice(&data[..n]);
        emulate_panel(&mut buf);
        blit(&mut display, &buf);
        println!("Viewing device dump: {path}");
        loop {
            window.update(&display);
            if window.events().any(|e| e == SimulatorEvent::Quit) {
                return;
            }
            std::thread::sleep(Duration::from_millis(33));
        }
    }

    // Live mode: run the real render loop.
    println!("Live sim. Any key = next screen, close window = quit.");
    let mut screen = 0u32;
    let mut frame = 0u32;
    let tick = Duration::from_millis(33); // ~30 fps, matching the device tick assumption
    loop {
        let start = Instant::now();

        poc_gui::render(&mut buf, W, H, screen, frame);
        emulate_panel(&mut buf);
        blit(&mut display, &buf);
        window.update(&display);

        for event in window.events() {
            match event {
                SimulatorEvent::Quit => return,
                SimulatorEvent::KeyDown { .. } => {
                    screen = (screen + 1) % poc_gui::screen_count();
                }
                _ => {}
            }
        }

        frame = frame.wrapping_add(1);
        if let Some(rem) = tick.checked_sub(start.elapsed()) {
            std::thread::sleep(rem);
        }
    }
}
