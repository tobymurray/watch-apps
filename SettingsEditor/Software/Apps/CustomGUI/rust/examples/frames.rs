//! Every screen as raw ABGR2222 bytes. Temporary instrument; not part of the app.
use settings_editor_gui::{render, State};

fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    std::fs::create_dir_all(&dir).unwrap();
    let (w, h) = (240u32, 240u32);
    let field_count = 8u8;
    for field in 0..field_count {
        for status in 0..7u8 {
            for editing in 0..2u8 {
                for (tag, value, lo, hi) in [
                    ("min", 0u32, 0u32, 100u32),
                    ("mid", 50, 0, 100),
                    ("max", 100, 0, 100),
                    ("wide", 12345, 0, 65535),
                ] {
                    let st = State {
                        field,
                        status,
                        editing,
                        field_count,
                        value,
                        value_min: lo,
                        value_max: hi,
                    };
                    let mut buf = vec![0u8; (w * h) as usize];
                    render(&mut buf, w, h, &st);
                    std::fs::write(
                        format!("{dir}/field{field}_status{status}_editing{editing}_{tag}.bin"),
                        &buf,
                    )
                    .unwrap();
                }
            }
        }
    }
}
