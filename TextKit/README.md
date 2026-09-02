# TextKit

Text for the UNA Watch's Rust GUIs: Poppins pre-rendered at build time into
2bpp glyph atlases, blitted onto the `ABGR2222` framebuffer through the lit
disc. `no_std`, no allocator, no scratch buffer, about 1.5 KB of code. Each
app's `CustomGUI` crate takes it as a Cargo path dependency; it owns no screen,
no frame struct and no C ABI.

Why this shape and not the others was decided by measurement in
[`Docs/TEXT.md`](../Docs/TEXT.md) at the repo root, with the scripts and
contact sheets beside it. This file is the record of what the crate is, what
was measured while building it, and which numbers would falsify which
decisions.

## The panel, as far as text is concerned

Sharp LS012B7DD06: 240×240 at 0.126 mm, round, two bits a channel, so a
glyph's anti-aliasing is a choice among 85, 170 and 255, and only the
inscribed disc is glass. Bright text on a dark ground is the only kind
proven to render; a black-on-white readout came back as a blank white band
on hardware (`RustGuiPoc/Docs/FINDINGS.md`). Measured on this glass by the
cartography research: 22 to 26 px em is confirmed readable, 11 to 12 px is
detection only. Falsified by a different display or a device photograph
that contradicts either.

## What it does

- **Faces** are `static`s in `src/faces.rs`, one per (weight, pixel size,
  character set) listed in `Tools/faces.json`. A face not listed does not
  exist, and a screen that names one fails to compile. The linker drops the
  faces an app does not reference: NotifyToggle names two of twelve and
  carries two.
- **Measure**: sum of advances, ink bounds, and how many characters had no
  glyph. Alignment and wrapping use the advance, as TouchGFX's
  `getStringWidth` did, so Barcode's measured bezel table transfers.
- **Draw** at a baseline or a top, left, centre or right, in any `ABGR2222`
  ink over a declared ground, clipped to the disc by
  `BarcodeLayout::pixelIsLit`'s rule (centre within 119.5 pitches).
- **Wrap**: greedy by word into caller-owned line slots, never splitting a
  word, reporting how many lines the text needed so overflow is a number
  the caller sees rather than a line that vanished.
- **Ladder**: `pick` returns the first face whose advance fits.
- **Missing glyph**: a one-pixel hollow box, capital height by 0.6 em, never
  `?` and never nothing; `Measure::missing` counts them so a caller with
  Barcode's refuse-rather-than-guess rule can refuse.
- **Composition**: a base letter followed by a combining mark becomes its
  precomposed Latin form when one exists (`src/compose.rs`, 161 pairs);
  anything else combining draws as missing on its own. Code points above
  U+FFFF draw as missing; that is emoji, and it is deliberate.

Coverage today is tier 0 (ASCII) and tier 1 (Latin-1 Supplement, Latin
Extended-A, … – — ‘ ’ “ ” € •) per face as the manifest says. Poppins has
one Greek code point and no Cyrillic, so tier 2 is a second face whatever
the mechanism; tier 3 (CJK) is a file, and the format has a header for that
day. Neither is built. `Docs/TEXT.md` costs both.

## The bar, and the test that holds it

TouchGFX Designer's font converter, the only pipeline this platform ever
had, turns out to be **FreeType's light autohinter, coverage rounded to the
nearest third (thresholds 43 / 128 / 213 of 255), blank edge rows and
columns cropped**. `Tools/atlas.py` does exactly that, and
`tests/touchgfx_parity.rs` holds it: every glyph the oracle carries from
Squash's shipped `Font_Poppins_*_2bpp` tables (SemiBold 18, Regular 14,
SemiBold 20, Regular 18) must match ours in width, height, bearings, advance
and every pixel. Across the four faces and 94 non-space ASCII glyphs each,
376 of 376 do. The glyph byte totals match too: SemiBold 18 2,638, Regular
14 1,583, SemiBold 20 3,331, exactly what the TouchGFX tables hold.

Falsified by a FreeType whose autohinter renders differently; the generator
writes the version it ran with into `src/faces.rs` (2.13.2, from
`freetype-py 2.5.1`, pinned in `Tools/requirements.txt` and in the toolchain
image), and the parity test is what notices.

Light autohinting snaps vertically only. Measured on the host
(`Docs/text-design/04_stems.png`): Regular's stems at 16 to 20 px are about
1.5 px wide and land as an 85 + 170 column pair hinted or not; SemiBold's at
20 and 24 px are two solid columns. For anything small that must read as
crisp, prefer SemiBold. Horizontal stem snapping is possible in the
generator and waits on a photograph saying stems are the problem.

## The format

Frozen once shipped. Little-endian. In `.rodata` it is a Rust `Face` whose
`nodes` and `data` are these two arrays; in a file it would be this header,
the nodes and the bitstream, in that order.

| field | type | meaning |
|---|---|---|
| `magic` | u32 | `"TXA1"` |
| `version` | u16 | 1 |
| `px` | u8 | em size the glyphs were rendered at |
| `ascent`, `descent` | u8, u8 | pixels the face may use above and below the baseline |
| `flags` | u8 | bit 0: hard-aliased build, levels are 0 or 3 only |
| `count` | u16 | nodes, sorted by code point |
| `data_len` | u32 | bytes of bitstream |
| node × `count` | 12 bytes | `off` u32, `cp` u16, `w` u8, `h` u8, `top` i8, `left` i8, `adv` u8, `flags` u8 |
| bitstream | | row-major, two bits a pixel, least-significant pair first, no row padding, glyph `n` starting at byte `off` |

The bit order is TouchGFX's own, established by decoding its tables: the 3×11
`!` is 9 bytes, which only unpadded packing gives, and the pixels come out
right only least-significant pair first.

## Footprint

The blit alone measured 610 bytes of code on the candidate harness
(`Docs/text-design/measurements/sizeproto/`). Linked into NotifyToggle with
measurement, coverage checks and composition, the crate is **1,530 bytes of
`.text`** and 7,271 of `.rodata`, of which 6,137 are the two faces that app
names and 966 the composition table; no `.bss` beyond the framebuffer. Per
face, bytes of glyph data plus 12 per node:

| face | glyphs | bytes |
|---|---|---|
| `SEMIBOLD_18_ASCII` | 95 | 3,778 |
| `REGULAR_14_ASCII` | 95 | 2,723 |
| `REGULAR_12_ASCII` | 95 | 2,359 |
| `SEMIBOLD_20_ASCII` | 95 | 4,471 |
| `REGULAR_18_LATIN` | 307 | 13,090 |
| `REGULAR_16_LATIN` | 307 | 11,119 |
| `SEMIBOLD_24_ANSWERS` (`YESNO`) | 6 | 376 |
| `SEMIBOLD_32_TITLE` (`SPIN`) | 5 | 386 |
| `SEMIBOLD_27_CLOCK` (`0-9:`) | 12 | 841 |
| `SEMIBOLD_36_CLOCK` | 12 | 1,395 |
| `SEMIBOLD_49_CLOCK` | 12 | 2,454 |
| `SEMIBOLD_60_CLOCK` | 12 | 3,598 |

Per-app before and after rows are in each app's README under Footprint.

## Building and testing

```sh
cd TextKit
cargo test --features std          # unit tests and the TouchGFX parity oracle
cargo build --release --target thumbv8m.main-none-eabihf
cargo run --features std --example measure SEMIBOLD_18_ASCII "NOTIFICATIONS"
```

Regenerating the atlases needs the pinned tools:

```sh
python3 -m venv .venv && .venv/bin/pip install -r Tools/requirements.txt
.venv/bin/python Tools/atlas.py            # rewrites src/faces.rs and src/compose.rs
.venv/bin/python Tools/atlas.py --check    # what CI should run: fails if either is stale
.venv/bin/python Tools/touchgfx_oracle.py  # refreshes tests/oracle/touchgfx.rs from Squash
```

The toolchain image carries the same pins, so `--check` can run in CI once
`app-build.yml` is repinned to an image built from the Dockerfile that added
them. The fonts are in `Fonts/`, byte-identical to the blobs TouchGFX
rasterized; `Fonts/FONTS.md` has the licence.

## Porting notes

What each port taught, so the next one starts further along.

### NotifyToggle

The smallest app, two words and a hint, on `embedded-graphics`'s fixed-width
`MonoTextStyle`. Ported to `SEMIBOLD_18_ASCII` for the words and
`REGULAR_12_ASCII` for the hint.

- **The bezel found the footer.** `R1 TOGGLE  R2 BACK` is 117 px in Regular 12
  against a 122 px chord at its old baseline of 222; moved to 220 where the
  chord is 129. Regular 14 would have been 143 px, wider than any row that
  low. The chord arithmetic is one line of Python
  (`sqrt(119.5² − (y − 119.5)²)`), and the bezel test now runs on every state,
  which is what caught it before a wrist did.
- **The linker does drop unreferenced faces.** Twelve faces in `faces.rs`,
  two named by the app; the map shows only those two.
- **Cost of joining.** `.text` 31,980 → 33,608, `.bss` unchanged at 58,432,
  `.uapp` 41,188 → 42,820. The 1.6 KB is 1.5 KB of TextKit code plus 6.1 KB
  of faces, less the 3.6 KB of `embedded-graphics` text machinery and mono
  fonts it no longer links. Proportional Poppins for the price of one atlas,
  and every string it draws now sits on the same face Barcode's and Spin's
  will.
- **The prototype's 610 bytes was the blit alone.** The crate an app actually
  links carries `measure`, `covers` and composition too: 1,530 bytes. Still a
  third of the u8g2 renderer, and there is no scratch buffer to add.
- **A face constant per role** (`WORD_FACE`, `HINT_FACE`) rather than a face
  name at each call site: the port was two edits to change a face and the
  coverage test checks each role's strings against its own face.
