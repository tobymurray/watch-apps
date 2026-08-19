# Prompt: Vector map packs, end to end — from an area a user picks to pixels on the watch

You are an expert embedded C++ engineer who is also fluent in Rust, map data pipelines and
cartography for constrained displays. Your task is to build the **vector** counterpart of a
raster map pipeline that already works end to end, and to do it as one continuous path: a
person picks an area in a tool, gets a file, copies it to their watch, and sees their own
streets under their GPS trace.

The raster pipeline exists and is not being deleted. `MapKit` draws `.rawtiles` packs under
the breadcrumb in `RunMap` / `HikeMap` / `BikeMap`, `MapManager` verifies them from boot,
and `slippypack` builds them. **You are building a second format and a second renderer
beside them, not a replacement**, and the two must coexist on one watch until the vector
path has earned the right to be the default.

Two things set the shape of the work, and they pull against each other:

1. **The raster path does not scale, and the arithmetic is not close.** A 10 × 8 km pack at
   z12–16 is 45,037,308 bytes; a real watch was measured carrying seven packs at 160.5 MiB;
   the watch-tuned world is ~1 TB. Every zoom level is a separate pre-rendered copy of the
   same ground, every style change is a re-render, and coverage is bought by the megabyte.
   Vector stores geometry once and derives every zoom, style and — potentially — orientation
   from it.
2. **"Vector rendering on the watch is out" is a decision that was already taken, with
   evidence, on 2026-08-05.** You are reopening it. That is legitimate — the premise it was
   taken under has changed — but it means the first thing you build is not a feature, it is
   the measurement that says whether the rest of this document should exist. See § 3.

Work in phases, each ending in a number measured on hardware. Do not build the pipeline and
then find out whether the watch can draw it.

---

## 0. Ground rules

- **Never post anything to GitHub.** No PRs, issues or comments on any repository, `una-sdk`
  above all. `gh` is read-only. Push branches to your own remotes only, plain push, never
  force.
- **Verify, don't trust this prompt.** Every figure below was read out of a repository on
  2026-08-18. Cite `file:line` for claims about code, `§` for claims about a format spec,
  and URL plus retrieval date for claims about anyone's terms. Label each finding
  `CONFIRMED` (traced to code, text or experiment) or `PLAUSIBLE` (reasoned), and for each
  `PLAUSIBLE` say what would settle it. This convention is already in use across both repos;
  match it.
- **Anything settled by experiment goes in `Docs/Investigations/<date>-<slug>/`** in the
  repository it belongs to — hypothesis, method, log, images, verdict. Failures included;
  they are usually the useful part.
- **Terms of use are a gate, not a factor.** The compliance findings in
  `slippypack/MAP_COMPLIANCE_APPENDIX.md` carry over to vector data unchanged and in one
  place get stricter: you are now redistributing geometry derived from OSM, not pixels, so
  the ODbL share-alike question is closer to the surface. Re-verify § 6 before choosing a
  source, and do not bulk-fetch tiles from anyone's server to test — not even "a few
  hundred".
- **Do not modify the SDK.** The app half lives entirely inside `watch-apps/`; anything the
  SDK genuinely lacks becomes a separate single-concern branch with the dependency stated.
- Conventional-commit titles scoped like the rest of the repo (`feat(mapkit): …`,
  `fix(mapmanager): …`), one concern per commit, and **no mention of Claude or AI assistance**
  in commits, PR bodies, code comments or docs.

---

## 1. Where the truth lives — read before designing anything

### In this repository (`watch-apps`)

| Thing | Where | What it tells you |
|---|---|---|
| The shared map layer | `MapKit/README.md` — **read all of it** | The selection rule, the three states that must not look alike, the hard-won constraints, the four-places rule for new sources, and the request log that closes the loop back to the pack builder |
| The RAM ceiling | `MapKit/Header/MapKit/TileCache.hpp` | `SLOTS = 1` is a measurement: 2 slots overflow `.bss` by 33,884 bytes on `RunMap`, 4 slots by 165,044. This is your entire budget conversation |
| Which pack, and how cheaply | `PackCatalog.hpp`, `PackSelection.hpp`, `PackDepth.hpp` | The peek is 292 bytes per file and rejects anything the drawing code would be wrong about. `PackDepth` exists because ranking on a *declared* `zoom_max` let an empty pack win |
| The verifier | `MapManager/README.md`, `Software/Libs/Header/Service.hpp:76` | Format-agnostic: every tracked file is an opaque blob with a trailing 4-byte little-endian CRC-32 footer. Only the tracked extension is `.rawtiles`-specific |
| The trust contract | `MapManager/Software/Libs/Header/PackTrustMarker.hpp` (normative), `MapKit/Header/MapKit/PackTrustReader.hpp` (mirror) | 16 bytes, tri-state, and the `(size, crc)` guard is the consumer's job |
| The credit that is owed | `MapKit/Header/MapKit/AttributionFace.hpp` | ODbL § 4.3 and the OSMF guidelines, implemented: pack-authored strings, shown at startup, dismissible, never substituted |
| Where a map was missing | `MapKit/Header/MapKit/TileRequestLog.hpp` | `SharedData/maps/requested-tiles.txt`, z12 tiles, first visit only. **This is the input to the area picker you are building** |
| The drawing that exists | `MapTileView.hpp/.cpp`, `TrackFaceMap.hpp` | One `draw()` pass: fill, `blitCopy` mosaic, Bresenham trace as small `fillRect`s, marker. `drawPartialBitmap` has two confirmed clipping defects on this target — do not reach for it |

### In `slippypack` (the host half of the raster pipeline)

- `MAP_CARTOGRAPHY_SPEC.md` — **the most useful document you will read.** The 64-colour
  panel model, the 14-slot palette with L\* and contrast ratios, rules R1–R5, line weights,
  the z11–16 ladder, `tile_dim = 128`, and § 9's proof that one pack plus four LUTs covers
  day / night / high-contrast / trail. Its **R4, "one code per feature class, no
  exceptions", is already a vector schema** — that is the seam this whole project walks
  through.
- `MAP_DELIVERY_WORKFLOW.md` — the verdict (pre-rendered archive → browser slice → USB),
  § 5.2's "what the watch app must provide" table, § 5.3's change list to the format, and
  the ranked risks. `MAP_END_USER_PATH.md` — the T1 / T2 / T3 tiers and the ranked source
  recommendation. `MAP_COMPLIANCE_APPENDIX.md` — which sources are permitted, and why the
  obvious one is not.
- `PLAN.md` (design and phasing), `DECISIONS.md` (~1000 numbered decisions with rationale),
  `crates/slippypack-core` (writer, reader, quantiser, projection, UUIDv5 identity, atomic
  write, per-host rate limiter). **Reuse this crate. Do not fork it.**

### In `una-sdk`

- The 2026-08-05 rawtiles map evaluation and its `findings/ecosystem.md` § 7 — the decision
  you are reopening, and the prior-art sweep behind it. Read it before arguing with it.
- `poc/athensrun`, `feat/rawtiles-container`, `Docs/deploy.md`, `cmake/una-app.cmake`
  (the 600 K GUI / 500 K service budgets), `Tests/Host/support/KernelTestDoubles.hpp`.

---

## 2. The premise, and what would refute it

State this arithmetic in your first investigation bundle, corrected against what you
actually measure:

- A raster pack carries every zoom separately, and the ladder is dominated by its deepest
  level — at E3's measured ≈ 2.2× per level (RLE ratios *improve* with zoom, so it is not
  the naive 4×), z12–16 costs roughly 1.8× what z16 alone costs. Vector carries geometry
  once and draws every zoom from it, including zooms it does not store.
- A raster pack has one style baked in. The four activity variants exist only because a
  palette-first pack can be LUT-mapped at blit time — a genuinely clever recovery from a
  constraint that vector does not have.
- Labels are baked at build time, so a pack is frozen to one locale
  (`MAP_CARTOGRAPHY_SPEC.md` § 5's stated revisit trigger).
- North-up is forced by `blitCopy` having no rotation. Vector geometry can be rotated in the
  projection, so **track-up becomes possible for the first time**. Do not build it in v1;
  do note it, because it is the largest user-visible prize on this road.

**The measurement that decides the premise (Gate A).** Build the *same ground* both ways —
the Athens 10 × 8 km extent is the natural subject, since a real raster pack of it exists to
compare against — and report bytes for: the shipped raster pack; the same area re-rendered
palette-first at `tile_dim` 128 with RLE; and your vector pack covering the same area at the
same *effective* zoom range. **If vector is not at least 10× smaller than the RLE raster
equivalent, the pivot has failed its own premise** and the right answer is to say so and
spend the effort on `tile_dim` 128 + RLE + the catalog instead. Report the number before you
build anything on top of it.

---

## 3. The decision you are reopening, and the gate that reopens it

`findings/ecosystem.md` § 7 concluded that every practical system pre-rasterises somewhere,
and that vector rendering on this watch was out. That was not a lazy conclusion and the
hardware has not changed. What has changed is the *scale* question in § 2 and the fact that
the palette work has since reduced on-device cartography to a 14-slot, one-code-per-class
model — which is exactly the shape a tiny renderer can execute.

So **Phase 0 is a spike, not a feature**, and it exists to kill the project cheaply if the
device says no. Three gates, all measured on hardware, all before any format is frozen:

- **Gate B — RAM.** The renderer's entire static working set must link into `RunMap`, the
  GUI that sets the shared ceiling. Your budget is the 64 KiB `TileCache` currently owns
  plus the ~31 KiB of headroom implied by the 2-slot overflow figure — call it ~96 KiB, and
  **re-measure it rather than trusting that subtraction**. A 240 × 240 ABGR2222 canvas is
  57,600 bytes of it. Anything that does not link is not a design.
- **Gate C — time.** Render the densest real viewport you can find (a European city centre
  at the deepest zoom you support) and measure the full path: seek, read, decode, rasterise,
  blit. **Budget: 100 ms.** Context for why that is the number and not a bigger one — a
  10 s GUI freeze from a 45 MB CRC scan was survivable, and at 201 MB the same freeze tripped
  the app-liveness watchdog and force-restarted the watch. The GUI runs at 10 fps and fixes
  arrive at 1 Hz; a redraw that misses a frame is fine, one that misses a second is not.
- **Gate D — legibility.** A 1:1 screenshot of your vector render beside
  `images/x12_arms_1to1.png` from the cartography investigation. Vector must not look worse
  than palette-first raster. If it does, the palette is being disobeyed somewhere; the
  spec's rules R1–R5 are the fix, not more colours.

**If Gate B or C fails, stop and write it up.** A truthful "measured, does not fit, here is
what it would take" is a good outcome for this phase. The fallback path is already known and
already recommended in `MAP_DELIVERY_WORKFLOW.md`, so failure here costs a spike, not a
product.

---

## 4. Hard constraints

- **Runtime.** Cortex-M33 (STM32U5A5), `-Os -fPIC -fno-exceptions -fno-rtti`, C++17. No
  exceptions, no RTTI, **no heap in the draw path**, no allocation after init. GUI 600 K,
  service 500 K. Apps do not create threads; the GUI thread is the only thread your renderer
  runs on and everything above is a promise you make to it.
- **Display.** 240 × 240, 8 bpp ABGR2222 on a Sharp LS012B7DD06A reflective memory-in-pixel
  panel. 64 colours, exactly — quantisation to it is lossless, not lossy. L\* runs 23.7 →
  100: there is no deep black, only three codes below L\* 40, and only one of them
  (`0xC0`) is neutral. Green carries 66.8 % of luminance. No anti-aliasing worth the name:
  every edge is hard. `LCD::blitCopy` is the proven path and cannot rotate.
- **Filesystem.** Sandbox-relative paths only — absolute volume-prefixed paths never resolve
  on hardware. `../SharedData/maps/` is how an app reaches the shared directory. `IFile` is
  absolute-seek plus `char*` read, no positioned read, no mmap. First filesystem touch after
  app start ≈ 113 ms; a 64 KiB tile read 6–9 ms thereafter.
- **Never CRC-verify on the GUI thread.** Trust comes from `MapManager`'s marker, polled
  cheaply. This applies to the new format identically and is not negotiable.
- **The activity never depends on the map.** In every failure state the run records and the
  breadcrumb draws. A vector decoder that hits a malformed tile mid-viewport must degrade to
  a state on the `MapStatus` list, not to a blank screen with no explanation and never to a
  fault.

---

## 5. The format

A new container. Working name **`.rawvec`**, spec in its own repository beside `rawtiles`,
versioned with the same discipline (normative document, wire version, conformance corpus of
golden and negative fixtures). Rename it in one commit before anything ships if the owner
prefers something else; do not leave the name to accrete.

**Reuse from `rawtiles`, deliberately and near-verbatim**, because these parts are proven and
because two formats that agree cost the reader half as much:

- Magic + wire major/minor, `pack_uuid` / `supersedes_uuid`, WebMercator + XYZ, bbox in
  microdegrees, `build_timestamp`, a fixed-size header whose length is a constant, a
  `(offset, count)` zoom directory, a tile index sorted strictly ascending by `(z, x, y)`,
  4-byte alignment, extension sections including **`ATTR` and `NAME` with the same strict
  UTF-8 / NFC / LF rules**, and a **trailing 4-byte little-endian CRC-32 footer**.
- That footer is what lets `MapManager` verify this format with no format knowledge at all.
  Departing from it means writing a second verifier; do not.
- Take `MAP_DELIVERY_WORKFLOW.md` § 5.3's change list with you: **C1** (attribution inside
  the identity descriptor) should be designed in from the start here rather than retrofitted,
  and **C0**'s lesson — that a display transfer function does not belong welded into a
  format — applies doubly to a format that ships no pixels at all.

**New, and where the design work actually is:**

- **A per-tile layer directory.** The renderer must be able to seek to one layer of one tile
  and read only that. This is not an optimisation; it is what makes painter's-order drawing
  possible across a multi-tile viewport within Gate B's budget — one layer of one tile
  resident at a time, L passes over the visible tiles, rather than every visible tile's
  entire geometry in RAM at once.
- **Hard, header-declared caps: bytes per tile, features per tile, points per feature.** The
  device's work must be bounded by the *header*, before a single tile is read. A writer that
  cannot meet a cap must split or simplify, never emit an oversized tile. Every cap is a
  reader-enforced validation rule, with a negative fixture.
- **Geometry encoding.** Tile-local integer coordinates over a declared extent (4096 is the
  conventional choice and is one shift from pixels at sensible tile sizes), zigzag varint
  deltas, explicit move/line/close commands. Points, lines, polygons. Clipping to a declared
  buffer at build time so the reader never clips a polygon, only rows.
- **Classes, not attributes.** One `u8` feature class per feature, drawn from a fixed
  enumeration that **is** `MAP_CARTOGRAPHY_SPEC.md` § 3's slot list: `water`, `water_dk`,
  `wood`, `wood_lt`, `landuse`, `building`, `road_major`, `road_minor`, `path`, `contour`.
  No free-text tags, no key-value maps, no per-feature style. R4 already says one code per
  class; this format is that rule made structural. Reserve the enum's high range so a class
  can be added without a wire break, and specify that a reader **skips unknown classes
  silently** — that is the forward-compatibility hinge and it needs a fixture.
- **Layer draw order is in the format, not in the app.** Three apps and a desktop preview
  must agree on what covers what.
- **The zoom ladder is sparse on purpose.** A vector tile at z13 can draw at z15. Decide and
  document which zooms carry geometry and what generalisation each one got; the app derives
  the rest. This is where most of the size win lives, and the legibility cost of overzooming
  too far is a Gate D question, not a matter of taste.
- **Labels: v1 stores anchors and strings, and the app may draw none of them.** Be honest
  about this in the README rather than quiet: baked raster labels are a real capability the
  vector path loses on day one, and a placement engine with collision boxes inside a 600 K
  GUI is not a v1 deliverable. Reserve the section, populate it, draw at most a handful with
  a trivially simple grid-based collision test, and measure it against Gate C before
  believing it.

---

## 6. On the watch — `MapKit`

`MapKit` becomes a two-format layer. Nothing about the existing raster path may regress, and
the parts that are pure code stay pure.

- **`VecContainer`** — reader for `.rawvec`, same shape as `SDK/RawTiles/Container.hpp`:
  eager full structural validation at open with an explicit result enum per rule, streaming
  seek+read over `IFile`, a caller-owned buffer, no heap, `skipCrcVerify` semantics
  identical. Write it as a first-class citizen here rather than vendoring it in — the
  vendoring of `Container.hpp` exists because that spec was moving under an SDK surface, and
  the same argument says a format you own in-flight belongs beside its consumer.
- **`VecRenderer`** — decode-and-rasterise into a static canvas. The recommended architecture,
  which you should adopt unless measurement says otherwise: **one 240 × 240 ABGR2222 canvas
  (57,600 bytes) with static storage duration**, painted layer by layer, then handed to
  `blitCopy` as a single bitmap. This is budget-neutral against the `TileCache` slot it
  replaces, reuses the one blit path proven on this hardware, and redraws at the 1 Hz fix
  cadence rather than at 10 fps because the panel holds its image for 11 µW. Polygon fills
  need a scanline crossing list bounded by the format's points-per-feature cap; lines reuse
  the Bresenham `fillRect` stepping `MapTileView` already does for the trace. The canvas is
  file-static in each app's `Model.cpp`, for the same linker-arbitrates-RAM reason the tile
  cache is.
- **Selection, with two formats present.** `PackSelection` currently ranks on the deepest
  zoom a pack has tiles at, and that fact is exactly right for raster and exactly wrong for
  vector: a vector pack's stored zoom is a statement about generalisation, not about how deep
  it can draw. **Introduce a per-format "drawable depth" and rank on that**, keeping the
  existing tie-breaks (smaller bbox, then lexicographically first filename) unchanged.
  Decide deliberately what happens when a raster and a vector pack both cover the fix, write
  the reason down, and test both orders — a silent swap mid-activity is the failure mode
  `PackSelection.hpp` already warns about for trust.
- **`PackCatalog` peeks both.** Dispatch on extension, read each format's header length, and
  keep the peek a screen rather than a validation. The existing rejections (tile dim, pixel
  format, projection, addressing) have `.rawvec` analogues; write them.
- **`MapStatus` is unchanged, and that is a requirement.** `NoFix` / `NoPack` / `PackError` /
  `Corrupt` / `Verifying` / `OffCoverage` / `Live` are the wearer-facing vocabulary and they
  are format-independent. `OffCoverage` for vector means "no tile at this spot", tested
  against the index, not the bbox — same rule, same reason.
- **`TileRequestLog` keeps writing z12 tiles.** Do not add a format field to it. A person
  who was somewhere with no map wanted a map, not a raster or a vector one, and the file
  format is `mapkit-requested-tiles v1` with a stated de-duplication key.
- **`AttributionFace` reads `ATTR` from vector packs too.** ODbL applies to geometry at least
  as much as to pixels.
- **Four places.** Every new source file goes in `mapkit.cmake` **and** the three apps'
  `simulator/gcc/Makefile`s. The CMake build works without the Makefiles and the omission
  only ever surfaces as a link error in a simulator nobody ran; `MapManager` shipped with
  exactly that defect once.

---

## 7. `MapManager`

Small and mostly mechanical, which is the point — the design already anticipated this.

- Track a **list** of extensions rather than one constant (`Service.hpp:76`). The verifier
  itself is format-agnostic and stays untouched: opaque blob, trailing CRC-32 footer.
- The roster row should say which format a pack is, because "you have three packs and none
  of them draws" is a sentence somebody will need to debug over USB.
- Extend `Service_test.cpp`'s discovery cases: a directory holding both formats, a
  `.rawvec.trust` sibling not mistaken for a pack, and the re-arm-on-size-change case that
  rescues a pack discovered mid-copy.
- Nothing about the trust marker changes. `<path>.trust` appends to the whole filename and
  works for any extension.

---

## 8. On the host — the pipeline that builds packs

**Extend `slippypack`; do not start a new tool.** Its core already owns projection, UUIDv5
identity, atomic write, SIGINT handling, per-host rate limiting, the CLI shell and the
planned PWA/OPFS machinery, and its `TileWriter` trait exists precisely as a
format-pluggability seam. A new `slippypack-vec` crate behind that seam inherits all of it
and keeps one tool in the user's hands.

- **Sources, in the order `MAP_END_USER_PATH.md` § 3 ranks them:** Protomaps' PMTiles basemap
  first, OpenFreeMap second, Geofabrik/BBBike PBF via `tilemaker` as the fallback for bespoke
  cartography. Vector-in, vector-out is a strictly easier pipeline than the raster one — no
  MapLibre Native, no `render_static`-per-tile problem, no C++ toolchain in `cargo install`,
  no Windows CI risk. **Phase 2's three named risks disappear on this path**, which is a
  genuine and underappreciated argument for the pivot; say so in the write-up, with the
  evidence.
- **The UI refuses prohibited hosts** rather than warning about them, and auto-fills `ATTR`
  per source kind. Both requirements carry over verbatim from `MAP_END_USER_PATH.md` § 4 and
  neither is satisfied by documentation.
- **Schema mapping is a checked-in, reviewable file**, not code: source layer/class →
  `.rawvec` class enum, per zoom. Somebody will need to argue about whether a `track` is a
  `path`, and that argument should happen in a diff.
- **Generalisation per stored zoom** — Douglas-Peucker or equivalent, with the tolerance
  stated in ground metres and justified against the panel's 126 µm pixel pitch, plus
  minimum-area culling for polygons that would render sub-pixel. This is where the size win
  is actually realised and it must be reproducible.
- **Determinism.** Same inputs, byte-identical pack, on every platform. It is what makes a
  catalog cacheable, an identity meaningful and a bug report reproducible.
- **A size estimate before the build runs**, in the tool. It is pure arithmetic, needs no
  network, and it is the only thing between a user and a surprise that does not fit their
  watch.
- **`inspect` for the new format** — the raster CLI already has one, and a human-readable
  dump of a pack's header, layer counts and per-zoom byte breakdown is how every size
  question after this gets answered.
- **A desktop preview that renders a `.rawvec` exactly as the watch will**, at 240 × 240,
  from the same palette. Ideally the device rasteriser compiled for the host, so the preview
  cannot drift from the renderer. This is worth more than it sounds: cartography iterations
  are minutes on a laptop and a firmware flash on a watch.

---

## 9. The user's path — the thing this is all for

`MAP_END_USER_PATH.md`'s three tiers survive the pivot unchanged, and vector makes T1 and T2
better rather than differently shaped.

- **T1 — "load one region on day one, not one pack per run."** A catalog of ready-made
  regional packs, static files with a browsable index. Vector packs should make a *country*
  the natural unit where raster made a metro area one. Confirm that with a real number and
  update the copy the watch's empty state shows.
- **T2 — draw a box.** A slice of a pre-rendered archive is a range read plus a re-index; the
  sorted `(z, x, y)` index and the zoom directory make that a filtered concatenation with
  recomputed offsets, exactly as it is for raster. Keep that property in the format design —
  it is cheap to keep and expensive to add later.
- **Seed the picker with the watch's own request log.** `requested-tiles.txt` is z12 tiles
  the wearer actually visited with no coverage, sitting in the same directory the packs go
  in, reachable over the same USB connection. "Import from watch" should be one button that
  turns those lines into a selection. This closes a loop the watch half already built and
  nothing yet consumes.
- **Also accept a GPX route** and offer a corridor around it. It is the same selection
  primitive and it is what "I'm running the Y trail on Saturday" actually means.
- **Transfer stays USB mass storage.** It works today. But re-run the arithmetic in risk R2:
  a 29 MiB metro raster pack over BLE is 5–16 minutes, and if vector makes the same coverage
  1–3 MiB, **phone-first transfer stops being blocked by size and starts being blocked only
  by whether the mobile app can carry a file at all.** That is a materially different
  product and it is worth a paragraph in the write-up even though it is not v1 work.
  Whatever the channel: serialise writes against BLE sync — concurrent USB-MSC and BLE sync
  is a documented volume-corruption mode — and verify after copy.

---

## 10. Phases, each ending in a measurement

| # | Phase | Exit condition |
|---|---|---|
| 0 | **Spike.** Hand-built geometry for one real area, a throwaway reader, the canvas renderer, on hardware | Gates B, C, D of § 3, plus Gate A's size comparison from § 2. **Stop here if any fail** |
| 1 | **The format.** Spec document, conformance corpus (golden + negative per validation rule), `slippypack-vec` writer, `VecContainer` reader | Reader and writer agree on the corpus; the corpus fails a reader with any single rule deleted |
| 2 | **The renderer, properly.** `VecRenderer` in `MapKit`, layered draw, all three apps building, simulator green | A real pack of your own city drawing under a live trace in the simulator, and Gate C re-measured on the real reader |
| 3 | **Coexistence.** Two-format catalog, drawable-depth selection, `MapManager` extension list, status states, attribution | Host tests cover a mixed directory; a watch carrying one pack of each format draws the right one and says why |
| 4 | **The pipeline.** Real sources, schema mapping, generalisation, determinism, size estimate, `inspect`, desktop preview | Two independent machines produce byte-identical packs from one source |
| 5 | **The picker.** Box selection, GPX corridor, import-from-watch, prohibited-host refusal, auto-filled attribution | Somebody who has not read any of this gets a working pack onto a watch, timed, without being told what to type |
| 6 | **The catalog.** Pre-built regional packs and an index | A new owner is covered on day one without building anything |

---

## 11. Tests and evidence

- **Keep the pure/impure split `MapKit` already has.** Geometry decoding, the class enum,
  clipping arithmetic, generalisation-independent projection and the selection rule are pure
  code with no SDK, no kernel, no filesystem, tested in `mapkit-pure-tests`. The rasteriser
  is pure too if you let it be — pass it a buffer and a stride — and that is worth designing
  for, because it is the part with the most arithmetic and the least visibility.
- **Fixtures build the smallest legal pack by hand**, as `PackFixture.hpp` does, with the CRC
  computed independently of the reader's own implementation and cross-checked against a
  pinned spec vector. A green test must not be one implementation agreeing with itself.
- **Check the important tests by mutation.** Delete the `(size, crc)` guard, the unknown-class
  skip, a per-tile cap, the corrupt-pack exclusion — each should fail tests, and the count
  belongs in the README the way the existing ones do.
- **`UNA_SDK` for the tests must point at a checkout whose `InMemoryFileSystem` has
  `InMemoryDirectory`** — the enumerating fake, currently only on `poc/athensrun`. The app
  build wants `apps-v1.3.0`. Two checkouts; say which in the tests' README.
- **The simulator earns its keep** — it caught a message-queue overflow, a list that never
  repainted and two clipped columns in `MapManager`. Run it before every hardware trip.
- **Hardware measurements are logged, not remembered.** Throughput, render time, RAM
  headroom, per-zoom byte counts, with the date and the build.

---

## 12. Delivery

- Build the way the repo does, and check it with Kira:
  `kira build-app --app RunMap --sdk /path/to/una-sdk --version <v> --out RunMap.uapp`.
  `$UNA_SDK` points at `apps-v1.3.0` for these apps; mainline is
  `KERNEL_INTERFACE_VERSION 3` and the mismatch is invisible until the app silently fails to
  run.
- **Nothing in `MapKit` may use `__FILE__` or `assert`.** Kira's `-fmacro-prefix-map` covers
  the SDK and the app subdirectory but not a sibling shared directory, so a `__FILE__` here
  would bake a build path into a shipped binary and make its CRC depend on where it was
  built. Measured as latent, not open; keep it that way.
- **Docs are deliverables, in this repository's house voice.** `MapKit/README.md` gains the
  vector half — what it is, the format contract, the renderer's budget with its measured
  numbers, and **what it does not do** (labels, rotation, whatever else you deferred).
  `MapManager/README.md` gains the second extension. Each app's README gains a row.
  `slippypack`'s `DECISIONS.md` gains numbered entries for every choice above, and the
  cartography spec gains a section on what changes when the style lives on the watch instead
  of in the pack.
- The root `README.md` table stays accurate.

---

## 13. Non-negotiables

- The activity records and the breadcrumb draws in every failure state, always.
- No CRC verification on the GUI thread, ever, in any format.
- No heap in the draw path; no allocation after init; every buffer fixed-size and
  linker-arbitrated.
- The three states that must not look alike stay distinct, and none may look like a crash.
- Attribution is shown, is the pack's own words, and is never abbreviated or substituted.
- No pack is drawn before `MapManager` says `Good` for those exact bytes.
- Every cap, tolerance and threshold carries the measurement that justified it, or a TODO
  naming the experiment needed to justify it.
- Sandbox-relative paths only.
- A number you did not measure is not a number you may write down.

---

## 14. Done

A person who has never opened a terminal picks their city in a tool, gets a file, drags it
onto a watch, and on Saturday sees their own streets under their trace — at a size where
"which pack do I need this weekend" has stopped being a question they ever have to ask.

And the honest version of the other outcome: you measured, the device said no, and you wrote
down exactly what it would take. Both of those are successful completions of this prompt.
Only an unmeasured claim is a failure.
