# incised-text: design record

Text for the UNA Watch's Rust GUIs: Poppins, and Atkinson Hyperlegible Next
behind the `atkinson` feature, pre-rendered at build time into 2bpp glyph
atlases and blitted onto any `embedded-graphics` `DrawTarget`. `no_std`,
no allocator, no scratch buffer. Each app's `CustomGUI` crate takes it as a
Cargo path dependency; it owns no screen, no frame struct, no framebuffer and no
C ABI.

Clipping is the target's, not this crate's: on the watch that target is
[`inscribed-disc`](https://crates.io/crates/inscribed-disc)'s `Surface`, which clips to the glass. A
consumer bringing its own colour implements `Ink`; `Ink for Abgr2222` is
inscribed-disc's type, behind the default-on `abgr2222` feature.

Why this shape and not the others was decided by measurement in
[`Docs/TEXT.md`](https://github.com/tobymurray/watch-apps/blob/main/Docs/TEXT.md) at the repo root, with the scripts and
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

## Two typefaces

**Poppins** Regular and SemiBold is the face the wearer already knows: the
blobs TouchGFX rasterized, held to its shipped tables pixel for pixel (below).
Its faces are always compiled, under the names the apps use.

**Atkinson Hyperlegible Next** Bold (wght 700 of the variable font) is behind
the `atkinson` feature, as `faces::ATKINSON_*`. It is here because its
confusable characters are drawn to differ, and Poppins' are not: in these
atlases Poppins' `I` and `l` are the same pixels at Regular 12 and Regular 18,
while `I`, `l`, `1` and `|` are four different bitmaps in every Atkinson ASCII
face (`tests/atkinson.rs` holds that). For an id, a code or a date read at a
glance, that difference is the point.

What a caller should know about it:

- **Digits are proportional by default.** `1` is narrower than `8`, so a
  centred clock would jitter as it ticks. The clock faces are built with the
  font's `tnum` feature, which swaps each digit for its tabular form, so every
  digit advances the same (`tests/atkinson.rs`); the ASCII faces keep the
  proportional digits, which read better in running text such as
  "Sat 17 Oct". Poppins' digits already share one advance in every face
  here, so its faces use no feature.
- **Zero is always slashed.** The font has no plain zero, in either its
  default or its tabular digits, so `0` never passes for `O`, and there is no
  way to get an unslashed one.
- **No on-glass evidence yet.** Every Atkinson face is measured on the host
  only. The panel claims in this README were made with Poppins; nothing here
  says how Atkinson's stems or slashed zero look on the glass until a device
  photograph is filed.

Sizes are matched on capital height, as `Docs/TEXT.md` matched the TouchGFX
faces: the ink rows of `H` rasterized with the generator's settings. The clock
faces carry no `H`, so theirs is measured from the same font at the same size.

| face | px | capital | matched against |
|---|---|---|---|
| `ATKINSON_BOLD_16_ASCII` | 16 | 11 | `POPPINS_REGULAR_16_LATIN`, 11 |
| `ATKINSON_BOLD_20_ASCII` | 20 | 13 | `POPPINS_REGULAR_18_LATIN`, 13; `POPPINS_SEMIBOLD_20_ASCII` is 14, which Atkinson reaches at 21 px |
| `ATKINSON_BOLD_25_ASCII` | 25 | 17 | Poppins SemiBold 24 and 25, both 17: the date and label size |
| `ATKINSON_BOLD_49_CLOCK` | 49 | 33 | Poppins SemiBold 47 and 48, 33; `POPPINS_SEMIBOLD_49_CLOCK` is 35, which Atkinson reaches at 52 px |
| `ATKINSON_BOLD_62_CLOCK` | 62 | 42 | `POPPINS_SEMIBOLD_60_CLOCK`, 42 |
| `ATKINSON_BOLD_23_LATIN` | 23 | 16 | `POPPINS_SEMIBOLD_22_LATIN`, 16 |
| `ATKINSON_BOLD_28_LATIN` | 28 | 18 | `POPPINS_SEMIBOLD_26_LATIN`, 19; Atkinson has no size with a 19-row capital, 27 and 28 give 18 and 29 gives 20 |

Poppins' digits stand taller than its capitals and Atkinson's do not: `1` is
44 rows in `POPPINS_SEMIBOLD_60_CLOCK` and 42 in `ATKINSON_BOLD_62_CLOCK`, so the
matched clock reads two rows shorter in its digits.

Atkinson's capital is about 0.68 em at wght 700 against Poppins SemiBold's
0.70, so it needs one or two more pixels of em for the same capital. At 62 px
`16:27` is 173 px of advance, against 176 for `POPPINS_SEMIBOLD_60_CLOCK`; `1:23:45`
is 229 px at 62 and 181 at 49. At 25 px
"Sat 17 Oct" is 119 px and "100 %" 75. Falsified by rerunning the generator
with a different FreeType, weight or font revision.

## What it does

- **Faces** are `static`s in `src/faces.rs`, one per (weight, pixel size,
  character set) listed in `Tools/faces.json`. A face not listed does not
  exist, and a screen that names one fails to compile. The linker drops the
  faces an app does not reference: NotifyToggle names two of twelve and
  carries two.
- **Measure**: sum of advances, the ink box in columns from the pen and rows
  from the baseline, and how many characters had no glyph. Alignment and wrapping use the advance, as TouchGFX's
  `getStringWidth` did, so Barcode's measured bezel table transfers.
- **Draw** at a baseline or a top, left, centre or right, in any ink over a
  declared ground, both carried by a `Style`. Partial coverage is blended
  through `Ink::shade`; where the pixels are allowed to land is the target's
  business.
- **Wrap**: greedy by word into caller-owned line slots, never splitting a
  word, reporting how many lines the text needed so overflow is a number
  the caller sees rather than a line that vanished.
- **Layout**: `layout` yields each character's pen, advance and inked
  columns where `draw` would put them, so a caller can diff two strings and
  redraw only the columns that changed (README, "Redrawing part of a line";
  the test that holds the recipe redraws through `clipped` and compares
  against a full redraw).
- **Culling**: `draw` skips a glyph whose ink box misses the target's
  `bounding_box`, so drawing a whole line through a clip of one digit offers
  the target that digit's pixels, not the line's. A target whose bounding
  box is smaller than where it accepts pixels would lose glyphs. `Surface`,
  `DiscClipped` and `embedded-graphics`' `Clipped` report the area they draw;
  `Cropped` does not clip, so a line drawn past its box keeps only the glyphs
  that touch it.
- **Coverage**: `covers` answers for a string, `uncovered` names what it
  lacks, `covered` lists what a face has.
- **Ladder**: `pick` returns the first face whose advance fits.
- **Missing glyph**: a one-pixel hollow box, capital height by 0.6 em, never
  `?` and never nothing; `Measure::missing` counts them so a caller with
  Barcode's refuse-rather-than-guess rule can refuse.
- **Composition**: a base letter followed by a combining mark becomes its
  precomposed Latin form when one exists (`src/compose.rs`, 173 pairs);
  anything else combining draws as missing on its own. Code points above
  U+FFFF draw as missing; that is emoji, and it is deliberate.

Coverage is tier 0 (ASCII) and tier 1 (`latin`, below) per face as the
manifest says. Neither bundled font has Greek or Cyrillic, so tier 2 is a
second font whatever the mechanism (below); tier 3 (CJK) is a file, and the
format has a header for that day. Neither is built. `Docs/TEXT.md` costs
both.

## The faces a watch face's text rows need

0.2 added faces for one consumer: a 240×240 face drawing a date, a 24-hour
clock, a battery percentage and labels from a Fluent catalogue and CLDR,
until then in a 5×7 bitmap font scaled ×3 (21 rows), ×5 (35) and ×7 (49).
Previews of its strings in each face are in [`Docs/previews/`](https://github.com/tobymurray/watch-apps/tree/main/IncisedText/Docs/previews),
drawn on the host by [`Docs/measurements/previews`](https://github.com/tobymurray/watch-apps/tree/main/IncisedText/Docs/measurements/previews); every
number below is from the generator and falsified by rerunning it.

**Latin text: Poppins SemiBold 22 and 26, Atkinson Bold 23 and 28.** 22 and
26 are the ends of the 22 to 26 px em this glass confirmed readable;
Atkinson's are matched on capital (the table above). Matching the bitmap's
21-row capital instead would need Poppins 30, at which Romanian's widest
CLDR date, `dum., 30 sept.`, is 216 px against the 213 px chord at the date
row's top. At 26 px it is 187, Polish `niedz., 30 mar` 187, German
`Mo., 30. Sept.` 174 and `Wed 30 Sept` 167; at Atkinson 28 they are 193, 191,
186 and 172. SemiBold, not Regular, because stems at these sizes are two
solid columns in SemiBold and an 85 + 170 pair in Regular (above).

**The `latin` tier**, measured against CLDR 49's main exemplar characters,
upper-cased, plus each locale's abbreviated weekday and month names, for the
106 Latin-script languages ICU4X 2.4 carries. Poppins covers 78 and Atkinson
75, among them every Latin-script official EU language but Maltese in
Poppins and Dutch in both (below), and Afrikaans, Albanian, Basque,
Bosnian, Catalan, Faroese, Galician, Icelandic, Indonesian, Irish, Kurdish,
Luxembourgish, Malay, Norwegian, Romansh, Scottish Gaelic, Serbian (Latin),
Sorbian, Swahili, Tagalog, Turkish, Turkmen, Welsh, Xhosa and Zulu. What 0.2
added to the tier, and why:

| added | for | Poppins | Atkinson |
|---|---|---|---|
| `Ș ș Ț ț` (Latin Extended-B, comma below) | Romanian | yes | yes |
| `Ẁ ẁ Ẃ ẃ Ẅ ẅ Ỳ ỳ` | Welsh | yes | yes |
| `Ə ə` | Azerbaijani | yes | not in the font |
| U+2212 `−` | CLDR's minus sign in 18 locales, Finnish, Estonian, Lithuanian, Croatian among them | yes | yes |
| U+202F narrow no-break space | CLDR's French grouping separator and Occitan's percent | synthesized | synthesized |

Each font's own gaps keep it from languages the tier asks for: Poppins has no
`Ħ ħ` (Maltese, which Atkinson covers); Atkinson has no `Ə ə` (Azerbaijani),
`Ō ō` (Māori, Silesian), `Ĩ ĩ Ũ ũ` (Kikuyu). Neither has Esperanto's
circumflexed consonants, Sami's and Wolof's `Ŋ ŋ`, Vietnamese's Latin
Extended Additional, the African `Ɔ Ɛ` letters, or `ʼ` (Breton, Uzbek,
Quechua). Dutch and Western Frisian pass but for an acute on `j` in `íj́`,
which has no precomposed form. Falsified by rerunning the measurement with a
newer CLDR or font.

Neither font has U+202F, so the generator gives it an inkless glyph as wide
as the font's thin space U+2009 where there is one (Atkinson) and a fifth of
an em, Unicode's description of a thin space, where there is not (Poppins).

**Numeric: Poppins SemiBold 47 and 67, Atkinson Bold 52 and 72.** Each is
the smallest size whose `1`, which has no overshoot, stands 35 or 49 rows:
the bitmap's ×5 and ×7, so the layout's rows carry over, as Spin's port kept
its heights. `23:59` is 194 px at Poppins 67 and 204 at Atkinson 72, against
a 233 px chord at the clock row's top; `--:--` is 174 and 128, since
Atkinson's hyphen is narrow. Digits are tabular in both: Poppins' already
are, Atkinson's take `tnum`. The character set is derived, not guessed, from
ICU4X 2.4's compiled CLDR 49 data for all 333 locales it holds, formatted
as the consumer formats them (`-u-hc-h23` and `fieldsets::T::hm()`, digits
`-u-nu-latn`):

| characters | from |
|---|---|
| `:` | `HH:mm` or `H:mm`, 325 locales |
| `.` | `HH.mm` or `H.mm`: Danish, Finnish, Indonesian, and en-DK, en-FI, en-ID; also Bulgarian's `ч.` |
| `-` | `H-mm`, Marathi; also every negative number, and the unset clock `--:--` |
| `h`, space | `HH 'h' mm`, Canadian French |
| `%`, U+00A0 | the percent format in 332 and 53 locales |
| `,` `'` U+2212 U+202F | decimal and grouping separators and the minus sign of the decimal format |

Bulgarian's `HH:mm ч.` needs `ч`, which neither font has. Arabic-script
locales add U+200E marks and Urdu `٪`; those locales draw nothing else
either. At Poppins 67 Canadian French's `23 h 59` is 252 px, wider than the
clock row; it wants the 47 px face.

## Greek and Cyrillic

Neither bundled font has them: Poppins has `π` and no Cyrillic; Atkinson
Hyperlegible Next has `Δ Ω μ π` and no Cyrillic. Tier 2 is therefore a
second font, and none is added until a consumer draws it. Candidates, all
OFL, measured from Google Fonts' files: the modern Greek letters (70) and
Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian and Kazakh
Cyrillic (114), rasterized by this generator at 23 px.

| font | Greek | Cyrillic | capital / x-height, em | 23 px atlas, both scripts |
|---|---|---|---|---|
| Poppins SemiBold | 1 | 0 | 0.701 / 0.554 | |
| Atkinson Hyperlegible Next | 4 | 0 | 0.668 / 0.496 | |
| **Noto Sans** wght 600 | 70 | 114 | 0.714 / 0.536 | 12,744 bytes |
| Inter wght 600 | 70 | 114 | 0.728 / 0.546 | 13,043 |
| Manrope wght 600 | 69 | 104 | 0.720 / 0.540 | 11,745 |
| Montserrat wght 600 | 8 | 114 | 0.700 / 0.517 | 9,051 |
| Andika Bold | 20 | 114 | 0.725 / 0.508 | 9,489 |

Noto Sans is the recommendation: complete in both scripts, closest to
Poppins' capital, and the candidate `Docs/TEXT.md` already named. None of
them draws Atkinson's confusables apart, so an Atkinson face's Greek or
Cyrillic would be Noto Sans as well. Each would be its own face behind a
`greek` or `cyrillic` feature, about 13 KB a size for both scripts.

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

Measured on `thumbv8m.main-none-eabihf` by
[`Docs/measurements/drawtarget`](https://github.com/tobymurray/watch-apps/tree/main/IncisedText/Docs/measurements/drawtarget), a staticlib
probe linking `draw`, `draw_top`, `measure`, `wrap` and `pick` against a real
`Surface`: **2,858 bytes of `.text`** and 7,471 of `.rodata`, of which 6,137
are the two faces the probe names and 1,038 the composition table; no `.bss`.
The same probe at 0.1 measures 2,632 and 7,399 with the toolchain used here
(rustc 1.99.1; DESIGN once recorded 2,620): 0.2's ink rows and layout add 70
bytes of `.text`, culling glyphs outside the target 156, and the composition
pairs for Romanian and Welsh 72 of `.rodata`. `layout`, `covered` and
`uncovered` cost nothing to an app that does not call them.
See [`Docs/2026-09-10-drawtarget-footprint.md`](https://github.com/tobymurray/watch-apps/blob/main/IncisedText/Docs/2026-09-10-drawtarget-footprint.md)
for what drawing through a `DrawTarget` cost and what recovered most of it.

The per-app rows below were measured against the previous shape, where this
crate owned a framebuffer of its own. **They are stale**: each app has since
deleted its private framebuffer as well, so both sides of the comparison
moved. Re-measuring them wants the toolchain image, which is where `.uapp`
numbers come from. Per face, bytes of glyph data plus 12 per node, which has
not changed:

| face | glyphs | bytes |
|---|---|---|
| `POPPINS_SEMIBOLD_18_ASCII` | 95 | 3,778 |
| `POPPINS_REGULAR_14_ASCII` | 95 | 2,723 |
| `POPPINS_REGULAR_12_ASCII` | 95 | 2,359 |
| `POPPINS_SEMIBOLD_20_ASCII` | 95 | 4,471 |
| `POPPINS_REGULAR_18_LATIN` | 323 | 13,952 (13,090 at 0.1, 307 glyphs) |
| `POPPINS_REGULAR_16_LATIN` | 323 | 11,840 (11,119 at 0.1) |
| `POPPINS_SEMIBOLD_22_LATIN` | 323 | 20,781 |
| `POPPINS_SEMIBOLD_26_LATIN` | 323 | 26,864 |
| `POPPINS_SEMIBOLD_47_NUMERIC` | 21 | 3,074 |
| `POPPINS_SEMIBOLD_67_NUMERIC` | 21 | 5,878 |
| `POPPINS_SEMIBOLD_24_ANSWERS` (`YESNO`), Spin's own | 6 | 376 |
| `POPPINS_SEMIBOLD_32_TITLE` (`SPIN`), Spin's own | 5 | 386 |
| `POPPINS_SEMIBOLD_27_CLOCK` (`0-9:`) | 12 | 841 |
| `POPPINS_SEMIBOLD_36_CLOCK` | 12 | 1,395 |
| `POPPINS_SEMIBOLD_49_CLOCK` | 12 | 2,454 |
| `POPPINS_SEMIBOLD_60_CLOCK` | 12 | 3,598 |
| `ATKINSON_BOLD_16_ASCII` | 95 | 3,098 |
| `ATKINSON_BOLD_20_ASCII` | 95 | 4,062 |
| `ATKINSON_BOLD_25_ASCII` | 95 | 5,848 |
| `ATKINSON_BOLD_49_CLOCK` (`0-9:`, `tnum`) | 12 | 2,398 |
| `ATKINSON_BOLD_62_CLOCK` | 12 | 3,706 |
| `ATKINSON_BOLD_23_LATIN` | 305 | 19,469 |
| `ATKINSON_BOLD_28_LATIN` | 305 | 25,311 |
| `ATKINSON_BOLD_52_NUMERIC` (`tnum`) | 21 | 3,624 |
| `ATKINSON_BOLD_72_NUMERIC` | 21 | 6,625 |

Per-app before and after rows are in each app's README under Footprint.

## Building and testing

```sh
cd IncisedText
cargo test                         # unit tests and the TouchGFX parity oracle
cargo test --features atkinson     # and the Atkinson faces' own tests
cargo build --release --target thumbv8m.main-none-eabihf
cargo run --example measure POPPINS_SEMIBOLD_18_ASCII "NOTIFICATIONS"
(cd Docs/measurements/previews && cargo run --release)   # redraws Docs/previews/
```

Regenerating the atlases needs the pinned tools:

```sh
python3 -m venv .venv && .venv/bin/pip install -r Tools/requirements.txt
.venv/bin/python Tools/atlas.py            # rewrites src/faces.rs and src/compose.rs
.venv/bin/python Tools/atlas.py --check    # what CI should run: fails if either is stale
.venv/bin/python Tools/touchgfx_oracle.py  # refreshes tests/oracle/touchgfx.rs from Squash
```

A face only one app draws is that app's, not the crate's: it lists it in a
`faces.json` beside its `Cargo.toml` and generates a module of its own
(Spin's `src/atlas.rs`):

```sh
.venv/bin/python Tools/atlas.py --manifest ../Spin/Software/Apps/CustomGUI/rust/faces.json \
    --out ../Spin/Software/Apps/CustomGUI/rust/src/atlas.rs [--check]
```

The toolchain image carries the same pins, and `app-build.yml` runs
`--check` and this crate's tests for every app whose renderer depends on it;
a change under `IncisedText/` triggers those apps' builds. The fonts are in
`Fonts/`: Poppins byte-identical to the blobs TouchGFX rasterized, Atkinson
Hyperlegible Next as Google Fonts serves it. `Fonts/FONTS.md` has the sources,
hashes and licences, and the OFL texts beside it ship with the crate.

## Porting notes

What each port taught, so the next one starts further along. The footprint
numbers in each are as they were measured at that port, when this crate still
owned a framebuffer and the app owned a second one.

### NotifyToggle

The smallest app, two words and a hint, on `embedded-graphics`'s fixed-width
`MonoTextStyle`. Ported to `POPPINS_SEMIBOLD_18_ASCII` for the words and
`POPPINS_REGULAR_12_ASCII` for the hint.

- **The bezel found the footer.** `R1 TOGGLE  R2 BACK` is 117 px in Regular 12
  against a 122 px chord at its old baseline of 222; moved to 220 where the
  chord is 129. Regular 14 would have been 143 px, wider than any row that
  low. The chord arithmetic is one line of Python
  (`sqrt(119.5² − (y − 119.5)²)`), and the bezel test now runs on every state,
  which is what caught it before a wrist did.
- **The linker does drop unreferenced faces.** Twelve faces in `faces.rs`,
  two named by the app; the map shows only those two.
- **Cost of joining.** `.text` 31,980 → 33,608, `.bss` unchanged at 58,432,
  `.uapp` 41,188 → 42,820. The 1.6 KB is 1.5 KB of IncisedText code plus 6.1 KB
  of faces, less the 3.6 KB of `embedded-graphics` text machinery and mono
  fonts it no longer links. Proportional Poppins for the price of one atlas,
  and every string it draws now sits on the same face Barcode's and Spin's
  will.
- **The prototype's 610 bytes was the blit alone.** The crate an app actually
  links carries `measure`, `covers` and composition too. Still a fraction of
  the u8g2 renderer, and there is no scratch buffer to add.
- **A face constant per role** (`WORD_FACE`, `HINT_FACE`) rather than a face
  name at each call site: the port was two edits to change a face and the
  coverage test checks each role's strings against its own face.

### Barcode

The first supersample-and-shrink app: two u8g2 faces, a 15,360-byte scratch
buffer, widths scaled by a height ratio. Ported to `POPPINS_SEMIBOLD_20_ASCII` for
the preferred id tier and `POPPINS_REGULAR_18_LATIN` for the rest.

- **The measured table came back.** Barcode's README measured the TouchGFX
  build's id widths with `getStringWidth` and never re-measured the u8g2
  build. Poppins' advances are those numbers again, so the 186 / 187 tier
  thresholds hold by construction and the `0123456789ABCD` id, which the
  u8g2 build had demoted to the small face because its bold face was
  wider, is bold again.
- **Baselines, not tops.** u8g2 placed text by the top of its ascent box;
  the atlases place by baseline. Keeping the picture still meant computing
  each old baseline once (box top plus the scaled ascent, 19 and 14) and
  naming it, rather than reusing the box tops with a face whose ascent
  differs.
- **Poppins is wider than Helvetica shrunk.** The same prompt wraps into
  three lines at Regular 18 where the shrink drew it in three narrower
  ones; nothing needed a fourth, and the wrap test asserts every literal
  fits four.
- **Footprint.** `.text` 42,176 → 46,780, `.bss` 73,712 → 58,352, `.uapp`
  72,004 → 76,176: 17,561 bytes of two atlases in, 6,125 of faces and
  15,360 of scratch out, so 10.8 KB less RAM and 4.2 KB more file. IncisedText
  in situ: 2,248 bytes of code with `wrap` linked, 18,743 of data.
- **Bars are not text.** The Code128 sub-pixel coverage path has its own
  four-level shading for dark bars on the white backing and stayed exactly
  as it was; only the glyph path changed.
- **A caption of twelve `W`s still overflows its 160 px box**, at 216 px in
  Regular 18 as it did in every build before; the crate clips it to the
  disc rather than to the box. Not new, not fixed here, and worth a ladder
  or an ellipsis if a real name ever does it.

### Spin

The most faces of the three: seven on-screen sizes from three u8g2 sources,
a 26,880-byte scratch buffer, and a `Face` type of its own (source plus
destination height). Ported to eight atlases, four of them thirteen glyphs
each for the clock.

- **Per-size character sets are where the savings are.** A SemiBold 60 face
  over ASCII would be 30 KB; over `0123456789:` it is 3.6 KB. The whole
  clock ladder, 27 / 36 / 49 / 60, is 8.3 KB. The manifest makes that a
  one-word choice per face, and the coverage test is what keeps a screen
  from asking a digit face for a letter, which u8g2's `_tn` faces used to
  answer by silently drawing nothing.
- **Keep the app's own names.** The screens asked for text by the heights
  the u8g2 build drew at (`CLOCK_XL_H`, `LABEL_H`, …). Rather than rewrite
  forty call sites, each height now selects the Poppins face whose capital
  matches, and the draw call places the capitals where the old top was
  (top + 0.7 em). Every layout constant stayed; the sheet shows nothing
  moving more than a pixel.
- **`&'static Face` through the old helpers.** `label_face()`,
  `bold_face(h)` and `number_face(h)` kept their signatures and return
  references into the atlases; `&label` at a call site coerces from
  `&&Face`, so the port touched the fonts section and one test.
- **Footprint.** `.text` 36,984 → 44,804, `.bss` 85,200 → 58,320, `.uapp`
  113,612 → 121,060: 25,201 bytes of atlases in, 7,266 of faces and 26,880
  of scratch out, 19 KB less RAM. IncisedText in situ 1,530 bytes of code,
  since nothing here wraps.
- **The one test that knew the renderer** asserted part-covered red on the
  discard screen through the old `shade`; it asks inscribed-disc's `shade` for
  the same two levels and passes unchanged in intent.
