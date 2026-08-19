# 2026-08-19 — Gate A, measured

**Gate A: is a vector pack ≥10× smaller than the RLE raster equivalent?**

**REFUTED against the cartography this project actually chose** — the vector
side is 3.1× smaller like-for-like, or 6.2× if the vector is credited for
overzooming a zoom level the raster has to store. Both are short of ≥10×.

But the headline hides the real finding, which is that **Gate A is not a
question about vector versus raster at all. It is a question about the raster
style you compare against**, and the answer swings by an order of magnitude
across styles this project has already produced.

Host-side work, no watch involved. Gate A was always the packer's question;
this is it answered with the data that was already on disk.

## What was compared

| | |
| --- | --- |
| Extent | `-76.015,44.59 → -75.889,44.662` — **10.0 × 8.0 km, 80 km²** |
| Raster | `athens-watch.rawtiles`, 687 tiles, z12–16, 256 px, ABGR2222 |
| Vector | `athens.pmtiles`, 230 tiles, z0–15, gzipped MVT |
| Overlap | **z12–15, 207 tiles in both**, identical bbox |

**This is Athens, Ontario — not Athens, Greece.** 44.63 °N, 75.95 °W: a village
and its farmland. Anyone carrying a density intuition from the Greek city has
the wrong picture, and density is the variable this gate is most sensitive to.

## The raster pack is not RLE-compressed, so RLE had to be measured

Every tile in the pack is exactly 65,536 B — 256×256×1, `compression = None`.
Gate A asks about the *RLE* equivalent, so the encoder had to be applied.

`slippypack`'s `rle8.rs` documents the spec § 9.11 canonical encoder in full
(literal runs `H ∈ [0x00,0x7F]`, repeat runs `H ∈ [0x80,0xFF]`, repeats only for
runs ≥ 3, both capped at 128). It was reimplemented here and validated two ways
before any number was believed: round-trip over 200 randomised cases, and a
fast length-only model checked byte-for-byte against the real encoder over
another 200. Both clean.

### The recorded 32.4% figure is wrong for these packs

`slippypack/MAP_DELIVERY_PROMPT.md` says "Spec RLE measured **32.4%** of raw ⇒
~14.6 MiB for the same area". Measured on the actual files:

| Pack | Tiles | Raw | Spec RLE8 | Ratio |
| --- | --- | --- | --- | --- |
| `athens-e2e.rawtiles` | 687 | 45,023,232 | 1,726,624 | **3.8 %** |
| `athens-watch.rawtiles` | 687 | 45,023,232 | 2,719,760 | **6.0 %** |
| `toronto.rawtiles` | 260 | 17,039,360 | 6,563,862 | **38.5 %** |

So the Athens RLE pack is **~2.6 MiB, not ~14.6 MiB** — the recorded estimate is
out by about 5×. The 32.4% is close to Toronto's 38.5%, which is the likely
provenance: a figure from one pack carried across to another.

## The measurement

Per zoom, Athens extent, `athens-watch` raster:

| Zoom | Tiles | Raster raw | Raster RLE8 | Vector MVT (gz) |
| --- | --- | --- | --- | --- |
| z12 | 6 | 393,216 | 90,774 | 82,956 |
| z13 | 16 | 1,048,576 | 195,225 | 98,302 |
| z14 | 42 | 2,752,512 | 367,085 | 99,173 |
| z15 | 143 | 9,371,648 | 686,137 | 156,773 |
| **z12–15** | **207** | **13,565,952** | **1,339,221** | **437,204** |
| z16 | 480 | 31,457,280 | 1,380,539 | *(not present)* |

- **Like-for-like, z12–15: 3.06×.**
- **Crediting the vector for overzoom** — vector stores z12–15 and renders z16
  from it, raster must store z16 — **6.22×.**

Neither clears ≥10×.

## Why the answer is really about the raster style

Applying the three measured RLE ratios to the same z12–15 raster:

| If the raster were styled like… | z12–15 raster | Ratio vs vector |
| --- | --- | --- |
| `athens-e2e` (3.8 %) | 848,173 | **1.94×** |
| `athens-watch` (6.0 %) | 1,339,221 | **3.06×** |
| `toronto` (38.5 %) | 8,593,335 | **19.66×** |

Gate A passes comfortably against one style and fails badly against another,
on the same geometry. The delivery prompt already warned about this —
"Treat compression ratio as an *output* of the cartography decision, not an
input" — and this is that warning with numbers on it.

**The uncomfortable part:** the watch cartography was deliberately made flat.
Fourteen slots, no dithering, no gradients, hard quantisation — all chosen for
legibility on a reflective panel. That same flatness is what makes RLE
devastatingly effective, and it is therefore what removes most of the vector
pack's size advantage. **The legibility decision and the size argument pull
against each other**, and nothing in the pivot's case acknowledged that.

## What would change the verdict

The vector side here is gzipped MVT, which carries attributes, labels and every
layer — a purpose-built watch pack carries none of that. MVT is therefore an
*upper bound* on the vector size, and the gate is closer than the headline
suggests:

| To clear ≥10× | Vector budget | Needs to be smaller than MVT by |
| --- | --- | --- |
| like-for-like | 133,922 B | **3.3×** |
| with overzoom credit | 271,976 B | **1.6×** |

**1.6× smaller than MVT is entirely plausible** — stripping attributes, labels
and unused layers could reach it on its own. So the honest verdict is: refuted
with the best proxy available today, and genuinely undecided until somebody
encodes one real tile in the actual wire format.

## The next step, and it is small

Take one real z14 tile from `athens.pmtiles`, decode the MVT, keep only the
layers the cartography spends slots on, encode it in MapLab's draft `VecScene`
format, and compare against that tile's 2,361 B of MVT. One tile settles the
1.6× question.

That work also pays a second debt. `GATES.md` records the scene presets as
judgements — "city centre is 433 features and 8,338 points because that seemed
like a city centre" — and names counting a real z14 tile as the work that would
upgrade them. It is the same tile, decoded the same way. Note the presets look
suspect already: MapLab's synthetic "city centre" encodes to 16,787 B while a
real z14 tile here is 2,361 B of MVT, which is either a very different density
or a very different encoding efficiency, and the difference matters for Gate C
as much as for Gate A.

Caveat on that comparison: 80 km² of rural Ontario is not a European city
centre. A denser extent has more ink, which compresses worse under RLE and
moves the gate toward vector. **Gate A should be re-measured on a dense extent
before the pivot is decided on it.**
