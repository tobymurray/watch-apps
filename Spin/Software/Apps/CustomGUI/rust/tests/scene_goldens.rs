//! Every scene still draws the frame it drew, byte for byte.
//!
//! `nothing_is_drawn_outside_the_bezel` and the layout tests say a frame is
//! *allowed*; this says it is *unchanged*. That is what a refactor needs — the
//! PanelKit conversion was held to it, and all 43 frames came out identical —
//! and it is what catches a pixel moving on a screen nobody thought to look at.
//!
//! A changed hash is not a failure to be silenced. Look at the screen
//! (`cargo run --features preview --bin preview`), decide whether the change is
//! wanted, and regenerate the file if it is.

use spin_gui::{render, scenes};

const GOLDENS: &str = include_str!("scene-goldens.txt");

fn hash(bytes: &[u8]) -> u32 {
    let mut h: u32 = 0x811C_9DC5;
    for &b in bytes {
        h = (h ^ b as u32).wrapping_mul(0x0100_0193);
    }
    h
}

#[test]
fn every_scene_draws_the_frame_it_drew() {
    let expected: Vec<(&str, &str)> = GOLDENS
        .lines()
        .filter(|l| !l.starts_with('#') && !l.trim().is_empty())
        .map(|l| {
            let (h, name) = l.split_once("  ").expect("hash then two spaces then name");
            (name, h)
        })
        .collect();

    let (w, h) = (240u32, 240u32);
    let mut buf = vec![0u8; (w * h) as usize];
    let actual = scenes::scenes();

    assert_eq!(
        actual.len(),
        expected.len(),
        "the catalogue has {} scenes and the goldens have {}; regenerate the file",
        actual.len(),
        expected.len()
    );

    let mut changed = vec![];
    for ((name, frame), (want_name, want_hash)) in actual.iter().zip(&expected) {
        assert_eq!(name, want_name, "scene order changed; regenerate the file");
        buf.iter_mut().for_each(|b| *b = 0);
        render(&mut buf, w, h, frame);
        let got = format!("{:08x}", hash(&buf));
        if got != *want_hash {
            changed.push(format!("  {name}: {want_hash} -> {got}"));
        }
    }
    assert!(changed.is_empty(), "these screens changed:\n{}", changed.join("\n"));
}
