//! Text, as a trait rather than a dependency.
//!
//! This crate does not rasterise and ships no font. A kit that depended on one
//! text implementation would ship its atlases, inherit its licence question and
//! force every adopter onto its typeface. What the widgets need is four
//! questions — how wide, how tall, draw it, and did anything not have a glyph —
//! and any of `TextKit`, `u8g2-fonts` or an `embedded-graphics` `MonoFont` can
//! answer them.

use crate::surface::{ByteColor, Surface};

/// Where a string sits relative to the x it is given.
///
/// The same three `embedded-layout`, `embedded-text` and `TextKit` all use;
/// coining a fourth set of names would help nobody.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Align {
    /// `x` is the left edge.
    #[default]
    Left,
    /// `x` is the centre.
    Center,
    /// `x` is the right edge.
    Right,
}

/// What a face knows about a string before it draws it.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct Measure {
    /// Sum of advances: what alignment and wrapping use.
    pub width: i32,
    /// Pixels the glyphs actually cover, which is narrower than `width`.
    pub ink_width: i32,
    /// Characters that had no glyph in this face.
    ///
    /// Non-zero is a decision the caller has to make, not a detail: `Barcode`'s
    /// rule is to refuse rather than draw a code it cannot promise, and a
    /// caller that cannot refuse should at least know.
    pub missing: u16,
}

/// Enough of a font for this crate's widgets to lay out and draw a line.
///
/// Deliberately smaller than any real text library's API: a widget needs to
/// place a string, not to shape one.
pub trait TextFace<C: ByteColor> {
    /// Baseline-to-baseline distance for stacked lines.
    fn line_height(&self) -> i32;

    /// Pixels above the baseline this face may use.
    fn ascent(&self) -> i32;

    /// Pixels below the baseline this face may use.
    fn descent(&self) -> i32;

    /// How wide `text` is, without drawing it.
    fn measure(&self, text: &str) -> Measure;

    /// Draws `text` with its baseline at `y`, aligned about `x`.
    fn draw(&self, surface: &mut Surface<C>, text: &str, x: i32, baseline: i32, align: Align, ink: C) -> Measure;

    /// Draws with the top of the face at `y` rather than the baseline.
    fn draw_top(&self, surface: &mut Surface<C>, text: &str, x: i32, top: i32, align: Align, ink: C) -> Measure {
        self.draw(surface, text, x, top + self.ascent(), align, ink)
    }
}

/// The first face in `faces` whose advance for `text` fits `max_width`.
///
/// The ladder a value row climbs down when the number grows a digit. Returns
/// `None` when even the last does not fit, so a caller can shorten the string
/// rather than have it silently overflow.
pub fn pick<'f, C: ByteColor, F: TextFace<C>>(faces: &'f [&'f F], text: &str, max_width: i32) -> Option<&'f F> {
    faces.iter().copied().find(|f| f.measure(text).width <= max_width)
}

/// Greedy word wrap into caller-owned line slots.
///
/// Returns how many lines the text *needed*, which may exceed `lines.len()`, so
/// overflow is a number the caller sees rather than a line that vanished. The
/// slots are filled up to their capacity either way.
pub fn wrap<'t, C: ByteColor, F: TextFace<C>>(
    face: &F,
    text: &'t str,
    max_width: i32,
    lines: &mut [&'t str],
) -> usize {
    let mut needed = 0;
    let mut rest = text.trim_start();

    while !rest.is_empty() {
        // The longest prefix ending at a word boundary that still fits.
        let mut end = rest.len();
        let mut fits = false;
        loop {
            let candidate = rest[..end].trim_end();
            if candidate.is_empty() {
                break;
            }
            if face.measure(candidate).width <= max_width {
                fits = true;
                break;
            }
            match rest[..end].rfind(char::is_whitespace) {
                Some(sp) => end = sp,
                // A single word wider than the line: emit it whole rather than
                // splitting it, and let the caller see it overflow.
                None => break,
            }
        }
        if !fits && end == rest.len() {
            // Nothing fitted; take the first word so the loop terminates.
            end = rest.find(char::is_whitespace).unwrap_or(rest.len());
        }
        let line = rest[..end].trim_end();
        if needed < lines.len() {
            lines[needed] = line;
        }
        needed += 1;
        rest = rest[end..].trim_start();
    }
    needed
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::color::Abgr2222;

    /// A face where every character is `w` wide, so wrapping can be checked
    /// without a font.
    struct Fixed {
        w: i32,
    }

    impl TextFace<Abgr2222> for Fixed {
        fn line_height(&self) -> i32 {
            self.w * 2
        }
        fn ascent(&self) -> i32 {
            self.w
        }
        fn descent(&self) -> i32 {
            self.w / 2
        }
        fn measure(&self, text: &str) -> Measure {
            Measure { width: text.chars().count() as i32 * self.w, ink_width: text.chars().count() as i32 * self.w, missing: 0 }
        }
        fn draw(&self, _s: &mut Surface<Abgr2222>, text: &str, _x: i32, _b: i32, _a: Align, _i: Abgr2222) -> Measure {
            self.measure(text)
        }
    }

    #[test]
    fn wrap_reports_more_lines_than_it_was_given_room_for() {
        let f = Fixed { w: 10 };
        let mut lines = ["" ; 2];
        let needed = wrap(&f, "one two three four five six", 40, &mut lines);
        assert!(needed > 2, "needed {needed}");
        assert_eq!(lines[0], "one");
    }

    #[test]
    fn wrap_never_splits_a_word() {
        let f = Fixed { w: 10 };
        let mut lines = [""; 8];
        let n = wrap(&f, "alpha bravo charlie", 60, &mut lines);
        for line in lines.iter().take(n) {
            assert!(!line.is_empty());
        }
        assert_eq!(lines[..n].join(" "), "alpha bravo charlie");
    }

    #[test]
    fn a_word_wider_than_the_line_is_emitted_whole() {
        let f = Fixed { w: 10 };
        let mut lines = [""; 4];
        let n = wrap(&f, "supercalifragilistic", 30, &mut lines);
        assert_eq!(n, 1);
        assert_eq!(lines[0], "supercalifragilistic");
    }

    #[test]
    fn wrap_terminates_on_pathological_input() {
        let f = Fixed { w: 10 };
        let mut lines = [""; 4];
        let n = wrap(&f, "   a    bb   ccc   ", 10, &mut lines);
        assert_eq!(n, 3);
    }

    #[test]
    fn pick_climbs_down_the_ladder() {
        let big = Fixed { w: 20 };
        let small = Fixed { w: 8 };
        let faces: [&Fixed; 2] = [&big, &small];
        assert_eq!(pick(&faces, "1234", 100).map(|f| f.w), Some(20));
        assert_eq!(pick(&faces, "1234", 50).map(|f| f.w), Some(8));
        assert_eq!(pick(&faces, "1234", 10).map(|f| f.w), None);
    }
}
