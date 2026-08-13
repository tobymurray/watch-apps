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

## Integrating: the trust-marker contract

If you're writing the next map-consuming app, this is all you need. The
**normative spec of the format** is the class comment on
`Software/Libs/Header/PackTrustMarker.hpp` — read that, not this summary,
before implementing a reader. The short version:

- For a pack at `<path>`, its marker is at exactly `<path>.trust` — same
  directory, suffix appended to the whole filename.
- 16 bytes, little-endian: `magic` (u32), `fileSize` (u64), `crc` (u32).
  Magic is `'MPT1'` for good, `'MPTX'` for bad. Anything else — wrong length,
  unknown magic, unreadable — is **`Absent`**, not an error.
- Three states, and all three need handling: `Good` (safe to use),
  `Bad` (confirmed corrupt: report it, don't wait), `Absent` (**no verdict
  yet — keep polling**, this is the normal state for the first minutes after
  a pack is deployed).
- **You must apply the `(size, crc)` guard yourself.** Only believe a marker
  if its `fileSize` matches the file's current size *and* its `crc` matches
  the CRC the file declares in its own trailing 4 bytes. On any mismatch,
  treat it as `Absent`. A marker describes the bytes that were scanned; a
  pack replaced since then is a different file at the same path.
- Verification is a background pass that starts at boot, so poll cheaply
  (a marker read is one 16-byte file read) rather than blocking on it.

`AthensRun`'s `Model::ensureMapPack()` (`una-sdk`, `poc/athensrun`) is a
worked example of exactly this, including the guard.

**What this protects against, and what it doesn't.** This is an integrity
check: truncated transfers, interrupted copies, bad sectors, bit rot. It is
*not* an authenticity check. A pack declares its own checksum, so a file
altered consistently (body changed, footer recomputed) verifies as `Good`;
and markers live in a directory every installed app can write, so a marker
can be forged with 16 bytes. The root of trust is that no app installed on
the watch is hostile. That's a fair assumption for how packs get here today
(a USB copy by the device's owner) and it should be revisited — with a
signature, not a checksum — if packs ever arrive over the air.

## Why it's pinned to SDK 1.3

Same reason as `Chrono` (see [its README](../Chrono/README.md#why-13-matters)):
an app carries the kernel interface version it was built against, and 1.4
firmware — the only line with a `KERNEL_INTERFACE_VERSION` of 3 — hasn't
shipped yet. Point `$UNA_SDK` at a checkout of `apps-v1.3.0`, and reuse
`Chrono`'s `SendMsg.hpp` pattern (`MapManager::sendMsg<T>(kernel)`) for the one
message that carries a payload (`MapManagerProgress`), since 1.3's
`IAppComm::allocateMessage` can't forward constructor arguments the way later
SDKs can.

**Known build gotcha on `apps-v1.3.0` — currently needs a local SDK edit:**
that tag predates mainline's `check_cxx_compiler_flag` probe for
`-fcyclomatic-complexity` (an ST-CubeIDE-GCC-only flag mainline
`arm-none-eabi-gcc` rejects outright). A from-scratch checkout of
`apps-v1.3.0` will fail to build *any* app — this one included — with
`-fcyclomatic-complexity: unrecognized command-line option` until that
unconditional flag is removed or guarded in that checkout's own
`cmake/una-app.cmake` (around the `una_app_build_service`/`una_app_build_gui`
compile-options list).

This is a defect in the `apps-v1.3.0` tag, not in this app — mainline
`una-sdk` already carries the fix (the probe at `cmake/una-app.cmake`, guarded
with `check_c_compiler_flag`/`check_cxx_compiler_flag`). But "works after you
hand-edit your SDK checkout" is not a buildable state for anyone else, so
**this should be resolved on the SDK side before this app is offered
upstream**: cherry-pick that probe onto the 1.3 line and cut an
`apps-v1.3.1`, then point `UNA_SDK` at that. Until then, patch it locally.

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

**Service** (`Service::run()`): every loop iteration, reconciles
`SharedData/maps/` against what it's tracking (throttled to once per
`kRescanPeriodMs`), then advances exactly one `PackCrcVerifier` by one slice
— sequential, not parallel, since only one pack is realistically being
verified at a time. It never self-terminates when the GUI closes, unlike a
typical utility app's service — the whole point of autostart is that
verification keeps moving whether anyone's looking or not.

Reconciling, not just discovering, is what makes the scan robust. A file
that's new becomes an entry; a tracked file whose **size has changed** since
its entry was armed is reset and re-verified; a tracked file that's **gone**
is dropped so it stops being counted. The size check is what rescues the
common real case: a pack discovered while it was still being copied over USB
gets a verdict about bytes that are about to change, and without the re-arm
that write-off would stand until the next reboot.

**PackCrcVerifier**: one file, `start()`/`step()`/`done()`. `start()` reads
the trailing 4-byte CRC footer and checks the sibling marker; if it already
carries a verdict — `Good` *or* `Bad` — for this exact `(size, crc)`,
verification is skipped entirely. Caching the `Bad` verdict matters as much
as the `Good` one: a confirmed-corrupt file doesn't become uncorrupt by being
re-read, so re-deriving that answer on every boot is pure cost. Otherwise
`step()` consumes a caller-supplied byte budget, reading in 4096-byte chunks
and updating a CRC-32/ISO-HDLC accumulator, until the file's read and the
computed CRC is compared against the declared one — writing a `Good` or `Bad`
marker either way. Ported from `AthensRun`'s original `MapPackCrcVerifier`,
generalized to take an explicit path (this app's Service does the directory
discovery that AthensRun's single-hardcoded-path version didn't need).

### What actually sets verification throughput

Worth writing down, because two rounds of tuning got this wrong before
getting it right, and the failure mode is invisible unless you do the
arithmetic.

The loop is gated by a kernel message wait. AthensRun's original version of
this design happened to hit ~112KB/s because its Service has constant
sensor-message traffic that wakes a 500ms wait early on almost every
iteration. Map Manager subscribes to no sensors, so nothing ever wakes it
early: with a flat 500ms wait it measured **~7.9KB/s on real hardware**
(confirmed via `mapmanager_verify.log`). Shortening the wait to 50ms while
work was pending took it to **~77KB/s** — a 10x win, and the wrong fix.

4096 bytes per 50ms wait is 81,920 B/s. That the measurement landed on
77KB/s is the tell: the *wait*, not the storage, was the bottleneck, and one
I/O chunk per wait meant the loop spent ~94% of its time blocked. Shortening
the wait moved the ceiling without removing it.

So the unit of work is now a **budget, not a chunk**: `step(maxBytes)`
consumes its whole budget in 4096-byte reads before returning, and Service
hands it `kSliceBudgetBytes` (64KB) per iteration. The wait amortises away
and the storage sets the rate. The tradeoff is message latency — the service
can't answer a message mid-slice — which is why the slice is sized at roughly
the ~50ms the old wait already cost.

Measured on the host fixture, an 8MB pack now completes in 129 loop
iterations where one-chunk-per-wait would have taken 2048 — **15.9x fewer
message waits**. Folding in the ~3ms per 4096-byte chunk that the on-device
77KB/s figure implies, that projects to ~1.1MB/s, i.e. **~3 minutes for a
201MB pack** against 42.5 minutes before. The iteration count is measured;
the wall-clock projection is arithmetic from an earlier measurement and
**still wants confirming on hardware** — re-read `mapmanager_verify.log`'s
`throughput=` field after the next deploy and correct these numbers.

Once idle, the wait goes the other way: it sleeps until the next rescan is
actually due (capped at the GUI refresh period while a screen is open)
instead of waking twice a second forever. This service is `APP_AUTOSTART` and
never exits, so an idle poll is a cost the device pays for its whole life;
the SDK's own autostart utility (`Alarm`) sleeps to its next scheduled work
for the same reason.

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

It does have a real consequence for *this* app, though, even though it isn't
this app's bug. On a watch with three alphabetically-earlier Utility apps
installed, Map Manager can't be opened at all — and since its screen is the
only place that explains "your map is missing because that pack failed its
CRC", the fallback becomes pulling `Debug/mapmanager_verify.log` over USB.
The verification itself is unaffected (that's the point of autostart), but
the diagnosis story is worse than it looks. Note also that "filled
alphabetically" means visibility depends on the app's *name* — which is an
argument for consumers surfacing pack state in their own UI rather than
relying on this one being reachable.

## Icon

A flat map-pin silhouette (`Resources/icon_60x60.png`/`icon_30x30.png`),
matching the exact convention every other app on this SDK already uses:
solid teal `(0, 128, 128)` for the 60x60 "normal" icon, solid gray
`(192, 192, 192)` for the 30x30 "small" one (see `Alarm`/`Timer`'s icons in
`una-sdk` for the same pattern), flat fill on a transparent background, no
gradients or fine detail. That last part matters more than usual here: this
display's framebuffer is 2 bits per channel (4 luminance levels), and a
subtle multi-tone icon would band or wash out the way OSM's default map tile
style did on this same screen (see the AthensRun investigation this app grew
out of). A bold single-color silhouette holds up at 30x30 and survives that
quantization cleanly, which is why the teal/gray convention already in use
elsewhere on this SDK works, not despite the display's limits.

## Known rough edges

- The inherited Designer-generated stopwatch widgets (title, lap list,
  play/pause icons) are hidden in `MainView::setupScreen()` rather than
  relabeled or removed, to avoid touching the fragile packed string table
  Designer generates (see the class doc comment on `MainView`). Cosmetically
  inert, functionally harmless.
- No persisted resume checkpoint: if the watch reboots mid-verification, the
  in-progress scan restarts from byte 0 next boot (only the finished
  tri-state marker survives a reboot, not partial byte progress). This was
  the more serious of the two rough edges while a 200MB pack took 42
  uninterrupted minutes — a watch rebooted daily might genuinely never
  finish one. At the current slice budget the same pack is single-digit
  minutes, which is why the fix was throughput rather than a checkpoint.
  If pack sizes grow by another order of magnitude, revisit that.
- The `.trust` marker of a pack that's been deleted is left behind rather
  than cleaned up. Deliberate: it's 16 bytes, it self-invalidates through its
  own `(size, crc)` guard, it's re-adopted for free if the pack comes back,
  and deleting user files isn't this app's job. The *entry* is dropped, so
  the counts on screen stay honest.
- One `Utility` app slot is shared with the alphabetical cap described below,
  which can make this app unopenable on a watch with several Utility apps
  installed. The service is unaffected; the diagnostic log is then the only
  way to see what it's doing.

## Building

Point `UNA_SDK` at an SDK checkout at `apps-v1.3.0` (see the build gotcha
above):

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.3.0
cd MapManager/Software/Apps/MapManager-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

Deploy by copying the resulting `.uapp` from `build/` (or `../../../Output/`)
into `Apps/MapManager/` on the watch's USB-MSC volume. `Apps/app_list.json` is
rebuilt by the kernel's own boot-time app scan, so no separate registration
step is needed — confirmed via `Debug/mapmanager_verify.log` showing the
Service start and begin scanning within ~8 seconds of a cold boot, before the
app was ever opened.

## Tests

`PackCrcVerifier`, `PackTrustMarker` and `Service`'s scan orchestration are
exercised against the same `InMemoryFileSystem`/`KernelFixture` test doubles
the SDK's own host tests use (`Tests/Host/support/KernelTestDoubles.hpp` in
`una-sdk`):

```sh
export UNA_SDK=/path/to/una-sdk
cd MapManager/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

**`UNA_SDK` for the tests must point at an SDK checkout whose
`InMemoryFileSystem` has `InMemoryDirectory`** — the real enumerating fake,
not the older `EmptyDirectory` stub that always reported no entries.
`Service_test.cpp` needs it to have a directory to scan at all. That
enhancement currently lives on `una-sdk`'s `poc/athensrun` branch and is
genuinely reusable beyond this app; it belongs upstream, and until it lands
there the app build (`apps-v1.3.0`) and the test build want different
checkouts. Point `UNA_SDK` accordingly for each.

`Service_test.cpp` covers what the verifier's own tests can't: discovering N
packs, tracking N independent verdicts, ignoring non-packs and its own
`.trust` files, re-arming a pack that changed size (the mid-copy case),
dropping one that disappeared while still finishing the rest, doing no I/O
once everything has settled, and leaving no open file handles behind. The
verifier tests pin the `(size, crc)` guard on both cached verdicts, the
footer-size edge cases, and `step()`'s budget behaviour.

What is still **not** covered by host tests: `run()`'s message loop (it
blocks on the kernel queue and never returns — `poll()` exists as the
testable seam for the work it does between waits), and the GUI. Both are
on-device-verified only.

## Where this should live

Worth stating plainly, because the current arrangement has a dependency
pointing the wrong way. This app's entire purpose is to be depended on by
apps that aren't in this repo — and `AthensRun`, which lives in `una-sdk`,
now has a hard runtime dependency on an app in a personal fork. The named
future consumers (`Hiking`, `Cycling`) are `una-sdk` examples too. That
inversion is also why the marker format is currently implemented twice by
copy-paste, once here and once as a read-only mirror in `AthensRun`.

The argument against simply moving the whole thing is fair: `una-sdk`'s
`Examples/` is a teaching corpus, not an app store, and this is a full app
with a TouchGFX tree it barely uses.

The split that resolves both: **the marker format belongs in `una-sdk` as a
small shared header** — that's the actual contract, and it's what every
consumer needs — while the app itself can live here. Until that happens,
`PackTrustMarker.hpp`'s class comment is the normative spec and `AthensRun`'s
copy is explicitly labelled a mirror of it.

## Provenance

Forked from `Chrono`'s shell (`SendMsg.hpp`, the `CMakeLists.txt` pattern,
the `apps-v1.3.0` build approach) — itself a fork of the SDK's `Stopwatch`
example — for the 1.3-compat groundwork only; none of Chrono's stopwatch
domain logic (`Stopwatch.hpp`, lap tracking) carried over.
