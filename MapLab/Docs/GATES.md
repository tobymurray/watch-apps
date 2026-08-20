# The gates — what MapLab is for, and where each answer stands

The vector-map pivot (`MapKit/Docs/VECTOR-PIPELINE-PROMPT.md`) rests on four
gates and a handful of numbers that were, until this app, either taken from a
model of the panel or inherited from a different firmware line. This is the
ledger. It uses the repository's verification convention: **CONFIRMED** (traced
to code, text or an experiment on this device), **LIKELY** (reasoned from
something confirmed), **UNVERIFIED**, **REFUTED**.

Update it from `Tools/maplab_report.py` output after every hardware session,
and cite the run id so a number can be traced back to the rows it came from.

Sessions so far:
[**2026-08-19 — the first hardware session**](Investigations/2026-08-19-first-hardware-session),
which holds the log and the card photographs every device figure below is
drawn from.

## The gates

| | Gate | Status | Evidence |
| --- | --- | --- | --- |
| **A** | Size: is a vector pack ≥10x smaller than the RLE raster equivalent? | **REFUTED against this cartography** | 3.1× like-for-like, 6.2× crediting overzoom, on the Athens extent, 2026-08-19. The answer swings 1.9×–19.7× with raster style. See below |
| **B** | RAM: does the renderer's static set link into `RunMap`? | **CONFIRMED — fits** | `Tools/gate_b_link_test.sh`, 2026-08-18, SDK at `apps-v1.4.0`. See below |
| **C** | Time: does a dense viewport render inside 100 ms? | **REFUTED — 4.8× over at real city density** | Runs 56/151 gave 160.5 ms on a preset since shown to be 3× too sparse; a real downtown z14 is ~480 ms. Rural passes. See below |
| **D** | Legibility: does the palette-first cartography hold up on glass? | **REFUTED as specified; CONFIRMED with a cased trace** | 24 cards across indoor, overcast and sun spanning 9.0 EV, 2026-08-19. R5 fails on the palette as written and holds once the trace is cased. See below |

## Gate A — measured, and it does not clear the bar

Full working:
[`Investigations/2026-08-19-gate-a-pack-size`](Investigations/2026-08-19-gate-a-pack-size).
Host-side, from files already on disk; no watch involved.

Comparing `athens-watch.rawtiles` against `athens.pmtiles` over their shared
z12–15 and identical bbox — 207 tiles each:

| | z12–15 |
| --- | --- |
| Raster, spec RLE8 | 1,339,221 B |
| Vector, gzipped MVT | 437,204 B |
| **Like-for-like** | **3.06×** |
| **Crediting the vector for overzooming z16** | **6.22×** |

Neither clears ≥10×.

**The recorded RLE figure was wrong.** `slippypack/MAP_DELIVERY_PROMPT.md` has
"Spec RLE measured 32.4% of raw ⇒ ~14.6 MiB". Measured with a round-trip-checked
reimplementation of the spec's own canonical encoder, Athens is **6.0%** —
about **2.6 MiB**, not 14.6. The 32.4% is close to Toronto's measured 38.5% and
looks like a figure carried between packs.

**The real finding is that Gate A is not a vector-versus-raster question.** On
the same geometry, the ratio is 1.94× against one of this project's own raster
styles, 3.06× against another, and 19.66× against a third. The watch
cartography was deliberately made flat for legibility — 14 slots, no dithering,
no gradients — and that flatness is exactly what makes RLE devastating. **The
legibility decision and the size argument pull against each other**, which
nothing in the pivot's case had acknowledged.

**It is closer than it looks, and one tile would settle it.** MVT carries
attributes, labels and every layer, so it is an upper bound on a purpose-built
vector pack. Clearing ≥10× needs a watch pack only **1.6× smaller than MVT** on
the overzoom framing — plausible from stripping attributes alone. Encoding one
real z14 tile in the draft `VecScene` format decides it, and is the same work
that would replace the scene presets with counts.

**Scope caveat.** The extent is 10.0 × 8.0 km of rural Ontario — Athens,
*Ontario*, not Greece. Density is the variable this gate is most sensitive to,
and a dense extent moves it toward vector. Re-measure on a city before deciding
the pivot on this number.

## Gate B, in detail — the one this app has already settled

Measured by resizing `MapKit::TileCache`'s buffer, building `RunMap` (the
largest of the three map GUIs, and so the one that sets the shared ceiling),
and reading the linker's verdict.

| Configuration | Result |
| --- | --- |
| `SLOTS = 2` (131,072 B of cache), SDK 1.4 | `region RAM overflowed by 37,740 bytes` |
| `SLOTS = 2`, pristine `apps-v1.3.0` (historical, `TileCache.hpp`) | overflowed by 33,884 bytes |
| 84,224 B in place of the cache, SDK 1.4 | **links** |
| 91,136 B (89 KiB) in place of the cache | links |
| 94,208 B (92 KiB) in place of the cache | overflowed by 908 bytes |
| 100,352 B (98 KiB) in place of the cache | overflowed by 6,924 bytes |

Two things follow, and the second is the gate.

**The instrument agrees with history.** 37,740 against 33,884 for the same
configuration on a different SDK line is the same finding, which is what makes
the rest of the table worth reading. The ~4 KB difference is unexplained and
small; if it ever matters, it is a 1.3-vs-1.4 comparison somebody should make
deliberately rather than infer from these two rows.

**The candidate renderer fits.** Two instruments agree on the ceiling. The
arithmetic says 131,072 − 37,740 = **93,332 B**; bisecting the buffer directly
brackets it between 91,136 B (links) and 94,208 B (short by 908). Both put the
ceiling for a single static buffer in RunMap's GUI at about **93 KB**, and the
candidate set is 84,224 B
— a 240×240 ABGR2222 canvas (57,600), a 24 KiB encoded tile, and a 2 KiB
decoder scratch. That leaves about **9 KB spare**, which is real but is not
room for a second canvas, a label collision grid, or a per-layer cache. The
budget is a design constraint, not a headroom.

The number to keep hold of: a vector renderer **replaces** the tile cache
rather than joining it. What it may spend is 93 KB, not 93 KB on top of the
64 KiB the cache holds today.

### What was tried first, and why it is not in the tool

The obvious instrument — drop a `.cpp` holding a large static array into
`RunMap`'s globbed `gui/src/` — **silently measures nothing.** The array never
reaches the link: `.bss` does not move, the ELF is byte-identical, and the test
reports "links" at every size. `__attribute__((used))`, `retain` and an
explicit `section(".bss")` all failed to save it. Two sizes were measured that
way and both results were discarded. It is written down here because the
failure is invisible — an instrument that always says yes looks exactly like
good news.

## Gate C, in detail — the session of 2026-08-19

Measured as a curve rather than a point, which is what makes the verdict
actionable: the pivot does not fail everywhere, it fails downtown.

| Scene | Features | Points | Render | Verdict vs 100 ms |
| --- | --- | --- | --- | --- |
| R06 rural | 70 | 1,428 | 24.0 ms | passes, 4.2× headroom |
| R07 suburban | 164 | 3,644 | 70.2 ms | passes, ~30% spare |
| R08 city centre | 433 | 8,338 | **160.5 ms** | **fails, 1.6× over** |

Blit adds 719 µs and is not material.

**The route to Gate C is not the wire format.** R05 puts decode+transform at
5.46 ms — 3.4% of the city render. The other 96.6% is rasterising. A perfect,
zero-cost format buys back 3%, so "design a faster format" cannot close a 1.6×
gap. What can: a rasteriser about 1.6× faster, or ~5,200 points instead of
8,338 (a ~38% density cut, which is generalisation's job). At 19.25 µs/point
that arithmetic is now measured rather than assumed.

**The watchdog is not the binding constraint.** W01 survived every step of the
staircase including 16 s (see below), so a 160 ms render is three orders of
magnitude short of taking the device down. Gate C is a smoothness budget, not a
stability one — which is a different kind of failure and admits different
remedies.

**The I/O cost arrives before the first pixel.** I07's 638 µs per seek is the
per-(tile, layer) cost of the layer directory. A 2×2 viewport with six layers
is 24 seeks ≈ 15 ms, i.e. 15% of the whole frame budget spent before anything
is drawn. That belongs in the format's design, not in its optimisation.

## W01 — the stair was exhausted before the watchdog was found

Seven steps ran in run 246 (100 ms → 8 s) and the eighth in run 946
(16,000 ms target, 16,001 ms measured, `valid=1`). **Every step survived. No
step has ever failed**, so the ladder is exhausted top to bottom without the
watchdog once firing.

The finding is therefore a **lower bound, not a ceiling**: the watchdog
tolerates **at least 16 s** of blocked GUI thread. The instrument tops out at
16 s by construction, so it cannot say where the limit is, only that it is
somewhere above the top of the ladder. The previous anecdote (~10 s survived)
is consistent with this and is neither confirmed nor refuted as a *limit*.

**Two independent signals agree that nothing fired.** Each step writes an
`about-to-block` row before it blocks, and every one of the eight is followed
by its `survived` row — no missing row, which is the designed detector. Beyond
that, uptime climbs monotonically across all five R rows in the log
(56,193 → 946,203 ms) and never jumps backwards, which per `BenchLog.hpp` means
app restarts within a single boot and **no reboot at any point in the session**.
The device was never taken down.

Extending the ladder is the only way to close this, and it is worth asking
whether the answer is worth the reboots — nothing in the render path is within
two orders of magnitude of 16 s. A 160.5 ms city render sits ~100× below the
lowest step that is known to be safe.

## The presets were 3× too sparse — 2026-08-19

Counted, not estimated:
[`Investigations/2026-08-19-real-tile-density`](Investigations/2026-08-19-real-tile-density).

This app's scene presets were judgements, and the ledger said so. Decoding real
MVT puts numbers on them, and they fall the wrong way:

| | features | points |
| --- | --- | --- |
| preset `city centre` | 433 | 8,338 |
| **real downtown Toronto z14** | **594** | **24,928** |
| preset `rural` | 70 | 1,428 |
| real rural z14 (Athens, Ontario) | 19 | 670 |

Real density spans **37×** rural to downtown; the presets span 5.8× and top out
well short of the ceiling.

**Gate C therefore fails by 4.8×, not 1.6×.** At R08's measured 19.25 µs/point a
real downtown tile is **479.9 ms**. The 100 ms budget buys 5,194 points, so the
pivot must discard **79%** of the geometry or the rasteriser must get **4.8×**
faster. Buildings alone are 61% of the points in 10% of the features — the
largest single lever, and still only enough to reach 184.5 ms.

Label layers are 0.4% of points, so "we only draw some layers" is not an
escape: essentially all of it is geometry that must be drawn.

**Gate A also gets worse.** At the draft format's own 2.01 B/point a real tile
encodes to ~50,188 B against 62,252 B of gzipped MVT — **1.24× smaller**, short
of the 1.6× Gate A needs. Closing it now means improving the wire format (2
B/point is about one byte per coordinate; delta-plus-varint should beat it),
not stripping the payload. That moves format work onto Gate A's critical path.

Runs 56/151 stay readable — the report prints cost per feature and per point
exactly so a corrected preset does not invalidate a session — but their
headline number describes a scene 3× lighter than a real city.

## Numbers this app takes, and what they replace

All device figures below are from runs 56 and 151 (2026-08-19, build 1.0.0),
two independent passes of the same suite that agree to within 0.3%; run 942 is
a partial third pass (R01–R03 only) and corroborates those three to within
0.4%. The watchdog figures are runs 246 and 946. Every row cited carries
`valid=1`, and **no row in the file is marked `INCOMPLETE`**, so no render
dropped a span or clipped a feature.

| Bench | Number | Previously | Status |
| --- | --- | --- | --- |
| R08 | dense viewport render | never measured | **CONFIRMED 160.5 ms** — 1.6× over budget |
| R05 | decode+transform share of a render | never measured | **CONFIRMED 5.46 ms = 3.4% of R08** |
| R09 | 64-entry restyle LUT over a full canvas | charter X7, "proven in simulation, per-frame cost unmeasured" | **CONFIRMED 4.39 ms**, ~3% on top of a city render |
| B01/B02 | full-screen canvas blit vs the 2×2 raster mosaic | never compared | **CONFIRMED 719 µs vs 813 µs** (canvas 1.13× faster) |
| I02 | first filesystem touch after app start | ~113 ms, measured on 1.3 | **CONFIRMED 32.0 ms on 1.4** — 3.5× better |
| I06 | 64 KiB read | 6–9 ms per tile, measured on 1.3 | **CONFIRMED 8.48 ms on 1.4** — top of the 1.3 range |
| I07 | 512 B read after a seek | never measured; the layer directory's whole cost model | **CONFIRMED 638 µs per (tile, layer)** |
| I11 | a real `.rawtiles` tile read | 6–9 ms on 1.3 | **CONFIRMED 9.04 ms on 1.4**, 687-tile pack |
| W01 | longest GUI-thread block the watchdog tolerates | anecdote: ~10 s survived, 201 MB scan did not | **CONFIRMED ≥16 s** (run 946); ceiling not found, ladder exhausted |
| Cards | the palette, the weights, the dash, the variants | a colorimetric model and simulated renders | **UNVERIFIED** — indoor half taken, daylight half outstanding |

## Host figures, for scale only

Taken on an Apple-silicon laptop, and recorded so that a device number can be
sanity-checked rather than compared. The device is a 160 MHz Cortex-M33; expect
one to two orders of magnitude.

| Scene | Encoded | Features | Points | Host render | Device (run 56) | Ratio |
| --- | --- | --- | --- | --- | --- | --- |
| rural | 4,263 B | 70 | 1,428 | ~100 µs | 24.0 ms | ~240× |
| suburban | 8,429 B | 164 | 3,644 | ~291 µs | 70.2 ms | ~241× |
| city centre | 16,787 B | 433 | 8,338 | ~670 µs | 160.5 ms | ~240× |

**The ratio is constant to within 0.5% across a 6× density range**, which is
more than this table was built to claim. It means the host figure is a usable
predictor of the device figure under a single scale factor of ~240, so a
rasteriser change can be evaluated on a laptop and only confirmed on glass.
Treat that as a working rule with three points behind it, not a law — and note
it holds for *this* rasteriser, whose inner loop is integer and cache-resident
on both machines. It would not survive a change that added floating point.

**The presets are judgements, not counts from a real extract**, and the report
prints cost per feature and per point precisely so that a corrected preset does
not invalidate a measurement. Counting features in a real z14 tile of a
European city centre is the work that would upgrade them.

## Gate D — the indoor half, 2026-08-19

Twelve cards photographed on a wrist, **backlight off, well-lit interior
room** — the photographs are in
[`Investigations/2026-08-19-first-hardware-session/cards/`](Investigations/2026-08-19-first-hardware-session/cards). That is one of the two conditions the card suite asks for; the daylight
half is outstanding, and it is the half the palette's whole argument is about.

**What these photographs can and cannot carry.** The suite's stated instrument
is the person holding the watch; these are the record, not the verdict. They
are the camera originals, 3000×4000 at quality 95, pulled off the phone over
`adb` and cropped to the panel without resampling — roughly 8 photo pixels per
panel pixel. Two limits remain and both are the phone's, not the transfer's:
JPEG 4:2:0 chroma subsampling, and **auto white balance** (EXIF
`WhiteBalance: 0`), which applies a different colour transform to every frame.
So slots still cannot be compared rigorously *between* cards, and these frames
cannot be compared against a future daylight set at all. Within a single frame
they are sound. Cards 3, 4 and 6–8 are geometry and are unaffected.

| Card | Reading from the photographs | Confidence |
| --- | --- | --- |
| 1 · 64 codes | Steps are **visibly unequal, and compress at the light end**: neighbours in the dark rows separate strongly, several adjacent pale patches in the lower rows are near-indistinguishable. E1 assumes equal steps and says so — this is the first evidence against it. The card's own caption also washes out over the paler patches, which is card 5's question answered incidentally | moderate |
| 2 · slots | The palest band **does effectively vanish** against paper, and the warm taupe band third down is marginal — a low-contrast warm grey a shade off the ground. Every saturated slot (greens, blues, brown, reds) separates cleanly, as do the two inset dark slots. White text over the saturated mid-dark fills is crisp | moderate |
| 3 · weights | 1 px lines are followable but weak. Diagonals show visible staircase — no antialiasing anywhere, as designed | good |
| 4 · dashes | The finest cycle **reads as a continuous line, not a dash**; the dash character only arrives a couple of steps up | good |
| 5 · text | The `paper` halo saves text over dark fills cleanly. Over light and mid fills it is marginal — halo and fill converge | moderate |
| 6 · scene 1× | Reads as a map. Roads, water and park all separate | good |
| 7 · scene 2× | Overzoom holds up — sparser, heavier strokes, still legible | good |
| 8 · scene ½× | **The weakest card.** Roads merge into a tangle; individual features are unrecoverable. Generalisation is not dropping enough at coarse zoom | good |
| 9 · night | Trace wins decisively against the dark ground | good |
| 10 · contrast | Trace visible, but competing with near-black roads | moderate |
| 11 · trail | Trace wins clearly against the muted basemap | good |
| 12 · trace | Day variant: the trace red and the road maroon **share a hue family**, and this is where R5 is weakest | moderate |

Three things worth carrying forward, none of them closeable indoors:

**Rule R5 is at risk in the day variants, not in night or trail.** The trace
must win against every basemap colour; against a dark maroon road at 2 px it is
separated mostly by lightness, and lightness is the axis a reflective panel
loses first in bright light. This is the single most important thing to check
in sunlight.

**Card 8 is a cartography finding, not a rendering one.** The ½× scene is too
dense to read, which says the zoom ladder's generalisation is under-aggressive
at coarse zoom. That is the same lever Gate C needs pulled (~38% fewer points),
so one change may serve both — the strongest cross-gate result of the session.

**Card 4 sets a floor on dash design.** A dash cycle finer than the second step
is indistinguishable from a solid line at 2 px, so the trail styling cannot use
the finest cycles regardless of what the spec's model predicted.

### What changed in the suite because of this session

The indoor half raised three things the twelve cards could not settle, so the
suite was extended to twenty before the daylight session rather than after it.
Cards 1–12 keep their numbers; the investigation bundle cites them.

| Added | Settles |
| --- | --- |
| `ramps` | whether the light-end compression seen on card 1 is real, in blocks big enough to judge |
| `slots at width` | whether a slot that survives as a 15 px band survives as a 1 px contour |
| `curves` | whether a weight survives an angle that is neither horizontal nor 45° |
| `text dark` | the halo over the dark fills card 5 never showed |
| `trace/slots` ×4 variants | **rule R5 against every slot in every variant**, which is the risk this session flagged and card 12 could not test |

Two defects were also fixed, both of which cost the indoor session data:

- **The caption sat across the middle of the panel** (y=134–178), obscuring
  card 1's swatches and illegible over the scene cards. It is now on the bottom
  arc.
- **The half-scale card drew one 120 px tile into the middle of a 240 px
  panel**, so three quarters of it was paper and the density question was being
  asked of a quarter of the field. It is now a 2×2 mosaic.

And one that would have made the daylight session answer the wrong question:
the text cards' glyphs were drawn **plain white with no halo at all**, while
their caption asked "does the halo save it". They are now `road_major` ink with
a `paper` halo.

## R5 is refuted, and the palette is why — 2026-08-19 lighting series

Full write-up and both photograph sets:
[`Investigations/2026-08-19-lighting-series`](Investigations/2026-08-19-lighting-series).

Rule R5 says the trace must win against every basemap colour in every variant.
It does not. Against `road_minor` it is marginal indoors and close to gone under
overcast, and the cause is structural rather than a matter of judgement:

| Slot | Code | Channels | Model L* |
| --- | --- | --- | --- |
| `trace` | `0xC3` | r3 g0 b0 | 51.76 |
| `road_minor` | `0xC1` | r1 g0 b0 | 36.58 |
| `road_major` | `0xC0` | r0 g0 b0 | 23.67 |

Green and blue are zero in all three. They are the **same hue**, separated by
lightness alone with no chroma difference at all — and lightness is the first
thing a reflective panel gives up as ambient rises and the glass returns the
sky. The control is `path` (`0xD0`, r0 g0 b1): a cool ink at the *same* modelled
L* as `road_minor`, over which the trace stays legible in both conditions.

**The remedy is one the spec already owns: case the trace.** A `paper` halo
under the trace ink wins against any basemap colour whatever its hue or
lightness, which is what R5 actually demands. `road_major` is already drawn
cased and casing survived both conditions on the line-weights card. No new
code, no palette change, one pixel of width each side.

**Cards 21–24 carried that remedy onto the panel** (build 1.2.0), uncased and
cased in one frame, and **direct sun confirms it works.** The uncased line
disappears into the warm bands; the cased line stays followable across every
slot, because a `paper` outline gives the trace a boundary that does not depend
on the colour underneath. Card 22 (`cased night`) is the clearest of the four.

So Gate D's verdict splits: **the cartography as specified fails R5, and the
cartography with a cased trace passes it.** The change costs one pixel of width
each side, needs no new code on the render path, and spends no palette slot.

The sun set also settles the ordering question the cards were built to ask.
Cards 18–20 draw the trace before the LUT and 22–24 after it; only the latter
keep a pale casing on the night ground. **The app-drawn trace belongs over the
restyled basemap, not inside the restyle.**

Those cards draw their casing **after** the variant LUT. `paper` is exactly
what a restyle remaps hardest, so a casing applied before it would put a dark
halo on a dark ground in `night` and rescue nothing. Since cards 18–20 draw the
trace before the LUT and 22–24 draw it after, the pair also answers a question
nobody had asked: whether the app-drawn trace belongs inside the basemap
restyle at all.

Changing the palette instead would mean giving the trace a chroma component to
get it off the pure-red axis — a larger change, and one that spends a colour
the 14 slots do not have going spare.

**Conditions, from EXIF rather than estimate**, each step cross-checked two
ways. Indoor→overcast: 7.09 EV by ISO and shutter, 7.10 EV by APEX brightness.
Overcast→sun: 1.90 EV and 1.89 EV. So overcast is ~137× the room, sun is ~3.7×
overcast and ~510× the room, and the series spans **9.0 EV**.

The sun set was taken at 16:13 local in late August — afternoon sun rather than
noon — so the bright end is not fully explored and the true ceiling sits
somewhat above it. R5 already fails well below that ceiling, so this does not
weaken the verdict; it would only matter for a claim that something *survives*
the brightest case.

## Gate A and Gate C are one gate — 2026-08-19

Full working:
[`Investigations/2026-08-19-format-ceiling`](Investigations/2026-08-19-format-ceiling).

Asked what an optimally designed wire format would buy. The answer reframes two
gates.

**A better format cannot touch Gate C.** R05 puts decode and transform at 3.4%
of a render; the rest is rasterising, which depends on how many points arrive,
not how they were spelled. Format work is a Gate A lever exclusively.

**The encoding floor is ~1.43 B/point** — the measured entropy of the
quantised delta stream, which deflate essentially reaches. That is 2.2× better
than gzipped MVT and 1.4× better than MapLab's draft. Naive delta+varint is
*worse* than absolute 8-bit coordinates: zigzag costs two bytes past ±63 and
every part opens with an absolute jump.

**Quantising to the 256 screen grid removes 20.4% of points for free** — MVT
carries 12-bit coordinates for a 240 px canvas, so those points were never
distinguishable. DP at 1 px removes another 50%, also free in the sense that a
sub-pixel deviation cannot be drawn.

Carrying both through, with the Athens Gate A anchors:

| Scenario | pts | render | vs MVT | Gate A l-f-l | Gate A overzoom |
| --- | --- | --- | --- | --- | --- |
| MVT as delivered | 24,928 | 480 ms | 1.0× | 3.1× | 6.2× |
| + quantise to 256 | 19,842 | 382 ms | 2.2× | 6.7× | 13.6× |
| + DP 1 px | 9,986 | 192 ms | 4.4× | **13.4×** | **27.1×** |
| + drop buildings | 5,308 | 102 ms | 8.2× | **25.1×** | **51.0×** |
| + trim landuse | 5,195 | **100 ms** | 8.4× | **25.7×** | **52.1×** |

**Gate A clears ≥10× at DP 1 px — long before Gate C clears 100 ms — and by the
time geometry is light enough to render in budget it clears by 25–52×.** Every
point removed to satisfy C pays A twice, since it removes both a point to draw
and a point to store. Gate A was only ever refuted because the pipeline was
being asked to store geometry it could never have rendered. **It does not decide
anything.**

**What does decide it is cartographic.** After quantise and DP 1 px, buildings
are 4,677 points and **90.0 ms — 90% of the frame budget** — for a slot the spec
itself calls "context only". Roads, water and landuse together are 102 ms;
buildings on top are 192 ms. So the question the pivot actually turns on is
whether the map can drop buildings. If it can, everything closes. If it cannot,
Gate C fails at 192 ms whatever the format does.

## Not measured here, deliberately

- **Power.** What a static map costs versus a 1 Hz redraw needs an unattended
  run and a battery subscription in a Service — which is `SleepLab/Probe`'s
  shape, not this one's. The panel holds its image for 11 µW per its datasheet,
  so the hypothesis is "redraw is the only cost"; it is untested.
- **Gate A.** Pack size is the packer's measurement, on a host, against the
  Athens extent. Nothing on the watch can answer it.
- **Labels.** No card renders placed labels, because no renderer places them
  yet. The text bed card is the closest thing: it asks whether an aliased glyph
  with a `paper` halo survives over each fill.
