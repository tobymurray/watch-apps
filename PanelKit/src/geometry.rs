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

// Counts calls to `lit_start`, the only unbounded thing in this module.
// Thread-local because `cargo test` runs in parallel and a shared counter picks
// up every other test's fills -- as a global it read 1,826 against a correct
// fast path. A `///` here is an `unused doc comment`, which is why it is not one.
#[cfg(test)]
thread_local! {
    pub(crate) static EDGE_SCANS: core::cell::Cell<usize> = const { core::cell::Cell::new(0) };
}

/// The leftmost lit pixel of row `y`, or `w` if the row is entirely bezel.
///
/// A linear scan, and the reason [`lit_span`] exists to avoid calling it.
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

/// The lit part of `[x0, x1)` on `row`, or `None` if none of it is glass.
///
/// A row's lit pixels are an interval, because [`is_lit`] depends on x only
/// through `(2x - (w-1))^2`, which is convex in x. So when both ends of the
/// span are lit the whole span is, and the answer costs two tests; and when the
/// span is a single pixel the first test has already settled it. Only a span
/// that genuinely straddles the rim needs [`lit_start`]'s scan.
///
/// That matters because callers plot single pixels through it — an arc sampler
/// does nothing else — so "per row" is "per pixel". MEASURED over Spin's 43
/// scenes: scanning unconditionally cost 128 us a frame against 43 for
/// byte-identical output, and 363 against 77 on the worst frame. Falsified by
/// `Spin/Software/Apps/CustomGUI/rust/examples/frametime.rs`, and by
/// `a_span_inside_the_disc_never_scans_for_the_edge` in this module.
pub fn lit_span(x0: i32, x1: i32, row: i32, w: i32, h: i32) -> Option<(i32, i32)> {
    if x0 >= x1 {
        return None;
    }
    let left = is_lit(x0, row, w, h);
    if left && is_lit(x1 - 1, row, w, h) {
        return Some((x0, x1));
    }
    if x1 - x0 == 1 {
        // One pixel, and `left` already answered for it.
        return None;
    }
    #[cfg(test)]
    EDGE_SCANS.with(|c| c.set(c.get() + 1));
    let start = lit_start(row, w, h);
    let end = w - start;
    let a = if x0 > start { x0 } else { start };
    let z = if x1 < end { x1 } else { end };
    if a < z {
        Some((a, z))
    } else {
        None
    }
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

    /// A span already inside the disc must never scan the row for its edge, and
    /// neither must a single pixel that is off it.
    ///
    /// The fault, stated as a count rather than a stopwatch: this clip once
    /// scanned every row from x = 0 to find the lit interval, and because an arc
    /// sampler plots single pixels, "per row" meant "per pixel". A timing test
    /// was tried and discarded — the disc/rect ratio is 1.86 broken against 1.42
    /// fixed in a debug build and 4.56 against 2.75 in release, so no threshold
    /// separates them in both profiles.
    #[test]
    fn a_span_inside_the_disc_never_scans_for_the_edge() {
        let arc = |r_outer: f32| {
            EDGE_SCANS.with(|c| c.set(0));
            let (cx, cy) = (119.5f32, 119.5f32);
            let mut a = 0.0f32;
            while a < core::f32::consts::PI * 2.0 {
                let (sa, ca) = (a.sin(), a.cos());
                let mut r = 100.0f32;
                while r <= r_outer {
                    let (x, y) = ((cx + r * sa) as i32, (cy - r * ca) as i32);
                    let _ = lit_span(x, x + 1, y, W, H);
                    r += 0.65;
                }
                a += 0.65 / r_outer;
            }
            EDGE_SCANS.with(|c| c.get())
        };

        assert_eq!(arc(118.0), 0, "an arc inside the disc scanned for an edge");
        // The case the first version of this fix missed: an arc that runs off
        // the glass. Every one of those pixels failed both tests and paid a full
        // scan to be told it was dark.
        assert_eq!(arc(130.0), 0, "an arc crossing the rim scanned for an edge");

        // The instrument, proven: a wide span that really does straddle the rim
        // must reach the scan, or the counts above measure nothing.
        EDGE_SCANS.with(|c| c.set(0));
        for row in 0..H {
            let _ = lit_span(0, W, row, W, H);
        }
        assert!(EDGE_SCANS.with(|c| c.get()) > 0, "a full-width span never scanned");
    }

    #[test]
    fn a_lit_span_matches_a_per_pixel_reference() {
        for w in [1, 2, 3, 7, 16, 33, 64, 240] {
            for h in [1, 2, 5, 17, 240] {
                for row in -1..=h {
                    for x0 in -2..w + 2 {
                        for x1 in x0..=(x0 + 5).min(w + 2) {
                            let got = lit_span(x0.max(0), x1.min(w), row, w, h);
                            let want: Vec<i32> = (x0.max(0)..x1.min(w))
                                .filter(|&x| is_lit(x, row, w, h))
                                .collect();
                            match (got, want.is_empty()) {
                                (None, true) => {}
                                (Some((a, z)), false) => {
                                    assert_eq!((a, z), (want[0], want[want.len() - 1] + 1),
                                        "w{w} h{h} row{row} [{x0},{x1})");
                                }
                                _ => panic!("w{w} h{h} row{row} [{x0},{x1}): got {got:?}, want {want:?}"),
                            }
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn chords_are_symmetric_about_the_centre() {
        for y in 0..H {
            assert_eq!(lit_chord(y, W, H), lit_chord(H - 1 - y, W, H), "row {y}");
        }
    }
}
