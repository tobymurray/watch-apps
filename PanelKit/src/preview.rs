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

use crate::geometry;
use crate::scene::Scenes;
use crate::surface::Surface;

/// Each 2-bit channel is one of these.
const LEVELS: [u8; 4] = [0, 85, 170, 255];

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

/// One `ABGR2222` byte to the RGB the panel actually shows.
#[inline]
pub const fn decode(byte: u8) -> [u8; 3] {
    [
        LEVELS[(byte & 0b11) as usize],
        LEVELS[((byte >> 2) & 0b11) as usize],
        LEVELS[((byte >> 4) & 0b11) as usize],
    ]
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
            .map_err(io::Error::other)
    } else {
        enc.set_color(png::ColorType::Rgba);
        enc.write_header()
            .and_then(|mut w| w.write_image_data(rgba))
            .map_err(io::Error::other)
    }
}

/// Renders one frame straight to a PNG.
pub fn write_frame<S: Scenes>(path: impl AsRef<Path>, state: &S::State, opts: Options) -> io::Result<()> {
    let (w, h) = S::size();
    let mut buf = vec![0u8; (w * h) as usize];
    {
        let mut surface = Surface::<S::Color>::round(&mut buf, w, h)
            .ok_or_else(|| io::Error::other("the frame buffer does not fit its geometry"))?;
        surface.clear(S::ground());
        S::render(&mut surface, state);
    }
    let rgba = to_rgba(&buf, w, h, opts);
    write_png(path, &rgba, w * opts.scale.max(1), h * opts.scale.max(1))
}

/// One PNG per scene, named for the scene.
pub fn write_scenes<S: Scenes>(dir: impl AsRef<Path>, opts: Options) -> io::Result<Vec<String>> {
    let dir = dir.as_ref();
    std::fs::create_dir_all(dir)?;
    let (w, h) = S::size();
    let scale = opts.scale.max(1);
    let mut written = vec![];
    let mut buf = vec![0u8; (w * h) as usize];

    for scene in S::scenes() {
        {
            let mut surface = Surface::<S::Color>::round(&mut buf, w, h)
                .ok_or_else(|| io::Error::other("the frame buffer does not fit its geometry"))?;
            surface.clear(S::ground());
            S::render(&mut surface, &scene.state);
        }
        let rgba = to_rgba(&buf, w, h, opts);
        let path = dir.join(format!("{}.png", scene.name));
        write_png(&path, &rgba, w * scale, h * scale)?;
        written.push(path.display().to_string());
    }
    Ok(written)
}

/// Every scene on one sheet, in catalogue order, `columns` wide.
///
/// Names are not drawn; this crate has no font.
pub fn contact_sheet<S: Scenes>(
    path: impl AsRef<Path>,
    columns: u32,
    opts: Options,
) -> io::Result<(u32, u32)> {
    let (w, h) = S::size();
    let mut frames = Vec::with_capacity(S::scenes().len());
    let mut buf = vec![0u8; (w * h) as usize];
    for scene in S::scenes() {
        {
            let mut surface = Surface::<S::Color>::round(&mut buf, w, h)
                .ok_or_else(|| io::Error::other("the frame buffer does not fit its geometry"))?;
            surface.clear(S::ground());
            S::render(&mut surface, &scene.state);
        }
        frames.push(buf.clone());
    }
    sheet_from_frames(path, &frames, w, h, columns, opts)
}

/// A sheet from frames already rendered, each `w * h` bytes.
pub fn sheet_from_frames(
    path: impl AsRef<Path>,
    frames: &[Vec<u8>],
    w: u32,
    h: u32,
    columns: u32,
    opts: Options,
) -> io::Result<(u32, u32)> {
    if frames.is_empty() {
        return Err(io::Error::other("a catalogue with no scenes makes no sheet"));
    }
    let need = (w as usize) * (h as usize);
    if let Some(i) = frames.iter().position(|f| f.len() != need) {
        return Err(io::Error::other(format!(
            "frame {i} is {} bytes, not the {need} its geometry needs",
            frames[i].len()
        )));
    }
    let columns = columns.max(1);
    let scale = opts.scale.max(1);
    let (cw, ch) = (w * scale, h * scale);
    // One panel pixel of gutter at this scale, so cells do not touch.
    let gutter = scale;
    let rows = (frames.len() as u32).div_ceil(columns);
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
    use crate::color::Abgr2222;
    use crate::scene::Scene;

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
            s.fill_rect(20, 100, (*state as i32 * 200) / 255, 40, Abgr2222::WHITE);
        }
        fn ground() -> Abgr2222 {
            Abgr2222::BLACK
        }
    }

    /// The panel has 64 colours, so a decoded frame can contain at most 64
    /// distinct triples, and every channel must be one the glass can show.
    #[test]
    fn decoding_only_ever_produces_colours_the_panel_has() {
        for byte in 0..=255u8 {
            for ch in decode(byte) {
                assert!(LEVELS.contains(&ch), "byte {byte:#04x} decoded to {ch}");
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
        let (w, h) = (240u32, 240u32);
        let frame = vec![0xFFu8; (w * h) as usize];
        let rgba = to_rgba(&frame, w, h, Options { scale: 1, bezel: Bezel::Transparent });
        let mut lit = 0;
        for y in 0..h as i32 {
            for x in 0..w as i32 {
                let alpha = rgba[(((y as u32) * w + x as u32) * 4 + 3) as usize];
                if geometry::is_lit(x, y, w as i32, h as i32) {
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
        let (w, h) = (240u32, 240u32);
        let mut frame = vec![0u8; (w * h) as usize];
        frame[120 * 240 + 120] = Abgr2222::WHITE.0;
        let rgba = to_rgba(&frame, w, h, Options { scale: 4, bezel: Bezel::Fill([0, 0, 0]) });
        // The one white pixel became exactly a 4x4 block, with no soft edge.
        let ow = w * 4;
        let mut white = 0;
        for px in rgba.chunks_exact(4) {
            assert!(LEVELS.contains(&px[0]) && LEVELS.contains(&px[1]) && LEVELS.contains(&px[2]));
            if px[..3] == [255, 255, 255] {
                white += 1;
            }
        }
        assert_eq!(white, 16, "expected a 4x4 block");
        assert_eq!(&rgba[((480 * ow + 480) * 4) as usize..][..3], &[255, 255, 255]);
    }

    #[test]
    fn a_sheet_holds_every_scene() {
        let dir = std::env::temp_dir().join("panelkit_preview_test");
        let sheet = dir.join("sheet.png");
        std::fs::create_dir_all(&dir).unwrap();
        let (w, h) = contact_sheet::<Demo>(&sheet, 2, Options { scale: 1, bezel: Bezel::Transparent }).unwrap();
        // Three scenes at two columns is two rows, with a one-pixel gutter.
        assert_eq!((w, h), (2 * 240 + 3, 2 * 240 + 3));
        assert!(std::fs::metadata(&sheet).unwrap().len() > 0);

        let files = write_scenes::<Demo>(&dir, Options { scale: 1, bezel: Bezel::Transparent }).unwrap();
        assert_eq!(files.len(), 3);
        assert!(files[0].ends_with("empty.png"));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
