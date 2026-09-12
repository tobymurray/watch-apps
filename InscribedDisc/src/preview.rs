//! Frames as images.
//!
//! Faithful in three things, and no image can be faithful in the rest.
//!
//! **The colours**: two bits a channel means each is one of 0, 85, 170 or 255,
//! so a frame decodes to at most 64 distinct triples. **The glass**: the mask
//! is [`crate::geometry::is_lit`], the same predicate the renderer clips with,
//! so an image cannot show a pixel the renderer was forbidden to draw. **The
//! grid**: scaling is integer nearest-neighbour, because smoothing would invent
//! shades the panel cannot produce.
//!
//! PROVEN ON THE WATCH, and not reproducible here: the panel is reflective, not
//! emissive, and dark thin strokes on light fills drop out on the glass — an
//! early black-on-white readout came back as a blank white band. **This will
//! show text the watch will not, so it is not evidence for a dark-on-light
//! claim; a device photograph is.** Falsified by a device capture that shows
//! such a readout rendering.
//!
//! Size is the third: 240 px at 0.126 mm is about 30 mm across, so a 1:1 image
//! on a 96 dpi monitor is roughly twice life size and flatters legibility.

use std::io;
use std::path::Path;

use crate::color::decode;
use crate::geometry;

/// `io::Error::other` in a form that predates it: it stabilised in 1.74, and
/// `rust-version` is package-level, so this one host-only convenience would
/// raise the floor for every embedded consumer of `geometry`.
fn other<E: Into<Box<dyn std::error::Error + Send + Sync>>>(e: E) -> io::Error {
    io::Error::new(io::ErrorKind::Other, e)
}

/// What to draw where the panel has no glass.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Bezel {
    /// Transparent, so the image cannot be mistaken for a square screen.
    #[default]
    Transparent,
    /// A flat colour, for a sheet or a page where transparency reads as a
    /// checkerboard.
    Fill([u8; 3]),
}

/// How to turn a frame into an image.
#[derive(Clone, Copy, Debug)]
pub struct Options {
    /// Integer, nearest-neighbour; 1 is one image pixel per panel pixel.
    pub scale: u32,
    /// What fills the corners the panel has no glass behind.
    pub bezel: Bezel,
}

impl Default for Options {
    fn default() -> Self {
        // 3 puts a 240-pixel panel at 720, which is large enough to see a
        // single pixel's shade without the file becoming unwieldy.
        Options { scale: 3, bezel: Bezel::Transparent }
    }
}

/// A frame as RGBA, masked to the glass and scaled.
pub fn to_rgba(frame: &[u8], w: u32, h: u32, opts: Options) -> Vec<u8> {
    let scale = opts.scale.max(1);
    let (ow, oh) = (w * scale, h * scale);
    let mut out = vec![0u8; (ow * oh * 4) as usize];

    for oy in 0..oh {
        for ox in 0..ow {
            let (x, y) = ((ox / scale) as i32, (oy / scale) as i32);
            let px = if geometry::is_lit(x, y, w as i32, h as i32) {
                let [r, g, b] = decode(frame[(y as u32 * w + x as u32) as usize]);
                [r, g, b, 255]
            } else {
                match opts.bezel {
                    Bezel::Transparent => [0, 0, 0, 0],
                    Bezel::Fill([r, g, b]) => [r, g, b, 255],
                }
            };
            let i = ((oy * ow + ox) * 4) as usize;
            out[i..i + 4].copy_from_slice(&px);
        }
    }
    out
}

/// Writes RGBA as a PNG, or RGB when every pixel is opaque.
///
/// # Errors
///
/// If the file cannot be created, or the encoder rejects the data.
pub fn write_png(path: impl AsRef<Path>, rgba: &[u8], w: u32, h: u32) -> io::Result<()> {
    let opaque = rgba.chunks_exact(4).all(|px| px[3] == 255);
    let file = std::fs::File::create(path)?;
    let mut enc = png::Encoder::new(io::BufWriter::new(file), w, h);
    enc.set_depth(png::BitDepth::Eight);
    if opaque {
        enc.set_color(png::ColorType::Rgb);
        let rgb: Vec<u8> = rgba.chunks_exact(4).flat_map(|px| [px[0], px[1], px[2]]).collect();
        enc.write_header()
            .and_then(|mut w| w.write_image_data(&rgb))
            .map_err(other)
    } else {
        enc.set_color(png::ColorType::Rgba);
        enc.write_header()
            .and_then(|mut w| w.write_image_data(rgba))
            .map_err(other)
    }
}

/// A sheet from frames already rendered, each `w * h` bytes.
///
/// # Errors
///
/// If `frames` is empty, if any frame is not `w * h` bytes, or if the file
/// cannot be written. A wrongly sized frame is refused rather than sliced.
pub fn sheet_from_frames(
    path: impl AsRef<Path>,
    frames: &[Vec<u8>],
    w: u32,
    h: u32,
    columns: u32,
    opts: Options,
) -> io::Result<(u32, u32)> {
    if frames.is_empty() {
        return Err(other("a catalogue with no scenes makes no sheet"));
    }
    let need = (w as usize) * (h as usize);
    if let Some(i) = frames.iter().position(|f| f.len() != need) {
        return Err(other(format!(
            "frame {i} is {} bytes, not the {need} its geometry needs",
            frames[i].len()
        )));
    }
    let columns = columns.max(1);
    let scale = opts.scale.max(1);
    let (cw, ch) = (w * scale, h * scale);
    // One panel pixel of gutter at this scale, so cells do not touch.
    let gutter = scale;
    // Not `div_ceil`, which stabilised in 1.73: see `other` above for why this
    // crate's floor is worth a plain idiom.
    let rows = (frames.len() as u32 + columns - 1) / columns;
    let (sw, sh) = (columns * cw + (columns + 1) * gutter, rows * ch + (rows + 1) * gutter);

    // The sheet's own ground is opaque: transparency between cells would make
    // the bezel and the gutter indistinguishable.
    let ground = match opts.bezel {
        Bezel::Fill(c) => c,
        Bezel::Transparent => [24, 24, 24],
    };
    let mut sheet = vec![0u8; (sw * sh * 4) as usize];
    for px in sheet.chunks_exact_mut(4) {
        px.copy_from_slice(&[ground[0], ground[1], ground[2], 255]);
    }

    let cell_opts = Options { scale, bezel: Bezel::Fill(ground) };
    for (i, frame) in frames.iter().enumerate() {
        let cell = to_rgba(frame, w, h, cell_opts);
        let col = i as u32 % columns;
        let row = i as u32 / columns;
        let x0 = gutter + col * (cw + gutter);
        let y0 = gutter + row * (ch + gutter);
        for y in 0..ch {
            let src = (y * cw * 4) as usize;
            let dst = (((y0 + y) * sw + x0) * 4) as usize;
            sheet[dst..dst + (cw * 4) as usize].copy_from_slice(&cell[src..src + (cw * 4) as usize]);
        }
    }

    write_png(path, &sheet, sw, sh)?;
    Ok((sw, sh))
}

/// Every colour the panel can show, as an 8×8 sheet.
///
/// # Errors
///
/// If the file cannot be written.
pub fn gamut_sheet(path: impl AsRef<Path>, cell: u32) -> io::Result<()> {
    let cell = cell.max(1);
    let (w, h) = (8 * cell, 8 * cell);
    let mut rgba = vec![255u8; (w * h * 4) as usize];
    for y in 0..h {
        for x in 0..w {
            let idx = (y / cell) * 8 + (x / cell);
            // The 64 opaque bytes: alpha is always 0b11 on this panel.
            let byte = 0b1100_0000 | idx as u8;
            let [r, g, b] = decode(byte);
            let i = ((y * w + x) * 4) as usize;
            rgba[i..i + 4].copy_from_slice(&[r, g, b, 255]);
        }
    }
    write_png(path, &rgba, w, h)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{color::Abgr2222, color::GREY_LEVELS, geometry, surface::Surface};

    const W: u32 = 240;
    const H: u32 = 240;

    /// The panel has 64 colours, so a decoded frame can hold at most 64 distinct
    /// triples and every channel must be one the glass can show.
    #[test]
    fn decoding_only_ever_produces_colours_the_panel_has() {
        for byte in 0..=255u8 {
            for ch in decode(byte) {
                assert!(GREY_LEVELS.contains(&ch), "byte {byte:#04x} decoded to {ch}");
            }
        }
        let distinct: std::collections::BTreeSet<[u8; 3]> = (0..=255u8).map(decode).collect();
        assert_eq!(distinct.len(), 64);
    }

    /// MEASURED: this panel lights 44,808 of its 57,600 pixels. A mask testing
    /// `240²` where the rule is `239²` admits 436 it does not. Re-count by
    /// summing `is_lit` over the buffer.
    #[test]
    fn nothing_outside_the_glass_is_ever_opaque() {
        let frame = vec![0xFFu8; (W * H) as usize];
        let rgba = to_rgba(&frame, W, H, Options { scale: 1, bezel: Bezel::Transparent });
        let mut lit = 0;
        for y in 0..H as i32 {
            for x in 0..W as i32 {
                let alpha = rgba[(((y as u32) * W + x as u32) * 4 + 3) as usize];
                if geometry::is_lit(x, y, W as i32, H as i32) {
                    assert_eq!(alpha, 255, "({x},{y}) is glass and was masked out");
                    lit += 1;
                } else {
                    assert_eq!(alpha, 0, "({x},{y}) is bezel and was drawn");
                }
            }
        }
        assert_eq!(lit, 44_808, "the lit-pixel count of this panel");
    }

    #[test]
    fn scaling_is_nearest_neighbour_and_invents_no_colours() {
        let mut frame = vec![0u8; (W * H) as usize];
        frame[120 * 240 + 120] = Abgr2222::WHITE.0;
        let rgba = to_rgba(&frame, W, H, Options { scale: 4, bezel: Bezel::Fill([0, 0, 0]) });
        let mut white = 0;
        for px in rgba.chunks_exact(4) {
            assert!(GREY_LEVELS.contains(&px[0]) && GREY_LEVELS.contains(&px[1]) && GREY_LEVELS.contains(&px[2]));
            if px[..3] == [255, 255, 255] {
                white += 1;
            }
        }
        assert_eq!(white, 16, "expected a 4x4 block");
    }

    #[test]
    fn a_sheet_holds_every_frame_it_was_given() {
        let dir = std::env::temp_dir().join("inscribed_disc_preview_test");
        std::fs::create_dir_all(&dir).unwrap();
        let sheet = dir.join("sheet.png");

        let mut frames = vec![];
        for width in [0i32, 100, 200] {
            let mut buf = vec![0u8; (W * H) as usize];
            {
                let mut s = Surface::<Abgr2222>::round(&mut buf, W, H).unwrap();
                s.clear(Abgr2222::BLACK);
                s.fill_rect(20, 100, width, 40, Abgr2222::WHITE);
            }
            frames.push(buf);
        }
        let (w, h) = sheet_from_frames(&sheet, &frames, W, H, 2, Options { scale: 1, bezel: Bezel::Transparent }).unwrap();
        assert_eq!((w, h), (2 * 240 + 3, 2 * 240 + 3));
        assert!(std::fs::metadata(&sheet).unwrap().len() > 0);

        // A frame that is not w*h bytes is refused rather than sliced.
        assert!(sheet_from_frames(&sheet, &[vec![0u8; 10]], W, H, 1, Options::default()).is_err());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
