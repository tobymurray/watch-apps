# Prompt: Solve text for the Rust GUIs, once

You are working in `watch-apps`, a set of UNA Watch apps whose GUIs are moving
from TouchGFX to a C++ shell around a `no_std` Rust renderer. The task is to
work out — and then build — how text should be drawn in that Rust environment,
for every app at once, against three goals that pull against each other:

1. **the smallest possible footprint** — code runs from a 600 KiB RAM window
   shared with the framebuffer, so every glyph table is RAM, not flash;
2. **the largest possible character set** — today every string is printable
   ASCII by construction, and that is a limitation, not a decision;
3. **the best possible looking glyphs** — on a panel with exactly four grey
   levels, where TouchGFX's pre-rendered anti-aliased Poppins is the bar the
   Rust ports are measured against and have not yet cleared.

There is a fourth resource you may spend: **a shared data directory on the
watch's filesystem**, reachable from a GUI process at frame time, which apps
already use to hold data too large to ship inside a binary. Font data may live
there if the design earns it.

**This is not a blank page, and the first thing to do is not write code.**
Three apps already draw text in Rust, two of them with the same
supersample-and-shrink trick, and the README of each records what it cost and
what it still gets wrong. Read them and then either build on them or argue with
them explicitly. Do not quietly re-decide something they settled, and do not
inherit something they only assumed.

## 0. Read first

Text in Rust, as it stands:

- `Barcode/Software/Apps/CustomGUI/rust/src/lib.rs` — the first port. The
  block comment above `type Font` and the one above `render_smoothed()` are
  the design record: why `u8g2-fonts`, why the `_tr` tier, why a face one size
  class up is rendered into a scratch buffer and area-averaged down to four
  levels, and why rasterizing the original TTF with `ab_glyph` was shelved.
  `word_wrap()`, `measure_width()` and the bearing fix in `render_smoothed()`
  are the API surface every app has needed so far.
- `Barcode/README.md` §"The GUI is Rust, through CustomGUI, not TouchGFX" —
  the footprint table against the TouchGFX build it replaced, the three size
  optimisations and what each cost in fidelity, and the open item: the bezel
  fit was never re-measured against the new fonts.
- `Barcode/README.md` §"Fitting the id on a round screen" — the disc-mask
  measurement method, the three-tier layout and why the id is measured rather
  than counted. Written for TouchGFX metrics; the method is what transfers.
- `Spin/Software/Apps/CustomGUI/rust/src/lib.rs` — the second port, and the
  more evolved one: `Face` (a source face plus a destination height), three
  source faces standing in for eight, a 63 px numeric face. `Spin/README.md`
  §"Text, and why it is not as rough as it should be" has the reasoning and
  §"Tests" the `nothing_is_drawn_outside_the_bezel` test.
- `NotifyToggle/Software/Apps/CustomGUI/rust/src/lib.rs` — the third: plain
  `embedded-graphics` `MonoTextStyle`, fixed-width 1bpp, no smoothing. The
  floor, and a reminder that a small app should not have to pay for the
  ceiling.
- `RustGuiPoc/Docs/FINDINGS.md` — the platform as measured on hardware: tick
  rate, push cost, RAM window, code-from-RAM, the dark-on-light dropout, the
  toolchain traps. Every number you would otherwise guess is here.
- `RustGuiPoc/README.md` §"The panel drops dark-on-light text".

Text under TouchGFX, which is the bar:

- `Squash/Software/Apps/TouchGFX-GUI/generated/fonts/src/` — what TouchGFX
  actually shipped on this platform: `Font_Poppins_*_2bpp_*.cpp` glyph
  bitmaps, `Table_*.cpp` glyph nodes, `Kerning_*.cpp`. **2bpp**, which is the
  panel's own depth. Read one glyph and one table row before designing a
  format; the answer to "what does a pre-rendered 2bpp glyph cost" is in
  these files, not in an estimate.
- The Poppins sources: `git show b0cf873^:Barcode/Software/Apps/TouchGFX-GUI/assets/fonts/`
  has `Poppins-Regular.ttf` and `Poppins-SemiBold.ttf` (SIL Open Font
  License). Other weights are in `SensorLab`'s history at `aedd9a6`.

The shared directory, as a precedent:

- `MapKit/README.md` §"Hard-won constraints this code is shaped around" — `../SharedData/` is the path that resolves, absolute paths
  never do, a CRC over 45 MB on the GUI thread froze it for ~10 s and a
  201 MB one tripped the watchdog and restarted the watch, and the tile cache
  is a static so the linker arbitrates RAM.
- `MapManager/README.md` — a background app that discovers, CRC-verifies and
  trust-marks files dropped into `SharedData/maps/` over USB, so consumers
  never verify on their own thread. If fonts go to the filesystem, this is the
  pattern for trusting them, and it is already built.
- `Barcode/Software/Apps/CustomGUI/Gui.cpp` — `mKernel.fs.file()` used from a
  GUI process for `last_code.txt`: the GUI half can read and write files.

The SDK, and the research beside it (`~/git/una-sdk`, and the `research`
branch checked out at `~/git/una-sdk-research`):

- `Libs/Header/SDK/Interfaces/IFileSystem.hpp` — the whole file API:
  `open`/`read`/`write`/`seek`/`getPosition`/`size`, all synchronous, all
  returning a bare `bool`. `skMaxPathLen` is 256.
- `Libs/Header/SDK/Interfaces/IAppMemory.hpp` and
  `Libs/Source/AppSystem/system.cpp` — the kernel's `malloc`/`free`/`realloc`
  that `operator new` forwards to, and the `_sbrk` stub that asserts. This is
  what "no heap" actually means here; read it before repeating the claim.
- `Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp` — the entry point
  placement-news `Gui` into static storage: no heap by construction, not by
  impossibility.
- `cmake/una-app.cmake` and `Libs/Source/AppSystem/linker/Main/Sections.ld` —
  `UNA_APP_GUI_RAM_LENGTH` 600K, **GUI stack 10 KiB**, `-mfpu=fpv5-sp-d16
  -mfloat-abi=hard`, and one memory region: `.text`, `.rodata` and every
  TouchGFX font section land in `RAM`.
- `git show docs/shared-data-directory:Docs/shared-data.md` — the shared
  directory's contract: the `../SharedData/<name>` whitelist, nesting, the
  ten-open-files budget, `rename()` not replacing, `mkdir` before `open`.
- `una-sdk-research`: `RESEARCH-INDEX.md`;
  `Docs/Research/2026-08-13-watch-cartography-prior-art.md` §"Legibility" —
  the measured size floors for text on this glass and the explicitly open
  question about anti-aliasing at two bits a channel;
  `Docs/Investigations/2026-08-12-map-e2e-run/README.md` finding 24 — why
  anti-aliased edges land off-palette; and
  `Docs/Investigations/2026-08-05-rawtiles-device-proof/README.md` — the GUI
  process reading files on hardware, with timings. Neither repo has any
  prior work on fonts as such: no glyph-atlas experiment, no TTF parsing, no
  font tool of the SDK's own. TouchGFX Designer, Windows-only, is the only
  font pipeline that has ever existed here.

Repo conventions:

- `CLAUDE.md` — the comment rule, "prefer measurement over assertion", where
  testable logic goes, conventional commits.
- `Barcode/Docs/QR-PROMPT.md` and `Spin/Docs/ENTRY-SCREEN-PROMPT.md` — the
  register this document is written in and the one expected back.

## 1. Hard constraints

Established, with sources. Do not re-derive them from guesses and do not
silently contradict them; if one is wrong, say so and say how you know.

**The panel.** Sharp LS012B7DD06 (the `UNAview_LS012` board in
[UNAWatch/una-hardware](https://github.com/UNAWatch/una-hardware)), spec
LD-29652B Table 3-1: 240×240 dots, **0.126 mm pitch** (about 202 dpi),
⌀30.24 mm active area, Normally Black, reflective with slight transmission,
**64 colours — "1 pixel has RGB each 2bit"**. The framebuffer is one byte per
pixel, `ABGR2222`; a channel truncates to its top two bits, so the only greys
are **0, 85, 170, 255**. Anti-aliasing here means choosing among two
intermediate shades, not 254.

**It is round.** Only the inscribed circle is lit. `BarcodeLayout::pixelIsLit`
models it as a pixel whose centre is within 119.5 pitches of the centre, so the
widest row is 238 px and the largest inscribed square is about 169 px. The
chord narrows fast towards the top and bottom — Spin's clock rows get 218 px.
A text box inset from the *buffer's* edge looks fine in every simulator and
loses its last glyph on the glass; two shipped apps did exactly that.

**The glass eats thin dark strokes on light fills.** Measured on hardware
(`RustGuiPoc/Docs/FINDINGS.md` §"The panel"): bright glyphs on black render
crisply; a black-text-on-white readout came back as a blank white band. This
is the panel, not the framebuffer, so no simulator will show it. Any quality
claim must hold for bright-on-dark, and any claim about dark-on-light must
come from a device capture.

**Size floors, measured on this glass.** The cartography research
(`2026-08-13-watch-cartography-prior-art.md`) puts the panel at 1.44 arc
minutes per pixel at wrist distance and records: an 11–12 px em is
"detection, not reading — confirmed unreadable"; 22–26 px em is "confirmed
readable on device"; 28 px is "comfortable". The FAA's 9×13 px minimum
glyph matrix is the standards floor it cites. It also names the open
question this task must close: **no study exists of anti-aliased text at
about 200 ppi with two bits a channel**, and it proposes the experiment —
hard-aliased, anti-aliased-then-quantised, and anti-aliased with a
deliberately chosen two-shade fringe, at 22 and 26 px, on the panel.
Barcode and Spin draw 16–18 px labels today, below the readable floor that
research measured; whether they are legible is a question for a photograph.

**The MCU.** `UNAcore` Rev 3.2's BOM names an **STM32U5A5QJI6Q** (Cortex-M33,
4 MB internal flash, 2.5 MB SRAM), and the research branch confirmed U5A5
three independent ways from a teardown; `RustGuiPoc` and the SDK's own docs
say U595, and are wrong. The BOM says 160 MHz; the actual clock has not been
read back from the RCC and is unverified. The core has a single-precision
FPU — the SDK compiles with `-mfpu=fpv5-sp-d16 -mfloat-abi=hard` and the
Rust target is `thumbv8m.main-none-eabihf` — so `f32` is hardware and `f64`
is software; a rasterizer should say which it uses. There is **no external
NOR flash and no PSRAM**; the only storage beyond the die is a 4 GB Kingston
eMMC, which is the filesystem the watch exposes over USB. So glyph data
cannot be memory-mapped from anywhere: every byte is either resident in RAM
or fetched through the filesystem API.

**RAM, and code is RAM.** The GUI process has a 600 KiB window and executes
from it, so `.text` and `.bss` come from one budget and every byte of glyph
data displaces something else. The framebuffer is 57,600 bytes of that.
**The GUI stack is 10 KiB** (`UNA_APP_GUI_STACK_SIZE`), so a rasterizer's
accumulation buffer, a glyph cache or a wrap table cannot live on the stack;
they are statics, and the linker arbitrates them at build time — MapKit's
rule.

**There is a heap, and it is the kernel's.** `IAppMemory` exposes
`malloc`/`free`/`realloc`, `operator new` forwards to it and `_sbrk` asserts.
No Rust crate here uses it: all three ports are `no_std` without `alloc`, and
Barcode's README records why — an allocator was thought to need a
hand-written `critical-section` for an undocumented interrupt architecture.
That reasoning was about `embedded-alloc`, not about this platform: the GUI
is one synchronous thread, a `GlobalAlloc` can forward to `kernel.mem`
through a C shim or run a bump allocator over a static arena, and neither
needs a critical section. If you take a heap, say which, what it costs, and
how a leak or fragmentation across ten thousand ticks is ruled out.

**Time.** The GUI ticks at 10 fps (median gap 100 ms, measured), pushing a
frame costs 21–24 ms, rendering is software only, and every push is a whole
frame. That leaves roughly 78 ms per frame for everything, and a per-pixel
pass over the panel fits comfortably. Text is not the bottleneck now; a design
that rasterizes outlines on demand must show it stays that way for the worst
screen — a full prompt wrapped to four lines, or a 63 px clock — with a
measurement, not a cycle estimate.

**Filesystem.** Paths are relative to the app's own directory;
`../SharedData/<name>` is a whitelist the kernel resolves to `Apps/SharedData/`
on the volume, nesting allowed, and every other `..` segment is rejected.
Absolute, volume-prefixed paths never resolve on hardware even though the
SDK overview still advertises them. `IFile` has `seek` and `getPosition`;
every call is synchronous on the calling thread and returns a bare `bool`,
so absent, locked and too-many-open are indistinguishable. **The whole watch
may hold ten files open at once**, shared with the system log, the activity
recorder and every other app; the eleventh fails whoever asks. `rename()`
will not replace an existing file, so write-temp-and-rename is unavailable.
Measured from a GUI process on hardware: **one 64 KiB `read()` takes 7–9 ms**
cold or warm; sustained sequential read is **about 2.9 MB/s** (MapManager,
160.5 MiB in 56.6 s). Nobody has measured a small read at a random offset,
which is the read a glyph store would make. **Measure it on the watch**
before designing around it. Files arrive by USB copy onto an exFAT volume the
phone also syncs over BLE; a file can be present and half-written, which is
why MapManager checks `(size, crc)` and re-verifies when the size changes.
The simulator does not enforce the whitelist, so a path that works there can
fail on the glass.

**The C ABI is hand-maintained.** Rust and C++ each declare the frame struct
and `offset_of!` assertions plus a startup fingerprint keep them honest.
Anything you pass across it — a string, a font handle, a glyph-cache pointer —
joins that contract.

**Today's text is ASCII by construction.** Barcode's phone form validates
names against `[ -~]{0,12}`, every prompt is a fixed English literal, Spin's
config is numbers and booleans. There is currently nothing a wider character
set would draw. That is the gap to close, not a reason to stop at ASCII: a
wearer called José or Zoë, a UI literal in another language, and a forwarded
notification are all things this platform will be asked for.

## 2. What exists, and what it costs

Confirm these rather than trusting them; they are what the design must beat.

**The supersample-and-shrink approach (Barcode, Spin).** `u8g2-fonts` ships
fixed-resolution 1bpp bitmap faces with a proportional-width measurement API.
Each string is rendered from a face one size class larger than wanted into a
static scratch buffer, then each destination pixel's block of source pixels
is averaged and the coverage mapped to one of four levels. What it costs and
what it does:

| | Barcode | Spin |
|---|---|---|
| source faces shipped | `helvB24_tr`, `helvR24_tr` | `fub49_tn`, `helvB24_tr`, `helvR24_tr` |
| on-screen heights | 24, 18 | 54, 44, 32, 25, 24, 18, 16 |
| scratch buffer (`.bss`) | 384×40 = 15,360 B | 384×70 = 26,880 B |
| glyph coverage | ASCII 32–127 | ASCII; `_tn` digits only |

Two u8g2 faces came to about 8.7 KB of `.text` (the "one font pair" note in
Barcode's README), and switching `_tf` to `_tr` saved about 7.3 KB more. The
whole Barcode GUI is 42,488 bytes of `.text` against TouchGFX's 84,712.

Its known weaknesses, all recorded in the two READMEs:

- Widths are the source face's width scaled by a height ratio (24/32, 9/16),
  not the destination's own metrics. Layout thresholds were re-derived
  empirically and the bezel-fit measurement was never redone.
- The greys come from area-averaging a 1bpp source, so the shape is
  Helvetica's at 32 px shrunk, not a glyph designed or hinted for 18 px.
  Against real device captures the mean pixel difference is 4.9–5.2 of 255,
  a fidelity-to-the-old-build number, not a quality number.
- One scratch buffer per app, sized by hand from the widest string each app
  happens to draw, with `.clamp()` calls that hide clipping rather than flag
  it.
- Every app carries its own copy of the renderer and its own font tables.
  Nothing is shared, and the two copies have already diverged (Spin's `Face`).

**TouchGFX's pre-rendered 2bpp Poppins (Squash, and Barcode before the
port).** Measured from the generated sources:

| face | glyphs | glyph bytes | kerning pairs |
|---|---|---|---|
| Poppins Regular 16 | 95 (U+0020–U+007E) plus U+2026 | 2,094 | 0 |
| Poppins SemiBold 20 | 95 | 3,426 | 0 |
| Poppins Medium 50 | 95 | 19,217 | 0 |

Plus one 16-byte `GlyphNode` per glyph (`Table_*.cpp`: data offset, code
point, width, height, top, left, advance, kerning index and size), so about
1.5 KB of nodes per face; whole faces come to 4,177 bytes (Regular 18),
4,972 (SemiBold 20) and 30,580 (SemiBold 60). So a real, designer-rasterized,
anti-aliased 20 px face at the panel's own depth is **about the size of one
of the 1bpp 32 px source faces the shrink trick needs**, and it is the thing
the shrink trick is approximating. Note what TouchGFX did *not* do here: no
kerning pairs were emitted, coverage was ASCII plus ellipsis with `?` as the
fallback glyph, the designer file asked for 4bpp and the generator emitted
2bpp, and the stock Running app ships **18 faces — about 178 KB, 30% of the
GUI window — for ASCII**. TouchGFX also ships, unused by every app here, the
machinery for fonts loaded from a file (`binary_fonts`, `UnmappedDataFont`,
`CachedFont`, `FontCache`); its on-disk format is one more precedent to read
before inventing one. Do not assume its converter is the ceiling either.

**`MonoTextStyle` (NotifyToggle).** Fixed-width, 1bpp, ASCII, a few hundred
bytes per face, no smoothing. Right for a two-word toggle; wrong for anything
proportional.

## 3. What "text" has to mean here

Before choosing a mechanism, write down what it must do. At minimum:

**Inventory the strings.** Every string each Rust GUI draws, with its face,
size, weight, alignment, colour and box: Barcode's id tiers, caption, pager,
prompts; Spin's clock, numbers, labels, headings, answers; NotifyToggle's
two. Then the strings that do not exist yet but plausibly will: a name with a
diacritic, a UI literal in a second language, a forwarded notification, a
unit symbol (°, ′, µ, ±), an ellipsis. The inventory decides the size and
weight ladder, which decides most of the footprint.

**Define coverage tiers and cost each one.** Tier them so the cost of every
step is a number, not a feeling:

| tier | what it adds | who needs it |
|---|---|---|
| 0 | printable ASCII | every app today |
| 1 | Latin-1 Supplement + Latin Extended-A, ° ′ µ ± … – — ‘ ’ “ ” | European names, units |
| 2 | Greek, Cyrillic | UI in more languages |
| 3 | CJK Unified Ideographs, Hangul, Kana | notifications; thousands of glyphs |
| 4 | scripts needing shaping — Arabic, Hebrew (RTL), Devanagari | a text shaper, not just glyphs |
| 5 | emoji | colour, on a 64-colour panel |

For each tier, cost the glyph data at each size on the ladder, in bytes, for
each candidate architecture in §4. State where tier 3 stops being a table in
`.text` and becomes a file. Tier 4 needs a shaper; `rustybuzz` and
`harfbuzz` are the honest references, and a shaper without `alloc` is a
research project — if you cannot cost it, say so and draw the line there.
Tier 5 is probably "don't", but say why with numbers. Poppins itself stops
at tier 1 (§4 C), so tiers 2 and up also name a second face and cost its
licence, its metrics mismatch against Poppins, and its file.

**Text is Unicode, so decide what the pipeline does with it.** UTF-8 in;
normalisation (a `é` may arrive as one code point or two); combining marks;
a missing-glyph policy. Barcode's ethos is refuse rather than draw something
plausible-but-wrong; decide whether a missing glyph draws tofu, falls back
to another face, or refuses the string, and make it one rule for every app.

**Fix the API the apps actually need.** From the three ports: measure a
string's advance and ink bounds; draw at a baseline or top with left, centre
or right alignment; greedy word-wrap against a pixel width; a face ladder
(preferred face if it fits, else smaller, else split); clip to a box or to
the lit disc; colour. Kerning is in scope if the face has pairs; Poppins
does. Decide what is a crate API and what stays per-app.

## 4. Candidate architectures — cost all of them

Cost each against the three goals with the same yardsticks (§7, §8), on the
same string inventory, and keep the losers in the write-up with their
numbers. At least these:

**A. Status quo: 1bpp u8g2 faces, supersample-and-shrink.** The baseline.
Already measured; re-measure on the shared inventory so the table is fair.

**B. Pre-rendered 2bpp glyph atlases, generated at build time from the TTF.**
What TouchGFX did, done in Rust and Python: a host tool rasterizes Poppins at
each size on the ladder with a real outline rasterizer, quantises coverage to
four levels (choose the thresholds deliberately — see §7 on gamma), packs
glyphs with a per-glyph node and optional kerning, and emits a `static` the
crate indexes. 2bpp is the panel's depth, so nothing is lost after
quantisation. Cost is (glyphs × ink area / 4) per size, and the sizes
multiply. Answer: how the tool is reproduced (checked-in generator, pinned
rasterizer, deterministic output), how a face at a size not on the ladder is
handled (not at all, or by nearest-and-scale), and where the ladder stops
being affordable in `.text`.

**C. Runtime outline rasterization from a TTF.** `ttf-parser` is `no_std` and
allocation-free for parsing; the rasterizer is the question.
`ab_glyph_rasterizer` accumulates into a `Vec`; `fontdue` and `swash` need
`alloc`; a scanline coverage rasterizer over a fixed static buffer is a few
hundred lines. Cost: the font file, the rasterizer code, a glyph cache so a
string is not re-rasterized every tick, and time per glyph on the target.
The file is smaller than it looks. Measured with `pyftsubset` on
Poppins-Regular from git (158,188 bytes, 1,059 glyphs, no hinting
instructions, kerning in `GPOS`):

| subset | code points | bytes |
|---|---|---|
| ASCII | 95 | 7,896 |
| ASCII + Latin-1 + Latin Ext-A + common punctuation | 329 | 20,240 |

So one 7.9 KB file covers every size and weight-by-scaling of tier 0, against
3.4 KB *per size* pre-rendered. Reproduce these numbers before relying on
them, and add the `GPOS` kerning subtable if kerning is kept. Note also that
**Poppins has no Greek or Cyrillic** (one Greek code point, zero Cyrillic in
its `cmap`), and does carry Devanagari — tier 2 means a second face whatever
the mechanism, and choosing it is part of the job. Benefit: any size, any weight in the file, and the only
route to arbitrary coverage without pre-rendering every code point at every
size. The core has a single-precision FPU (§1); keep the rasterizer in `f32`
or fixed point and check the build never pulls in `f64` soft-float.

**D. Hybrid: small resident face plus a glyph store on the filesystem.** Tier
0 or 0+1 in `.text` so an app can always draw something; wider coverage as a
pre-rendered 2bpp glyph file per face and size in `../SharedData/fonts/`,
indexed for random access, read on demand into a small static glyph cache,
trusted the way MapManager trusts a map pack. Cost: the cache (RAM), the
index read (time and bytes), the read latency of one glyph on the GUI thread
at frame time, and the provisioning story — who puts the file there, what
happens when it is missing, stale, or half-copied. This is the only shape in
which tier 3 is plausible; work out whether it is also the right shape for
tiers 1 and 2 or whether those simply belong in `.text`.

**E. A TTF on the filesystem, rasterized at runtime.** C with the font moved
out of RAM. The read pattern matters: outline data for one glyph is a small
read at a random offset, and whether that is 1 ms or 30 ms on this
filesystem decides the design. Measure it.

**F. Anything else you can justify** — signed-distance fields (probably
wrong at four levels and 16 px, but say why), stroke fonts, a subset of
TouchGFX's own tables reused verbatim, the kernel's own font service if one
turns out to be exposed to apps (check the SDK; do not assume either way).

For every candidate, produce the same row: `.text` bytes, `.bss` bytes, file
bytes on the watch, worst-case render time for the inventory's heaviest
screen, coverage tiers reachable, quality score (§7), and the risks that are
not numbers (a heap, a half-copied file, an undocumented interrupt
architecture).

## 5. If data leaves the binary

`../SharedData/` is the only place it can go. Before spending it:

- **Establish the read path from a GUI process on hardware.** Open, seek,
  read a few hundred bytes at an offset, close; time each with the kernel
  clock over a few hundred repetitions; report the distribution, not the
  mean. MapKit's numbers are whole-file sequential; yours will not be.
- **Never verify on the GUI thread.** A whole-file CRC at 2.9 MB/s is 350 ms
  per MB; MapKit froze for ten seconds and MapManager exists so no one does
  that again. Reuse its trust marker, or justify a second mechanism.
- **Design for absence.** The file is missing on first install, during a USB
  copy, and after a phone sync that touched the volume. Every screen must
  still draw with the resident face, and the fallback must be visible in a
  test, not discovered on a wrist.
- **Version the format** with a magic, a version, and the `(size, crc)` guard
  MapManager already uses, and say what an older app does with a newer file.
- **Say who writes it.** Today the only route onto the watch is a USB copy
  by the owner. Cost what a phone-side or install-time route would need, and
  do not build one unless the design cannot work without it.
- **Respect the directory's contract.** `mkdir("../SharedData/fonts")`
  before the first `open`; the full path under 256 bytes; no
  write-temp-and-rename, so an updater has to cope with a reader seeing a
  half-written file; nothing cleans the directory up, so a font pack outlives
  the app that wanted it; and every open file counts against the watch's ten.
  A glyph store that holds a file open across frames is spending one of them
  for the whole app session — decide whether it may.
- **Budget the read against the tick.** At 7–9 ms per 64 KiB, a cold glyph
  fetch is affordable once, not once per glyph per frame. The cache is the
  design, the file is the backing store, and a screen must never wait on
  more than one miss.

Everything under this heading is optional. If §4 shows tiers 0–2 fit in
`.text` at an acceptable cost and no app needs tier 3 yet, the right answer
may be a format that *can* live in a file and a build that does not yet.

## 6. Design questions to answer before writing code

Answer these in a document, with reasoning and numbers, and get them agreed.

**One crate or three copies?** Every Rust GUI needs this, and MapKit is the
precedent for a shared top-level directory that is not an app. One constraint
is already known: a shared crate must be a Cargo *dependency* of each app's
crate, not a second staticlib linked beside it — two staticlibs each carrying
a `#[panic_handler]` collide at link time. Decide the crate's name, location,
what it owns (fonts, rasterizer, layout, cache) and what it must not
(screens, the C ABI).

**Which faces, which sizes, which weights.** Poppins is what the wearer has
seen and what TouchGFX is measured with, but justify it against the panel:
a geometric sans with thin joins may be exactly what the glass drops at small
sizes, and a face with a taller x-height or heavier hairlines may read better
at 16 px on 202 dpi with four greys. If you keep Poppins, say what you
measured; if you change, say what you measured.

**Hinting and grid-fitting.** At 16–24 px with two intermediate greys,
unhinted outlines blur stems across two columns at 50% each, which the panel
renders as two 85-or-170 columns and the eye reads as bold and soft. Decide
whether the tool snaps stems (TrueType hinting instructions, autohinting, or
simple vertical stem alignment), and show the difference in a capture.

**Gamma and the four levels.** Linear coverage of 0.33 is not the grey that
looks a third on; the reflective panel has its own curve and it has never
been measured. Decide the coverage-to-level thresholds by measurement on the
glass, not by rounding, and record the numbers and the photograph.

**Bright-on-dark only, or both?** The dropout finding argues for a rule.
Decide whether the crate refuses dark-on-light, permits it above a stroke
weight, or leaves it to the app; whichever, put the measurement behind it.

**What happens at a missing glyph, an unfittable string, a too-long string.**
One rule each, shared by every app, tested.

**Does anything need a heap?** If yes, choose between a bump allocator over
a static arena with a documented reset point and a `GlobalAlloc` over the
kernel's `malloc`, say why, and measure what it costs — including whether
the kernel allocator's memory is inside or outside the 600 KiB window, which
nobody has checked.

**What the C ABI carries.** Strings are already copied into fixed arrays in
the frame struct. Decide whether that stays, whether a font choice crosses
the ABI, and whether a glyph store handle does.

## 7. Quality: measure "best looking"

The panel makes this measurable, because there are only four levels:

- **Reference rasterization.** For each string in the inventory, rasterize
  the TTF at the target size at high resolution with a trusted rasterizer,
  box-filter to the panel grid, quantise to four levels with the chosen
  thresholds. This is the ideal any pre-rendered or runtime path is compared
  against. Mean and max pixel difference, per string, per candidate.
- **Device captures.** Framebuffer dumps from the watch (RustGuiPoc's long
  press writes `fb_dump.bin`) and photographs of the glass in daylight and
  under the frontlight. The framebuffer diff proves the code; only the
  photograph proves the glass, and the dropout finding says they disagree.
- **The bezel test.** Every screen, every candidate: no lit pixel outside the
  disc. Spin's `nothing_is_drawn_outside_the_bezel` is the shape; Barcode's
  README has the disc-mask recipe and the ImageMagick `-fx` trap that makes a
  malformed expression read as a pass.
- **The three-condition experiment the research asked for.** The same
  strings at 22 and 26 px, bright-on-dark, drawn hard-aliased, anti-aliased
  then quantised, and anti-aliased with a chosen two-shade fringe; then the
  same at the 16–18 px the apps actually use. Photographed on the glass in
  daylight and under the frontlight. This is the measurement that decides
  whether smoothing is worth any bytes at all on this panel, and nobody has
  taken it.
- **A legibility read.** Put the same screen up in A, B and the best of C–E
  and have someone who has not read this document say which they would rather
  wear. Record what they said; it is evidence, not the decision.

## 8. Footprint and speed: measure both

- `arm-none-eabi-size` and the linker map for `.text`, `.data`, `.bss` of the
  GUI ELF, per candidate, on the same inventory; the packaged `.uapp` size
  beside it. Barcode's README has the table format.
- Render time per string and per worst screen, measured on the watch with
  the kernel clock, not estimated. The budget is the 78 ms of slack in a
  100 ms tick.
- For filesystem candidates, the read timings from §5.
- Show the marginal cost of each coverage tier and each size on the ladder
  separately, so the next person can decide to drop one with a number.

## 9. Shape of the deliverable

1. **A design document first**, in the register of `Barcode/Docs/SYMBOLOGIES.md`:
   the constraints, the inventory, the candidates with their measured rows,
   the decision and its reasoning, the rejected paths with their numbers.
   Agreed before the crate is written.
2. **A shared crate**, `no_std`, with a host test suite that renders every
   inventory string, checks the bezel, checks coverage tiers, and diffs
   against the reference rasterization; and a host simulator path like
   Barcode's `sim` binary.
3. **A host tool**, checked in with pinned dependencies and deterministic
   output, that turns a TTF into whatever the crate consumes — and, if §5
   is used, into the on-watch file, with its CRC.
4. **Barcode, Spin and NotifyToggle ported to it**, one commit each, each
   with its own before/after footprint row and its device captures. If an
   app gets bigger, say so in the commit.
5. **A README for the crate** that is the design record: what the panel is,
   what was measured, which numbers would falsify which decisions.

## 10. Build, test, deploy

No `cmake` or ARM toolchain on the development Mac; everything is in Docker,
run by image ID (the tags are local-only and `docker run` by name tries to
pull). Mount the repo at `/apps` and the SDK at `/sdk`. `Barcode/Docs/QR-PROMPT.md`
§7 has the commands, the install procedure (delete the old `.uapp`, turn off
BLE sync, strip xattrs, verify after remount) and the simulator traps. The
Rust target is `thumbv8m.main-none-eabihf`; `Barcode/README.md` §"Building"
has the toolchain setup. `cargo test --features std` runs a crate's host
tests; the render tests share one static scratch buffer, so read the note on
`SS_LOCK` before adding parallel tests. The SDK's simulator does not run
CustomGUI apps at all, so the crate's own `sim` binary is the only preview
and the watch is the only truth.

Let cargo decide when to rebuild; a CMake rule with `OUTPUT` and no
`DEPENDS` shipped a stale renderer once. Build against the SDK version the
watch runs, not newer — the kernel refuses a newer app silently at install.

## 11. Conventions

Conventional commits, one logical change per commit, scoped to the app or to
the new crate; CI cuts versions from the commit type, so never bump
`appVersion` by hand. Comments follow `CLAUDE.md`: one sentence, owned by the
file it sits in, with the narrow carve-out for hardware behaviour proven on
the watch, frozen formats and measurements — each naming what would falsify
it. The long-form reasoning goes in the README, not the source.

## 12. Non-negotiables

1. **Only assert what you have measured or can cite.** This repo has twice
   shipped confident numbers that were wrong; both were caught by measuring.
   A claim about how text looks on the glass needs a photograph of the glass.
2. **Nothing lit outside the disc, on any screen, in any candidate.** Tested,
   not eyeballed.
3. **Every app still draws every string it draws today**, byte-for-byte
   identical inputs, and looks at least as good on the watch. Show the
   before/after captures.
4. **The footprint of each coverage tier and each size is visible**, so the
   trade is made with numbers by whoever makes it next.
5. **Do not build the filesystem path unless the design needs it.** A format
   that could live in a file is fine; a provisioning pipeline nobody uses is
   not.
6. **If the answer is "the status quo, with one shared crate", that is
   acceptable** — provided it is the answer the measurements gave, and the
   rows for B through E are in the document beside it.
