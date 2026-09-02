//! Prints a string's advance and ink bounds in one face: `cargo run --example measure SEMIBOLD_18_ASCII "NOTIFICATIONS"`.
use textkit::faces::*;
use textkit::Face;

fn face(name: &str) -> &'static Face {
    match name {
        "SEMIBOLD_18_ASCII" => &SEMIBOLD_18_ASCII,
        "REGULAR_14_ASCII" => &REGULAR_14_ASCII,
        "REGULAR_12_ASCII" => &REGULAR_12_ASCII,
        "SEMIBOLD_20_ASCII" => &SEMIBOLD_20_ASCII,
        "REGULAR_18_LATIN" => &REGULAR_18_LATIN,
        "REGULAR_16_LATIN" => &REGULAR_16_LATIN,
        "SEMIBOLD_24_ANSWERS" => &SEMIBOLD_24_ANSWERS,
        "SEMIBOLD_32_TITLE" => &SEMIBOLD_32_TITLE,
        "SEMIBOLD_27_CLOCK" => &SEMIBOLD_27_CLOCK,
        "SEMIBOLD_36_CLOCK" => &SEMIBOLD_36_CLOCK,
        "SEMIBOLD_49_CLOCK" => &SEMIBOLD_49_CLOCK,
        "SEMIBOLD_60_CLOCK" => &SEMIBOLD_60_CLOCK,
        _ => panic!("no face {name}; see Tools/faces.json"),
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let f = face(&args[1]);
    for text in &args[2..] {
        let m = f.measure(text);
        println!("{:22} {:?}: advance {} ink {}..{} missing {} (ascent {} descent {})", args[1], text, m.advance, m.ink_left, m.ink_right, m.missing, f.ascent, f.descent);
    }
}
