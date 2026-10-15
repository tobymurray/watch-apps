//! Checking a finished frame against the glass, for tests and host tools.
//!
//! The clip in [`Surface`](crate::surface::Surface) guarantees nothing is drawn
//! off the glass. It cannot say whether anything was *meant* to be there: a
//! line of text that runs under the rim is clipped silently and the result
//! looks fine on a square simulator. This module answers that question after
//! the fact, from the bytes alone, so a layout can be proven on the host
//! before an image is installed.

use crate::geometry::is_lit;

/// Every pixel of `frame` that is not `ground` and that the glass, inset by
/// `inset` pixels all round, would not show.
///
/// Two ways to use it. Render with
/// [`Surface::rect`](crate::surface::Surface::rect), where nothing is clipped,
/// and an empty result at `inset` 0 says the scene is wholly on the glass.
/// Or render with the disc clip and a positive `inset`, and an empty result
/// says every drawn pixel keeps that margin from the rim.
///
/// `frame` is one byte a pixel, row-major, `w` by `h`; the result is in
/// row-major order. Anything beyond `w * h` bytes is ignored.
pub fn off_glass(
    frame: &[u8],
    w: i32,
    h: i32,
    inset: i32,
    ground: u8,
) -> impl Iterator<Item = (i32, i32)> + '_ {
    let len = if w > 0 && h > 0 { (w * h) as usize } else { 0 };
    frame
        .iter()
        .take(len)
        .enumerate()
        .filter(move |(_, &b)| b != ground)
        .map(move |(i, _)| ((i as i32) % w, (i as i32) / w))
        .filter(move |&(x, y)| !is_lit(x - inset, y - inset, w - 2 * inset, h - 2 * inset))
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: i32 = 240;
    const H: i32 = 240;

    #[test]
    fn a_corner_pixel_is_off_the_glass_and_the_centre_is_not() {
        let mut frame = vec![0u8; (W * H) as usize];
        frame[0] = 1;
        frame[(119 * W + 119) as usize] = 1;
        let off: Vec<_> = off_glass(&frame, W, H, 0, 0).collect();
        assert_eq!(off, vec![(0, 0)]);
    }

    #[test]
    fn an_inset_moves_the_rim_inwards() {
        // Row 3, centre column: on the glass, but not on a glass inset by 5.
        let mut frame = vec![0u8; (W * H) as usize];
        frame[(3 * W + 119) as usize] = 1;
        assert_eq!(off_glass(&frame, W, H, 0, 0).count(), 0);
        assert_eq!(off_glass(&frame, W, H, 5, 0).count(), 1);
    }

    #[test]
    fn the_ground_colour_is_never_reported() {
        let frame = vec![7u8; (W * H) as usize];
        assert_eq!(off_glass(&frame, W, H, 0, 7).count(), 0);
        // And a different ground reports every corner pixel: the complement
        // of a 240-disc in its square, MEASURED as 12,792 pixels, 22.2%, the
        // figure the README quotes for a 4-pixel inset on a square target.
        assert_eq!(off_glass(&frame, W, H, 0, 0).count(), 12_792);
    }

    #[test]
    fn a_short_frame_is_read_as_far_as_it_goes() {
        let frame = vec![1u8; 10];
        assert_eq!(off_glass(&frame, W, H, 0, 0).count(), 10);
    }
}
