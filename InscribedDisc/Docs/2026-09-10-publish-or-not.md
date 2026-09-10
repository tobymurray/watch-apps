# Should `inscribed-disc` be published, and as what?

2026-09-10. Every crates.io figure below was read on that date with the crates.io
API; every footprint and frame time was produced by
[`Docs/measurements/disc-wrapper`](measurements/disc-wrapper), which says how to
re-run each one.

---

## The recommendation

> **Decided after this section: publish one crate, `inscribed-disc`, with the
> generic wrapper promoted into it and `color`, `preview` and `panic` kept
> behind default-off features.** A default-off feature is not compiled, so it
> costs a consumer nothing (§4 measures that for `png`), and `Abgr2222` is the
> right type for the three round Sharp `DD` panels, which no crate on crates.io
> provides. Splitting has one in-repo consumer today — the condition that parked
> five modules already — and is a normal 0.x operation if a second appears. What
> this section recommends is the split; what was taken from it is the promotion.

**Publish a narrower crate, and not this one.** The general thing here is the
disc geometry plus a `DrawTarget` wrapper that clips to it, generic over any
`PixelColor`. `Surface`, `Abgr2222`, `shade`, the widgets, `preview` and the
panic handler are not general, have one caller each, and should stay in this
repository.

Three findings decide it, and they point the same way.

1. **The disc clip is a real gap.** `DrawTargetExt::clipped` returns
   `Clipped<'a, T>` whose only field is `clip_area: Rectangle`; it intersects
   rectangles and tests `Rectangle::contains`. Nothing on crates.io targets
   round displays. `embedded-graphics` has no open issue or PR for
   non-rectangular clipping.
2. **But forty lines do replace `Surface`.** 58 non-comment lines, byte-identical
   output, **84 bytes cheaper** linked, 1.15–1.20× the frame time.
3. **And the forty lines reach panels `Surface` cannot.** `ByteColor` is what
   shrinks a real audience to one device — not the roundness. Once the wrapper
   is generic it compiles against `Rgb565`, which is every GC9A01 module.

So the crate as it stands is the wrong 1,500 lines to publish, and the right
~400 lines are worth publishing, because the geometry is not the part a
developer would get right unaided. `largest_square`'s closed form is off by
one; `lit_start` written as a scan costs 3×. Both are recorded here with the
counts. That is a dependency, not a gist.

---

## 1. The forty-line verdict

`Docs/measurements/disc-wrapper/src/disc.rs`, written before looking at whether
it would win:

```rust
pub struct DiscClipped<'a, T> { parent: &'a mut T, w: i32, h: i32 }

impl<'a, T: DrawTarget> DiscClipped<'a, T> {
    pub fn new(parent: &'a mut T) -> Self {
        let s = parent.bounding_box().size;
        Self { parent, w: s.width as i32, h: s.height as i32 }
    }
    fn lit(w: i32, h: i32, p: Point) -> bool {
        if p.x < 0 || p.y < 0 || p.x >= w || p.y >= h { return false; }
        let (dx, dy, d) = (2 * p.x - (w - 1), 2 * p.y - (h - 1), w.min(h) - 1);
        dx * dx + dy * dy <= d * d
    }
    fn start(w: i32, h: i32, row: i32) -> i32 { /* the isqrt half-chord */ }
}

impl<T: DrawTarget> DrawTarget for DiscClipped<'_, T> {
    type Color = T::Color;
    type Error = T::Error;

    fn draw_iter<I: IntoIterator<Item = Pixel<Self::Color>>>(&mut self, pixels: I)
        -> Result<(), Self::Error>
    {
        let (w, h) = (self.w, self.h);
        self.parent.draw_iter(pixels.into_iter().filter(|Pixel(p, _)| Self::lit(w, h, *p)))
    }

    fn fill_solid(&mut self, area: &Rectangle, color: Self::Color) -> Result<(), Self::Error> {
        // per row: both ends lit means the whole span is; otherwise one isqrt
        // and delegate a one-row Rectangle to the parent's fill_solid.
    }
}
```

Copying `Clipped`'s trick of pulling the clip out into locals before touching
`self.parent` is what makes the borrow check pass; that is the only non-obvious
line in it.

**It is correct.** `the_wrapper_paints_exactly_what_the_surface_paints` draws a
scene chosen to straddle the rim on every row — a full-width band, a ring, a
diagonal past both corners, a triangle to the bottom edge, 240 single pixels —
through `Surface::round` and through the wrapper over a plain rectangular
target, and compares all 57,600 bytes. **Zero differ.**

**It is cheaper.** Linked into an ELF with `rust-lld` and the panic-handler
measurement's linker script, `thumbv8m.main-none-eabihf`, `opt-level = "z"`:

| | linked `.text` | over the rect-clip floor |
|---|---|---|
| `Plain`, rect clip only | 444 | — |
| **`inscribed_disc::Surface::round`** | **772** | +328 |
| **`DiscClipped` over `Plain`** | **688** | **+244** |

Adopting `inscribed-disc` for the clip costs **84 bytes more** than writing the
wrapper. That is small either way; what it kills is the footprint argument for
the crate.

Read that as "what adopting costs against writing the wrapper", not "what the
clip costs": `Surface`'s 772 includes `round`'s `checked_mul`, its `Option` and
the `unwrap` panic path, where the wrapper's parent is constructed directly.
That is the decision being priced, so it is the right comparison, but it is not
a clip-against-clip one.

**It is fast enough, and the fast path is three lines.** Median of nine
interleaved trials, aarch64 host:

| | µs/frame | vs `Surface::round` |
|---|---|---|
| `Plain`, rect clip | 12–13 | 0.6× |
| `inscribed_disc::Surface::round` | 20–22 | — |
| `DiscClipped`, per-row `fill_solid` | 23–25 | **1.15–1.20×** |
| `DiscNaive`, `draw_iter` filter only | 158–168 | **7.4–8.5×** |

Answering §2's three sub-questions:

- **The row-fill `fill_solid` fast path.** Worth having: 7.4–8.5× without it.
  But it is not `Surface`'s to sell — 12 lines in the wrapper get within 20% of
  it. And the naive wrapper is *fast enough anyway* on this platform: 168 µs
  against 78 ms of per-frame slack is 0.2% of the budget. The fast path buys
  waste, not a frame. That was already this repository's own finding — the
  128 µs figure in `geometry.rs` is a scan against a rect clip, and the honest
  cost of the current disc clip is 57.9 µs against 43.0, recorded in
  `tmp/adversarial-review/inscribed-disc/frametime.txt`. **`README.md` line 65 reads
  as though the fast path recovers 128 µs to 43; it recovers it to 57.9, and 43
  is rect clipping with no disc at all.** That sentence should be fixed whatever
  is decided here.
- **`lit_span` / `lit_chord`, and whether anyone needs the chord table.** The
  wrapper needs `lit_start`'s closed form and the span-ends early-out, and I
  only wrote them correctly because they were in front of me. `lit_chord` and
  `box_is_lit` it does not need. `largest_square` it does not need either —
  and that is the single most valuable function here, because it is the one a
  developer writes as `floor(d / √2)` and gets **169 where the answer is 168**.
  This is the part that should be a dependency.
- **The `ByteColor` bound: does it buy anything a generic wrapper would lose?**
  It buys the `buf.fill(byte)` in `clear`, the `slice::fill` per row, and
  `bytes() -> &[u8]` for handing a frame to a display. Those are real and they
  are `Surface`'s, not the clip's. What they cost is §3.

---

## 2. The addressable population

Named devices, with what was checked.

### Would compile against `Surface` today

| device / panel | round | framebuffer | verdict |
|---|---|---|---|
| **Sharp LS012B7DD06**, 1.2″ 240×240 | yes, circular, 30.24 × 30.24 mm active | 2 bits a channel, **64 colours** | the panel this was written for |
| **Sharp LS013B7DD02**, 1.29″ 260×260 | yes, circular | `DD` colour part | qualifies; depth not individually confirmed |
| **Sharp LS014B7DD01**, 1.39″ 280×280 | yes, circular | *"a 2-bit gray scale signal for each dot, thus presenting a palette of 64 colors"* | **qualifies, confirmed** |

That is the list. Three panels, one family, one vendor. `geometry` already
generalises to all three — `counts` re-derives the lit count, widest row, chord
parity and inscribed square at 240, 260, 280 and 128, and every one holds.

**And even those three only qualify if the adopter uses `Abgr2222`.** `Gray8` —
`embedded-graphics`' own byte-per-pixel colour — does **not** compile against
`Surface`:

```
error[E0599]: the associated function or constant `round` exists for
              struct `Surface<'_, Gray8>`, but its trait bounds were not satisfied
              doesn't satisfy `Gray8: ByteColor`
```

and the adopter **cannot fix it**, because they own neither `Gray8` nor
`ByteColor`:

```
error[E0117]: only traits defined in the current crate can be implemented
              for types defined outside of the crate
```

So the population is not "byte-per-pixel round panels". It is "byte-per-pixel
round panels whose owner adopts this crate's colour type or writes a newtype".
§5 fixes this for nothing.

### Round, and excluded

| device / panel | round | framebuffer | why excluded |
|---|---|---|---|
| **GC9A01 modules** (Waveshare 1.28″, AliExpress, M5Stack Dial) | yes, 240×240 | **12/16/18-bit only** — RGB444/565/666, no 8-bit mode | `ByteColor`. `gc9a01-rs` (40,238 downloads) is `type Color = Rgb565` |
| **Sharp LS010B7DH01 / LS010B7DH04**, 0.99″ 128×128 | yes, circular | `DH` = monochrome, 1bpp packed | not a byte a pixel. Has its own driver crate, `ls010b7dh01` — listed in `embedded-graphics`' README, last published 2018, predating `DrawTarget`: it implements no `type Color` at all and ships its own midpoint `draw_circle` |
| **Bangle.js 1** | round *case*, **square display** | 240×240, 16-bit | round bezel over a square screen whose corners are visible — a disc clip would be wrong |

### Not round at all

**PineTime** — square 240×240 ST7789, 16bpp. **Watchy** — square 200×200
monochrome e-paper. **LILYGO T-Watch 2020 / S3** — square 240×240 ST7789.
**Bangle.js 2** — square 176×176 LPM013M126, 3-bit. None of these has the
problem the crate solves.

### What that implies

The disc clip has an audience — every GC9A01 module ever sold, plus the round
Sharp parts, plus round e-paper. The **byte-per-pixel** bound is what reduces
that audience to three panels from one vendor. So §3's question — honest scope
or central mistake — has two answers:

- For `Surface`, **honest scope**. It is a framebuffer with stride arithmetic
  against `u8`. Lifting it to arbitrary `Raw` widths is real work and should
  wait for someone who wants it, exactly as `DESIGN.md` §10 says.
- For the **crate**, **central mistake**, because the crate's whole claim is the
  clip, and the clip does not need the bound at all. `DiscClipped` over
  `embedded-graphics-framebuf` disc-clips `Rgb565` at 240×240 with 44,296 glass
  pixels painted and **0 behind the bezel**, and `BinaryColor` at 128×128 with
  12,644 and **0**. Both are tests in the harness. `Surface` compiles against
  neither.

The size of the gap, for the record: on 240×240 the bezel is **12,792 pixels,
22.2% of the framebuffer**, and a 4-pixel inset — the shape a developer writes
believing it is safe — paints every one of them.

---

## 3. The ecosystem, re-read on 2026-09-10

| crate | version | total downloads | recent 90d | last release |
|---|---|---|---|---|
| `embedded-graphics` | 0.8.2 | 2,650,029 | 475,417 | 2026-02-15 |
| `embedded-graphics-core` | 0.4.1 | 2,415,800 | 464,209 | 2026-02-15 |
| `slint` | 1.17.1 | 1,654,304 | 466,514 | 2026-07-07 |
| `embedded-graphics-framebuf` | 0.5.0 | 225,880 | 22,651 | 2023-07-17 |
| `embedded-text` | 0.7.3 | 92,661 | 9,857 | 2025-10-10 |
| `u8g2-fonts` | 0.8.0 | 71,746 | 18,419 | 2026-05-24 |
| `embedded-layout` | 0.4.2 | 60,263 | 3,660 | 2025-04-26 |
| `gc9a01-rs` | 0.4.2 | 40,238 | — | — |
| `embedded-canvas` | 0.3.2 | 14,068 | 1,278 | 2025-08-25 |
| `kolibri-embedded-gui` | 0.1.0 | 2,661 | 100 | 2025-02-25 |
| `matrix-gui` | 0.3.1 | 191 | 119 | 2026-07-23 |

`embedded-canvas` at 14,068 downloads is the useful comparison: a small
`embedded-graphics` adjunct is a legitimate crate, and this one would be about
the same size.

**`embedded-graphics-framebuf` — concede or refute.** `DESIGN.md` §10 says it
"does not fit". Half right, and the half that is wrong matters:

```rust
pub struct FrameBuf<C: PixelColor, B: FrameBufferBackend<Color = C>> { ... }
impl<C: PixelColor, B: FrameBufferBackend<Color = C>> DrawTarget for FrameBuf<C, B>
```

It is **fully generic over `PixelColor`** — strictly more general than
`Surface` — and it has DMA-capable backends. What it does not have is the disc
clip, and it supplies only `draw_iter` and `clear`, no `fill_solid` or
`fill_contiguous`, so it is per-pixel regardless. It also gives no safe
`&[u8]`; raw bytes come out through `ReadBuffer`, unsafely. So the honest
statement is: **`embedded-graphics-framebuf` + a disc wrapper is the general
version of `Surface`**, slower and more general, and that composition is the
crate being recommended.

**What the earlier survey did not cover.** Searched crates.io on 2026-09-10 for
`round`, `circular`, `disc`, `round+display`, `gc9a01`, `smartwatch`, `bezel`,
`memory-lcd`, `ls012b7dd06`, `sharp+memory`, `roundel` and `pinetime`, taking
the top results by downloads.

- **Nothing targets round displays.** Everything the `gc9a01` search returns is
  a driver. Everything the `smartwatch` search returns is non-graphical.
- **No driver ships a disc-aware `DrawTarget`.** `gc9a01-rs` 0.4.2 —
  `type Color = Rgb565`, and grepping its whole `src/` for
  `inscribed|is_lit|circular|disc|round|bezel|chord` finds one hit: a doc link
  to an AliExpress listing for a round module. Its users have a round panel and
  a rectangular clip. `sharp-memory-display` 0.3.0 — `BinaryColor`, 1bpp
  packed, and **it already supports `ls012b7dd06` and `ls010b7dh04` as
  features**, both round panels, with no disc awareness anywhere in the crate.
  `memory-lcd-spi` 0.0.7 — `Rgb111`/`RawU4` and `BinaryColor`, rectangular JDI
  and Sharp parts.
- **`embedded-graphics` has no open issue or PR for non-rectangular clipping.**
  The repository is alive (last push 2026-08-02, 90 open issues) and searching
  its issues for `clip`, `round`, `circular` and `mask` turns up nothing open on
  the subject; the only open milestone is 0.8. So "contribute upstream instead"
  is not available as a redirect — there is nothing to contribute *to*. Opening
  an issue proposing `DrawTargetExt::clipped_to` over a predicate is worth
  doing, and does not block publishing.

---

## 4. Dependencies

**`png` behind `preview` costs a `no_std` consumer nothing. Measured.** In a
consumer crate depending on `inscribed-disc` with `default-features = false`, `png`,
`flate2`, `miniz_oxide`, `fdeflate`, `crc32fast`, `adler2`, `simd-adler32` and
`bitflags` are all **absent from the resolved lockfile** — not merely unbuilt.
Identical under resolver 1, 2 and 3. With `preview` on they appear, and
`miniz_oxide` appears twice over (0.9.1 via `flate2`, 0.8.9 direct from `png`).

So `preview` should **not** be a second crate: it costs nothing off, and
splitting it buys a second name and a second version to keep in step. Keep it.
The falsifier is a future Cargo that resolves inactive optional dependencies, or
a consumer with `--all-features` in CI, which is a self-inflicted wound.

**`embedded-graphics` 0.8 is a safe floor.** MSRV 1.71.1 per its README;
0.8.2 released 2026-02-15 after a two-and-a-half-year gap from 0.8.1
(2023-08-10). No 0.9 milestone exists; the only open one is 0.8. A `DrawTarget`
impl is not at near-term risk. `inscribed-disc` declares `rust-version = "1.81"`,
ten releases stricter than its only dependency needs — worth checking whether
anything actually requires it.

**`ByteColor` can be replaced, it compiles, and it costs zero. Applied and
tested.** Not as `where RawU8: From<Self>` — a where-clause on a trait is not an
implied bound, so that spelling leaked `RawU8: From<C>` into every downstream
signature, `widgets` included. As a **supertrait** it does not leak:

```rust
pub trait ByteColor: PixelColor<Raw = RawU8> + From<RawU8> + Into<RawU8> + Copy {
    #[inline]
    fn to_byte(self) -> u8 { self.into().into_inner() }
    #[inline]
    fn from_byte(b: u8) -> Self { Self::from(RawU8::new(b)) }
}

impl<C: PixelColor<Raw = RawU8> + From<RawU8> + Into<RawU8> + Copy> ByteColor for C {}
```

`Abgr2222` then drops its hand-written impl and gains the two `From` impls every
`embedded-graphics` colour type already has from that crate's own macros.
Measured on a copy of the crate:

- **`cargo test --all-features` passes**, no other file changed.
- Builds for `thumbv8m.main-none-eabihf` **and** `riscv32imac-unknown-none-elf`.
- Linked tier-0 `.text`: **656 bytes either way**, exactly.
- One body, two bounds, in `measurements/disc-wrapper/bound`: **422 bytes
  either way**, exactly.
- **`Gray8` now works with no impl written anywhere.**
- `Rgb565` is still refused, and the message is better —
  `<Rgb565 as PixelColor>::Raw = RawU8` says what is wrong, where
  `Rgb565: ByteColor` sends the reader to this crate's docs.

**Do this regardless of the publish decision.** It removes a concept from the
public API, costs nothing, and fixes a real defect: today an adopter with a
`Gray8` panel is blocked by the orphan rule with no workaround but a newtype.

---

## 5. The name

> **Decided against this section: `inscribed-disc`, one crate.** The name states
> the crate's central assumption — that the glass is the inscribed disc of a
> square framebuffer — which is also `DESIGN.md`'s own falsifier for the
> geometry, so a reader whose glass is shaped otherwise learns it before
> installing. `inscribed-disc`, `inscribed_disc` and `inscribeddisc` all return
> 404 from `https://crates.io/api/v1/crates/<name>`, checked 2026-09-10. What
> follows is the survey that was run before that choice, kept as it was written.

**What the crate is for**, first, because the name has to fit it: *on a round
display the framebuffer is square and the glass is not, so nothing stops you
painting pixels the wearer will never see. This says where the glass is, and
gives you a `DrawTarget` that will not let you paint off it.*

**`disc-clip`**, confirmed free on 2026-09-10 by a 404 from the crates.io API,
along with its `discclip` and `disc_clip` normalisations. The old name,
`lit-disc`, `disc-target`, `round-display`, `roundel`, `circlet`, `discface`,
`roundgfx`, `round-target`, `embedded-graphics-round`, `embedded-graphics-disc`,
`embedded-disc`, `embedded-glass`, `glass-clip` and `round-clip` are also free;
`porthole` and `bezel` are taken.

Testing the objections to the old name rather than accepting them: all three
hold. "-kit" promises a framework that was removed and `DESIGN.md`'s own parked
list says so. Its stem signals neither constraint that decides whether a reader
can use it. And `-Kit` being this monorepo's convention was an argument for
keeping the directory name, which the decision above overrode: a crate and the
directory holding it should not answer to two names.

### The `embedded-graphics-` prefix is fine, and that argument is withdrawn

An earlier draft of this section said the prefix was not the third-party
convention, on the evidence of four crates. **A census says the opposite, and
the claim was wrong.** Every crates.io crate whose name begins
`embedded-graphics-`, with its owner:

| crate | owner | |
|---|---|---|
| `embedded-graphics`, `-core`, `-simulator` | `jamwaffles` + the org team | official |
| `-framebuf`, `-sparklines` | `bernii` | third-party |
| `-colorcast` | `kpcyrd` | third-party |
| `-coordinate-transform` | `msvisser` | third-party |
| `-gop` | `nytpu` | third-party |
| `-profont` | `tes-maker` | third-party |
| `-readback` | `goyox86` | third-party |
| `-terminal` | `L-jasmine` | third-party |
| `-unicodefonts` | `j-g00da` | third-party |

Nine of twelve are third-party, by eight independent owners. And
`embedded-graphics`' own README lists its adjuncts in **every** naming style
side by side — `tinybmp`, `ibm437`, `u8g2-fonts`, `eg-seven-segment`,
`embedded-canvas`, `embedded-graphics-framebuf` — with no stated convention
anywhere in the crate or its docs. The prefix is neither required nor
discouraged, and no disclaimer is owed for using it.

So the prefix does not decide this. What is left does:

- **`-round` promises round drawing primitives** — arcs on the bezel, radial
  layout, text on a curve, a rim scrollbar — and there are none. The arc sampler
  is parked with zero callers and `DESIGN.md` §8 item 1 records the curved
  scrollbar as still open. That objection never depended on the prefix.
- **It takes the good general name** a fuller round-display crate should have
  one day.
- **The name does not have to carry discovery.** crates.io ranks on description
  and keywords, not only the name: searching `gc9a01` returns
  `embedded-graphics` and `display-driver`, neither of which has that string in
  its name. And the real discovery channel is
  `embedded-graphics`' README, whose "Additional functions provided by external
  crates" section says *"If you know of a crate that is not in this list, please
  open an issue to add it."* One issue there does more than any name, and it
  supplies the ecosystem context that a prefix would otherwise have to carry.

`disc-clip` parses cold, with no vocabulary the reader has to be taught, and
claims exactly the mechanism. `lit-disc` is this repository's own term and
covers both halves more exactly, but "lit" means nothing to someone who has not
read `geometry.rs` — that is a gloss the name should not need.

**What a reader would expect from `disc-clip` that it does not deliver:** that
it is *only* a clipper. The layout geometry travels with it — `largest_square`
for the largest centred square wholly on the glass, `lit_chord` for how wide a
row may be — and that is the half someone would not go looking for under this
name. It is also the half worth depending on, since the closed form for the
inscribed square is off by one. Name them in the README's second line.

What the name notably does **not** need to say is *low bit depth*. That
constraint dies with `Abgr2222` and `shade`, which stay in this repository; the
narrow crate is generic over `PixelColor` and has no bit-depth limit at all.

**Equal-standing alternative:** `embedded-graphics-disc`. It sheds `-round`'s
promise of primitives while keeping the prefix's placement, and with the
convention argument withdrawn it costs nothing but length. The choice between
the two is now taste: `disc-clip` is short and `use disc_clip::DiscClipped`
reads better; `embedded-graphics-disc` tells the reader which ecosystem it plugs
into without their having to read the description. Getting listed in the
`embedded-graphics` README supplies that context either way, which is the
tiebreaker for the shorter name.

---

## 6. What would change this answer

**What would make it "publish as-is":** a second adopter, on a second panel,
using `Surface` rather than the clip alone. That is the thing not in evidence.
One application, one panel, one 23-second session. `DESIGN.md` already sets a
two-caller rule for widgets; the same rule applied to `Surface` says wait.

**What would make it "don't publish at all":** finding that the geometry is
reproducible from a formula after all. It is not — 168 against 169, and the
scan against the closed form — but if `largest_square` turned out to be
`floor((d - 1) / √2)` in closed form for every panel size, the crate would
collapse to the forty lines and a blog post would serve more people. Worth ten
minutes with `counts` before publishing anything.

**What I expect to regret about publishing the narrow crate:**

1. **That it is a small crate holding a permanent name.** `disc-clip` is chosen
   partly *because* it is modest enough not to block the fuller round-display
   crate that should own `embedded-graphics-round` one day. The regret in the
   other direction is that a reader who wants the layout geometry will not guess
   it lives behind a name that says "clip" — which is a README problem, and the
   cheaper of the two to fix.
2. **That nobody has a round display and a Rust project.** The evidence for
   demand is `gc9a01-rs`'s 40,238 downloads and no disc-aware target anywhere —
   which is either an unserved need or a need nobody feels, and downloads cannot
   tell the two apart. The falsifier is cheap and worth running first: open the
   `embedded-graphics` issue proposing predicate clipping and see whether
   anyone says "I need this".
3. **The panic handler, if it ever ships in a general crate.** Only one
   `#[panic_handler]` can exist in a binary, so a graphics crate that owns one
   collides with `panic-halt`, `panic-probe` and `defmt`. It is default-off, so
   it is safe today, and it should not travel with a public crate at all.

---

## Corrections to this repository's own documents

Found while checking, and each is the kind of claim `CLAUDE.md` says a file
cannot keep true:

- **`Docs/DESIGN.md` counts tests, and counts the files
  `cargo package --list` prints.** Both were wrong, and correcting the numbers
  would only reset the clock: nothing in that file changes when a test is added.
  Deleted rather than updated — the commands are the answer.
- **`Docs/DESIGN.md` "What is not built" says `Cargo.toml` declares
  `MIT OR Apache-2.0` against an MIT `LICENSE`.** `Cargo.toml` now says `MIT`
  and `LICENSE` is MIT; the discrepancy is settled and the entry is stale.
- **`README.md:65`** attributes the whole 128 µs → 43 µs gap to the fast path.
  43 µs is rect clipping with no disc; the fast path lands at 57.9 µs.
- **`Docs/DESIGN.md` §10 says `embedded-graphics-framebuf` "does not fit".** It
  fits better than `Surface` in the one dimension that decides the crate's
  audience: it is generic over `PixelColor`.
