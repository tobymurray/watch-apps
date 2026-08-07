# Chrono — the SDK's Stopwatch example, backported to SDK 1.3

A lap stopwatch: start, pause, lap, reset, with a scrollable lap list. It is a
fork of the `Stopwatch` example app UNA added to the SDK on 2026-07-23, one day
after `apps-v1.3.0` was tagged — so upstream it first appears in the 1.4 line and
there is no 1.3 build of it. Chrono is that app made to build and run against
1.3, under its own name.

## Why 1.3 matters

An app carries the kernel interface version it was compiled against, and
`Libs/Source/AppSystem/system.cpp` refuses to start when the running kernel's
version is lower:

```cpp
if (gIKernel->version < KERNEL_INTERFACE_VERSION) { /* refuse to launch */ }
```

That constant went from `2` to `3` between 1.3 and 1.4. So a Stopwatch built
from the 1.4 SDK will not launch on a watch whose kernel is still v2, and
nothing about the app itself requires v3 — it uses none of what the bump brought
in (home-screen widget messages, `SDK::Variant::Config`, `SDK::SpeedSmoother`).
Building it against 1.3 is the whole fix.

## What it took

One SDK API, in six places. `SDK::send_msg<T>(kernel, ...)` — allocate a
message, send it, release it on return — landed in `MessageGuard.hpp` after 1.3,
together with the variadic forwarding in `make_msg` and
`IAppComm::allocateMessage` that lets a message be built with constructor
arguments. Everything else the app touches was already there at 1.3: the GUI
helpers, `DualAppComm`, `TouchGFXCommandProcessor`, the kernel providers, the
`RequestSetCapabilities` message, and the whole `una_app_*` CMake API.

So the backport is:

- [`Libs/Header/SendMsg.hpp`](Software/Libs/Header/SendMsg.hpp) — `Chrono::sendMsg<T>(kernel)`,
  the nullary half, written on the `MessageGuard` and `make_msg` that 1.3 does
  have. Five of the six call sites read as before.
- `Service::publish()` — the sixth site is the one that carries a payload, so it
  allocates through `make_msg` and assigns the snapshot through the guard
  instead of passing it to the message's constructor.

Both are marked in place. On an SDK of 1.4 or later the header can be deleted
and the call sites pointed back at `SDK::send_msg`, which behaves identically.

## What else changed from upstream

- **Name and AppID.** `Chrono` / `FD92465DEEB5F39C`, so it installs alongside
  UNA's `Stopwatch` rather than replacing it. The on-screen title reads CHRONO;
  the text assets were regenerated for it. The domain code still calls the thing
  a stopwatch, because that is what it is.
- **Out-of-tree paths.** The CMake project and `config/gcc/app.mk` find the SDK
  through `$UNA_SDK`; in the SDK tree they counted `..` levels back to it, which
  means nothing from here.
- **Tests.** The app's host tests lived in the SDK at `Tests/Host/apps/Stopwatch/`
  and were wired into its shared test binary, so they do not travel with the app
  directory. They are in [`Tests/`](Tests) now.

## Buttons

| Button | Position | Running | Stopped |
| --- | --- | --- | --- |
| `SW3` / R1 | top right | pause | start |
| `SW4` / R2 | bottom right | lap | **back — leaves the app** |
| `SW1` / L1 | top left | scroll laps up | scroll laps up |
| `SW2` / L2 | bottom left | scroll laps down | reset |

Leaving does not lose a running clock. The Service owns the state and keeps
timing whether or not the GUI is up, so re-entering picks the time back up — and
the app never closes itself on a timeout.

## How it is put together

```
Software/
├── Libs/
│   ├── Header/Stopwatch.hpp     # the core: a plain state struct + timekeeping
│   ├── Header/Commands.hpp      # the message contract between the halves
│   ├── Header/SendMsg.hpp       # the 1.3 backport
│   ├── Header/Service.hpp
│   └── Sources/Service.cpp      # owns the state, answers every command with a snapshot
└── Apps/
    ├── Chrono-CMake/            # build glue
    └── TouchGFX-GUI/            # the GUI
```

Nothing is polled. The Service sends a full snapshot only when something
changes, and the GUI extrapolates the running time from it against the same
monotonic kernel tick the Service stamped it with. The state is bounded to 50
laps so it fits the largest kernel message pool block and the path stays
allocation-free.

## Building

Point `UNA_SDK` at an SDK checkout at `apps-v1.3.0`:

```sh
export UNA_SDK=/path/to/una-sdk       # checked out at apps-v1.3.0
cd Chrono/Software/Apps/Chrono-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=1.0.0 .. && cmake --build build
```

A later SDK builds it too — `Chrono::sendMsg` keeps working — but then the
resulting app carries interface version 3 and the point of the fork is gone.

## Tests

The stopwatch core is header-only and knows nothing about the SDK, so its tests
need only GoogleTest, which is taken at the SDK's pinned revision:

```sh
export UNA_SDK=/path/to/una-sdk
cd Chrono/Tests
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

## Provenance

Derived from the UNA SDK's MIT-licensed `Stopwatch` example; the original
copyright notices are retained in the files that carry them. The app's history
came across by `git subtree`, so `git log Chrono/` shows where each piece
came from.
