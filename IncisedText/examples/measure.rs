//! Prints a string's advance and ink bounds in one face: `cargo run --example measure POPPINS_SEMIBOLD_18_ASCII "NOTIFICATIONS"`.
use incised_text::faces::*;
use incised_text::Face;

fn face(name: &str) -> &'static Face {
    match name {
        "POPPINS_SEMIBOLD_18_ASCII" => &POPPINS_SEMIBOLD_18_ASCII,
        "POPPINS_REGULAR_14_ASCII" => &POPPINS_REGULAR_14_ASCII,
        "POPPINS_REGULAR_12_ASCII" => &POPPINS_REGULAR_12_ASCII,
        "POPPINS_SEMIBOLD_20_ASCII" => &POPPINS_SEMIBOLD_20_ASCII,
        "POPPINS_REGULAR_18_LATIN" => &POPPINS_REGULAR_18_LATIN,
        "POPPINS_REGULAR_16_LATIN" => &POPPINS_REGULAR_16_LATIN,
        "POPPINS_SEMIBOLD_27_CLOCK" => &POPPINS_SEMIBOLD_27_CLOCK,
        "POPPINS_SEMIBOLD_36_CLOCK" => &POPPINS_SEMIBOLD_36_CLOCK,
        "POPPINS_SEMIBOLD_49_CLOCK" => &POPPINS_SEMIBOLD_49_CLOCK,
        "POPPINS_SEMIBOLD_60_CLOCK" => &POPPINS_SEMIBOLD_60_CLOCK,
        "POPPINS_SEMIBOLD_22_LATIN" => &POPPINS_SEMIBOLD_22_LATIN,
        "POPPINS_SEMIBOLD_26_LATIN" => &POPPINS_SEMIBOLD_26_LATIN,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_16_ASCII" => &ATKINSON_BOLD_16_ASCII,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_20_ASCII" => &ATKINSON_BOLD_20_ASCII,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_25_ASCII" => &ATKINSON_BOLD_25_ASCII,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_49_CLOCK" => &ATKINSON_BOLD_49_CLOCK,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_62_CLOCK" => &ATKINSON_BOLD_62_CLOCK,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_23_LATIN" => &ATKINSON_BOLD_23_LATIN,
        #[cfg(feature = "atkinson")]
        "ATKINSON_BOLD_28_LATIN" => &ATKINSON_BOLD_28_LATIN,
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
