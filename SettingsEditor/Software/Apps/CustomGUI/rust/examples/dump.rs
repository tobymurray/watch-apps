//! Writes one raw framebuffer per screen to a directory, so the screens can be
//! looked at where SDL is not available:
//!
//!     cargo run --example dump --features std -- /tmp/frames
//!
//! Each file is 240*240 bytes, one ABGR2222 pixel each, exactly what `render()`
//! hands the kernel. `Tools/contact_sheet.py` lays them out as one image.
use std::fs;
use std::path::PathBuf;

use settings_editor_gui::{render, State, FIELDS};

const W: u32 = 240;
const H: u32 = 240;

fn state(field: u8, status: u8, editing: u8, value: u32, min: u32, max: u32) -> State {
    State {
        field,
        status,
        editing,
        field_count: FIELDS.len() as u8,
        value,
        value_min: min,
        value_max: max,
    }
}

fn main() {
    let dir = PathBuf::from(
        std::env::args()
            .nth(1)
            .expect("usage: dump <output directory>"),
    );
    fs::create_dir_all(&dir).expect("could not create the output directory");

    // The field the wearer is on, its live value and range, and what may be
    // claimed about it. Every screen the app can draw is here, including the
    // ones a wrist only reaches by something going wrong.
    let screens: [(&str, State); 12] = [
        ("01-units-metric", state(0, 0, 0, 0, 0, 1)),
        ("02-notifications-off", state(1, 0, 0, 0, 0, 1)),
        ("03-step-goal", state(3, 0, 0, 5000, 500, 50000)),
        ("04-step-goal-editing", state(3, 0, 1, 8000, 500, 50000)),
        ("05-weight", state(6, 0, 0, 90, 25, 250)),
        ("06-weight-editing", state(6, 0, 1, 75, 25, 250)),
        ("07-max-heart-rate", state(7, 0, 0, 184, 90, 220)),
        ("08-max-heart-rate-at-the-top", state(7, 0, 1, 220, 90, 220)),
        ("09-live-only", state(2, 1, 0, 30, 5, 240)),
        ("10-not-saved", state(6, 2, 0, 75, 25, 250)),
        (
            "11-max-heart-rate-set-on-phone",
            state(7, 4, 0, 100, 90, 220),
        ),
        ("12-height-not-set", state(5, 6, 0, 0, 100, 250)),
    ];

    let mut buf = vec![0u8; (W * H) as usize];
    for (name, st) in screens {
        render(&mut buf, W, H, &st);
        let path = dir.join(format!("{name}.raw"));
        fs::write(&path, &buf).expect("could not write a frame");
        println!("{}", path.display());
    }
}
