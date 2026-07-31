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
            ├── Cargo.toml
            ├── .cargo/config.toml        # target = thumbv8m.main-none-eabihf
            └── src/lib.rs                # ABGR2222 DrawTarget + two screens
```

## Build status (as scaffolded)

| Piece | Status |
|---|---|
| **Rust core** (`libpoc_gui.a`, Cortex-M33) | ✅ **Builds** — `cargo build --release` compiles clean for `thumbv8m.main-none-eabihf` (hard-float, matching the SDK's `-mfpu=fpv5-sp-d16 -mfloat-abi=hard`); exports `poc_gui_render` + `poc_gui_screen_count`. |
| **C++ shim + `.uapp` link** | ⏳ **Not built in the authoring env** — needs `arm-none-eabi-gcc` + `cmake`, which weren't installed there. The sources are written against the SDK's verbatim idioms; build on a toolchain machine or in CI (below). |
| **On-device run** | ⏳ Untested — pending the full build + a watch (or simulator). |

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
