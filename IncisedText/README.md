# incised-text

Anti-aliased text for [`embedded-graphics`](https://crates.io/crates/embedded-graphics)
draw targets, `no_std` and allocation-free. Fonts are rendered at build time into
2-bit (four-level) glyph atlases; at run time the crate measures, aligns, wraps and
draws them.

```rust
use incised_text::{faces, Style};
use inscribed_disc::{Abgr2222, Surface};

let mut frame = vec![0u8; 240 * 240];
let mut screen = Surface::<Abgr2222>::round(&mut frame, 240, 240).unwrap();
let style = Style::centered(Abgr2222::WHITE, Abgr2222::BLACK);
let drawn = faces::POPPINS_SEMIBOLD_20_ASCII.draw(&mut screen, "16:27", 120, 120, style).unwrap();
assert_eq!(drawn.missing, 0);
```

Any `DrawTarget` works whose colour implements `Ink`; the default `abgr2222` feature
implements it for [`inscribed-disc`](https://crates.io/crates/inscribed-disc)'s
`Abgr2222`.

## Faces

Each face is one font at one pixel size over one character set, in `faces`:

| Faces | Font | Sizes (px) | Characters |
|---|---|---|---|
| `POPPINS_REGULAR_{12,14}_ASCII` | Poppins Regular | 12, 14 | ASCII |
| `POPPINS_REGULAR_{16,18}_LATIN` | Poppins Regular | 16, 18 | ASCII, Latin-1, Latin Extended-A |
| `POPPINS_SEMIBOLD_{18,20}_ASCII` | Poppins SemiBold | 18, 20 | ASCII |
| `POPPINS_SEMIBOLD_{27,36,49,60}_CLOCK` | Poppins SemiBold | 27, 36, 49, 60 | `0123456789:` |
| `ATKINSON_BOLD_{16,20,25}_ASCII` | Atkinson Hyperlegible Next Bold | 16, 20, 25 | ASCII |
| `ATKINSON_BOLD_{49,62}_CLOCK` | Atkinson Hyperlegible Next Bold | 49, 62 | `0123456789:`, tabular |

The `ATKINSON_*` faces need the `atkinson` feature. Atkinson Hyperlegible Next draws
`I`, `l`, `1` and `|` differently, and its zero is always slashed.

A character a face lacks draws as a hollow box and is counted in `Measure::missing`.
Faces of your own come from `Tools/atlas.py` in the
[repository](https://github.com/tobymurray/watch-apps/tree/main/IncisedText).

The design, the format and the measurements behind it are in
[`Docs/DESIGN.md`](https://github.com/tobymurray/watch-apps/blob/main/IncisedText/Docs/DESIGN.md).

## Licence

The code is MIT or Apache-2.0, at your option. The glyph data in `src/faces.rs` is
rendered from Poppins and Atkinson Hyperlegible Next and is under the SIL Open Font
License 1.1; `Fonts/FONTS.md` names the sources and `Fonts/OFL-*.txt` are the texts.
