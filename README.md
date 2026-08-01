# watch-apps

Apps I've written for the UNA&nbsp;Watch, built out of tree against the SDK rather
than living inside it.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.
> "UNA" and "UNA Watch" are their trademarks, used here only to say what these
> apps run on.

| App | What it is |
| --- | --- |
| [`GpsLab`](GpsLab) | The Running activity plus GNSS instrumentation — per-sample error estimate, fix and dead-reckoning state, recorded alongside the activity. |
| [`RustGuiPoc`](RustGuiPoc) | A proof of concept: a watch app whose GUI is drawn by Rust and `embedded-graphics` through the SDK's CustomGUI entry point, instead of TouchGFX. |

Both began as example apps inside the SDK tree and were moved here with their
history intact.

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

## Licence

MIT — see [LICENSE](LICENSE). Both apps derive from the UNA SDK's MIT-licensed
examples, whose copyright notice is retained there; the derived work is mine.
