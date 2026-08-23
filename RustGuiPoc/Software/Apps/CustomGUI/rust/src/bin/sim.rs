use std::time::{Duration, Instant};

use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{
    sdl2::Keycode, OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window,
};
use poc_gui::State;

const W: u32 = 240;
const H: u32 = 240;
const CHANNEL_LEVELS: u8 = 85;
const DISPLAY_SCALE: u32 = 2;
const FRAME_INTERVAL: Duration = Duration::from_millis(33);
const SAMPLE_INTERVAL: Duration = Duration::from_millis(100);
const STALE_AFTER_MS: u32 = 500;
const TILT_STEP_G: f32 = 0.1;
const TILT_LIMIT_G: f32 = 1.5;

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

fn emulate_panel(_buf: &mut [u8]) {}

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

fn view_device_dump(path: &str, display: &mut SimulatorDisplay<Rgb888>, window: &mut Window) {
    let data = std::fs::read(path).unwrap_or_else(|e| panic!("read {path}: {e}"));
    let n = (W * H) as usize;
    assert!(data.len() >= n, "dump too small: {} < {}", data.len(), n);

    let mut buf = vec![0u8; n];
    buf.copy_from_slice(&data[..n]);
    emulate_panel(&mut buf);
    blit(display, &buf);
    println!("Viewing device dump: {path}");

    loop {
        window.update(display);
        if window.events().any(|e| e == SimulatorEvent::Quit) {
            return;
        }
        std::thread::sleep(FRAME_INTERVAL);
    }
}

fn clamp_tilt(g: f32) -> f32 {
    g.clamp(-TILT_LIMIT_G, TILT_LIMIT_G)
}

fn main() {
    let output = OutputSettingsBuilder::new().scale(DISPLAY_SCALE).build();
    let mut display = SimulatorDisplay::<Rgb888>::new(Size::new(W, H));
    let mut window = Window::new("RustGuiPoc sim (240x240, ABGR2222, round)", &output);

    if let Some(path) = std::env::args().nth(1) {
        view_device_dump(&path, &mut display, &mut window);
        return;
    }

    println!("Interactive sim. Arrows = tilt, TAB = next screen, S = toggle stale.");

    let mut buf = vec![0u8; (W * H) as usize];
    let mut screen = 0u32;
    let mut st = State { accel_z_g: 1.0, valid: 1, ..Default::default() };
    let mut sampling = true;
    let mut last_sample = Instant::now();

    loop {
        let start = Instant::now();

        for event in window.events() {
            match event {
                SimulatorEvent::Quit => return,
                SimulatorEvent::KeyDown { keycode, .. } => match keycode {
                    Keycode::Tab => screen = (screen + 1) % poc_gui::screen_count(),
                    Keycode::S => sampling = !sampling,
                    Keycode::Left => st.accel_x_g = clamp_tilt(st.accel_x_g - TILT_STEP_G),
                    Keycode::Right => st.accel_x_g = clamp_tilt(st.accel_x_g + TILT_STEP_G),
                    Keycode::Down => st.accel_y_g = clamp_tilt(st.accel_y_g - TILT_STEP_G),
                    Keycode::Up => st.accel_y_g = clamp_tilt(st.accel_y_g + TILT_STEP_G),
                    _ => {}
                },
                _ => {}
            }
        }

        if sampling && last_sample.elapsed() >= SAMPLE_INTERVAL {
            last_sample = Instant::now();
            st.samples = st.samples.wrapping_add(1);
        }
        st.sample_age_ms = last_sample.elapsed().as_millis() as u32;
        st.valid = u8::from(st.sample_age_ms <= STALE_AFTER_MS);

        poc_gui::render(&mut buf, W, H, screen, &st);
        emulate_panel(&mut buf);
        blit(&mut display, &buf);
        window.update(&display);

        st.frames = st.frames.wrapping_add(1);
        if let Some(remaining) = FRAME_INTERVAL.checked_sub(start.elapsed()) {
            std::thread::sleep(remaining);
        }
    }
}
