# Map Manager — a shared, autostart map-pack verifier

A background `Utility` app that owns offline map-pack management generically,
so map-consuming apps (`AthensRun` today; `Hiking`/`Cycling` tomorrow) don't
each need to run their own copy of this pipeline. It scans `../SharedData/maps/`
— a real, writable, cross-app directory reached the same way any app reaches
outside its own folder — for `*.rawtiles` files, CRC-32-verifies each one in
the background from boot (`APP_AUTOSTART`, before any app is ever opened), and
writes a small tri-state trust marker (`<pack>.rawtiles.trust`) next to each
one once it's checked. Any app reads that marker directly; nothing here is
specific to the rawtiles format itself — the verifier treats every tracked
file as an opaque blob with a trailing 4-byte little-endian CRC-32 footer.

Its own screen shows exactly what it's doing: current pack name, percent
complete, and an ETA computed from this run's *actual observed* throughput —
not a hardcoded assumption, since storage speed varies by device.

## Why this exists

`AthensRun` originally verified its own single, hardcoded map pack privately,
starting only once a user opened the app. Any second map-consuming app would
have needed to duplicate that whole verifier/marker/log pipeline for its own
private copy. Map Manager centralizes it: one app, one shared location, one
verification pass — autostart means it has a head start on the file transfer
time long before anyone opens an activity.

## What it does *not* do

It has no opinion on where packs come from, what they contain, or which app
wants them. It doesn't fetch, generate, or delete packs — only discovers
whatever's already been placed in `SharedData/maps/` (over USB today) and
verifies it. A consuming app is responsible for its own two-phase open: try
to open the file structurally, then trust its contents only once this app's
marker says `Good`. See `AthensRun/Software/Libs/Header/MapPackPaths.hpp` and
`gui/src/model/Model.cpp`'s `ensureMapPack()` in the `una-sdk` repo for that
consumer-side half of the contract.

## Why it's pinned to SDK 1.3

Same reason as `Chrono` (see [its README](../Chrono/README.md#why-13-matters)):
an app carries the kernel interface version it was built against, and 1.4
firmware — the only line with a `KERNEL_INTERFACE_VERSION` of 3 — hasn't
shipped yet. Point `$UNA_SDK` at a checkout of `apps-v1.3.0`, and reuse
`Chrono`'s `SendMsg.hpp` pattern (`MapManager::sendMsg<T>(kernel)`) for the one
message that carries a payload (`MapManagerProgress`), since 1.3's
`IAppComm::allocateMessage` can't forward constructor arguments the way later
SDKs can.

**Known build gotcha on `apps-v1.3.0`:** that tag predates mainline's
`check_cxx_compiler_flag` probe for `-fcyclomatic-complexity` (an
ST-CubeIDE-GCC-only flag mainline `arm-none-eabi-gcc` rejects outright). A
from-scratch checkout of `apps-v1.3.0` will fail to build *any* app —
this one included — with `-fcyclomatic-complexity: unrecognized command-line
option` until that unconditional flag is removed or guarded in that
checkout's own `cmake/una-app.cmake` (around the `una_app_build_service`/
`una_app_build_gui` compile-options list). This is a defect in the
`apps-v1.3.0` tag itself, not in this app; fix it locally in your checkout
until a newer 1.3-line tag carries mainline's probe.

## How it is put together

```
Software/
├── Libs/
│   ├── Header/
│   │   ├── Commands.hpp          # MapManagerProgress / MapManagerRequest messages
│   │   ├── ManagerLog.hpp        # diagnostic file log (Debug/mapmanager_verify.log)
│   │   ├── PackCrcVerifier.hpp   # per-file resumable CRC-32 scanner
│   │   ├── PackTrustMarker.hpp   # 16-byte tri-state marker (Absent/Bad/Good)
│   │   ├── SendMsg.hpp           # the SDK-1.3 backport (see Chrono)
│   │   └── Service.hpp
│   └── Sources/
│       ├── PackCrcVerifier.cpp
│       └── Service.cpp           # scan, drive one verifier at a time, publish
└── Apps/
    ├── MapManager-CMake/         # build glue
    └── TouchGFX-GUI/             # the one-screen GUI
```

**Service** (`Service::run()`): every loop iteration, scans `SharedData/maps/`
for untracked `*.rawtiles` files (throttled to once per `kRescanPeriodMs` —
packs are deployed over USB one at a time in practice, so this doesn't need
to be fast), then drives exactly one in-progress `PackCrcVerifier` per
iteration — sequential, not parallel, since only one pack is realistically
being verified at a time. It never self-terminates when the GUI closes,
unlike a typical utility app's service — the whole point of autostart is that
verification keeps moving whether anyone's looking or not.

**A real bug found on-device, fixed here**: the message-wait timeout that
gates the loop (`mKernel.comm.getMessage(msg, ...)`) needs to be short while
work is pending. AthensRun's original version of this same verifier design
happened to hit ~112KB/s because its Service has constant sensor-message
traffic that wakes a 500ms wait early on almost every iteration. Map Manager
subscribes to no sensors, so nothing ever wakes it early — with a flat 500ms
wait it measured **~7.9KB/s on real hardware** (confirmed via
`mapmanager_verify.log`), which would take ~7 hours to verify a 200MB pack.
Shortening the wait to 50ms whenever an entry is pending (falling back to
500ms once idle, so it doesn't spin needlessly) fixed it to **~77KB/s**,
confirmed on the same hardware and pack.

**PackCrcVerifier**: one file, `start()`/`step()`/`done()`. `start()` reads
the trailing 4-byte CRC footer and checks the sibling marker; if it already
says `Good` for this exact `(size, crc)`, verification is skipped entirely.
Otherwise `step()` advances a bounded 4096-byte chunk per call, updating a
CRC-32/ISO-HDLC accumulator, until the file's read and the computed CRC is
compared against the declared one — writing a `Good` or `Bad` marker either
way. Ported from `AthensRun`'s original `MapPackCrcVerifier`, generalized to
take an explicit path (this app's Service does the directory discovery that
AthensRun's single-hardcoded-path version didn't need).

## Buttons

The screen is read-only — verification is autonomous, nothing to command it
to do.

| Button | Position | Action |
| --- | --- | --- |
| `SW4` / R2 | bottom right | back — leaves the app |

## A real firmware quirk found while testing this

The on-watch quick-access menu (behind top-left `L1`) appears to cap out at
**3** installed `Utility`-type apps, filled alphabetically by name, alongside
its fixed `Power off` / `Settings` / `Alerts on` entries — confirmed by
removing other Utility apps and watching Map Manager appear once a slot
opened up. This is closed kernel/launcher firmware behavior, not present
anywhere in the `una-sdk` source, and not a defect in this app: its Service,
autostart, and app-registry entry (`Apps/app_list.json`) are all confirmed
correct regardless of whether that particular menu has room to show its icon.
Worth knowing before assuming a newly-deployed Utility app is broken because
it's "missing" from that menu.

## Known rough edges

- **Icons** (`Resources/icon_60x60.png`, `icon_30x30.png`) are still
  `Chrono`'s stopwatch placeholders, copied verbatim. Easy to swap for real
  artwork later; `app_merging.py` hard-requires *some* icon pair for any
  non-`Glance` app type, so a placeholder was necessary to build at all.
- The inherited Designer-generated stopwatch widgets (title, lap list,
  play/pause icons) are hidden in `MainView::setupScreen()` rather than
  relabeled or removed, to avoid touching the fragile packed string table
  Designer generates (see the class doc comment on `MainView`). Cosmetically
  inert, functionally harmless.
- No persisted resume checkpoint: if the watch reboots mid-verification, the
  in-progress scan restarts from byte 0 next boot (only the finished
  tri-state marker survives a reboot, not partial byte progress).

## Building

Point `UNA_SDK` at an SDK checkout at `apps-v1.3.0` (see the build gotcha
above):

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.3.0
cd MapManager/Software/Apps/MapManager-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 .. && cmake --build build
```

Deploy by copying the resulting `.uapp` from `build/` (or `../../../Output/`)
into `Apps/MapManager/` on the watch's USB-MSC volume. `Apps/app_list.json` is
rebuilt by the kernel's own boot-time app scan, so no separate registration
step is needed — confirmed via `Debug/mapmanager_verify.log` showing the
Service start and begin scanning within ~8 seconds of a cold boot, before the
app was ever opened.

## Tests

`PackCrcVerifier`/`PackTrustMarker`'s core is exercised against the same
`InMemoryFileSystem`/`KernelFixture` test doubles the SDK's own host tests
use (`Tests/Host/support/KernelTestDoubles.hpp` in `una-sdk`):

```sh
export UNA_SDK=/path/to/una-sdk
cd MapManager/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

`Service::scanForNewPacks()`'s directory-scan orchestration (discovering N
files, tracking N independent verifier states) is **not** host-tested — it's
verified on-device only (see `mapmanager_verify.log`'s `scanForNewPacks()`
lines). `InMemoryFileSystem`'s `IDirectory` was enhanced from a no-op stub
(`EmptyDirectory`, which always returned no entries) to a real
`InMemoryDirectory` that enumerates seeded files — a genuinely reusable
addition to the SDK's own test support — but nothing in this app's own test
suite exercises it yet. A real gap, not a design choice; the honest fallback
noted at design time in case there wasn't room for it, which is what
happened.

## Provenance

Forked from `Chrono`'s shell (`SendMsg.hpp`, the `CMakeLists.txt` pattern,
the `apps-v1.3.0` build approach) — itself a fork of the SDK's `Stopwatch`
example — for the 1.3-compat groundwork only; none of Chrono's stopwatch
domain logic (`Stopwatch.hpp`, lap tracking) carried over.
