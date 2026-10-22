//! Every atlas glyph TouchGFX also shipped must be the same pixels, metrics and advance; this fails
//! if `Tools/atlas.py`'s hinting, thresholds or cropping change, or FreeType's autohinter does.

#[path = "oracle/touchgfx.rs"]
mod touchgfx;

use incised_text::faces;
use incised_text::Face;
use touchgfx::Oracle;

fn levels(face: &Face, cp: u32) -> (Vec<u8>, &incised_text::Node) {
    let node = face.glyph(char::from_u32(cp).unwrap()).expect("glyph present");
    let n = node.w as usize * node.h as usize;
    let mut out = Vec::with_capacity(n);
    for i in 0..n {
        let bit = node.off as usize * 4 + i;
        out.push((face.data[bit / 4] >> (2 * (bit % 4))) & 3);
    }
    (out, node)
}

fn check(face: &Face, oracle: &[Oracle], name: &str) {
    assert!(!oracle.is_empty());
    for o in oracle {
        let (ours, node) = levels(face, o.cp);
        assert_eq!((node.w, node.h, node.top, node.left, node.adv), (o.w, o.h, o.top, o.left, o.adv), "{name} U+{:04X} metrics", o.cp);
        assert_eq!(ours, o.levels, "{name} U+{:04X} pixels", o.cp);
    }
}

#[test]
fn semibold_18_matches_touchgfx() {
    check(&faces::POPPINS_SEMIBOLD_18_ASCII, touchgfx::POPPINS_SEMIBOLD_18_ASCII, "SemiBold 18");
}

#[test]
fn regular_14_matches_touchgfx() {
    check(&faces::POPPINS_REGULAR_14_ASCII, touchgfx::POPPINS_REGULAR_14_ASCII, "Regular 14");
}

#[test]
fn semibold_20_matches_touchgfx() {
    check(&faces::POPPINS_SEMIBOLD_20_ASCII, touchgfx::POPPINS_SEMIBOLD_20_ASCII, "SemiBold 20");
}

#[test]
fn regular_18_matches_touchgfx() {
    check(&faces::POPPINS_REGULAR_18_LATIN, touchgfx::POPPINS_REGULAR_18_LATIN, "Regular 18");
}
