# 2026-08-19 — what a real tile actually contains

`GATES.md` recorded the scene presets as judgements: "city centre is 433
features and 8,338 points because that seemed like a city centre, not because
anyone counted a real z14 tile." This is that count.

It was expected to cut either way — presets too dense would have *rescued*
Gate C, presets too sparse would worsen it. **The presets are too sparse, by
3×**, and both Gate A and Gate C get worse as a result.

Host-side, from `toronto.pmtiles` and `athens.pmtiles`, already on disk.
[`mvt.py`](mvt.py) is the minimal MVT/PMTiles reader written for it — no
library was available, so it parses the protobuf and the geometry command
stream directly.

## The count

Downtown Toronto (`-79.42,43.63 → -79.35,43.68`), z14, 16 tiles, per tile:

| layer | features | points | drawn? |
| --- | --- | --- | --- |
| buildings | 62 | **15,341** | yes (`building`, "context only") |
| roads | 199 | 5,578 | yes |
| landuse | 326 | 3,795 | yes |
| water | 6 | 209 | yes |
| earth | 1 | 5 | yes |
| pois | 88 | 88 | no — labels |
| places | 7 | 7 | no — labels |
| boundaries | 0 | 3 | no |
| **drawn total** | **594** | **24,928** | |

Label layers are 0.4% of points, so "the renderer only draws some layers" turns
out not to be a get-out: essentially all the geometry is geometry it must draw.

| | features | points |
| --- | --- | --- |
| MapLab preset `city centre` | 433 | 8,338 |
| **real downtown z14** | **594** | **24,928** |
| ratio | 1.4× | **3.0×** |
| real rural z14 (Athens, Ontario) | 19 | 670 |
| MapLab preset `rural` | 70 | 1,428 |

The real density range, rural to downtown, is **37×** (670 → 24,928). The
presets span **5.8×** and top out well below the real ceiling. They are not
merely imprecise; they are the wrong shape.

## Gate C is much worse than measured

R08 measured 160.5 ms at 19.25 µs/point. Applying that rate to real densities:

| Scene | Points | Render | vs 100 ms |
| --- | --- | --- | --- |
| MapLab preset `city centre` | 8,338 | 160.5 ms | 1.6× over |
| **real downtown z14** | **24,928** | **479.9 ms** | **4.8× over** |
| real downtown, buildings dropped | 9,587 | 184.5 ms | 1.8× over |
| real rural z14 | 670 | 12.9 ms | passes |

**The 100 ms budget buys 5,194 points.** A real downtown tile has 24,928, so
the pivot must discard **79% of the geometry**, or the rasteriser must get
**4.8× faster**. The earlier figure of 38% came from the preset and was
optimistic by more than a factor of two.

**Buildings are 61% of the points** in 10% of the features — by far the largest
single lever. Dropping them entirely still leaves 184.5 ms, so they are
necessary but nowhere near sufficient. The spec calls `building` "context
only", which makes it the cheapest thing to reconsider.

## Gate A gets worse too

MapLab's own recorded scene sizes give the draft format's cost per point, which
improves with density as fixed overhead amortises:

| Scene | Encoded | Points | B/point |
| --- | --- | --- | --- |
| rural | 4,263 | 1,428 | 2.99 |
| suburban | 8,429 | 3,644 | 2.31 |
| city | 16,787 | 8,338 | 2.01 |

At 2.01 B/point a real downtown z14 tile encodes to ~50,188 B. The same tile is
**62,252 B as gzipped MVT**. So a purpose-built vector pack is only **1.24×
smaller than MVT** — short of the **1.6×** that
[Gate A](../2026-08-19-gate-a-pack-size) needs to clear ≥10×.

So the hope that stripping attributes and unused layers would close Gate A does
not survive contact with a real tile. It could still be closed by making the
*format* better rather than the payload smaller — 2 B/point is roughly one byte
per coordinate, and delta-plus-varint on dense polylines should beat that — but
that is format work, not packing work, and it is now on the critical path for
Gate A rather than a nicety.

## What this changes

- **The presets should be replaced with these counts**, not adjusted. Rural
  1,428 → 670; city 8,338 → 24,928. The report already prints cost per feature
  and per point precisely so a corrected preset does not invalidate a session,
  so runs 56/151 remain readable — but their headline number is answering a
  question about a scene 3× lighter than a real city.
- **Gate C's remedy is now generalisation-first.** A 4.8× rasteriser speedup is
  a much larger claim than a 1.6× one; dropping 79% of geometry is aggressive
  but is what zoom-appropriate generalisation is *for*, and card 8 already said
  it is under-aggressive.
- **Gate A now depends on the wire format**, not on what goes into it.

## Caveats

- One extent, one vendor's tiling, one z14. Toronto downtown is a reasonable
  worst case for a wrist map but it is not *the* worst case.
- A viewport is 240 px against a 256 px tile, so one tile ≈ one screen. A
  viewport straddling four tiles draws no more geometry, but it does pay four
  times the per-tile I/O — which bench I07 already costs at 638 µs per
  (tile, layer).
- Point counts come from MVT geometry commands, which is the geometry a
  renderer would receive. Generalisation applied *at pack time* would reduce
  them, and that is exactly the lever Gate C now needs.
