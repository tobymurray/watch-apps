use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{
    sdl2::Keycode, OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window,
};
use inscribed_disc::color;
use inscribed_disc::geometry;
use notify_toggle_gui::State;

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
            let lit = geometry::is_lit(x as i32, y as i32, W as i32, H as i32);
            let color = if lit {
                to_rgb888(color::decode(buf[(y * W + x) as usize]))
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
    let mut window = Window::new("NotifyToggle sim (240x240, ABGR2222, round)", &output);

    println!("Interactive sim. SPACE/R1 = toggle, U = unreadable, N = not saved, L = live only, S = unreadable settings, F = unsupported firmware, ESC/R2 = quit.");

    let mut buf = vec![0u8; (W * H) as usize];
    let mut st = State { enabled: 1, known: 1, status: 0, _pad: [0; 1] };

    notify_toggle_gui::render(&mut buf, W, H, &st);
    blit(&mut display, &buf);
    window.update(&display);

    'running: loop {
        for event in window.events() {
            match event {
                SimulatorEvent::Quit => break 'running,
                SimulatorEvent::KeyDown { keycode, .. } => match keycode {
                    Keycode::Space | Keycode::Return => {
                        st.known = 1;
                        st.status = 0;
                        st.enabled = if st.enabled == 0 { 1 } else { 0 };
                        notify_toggle_gui::render(&mut buf, W, H, &st);
                        blit(&mut display, &buf);
                    }
                    // The three screens a wrist only reaches by something going
                    // wrong, each previewable here on purpose.
                    Keycode::U => {
                        st.known = 0;
                        st.status = 2;
                        notify_toggle_gui::render(&mut buf, W, H, &st);
                        blit(&mut display, &buf);
                    }
                    Keycode::N => {
                        st.known = 1;
                        st.status = 3;
                        notify_toggle_gui::render(&mut buf, W, H, &st);
                        blit(&mut display, &buf);
                    }
                    Keycode::L => {
                        st.known = 1;
                        st.status = 4;
                        notify_toggle_gui::render(&mut buf, W, H, &st);
                        blit(&mut display, &buf);
                    }
                    Keycode::S => {
                        st.known = 0;
                        st.status = 5;
                        notify_toggle_gui::render(&mut buf, W, H, &st);
                        blit(&mut display, &buf);
                    }
                    Keycode::F => {
                        st.known = 0;
                        st.status = 1;
                        notify_toggle_gui::render(&mut buf, W, H, &st);
                        blit(&mut display, &buf);
                    }
                    Keycode::Escape | Keycode::Backspace => break 'running,
                    _ => {}
                },
                _ => {}
            }
        }
        window.update(&display);
        std::thread::sleep(std::time::Duration::from_millis(33));
    }
}
