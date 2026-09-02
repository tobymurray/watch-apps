# Text for the Rust GUIs

A design for how the `CustomGUI` Rust renderers draw text, written before the
crate, against the brief in [`TEXT-PROMPT.md`](TEXT-PROMPT.md). Everything
numeric here was measured on this Mac or read from a build; the measurements,
the scripts that produced them and the contact sheets are in
[`text-design/`](text-design/). Nothing here is evidence about the glass: no
watch was available to this pass, and the section "What only the watch can
settle" lists what that leaves open.

**Status: a proposal, to be agreed before any crate is written.** Three apps
draw text in Rust today, two of them through the same trick, and the decision
below keeps what they got right and replaces what they only assumed.

## The short version

1. **Pre-render Poppins at build time into 2bpp glyph atlases and blit them.**
   That is what TouchGFX did, and its converter turns out to be reproducible
   exactly: FreeType 2.13.2's light autohinter, coverage rounded to the nearest
   third, blank edge rows cropped. Run that way, a checked-in Python generator
   produced **376 of 376 glyphs pixel-identical** to the Poppins tables the
   Squash app ships (Regular 16 and 18, SemiBold 20, Medium 50), with every
   advance width matching. The bar the wearer saw is not approximated; it is
   regenerated.
2. **It is smaller than what ships today, and it adds coverage.** The blitter is
   about 600 bytes of code and needs no scratch buffer. Barcode's text machinery
   goes from about 24 KB to about 18 KB while its Regular face gains Latin-1 and
   Latin Extended-A; Spin's goes from about 38 KB to about 25 KB with the same
   gain on its label face. NotifyToggle can join for about 7 KB or stay on its
   fixed-width floor.
3. **One shared crate, `TextKit`**, a Cargo dependency of each app's crate in the
   way MapKit is a shared directory: the atlas format and its decoder,
   measurement, alignment, greedy wrap, the size ladder, the lit-disc clip, the
   missing-glyph rule and the host tests. It owns no screen, no frame struct and
   no C ABI.
4. **The atlas format can live in a file; no file path is built.** Same bytes in
   `.rodata` or under `../SharedData/fonts/`, with a header carrying magic,
   version and the `(size, crc)` guard MapManager already uses. Nothing an app
   draws today or plausibly soon needs the filesystem, and the one read that
   would decide such a design, a few hundred bytes at a random offset from a
   GUI process, has never been timed on the watch.
5. **Runtime outline rasterization was built, measured, and rejected for now.**
   A `no_std`, alloc-free prototype on `ttf-parser` links at 33 KB of code, of
   which about 24 KB is a CFF parser Poppins never uses and `core`'s decimal
   float parser that it drags in along with nine f64 soft-float routines. A
   purpose-built `glyf` reader would bring it to roughly 11 KB, and the font
   subset is 7.8 KB per weight for ASCII or 20 KB for tier 1 at every size. It
   becomes the cheaper design only once an app wants tier-1 coverage at about
   three or more sizes of one weight, and it has no autohinter. The break-even
   is in the tables, so the next person can switch with a number.
6. **Two things the brief states are wrong, and one is imprecise.** Poppins has
   **no kerning pairs**: its `GPOS` carries only Devanagari mark positioning and
   a subset with the `kern` feature is byte-identical to one without, so
   TouchGFX's empty kerning tables reflect the font, not the converter. Poppins
   has **no hinting instructions** either (no `fpgm`, `prep` or `cvt `), so
   TrueType hinting is not a lever that exists. And the TouchGFX glyph byte
   counts in the brief are about 96 bytes per face high, from counting the
   `0x0020` inside each `// Unicode:` comment as a data byte; the corrected
   totals are below.

## What this corrects in the record

Read before the tables, because two of these change what "the bar" means.

**TouchGFX's converter is FreeType light autohinting plus round-to-third.**
Tested against the shipped `Font_Poppins_*_2bpp_0.cpp` bitmaps in
`Squash/Software/Apps/TouchGFX-GUI/generated/fonts/src/`, decoding the 2bpp
stream as row-major, two bits a pixel, least-significant pair first, no row
alignment (the 3×11 `!` is 9 bytes, which only that packing gives). Three
hinting modes and nine threshold sets were tried:

| face | hinting | thresholds (of 255) | glyphs pixel-identical | identical after cropping a blank edge | advances match |
|---|---|---|---|---|---|
| Regular 16 | light | 43 / 128 / 213 | 49 | 45 | 95 / 95 |
| Regular 18 | light | 43 / 128 / 213 | 45 | 49 | 95 / 95 |
| SemiBold 20 | light | 43 / 128 / 213 | 64 | 30 | 95 / 95 |
| Medium 50 | light | 43 / 128 / 213 | 58 | 36 | 95 / 95 |
| any | none | best of nine | 1 to 3 | | 45 to 56 / 95 |
| any | normal | best of nine | 0 to 7 | | 67 to 73 / 95 |

Every one of the 94 non-space ASCII glyphs in each face is accounted for by
the first two columns. The "crop" is the converter dropping a fully blank
first or last row or column that FreeType leaves in the bitmap. 43 / 128 / 213
is `round(v × 3 / 255)`, so a coverage of one sixth becomes level 1 and five
sixths becomes level 3. Falsified by a different FreeType version producing
different autohinted bitmaps; the generator records the version it ran with
and the test suite carries a few of Squash's glyphs as the oracle.
Script: [`text-design/measurements/parity.py`](text-design/measurements/parity.py).

**Corrected TouchGFX face sizes**, from the same files with comment lines
stripped (16-byte `GlyphNode` each, ellipsis included):

| face | glyphs | glyph bytes | nodes | total |
|---|---|---|---|---|
| Regular 14 | 96 | 1,587 | 1,536 | 3,123 |
| Regular 16 | 96 | 2,003 | 1,536 | 3,539 |
| Regular 18 | 96 | 2,545 | 1,536 | 4,081 |
| SemiBold 18 | 96 | 2,647 | 1,536 | 4,183 |
| SemiBold 20 | 96 | 3,340 | 1,536 | 4,876 |
| SemiBold 25 | 96 | 5,102 | 1,536 | 6,638 |
| SemiBold 30 | 96 | 7,396 | 1,536 | 8,932 |
| Medium 50 | 96 | 19,178 | 1,536 | 20,714 |
| SemiBold 60 | 96 | 28,948 | 1,536 | 30,484 |

**Poppins, as a file.** 1,059 glyphs, 471 code points, 1000 units per em,
ascender 1050 and descender −350 (a 1.4 em line), cap height 698 (Regular) and
701 (SemiBold), x-height 548 and 554. Coverage: ASCII 95 of 95, Latin-1
Supplement 96 of 96, Latin Extended-A 107 of 128, Latin Extended-B 8, Greek
**1**, Cyrillic **0**, Devanagari 94. Of the unit and punctuation symbols the
brief names: ° µ ± … – — ‘ ’ “ ” € are present; **′ (U+2032) and Greek mu
(U+03BC) are not**, and neither is any heart, which is why Spin hand-draws its
own. `GPOS` features are `abvm` and `blwm` only; there is no `kern` table.

Subset sizes, reproduced with `pyftsubset` from fontTools 4.60.2, hinting
dropped, `GSUB`/`GDEF`/`name` dropped:

| subset | code points | Regular | SemiBold |
|---|---|---|---|
| ASCII | 95 | 7,808 | 7,772 |
| ASCII + Latin-1 + Latin Ext-A + … – — ‘ ’ “ ” € • | 307 | 20,212 | 20,164 |

The brief's 7,896 and 20,240 were within 1.5 % and used a slightly different
option set. Of the 7,808 bytes, 6,714 are `glyf`.

**The heap exists and its route is known; its location is not.** In
`una-sdk/Libs/Source/AppSystem/system.cpp`, `_malloc_r` calls
`imem->malloc(size)` on the kernel's `IAppMemory`, `operator new` forwards to
`_malloc_r`, and `_sbrk` asserts. Whether the kernel allocator hands out memory
inside or outside the GUI's 600 KiB window is still unmeasured. The design
below needs no heap, so this stays a note.

**No kernel font service is exposed to apps.** `grep -i font` over
`Libs/Header/SDK/Interfaces/` and `Libs/Header/SDK/Kernel/` finds nothing.
TouchGFX Designer remains the only font pipeline that has existed here, and it
is replaced below by a 90-line Python script.

**The panel research repo has no font work.** The only hits for glyph, TTF or
atlas in `una-sdk-research/Docs` are the map style scripts and the TouchGFX
widget docs.

## The status quo, re-measured

Fresh builds of all three GUIs in CI's toolchain image
(`ghcr.io/tobymurray/watch-apps-toolchain@sha256:f5689e68…`, run by ID
`f5689e6804e6`), read from `<App>/Output/<App>GUI.elf.elf.map`:

| | Barcode | Spin | NotifyToggle |
|---|---|---|---|
| `.text` | 42,176 | 36,984 | 31,980 |
| `.data` + `.got` | 200 | 256 | 228 |
| `.bss` | 73,712 | 85,200 | 58,432 |
| `.stack` | 10,240 | 10,240 | 10,240 |
| packaged `.uapp` | 72,004 | 113,612 | 41,188 |
| of which faces (`.rodata`) | 6,125 | 7,266 | about 1,500 |
| of which scratch buffer (`.bss`) | 15,360 | 26,880 | 0 |
| of which font renderer code | about 2,050 (`u8g2_fonts`) + the shrink loop | about 2,890 + the shrink loop | 3,618 (`embedded_graphics` text + mono font) |

The face sizes are the crate's own files: `helvB24_tr` 2,965, `helvR24_tr`
3,160, `fub49_tn` 1,141. `.bss` less the 57,600-byte framebuffer is the
scratch buffer plus a few hundred bytes; the Barcode README's 83,952 is this
`.bss` with the stack folded in. The SDK shell's floor is visible in
NotifyToggle, which draws two words: about 32 KB of `.text` before any text
machinery worth the name.

How the status quo's output compares with a real rasterizer is in
[`text-design/01_barcode_tiers.png`](text-design/01_barcode_tiers.png) and
[`02_spin_sizes.png`](text-design/02_spin_sizes.png): each row is the app's own
supersample-and-shrink beside Poppins rendered three ways at the size the app
replaced or matches by cap height, all four levels shown, 4× nearest. What the
sheets and [`metrics.csv`](text-design/measurements/ref/metrics.csv) show:

- **Barcode's large id is bigger and heavier than the SemiBold 20 it
  replaced.** `helvB24` shrunk to 24 has a 17 px capital against Poppins
  SemiBold 20's 14, and `0123456789ABCD` measures 203 px against 184. The tier
  logic still keeps it inside the 187 px box, so nothing clips, but the large
  tier now covers fewer ids than the TouchGFX build's measured table.
- **The shrink is coarse where it should be fine.** Averaging a 32-row 1bpp
  source into 18 rows leaves 17 to 22 % of the id's ink pixels at an
  intermediate level where a real rasterizer leaves about 40 %; at 54 px, where
  the source is 63 rows, only 4.5 % against 17 %. The digits are nearly hard
  edged, which is why Spin's clock looks crisper than its labels.
- **Small labels go the other way.** At 16 px the shrink leaves 58 % of ink
  partial against Poppins' 65 %; the shape is Helvetica's at 32 shrunk, so the
  bowls are rounder and the counters more closed than a face designed for
  16 px.
- **Its widths are a ratio, not a metric.** `Face::width` scales the source
  face's ink width by a height ratio, and the sheets show it 5 to 12 % off the
  destination's real advance width in both directions.

## The inventory

Every string the three Rust GUIs draw, from `lib.rs` of each, with the face the
app uses today, the size on the panel, and the Poppins size whose capital
height matches it (Poppins capital = 0.698 em; the status quo's capitals were
measured from its own output).

**Barcode** (`Barcode/Software/Apps/CustomGUI/rust/src/lib.rs`)

| string | source | today | matches | box | align | colour |
|---|---|---|---|---|---|---|
| id, one line, preferred | `[ -~]{1,16}`, validated upstream | `helvB24_tr` → 24 | SemiBold 24 (TouchGFX shipped SemiBold 20) | x 27, w 187, y 178 | centre | white |
| id, one line, fallback; id split over two lines | same | `helvR24_tr` → 18 | Regular 18 | same box; rows 169 and 192 | centre | white |
| caption (name) | `[ -~]{0,12}` | `helvR24_tr` → 18 | Regular 18 | x 40, w 160, y 48 | centre | white |
| prompt, up to 4 wrapped lines | 8 English literals in `Gui.cpp`, one built from `kFormatNames` | `helvR24_tr` → 18 | Regular 18 | x 20, w 200, top 72, pitch 24 | centre | white |

The prompt literals: "input.json has no usable code"; "No codes set yet. Open
the UNA app and enter your ID"; "That ID cannot be drawn: 1-16 plain
characters"; "ITF needs an even count of digits, 2 to 16"; "ITF only draws
digits 0-9"; "That ID starts or ends with a space, remove it"; "Unknown
format. Set it to Code128, QRCode or ITF."; "No codes yet. Set one in the UNA
app, or write input.json.". The widest wrapped line is 200 px; the worst
screen is four lines, about 90 glyphs.

**Spin** (`Spin/Software/Apps/CustomGUI/rust/src/lib.rs`)

| string | today | matches | where | align | colour |
|---|---|---|---|---|---|
| clock `M:SS` / `H:MM:SS`, largest of three that fits 218 px | `fub49_tn` → 54, 44, 32 | SemiBold 60, 49, 36, digits and `:` only | y 70 | centre | white, or 170 grey when paused |
| heart rate `bpm` | `fub49_tn` → 25 | SemiBold 27, digits | y 136, in a centred group | left | white |
| labels: STRAP READY, FINDING STRAP, WRIST SENSOR, TARGET, MIN, START, EXIT, PAUSED, TARGET MET, BPM, `---`, SAVE, DISCARD, AVG, NO HEART RATE, KJ, KCAL, DONE, THIS RIDE?, ESTIMATE, +100, +10, SKIP, NOTHING WAS SAVED, and the numbers beside them | `helvR24_tr` → 16 | Regular 16 | rows and the four hint anchors | left, centre, right | white, 170 grey, amber, red |
| headings: SAVED, NOT SAVED, DISCARD, BIKE KJ, DISCARDED | `helvB24_tr` → 18 | SemiBold 18 | y 50, 92, 96, 66 | centre | white, amber |
| answers: YES, NO | `helvB24_tr` → 24 | SemiBold 24 | hint anchors | left, right | red, white |
| title: SPIN | `helvB24_tr` → 32 | SemiBold 32 | y 70 or 84 | centre | white |

`NOTHING WAS SAVED` is the widest label, 175 px today. The heart is hand-drawn
and stays so; Poppins has no heart.

**NotifyToggle** (`NotifyToggle/Software/Apps/CustomGUI/rust/src/lib.rs`)

| string | today | matches | colour |
|---|---|---|---|
| NOTIFICATIONS; ON, OFF, `?` | `FONT_9X15_BOLD`, 15 px cell, 10 px capital | SemiBold 14 | white; green, grey, amber |
| R1 TOGGLE  R2 BACK | `FONT_6X10`, 7 px capital | Regular 10 | grey |

**What does not exist yet but will be asked for**: a wearer's name with a
diacritic (José, Zoë) in Barcode's caption and any future greeting; a UI
literal in a second language, which for the languages Poppins covers is
tier 1; a forwarded notification, which is arbitrary Unicode and the only
plausible route to tiers 2 to 5; unit symbols ° ′ µ ± in a future sensor app;
an ellipsis for anything truncated.

**Against the measured floors.** The cartography research puts 22 to 26 px em
as "confirmed readable on device", 28 "comfortable", 11 to 12 "detection, not
reading", and the FAA's 9×13 pixel capital as the standards floor. Spin's
labels are a 16 px em with a 12 px capital, Barcode's small tier an 18 px em
with a 13 px capital: at or just above the pixel-matrix floor, below the
confirmed-readable size. Whether they read is a question for the glass; what
this design changes is that raising them becomes one line in a manifest with
a known cost, which the ladder table gives.

## Coverage tiers, costed

Per face and size, in bytes, for the two mechanisms that stay in `.text`.
Atlas figures are from the generator with the settings that reproduce TouchGFX
(light autohint, round-to-third, cropped ink, 12-byte node per glyph); TTF
figures are the `pyftsubset` numbers above and cover every size at once.

| tier | adds | Regular atlas at 16 px | at 18 px | at 22 px | TTF subset, any size | who needs it |
|---|---|---|---|---|---|---|
| 0 | printable ASCII, 95 | 3,139 | 3,678 | 4,753 | 7,808 | every app today |
| 1 | Latin-1, Latin Ext-A, … – — ‘ ’ “ ” € •, 307 | 11,119 | 13,090 | 17,079 | 20,212 | European names, units except ′ |
| 2 | Greek 105 and Cyrillic 226 | +4,529 and +10,684 (16 px, Arial Unicode as proxy) | | | not in Poppins | UI in more languages |
| 3 | Kana 184, CJK 20,902, Hangul 11,172 | +11,035; +1,598,998; +787,768 (16 px, proxy) | | | not in Poppins | notifications |
| 4 | Arabic, Hebrew, Devanagari shaping | a shaper: `rustybuzz` is `no_std` but `extern crate alloc` | | | | notifications |
| 5 | emoji | colour, so one byte a pixel: about 3,600 glyphs × 20 × 20 px ≈ 1.4 MB at 20 px, arithmetic not a measurement | | | | don't |

Tier 1 is affordable in `.text` at one or two sizes per weight. **Tier 2 needs
a second face whatever the mechanism**, because Poppins has one Greek code
point and no Cyrillic; Noto Sans (SIL OFL, x-height 536 to Poppins' 548) is the
obvious candidate and is not chosen here, because nothing draws it yet. Tier 3
stops being a table at Kana already if it must sit beside everything else, and
CJK is a file or nothing: 1.6 MB at 16 px is two and a half GUI windows.
Tier 4 is a shaper, and a shaper without `alloc` is a research project this
design declines to cost; Arabic or Hebrew text arriving in a notification will
draw as missing-glyph boxes, visibly. Tier 5 is a 64-colour panel drawing
colour art at 20 px; the cost is a face this project does not have and about
1.4 MB of it.

## The candidates, with their rows

All four `no_std` prototypes were built in one workspace for
`thumbv8m.main-none-eabihf`, `opt-level = "z"`, LTO, `panic = "abort"`,
against a 600 KiB linker region, each rendering the same string with three
faces into a 57,600-byte framebuffer, measured with `llvm-size`
([`text-design/measurements/sizeproto/`](text-design/measurements/sizeproto/)).
"Code" is `.text` less the 74-byte empty harness; "data" is `.rodata` less its
36; "scratch" is `.bss` less the framebuffer.

| | code | data | scratch | render time, worst screen | tiers reachable | quality | risks that are not numbers |
|---|---|---|---|---|---|---|---|
| **A** status quo: three u8g2 1bpp faces, supersample and shrink | 4,054 | 8,740 (7,266 faces) | 26,880 | unmeasured; a per-pixel pass over every glyph's source block | 0; tier 1 via `_te` faces at 17,237 and 18,113 bytes each; Greek and Cyrillic only in fixed-width u8g2 faces; no route to 3 | Helvetica shrunk; widths a ratio; 4.5 % partial pixels at 54 px | one hand-sized scratch per app with `.clamp()` hiding overflow; two diverged copies |
| **B** pre-rendered 2bpp Poppins atlases, blitted | **610** | 21,412 (Regular 18 tier 1 + SemiBold 20 + SemiBold 54 digits) | **0** | a copy per glyph pixel; unmeasured on the watch, cannot exceed A's | 0 and 1 in `.text`; 2 with a second face; 3 only as a file | **identical to TouchGFX's shipped glyphs**, proven on 376 | sizes multiply; a size not generated is a build error; the generator must stay deterministic |
| **C** runtime outline rasterization, `ttf-parser` + own row-sweep coverage rasterizer, f32 | 33,102 as built; about 11,000 with a purpose-built `glyf` reader | 27,984 fonts + 14,400 of `core` float-parsing tables as built | 8,584 (512-edge list + one row) plus any glyph cache | host: 0.8 µs a glyph at 18 px, 2.4 µs at 54 px, 0.075 ms for the four-line prompt; **not measured on the Cortex-M33** | 0 and 1 from one file per weight at every size; 2 with a second file | reference coverage but **no autohinter**: 16 to 20 px Regular stems straddle two columns, see below | pulled in f64 soft-float and a CFF parser through a dependency; every string re-rasterized every tick unless cached |
| **D** small resident face + 2bpp glyph store on the filesystem | B's 610 plus a reader and cache, unwritten | B's data for the resident tier | a glyph cache, about 8 KB for one screen of 54 px digits | one cold miss per new glyph; **the random-offset read is unmeasured** | the only shape for tier 3 | B's | half-copied files, one of ten open handles for the session or an open per miss, a provisioning route that does not exist |
| **E** TTF on the filesystem, rasterized at runtime | C's | 0 resident | C's plus a cache | C's plus a small read per glyph miss; unmeasured | C's | C's | C's and D's together |
| **F** signed-distance fields, stroke fonts, TouchGFX tables verbatim, a kernel font service | not built | | | | | | SDF at 16 px with two intermediate levels quantises its whole gradient into one fringe pixel; stroke fonts are not Poppins; TouchGFX's tables are B in a 16-byte node with no tier 1; there is no kernel service |

The C prototype's 33 KB, broken down by `llvm-nm`: `draw_text` with the
rasterizer inlined 8,868; `ttf_parser::glyf::outline_impl` 1,916 and
`glyph_index` 1,760; the **CFF parser about 6,200** (`__parse_char_string`
4,312 alone), which Poppins as a `glyf` font never calls; `core::num::dec2flt`
about 4,600 of code plus a **10,416-byte `POWER_OF_FIVE_128` table**, reached
from the CFF dictionary parser's real-number decoding; and `__muldf3`,
`__divdf3` and seven `__aeabi_d*` routines, about 2,600, whose only callers are
that float parser. `ttf-parser` 0.25 has no feature that removes CFF. The
"about 11 KB" is what remains when those are subtracted, which a 300-line
reader of `head`, `hhea`, `hmtx`, `loca`, `glyf` and `cmap` format 4 would
leave; it is derived from the measured breakdown, not measured itself.

The host timing is on an Apple M5. No scaling to a 160 MHz Cortex-M33 is
offered as a number; even at 300× slower the four-line prompt is 22 ms against
78 ms of slack, and the point of recording it is that C's time is not what
rules it out.

**Per app**, the text machinery each candidate would carry, with today's
sizes kept and Barcode's large tier returned to the SemiBold 20 whose bezel
fit was measured:

| app | today (A) | B | C with the slim reader | what B adds |
|---|---|---|---|---|
| Barcode: SemiBold 20 tier 0, Regular 18 **tier 1** | 6,125 faces + 15,360 scratch + about 2,500 code ≈ 24,000 | 4,471 + 13,090 + 610 ≈ **18,200** | 7,772 + 20,212 + 11,000 + 8,584 ≈ 47,600 | tier 1 on every string but the id, Poppins metrics, no scratch |
| Spin: Regular 16 **tier 1**, SemiBold 18 tier 0, YES/NO at 24, SPIN at 32, digits at 27, 36, 49, 60 | 7,266 + 26,880 + about 4,000 ≈ 38,000 | 11,119 + 3,778 + 368 + 374 + 950 + 1,513 + 2,662 + 3,897 + 610 ≈ **25,300** | 7,772 + 20,212 + 11,000 + 8,584 ≈ 47,600 | tier 1 on labels, real digits at every clock size |
| Spin with labels and headings raised to the 22 px floor | | 17,079 + 5,329 + 441 + 374 + 950 + 1,513 + 2,662 + 3,897 + 610 ≈ 32,900 | ≈ 47,600 | |
| NotifyToggle: SemiBold 14 + Regular 10 matching today's capitals, or SemiBold 18 + Regular 14 | about 5,100 | 2,864 + 2,061 + 610 ≈ 5,500, or 3,778 + 2,723 + 610 ≈ 7,100 at the larger pair | | proportional Poppins; optional |

**Where B stops winning.** Tier 1 costs 11 to 17 KB per size per weight
between 16 and 22 px; C costs about 11 KB of code once plus 20 KB per weight
for every size. So C is cheaper than B once one weight needs tier 1 at about
three sizes, or tier 0 at about eight. No app is near either. When one is, C's
data format for a glyph cache should be B's atlas format, so the blitter,
layout and tests carry over and only the source of glyph bitmaps changes.

The atlas ladder, so the next size added is a number rather than a feeling
(bytes, 12-byte nodes, cropped ink, light autohint):

| px | Regular T0 | Regular T1 | SemiBold T0 | SemiBold T1 | SemiBold digits `0-9 : - +` |
|---|---|---|---|---|---|
| 14 | 2,723 | 9,598 | 2,864 | 10,099 | 387 |
| 16 | 3,139 | 11,119 | 3,314 | 11,744 | 442 |
| 18 | 3,678 | 13,090 | 3,778 | 13,681 | 505 |
| 20 | 4,193 | 14,867 | 4,471 | 16,022 | 597 |
| 22 | 4,753 | 17,079 | 5,329 | 19,474 | 699 |
| 24 | 5,716 | 20,722 | 6,034 | 21,921 | 791 |
| 26 | 6,393 | 23,080 | 6,891 | 25,131 | 896 |
| 28 | 7,103 | 25,770 | 7,562 | 27,568 | 1,014 |
| 32 | 8,923 | 32,551 | 9,368 | 34,392 | 1,228 |
| 36 | 10,797 | 39,611 | 11,508 | 42,265 | 1,513 |
| 49 | 18,640 | 68,905 | 20,780 | 76,916 | 2,662 |
| 60 | 28,076 | 103,939 | 29,993 | 111,384 | 3,897 |
| 63 | 30,028 | 111,617 | 33,470 | 124,056 | 4,328 |

A face's code-point set is chosen per size in the manifest, which is what makes
a 60 px clock cost 3.9 KB rather than 30 KB: the clock draws thirteen glyphs.

## Quality, as far as a host can take it

The reference is FreeType's exact-area coverage of the Poppins outline on the
panel grid (an exact box filter), quantised to four levels, which is also what
TouchGFX shipped. The measurements taken
([`text-design/measurements/ref/render.py`](text-design/measurements/ref/render.py)):

**The three conditions the research asked for**, at 16, 18, 22 and 26 px in
both weights, bright on dark, as
[`03_three_conditions.png`](text-design/03_three_conditions.png): hard-aliased;
anti-aliased then quantised at 43 / 128 / 213; anti-aliased with a deliberate
single fringe shade (only 170 used, nothing at 85, which Spin's own note says
washes out in daylight); and light-autohinted then quantised. The fraction of
ink pixels that are partial:

| em | weight | AA → 4 levels | one fringe shade (170) | light-hinted AA → 4 |
|---|---|---|---|---|
| 16 | Regular | 0.61 | 0.47 | 0.62 |
| 16 | SemiBold | 0.49 | 0.37 | 0.45 |
| 18 | Regular | 0.59 | 0.48 | 0.56 |
| 22 | Regular | 0.51 | 0.38 | 0.50 |
| 22 | SemiBold | 0.39 | 0.25 | 0.35 |
| 26 | SemiBold | 0.31 | 0.23 | 0.28 |

At 16 px Regular, three ink pixels in five are a grey. That is the number the
glass has to be asked about: whether a 202 dpi panel fuses them into a stroke
or shows a soft, bold-looking smear. On the host sheet the single-fringe
variant reads heavier and cleaner at 16 and 18 and no different at 26. The
frames for the photograph exist; the photograph does not.

**Stems**, as [`04_stems.png`](text-design/04_stems.png): `lHIl` at 16 to
24 px, unhinted against light-autohinted, 8×. Regular's vertical stems at 16,
18 and 20 px are about 1.5 px wide and land as an 85 + 170 column pair in
every case, hinted or not; **FreeType's light autohinter snaps only
vertically**, so it makes the crossbar of `H`, the baseline and the x-height
crisp and leaves stems alone. SemiBold's stems at 20 and 24 px are two full
columns and mostly solid. This is the mechanism behind the brief's "bold and
soft" prediction, it is real on the host, and the only lever this design has
is the weight: for anything at 16 to 18 px that must read as crisp, prefer
SemiBold. Horizontal stem snapping is possible in the generator (it is a
build-time script with the outline in hand), is not FreeType's light mode, and
is left as the first experiment once a photograph says stems are the problem.

**Fidelity to the TouchGFX bar** is the parity table above: identical.

**The bezel.** Every candidate's blit clips to the lit disc by the same rule as
`BarcodeLayout::pixelIsLit` (centre within 119.5 pitches), inside the crate,
so a text box drawn from the buffer's edge cannot light a pixel behind the
bezel. Spin's `nothing_is_drawn_outside_the_bezel` becomes a crate test run
over every inventory string at every position an app uses.

## What only the watch can settle

In order of how much they could change the decision:

1. **The photograph.** The three-condition frames above, on the glass, in
   daylight and under the frontlight, at 22 and 26 px and at the 16 to 18 px
   the apps draw. This decides whether four-level anti-aliasing is worth any
   bytes at all; if the answer is "hard-aliased reads better", B's generator
   emits 1bpp-in-2bpp atlases at a quarter of the size and nothing else
   changes.
2. **Thresholds.** 43 / 128 / 213 is what the wearer has been looking at, so it
   is the baseline. The panel's own curve has never been measured; a
   photographed grey ramp of the four levels beside a coverage ramp is the
   experiment, and the thresholds are one line in the generator.
3. **Dark on light.** Unmeasured except for the one dropout. The crate permits
   it with the ground colour passed explicitly, and no app screen uses it until
   a device capture says which stroke weight survives.
4. **Render time.** The blit is bounded above by today's shrink loop, which
   ships; measure it anyway with the kernel clock on the four-line prompt and
   the 60 px clock, and record it in the crate README.
5. **The random-offset read.** Only if D or E is ever pursued. Open, seek,
   read 300 bytes, close, a few hundred times, from a GUI process, with the
   distribution reported.
6. **A legibility read** of A against B at the same screen by someone who has
   not read this.

## Design decisions

**One crate.** `TextKit/` at the top level, beside `MapKit/`, crate name
`textkit`, `no_std`, no `alloc`, a `path` dependency of each app's crate (never
a second staticlib, which would collide on `#[panic_handler]`). It owns the
atlas format and decoder, face metrics, measurement (advance and ink bounds),
drawing with left, centre and right alignment at a baseline or a top, greedy
word wrap into caller-owned line slots, the size ladder (preferred face if it
fits, else the next), the lit-disc clip, colour shading, the missing-glyph
rule, normalisation, and a host `std` feature with the reference-diff tests
and a preview path like Spin's. It does not own screens, frame structs, the C
ABI, or `embedded-graphics` (an optional adapter at most; the apps' `FrameBuf`
is a `&mut [u8]` with a width, and so is the crate's target). The generator
lives at `TextKit/Tools/atlas.py` with FreeType and fontTools pinned in the
toolchain image, its output checked in, and a CI step that regenerates and
diffs to zero.

**Faces, sizes, weights.** Poppins Regular and SemiBold, the two the wearer
has seen, from `b0cf873^`. Sizes and code-point sets per app in a manifest the
generator reads; the initial ladders are the inventory tables above, with
Barcode's large tier at SemiBold 20 so the measured 186 px rule in its README
holds by construction and that open item closes. Any size not in the manifest
does not exist, and a screen asking for one fails to compile. Medium and Light
stay in git history until a screen wants them.

**Hinting.** The generator's light autohinting, because it reproduces the bar
and because Ledermann's null result on grid-fitting is the one the research
itself expects not to transfer to 202 dpi. Horizontal stem snapping is an
experiment gated on a photograph.

**Thresholds.** 43 / 128 / 213, the wearer's baseline, revisable by
measurement on the glass.

**Bright on dark.** The rule, in the crate's tests: every inventory string in
every app is bright on a darker ground. The draw call takes the ground colour
so a future dark-on-light screen is possible, and it is refused by the tests
until a device capture is filed beside it.

**Missing glyph.** One rule for every app: a code point without a glyph draws a
one-pixel hollow box, capital height by 0.6 em, in the ink colour, never `?`
and never nothing; `measure` reports how many were missing so a caller with
Barcode's ethos can refuse the string instead. A test asserts that every
literal in every app is covered, so a box can only reach the glass from
runtime data.

**Normalisation.** UTF-8 in. A base letter followed by a combining mark that
has a precomposed form in tier 1 (about 120 pairs, a 500-byte table) is
composed before lookup; any other combining mark draws as a missing glyph on
its own. No other normalisation. Supplementary-plane code points are not
representable in the 16-bit node and draw as missing; that is emoji, and it
is deliberate.

**An unfittable string.** The ladder steps down through the faces the caller
offers; if the smallest still does not fit, the caller gets the measured width
back and decides (Barcode splits the id by character count; Spin's clock has
already chosen its smallest face). The crate never truncates on its own. A
caller wanting an ellipsis asks for one; `…` is in tier 1.

**Heap.** None. Nothing in B allocates; the atlases are `static`, the line
slots are the caller's, and the only working storage is a handful of `i32`.
If C is ever adopted, its edge list and row accumulator are statics too, so
this stays true.

**The C ABI.** Unchanged. Strings keep arriving as fixed byte arrays in the
frame struct; the face is chosen in Rust per string; no font handle crosses.

**The format.** Frozen once an app ships it, so it is written down here rather
than in a comment. Little-endian throughout.

| offset | field | meaning |
|---|---|---|
| 0 | `magic` u32 | `"TXA1"` |
| 4 | `version` u16 | 1 |
| 6 | `px` u8 | em size the glyphs were rendered at |
| 7 | `ascent` u8 | pixels above the baseline the face may use |
| 8 | `descent` u8 | pixels below |
| 9 | `flags` u8 | bit 0: glyphs are 1bpp values in 2bpp cells (hard-aliased build) |
| 10 | `count` u16 | glyph nodes that follow, sorted by code point |
| 12 | `data_len` u32 | bytes of glyph bitstream after the nodes |
| 16 | nodes | `count` × 12 bytes: `off` u32, `cp` u16, `w` u8, `h` u8, `top` i8, `left` i8, `adv` u8, reserved u8 |
| 16 + 12·count | bitstream | row-major, two bits a pixel, least-significant pair first, no row padding, each glyph starting at byte `off` |

In `.rodata` the same layout is a Rust `static` the generator emits, so the
decoder is one code path. In a file it would be preceded by nothing and
followed by MapManager's `(size, crc)` marker convention at `<path>.trust`;
that path is described so it can be built, and is not built.

## The rejected paths, kept

**A, kept as the design.** It costs 15 to 27 KB of scratch RAM per app, sized
by hand, with clamps that hide overflow; its widths are a ratio; its shapes are
Helvetica's; its two copies have diverged; and its route to tier 1 is 17 KB
per face for u8g2's `_te` tier, more than a Poppins tier-1 atlas at 18 px. Its one advantage, that the same three faces serve seven sizes, is what
the ladder table prices at 13 KB for Spin's whole digit ladder.

**C, now.** 33 KB of code as a crate would build it today, 11 KB with work the
project would have to do and maintain, no autohinter, a rasterizer to measure
on the watch, and a font file that only pays off at three or more tier-1
sizes. Its prototype is checked in because it is the fallback when the ladder
grows, and because its rasterizer output is the same coverage the generator
produces, so the day it is needed the tests already exist.

**D and E, now.** No app needs tier 3; the read that would size the cache is
unmeasured; the provisioning route is a USB copy by the owner; and the
directory's contract (ten open files for the whole watch, no rename-over,
nothing cleans up) is a cost this design does not have to pay yet. The format
is file-ready so that when a notification app wants Kana, the work is a reader
and a cache, not a new format.

**F.** SDF has no room to work with two intermediate levels at a 12 px
capital; a stroke font is not the face the wearer knows; TouchGFX's own tables
are the same idea as B with a 16-byte node and no way to add tier 1 without
the Windows-only Designer; and there is no kernel font service.

## Reproducing the numbers

- Fresh GUI maps: [`text-design/measurements/build-all.sh`](text-design/measurements/build-all.sh)
  builds the three GUIs in the CI image by ID; maps land in `<App>/Output/`.
- Size prototypes: `cd Docs/text-design/measurements/sizeproto && cargo build --release`,
  then `llvm-size -A target/thumbv8m.main-none-eabihf/release/proto_*`
  (`llvm-size` and `llvm-nm` are in rustup's `llvm-tools`). `gen_atlas.py`
  regenerates `proto_b_atlas/src/atlas.rs` and needs the two Poppins TTFs from
  `git show b0cf873^:Barcode/Software/Apps/TouchGFX-GUI/assets/fonts/`.
- Reference renders and metrics: `measurements/ref/render.py` in a venv with
  `fonttools brotli pillow freetype-py numpy`; it shells out to the `sq`
  binary in `measurements/sq/`, which is the apps' own supersample-and-shrink
  lifted verbatim.
- Parity with TouchGFX: `measurements/parity.py` against Squash's generated
  fonts. FreeType 2.13.2 via `freetype-py 2.5.1`.
- Subsets: `pyftsubset Poppins-Regular.ttf --unicodes=U+0020-007E --no-hinting --layout-features= --drop-tables+=DSIG,GSUB,GDEF,name --name-IDs= --notdef-outline --desubroutinize`.
