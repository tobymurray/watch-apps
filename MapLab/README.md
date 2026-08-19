# Map Lab — taking the vector map's numbers off the model and onto the glass

A foreground `Utility` app that measures, on the watch, everything the
[vector-map pivot](../MapKit/Docs/VECTOR-PIPELINE-PROMPT.md) currently assumes.
It renders vector geometry with a candidate rasteriser, times every stage,
blits through the real framebuffer path, exercises the filesystem the way a
per-tile layer directory would, finds where the app-liveness watchdog fires,
and puts twelve cartography cards on the panel to be looked at.

**Why now.** Every cartographic decision behind the map work so far was taken
against a *colorimetric model* of this panel and a set of simulated renders:
the 64 codes and their L\* values, the 14-slot palette and its contrast ratios,
the line weights (which the spec itself calls "judgements informed by the 1:1
renders, not measurements"), and the four restyle variants ("proved the
mechanism in simulation"). The render budget was inherited from anecdote. The
RAM ceiling was measured against a firmware line the watch no longer runs.
We have the hardware now.

| | |
| --- | --- |
| AppID | `CA0462AE630B62DE` |
| Type | `Utility`, not autostart |
| SDK | **`apps-v1.4.0`** — the line the watch runs, confirmed 2026-08-18 |
| Shared code | [`../MapKit`](../MapKit)'s vendored `rawtiles` reader, for the raster baseline |
| Writes | `Apps/MapLab/maplab_log.csv`, read by [`Tools/maplab_report.py`](Tools/maplab_report.py) |

## What it has already settled

**Gate B — the renderer fits.** The candidate static working set (a 240×240
ABGR2222 canvas at 57,600 B, a 24 KiB encoded tile, a 2 KiB decoder scratch =
84,224 B) links into `RunMap`'s GUI, which is the largest of the three map GUIs
and sets the shared ceiling. The ceiling itself is about **93 KB**, established twice over: `TileCache::SLOTS
= 2` overflows RAM by 37,740 bytes (131,072 − 37,740 = 93,332), and bisecting
the buffer directly brackets it between 91,136 B, which links, and 94,208 B,
which is short by 908. The first of those reproduces the historical 33,884
measured on `apps-v1.3.0`, which is what makes the instrument trustworthy.
About **9 KB spare**: enough to build on, not enough for a second canvas or a
label collision grid.

A vector renderer **replaces** the tile cache rather than joining it, so what
it may spend is 93 KB, not 93 KB on top of the 64 KiB the cache holds today.

See [`Docs/GATES.md`](Docs/GATES.md) for the full ledger, including the
instrument that silently measured nothing and had to be thrown away.

## The screen

Four buttons, the same four meanings everywhere: `L1` up, `L2` down, `R1` do
it, `R2` back. Four modes.

- **run benches** — steps the suite, one bench per tick, appending each row to
  the log as it completes. One bench at a time is deliberate: a suite that ran
  to completion inside one call would block the GUI thread for the best part of
  a minute, which is the thing the staircase exists to find the limit of.
- **cards** — the twelve visual cards, full screen, one at a time.
- **watchdog stair** — a deliberate, one-press-per-step ladder of GUI-thread
  blocks. It says on screen that it may restart the watch, because it may.
- **exit**.

## The benches

`R` renders, `B` blits, `I` filesystem, `W` the watchdog. Each writes one row;
the three bench-specific integers in each row (`a`, `b`, `c`) mean what this
table says they mean.

| id | what it measures | why it is here |
| --- | --- | --- |
| `R01` | clearing a 240×240 canvas | the memory-bandwidth floor every frame pays |
| `R02` | 1000 3 px stamps | the GPS trace's own primitive, so it is comparable with the shipped map apps |
| `R03` | a 9-segment 2 px polyline | the road primitive |
| `R04` | a concave polygon fill | the water/wood primitive, and where the scanline budget bites |
| `R05` | decode + transform, no drawing | the split between wire format and rasteriser — a faster format can only buy back this share |
| `R06`–`R08` | full render, rural / suburban / city centre | **Gate C**, as a curve rather than a point |
| `R09` | a 64-entry restyle LUT over the canvas | charter **X7**, simulated in 2026-08 and never timed on hardware |
| `B01` | one full-screen canvas blit | the fixed cost of the canvas architecture |
| `B02` | a 2×2 mosaic of a 256 px raster tile | what the shipped raster path does, for comparison |
| `I00` | writing a 1 MiB fixture | append throughput, and the file the read benches need |
| `I01` | open + close | the per-file overhead a multi-tile viewport pays |
| `I02` | the app's first filesystem touch | ~113 ms on 1.3; unknown on 1.4, and it is the first frame's cost |
| `I03`–`I06` | sequential reads at 256 B, 4 K, 16 K, 64 K | the size/latency curve that sets the format's tile cap |
| `I07` | 512 B after a random seek | **the layer directory's cost model**: one seek per (tile, layer) |
| `I08` | enumerating `SharedData/maps/` | what pack discovery costs before anything is drawn |
| `I09` | appending 4 KiB | for anything that writes on the GUI thread |
| `I10` | opening a real `.rawtiles` pack, `skipCrcVerify` | ties these numbers to the shipped path; skipped when no pack is installed |
| `I11` | reading real tiles from that pack | previously 6–9 ms per 64 KiB tile, on 1.3 |
| `W01` | a staircase of GUI-thread blocks, 100 ms → 16 s | the render budget is currently folklore with a number attached |

**Every bench is self-sizing.** The device offers one clock — `getTimeMs()` —
and several of these subjects cost microseconds, so nothing is timed once:
`Bench.hpp` scales the iteration count until the run clears a 200 ms floor,
which puts millisecond quantisation under 0.5 %. A subject slower than the
floor runs once and says so, because a single-shot cost is exactly what a
frame budget is about.

**A measurement that could not be taken prints as `not measured`, never as a
zero.** Every row carries a validity flag, and a render that dropped a span or
clipped a feature is marked `INCOMPLETE` — a renderer that draws less is
measurably faster and looks identical.

## The cards

Twelve, each asking one question. The person holding the watch is the
instrument, so the question is on the screen under the card.

| card | asks |
| --- | --- |
| 64 codes | is one quantum of a channel one perceptual step? (E1 assumes equal steps and says so) |
| slots | which of the 14 specified slots vanish against paper? |
| weights | the thinnest road you can follow, cased and uncased, straight and diagonal |
| dashes | which on/off cycle reads as a trail at 2 px |
| text | does a `paper` halo save aliased text over each fill |
| scene 1× | is this a map |
| scene 2× | overzoom — the sparse-ladder question, and most of the size win |
| scene ½× | is a coarse zoom too dense, i.e. is generalisation doing its job |
| night / contrast / trail | the three LUT variants, on glass instead of in simulation |
| trace | does the trace win against every basemap colour, in every variant (rule R5) |

Look at them **indoors and in sunlight** — this is a reflective panel, and its
whole argument is about ink range. Photograph them into
`Docs/Investigations/<date>-<slug>/` with what you concluded, including what
you could not tell apart.

## What is in here

```
MapLab/
├── Docs/GATES.md                 # the ledger: what is confirmed, what is not
├── Tools/maplab_report.py        # parse the log, print the gate verdicts
├── Tools/gate_b_link_test.sh     # the RAM experiment, run against RunMap
├── Tests/                        # 51 host tests, no SDK, no device
└── Software/
    ├── Libs/Header|Sources/
    │   ├── Palette.{hpp,cpp}     # the 14 slots and the four restyle LUTs
    │   ├── Canvas.{hpp,cpp}      # the candidate rasteriser
    │   ├── VecScene.{hpp,cpp}    # a draft wire format, decoder and generator
    │   ├── SceneRender.{hpp,cpp} # the spec's style, applied
    │   ├── Cards.{hpp,cpp}       # the twelve pictures
    │   ├── Bench.{hpp}           # the timing harness
    │   ├── BenchLog.{hpp,cpp}    # the CSV, normatively specified in the header
    │   ├── BenchSuite.{hpp,cpp}  # the benches themselves
    │   └── Service.{hpp,cpp}     # exists because a Utility app must have one
    └── Apps/
        ├── MapLab-CMake/
        └── TouchGFX-GUI/         # one screen, four modes, a canvas widget
```

**`Canvas`, `VecScene` and `SceneRender` are not mocks.** If the pivot happens,
they are what `MapKit` grows a renderer out of, which is why they are written
to the constraints the render path will actually have: caller-owned pixels, no
heap, no floating point anywhere, everything clipped, and every primitive
counting the work it could not do. The wire format is explicitly a *draft* —
its job is to make the benchmark measure the real access pattern (a per-tile
layer directory, one seek per layer, decode straight into screen coordinates)
rather than a hand-waved one.

## Why everything runs on the GUI thread

Every other app here puts its work in the Service. This one does not, and the
inversion is the point: what is being measured is what a *renderer* would pay,
and a renderer lives in the GUI process, draws from `draw()`, and shares a
thread with TouchGFX. `blitCopy` is only legal inside `draw()` at all — so the
two blit benches are handed to the canvas widget, which times them in its own
draw pass and hands the result back.

The Service exists because `app_merging.py` makes the GUI ELF mandatory and the
packer expects a Service beside it. It blocks on the kernel queue and exits when
the screen closes. See `Software/Libs/Header/Commands.hpp`, which documents the
absence of a message protocol so the next reader does not conclude it was
forgotten.

## Building

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.4.0
cd MapLab/Software/Apps/MapLab-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

Or in containers, which is how the build in this repository was verified:

```sh
docker run --rm --platform linux/amd64 -v "$PWD:/w" -v "$UNA_SDK:/sdk" \
  -e UNA_SDK=/sdk -w /w/MapLab/Software/Apps/MapLab-CMake sleeplab-arm:latest \
  bash -lc "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=0.1.0 . && cmake --build build -j\$(nproc)"
```

Deploy by copying the `.uapp` into `Apps/MapLab/` on the USB-MSC volume; the
kernel rebuilds `app_list.json` on its own boot scan. Pull `maplab_log.csv`
back the same way and run it through `Tools/maplab_report.py`.

**Targets 1.4, unlike the map apps it is measuring for.** `RunMap`, `HikeMap`,
`BikeMap` and `MapManager` are pinned to `apps-v1.3.0`, and the watch was
confirmed on the 1.4 line on 2026-08-18 — so those apps, as pinned, do not run
on this device at all. That is not this app's problem to fix, but it is the
reason Gate B was measured with `UNA_SDK` pointing at 1.4: a ceiling measured
against a kernel interface the watch rejects would not be a ceiling anyone can
spend.

## The simulator

```sh
export UNA_SDK=/path/to/una-sdk
cd MapLab/Software/Apps/TouchGFX-GUI
make -f simulator/gcc/Makefile -j8
```

Worth running before every deploy — it links every source and exercises the
model, the log and the card renderer on the desktop. It will not tell you
anything about *timing*: the numbers it produces are a laptop's, and the whole
point of this app is that a laptop's numbers are not the device's.

Two known limitations, neither this app's doing: **synthetic key injection does
not reach the SDL window**, so the modes have to be driven by pressing keys in
it by hand; and the SDK's simulator prints `pure virtual method called` on
teardown when the app is stopped externally. That second one is inherited —
the stock `SleepLab/Probe` simulator prints it too, verified 2026-08-18 — and
`Model`'s destructor unregisters its callbacks anyway, which is correct
regardless of who is at fault.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk        # only for GoogleTest
cd MapLab/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

51 tests, one executable, **no SDK linked at all** — everything under test here
is arithmetic and bytes, and keeping it buildable with a plain compiler is what
keeps it that way.

Covered: the palette against the spec's own r/g/b triples (a transcription
error is the failure mode, so each slot is asserted independently of its byte);
the rasteriser by exact pixel counts, including that fills are half-open in
both axes so two polygons sharing an edge paint it once; the dash phase; that
a crossing-budget overflow is counted rather than swallowed; the wire format's
round trip, its negative cases (bad magic, wrong extent, truncation at every
length, a layer pointing outside the tile, a payload cut short mid-feature),
and that an **unknown class is skipped rather than rejected**, which is the
forward-compatibility hinge; that generation is deterministic and every preset
fits the buffer the app gives it; that casing really is a second decode pass;
that a too-small scratch buffer is reported as clipping; and that every card
covers the panel and draws its own subject.

Three checked by mutation rather than assumed: making the polygon fill
inclusive in x fails 2 tests, keeping unknown classes fails 1, and dropping the
clipped-feature counter fails 1.

Not covered: `BenchSuite` and `BenchLog`. They are thin wrappers around the
filesystem whose entire content is the thing being measured — a fake filesystem
would time the fake.

## Known rough edges

- **The presets are judgements.** "City centre" is 433 features and 8,338
  points because that seemed like a city centre, not because anyone counted a
  real z14 tile. The report prints cost per feature and per point so a
  corrected preset does not invalidate a session's numbers, but the correction
  is real work that has not been done.
- **No power measurement.** What a static map costs against a 1 Hz redraw needs
  an unattended run and a battery subscription in a Service, which is
  `SleepLab/Probe`'s shape rather than this one's.
- **`W01` is destructive by design.** It is expected to take the device down at
  some step; every step is written to the log *before* it is taken, and the
  missing row is the finding.
- **The 1 MiB I/O fixture stays** in the app folder between runs, deliberately:
  writing it is a measurement (`I00`) and deleting it would make every session
  pay for it again.
- **No labels anywhere.** No card renders placed labels, because no renderer
  places them yet; the text bed asks the halo question instead. Baked raster
  labels are a real capability the vector path loses on day one, and this app
  does not pretend otherwise.

## Provenance

Forked from [`SleepLab/Probe`](../SleepLab/Probe)'s shell — the 1.4 CMake
setup, the TouchGFX tree, the one-screen hand-built-widgets pattern, and the
normative-CSV-plus-report-script discipline. None of its sensor code carried
over. The palette, the line weights, the zoom ladder and the four variants are
transcribed from `slippypack/MAP_CARTOGRAPHY_SPEC.md`; the failure states, the
blit recipe and the "never CRC on the GUI thread" rule come from
[`MapKit`](../MapKit).
