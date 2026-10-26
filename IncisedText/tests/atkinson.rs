//! What the Atkinson Hyperlegible Next faces are for: clock digits that keep one width, and
//! confusable characters that never draw alike.
#![cfg(feature = "atkinson")]

use incised_text::faces::*;
use incised_text::Face;

fn clocks() -> [(&'static str, &'static Face); 2] {
    [("ATKINSON_BOLD_49_CLOCK", &ATKINSON_BOLD_49_CLOCK), ("ATKINSON_BOLD_62_CLOCK", &ATKINSON_BOLD_62_CLOCK)]
}

fn ascii() -> [(&'static str, &'static Face); 3] {
    [
        ("ATKINSON_BOLD_16_ASCII", &ATKINSON_BOLD_16_ASCII),
        ("ATKINSON_BOLD_20_ASCII", &ATKINSON_BOLD_20_ASCII),
        ("ATKINSON_BOLD_25_ASCII", &ATKINSON_BOLD_25_ASCII),
    ]
}

fn latin() -> [(&'static str, &'static Face); 2] {
    [("ATKINSON_BOLD_23_LATIN", &ATKINSON_BOLD_23_LATIN), ("ATKINSON_BOLD_28_LATIN", &ATKINSON_BOLD_28_LATIN)]
}

/// Width, height and every coverage level: the glyph as the panel would show it, wherever it sits.
fn bitmap(face: &Face, c: char) -> (u8, u8, Vec<u8>) {
    let node = face.glyph(c).unwrap_or_else(|| panic!("no glyph for {c:?}"));
    let levels = (0..node.w as usize * node.h as usize)
        .map(|i| {
            let bit = node.off as usize * 4 + i;
            (face.data[bit / 4] >> (2 * (bit % 4))) & 3
        })
        .collect();
    (node.w, node.h, levels)
}

#[test]
fn clock_digits_share_one_advance() {
    for (name, face) in clocks() {
        let advances: Vec<u8> = ('0'..='9').map(|c| face.glyph(c).expect("digit").adv).collect();
        assert!(advances.iter().all(|&a| a == advances[0]), "{name}: digit advances {advances:?}");
        assert_eq!(face.measure("11:11").advance, face.measure("88:88").advance, "{name}");
    }
}

#[test]
fn clock_faces_cover_a_clock_and_nothing_else() {
    for (name, face) in clocks() {
        assert!(face.covers("0123456789:"), "{name}");
        assert!(!face.covers("a"), "{name}");
    }
}

#[test]
fn confusables_draw_differently() {
    let confusables = ['I', 'l', '1', '|'];
    for (name, face) in ascii().into_iter().chain(latin()) {
        for (i, &a) in confusables.iter().enumerate() {
            for &b in &confusables[i + 1..] {
                assert_ne!(bitmap(face, a), bitmap(face, b), "{name}: {a:?} and {b:?} are the same pixels");
            }
        }
    }
}

#[test]
fn ascii_faces_cover_ascii() {
    let all: String = (' '..='~').collect();
    for (name, face) in ascii() {
        assert!(face.covers(&all), "{name}");
        assert_eq!(face.nodes.len(), 95, "{name}");
    }
}

#[test]
fn latin_faces_cover_the_languages_their_tier_names() {
    for (name, face) in latin() {
        for text in ["mer. 30 sept.", "Mi., 30. Sept.", "März", "śr. 30 paź", "mie., 30 sept.", "Știință", "Llun 30 Medi, Ŵŷ ẃ", "Ħamis"] {
            assert!(face.covers(text), "{name}: {text:?} lacks {:?}", face.uncovered(text).collect::<String>());
        }
    }
}

/// The font's own gaps, which make its `latin` faces smaller than Poppins': Azerbaijani's schwa,
/// Māori's and Latvian's macron o, the micro sign, Esperanto and Sami.
#[test]
fn latin_faces_lack_what_the_font_lacks() {
    for (name, face) in latin() {
        assert_eq!(face.uncovered("Əə Ōō µ ĉĝĥĵŝŭ Ŋŋ").filter(|&c| c != ' ').collect::<String>(), "ƏəŌōµĉĝĥĵŝŭŊŋ", "{name}");
        assert_eq!(face.nodes.len(), 305, "{name}");
    }
}
