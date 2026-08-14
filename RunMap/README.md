# Run Map — the stock Running app, with a map

The SDK's `Running` activity app at `apps-v1.3.0`, with one thing added: the
in-activity screen gains a map face. Tiles from an offline `.rawtiles` pack
underneath, the GPS breadcrumb over them, one line of status at the bottom.

Everything else is the stock app, unchanged — the menus, the intervals
workouts, the lap handling, the summary, the `.fit` it writes. It installs
alongside `Running` rather than replacing it: different name, different AppID.

| | |
| --- | --- |
| AppID | `96CB8FD134CF855C` |
| Type | `Activity` |
| Forked from | `Examples/Apps/Running` at `apps-v1.3.0` |
| Shared code | [`../MapKit`](../MapKit) |
| Needs | [`MapManager`](../MapManager) installed, and a pack in `Apps/SharedData/maps/` |

## What it adds

**A fifth face on the activity screen**, after the three stock ones (and the
intervals face, in a workout). `L1` / `L2` scroll to it the same way they scroll
to any other face — it is appended last, so every stock face keeps the position
the stock app gives it.

**A `GPS_POSITION` message**, Service → GUI. The stock app forwards only the
fix *state*; the coordinates stay in the Service, which is all the stock faces
need. A map needs *where*, not just *whether*. It is sent at 1 Hz from the same
place as the clock and battery updates — well inside the GUI's ten-deep message
queue, which is drained once per frame at 10 Hz.

**`R2` cycles the zoom, but only on the map face**, and only in a free run.
This is the one button that behaves differently from the stock app, and it is
worth being precise about why it is not a change: the map face is *new*, so R2
on it did not previously do anything. On every stock face R2 is still the lap
button. In an intervals workout R2 stays next-phase everywhere, including the
map — advancing a phase is not something to hide behind a face. A lap is always
one `L1`/`L2` press away.

The zoom steps through the *selected pack's own* `zoomMin..zoomMax` and wraps,
starting at the finest: the wearer is looking at where they are, and can zoom
out from there.

## What it deliberately does not do

**The post-activity summary screen still has no map.** The stock app already
carries `Map.hpp`, `PolyLine.hpp` and `SummaryFaceMap.hpp` — that is the
breadcrumb-on-black you get after finishing a run, and it is untouched. It has
no tile view, so a saved run's map is still a trace on a blank background even
though the live screen now has a basemap.

That is a real gap and it was deferred on purpose, twice: once by the proof of
concept this grew out of, and again here, because the whole point of these three
apps is to be the stock apps *plus a live map* and nothing else. It is not an
oversight, and the next person to look should not spend an afternoon working out
whether it was missed. Doing it properly means giving the summary face a
`MapTileView` and deciding what pack a *finished* activity should be drawn
against — which is a different question from the live one, because a saved
activity has a bounding box of its own and no current fix.

## How the map behaves

All the interesting behaviour — which pack gets drawn and why, the three
failure states that must not look alike, the trust contract with `MapManager` —
lives in [MapKit's README](../MapKit/README.md). The short version:

| Status line | What it means |
| --- | --- |
| `acquiring GPS` | No fix yet, so "which pack covers me" has no answer. |
| `no map for here` | Nothing on this watch covers this position. |
| `verifying map` | Pack is fine; MapManager has not finished checking it. **Resolves itself.** |
| `map pack corrupt` | MapManager confirmed these exact bytes corrupt. |
| `map: <reason>` | The pack failed to open. |
| `off map  z<n>` | Pack is live, but there is no tile at this spot and zoom. |
| `z<n>` | Drawing. |

**In every one of these the activity keeps recording and the breadcrumb keeps
drawing.** The map is an addition to the run, never a precondition for it.

## Building

Pinned to SDK 1.3, for the same reason as [`Chrono`](../Chrono/README.md#why-13-matters)
and [`MapManager`](../MapManager/README.md#why-its-pinned-to-sdk-13): an app
carries the kernel interface version it was built against, and 1.4 firmware has
not shipped.

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.3.0
cd RunMap/Software/Apps/RunMap-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

That works against a **pristine** `apps-v1.3.0` with no SDK edit. The tag ships
`-fcyclomatic-complexity` unconditionally — an ST-CubeIDE-GCC-only flag that
mainline `arm-none-eabi-gcc` rejects outright, so a from-scratch checkout fails
to build *any* app, the stock `Running` included. This app's `CMakeLists.txt`
probes for the flag the way mainline does and drops it at directory scope when
the compiler rejects it. Same treatment as MapManager's, and the same argument
applies: this is a defect in the tag, and the real fix is an `apps-v1.3.1` with
mainline's probe cherry-picked onto it.

Deploy by copying the resulting `.uapp` into `Apps/RunMap/` on the watch's
USB-MSC volume, and put a `.rawtiles` pack in `Apps/SharedData/maps/`.
MapManager will verify it in the background from boot and write the marker this
app reads.

### Simulator

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.3.0
cd RunMap/Software/Apps/TouchGFX-GUI
make -f simulator/gcc/Makefile -j8
```

Its filesystem root is `../../../../../Output/` **relative to the working
directory you launch from**, so run it from a scratch directory rather than the
source tree, and put packs in `SharedData/maps/` beside that root. See the
verification note below for what the simulator can and cannot show.

## How it was verified

Stated plainly, because the honest answer is mixed.

**On hardware: nothing.** No part of this has run on a watch. Everything below
is a host test or the desktop simulator.

**Host tests** ([`MapKit/Tests`](../MapKit/Tests), 90 of them) cover the pack
selection rule and all its tie-breaks, the `(size, crc)` trust guard in both
directions, the header screen, trace decimation and thinning, the projection
and zoom rescaling, and `MapSession`'s whole state machine including
`verifying → trusted`, `verifying → corrupt`, walking into and out of coverage,
and falling back to a second pack when the first is condemned. Two behaviours
were confirmed by mutation rather than assumed.

**The simulator** runs the real Service thread beside the GUI, so it exercises
the pipeline end to end against a real filesystem. Its log confirms discovery →
header peek → selection → structural open → trust-marker read, on both this app
and `BikeMap`, against a generated test pack: `map: opened
../SharedData/maps/vinnytsia.rawtiles, z13..z16, 100 tiles` then `trusted via
Map Manager's marker`. Removing the marker leaves it in `verifying`; removing
the pack gives `no pack covers this position`. The simulator is also what found
the once-per-second rescan described in
[MapKit's README](../MapKit/README.md#which-pack-to-draw) — a defect no host
test had been written to catch.

**Unproven: everything that draws.** The map face has never been seen. The
simulator can build and run it, but reaching the face needs button presses, and
**synthetic key injection does not reach the SDL window** — independently
reproduced here with the window focused and a real XTEST keypress ignored,
matching what MapManager's README already recorded. A scratch build that jumped
straight into a running activity did not take effect either. So `MapTileView`
and `TrackFaceMap` — the tile mosaic, the `blitCopy` alignment, the trace
rendering, the status line's fit inside the round bezel — are carried over
unchanged from a proof of concept that *was* verified on hardware, but have not
been re-checked since. The changes to them are small and known: a named
constant for the trace's storage zoom (host-tested), an explicit trace-only
mode, and the corrupt-pack status. **Look at this screen first.**
