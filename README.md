# RustGuiPoc — a Rust frontend for a UNA Watch app

A proof of concept: a watch app whose GUI is drawn by **Rust + embedded-graphics**
instead of TouchGFX, using the SDK's existing **CustomGUI** entry point. It
renders two switchable screens into the watch's 8bpp **ABGR2222** framebuffer and
pushes them over the kernel message bus — no TouchGFX, no reverse-engineering,
entirely on the supported app path.

This is the "dip a toe into embedded Rust on the frontend" step, ahead of any
bare-metal Embassy firmware work.

## What it demonstrates

- A **non-TouchGFX GUI** via `Libs/Source/AppSystem/EntryPoint/CustomGUI/main.cpp`
  (the SDK's own seam: it needs only a `Gui { Gui(kernel); run(); }` class).
- The **framebuffer message protocol**: `RequestDisplayConfig` → render →
  `RequestDisplayUpdate`, paced by `EVENT_GUI_TICK`, gated by GUI resume/suspend
  — mirroring `Libs/Source/Port/TouchGFX/TouchGFXCommandProcessor.cpp`.
- A clean **C++ ⇄ Rust seam**: the C++ shim owns all kernel plumbing; the Rust
  `no_std` staticlib owns *only* rendering, over a tiny C ABI (`poc_gui.h`).
- **Switching between two UIs** on-device: `SW2` (top-right button) cycles screens.

## Layout

```
Software/
├── Libs/                          # minimal, stateless Service half (every .uapp needs one)
│   ├── Header/Service.hpp
│   ├── Sources/Service.cpp
│   └── libs.cmake
└── Apps/
    ├── RustGuiPoc-CMake/CMakeLists.txt   # build glue (mirrors an example app)
    └── CustomGUI/
        ├── Gui.hpp / Gui.cpp             # C++ shim: message loop + framebuffer
        ├── poc_gui.h                     # C ABI to the Rust core
        └── rust/                         # no_std embedded-graphics rendering crate
            ├── Cargo.toml                # device staticlib + optional `sim` bin
            ├── .cargo/config.toml
            └── src/
                ├── lib.rs                # ABGR2222 DrawTarget + render() (shared)
                └── bin/sim.rs            # desktop SDL simulator (--features sim)
```

## Build status (as scaffolded)

| Piece | Status |
|---|---|
| **Rust core** (`libpoc_gui.a`, Cortex-M33) | ✅ **Builds** — `cargo build --release` compiles clean for `thumbv8m.main-none-eabihf` (hard-float, matching the SDK's `-mfpu=fpv5-sp-d16 -mfloat-abi=hard`); exports `poc_gui_render` + `poc_gui_screen_count`. |
| **C++ shim + `.uapp` link** | ⏳ **Not built in the authoring env** — needs `arm-none-eabi-gcc` + `cmake`, which weren't installed there. The sources are written against the SDK's verbatim idioms; build on a toolchain machine or in CI (below). |
| **Desktop `sim` bin** | ✅ **Compiles** — links the same crate/`render()`; only the SDL2 *system* library is needed to finish linking (`brew install sdl2`). |
| **On-device run** | ⏳ Iterating on hardware (see the render notes above). |

The interesting/novel half (Rust rendering into ABGR2222 on Cortex-M33) is
proven to compile; the remaining work is the ordinary arm-gcc link, which the
CMake expresses.

## Building the full `.uapp`

Requires the ARM embedded toolchain, CMake ≥ 3.21, Python 3, and a Rust toolchain
with the Cortex-M33 target.

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk          # repo root
cd Software/Apps/RustGuiPoc-CMake
cmake -B build -G Ninja                   # una-app.cmake selects the arm toolchain
cmake --build build                       # cargo builds libpoc_gui.a, then arm-gcc links the ELFs + merges the .uapp
```

The resulting `*.uapp` is copied to `Software/Output/`; deploy it over USB mass
storage per `Docs/deploy.md`.

## Desktop simulator (develop without the watch)

A host SDL window that renders the GUI, so you can iterate layout/animation/logic
in seconds instead of the reflash loop.

```sh
brew install sdl2                                    # macOS (or the OS SDL2 dev pkg)
cd Software/Apps/CustomGUI/rust
cargo run --bin sim --features sim                   # live animation
cargo run --bin sim --features sim -- fb_dump.bin    # view a raw device fb dump
```

Controls: any key = next screen; close the window = quit.

### Accuracy: the sim runs the same code as the device

The sim links this crate and calls the **same `poc_gui::render()`** the firmware
calls, into an identical 240×240 ABGR2222 buffer — so the framebuffer is
byte-identical to the device's *by construction* (no reimplementation to drift).
The sim then applies only what the physical panel does:

- **Color** — decodes ABGR2222 with the panel's 2-bits-per-channel gamut
  (0/85/170/255), so on-screen colors match the device's 64-color range, not full
  24-bit color.
- **Shape** — a round mask (black outside the inscribed circle) reproducing the
  circular bezel.
- **Panel quirks** — `emulate_panel()` in `sim.rs` is an identity hook today; it's
  the single place to encode device-only behavior (see below).

What the sim **cannot** show without calibration: the on-device **font-glyph
under-render** (thin features vanish on the panel — the reason the clock is drawn
as filled-rectangle 7-segment digits). That's physical panel behavior, not in the
buffer, so the sim renders such text fine while the watch doesn't.

## Tightening the sim ↔ hardware loop

The goal is that "looks right in the sim" ⇒ "looks right on the watch". Three levers:

1. **Single source of truth (done).** Sim and device call the same `render()`; the
   only differences left are the panel's, not code drift.
2. **Framebuffer dump + compare (wired).** On-device, **long-press SW3**
   (bottom-left / DOWN) to write the current framebuffer to
   `Apps/RustGuiPoc/fb_dump.bin`. Pull it over USB mass storage and load it in the
   sim: `cargo run --bin sim --features sim -- fb_dump.bin`. Because both sides use
   the same `render()`, the dumped bytes equal what the sim renders for that
   screen — so this confirms parity at the framebuffer level and isolates every
   remaining visual difference as *pure panel behavior*. (Static elements match
   exactly; animated ones like the ball depend on the frame counter.)
3. **Calibrate `emulate_panel()`.** Feed the differences from step 2 into the
   `emulate_panel()` hook (e.g. a thin-feature erosion approximating the glyph
   drop-out). Once calibrated, the sim predicts what the watch will actually show.

## Known PoC shortcuts (flagged for a real version)

- **CMake reuses the `TOUCHGFX_PATH` gate** so the SDK's `una_app_build_app()`
  merges our CustomGUI ELF, and **`TOUCHGFX_LIBS`** as the hook to link the Rust
  archive. A clean version would add a dedicated `CUSTOMGUI_PATH` path to
  `cmake/una-app.cmake` instead of borrowing the TouchGFX names.
- **Display config is queried but clamped** to a 240×240 / 8bpp static
  framebuffer. Confirm the real panel's resolution/format via `RequestDisplayConfig`
  on hardware and size the buffer to it.
- **The "clock" is derived from the frame counter**, not a real time source — the
  PoC proves the render loop is live, not timekeeping.
- **No icons / minimal Service** — frontend demo only.

## Why this and not Slint

At 8bpp ABGR2222 on a few-bit reflective LCD, Slint's rendering sophistication
(AA text, gradients) can't show, and its runtime/integration cost isn't worth it
for a handful of button-navigated screens. embedded-graphics' blocky primitives
match the panel and keep the toolchain minimal while learning. Because both just
fill a framebuffer, migrating to Slint later reuses this entire shim/loop.
