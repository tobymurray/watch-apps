# watch-apps

Apps I've written for the UNA&nbsp;Watch, built out of tree against the SDK rather
than living inside it.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.
> "UNA" and "UNA Watch" are their trademarks, used here only to say what these
> apps run on.

| App | What it is |
| --- | --- |
| [`Barcode`](Barcode) | A parkrun-style Code 128 barcode for an id you supply, read from a small JSON file you write into the app's folder over USB — because the SDK has no supported way to get a user-specific value onto the watch. |
| [`BikeMap`](BikeMap) | The stock Cycling activity with a live map on the in-activity screen: offline basemap tiles under the GPS breadcrumb, instead of breadcrumb-on-black. |
| [`Chrono`](Chrono) | The SDK's Stopwatch example backported to SDK 1.3, which upstream never had a build of — so it launches on a watch whose kernel is still on interface version 2. |
| [`GpsLab`](GpsLab) | The Running activity plus GNSS instrumentation — per-sample error estimate, fix and dead-reckoning state, recorded alongside the activity. |
| [`HikeMap`](HikeMap) | The stock Hiking activity with the same live map. |
| [`MapManager`](MapManager) | A background, autostart `Utility` app that discovers and CRC-verifies offline map packs dropped into the shared `SharedData/maps/` directory, so map-consuming apps read from one already-verified location instead of each running their own copy of this pipeline. |
| [`RunMap`](RunMap) | The stock Running activity with the same live map. |
| [`RustGuiPoc`](RustGuiPoc) | A proof of concept: a watch app whose GUI is drawn by Rust and `embedded-graphics` through the SDK's CustomGUI entry point, instead of TouchGFX. |
| [`SleepLab`](SleepLab) | A background, autostart `Utility` app that records a night of wrist data and scores it with a published actigraphy algorithm — and refuses to report sleep stages, an unworn night, or a heart-rate figure it has not earned a baseline for, because a sleep app's failures are silent. |
| [`Squash`](Squash) | A squash activity app, and the raw 100 Hz IMU recorder it is being built out of — because tuning shot detection needs labelled court data that does not exist yet. |
| [`SunGlance`](SunGlance) | A `Glance` card that says what the sun does next — sunrise or sunset, and how long until it — for a position written into the app's folder at install time, because a three-second card cannot afford a GNSS fix and does not need one. |

Most of these started as example apps inside the SDK tree and came here with
their history intact (`Barcode`, `GpsLab`, `RustGuiPoc`, `Squash`); `Chrono`
is a fork of an example that is still upstream, carrying its own name and
AppID so the two install side by side; `MapManager` is new here, built off
`Chrono`'s shell for the SDK-1.3 groundwork only. `BikeMap`, `HikeMap` and
`RunMap` are forks of `Cycling`, `Hiking` and `Running` in the same
install-alongside sense, and are the same app three times over as far as the map
goes — the code that draws it lives once, in [`MapKit`](MapKit), which is a
shared directory rather than an app.

## Building

Each app is a self-contained app root: a `Software/` directory holding exactly one
`*-CMake` project, which finds the SDK through `$UNA_SDK` rather than by relative
path. So an SDK checkout anywhere will do — but **it has to be the right one.**

**`$UNA_SDK` must point at an `apps-v1.3.0` checkout, not at mainline.** An app
carries the kernel interface version it was built against: `apps-v1.3.0` is
`KERNEL_INTERFACE_VERSION 2`, mainline is `3`, and the watch runs the 1.3 line.
The two are not compatible and nothing catches the mistake — the build succeeds,
the `.uapp` header looks identical (it carries app id, app version and libc
version, none of which change), and the app simply does not run once installed.
Chrono's and Map Manager's READMEs explain the pinning; it applies to every app
here.

The three map apps additionally reach `MapKit/` by relative path within this repository, which is the arrangement
Kira's registry recommends for a monorepo and which its one-`*-CMake`-per-app
rule is unaffected by — see [MapKit's README](MapKit/README.md#layout-and-why-kira-is-fine-with-it).

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0    # not mainline; see above
cd GpsLab/Software/Apps/GpsLab-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=1.0.0 .. && cmake --build build
```

Or let [Kira](https://github.com/tobymurray/kira) do it, which additionally checks
the result against what the `CMakeLists.txt` declares — the AppID, the type, the
version, and the `.uapp`'s own CRC:

```sh
kira build-app --app GpsLab --sdk /path/to/una-sdk --version 1.0.0 --out GpsLab.uapp
```

`Squash` also carries host tests for its recorder path under
[`Squash/Tests`](Squash/Tests) — see [its README](Squash/README.md#tests) —
`Chrono` carries tests for its stopwatch core under [`Chrono/Tests`](Chrono/Tests),
`MapManager` carries tests for its verifier core under
[`MapManager/Tests`](MapManager/Tests), `SleepLab` carries its sleep engine and
storage under [`SleepLab/Tests`](SleepLab/Tests), `SunGlance` carries its solar
core, its wording and its glance wiring under
[`SunGlance/Tests`](SunGlance/Tests), and the three map apps share one suite
under [`MapKit/Tests`](MapKit/Tests).

Two different SDKs, depending on the app. `Chrono`, `MapManager` and the three
map apps are pinned to `apps-v1.3.0` — point `$UNA_SDK` at a checkout of that
tag, and see their READMEs
([Chrono](Chrono/README.md#why-13-matters), [MapManager](MapManager/README.md#why-its-pinned-to-sdk-13))
for why. `SleepLab` and `SunGlance` target **`apps-v1.4.0`** and will not run
on a 1.3 kernel: an app carries the interface version it was built against, and
the mismatch shows up as an instant `App PID` error screen rather than as a
build failure — see [SleepLab's README](SleepLab/README.md#building).

## Licence

MIT — see [LICENSE](LICENSE). Both apps derive from the UNA SDK's MIT-licensed
examples, whose copyright notice is retained there; the derived work is mine.
