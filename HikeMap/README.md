# Hike Map — the stock Hiking app, with a map

The SDK's `Hiking` activity app at `apps-v1.3.0`, with one thing added: the
in-activity screen gains a map face. Tiles from an offline `.rawtiles` pack
underneath, the GPS breadcrumb over them, one line of status at the bottom.

Everything else is the stock app, unchanged — the menus, the elevation face, the
lap handling, the summary, the `.fit` it writes. It installs alongside `Hiking`
rather than replacing it: different name, different AppID.

Of the three map apps this is the one whose activity most often goes somewhere
without a phone signal, which is the whole argument for an offline basemap.

| | |
| --- | --- |
| AppID | `478A8753AF5C658B` |
| Type | `Activity` |
| Forked from | `Examples/Apps/Hiking` at `apps-v1.3.0` |
| Shared code | [`../MapKit`](../MapKit) |
| Needs | [`MapManager`](../MapManager) installed, and a pack in `Apps/SharedData/maps/` |

## What it adds

**A fifth face on the activity screen**, after the stock four (total, overview,
elevation, status). `L1` / `L2` scroll to it the same way they scroll to any
other face — it is appended last, so every stock face keeps the position the
stock app gives it.

**A `GPS_POSITION` message**, Service → GUI. The stock app forwards only the fix
*state*; the coordinates stay in the Service, which is all the stock faces need.
A map needs *where*, not just *whether*. Sent at 1 Hz from the same place as the
clock and battery updates.

**`R2` cycles the zoom, but only on the map face, and only when there is a
map to zoom.** On every stock face R2 is still the lap button, and on the map
face with no usable pack it is the lap button too. The map face is *new*, so R2 on it did not previously do
anything — this takes nothing away. A lap is always one `L1`/`L2` press away.
The zoom steps through the selected pack's own `zoomMin..zoomMax` and wraps,
starting at the finest, which on a hike is the one you actually want when you
stop to check where the path went.

## What it deliberately does not do

**The post-activity summary screen still has no map.** The stock app already
carries `Map.hpp`, `PolyLine.hpp` and `SummaryFaceMap.hpp` — the
breadcrumb-on-black you get after finishing a hike — and it is untouched. It has
no tile view, so a saved hike's map is still a trace on a blank background even
though the live screen now has a basemap.

Deferred on purpose, twice: once by the proof of concept this grew out of, and
again here, because the point of these three apps is to be the stock apps *plus
a live map* and nothing else. Not an oversight. Doing it properly means giving
the summary face a `MapTileView` and deciding what pack a *finished* activity
should be drawn against — a different question from the live one, because a
saved activity has a bounding box of its own and no current fix.

## With no maps installed

Worth being exact about, because it is the state most watches will be in.
Everything the Hiking app does, this app still does: the hike records, the
`.fit` is written, the stock faces are unchanged, and the summary is the same.
Nothing waits on a map, nothing fails, and nothing is logged as an error --
running with no `SharedData/maps/` directory at all produces one informational
line and no more.

Two things are still different from stock, and neither is a bug:

- **The map face is still there**, showing the breadcrumb on a blank background
  with `no map for here`. It is not hidden when there is no pack, deliberately:
  the face carousel changing length underneath you -- as a pack finishes
  verifying, or as you walk into coverage -- would be worse than a face that
  says why it is empty. It is also the only place the app can tell you that a
  pack failed, which matters, since MapManager's own screen
  [may not be reachable](../MapManager/README.md#a-real-firmware-quirk-found-while-testing-this).
- **The map costs its RAM either way.** The tile cache is one 64 KiB static
  slot, claimed at link time whether or not a pack is ever installed.

`R2` is *not* one of the differences. It cycles the zoom on the map face only
when there is a map to zoom; with no pack it takes a lap, exactly as it does on
every other face. Someone who never installs a pack should not lose a button to
a feature they are not using.

## How the map behaves

Which pack gets drawn and why, the three failure states that must not look
alike, and the trust contract with `MapManager` are all in
[MapKit's README](../MapKit/README.md). The short version:

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
drawing.** The map is an addition to the hike, never a precondition for it —
which matters more here than on the other two, because this is the app most
likely to be a long way from anywhere when something goes wrong with a pack.

## Building

Pinned to SDK 1.3, for the same reason as [`Chrono`](../Chrono/README.md#why-13-matters)
and [`MapManager`](../MapManager/README.md#why-its-pinned-to-sdk-13): an app
carries the kernel interface version it was built against, and 1.4 firmware has
not shipped.

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.3.0
cd HikeMap/Software/Apps/HikeMap-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

That works against a **pristine** `apps-v1.3.0` with no SDK edit. The tag ships
`-fcyclomatic-complexity` unconditionally — an ST-CubeIDE-GCC-only flag that
mainline `arm-none-eabi-gcc` rejects, so a from-scratch checkout fails to build
*any* app, the stock `Hiking` included. This app's `CMakeLists.txt` probes for
it the way mainline does and drops it when the compiler says no.

Deploy by copying the resulting `.uapp` into `Apps/HikeMap/` on the watch's
USB-MSC volume, and put a `.rawtiles` pack in `Apps/SharedData/maps/`.

The simulator builds with `make -f simulator/gcc/Makefile -j8` from
`Software/Apps/TouchGFX-GUI`; its filesystem root is `../../../../../Output/`
relative to the directory you launch it from, with packs in `SharedData/maps/`
beside that root.

## How it was verified

**On hardware: nothing.** No part of this has run on a watch.

**Host tests** ([`MapKit/Tests`](../MapKit/Tests), 93 of them) cover the shared
layer: pack selection and its tie-breaks, the `(size, crc)` trust guard, the
header screen, trace decimation, the projection maths, and `MapSession`'s state
machine. They are shared with the other two map apps, because the logic is.

**The simulator** builds and links for this app, with every `MapKit` source in
its `ADDITIONAL_SOURCES_UNA`, but the pipeline was exercised at runtime on
`RunMap` and `BikeMap` rather than here — the code doing that work is identical
across the three, and the only per-app difference is which face carousel the map
is appended to.

**Unproven: everything that draws.** The map face has never been seen, here or
on any of the three apps. Reaching it in the simulator needs button presses, and
synthetic key injection does not reach the SDL window — reproduced with the
window focused and a real XTEST keypress ignored. See
[RunMap's README](../RunMap/README.md#how-it-was-verified) for the full account;
it applies unchanged to this app, since the drawing code is the same code.
