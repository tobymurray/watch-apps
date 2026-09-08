//! The scene catalogue: one table of named frames driving the preview, the
//! simulator and the tests.
//!
//! This is what replaces a visual designer on this class of panel, and it is a
//! better instrument than one. The failures here — a glyph dropping out on the
//! glass, a footer losing its ends behind the bezel, a gradient banding, holes
//! inside a swept arc — are invisible in a WYSIWYG canvas and obvious on a
//! sheet of real frames.
//!
//! # Why the table is `const`
//!
//! The catalogue this was distilled from returned a `Vec` behind
//! `#[cfg(feature = "std")]`, which meant the device could never draw its own
//! scenes. A `const` slice can, and a self-demo screen on hardware is how this
//! platform's dark-on-light dropout was found in the first place — no simulator
//! shows it.

use crate::surface::{ByteColor, Surface};

/// One named frame worth looking at.
///
/// The name is the filename in a contact sheet and the label in a failure, so
/// it should say what is special about the frame rather than number it.
pub struct Scene<S: 'static> {
    /// What makes this frame worth keeping.
    pub name: &'static str,
    /// The state to render.
    pub state: S,
}

/// An app's catalogue, and the renderer that draws it.
///
/// Implementing this is what buys an app the bezel test, the determinism test
/// and the contact sheet, without it having to declare any of them.
pub trait Scenes {
    /// The state one frame is drawn from.
    type State: 'static;
    /// The colour this app draws in.
    type Color: ByteColor;

    /// Every frame worth reviewing: the narrowest and widest each field gets,
    /// both ends of every range, and each state that has ever looked wrong.
    fn scenes() -> &'static [Scene<Self::State>];

    /// Draws one.
    fn render(surface: &mut Surface<Self::Color>, state: &Self::State);

    /// The panel these scenes are cut for.
    fn size() -> (u32, u32) {
        (240, 240)
    }

    /// What the bezel must hold, so a frame can be checked against it.
    fn ground() -> Self::Color;
}

/// FNV-1a over a frame's bytes.
///
/// A golden per scene, small enough to check in and to diff. Chosen over a
/// snapshot library because a changed hash is one line in a review and a
/// changed 57,600-byte frame is not — the contact sheet is what a human looks
/// at when the hash moves.
pub fn frame_hash(bytes: &[u8]) -> u32 {
    let mut h: u32 = 0x811C_9DC5;
    for &b in bytes {
        h = (h ^ b as u32).wrapping_mul(0x0100_0193);
    }
    h
}

/// Renders every scene and hands each to `f` as `(name, bytes)`.
///
/// The one traversal the preview binary, the simulator and the tests all use,
/// so a scene cannot be in the sheet but out of the tests.
pub fn for_each_frame<S: Scenes>(buf: &mut [u8], mut f: impl FnMut(&'static str, &[u8])) {
    let (w, h) = S::size();
    for scene in S::scenes() {
        let mut surface = match Surface::<S::Color>::round(buf, w, h) {
            Some(s) => s,
            None => return,
        };
        surface.clear(S::ground());
        S::render(&mut surface, &scene.state);
        let n = (w as usize) * (h as usize);
        f(scene.name, &buf[..n]);
    }
}

/// Every check an adopting app inherits, run over its whole catalogue.
///
/// # Panics
///
/// With the name of the offending scene, which is why this takes the catalogue
/// rather than one frame.
#[cfg(any(test, feature = "std"))]
pub fn check_every_scene<S: Scenes>() {
    let (w, h) = S::size();
    let n = (w as usize) * (h as usize);
    let ground = S::ground().to_byte();

    let mut names: std::collections::BTreeSet<&'static str> = Default::default();
    for scene in S::scenes() {
        assert!(names.insert(scene.name), "two scenes are called {:?}", scene.name);
    }
    assert!(!S::scenes().is_empty(), "a catalogue with no scenes checks nothing");

    let mut buf = vec![0u8; n + 64];
    for scene in S::scenes() {
        buf.iter_mut().for_each(|b| *b = 0xAA);
        {
            let mut s = Surface::<S::Color>::round(&mut buf, w, h).expect("buffer fits");
            s.clear(S::ground());
            S::render(&mut s, &scene.state);
        }
        assert!(
            buf[n..].iter().all(|&b| b == 0xAA),
            "{}: rendering wrote past the framebuffer",
            scene.name
        );
        for y in 0..h as i32 {
            for x in 0..w as i32 {
                if !crate::geometry::is_lit(x, y, w as i32, h as i32) {
                    assert_eq!(
                        buf[(y * w as i32 + x) as usize],
                        ground,
                        "{}: ({x},{y}) is lit behind the bezel",
                        scene.name
                    );
                }
            }
        }
    }

    // Determinism, which is weaker than it looks once a renderer holds state
    // across ticks: what it proves is that a frame is a function of the state
    // it is handed, which is what a golden hash relies on.
    let mut a = vec![0u8; n];
    let mut b = vec![0u8; n];
    for scene in S::scenes() {
        for target in [&mut a, &mut b] {
            let mut s = Surface::<S::Color>::round(target, w, h).expect("buffer fits");
            s.clear(S::ground());
            S::render(&mut s, &scene.state);
        }
        assert_eq!(a, b, "{}: two renders of one state differed", scene.name);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color::Abgr2222;

    struct Demo;

    static SCENES: &[Scene<u8>] = &[
        Scene { name: "empty", state: 0 },
        Scene { name: "half", state: 128 },
        Scene { name: "full", state: 255 },
    ];

    impl Scenes for Demo {
        type State = u8;
        type Color = Abgr2222;
        fn scenes() -> &'static [Scene<u8>] {
            SCENES
        }
        fn render(s: &mut Surface<Abgr2222>, state: &u8) {
            let w = (*state as i32 * 200) / 255;
            s.fill_rect(20, 100, w, 40, Abgr2222::WHITE);
        }
        fn ground() -> Abgr2222 {
            Abgr2222::BLACK
        }
    }

    #[test]
    fn an_adopting_app_inherits_every_check() {
        check_every_scene::<Demo>();
    }

    #[test]
    fn every_scene_is_visited_once() {
        let mut buf = vec![0u8; 240 * 240];
        let mut seen = vec![];
        for_each_frame::<Demo>(&mut buf, |name, bytes| {
            assert_eq!(bytes.len(), 240 * 240);
            seen.push(name);
        });
        assert_eq!(seen, ["empty", "half", "full"]);
    }

    #[test]
    fn different_frames_hash_differently_and_equal_ones_do_not() {
        let mut buf = vec![0u8; 240 * 240];
        let mut hashes = vec![];
        for_each_frame::<Demo>(&mut buf, |_, bytes| hashes.push(frame_hash(bytes)));
        assert_eq!(hashes.len(), 3);
        assert_ne!(hashes[0], hashes[1]);
        assert_ne!(hashes[1], hashes[2]);

        let mut again = vec![];
        for_each_frame::<Demo>(&mut buf, |_, bytes| again.push(frame_hash(bytes)));
        assert_eq!(hashes, again, "the same catalogue hashed differently twice");
    }

    /// The bezel check is belt and braces: a renderer that goes through
    /// [`Surface`] cannot violate it, because the surface clips. What it
    /// catches is a renderer that got at the bytes another way, which is how
    /// two apps in this crate's home repository lost a glyph on the glass.
    #[test]
    fn the_bezel_check_catches_a_frame_the_surface_did_not_clip() {
        let (w, h) = (240i32, 240i32);
        let n = (w * h) as usize;
        let mut buf = vec![Abgr2222::BLACK.to_byte(); n];
        buf[0] = Abgr2222::WHITE.to_byte();

        let offending = (0..h)
            .flat_map(|y| (0..w).map(move |x| (x, y)))
            .find(|&(x, y)| {
                !crate::geometry::is_lit(x, y, w, h)
                    && buf[(y * w + x) as usize] != Abgr2222::BLACK.to_byte()
            });
        assert_eq!(offending, Some((0, 0)), "the check did not find the lit corner");
    }
}
