# InscribedDisc — design record

What was measured, what was decided, what was rejected and why, and which
numbers would falsify which decision. The crate's own README is one level up and
is for someone consuming it; this is for someone changing it.

---

## Status

**Cut back to what has callers.** Five modules were written against a list of
screens nobody has built yet and are parked — see
[What is parked](#what-is-parked). What ships is the surface, the colour, the
geometry, the widgets and the preview: about 1,500 lines, with `Spin` as the one
adopting app in this repository.

`Spin`'s 43 scene frames are byte-identical after the move, which is the bar for
an app already installed on wrists, and it has since recorded a session on one.
What is still missing is listed rather than buried: see
[What is not built](#what-is-not-built).

---

## 1. The survey, reproduced

The case for the kit rests on a duplication census. Every figure below was
recomputed from the seven crates rather than inherited, and **four of the
claims that motivated this work turned out to be wrong**. They are corrected
here because a kit justified by a wrong number is a kit nobody can argue with.

### What is duplicated, confirmed

| block | finding |
|---|---|
| `FrameBuf` + `OriginDimensions` + `DrawTarget` | **byte-identical in all seven**, 30 lines each — MD5 `bbcf04f1…` across every crate |
| `Abgr2222` and its shift constants | identical in all seven bar the palette constants each app declares (2, 2, 2, 5, 5, 5, 5) and where the `struct` line sits |
| panic handler | 12 lines in five crates, 6 in two — see the drift below |
| `Buf<N>` formatting buffer | 27–29 lines in four crates |
| C ABI mirror | 224 lines across six headers plus 143 in seven `lib.rs` files, **367 total** |
| `queryDisplayConfig` | present in all six shells |

The ABI figure of 367 is higher than the 244 the survey quoted, because it
counts field declarations, `offsetof`/`offset_of!` assertions and fingerprint
walk lines on both sides. `Spin` dominates either way: 73 header lines and 42
Rust ones, 115 for one app.

### The drift, confirmed

Two findings, and both are the kind that only a shared crate fixes:

- **The colour-depth guard exists in two of six shells.** `mDisplayUsable` /
  `kMaxBitsPerPixel` — which withholds the frame when a panel reports more than
  8bpp rather than letting the kernel read past the framebuffer — is in
  `NotifyToggle` and `UnitToggle` only. `RustGuiPoc`, `QrGuiPoc`, `Spin` and
  `Barcode` do not have it.
- **The two most complex shipping apps have the worst panic diagnostics.**
  Five crates capture `file:line` and the message; `Spin` and `Barcode` send
  the literal `b"panic"`, having been forked before the improvement landed. The
  five that are right differ from each other *only* in the extern symbol name,
  which is precisely what a kit removes.

### Corrections

**1. The three page indicators do not disagree about centring.** The survey
said two of `RustGuiPoc::page_dots`, `Barcode::draw_pager` and
`SettingsEditor::draw_position` differ about whether the mark's own width
belongs in the centring. Computing the ink extent of all three at counts 1–8
says otherwise: every one is centred, because each is consistent with its own
coordinate convention — `RustGuiPoc`'s helper takes a *centre*, so the width
cancels; the other two take a *top-left*, so it appears explicitly. `RustGuiPoc`
and `SettingsEditor` produce byte-identical extents.

There is a real defect, and it is smaller and different: `Barcode` truncates
the whole quantity in one division, `(panel − total) / 2`, where the others
truncate each term. At even counts that puts its row **half a pixel left of
centre** — 119.5 against 120.0 at counts 2, 4, 6 and 8. The kit takes the
per-term form and `a_row_is_centred_at_every_count` holds it.

**2. The arc step is 0.65, not 0.75.** `Spin` ships `ARC_ANGULAR_PX = 0.65`;
its own comment records 0.85 → 95 unpainted pixels, 0.75 → 4, 0.70 clean, and
says 0.65 is "that with margin". The kit takes 0.65 and re-measures it — see
§6, where reproducing this found a bug in the kit's own trigonometry.

**3. No row of this panel is 129 pixels wide.** The brief cites a lit chord of
"129 px at row 220". Every chord on a 240-wide panel is **even**, because the
disc is symmetric about x = 119.5 and a lit pixel at `x` implies one at
`239 − x`. The measured value at row 220 is **130**; row 222's 122 is right.
`every_chord_is_even` is now a test, so an odd chord quoted for this panel
fails rather than propagating.

**4. The largest inscribed square is 168, not 169.** Side 169 puts its corner
at 169² + 169² = 57,122 against the disc's 239² = 57,121 and misses by one.
The kit searches against the predicate rather than computing `floor(d/√2)`,
which is off by exactly this.

Two smaller notes. The seven `lib.rs` line counts are each one below the
figures quoted — a uniform off-by-one, cosmetic. And the six `run()` loops share
**29–34 lines pairwise**, not the 48–59 claimed; 56 holds only for the
`NotifyToggle`/`UnitToggle` pair. The shells share a *skeleton*, not half their
body, and the larger win is `queryDisplayConfig`, `renderAndPush` and the panic
trampoline, which live outside `run()`.

### And one the survey could not have known

**`SettingsEditor` did not compile for the watch at all.** Its panic handler
references `Buf::<192>`, which is defined nowhere in the crate and is not
exported by `TextKit`. Because the handler is `#[cfg(not(feature = "std"))]`,
only a device build reaches it, and nothing had ever done one — the app has no
C++ shell, no CMake target and no app id. It fails with:

```
error[E0433]: cannot find type `Buf` in this scope
error: unwinding panics are not supported without std
```

This makes it an even better first adopter than the brief argued: there was
nothing to break because nothing worked. It builds now.

---

## 2. The one number nobody had

> *"whether generic-over-`PixelColor` costs more than the concrete `Abgr2222`
> path on this target. Nothing in the repo knows this, and it is one afternoon
> with two builds and a map file."*

Measured, on `thumbv8m.main-none-eabihf`, with four primitives — a filled
rectangle, a disc-clipped fill, a page-indicator row and a swept arc — written
three ways with only the abstraction differing. Full method in
[`Docs/2026-09-08-generic-vs-concrete.md`](2026-09-08-generic-vs-concrete.md).

| variant | `.text` | vs concrete |
|---|---|---|
| concrete: one colour type, direct byte writes | 884 | — |
| generic over `DrawTarget`, default `fill_solid` | 966 | +82 (+9.3%) |
| **generic, with a `fill_solid` row fast path** | **950** | **+66 (+7.5%)** |
| generic, two colour types in one binary | 1,814 | +930 (+105%) |

**The genericity is affordable and the kit is generic.** Three things fall out:

1. At a single instantiation it costs **66 bytes**, 7.5% of the primitive set.
   An app instantiates exactly one colour type, so that is what an app pays.
2. The 848 bytes a *second* colour type costs is near-linear monomorphisation,
   and no watch app pays it. It is what an off-watch adopter's `Rgb565` build
   would cost in its own binary, which is the right place for it.
3. Supplying `fill_solid` is **16 bytes cheaper** than leaving the default,
   because the specialised body replaces `fill_contiguous`'s machinery rather
   than adding to it — and it restores the row-fill fast path that the default,
   which walks a pixel at a time, gives up. The kit's `Surface` supplies it.

**The falsifier, and it matters.** The first run of this experiment said +710
(+80%), which points at the opposite conclusion — a concrete kit with a generic
veneer. It was wrong: `pub mod concrete` was still compiled into the `generic`
build, so the "generic" figure was generic *plus* concrete. Gating the modules
per feature is what fixed it. Re-measure with the harness in
[`Docs/measurements/genericity`](measurements/genericity), and read the
per-symbol breakdown, not only the total — a contaminated total looks entirely
plausible.

---

## 3. Who owns UI state

**Decided: Rust owns UI state; the C++ shell is a pump.**

Today six of seven apps do the opposite — C++ builds a flat `#[repr(C)]` struct
each tick and Rust draws it, with `Spin`'s 117-line `handleButton()` holding
that app's entire navigation in a file with no host test. `SettingsEditor` went
the other way: `on_button(&mut State, Button) -> Action`, `external_change`,
`step_towards`, all `cargo test`-able without a kernel, and it is the only app
whose navigation is tested at all.

Why the second shape:

- Navigation is logic worth testing, and `CLAUDE.md` already says logic worth
  testing goes where a kernel is not needed to run it.
- A screen stack with history and an animation clock are *state*, and state
  that grows. In C++ it grows untested; in Rust it grows beside a scene table.
- It collapses the ABI (§4).
- It is the only version that can be published. A public crate cannot contain a
  state machine that lives in someone's C++.

The costs, stated rather than discovered:

- The shell still owns every kernel interaction — filesystem, capabilities,
  `SettingsKit`, messages to the Service. So every `Action` Rust returns must be
  something the shell can execute. `inscribed_disc::Action` has four values and
  `Shell::applyAction` switches over them **without a `default`**, so adding one
  is a compiler warning rather than a press that silently does nothing.
- `render()` stops being a pure function of its arguments once Rust holds state
  across ticks, which weakens `render_is_deterministic`. Replaced by the scene
  catalogue: a scene is a named state, the golden is its frame hash, and
  determinism is asserted per scene over the whole catalogue.
- Suspend and resume cross the ABI as events rather than as shell-local
  booleans. `Anim` is documented for both readings — wall-clock, which jumps to
  where it should be, and `Anim::resumed`, which replays.

**What would falsify it:** an app whose navigation genuinely cannot be expressed
without kernel state in the middle of it — where the `Action` enum grows past
roughly a dozen values, or grows a variant carrying a payload the shell has to
interpret. That is the signal the split is in the wrong place. It has not
happened at four values.

---

## 4. The ABI

`FINDINGS.md` is explicit that the hand-maintained C ABI is the weakest part of
the design, that five hand-edits to one struct produced one shipped bug, and
that generating one declaration from the other "would remove the class rather
than guard it".

**Codegen is the second-best answer. The better one falls out of §3: an ABI
that does not grow per app.** With Rust owning state, the C surface is fixed and
the kit declares it once:

- **events in** — tick with a monotonic millisecond count, button `(id, press)`,
  resume, suspend, stop;
- **pixels out** — buffer, length, geometry;
- **data in** — an opaque byte payload the Rust side reads with explicit
  little-endian accessors, rather than a `#[repr(C)]` layout both halves must
  agree about.

There is then no per-app struct layout to mirror: the 367 hand-mirrored lines go
to roughly zero rather than to "generated", and the fingerprint becomes a kit
constant checked once at startup.

**The cost, honestly.** Explicit field reads are more code at each call site
than a struct member access, and they move a compile-time error — a wrong
`offsetof` — to a runtime one, a short payload. Two things keep that safe: the
reader **refuses rather than truncates** when the payload is shorter than the
field being read, and the schema is written once in Rust with the C side
generated from it, so there is no second declaration to disagree.

**The startup fingerprint check stays either way.** It is the only thing that
catches a stale archive against a newer header, since each satisfies its own
compile-time assertions, and a stale archive has already faked a sensor fault in
this repository once.

**Status: designed, not built.** `Shell` takes the fixed event surface and
`Action`, which is the half that matters for the app here; the opaque
payload reader is specified above and not yet written, because no adopting app
needs one until `Spin` moves. Do not read this section as shipped.

---

## 5. Architecture, costed

| | reaches | `.text` measured |
|---|---|---|
| **A. Free functions** — the apps' primitives, deduplicated | §4 items 6–10, none of 1–5 | tier 0+1: **1,254** |
| **B. Immediate mode with focus** — Kolibri's model minus smartstate, focus instead of a pointer | 1–3, 9 | +tier 2: **1,516** total |
| **C. B plus a screen stack and an animation clock** | all but 11, 12 | +tier 3, inside the same 1,516 |
| **D. Retained widget tree** — TouchGFX, Slint | everything | rejected |

**D is rejected on a hardware fact, not on taste.** `RequestDisplayUpdate`'s
`x/y/width/height` are marked reserved: the panel takes whole frames only. A
retained tree exists to know what changed so that less can be redrawn, and here
there is nothing to invalidate and nothing to gain. Kolibri's most complex
subsystem — "Smartstate", claiming 15× on SPI — is machinery for a problem this
panel does not have. That is why this kit can be a fraction of its complexity
without being less capable *on this class of device*, and the crate documentation
says so out loud because it is the reason the footprint argument survives.

**A as the floor, B and C as features** — and the tiering is measured, not
asserted:

Harness: [`Docs/measurements/tier-footprint`](measurements/tier-footprint).

Harness: [`Docs/measurements/tier-footprint`](measurements/tier-footprint).

| linked | `.text` | `.rodata` |
|---|---|---|
| surface, colour, geometry | **278** | 0 |
| + widgets | 694 | 0 |

Tier 0 was 184 bytes before `lit_start` became closed-form; the integer square
root that removed the per-pixel scan costs 94 bytes of the 278. That is the
trade, and §6 has the frame times that bought it.

The figures for the parked tiers are in git at `8055796` and are not repeated
here, because nothing links them.

---

## 6. Measured constants, with their falsifiers

Every number the kit carries is re-run by its own tests on every build.

**The arc sample step, and the dither's error bound.** Both were measured and
both are **parked** with the modules that carried them — the counts, and the
tests that re-derive them, are in `src/draw.rs` and `src/dither.rs` at commit
`8055796`. They are recorded in [What is parked](#what-is-parked) rather than
here, because a falsifier this file names must be one a reader can run.

**The lit disc.** A pixel centre within 119.5 pitches, doubled so the half-pitch
is exact in integers — `BarcodeLayout::pixelIsLit`'s rule and
`TextKit::Canvas::is_lit`'s, which agree. Consequences held as tests: the widest
row is **238**, every chord is **even**, rows 70 and 168 hold **218**, row 220
holds **130** and row 222 **122**, and the largest centred square is **168**.
Falsified by a panel of a different size or a display whose glass is not the
inscribed disc.

**Bright on dark.** An early black-text-on-white readout came back from the
glass as a blank white band. This is the glass, not the framebuffer, so no
simulator shows it. Every default in this kit is bright-on-dark, and a
dark-on-light claim needs a device capture.

**The panic handler is two features, because one half is 78% of the cost.**
Linking `Spin`'s real renderer against the `b"panic"` literal it shipped:
`file:line` costs **+706 bytes**, and adding the message takes it to **+3,226**.
The message is `core::fmt`. And a handler that itself uses a panicking
operation — slice indexing, `copy_from_slice` — costs **eight times** what the
same output costs written with iterator zips, because its own panic path drags
the formatting in. [`Docs/2026-09-09-panic-handler-cost.md`](2026-09-09-panic-handler-cost.md).

**`fill_rect` as a direct row fill**, not `Rectangle::into_styled().draw()`,
which pulls in the point-iterator layer once per distinct colour. `Spin`
recorded the reason and the kit keeps it, now also as `Surface`'s `fill_solid`
override — measured at 16 bytes cheaper than the default (§2).

---

## 7. The seam this kit exists to close

In the seven apps, `FrameBuf` clipped to the **rectangle** and
`TextKit::Canvas`, built over *the same buffer* on every text call, clipped to
the **disc**. Two views of one framebuffer with two clip policies, and which one
applied depended on which API had drawn. Nothing was visibly wrong because the
layout constants kept the shapes inside the glass by hand — which is exactly the
arrangement that loses a glyph when a constant moves.

`Surface` owns the clip, and `fill_rect`, `fill_solid` and `draw_iter` all go
through it. `fill_rect_and_draw_iter_paint_the_same_pixels` holds them together.

There is one escape hatch. A text implementation that rasterises through its own
surface type cannot go through `DrawTarget` a glyph at a time and stay
affordable, so `Surface::bytes_mut()` exists, is documented as bypassing the
clip, and says that an adapter using it **must** apply `geometry::is_lit` itself
and must have a test saying so. `SettingsEditor`'s bridge does; the two rules
are identical, which is why the app's own bezel test still passes unchanged.

---

## 8. Reaching the ceiling

> **Facilities named below as `nav::…`, `text::…` and `scene::…` are parked, not
> shipped** — see [What is parked](#what-is-parked). The answers stand as design
> answers; what changed is that none of them earned a caller, which is why they
> are in git rather than in `src/`.

The seven apps share a shape — one flat struct, a `screen: u8`, one screenful of
static content — and it has already run out in `Spin`. What follows is how each
of the thirteen things past that shape is built on this kit. Where something is
answered rather than built, it says so.

1. **A list longer than the screen.** `nav::Focus` over the item count, with the
   caller holding a window offset and drawing `visible` items from it. Focus
   wraps, which with two buttons and no pointer is what stops an overshoot from
   costing a full lap. The scrollbar is the open piece: on a round panel the
   right-hand edge is a curve, so the honest form is an *arc* on the bezel
   diagonal, drawn with `draw::fill_arc` at the measured step, not a straight
   bar clipped by the disc. **Not built** — one caller.

2. **A body of text longer than the screen.** `text::wrap` reports how many
   lines the text *needed*, which may exceed the slots given, so overflow is a
   number the caller sees rather than a line that vanished. The caller owns the
   scroll offset in lines and re-wraps each frame — at 78 ms of slack and a
   greedy wrap this is affordable, and it avoids storing a wrapped copy. At the
   seam the kit draws nothing special; a partial line at the bottom is a line,
   and the pager says there is more.

3. **A screen stack with real back behaviour.** `nav::Stack<S, N>`, built. It
   holds the caller's screen enum *and the focus that screen had*, so
   list → item → confirm → back → back lands on the item that was selected.
   A `screen: u8` cannot express this and neither can a flat struct — that is
   the whole argument for §3. A full stack refuses to push rather than dropping
   the bottom, because navigation that silently forgets where it came from is
   worse than navigation that will not go deeper.
   `back_returns_to_where_you_were` is the test.

4. **A transition.** `nav::Anim` over the tick's millisecond count plus
   `ease_in_out`, with the screen drawn at an offset — **one render of a shifted
   layout**, not two renders and a composite. At 10 fps a transition is three or
   four frames, so a second full render inside 78 ms buys nothing a shifted
   layout does not. **Not built** as a facility: the clock and the easing are,
   the shifted-layout convention is the caller's.

5. **An animation that is not a transition.** The clock is `Anim`, driven by the
   monotonic millisecond count the shell passes on tick — deliberately not a
   tick counter. `EVENT_GUI_TICK` stops arriving while suspended, so a counted
   animation freezes and resumes mid-stride where a wall-clock one jumps to
   where it should be. Both readings are available and documented:
   `progress(now_ms)` jumps, `resumed(now_ms)` replays, and which is right
   depends on whether the wearer was meant to see the start. `progress` is
   tested across the 49-day `u32` wrap.

6. **A chart.** `draw::accumulate_coverage` is the primitive: area-averaged
   resampling, order-independent by construction, which on four levels a channel
   is the only correct way to fit an hour of samples into 200 columns —
   independently rounding each sample's rectangle is how a barcode blurs and how
   a trace grows and loses spikes. Axes and a legible value are
   `text::TextFace` at 22–26 px em. **Not built** — one caller.

7. **A watch face.** `draw::fill_arc` for a battery arc, `fill_circle` for a
   hub, and hands as thin filled triangles. What the kit deliberately does *not*
   have is `Spin`'s zone model, which is that app's and not a kit concept.
   **Not built** — no caller yet, and a watch face is the strongest candidate
   for the second one.

8. **A gradient that does not band.** `dither::dither_rgb(r, g, b, x, y)`, one
   call, and the thing no competing framework on this panel has: TouchGFX has
   zero occurrences of the word "dither" across the entire framework and will
   band a gradient with its own painter. Built.

9. **A modal, and a toast.** Both are a `Stack::push` onto a screen that draws
   over the frame, which the stack already carries. The kit does **not** ship a
   confirm dialog: one caller, no measured reason, and `Spin`'s carries a
   product decision — answers in a face half again the label size "because they
   are the one place where a misread costs the wearer their ride" — that is not
   the kit's to make. Wait for the second caller.

10. **An icon or an image.** From `.rodata` as a byte slice blitted through
    `Surface`, or from `../SharedData/` at frame time — `MapKit` already reads
    files on the GUI thread and `MapManager` already CRC-verifies what lands
    there, so the precedent and the verification both exist outside this kit.
    **Not built** — no caller.

11. **Text the wearer entered.** A four-button character picker: `nav::Focus`
    over an alphabet, `Select` to commit, `Back` to delete. The kit has the
    focus ring and the stack; what it does not have is the alphabet layout,
    which is the actual hard problem and wants a measurement — characters per
    screen against presses per character — that nothing here has made.
    **Not built, and the least ready of the thirteen.**

12. **A string that is not ASCII.** `text::Measure::missing` counts characters
    with no glyph, and the kit takes no view on what to do about it, because the
    two precedents genuinely differ: `Barcode` refuses rather than draw a code
    it cannot promise, `TextKit` draws a hollow box. A widget that cannot refuse
    should at least be able to see the count, and `missing` is on the return of
    every measure and draw so a caller can branch on it.

13. **Live data that is 2 s stale.** The transport bursts about once a second
    regardless of sample rate, with worst observed gaps of 1,272 ms at default
    config and 2,038 ms at 2 s driver latency, so any staleness gate must clear
    **the transport's cadence, not the sample period**. That number belongs to
    the platform, not to a widget, so the kit does not ship a gate — it would be
    one constant pretending to be general. What the kit does is make the
    distinction cheap to express: `NotifyToggle`'s `known == 0` and `SleepLab`'s
    refusal to report unearned figures are both "draw absence, not zero", and
    that is a palette and a string, not a facility. **Answered, not built.**

Items 3 and 5 are the ones a flat struct and a C++-owned state machine cannot
carry, and they are built.

---

## 9. Looking at frames

The kit renders frames to PNG, and the point of doing it here rather than in
each app is fidelity: three preview implementations in this repository each
rolled their own idea of where the glass ends, and one of them was wrong in the
dangerous direction.

**What the image reproduces exactly.** The colours — two bits a channel means
each channel is one of 0, 85, 170 or 255, and a frame decodes to at most 64
distinct triples, which is all the panel has ([`Docs/gamut.png`](gamut.png)
is the lot). The glass — the mask is `geometry::is_lit`, *the same predicate the
renderer clips with*, so an image cannot show a pixel the renderer was forbidden
to draw. And the pixel grid — scaling is integer and nearest-neighbour, because
smoothing would invent intermediate colours the panel cannot produce, and seeing
what four levels does to an edge is the whole reason to look.

Verified end to end on a real 480×480 output file rather than only in unit
tests: two alpha values (0 bezel, 255 glass) and no soft edge between them, zero
colours outside the panel's 64, and exactly 179,232 opaque pixels — 44,808 lit
pixels at scale 2, which is this panel's lit-pixel count.

**The defect this replaces.** `Spin`'s own preview tested `dx² + dy² <= 240²`
where the panel's rule is `239²`, so it showed **436 pixels the watch does not
light**. A preview that is more generous than the glass is the §3 bezel trap
with a picture attached, and `nothing_outside_the_glass_is_ever_opaque` is now
the test.

**What no image can reproduce**, stated in the module docs so nobody quotes a
screenshot as evidence for it:

- The panel is **reflective, not emissive**. Contrast and how a shade reads
  outdoors do not transfer from a monitor.
- **Dark thin strokes on light fills drop out.** Measured on hardware; the
  preview will happily show text the watch will not. A dark-on-light quality
  claim needs a device photograph.
- **Size.** 240 px at 0.126 mm is about 30 mm across, so a 1:1 image on a 96 dpi
  monitor is roughly twice life size and flatters legibility.

[`Spin/Docs/contact-sheet.png`](../../Spin/Docs/contact-sheet.png) is all 43 of its scenes on
one sheet — 586 KB, which is what 43 real frames costs, and the reason the
cheap regression check is the golden *hashes* beside it rather than the image.

## 10. Publishing: the split

**`inscribed-disc` (public):** `no_std`, no allocator, generic over
`PixelColor` and `DrawTarget`, with the round geometry, the two draw targets,
the low-bit-depth palette and the PNG preview.

`geometry` is the core and it is always present, because `src/geometry.rs` has
**no `use` statements at all**: it compiles as a standalone crate with an empty
dependency tree, passes its own tests there, and builds for
`thumbv8m.main-none-eabihf`. Everything else is a feature.

| feature | default | gates |
|---|---|---|
| — | always | `geometry` |
| `embedded-graphics` | **on** | `clip` (`DiscClipped`), `surface` (`Surface`, `ByteColor`) |
| `abgr2222` | off | `color` (`Abgr2222`, `shade`, the palette, `GREY_LEVELS`, `CHANNEL_MAX`) |
| `preview` | off | `preview`; implies `std` and `abgr2222`; brings `png` |
| `panic-handler` | off | `panic`, location only |
| `panic-message` | off | implies `panic-handler`; adds the message |
| `std` | off | host assertions |

**An earlier version of this section said there was deliberately no `abgr2222`
feature, because "the colour type is always present, so a feature for either
would gate nothing and say it did". That reasoning was about a crate whose only
audience had this panel.** It is wrong now: the crate's audience is every round
`DrawTarget`, most of which will never construct an `Abgr2222`, and gating it
removes real code for the majority of them. The same argument does not rescue a
`round` feature — the clip stays a runtime choice between `Surface::round` and
`Surface::rect`, and `DiscClipped` has no `rect` mode to choose between.

`preview` is still the only feature with a dependency beyond
`embedded-graphics` (`png`), and it is host-only.

`Cargo.toml`'s `include` lists the Rust sources, the README, this file and the
licence, so nothing else ships in the crate — `cargo package --list` is the
check, and everything it prints is Rust or documentation.

Verified rather than asserted: `cargo publish --dry-run` is clean;
`cargo doc --all-features` builds with `-D warnings`; the crate compiles for
`riscv32imac-unknown-none-elf` as well as `thumbv8m.main-none-eabihf`; the core
builds alone under `--no-default-features`; and
`cargo tree --no-default-features -e normal` prints the crate and nothing
else, which is the only thing that says the gating works — a build succeeds
whether or not `embedded-graphics` was resolved.

**But "platform-generic" is a weaker claim than it first looks, and the limit is
`ByteColor`.** `Surface` is generic over the colour type, but bounded on
`ByteColor: to_byte() -> u8`, so it serves **byte-per-pixel panels only**. A
16bpp `Rgb565` panel — much the most common thing an adopter would bring — does
not compile against it:

```
error[E0599]: the associated function `round` exists for `Surface<'_, Rgb565>`,
              but its trait bounds were not satisfied
```

The riscv build tests that the crate compiles on another *architecture*, not
that it serves another *panel format*, and the genericity measurement in §2 used
a second byte-wide type. So what is actually established is: generic over
byte-per-pixel colour encodings, at a measured 66 bytes, on more than one
architecture. Which is the real shape of the thing — it is a kit for
**low-bit-depth** panels, and one byte a pixel is what "low-bit-depth" has meant
throughout.

Lifting it wants a `Raw`-width-generic buffer with the stride arithmetic and the
row fill written against `PixelColor::Raw` rather than `u8`, which is a real
piece of work and should not be done speculatively — it wants an adopter with a
16bpp panel asking for it, and the §2 measurement re-run at that width, since
`to_byte`/`from_byte` stop being free.

**The UNA half (this repo, not published):** `Header/GuiShell.hpp`,
`inscribed-disc.cmake`, and the kernel and `SettingsKit` couplings. Nothing in the
public crate names UNA, and it ships no font.

**Text is not in the crate at all.** It ships no font and no text interface. The
trait that was here — four questions a widget needs answered — is parked,
because the adopter routes around it: it takes `Surface::bytes_mut()` and hands
the raw buffer to `TextKit`'s own rasteriser. The orphan rule is why — an app
owns neither the trait nor `textkit::Face`, so it cannot write the impl without
a newtype, which was built and measured at 47 lines and 112 bytes of `.text` to
convert between two spellings of the same thing. That escape hatch is the honest
current state of the seam, and it is documented on the method rather than
dressed up as an abstraction nobody uses.

**Not depended on, and why.** `embedded-graphics-framebuf` already provides a
framebuffer `DrawTarget`, and does not fit: this one is a byte-per-pixel
`PixelColor` with a **round clip** and a row-fill `fill_solid`, and the clip is
the entire point. `embedded-layout`'s alignment vocabulary is worth stealing
rather than re-coining — `text::Align` uses `Left/Center/Right`, the same three
it, `embedded-text` and `TextKit` all use. `embedded-text` overlaps `wrap`,
which is 45 lines here against a dependency; the kit keeps its own because the
slot-plus-`needed` contract is what makes overflow visible.

---

## 11. Adoption

### `SettingsEditor`

**Not in this repository.** It was the first crate the kit was fitted to, and
the numbers that exercise gave — it did not compile for the watch before and
does after — are why the panic handler and the surface look as they do. But the
app is uncommitted work, so nothing here can be checked against it, and none of
its figures are quoted as evidence. `Spin` below is the adoption this repository
can show you.

### `Spin`

The app that had actually reached the ceiling: 1,581 lines, six screens, a zone
ring, a measured arc sampler and 115 lines of hand-mirrored ABI. Moving it is
the test of whether this is a kit or a library.

It is installed on a watch, so the bar is not "still compiles" but **"draws the
identical pixels"**. Its 43-scene catalogue is the instrument: every frame was
hashed before the change and after.

| | before | after |
|---|---|---|
| `lib.rs` | 1,580 lines | **1,482** (−98) |
| host tests | 43 pass | **43 pass**, plus the golden test |
| **all 43 scene frames** | — | **byte-identical** |
| own `.text` (archive) | 5,082 | 5,198 (+116) |
| own `.rodata` (archive) | 462 | 530 (+68) |

The +116 bytes are not overhead, they are the diagnostics: this app's panic
handler sent the literal `"panic"`, and the kit's formats `file:line` and the
message. That is the drift the kit fixes by existing, and it is why the cost
sits in the app rather than being avoided.

Two things changed behaviour rather than only structure, and both are recorded
because "byte-identical frames" could otherwise hide them:

- **The surface owns the clip.** Shapes clipped to the square buffer and only
  text clipped to the disc, over the same bytes. The frames are identical
  because nothing was ever painted outside the glass — which is exactly what
  `nothing_is_drawn_outside_the_bezel` had been checking *after the fact*
  instead of preventing.
- **The undefined symbol moved.** The archive now needs `inscribed_disc_host_panic`
  where it needed `spin_gui_host_panic`, and `Gui.cpp` was changed to define it.
  Nothing but a link catches that: the crate compiled fine with the symbol
  dangling. `llvm-nm --undefined-only` on the archive is the check, and it is
  the one ergonomic cost of the kit owning the panic handler.

**What did not move to the kit, and why.** `Spin`'s arc sampler stays on
`micromath`'s trig rather than `draw::fill_arc`'s. The two agree to about
1e-6, which is far below a pixel, but "about" is not "byte-identical", and the
frames are the thing being protected. Swapping it wants its own measurement —
count the differing pixels across all 43 scenes and decide — not a refactor that
assumes the answer.

## What is parked

Recoverable whole from commit `8055796`, e.g.
`git show 8055796:InscribedDisc/src/nav.rs`.

The measure that decided it: of 2,810 lines of `src/`, **1,251 had no consumer
outside this crate**. The apparent callers for three of them were
`Docs/measurements/tier-footprint`, a probe written to make each tier link so a
footprint table could be produced — a fixture that exists to be measured, not a
use.

| module | lines | why it went |
|---|---|---|
| `nav` | 405 | Focus ring, screen stack, animation clock. Written against §8's list of screens; **zero callers**. The two-caller rule this file sets for widgets applies to it too. |
| `text` | 180 | `TextFace`, the trait §10 called the seam that makes publishing possible. **No implementor**, and the orphan rule is why: an app owns neither the trait nor `textkit::Face`. A newtype bridge was built and works — 43 golden frames unchanged — at 47 lines and 112 bytes per app, to convert between two spellings of the same thing. The shape that would earn it is `TextKit` implementing the trait itself, which needs no newtype and would serve every adopter at once. |
| `scene` | 198 | The `Scenes` catalogue trait. **No implementor** — `Spin`'s catalogue returns a `Vec` and kept its own tests, so the checks this was meant to hand an adopter were never inherited by one. |
| `draw` | 301 | The arc sampler, `fill_circle`, `accumulate_coverage`. **Zero callers**: `Spin` kept its own sampler on `micromath` rather than take this one, because swapping the trig would move pixels. |
| `dither` | 167 | Ordered dithering. **Zero callers** — no screen in either app draws a gradient. |

`preview`'s three `Scenes`-generic helpers went with `scene`; `to_rgba`,
`write_png`, `sheet_from_frames` and `gamut_sheet`, which `Spin` actually calls,
stayed.

What the parked code is not: wrong. `draw`'s arc step and `dither`'s error bound
are real measurements with real tests, and `nav` answers questions §8 asks
properly. They are unexercised, which on a crate about to be published is the
more dangerous property — the adversarial review found seven faults in the code
that *is* used, and none of this had that scrutiny.

Bring one back when a screen needs it, and let that screen tell you the shape.

## What is not built

Listed so the gap is not something a reader has to find:

- **The opaque payload reader** of §4. The fixed event ABI is built; the data
  half is designed and specified only.
- **The backports** — the colour-depth guard to the four shells missing it, the
  real panic handler to `Spin` and `Barcode`. Both are what the kit fixes *by
  existing*, and both still need doing to the shipping apps.
- **The scaffold** (`new-gui-app <Name>`). "Get started without reinventing
  everything" is a command you can run or it is a claim, and right now it is a
  claim.
- **`.uapp` sizes, and `.bss`.** Those still need the packer and the pinned
  Docker image. `.text` and `.rodata` *are* now measured linked, with `rust-lld`
  and a minimal linker script — see the panic-handler measurement — so where a
  figure in this file is an archive upper bound it says so.
- **Frame time on hardware.** The 78 ms slack is the platform's; no scene in
  this kit has been timed against it, on the host or on a watch.
- **Trusted Publishing, a CHANGELOG, and the licence text.** `Cargo.toml`
  declares `MIT OR Apache-2.0`, matching `TextKit` and `EffortKit`, but the
  repository's root `LICENSE` is MIT only and no crate here ships licence files.
  That discrepancy predates this crate and is not one to fix unilaterally, but
  it has to be settled before anything is actually published.
- **`cargo semver-checks`.** Not installed here, and at 0.1.0 there is no
  published baseline for it to compare against. It belongs in CI from the first
  release, not before it.
- **A ride longer than 23 seconds.** `Spin 0.10.0` runs on the watch and has
  recorded a session end to end — `start version=0.10.0`, samples, `stop
  saved=1`, `recoveries=0 dropped=0`, and no panic — but it ceased twice as
  `too_short`, so the lap split, the target arc filling and the confirm-discard
  screen have not been drawn on glass. A working app is also not a
  pixel-accurate one: the goldens say the frames match the pre-conversion build,
  and nothing has compared glass against glass.
- **Anything past a byte a pixel**, as above. The public claim has to be
  "low-bit-depth, one byte a pixel" until someone lifts `ByteColor`.
- **Tier 2 beyond two widgets.** The value row and the four-button hint ring
  have their warrants and are not written.
- **Anything in [What is parked](#what-is-parked)**, which is where the focus
  model, the text trait, the scene catalogue, the arc sampler and the dither
  now live.

## What is deliberately not built

Different list, and these are decisions rather than gaps: partial redraw, damage
tracking and dirty rectangles (the hardware deletes the problem); pointer hit
testing (there is no pointer); a DSL or visual builder (it would throw away the
measured constants, which is the codebase's best property); a confirm dialog
(one caller); a staleness gate (one constant pretending to be general); text
rasterisation (a trait instead).

---

## Layout

```
InscribedDisc/
  src/
    lib.rs        crate root and features
    surface.rs    the framebuffer, the clip, the fill_solid fast path
    color.rs      Abgr2222, the palette, shade()
    geometry.rs   the lit disc, chords, the inscribed square
    widgets.rs    the page indicator, the toggle pill
    preview.rs    frames to PNG, sheets, the gamut sheet
    panic.rs      one panic handler, one host symbol
  Header/
    GuiShell.hpp  the C++ pump, header-only, host-type-checkable
  inscribed-disc.cmake  the two lines nobody can guess
  Docs/           one file per measurement, and the gamut sheet
```

62 tests, all passing, on `cargo test --all-features`.

[`embedded-graphics`]: https://crates.io/crates/embedded-graphics
