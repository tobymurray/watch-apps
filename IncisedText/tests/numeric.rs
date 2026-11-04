//! What the numeric faces promise: every 24-hour time CLDR's Latin-digit locales write, the numbers
//! and percentages beside it, tabular digits, and the digit heights Docs/DESIGN.md chose.

use incised_text::faces::*;
use incised_text::Face;

fn numeric() -> Vec<(&'static str, &'static Face, i32)> {
    vec![
        ("POPPINS_SEMIBOLD_47_NUMERIC", &POPPINS_SEMIBOLD_47_NUMERIC, 35),
        ("POPPINS_SEMIBOLD_67_NUMERIC", &POPPINS_SEMIBOLD_67_NUMERIC, 49),
        #[cfg(feature = "atkinson")]
        ("ATKINSON_BOLD_52_NUMERIC", &ATKINSON_BOLD_52_NUMERIC, 35),
        #[cfg(feature = "atkinson")]
        ("ATKINSON_BOLD_72_NUMERIC", &ATKINSON_BOLD_72_NUMERIC, 49),
    ]
}

/// 23:59 in each distinct `-u-hc-h23` `T::hm` pattern ICU4X 2.4 (CLDR 49) holds: `HH:mm`, `H:mm`,
/// `HH.mm`, `H.mm`, `H-mm` and `HH 'h' mm`, then the unset clock.
const TIMES: [&str; 5] = ["23:59", "23.59", "23-59", "23 h 59", "--:--"];

/// CLDR 49 percent and decimal formats with Latin digits: no-break, narrow no-break and Swiss
/// apostrophe grouping, the minus sign, and the decimal comma.
const NUMBERS: [&str; 8] = ["100 %", "100\u{A0}%", "100\u{202F}%", "\u{2212}5\u{A0}%", "-5%", "4,5", "12'345.6", "\u{2212}12\u{202F}345,6"];

#[test]
fn every_cldr_24_hour_time_and_number_is_covered() {
    for (name, face, _) in numeric() {
        for text in TIMES.iter().chain(&NUMBERS) {
            assert!(face.covers(text), "{name}: {text:?} lacks {:?}", face.uncovered(text).collect::<String>());
        }
    }
}

#[test]
fn bulgarian_time_needs_cyrillic_which_no_bundled_font_has() {
    for (name, face, _) in numeric() {
        assert_eq!(face.uncovered("23:59 ч.").collect::<String>(), "ч", "{name}");
    }
}

#[test]
fn numeric_faces_hold_numbers_and_nothing_else() {
    for (name, face, _) in numeric() {
        assert_eq!(face.covered().count(), 21, "{name}");
        assert!(!face.covers("a"), "{name}");
        assert!(!face.covers("H"), "{name}");
    }
}

#[test]
fn digits_are_tabular() {
    for (name, face, _) in numeric() {
        let advances: Vec<u8> = ('0'..='9').map(|c| face.glyph(c).expect("digit").adv).collect();
        assert!(advances.iter().all(|&a| a == advances[0]), "{name}: {advances:?}");
        assert_eq!(face.measure("11:11").advance, face.measure("00:00").advance, "{name}");
    }
}

/// `1` has no overshoot, so its rows are the digit height Docs/DESIGN.md chose for each face.
#[test]
fn digits_stand_at_the_chosen_height() {
    for (name, face, rows) in numeric() {
        assert_eq!(face.glyph('1').unwrap().h as i32, rows, "{name}");
    }
}
