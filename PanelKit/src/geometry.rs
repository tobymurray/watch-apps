//! Where the glass is.
//!
//! On a round panel the framebuffer is square and the display is not, so a box
//! inset from the buffer's edge is not inset from the glass. Two shipped apps
//! in this crate's home repository lost their last glyph that way, and no
//! simulator showed it, because a simulator draws the square.

/// Whether the panel would show this pixel at all.
///
/// A pixel centre within the inscribed disc, doubled so the half-pitch is
/// exact in integers: `BarcodeLayout::pixelIsLit`'s rule and
/// `TextKit::Canvas::is_lit`'s, which agree.
#[inline]
pub const fn is_lit(x: i32, y: i32, w: i32, h: i32) -> bool {
    if x < 0 || y < 0 || x >= w || y >= h {
        return false;
    }
    let dx = 2 * x - (w - 1);
    let dy = 2 * y - (h - 1);
    let d = if w < h { w - 1 } else { h - 1 };
    dx * dx + dy * dy <= d * d
}

/// How many pixels of row `y` are glass, on a `w` by `h` panel.
///
/// The number a layout needs and the one that is easy to get wrong: the widest
/// row of a 240-pixel panel is 238, not 240, and row 222 holds 122 where row
/// 220 holds 129.
pub const fn lit_chord(y: i32, w: i32, h: i32) -> i32 {
    if y < 0 || y >= h {
        return 0;
    }
    let mut x = 0;
    while x < w {
        if is_lit(x, y, w, h) {
            let mut x1 = w - 1;
            while x1 > x {
                if is_lit(x1, y, w, h) {
                    return x1 - x + 1;
                }
                x1 -= 1;
            }
            return 1;
        }
        x += 1;
    }
    0
}

/// The leftmost lit pixel of row `y`, or `w` if the row is entirely bezel.
pub const fn lit_start(y: i32, w: i32, h: i32) -> i32 {
    let mut x = 0;
    while x < w {
        if is_lit(x, y, w, h) {
            return x;
        }
        x += 1;
    }
    w
}

/// Whether every pixel of this box is glass.
///
/// The predicate a layout constant should be held to, so the bezel trap is a
/// failing test rather than a photograph.
pub const fn box_is_lit(x: i32, y: i32, w: i32, h: i32, panel_w: i32, panel_h: i32) -> bool {
    if w <= 0 || h <= 0 {
        return false;
    }
    // The corners bound it: the disc is convex, so a box whose four corners are
    // inside it is inside it.
    is_lit(x, y, panel_w, panel_h)
        && is_lit(x + w - 1, y, panel_w, panel_h)
        && is_lit(x, y + h - 1, panel_w, panel_h)
        && is_lit(x + w - 1, y + h - 1, panel_w, panel_h)
}

/// The side of the largest centred axis-aligned square wholly on the glass.
///
/// Searched against [`box_is_lit`] rather than computed as `floor(d / sqrt 2)`,
/// because the square is centred on integer pixels and the closed form is one
/// too large for a 240-pixel panel: side 169 puts its corner at 169² + 169² =
/// 57,122 against the disc's 239² = 57,121, and misses by one.
pub const fn inscribed_square(w: i32, h: i32) -> i32 {
    let limit = if w < h { w } else { h };
    let mut best = 0;
    let mut s = 1;
    while s <= limit {
        if box_is_lit((w - s) / 2, (h - s) / 2, s, s, w, h) {
            best = s;
        }
        s += 1;
    }
    best
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: i32 = 240;
    const H: i32 = 240;

    /// MEASURED, and the number every layout in this repository is cut against:
    /// the widest row of this panel is 238 pixels, not 240. Falsified by a
    /// panel of a different size or a display whose glass is not the inscribed
    /// disc.
    #[test]
    fn widest_row_is_238() {
        let widest = (0..H).map(|y| lit_chord(y, W, H)).max().unwrap();
        assert_eq!(widest, 238);
    }

    /// MEASURED: the footer row 220 holds 130 pixels of glass and row 222 holds
    /// 122, which is why a 117-pixel footer sits at 220 and not two rows lower.
    /// Falsified by a panel of a different size or a different lit rule.
    #[test]
    fn footer_rows_hold_their_measured_chords() {
        assert_eq!(lit_chord(220, W, H), 130);
        assert_eq!(lit_chord(222, W, H), 122);
    }

    /// Every chord is even, because the disc is symmetric about x = 119.5 on an
    /// even-width panel: a lit pixel at `x` implies one at `w - 1 - x`. An odd
    /// chord quoted for this panel is arithmetically impossible, whatever
    /// measured it. Falsified by an odd-width panel, where it should not hold.
    #[test]
    fn every_chord_is_even() {
        for y in 0..H {
            assert_eq!(lit_chord(y, W, H) % 2, 0, "row {y}");
        }
    }

    /// The rows a full-width clock can use: 218 pixels at rows 70 and 168.
    #[test]
    fn clock_rows_hold_218() {
        assert_eq!(lit_chord(70, W, H), 218);
        assert_eq!(lit_chord(168, W, H), 218);
    }

    #[test]
    fn inscribed_square_is_168() {
        assert_eq!(inscribed_square(W, H), 168);
    }

    #[test]
    fn corners_are_bezel_and_the_centre_is_glass() {
        assert!(!is_lit(0, 0, W, H));
        assert!(!is_lit(239, 239, W, H));
        assert!(is_lit(120, 120, W, H));
    }

    #[test]
    fn a_box_inset_from_the_buffer_is_not_inset_from_the_glass() {
        // The mistake, stated as a test: 8 px in from every edge of the buffer
        // still has its corners behind the bezel.
        assert!(!box_is_lit(8, 8, W - 16, H - 16, W, H));
        // The inscribed square, centred, is not.
        let s = inscribed_square(W, H);
        assert!(box_is_lit((W - s) / 2, (H - s) / 2, s, s, W, H));
    }

    #[test]
    fn chords_are_symmetric_about_the_centre() {
        for y in 0..H {
            assert_eq!(lit_chord(y, W, H), lit_chord(H - 1 - y, W, H), "row {y}");
        }
    }
}
