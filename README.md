# watch-apps

Apps I've written for the UNA&nbsp;Watch, built out of tree against the SDK rather
than living inside it.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.
> "UNA" and "UNA Watch" are their trademarks, used here only to say what these
> apps run on.

| App | What it is |
| --- | --- |
| [`Barcode`](Barcode) | A parkrun-style Code 128 barcode for an id you supply, read from a small JSON file you write into the app's folder over USB — because the SDK has no supported way to get a user-specific value onto the watch. |
| [`Chrono`](Chrono) | The SDK's Stopwatch example backported to SDK 1.3, which upstream never had a build of — so it launches on a watch whose kernel is still on interface version 2. |
| [`GpsLab`](GpsLab) | The Running activity plus GNSS instrumentation — per-sample error estimate, fix and dead-reckoning state, recorded alongside the activity. |
| [`RustGuiPoc`](RustGuiPoc) | A proof of concept: a watch app whose GUI is drawn by Rust and `embedded-graphics` through the SDK's CustomGUI entry point, instead of TouchGFX. |
| [`Squash`](Squash) | A squash activity app, and the raw 100 Hz IMU recorder it is being built out of — because tuning shot detection needs labelled court data that does not exist yet. |

All of them started as example apps inside the SDK tree and came here with their
history intact. The first four were moved out; `Chrono` is a fork of an example
that is still upstream, carrying its own name and AppID so the two install side
by side.

## Building

Each app is a self-contained app root: a `Software/` directory holding exactly one
`*-CMake` project, which finds the SDK through `$UNA_SDK` rather than by relative
path. So an SDK checkout anywhere will do.

```sh
export UNA_SDK=/path/to/una-sdk
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
[`Squash/Tests`](Squash/Tests) — see [its README](Squash/README.md#tests) — and
`Chrono` carries tests for its stopwatch core under [`Chrono/Tests`](Chrono/Tests).

`Chrono` is the one app that is pinned to a particular SDK: point `$UNA_SDK` at a
checkout of `apps-v1.3.0` for it, and see [its README](Chrono/README.md#why-13-matters)
for why.

## Licence

MIT — see [LICENSE](LICENSE). Both apps derive from the UNA SDK's MIT-licensed
examples, whose copyright notice is retained there; the derived work is mine.
