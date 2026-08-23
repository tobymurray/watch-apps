//! Desktop simulator for the RustGuiPoc GUI.
//!
//! Accuracy by construction: this binary links the PoC crate and calls the same
//! [`poc_gui::render`] the device firmware calls, with the same [`poc_gui::State`]
//! struct the C++ shim fills, into an identical 240x240 ABGR2222 buffer. So the
//! framebuffer is byte-identical to the device's. The sim then only applies the
//! transforms the *physical panel* applies:
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
//! Because the device gets its `State` from the accelerometer and the sim gets it
//! from the arrow keys, layout and staleness behaviour can be exercised here —
//! including the NO DATA path, which is awkward to reproduce on a wrist.
//!
//! Usage:
//!   cargo run --bin sim --features sim              # interactive
//!   cargo run --bin sim --features sim -- dump.bin  # view a raw device fb dump
//!
//! Controls: arrows = tilt, TAB = next screen, S = toggle stale, close = quit.

use std::time::{Duration, Instant};

use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{
    sdl2::Keycode, OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window,
};
use poc_gui::State;

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
    let mut window = Window::new("RustGuiPoc sim (240x240, ABGR2222, round)", &output);

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

    println!("Interactive sim. Arrows = tilt, TAB = next screen, S = toggle stale.");

    let mut screen = 0u32;
    let mut st = State { accel_z: 1.0, valid: 1, ..Default::default() };
    let mut stale = false;
    // The device's Service samples at 10 Hz; mimic that so AGE on the
    // diagnostics screen behaves the way it does on the watch.
    let sample_every = Duration::from_millis(100);
    let mut last_sample = Instant::now();
    let tick = Duration::from_millis(33); // ~30 fps, matching the device tick

    loop {
        let start = Instant::now();

        for event in window.events() {
            match event {
                SimulatorEvent::Quit => return,
                SimulatorEvent::KeyDown { keycode, .. } => match keycode {
                    Keycode::Tab => screen = (screen + 1) % poc_gui::screen_count(),
                    Keycode::S => stale = !stale,
                    Keycode::Left => st.accel_x = (st.accel_x - 0.1).max(-1.5),
                    Keycode::Right => st.accel_x = (st.accel_x + 0.1).min(1.5),
                    Keycode::Down => st.accel_y = (st.accel_y - 0.1).max(-1.5),
                    Keycode::Up => st.accel_y = (st.accel_y + 0.1).min(1.5),
                    _ => {}
                },
                _ => {}
            }
        }

        // Stand in for the Service: a new sample every 100 ms unless "stale" is
        // toggled, which is how the NO DATA path gets exercised.
        if !stale && last_sample.elapsed() >= sample_every {
            last_sample = Instant::now();
            st.samples = st.samples.wrapping_add(1);
        }
        st.sample_age_ms = last_sample.elapsed().as_millis() as u32;
        st.valid = u8::from(st.sample_age_ms <= 500); // matches Gui.hpp kStaleAfterMs

        poc_gui::render(&mut buf, W, H, screen, &st);
        emulate_panel(&mut buf);
        blit(&mut display, &buf);
        window.update(&display);

        st.frames = st.frames.wrapping_add(1);
        if let Some(rem) = tick.checked_sub(start.elapsed()) {
            std::thread::sleep(rem);
        }
    }
}
