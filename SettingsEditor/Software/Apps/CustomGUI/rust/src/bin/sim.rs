//! Drives the real `render()` and the real editor state machine at the real
//! size, with the real four grey levels, so the screens can be looked at
//! before the C++ shell exists.
//!
//! The arrow keys are the watch's four buttons: UP = L1, DOWN = L2,
//! RIGHT/SPACE = R1, LEFT/ESC = R2.
use embedded_graphics::{pixelcolor::Rgb888, prelude::*};
use embedded_graphics_simulator::{
    sdl2::Keycode, OutputSettingsBuilder, SimulatorDisplay, SimulatorEvent, Window,
};
use inscribed_disc::color;
use inscribed_disc::geometry;
use settings_editor_gui::{on_button, Action, Button, State, FIELDS};

/// The crate decodes the panel's byte; the simulator wants its colour type.
fn to_rgb888([r, g, b]: [u8; 3]) -> Rgb888 {
    Rgb888::new(r, g, b)
}

const W: u32 = 240;
const H: u32 = 240;
const DISPLAY_SCALE: u32 = 2;

/// What the shell would read off the watch, in the order `FIELDS` declares.
/// The ranges are the ones `SettingsKit/Header/EditableFields.hpp` writes.
const LIVE: [(u32, u32, u32); 8] = [
    (0, 0, 1),
    (0, 0, 1),
    (30, 5, 240),
    (5000, 500, 50000),
    (5, 1, 200),
    (190, 100, 250),
    (90, 25, 250),
    (184, 90, 220),
];

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

/// Stands in for the shell's read of the live struct and its commit.
fn read_field(state: &mut State, committed: &[u32; 8]) {
    let (_, min, max) = LIVE[state.field as usize];
    state.value = committed[state.field as usize];
    state.value_min = min;
    state.value_max = max;
}

fn main() {
    let output = OutputSettingsBuilder::new().scale(DISPLAY_SCALE).build();
    let mut display = SimulatorDisplay::<Rgb888>::new(Size::new(W, H));
    let mut window = Window::new("SettingsEditor sim (240x240, ABGR2222, round)", &output);

    println!(
        "UP/DOWN = L1/L2, RIGHT or SPACE = R1, LEFT or ESC = R2. \
         0..6 preview a status: 0 ok, 1 live-only, 2 not-saved, 3 cant-confirm, \
         4 set-on-phone, 5 view-only, 6 not-set."
    );

    let mut committed: [u32; 8] = [
        LIVE[0].0, LIVE[1].0, LIVE[2].0, LIVE[3].0, LIVE[4].0, LIVE[5].0, LIVE[6].0, LIVE[7].0,
    ];
    let mut buf = vec![0u8; (W * H) as usize];
    let mut st = State {
        field: 0,
        status: 0,
        editing: 0,
        field_count: FIELDS.len() as u8,
        value: 0,
        value_min: 0,
        value_max: 1,
    };
    read_field(&mut st, &committed);

    settings_editor_gui::render(&mut buf, W, H, &st);
    blit(&mut display, &buf);
    window.update(&display);

    'running: loop {
        for event in window.events() {
            let pressed = match event {
                SimulatorEvent::Quit => break 'running,
                SimulatorEvent::KeyDown { keycode, .. } => match keycode {
                    Keycode::Up => Some(Button::Up),
                    Keycode::Down => Some(Button::Down),
                    Keycode::Right | Keycode::Space | Keycode::Return => Some(Button::Select),
                    Keycode::Left | Keycode::Escape | Keycode::Backspace => Some(Button::Back),
                    // The statuses a wrist only reaches by something going
                    // wrong, each previewable here on purpose.
                    Keycode::Num0 => {
                        st.status = 0;
                        None
                    }
                    Keycode::Num1 => {
                        st.status = 1;
                        None
                    }
                    Keycode::Num2 => {
                        st.status = 2;
                        None
                    }
                    Keycode::Num3 => {
                        st.status = 3;
                        None
                    }
                    Keycode::Num4 => {
                        st.status = 4;
                        None
                    }
                    Keycode::Num5 => {
                        st.status = 5;
                        None
                    }
                    Keycode::Num6 => {
                        st.status = 6;
                        None
                    }
                    _ => None,
                },
                _ => None,
            };

            if let Some(button) = pressed {
                match on_button(&mut st, button) {
                    Action::Exit => break 'running,
                    Action::ReadField => read_field(&mut st, &committed),
                    Action::Commit => {
                        committed[st.field as usize] = st.value;
                        println!("commit {} = {}", FIELDS[st.field as usize].name, st.value);
                    }
                    Action::Redraw | Action::Ignore => {}
                }
            }

            settings_editor_gui::render(&mut buf, W, H, &st);
            blit(&mut display, &buf);
        }
        window.update(&display);
        std::thread::sleep(std::time::Duration::from_millis(33));
    }
}
