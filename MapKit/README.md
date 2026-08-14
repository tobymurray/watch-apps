# MapKit — the offline-map layer the map-enabled activity apps share

One copy of the map code, sitting beside the apps that use it rather than
inside any of them. [`BikeMap`](../BikeMap), [`HikeMap`](../HikeMap) and
[`RunMap`](../RunMap) are otherwise the stock `Cycling` / `Hiking` / `Running`
apps; everything that makes them *map* apps is here.

It draws a live basemap under the GPS breadcrumb on the in-activity screen:
tiles from a `.rawtiles` pack underneath, the trace on top, one line of status
at the bottom. It does not fetch packs, generate them, or verify them —
[`MapManager`](../MapManager) owns all of that, and these apps are pure
consumers of the verdict it publishes.

## What is in here

```
MapKit/
├── mapkit.cmake                  # MAPKIT_SOURCES / MAPKIT_INCLUDE_DIRS
├── Header/
│   ├── SDK/RawTiles/Container.hpp    # vendored .rawtiles reader
│   └── MapKit/
│       ├── MapMath.hpp               # WebMercator lat/lon -> world pixels
│       ├── TraceBuffer.hpp           # the breadcrumb ring buffer
│       ├── TileCache.hpp             # fixed-slot LRU of decoded tiles
│       ├── TileRequestLog.hpp        # where the map was missing
│       ├── PackTrustReader.hpp       # reader for MapManager's trust marker
│       ├── PackSelection.hpp         # *which pack* — the rule, as pure code
│       ├── PackCatalog.hpp           # what packs exist, cheaply
│       ├── MapSession.hpp            # all live map state for one app
│       ├── MapTileView.hpp           # the tile+trace drawing widget
│       └── TrackFaceMap.hpp          # the activity screen's map face
├── Sources/
└── Tests/                            # host tests, see below
```

The split worth knowing: **`PackSelection` and `MapMath` and `TraceBuffer` are
pure code** — no SDK, no kernel, no filesystem — and are tested as such.
`PackCatalog`, `PackTrustReader` and `MapSession` are the parts that touch
files. `MapTileView` and `TrackFaceMap` are the parts that touch pixels, and
are the only parts no host test reaches.

## Which pack to draw

This is the one design decision these apps had to make that the proof of
concept did not. `AthensRun` hardcoded a single pack path. A generic app
cannot: `MapManager` discovers and verifies *every* `*.rawtiles` file dropped
into `../SharedData/maps/`, and a watch can carry several — a city at high
zoom, a region at low zoom, somewhere last summer's holiday was.

**The rule.** Among the packs whose bounding box contains the current fix, and
that are not *known* corrupt, pick:

1. the largest `zoom_max` — the most detail available here;
2. tie-break on the smallest bbox area — the more local of two packs that reach
   the same zoom is the one built for this place;
3. tie-break on the lexicographically first filename — so the choice is
   deterministic and reproducible in a bug report, never "whatever the
   directory happened to list first".

If nothing contains the fix there is no selection: the trace draws on the
background and the status line says `no map for here`. A pack that does not
cover you is a blank screen either way, so preferring a distant one over none
would only be a more confusing blank screen.

**Verification state is deliberately not part of the ranking**, and this is the
part worth arguing with. Trust is not a property of a pack's *suitability*; it
is a property of how far a background scan has got. Ranking on it would mean
the map silently swaps from the coarse pack to the detailed one partway through
an activity, at a moment governed by disk throughput — which reads as a glitch,
and is worse than a few seconds of an honest `verifying map`. So trust gates
*rendering*, not *choice*.

`Bad` is the one verdict that does participate, as an exclusion. It is final —
a file confirmed corrupt does not become uncorrupt by being read again — so
continuing to prefer a corrupt pack over a working one would be choosing a
permanently blank screen on purpose.

**Why not a configured pack name.** Kira's `[config]` block could write the
wanted filename into the app's folder, and that would be less code. It was
rejected because it puts the work on the wearer at exactly the wrong moment:
the answer to "which pack" changes when you travel, which is when you are least
able to plug the watch into a computer and edit a JSON file. Coverage is a
question the pack header already answers, for free, correctly, every time.

**When the question gets asked**, and the distinction that matters:

- **The directory scan runs exactly once**, on the first GPS fix. Packs cannot
  appear while the app is running, because connecting USB to copy one
  terminates every running app (see
  [MapManager's README](../MapManager/README.md#reading-the-log-every-usb-connection-restarts-the-service)),
  so a rescan could only ever re-find what the first one found. The accepted
  cost is that a pack arriving mid-run is not picked up, which costs nothing
  real for that same reason.
- **Re-selection runs whenever it could change the answer** — nothing selected,
  or the wearer has left the selected pack's box. It is pure: it runs against
  the catalog already in memory and costs nothing worth throttling. It has to
  keep running, because walking *into* coverage is how a wearer who started
  outside every pack gets a map at all.
- **The marker poll keeps running** until the verdict resolves either way,
  because that genuinely does change underfoot.

Those first two being one function was a real defect, and the simulator is what
found it: with the maps directory emptied, "no pack selected" is true forever,
so a scan keyed on it ran *every GPS sample* — a directory walk plus a header,
footer and marker read per pack, once a second, for a whole activity, on the
GUI thread, to re-learn what it already knew. It is now two functions and a
regression test
([`TheDirectoryIsScannedExactlyOncePerLaunch`](Tests/MapSession_test.cpp)) that
fails if they are merged again.

## The three states that must not look alike

> "no pack" and "pack not yet verified" must not look alike, and neither may
> look like a crash.

In every one of them the activity keeps recording and the breadcrumb keeps
drawing on the background. Only the status line differs:

| `MapStatus` | Status line | What it means |
| --- | --- | --- |
| `NoFix` | `acquiring GPS` | No fix yet, so "which pack covers me" has no answer. |
| `NoPack` | `no map for here` | Looked; nothing on this watch covers this position. |
| `Verifying` | `verifying map` | Pack is structurally fine; no verdict yet. **Resolves itself.** |
| `Corrupt` | `map pack corrupt` | MapManager confirmed these exact bytes corrupt. Final. |
| `PackError` | `map: <reason>` | The pack failed its structural open. Final. |
| `OffCoverage` | `off map  z<n>` | Pack is live, but there is no tile at this spot and zoom. |
| `Live` | `z<n>` | Drawing. |

`Corrupt` is its own state rather than a `PackError` with a reason, and that is
a bug fix carried over from the proof of concept. A corrupt pack opens *fine*
structurally — the CRC is the only thing wrong with it — so `AthensRun`, which
routed that state through `Container::describeResult(openResult)`, displayed a
broken map as **`map: ok`**.

`OffCoverage` is tested against the tile index, not the bounding box: a bbox is
a rectangle drawn around a set of tiles, not the set itself, and it only
promises coverage somewhere in `[zoomMin, zoomMax]`, not at every zoom. The
honest test is whether the tile under the crosshair actually exists.

## Asking for the maps that are missing

A watch that says `no map for here` knows something useful, and until this it
threw it away. `TileRequestLog` writes it down: one line per distinct area
entered without tile coverage, appended to
`../SharedData/maps/requested-tiles.txt` — beside the packs, where a desktop
tool can pick it up over the same USB connection used to deploy packs in the
first place.

It closes the loop the other way round from everything else here. MapManager
tells apps which packs are trustworthy; this tells whoever makes the packs
which ones are missing.

```
# mapkit-requested-tiles v1
# Places visited with no map coverage. One line per distinct tile, first
# visit only. z/x/y are XYZ (slippy) tile coordinates; lat/lon is that
# tile's centre in degrees. Duplicates across sessions are possible --
# de-duplicate on z/x/y when consuming.
12/2371/1402 49.2391 28.4326 BikeMap
```

**It records a tile, not a fix**, quantised to `kRequestZoom` (z12, the
coarsest zoom real packs carry), and only the first visit to each tile. That is
not a privacy gesture bolted on afterwards — it is what makes the thing work at
all:

- *The unit of a pack is an area.* "Build something covering these tiles" is a
  request a generator can act on; ten thousand fixes is one it would have to
  reduce first.
- *It bounds the writing.* At 1 Hz, an unquantised log would append tens of
  thousands of lines per activity, on the GUI thread, to internal flash.
  Quantised, a walk crosses a z12 tile boundary every half hour or so.
- *It bounds what is disclosed.* This file sits in a directory every installed
  app can read and anyone who plugs the watch in can copy. A z12 tile is
  several kilometres across, so it says "somewhere around here" — all a pack
  generator needs, and rather less than a track log. Worth knowing before
  handing the watch to someone.

**Only two states file a request:** no pack covers the position, and a pack was
selected but has no tile here at this zoom. A corrupt or unopenable pack also
leaves the screen blank, but the answer there is to re-copy a pack that already
exists, not to build a new one — filing those would put work in the queue that
nobody should do.

The extension is deliberate. MapManager tracks `*.rawtiles` and would adopt and
CRC-verify anything that matched, so this is `.txt` and a test asserts it stays
that way.

Bounds: `kMaxTilesPerSession` (64) distinct tiles per run of the app, and the
file stops being appended to at `kMaxBytes` (256 KiB) rather than rotating — an
old request is exactly as valid as a new one, so dropping the oldest to make
room would discard the very thing being collected. De-duplication is
within-session only, so returning to the same uncovered place on another day
appends the same line again; the header says to de-duplicate on `z/x/y`, and
the file is meant to be collected and cleared rather than kept forever.

## The contract with MapManager

The **normative spec** of the trust-marker format is the class comment on
[`MapManager/Software/Libs/Header/PackTrustMarker.hpp`](../MapManager/Software/Libs/Header/PackTrustMarker.hpp).
`PackTrustReader.hpp` here is a read-only *mirror* of it, and says so. If the
two ever disagree, that one is right.

Two things about this consumer half are worth stating on their own:

- **The `(size, crc)` guard is the consumer's job, not the marker's**, and it
  is the one part an integrator can silently omit and still appear to work. So
  it is not offered as an option: `PackTrustReader::verdictFor(fileSize, crc)`
  applies it, and `read()` — the unguarded primitive — exists mainly so the
  difference stays visible. Every case that ends in `Absent` in
  [`PackTrustReader_test.cpp`](Tests/PackTrustReader_test.cpp) is a case where a
  reader that skipped the guard would have said `Good` or `Bad` instead.
- **Write support is deliberately absent.** MapManager owns verification. An
  app that could write a marker could publish a verdict it never earned, and
  markers live in a directory every installed app can write.

## Hard-won constraints this code is shaped around

None of these are theoretical; each cost somebody real time.

- **Never CRC-verify on the GUI thread.** `Container::openFromFile()` is always
  called with `skipCrcVerify=true`. A mandatory whole-file scan froze the GUI
  for ~10 s at 45 MB and, at 201 MB, tripped the app-liveness watchdog and
  **force-restarted the watch**. Skipping only the CRC is safe: every other
  structural rule still runs, so a genuinely malformed pack still fails
  immediately. Trust comes from MapManager's marker, polled cheaply, never
  blocked on.
- **`PackCatalog` peeks headers; it does not open packs.** A structural open
  walks the whole tile index — one seek+read per entry, thousands on a real
  pack. Doing that for every candidate just to decide which one to draw would
  put an unbounded multiple of that cost on the GUI thread. The peek is 292
  bytes per file. Only the winner is opened.
- **Absolute, volume-prefixed paths never resolve on hardware.** Only
  sandbox-relative ones do. `../SharedData/maps/` reaches the shared directory.
- **The tile cache must have static storage duration in the app**, not live in
  the TouchGFX `FrontendHeap` object, so the linker — not the heap — arbitrates
  the GUI RAM budget at build time. Each app's `Model.cpp` declares it
  file-static for exactly this reason.
- **`TileCache::SLOTS` is 1, and that is a measurement.** Re-measured for these
  apps against a pristine `apps-v1.3.0`, on `RunMap` (the largest of the three
  GUIs, and therefore the one that sets the shared ceiling):

  | Slots | Result |
  | --- | --- |
  | 4 | `.bss` will not fit in region `RAM`, overflowed by **165,044** bytes |
  | 2 | `.bss` will not fit in region `RAM`, overflowed by **33,884** bytes |
  | 1 | links |

  The 4-slot figure reproduces the ~160 KB the proof of concept recorded on the
  `Running` GUI, which is a good sign that nothing has drifted. Note that the
  2-slot margin is only ~34 KB: a second slot is not "nearly there". More slots
  need RAM reclaimed from the inherited activity GUI, not a bigger number.
- **The display is 2 bits per channel.** Map legibility depends heavily on the
  tile *style* the pack was built from — measured: CyclOSM quantizes well, OSM
  standard washes out. That is a pack-authoring concern rather than an app one,
  but it will be the first thing blamed when a map looks bad.

## The trace's zoom is the app's, not the pack's

`TraceBuffer` stores points as world pixels at a fixed `MapMath::TRACE_ZOOM`
(z18) and shifts them to the display zoom at draw time. It has to be fixed
rather than pack-derived, because the breadcrumb outlives pack selection: an
activity may cross from one pack to another and must keep one continuous trace.

z18 is chosen because the world is then 67,108,864 px on a side — comfortably
inside the `int32` the buffer stores — while being at least as fine as any pack
these apps will meet (the real packs measured for this work are z12–z16). A z16
render from z18 storage is bit-identical to storing at z16 natively, which
[`MapMath_test.cpp`](Tests/MapMath_test.cpp) pins directly. The proof of concept
stored at z16 with a 6 px decimation threshold; 24 px at z18 is the same ground
distance, so the decimation behaviour is unchanged.

## Wiring it into an app

Two edits, one per build system. In the app's `<App>-CMake/CMakeLists.txt`:

```cmake
set(MAPKIT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../MapKit")
include(${MAPKIT_PATH}/mapkit.cmake)     # sets MAPKIT_SOURCES, MAPKIT_INCLUDE_DIRS
```

then fold `${MAPKIT_SOURCES}` into `GUI_SOURCES` and `${MAPKIT_INCLUDE_DIRS}`
into `GUI_INCLUDE_DIRS`. **GUI only** — nothing here belongs in a Service.

In the app's `simulator/gcc/Makefile`, every MapKit source has to be listed in
`ADDITIONAL_SOURCES_UNA` and `MapKit/Header` added to
`ADDITIONAL_INCLUDE_PATHS`. This is the step most likely to be missed, because
the CMake build will happily go on working without it and the omission only
shows up as a link error in a simulator nobody ran. MapManager shipped with
exactly that defect for a while — a Makefile missing one source file, which had
therefore never linked. **If you add a file to `Sources/`, it goes in four
places: `mapkit.cmake` and three Makefiles.**

Sources are listed explicitly rather than globbed, deliberately. Each app's own
`touchgfx.cmake` globs `gui/src/**`, which is right there — everything under it
belongs to that app. Here it would not be: a stray file in this directory would
silently join three binaries at once.

## Layout, and why Kira is fine with it

Kira's one hard layout rule is that **each app directory contain exactly one
`*-CMake` project under `Software/`** — it refuses to guess between two. That
is checked by `find_project()` in `kira-cli`, which scans only
`<app_root>/Software/*/*-CMake/CMakeLists.txt`, exactly two levels deep. A
sibling directory at the repository root with no CMake project of its own
cannot break it.

And the shared directory really does reach the build: Kira's registry workflow
does `git fetch --depth 1 origin <rev>` then `checkout FETCH_HEAD` and passes
`--app src/<subdir>`, so the **whole repository** is on disk and `../MapKit`
resolves. The registry README recommends exactly this shape — one repository,
one manifest per app, "shared helpers are just a directory rather than a
submodule".

**One seam to know about.** Kira builds with
`-fmacro-prefix-map=<sdk>=/una-sdk -fmacro-prefix-map=<app>=/una-app` so its
builds are path-independent. `<app>` is the app *subdirectory*, so a sibling
`MapKit/` is covered by neither mapping — any `__FILE__` reaching a MapKit
translation unit would bake the absolute build path into the binary and make
its CRC depend on where it was built.

Measured, rather than assumed: building `BikeMap` twice from two checkouts at
different paths produces a **byte-identical GUI ELF**, and `strings` finds no
build path in it. So the seam is latent, not open. It is not hypothetical
either — the same comparison shows the *Service* ELF differing between the two,
carrying three absolute paths from `Software/Libs/Sources/`. Those are stock
files inside the app subdirectory, so Kira's own mapping covers them; the point
is that the mechanism is live in this codebase and MapKit is simply outside the
mapping that neutralises it.

So: nothing here uses `__FILE__` or `assert`, and nothing here should start to.
If MapKit ever needs a diagnostic that names its own file, the fix is a third
prefix map upstream in Kira, not a literal path in a shipped binary.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk        # see the note below
cd MapKit/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Two executables, because they need different things. `mapkit-pure-tests`
(`MapMath`, `TraceBuffer`, `PackSelection`) links no SDK at all — keeping those
parts buildable on their own is what keeps them pure. `mapkit-kernel-tests`
(`PackTrustReader`, `PackCatalog`, `MapSession`) uses the SDK's own kernel test
doubles.

**`UNA_SDK` for the tests must point at an SDK checkout whose
`InMemoryFileSystem` has `InMemoryDirectory`** — the real enumerating fake, not
the older `EmptyDirectory` stub that always reported no entries. `PackCatalog`'s
whole job begins with a directory scan. That enhancement currently lives on
`una-sdk`'s `poc/athensrun` branch rather than in `apps-v1.3.0`, so the app
build and the test build want different checkouts. Point `UNA_SDK` accordingly
for each. (This is the same constraint MapManager's tests carry, for the same
reason.)

What the fixtures do: [`PackFixture.hpp`](Tests/PackFixture.hpp) builds the
smallest legal `rawtiles` v1 pack by hand — a 292-byte header plus a 4-byte
CRC-32 footer, zero tiles — and the matching 16-byte trust markers. Its CRC is
computed independently of `Container.cpp`'s and cross-checked against the
spec's own pinned vector, so a green test cannot be one implementation agreeing
with itself.

Covered: the selection rule and each of its tie-breaks, bbox edge inclusivity,
whole-world bbox area without `int32` overflow, corrupt exclusion; the
`(size, crc)` guard in both directions (a stale `Bad` marker must not condemn a
correctly re-copied pack, just as a stale `Good` must not bless a replaced one);
the catalog's header screen and its refusal to truncate a path, its hard cap,
and that it leaves no open file handles behind; the request log's quantising,
its within-session de-duplication, appending to a file an earlier session left
behind, both its bounds, and that its path cannot be mistaken for a pack; the
inverse projection it needs, round-tripped against a tolerance derived from the
forward projection's own resolution rather than a number that happened to pass;
trace decimation, thinning that
coarsens rather than truncates and always keeps the newest point; the projection
and the trace-zoom rescaling identity; and `MapSession`'s whole state machine,
including `verifying → trusted` and `verifying → corrupt` resolving on a later
GPS sample with nothing else happening, walking into and out of coverage, the
scan-once property above, falling back to a second covering pack when the first
is condemned mid-activity, and `cycleZoom()` reporting that it did nothing so a
consuming app can hand its button back to whatever that button means elsewhere.

There is also a test for the whole feature being absent: no shared map
directory, no packs, ten GPS samples, and the breadcrumb still grows while
every map-facing accessor reports the honest nothing. A watch that never has a
pack on it is the common case, not an edge case.

Two of these have been checked by mutation rather than assumed: deleting the
`(size, crc)` guard fails six tests, and dropping the known-corrupt exclusion
from the selection rule fails three. The scan-once regression test was itself
wrong on the first attempt — it asserted against a handle map keyed by path,
which does not change when the same files are reopened — and was rewritten to
assert the visible consequence instead, then re-checked against the mutation.

**Not covered:** anything that draws. `MapTileView` and `TrackFaceMap` are
pixels, and no host test reaches them — see the honest account in each app's
README.

## Where this should live

All of this is copy-paste that should not stay copy-paste. Two seams, in
increasing order of how ready they are:

1. **MapManager's marker format wants to be a shared header in `una-sdk`.**
   That is the actual contract, it is what every consumer needs, and it is
   currently implemented three times: once in MapManager, once here, and once
   in `AthensRun`. MapManager's own README already argues for this and it is
   the smaller, more obviously correct move of the two.
2. **`MapKit` wants to be an SDK library.** It is not one yet, and it should not
   be moved as-is: the `rawtiles` container it depends on is itself vendored
   from a `una-sdk` feature branch (`feat/rawtiles-container`) while the format
   is still v0.x, and freezing an SDK surface around a moving spec is exactly
   what that vendoring was meant to avoid. The order is: `rawtiles` reaches
   1.0 → `Container` becomes an SDK library and the vendored copy here is
   deleted → the rest of `MapKit` follows it, and this directory becomes a
   README pointing at the SDK.

Until then, the seams are clean: `SDK/RawTiles/Container.{hpp,cpp}` is a
byte-faithful vendor drop that carries its own provenance notice and is marked
do-not-edit, and `PackTrustReader.hpp` is marked a mirror of the normative
spec. Neither has been improved in place, which is what keeps re-vendoring or
deleting them a mechanical job rather than a merge.

## Provenance

The map code comes from `AthensRun`, a proof of concept on `una-sdk`'s
`poc/athensrun` branch, which established that a live basemap with the GPS
trace over it works on this hardware. That app was built for one place and one
pack; nothing about that place survives here, and the apps that use MapKit are
forks of the stock activity apps rather than of it.

What changed on the way over: the hardcoded pack path became the selection rule
above; the `AthensRun::` namespace became `MapKit::`; the trace's storage zoom
became a named constant and moved from z16 to z18; the drawing widget gained an
explicit "trace only, no basemap" mode so the three failure states share one
code path; the corrupt-pack status became its own state instead of printing
`map: ok`; and the private CRC verifier was left behind entirely, because
MapManager owns that now and porting it would have recreated the GUI-freeze bug
described above.
