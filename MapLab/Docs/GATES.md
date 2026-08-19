# The gates — what MapLab is for, and where each answer stands

The vector-map pivot (`MapKit/Docs/VECTOR-PIPELINE-PROMPT.md`) rests on four
gates and a handful of numbers that were, until this app, either taken from a
model of the panel or inherited from a different firmware line. This is the
ledger. It uses the repository's verification convention: **CONFIRMED** (traced
to code, text or an experiment on this device), **LIKELY** (reasoned from
something confirmed), **UNVERIFIED**, **REFUTED**.

Update it from `Tools/maplab_report.py` output after every hardware session,
and cite the run id so a number can be traced back to the rows it came from.

## The gates

| | Gate | Status | Evidence |
| --- | --- | --- | --- |
| **A** | Size: is a vector pack ≥10x smaller than the RLE raster equivalent? | **UNVERIFIED** | Host-side. Not MapLab's question — it belongs to the packer, against the Athens extent |
| **B** | RAM: does the renderer's static set link into `RunMap`? | **CONFIRMED — fits** | `Tools/gate_b_link_test.sh`, 2026-08-18, SDK at `apps-v1.4.0`. See below |
| **C** | Time: does a dense viewport render inside 100 ms? | **UNVERIFIED** | Benches R05–R08 + B01. Host figures below are not device figures |
| **D** | Legibility: does the palette-first cartography hold up on glass? | **UNVERIFIED** | The twelve cards. Needs eyes, a camera, and daylight |

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

## Numbers this app takes, and what they replace

| Bench | Number | Previously | Status |
| --- | --- | --- | --- |
| R08 | dense viewport render | never measured | **UNVERIFIED** |
| R05 | decode+transform share of a render | never measured | **UNVERIFIED** |
| R09 | 64-entry restyle LUT over a full canvas | charter X7, "proven in simulation, per-frame cost unmeasured" | **UNVERIFIED** |
| B01/B02 | full-screen canvas blit vs the 2×2 raster mosaic | never compared | **UNVERIFIED** |
| I02 | first filesystem touch after app start | ~113 ms, measured on 1.3 | **UNVERIFIED on 1.4** |
| I06 | 64 KiB read | 6–9 ms per tile, measured on 1.3 | **UNVERIFIED on 1.4** |
| I07 | 512 B read after a seek | never measured; the layer directory's whole cost model | **UNVERIFIED** |
| I11 | a real `.rawtiles` tile read | 6–9 ms on 1.3 | **UNVERIFIED on 1.4** |
| W01 | longest GUI-thread block the watchdog tolerates | anecdote: ~10 s survived, 201 MB scan did not | **UNVERIFIED** |
| Cards | the palette, the weights, the dash, the variants | a colorimetric model and simulated renders | **UNVERIFIED** |

## Host figures, for scale only

Taken on an Apple-silicon laptop, and recorded so that a device number can be
sanity-checked rather than compared. The device is a 160 MHz Cortex-M33; expect
one to two orders of magnitude.

| Scene | Encoded | Features | Points | Host render |
| --- | --- | --- | --- | --- |
| rural | 4,263 B | 70 | 1,428 | ~100 µs |
| suburban | 8,429 B | 164 | 3,644 | ~291 µs |
| city centre | 16,787 B | 433 | 8,338 | ~670 µs |

**The presets are judgements, not counts from a real extract**, and the report
prints cost per feature and per point precisely so that a corrected preset does
not invalidate a measurement. Counting features in a real z14 tile of a
European city centre is the work that would upgrade them.

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
