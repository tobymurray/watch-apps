//! Spot-check the crate's own published constants independently.
use inscribed_disc::geometry::{is_lit, largest_square, lit_chord};
fn main() {
    for (w, h) in [(240, 240), (260, 260), (280, 280), (128, 128)] {
        let lit: i32 = (0..h).map(|y| (0..w).filter(|&x| is_lit(x, y, w, h)).count() as i32).sum();
        let widest = (0..h).map(|y| lit_chord(y, w, h)).max().unwrap();
        let odd = (0..h).filter(|&y| lit_chord(y, w, h) % 2 != 0).count();
        println!("{w}x{h}: lit={lit} ({:.1}% of buffer) widest_row={widest} odd_chords={odd} largest_square={}",
                 100.0 * lit as f64 / (w * h) as f64, largest_square(w, h));
    }
    // The bezel is what a rect clip lets you paint and the glass never shows.
    let (w, h) = (240, 240);
    let bezel = (0..h).map(|y| (0..w).filter(|&x| !is_lit(x, y, w, h)).count()).sum::<usize>();
    println!("240x240 bezel = {bezel} pixels, {:.1}% of the framebuffer", 100.0 * bezel as f64 / 57600.0);
}
