//! Where the glass is.
//!
//! On a round panel the framebuffer is square and the display is not, so a box
//! inset from the buffer's edge is not inset from the glass, and no simulator
//! shows the difference.

/// Whether the panel would show this pixel at all: a pixel centre within the
/// inscribed disc, doubled so the half-pitch is exact in integers.
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

// Counts calls to `lit_start`. Thread-local because `cargo test` runs in
// parallel and a shared counter picks up every other test's fills -- as a global
// it read 1,826 against a correct fast path. A `///` here is an `unused doc
// comment`, which is why it is not one.
#[cfg(test)]
thread_local! {
    pub(crate) static EDGE_SCANS: core::cell::Cell<usize> = const { core::cell::Cell::new(0) };
}

/// Integer square root, for [`lit_start`]'s half-chord.
const fn isqrt(v: i32) -> i32 {
    if v <= 0 {
        return 0;
    }
    let mut x = v;
    let mut y = (x + 1) / 2;
    while y < x {
        x = y;
        y = (x + v / x) / 2;
    }
    x
}

/// The leftmost lit pixel of row `y`, or `w` if the row is entirely bezel.
///
/// Closed form: the lit x are those with `(2x - (w-1))^2 <= d^2 - dy^2`, so the
/// half-chord is one integer square root. MEASURED: the O(width) scan this
/// replaces cost 128 us a frame against 43 for byte-identical output, because
/// it ran once per row of every clipped fill and callers plot single pixels.
/// `lit_start_agrees_with_a_scan` checks the two agree.
pub fn lit_start(y: i32, w: i32, h: i32) -> i32 {
    #[cfg(test)]
    EDGE_SCANS.with(|c| c.set(c.get() + 1));
    if y < 0 || y >= h {
        return w;
    }
    let dy = 2 * y - (h - 1);
    let d = if w < h { w - 1 } else { h - 1 };
    let r2 = d * d - dy * dy;
    if r2 < 0 {
        return w;
    }
    let r = isqrt(r2);
    let num = (w - 1) - r;
    // Ceiling division; `num` can be negative on a wide short panel.
    let mut x = if num >= 0 { (num + 1) / 2 } else { -((-num) / 2) };
    if x < 0 {
        x = 0;
    }
    if x >= w || !is_lit(x, y, w, h) {
        return w;
    }
    x
}

/// The lit part of `[x0, x1)` on `row`, or `None` if none of it is glass.
///
/// A row's lit pixels are an interval, because [`is_lit`] depends on x only
/// through `(2x - (w-1))^2`, which is convex in x.
pub(crate) fn lit_span(x0: i32, x1: i32, row: i32, w: i32, h: i32) -> Option<(i32, i32)> {
    if x0 >= x1 {
        return None;
    }
    let left = is_lit(x0, row, w, h);

    // A single pixel first, answered from `left` alone: callers plot far more
    // single pixels than anything else, and asking about the other end would
    // spend a second `is_lit` on the pixel just tested.
    if x1 - x0 == 1 {
        return if left { Some((x0, x1)) } else { None };
    }
    if left && is_lit(x1 - 1, row, w, h) {
        return Some((x0, x1));
    }

    // Only a span straddling the rim reaches the arithmetic below. It is O(1),
    // but its integer square root is dearer than the two tests above: MEASURED,
    // going straight to it costs 160 us a frame against 55 with these
    // early-outs, and the scan it replaced cost 128. Re-run
    // `Spin/Software/Apps/CustomGUI/rust/examples/frametime.rs`.
    let start = lit_start(row, w, h);
    if start >= w {
        return None;
    }
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
/// Searched rather than `floor(d / sqrt 2)`: MEASURED, on a 240-pixel panel the
/// closed form gives 169, whose corner is at 169² + 169² = 57,122 against the
/// disc's 239² = 57,121. The answer is 168.
pub const fn largest_square(w: i32, h: i32) -> i32 {
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

    /// MEASURED: the widest row of a 240-pixel panel is 238, not 240.
    #[test]
    fn widest_row_is_238() {
        let widest = (0..H).map(|y| lit_chord(y, W, H)).max().unwrap();
        assert_eq!(widest, 238);
    }

    /// MEASURED: row 220 holds 130 pixels of glass and row 222 holds 122, so a
    /// footer wider than 122 cannot sit two rows lower than 220.
    #[test]
    fn footer_rows_hold_their_measured_chords() {
        assert_eq!(lit_chord(220, W, H), 130);
        assert_eq!(lit_chord(222, W, H), 122);
    }

    /// Every chord is even on an even-width panel, because the disc is
    /// symmetric about x = 119.5: a lit pixel at `x` implies one at `w-1-x`. An
    /// odd chord quoted for this panel is arithmetically impossible.
    #[test]
    fn every_chord_is_even() {
        for y in 0..H {
            assert_eq!(lit_chord(y, W, H) % 2, 0, "row {y}");
        }
    }

    /// MEASURED: rows 70 and 168 hold 218 pixels.
    #[test]
    fn clock_rows_hold_218() {
        assert_eq!(lit_chord(70, W, H), 218);
        assert_eq!(lit_chord(168, W, H), 218);
    }

    #[test]
    fn largest_square_is_168() {
        assert_eq!(largest_square(W, H), 168);
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
        let s = largest_square(W, H);
        assert!(box_is_lit((W - s) / 2, (H - s) / 2, s, s, W, H));
    }

    /// The closed form must agree with the scan it replaced, everywhere.
    #[test]
    fn lit_start_agrees_with_a_scan() {
        fn by_scan(y: i32, w: i32, h: i32) -> i32 {
            let mut x = 0;
            while x < w {
                if is_lit(x, y, w, h) {
                    return x;
                }
                x += 1;
            }
            w
        }
        let sizes = [1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 63, 64, 119, 120, 239, 240, 241, 320];
        for &w in &sizes {
            for &h in &sizes {
                for y in -2..h + 2 {
                    assert_eq!(lit_start(y, w, h), by_scan(y, w, h), "w{w} h{h} row{y}");
                }
            }
        }
    }

    /// The early-outs must fire for the shapes callers draw, and the arithmetic
    /// must still be reachable.
    ///
    /// Counts rather than times: MEASURED, the disc/rect ratio is 1.86 with the
    /// fault and 1.42 without it in a debug build, and 4.56 against 2.75 in
    /// release, so no single threshold separates them in both profiles.
    #[test]
    fn the_shapes_callers_draw_never_reach_the_arithmetic() {
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
        assert_eq!(arc(118.0), 0, "an arc inside the disc reached the arithmetic");
        // The case the first fix missed: an arc running off the glass.
        assert_eq!(arc(130.0), 0, "an arc crossing the rim reached the arithmetic");

        // A wide span wholly inside the disc -- the other half, and the half an
        // arc never exercises because an arc plots single pixels. Without this,
        // deleting the both-ends test leaves the suite green.
        EDGE_SCANS.with(|c| c.set(0));
        for row in 40..200 {
            assert_eq!(lit_span(40, 200, row, W, H), Some((40, 200)));
        }
        assert_eq!(EDGE_SCANS.with(|c| c.get()), 0, "a wide interior span reached the arithmetic");

        // And the arithmetic must still be reachable, or the counts above
        // measure nothing.
        EDGE_SCANS.with(|c| c.set(0));
        for row in 0..H {
            let _ = lit_span(0, W, row, W, H);
        }
        assert!(EDGE_SCANS.with(|c| c.get()) > 0, "a full-width span never reached it");
    }

    #[test]
    fn a_lit_span_matches_a_per_pixel_reference() {
        for w in [1, 2, 3, 7, 16, 33, 64, 240] {
            for h in [1, 2, 5, 17, 240] {
                for row in -1..=h {
                    for x0 in -2..w + 2 {
                        for x1 in x0..=(x0 + 5).min(w + 2) {
                            // Unclamped: `lit_span` accepts these, so they are
                            // part of its contract even though `fill_rect`
                            // happens never to pass them.
                            let got = lit_span(x0, x1, row, w, h);
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
