//! Every screen as raw ABGR2222 bytes. Temporary instrument; not part of the app.
//!
//! The bar widths and matrix bits are synthetic patterns, not real symbols: this
//! compares one build against another, so the input only has to be identical in
//! both and to reach every drawing path.
use barcode_gui::*;

fn blank() -> Frame {
    Frame {
        total_modules: 0,
        kind: KIND_PROMPT,
        width_count: 0,
        widths: [0; MAX_WIDTHS],
        matrix_bits: [0; MAX_MATRIX_BITS],
        matrix_size: 0,
        id: [0; ID_LEN],
        name: [0; NAME_LEN],
        index: 0,
        count: 0,
        message: [0; MESSAGE_LEN],
    }
}

fn put(dst: &mut [u8], s: &str) {
    let n = s.len().min(dst.len());
    dst[..n].copy_from_slice(&s.as_bytes()[..n]);
    for b in &mut dst[n..] {
        *b = 0;
    }
}

fn linear(kind: u8, id: &str, name: &str, bars: usize, index: u8, count: u8) -> Frame {
    let mut f = blank();
    f.kind = kind;
    f.width_count = bars as u8;
    // 1,2,3,4 repeating: every bar width the renderer can be handed.
    for i in 0..bars {
        f.widths[i] = (i % 4 + 1) as u8;
    }
    f.total_modules = f.widths[..bars].iter().map(|&w| w as u16).sum();
    put(&mut f.id, id);
    put(&mut f.name, name);
    f.index = index;
    f.count = count;
    f
}

fn qr(size: u8, id: &str, name: &str) -> Frame {
    let mut f = blank();
    f.kind = KIND_QR;
    f.matrix_size = size;
    // A checkerboard: alternating modules exercise both dark and light.
    for i in 0..MAX_MATRIX_BITS {
        f.matrix_bits[i] = 0b1010_1010;
    }
    put(&mut f.id, id);
    put(&mut f.name, name);
    f
}

fn prompt(message: &str) -> Frame {
    let mut f = blank();
    f.kind = KIND_PROMPT;
    put(&mut f.message, message);
    f
}

fn main() {
    let dir = std::env::args().nth(1).expect("output directory");
    std::fs::create_dir_all(&dir).unwrap();
    let (w, h) = (240u32, 240u32);

    let mut frames: Vec<(String, Frame)> = Vec::new();
    for (tag, kind) in [("code128", KIND_CODE128), ("itf", KIND_ITF)] {
        for (idtag, id) in [
            ("short", "A1234"),
            ("tier_edge", "0123456789ABCD"),
            ("tier_over", "0123456789ABCDE"),
            ("longest", "0123456789ABCDEFGHIJKLM"),
        ] {
            for (ntag, name, index, count) in
                [("noname", "", 0u8, 0u8), ("named", "GYMWORLD", 2, 5), ("widename", "WWWWWWWWWWWW", 1, 3)]
            {
                for bars in [11usize, 60, MAX_WIDTHS] {
                    frames.push((
                        format!("{tag}_{idtag}_{ntag}_bars{bars}"),
                        linear(kind, id, name, bars, index, count),
                    ));
                }
            }
        }
    }
    for size in [21u8, 25] {
        frames.push((format!("qr_{size}"), qr(size, "0123456789ABCD", "GYMWORLD")));
    }
    for (tag, msg) in [
        ("empty", ""),
        ("short", "No codes yet."),
        ("wrapping", "No codes yet. Set one in the UNA app, or write input.json."),
        ("long", "Set it to Code128, then reopen this app so it can read the file again and show you the barcode it built."),
    ] {
        frames.push((format!("prompt_{tag}"), prompt(msg)));
    }

    for (name, frame) in frames {
        let mut buf = vec![0u8; (w * h) as usize];
        render(&mut buf, w, h, &frame);
        std::fs::write(format!("{dir}/{name}.bin"), &buf).unwrap();
    }
}
