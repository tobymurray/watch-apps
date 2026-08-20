# 2026-08-19 — the best the format can do, and why that is the wrong question

Asked: if the wire format is optimised freely — ignoring anything in `rawtiles`
that does not fit — where does that leave the gates?

**Answer: format optimisation alone rescues Gate A and does nothing whatever
for Gate C. And it turns out not to matter, because Gate A and Gate C are the
same constraint wearing different clothes — both are driven by point count, and
anything that makes C pass makes A pass with an order of magnitude to spare.**

Measured on downtown Toronto z14 (16 tiles, drawn layers only), the densest
real data on hand. Method and reader in
[`2026-08-19-real-tile-density`](../2026-08-19-real-tile-density).

## Why a better format cannot touch Gate C

Bench R05 already settled this and it is worth restating: **decode and transform
are 3.4% of a render.** The other 96.6% is rasterising, which is a function of
how many points arrive, not how they were spelled. A perfect, zero-cost,
infinitely clever format buys back 3.4% of 480 ms.

Format work is therefore a Gate A lever exclusively.

## The encoding floor, measured

Per tile, 19,842 points after quantising to the 256-unit screen grid:

| Encoding | B/tile | B/point |
| --- | --- | --- |
| absolute 8-bit x,y | 46,998 | 2.37 |
| delta + zigzag varint | 50,562 | 2.55 |
| **entropy floor of the delta stream** (4.26 bits) | **28,459** | **1.43** |
| deflate over the delta stream | 28,181 | 1.42 |
| lzma over the delta stream | 26,070 | 1.31 |
| *gzipped MVT, for reference* | *62,252* | *2.49* |
| *MapLab `VecScene` draft* | *50,105* | *2.01* |

Two things worth noticing.

**Naive delta+varint is worse than storing absolute 8-bit coordinates.**
Zigzag varint costs two bytes as soon as a delta exceeds ±63, and every part
starts with an absolute jump. The obvious optimisation is a pessimisation here;
the win only appears once the delta stream is entropy-coded.

**The floor is ~1.43 B/point** and deflate essentially reaches it, so a
bit-packed scheme with small fixed-width deltas and an escape should land close
without needing a general-purpose decompressor on the read path — which matters,
because the render path has no heap and R05's 3.4% is a budget a decompressor
would spend.

So the best a format can do is about **2.2× smaller than MVT** on unsimplified
geometry, or **1.4× better than the current draft**.

## Quantising to the screen grid is free, and not small

MVT carries 4096-extent coordinates — 12 bits per axis. The canvas is 240 px.
Quantising to 256 and dropping consecutive duplicates removes **20.4% of all
points** before any simplification, because at screen resolution they were
never distinguishable.

| | points/tile |
| --- | --- |
| MVT as delivered | 24,928 |
| quantised to 256 | 19,842 |

That is the one reduction with no cartographic cost at all: those points could
not have been drawn differently.

## Simplification, and where it runs out

Douglas–Peucker on the quantised geometry:

| Simplify | points/tile | render | vs 100 ms |
| --- | --- | --- | --- |
| none | 19,842 | 382.0 ms | 3.8× over |
| DP 0.5 px | 12,205 | 234.9 ms | 2.3× over |
| **DP 1.0 px** | **9,986** | **192.2 ms** | **1.9× over** |
| DP 2.0 px | 8,084 | 155.6 ms | 1.6× over |

DP 1 px is free in the same sense — a deviation under one pixel cannot be
rendered. Past that it costs shape, and it yields little: doubling the tolerance
to 2 px buys only 19% more.

**Geometry alone does not get there.** After every reduction that costs nothing
visually, a real downtown tile is still 1.9× over budget.

## Where the time actually goes

After quantise + DP 1 px:

| layer | points/tile | render |
| --- | --- | --- |
| buildings | 4,677 | 90.0 ms |
| roads | 3,322 | 64.0 ms |
| landuse | 1,875 | 36.1 ms |
| water | 106 | 2.0 ms |
| earth | 4 | 0.1 ms |
| **total** | **9,986** | **192.2 ms** |

**Buildings alone consume 90% of the frame budget.** The spec calls `building`
"context only" — the cheapest thing in the cartography to reconsider, and the
one that decides Gate C.

- roads + water + earth: 3,432 pts, **66.1 ms** — passes with room
- \+ landuse: 5,308 pts, **102.2 ms** — 2% over
- \+ buildings: 9,986 pts, **192.2 ms** — fails

So the budget affords **roads, water, and landuse — but not buildings**, and
landuse only just.

## The two gates are one gate

Applying the entropy-floor encoding at each stage, and carrying the Athens
Gate A anchors through (raster RLE8 1,339,221 B like-for-like, 2,719,760 B with
the vector credited for overzooming z16, against 437,204 B of MVT):

| Scenario | pts | render | B/tile | vs MVT | Gate A l-f-l | Gate A overzoom |
| --- | --- | --- | --- | --- | --- | --- |
| MVT as delivered | 24,928 | 480 ms | 62,071 | 1.0× | 3.1× | 6.2× |
| + quantise to 256 | 19,842 | 382 ms | 28,374 | 2.2× | 6.7× | 13.6× |
| + DP 1 px | 9,986 | 192 ms | 14,280 | 4.4× | **13.4×** | **27.1×** |
| + drop buildings | 5,308 | 102 ms | 7,590 | 8.2× | **25.1×** | **51.0×** |
| + trim landuse to budget | 5,195 | **100 ms** | 7,429 | 8.4× | **25.7×** | **52.1×** |

Read the last two columns against Gate A's ≥10× bar and the render column
against Gate C's 100 ms.

**Gate A stops being the binding constraint as soon as generalisation is
serious.** It clears ≥10× at DP 1 px — well before Gate C does — and by the time
the geometry is light enough to render in budget it clears by 25–52×. Every
gram of geometry removed to satisfy C pays Gate A twice, because it removes both
points to draw and points to store.

The corollary is the useful one: **there is no version of this where Gate A
decides anything.** It was measured as refuted at 3.1× because the pipeline was
being asked to store geometry it could never have rendered.

## So where does it leave us

- **Optimise the format and nothing else:** Gate A 6.7×/13.6×, Gate C unmoved at
  382 ms. Passes on the overzoom framing only, and the pivot still fails.
- **Generalise properly:** both gates pass, and the format choice stops
  mattering much — even MapLab's existing draft at 2.01 B/point clears ≥10× on
  the overzoom framing once the geometry is simplified.
- **The decision that actually gates the pivot is cartographic, not technical:
  can the map drop buildings?** If yes, everything closes. If no, Gate C fails
  at 192 ms whatever the format does.

## Caveats

- One extent, one vendor's tiling, one zoom. Downtown Toronto is a fair worst
  case for a wrist map, not the worst case that exists.
- 1.43 B/point is an entropy floor, not a shipped encoding; a streamable
  bit-packed scheme lands somewhat above it. The conclusions hold at the draft's
  2.01 B/point too — that column is in the table above.
- DP was run per part with no topology preservation. A real generaliser must not
  pull shared boundaries apart, which costs some of this reduction back.
- The 19.25 µs/point rate is R08's, measured on a scene of different
  composition. Cost per point is the report's own normalisation, but a
  building-heavy tile is polygon fill rather than polyline stroke, and the two
  do not have to cost the same per point.
