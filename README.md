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
| [`NotifyToggle`](NotifyToggle) | A single on/off switch for phone-notification forwarding, reading and writing the watch's real `settings.json` flag directly, in-process — because the SDK's sandboxed filesystem API cannot reach that file at all on this firmware, and the paired phone app itself turns out to persist it over a plain BLE file overwrite rather than any documented mechanism. |
| [`QrGuiPoc`](QrGuiPoc) | A proof slice built on `RustGuiPoc`'s architecture: `Barcode`'s QR screen alone, redrawn through CustomGUI + Rust instead of TouchGFX, reusing `Barcode`'s own encoder unmodified, to measure what the rewrite would actually cost before attempting the rest of the app. |
| [`RunMap`](RunMap) | The stock Running activity with the same live map. |
| [`RustGuiPoc`](RustGuiPoc) | A proof of concept: a watch app whose GUI is drawn by Rust and `embedded-graphics` through the SDK's CustomGUI entry point, instead of TouchGFX. It shows a live accelerometer reading, which a GUI process cannot read on its own, so the Service half feeds it over the message bus. |
| [`SleepLab`](SleepLab) | A background, autostart `Utility` app that records a night of wrist data and scores it with a published actigraphy algorithm — and refuses to report sleep stages, an unworn night, or a heart-rate figure it has not earned a baseline for, because a sleep app's failures are silent. |
| [`Spin`](Spin) | A stationary bike ride: the clock, your heart rate and the zone it puts you in, and a FIT file that says `indoor_cycling` rather than a bike ride that covered no ground. Its GUI is Rust through CustomGUI and its settings come from the phone, like `Barcode`. |
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
shared directory rather than an app. `Spin` is new here too, forked from
`Squash` for its Service half — the indoor, GPS-free shape of an activity was
already right — and then given a Rust `CustomGUI` frontend instead of `Squash`'s
TouchGFX one. `NotifyToggle` is new here too, built from scratch on the same
Rust `CustomGUI` pattern.

## Building

Each app is a self-contained app root: a `Software/` directory holding exactly one
`*-CMake` project, which finds the SDK through `$UNA_SDK` rather than by relative
path. So an SDK checkout anywhere will do — but **it has to be the right one.**

**`$UNA_SDK` should point at an `apps-v1.4.0` checkout.** An app carries the
kernel interface version it was built against, and `Libs/Source/AppSystem/system.cpp`
refuses to launch when the running kernel's version is *lower* than the app's. So
the mistake to avoid is building against an SDK newer than the watch's firmware:
the build succeeds, the `.uapp` header looks identical, and the app simply does
not run once installed. The check is one-directional, so an app built against an
older SDK still runs on newer firmware — it just cannot use what the newer line
added.

The three map apps additionally reach `MapKit/` by relative path within this repository, which is the arrangement
Kira's registry recommends for a monorepo and which its one-`*-CMake`-per-app
rule is unaffected by — see [MapKit's README](MapKit/README.md#layout-and-why-kira-is-fine-with-it).

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.4.0
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
[`SunGlance/Tests`](SunGlance/Tests), `Barcode` carries its Code 128 encoder,
the round-panel geometry that encoder is drawn into, and the service that turns
`input.json` into a barcode, under [`Barcode/Tests`](Barcode/Tests), and the
three map apps share one suite
under [`MapKit/Tests`](MapKit/Tests), and `Spin` carries a decode-it-back test
for the FIT file it writes under [`Spin/Tests`](Spin/Tests). `RustGuiPoc`,
`QrGuiPoc` and `NotifyToggle`'s tests live inside their own crates rather than
in a `Tests/` directory — see [RustGuiPoc's README](RustGuiPoc/README.md#tests),
[QrGuiPoc's](QrGuiPoc/README.md#tests) and
[NotifyToggle's](NotifyToggle/README.md#tests) — and so do the renderer tests
of the three apps that ship a Rust GUI, `Barcode`, `NotifyToggle` and `Spin`,
which CI runs by discovering the crate rather than by being told about it.

`SleepLab`, `SunGlance`, `Spin`, `RustGuiPoc` and `QrGuiPoc` target `apps-v1.4.0`. `Chrono`,
`MapManager` and the three map apps were pinned to `apps-v1.3.0` back when no
1.4 firmware had shipped — their binaries still run on 1.4, since the launch
check only refuses a kernel older than the app, but the pinning rationale in
their READMEs
([Chrono](Chrono/README.md#why-13-matters), [MapManager](MapManager/README.md#why-its-pinned-to-sdk-13))
no longer applies and neither has been rebuilt against 1.4 here.

## Licence

MIT — see [LICENSE](LICENSE). Both apps derive from the UNA SDK's MIT-licensed
examples, whose copyright notice is retained there; the derived work is mine.
