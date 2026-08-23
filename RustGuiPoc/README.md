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
- **A real sensor on screen**, over the whole path a real app has to use —
  see below. Nothing on either screen is invented.

## The data path is the point

A GUI process on this platform **cannot read a sensor.** `SDK::Kernel` is
`{sys, log, mem, comm, fs}` and nothing more; `SDK::Sensor::Connection` only
works from the Service half. So a live accelerometer reading has to travel:

```
accelerometer
  → Service (SDK::Sensor::Connection, 10 Hz)     Software/Libs/Sources/Service.cpp
  → CustomMessage::AccelValues over the bus      Software/Libs/Header/Commands.hpp
  → Gui.cpp (ages the sample, owns the clock)    Software/Apps/CustomGUI/Gui.cpp
  → Rust render(&State)                          rust/src/lib.rs
  → RequestDisplayUpdate → panel
```

That is the half of the architecture a rendering demo cannot prove, and it is
the half that decides whether the approach is usable. Two details worth copying:

- **`render()` is a pure function of `(buffer, geometry, screen, state)`.** The
  device, the host simulator and the unit tests all drive the one renderer
  through the one `State` struct, so none of them can drift and none has a
  private path to the truth.
- **The shim decides what is trustworthy, not the renderer.** `Gui.cpp` ages the
  newest sample against `sys.getTimeMs()` and clears `valid` past 500 ms; the
  screen then shows `NO DATA` rather than the last number it happened to see. A
  sensor display that keeps showing a stale reading is worse than one that admits
  it has nothing.

Handling a custom message costs exactly one `case` here. The TouchGFX port has to
queue custom messages and replay them outside its frame cycle (see
`Docs/TouchGFX-Port-Architecture.md` on `mUserQueue`); owning the loop outright
means the sensor sample is just another message.

## Screens

| # | Screen | Shows |
|---|--------|-------|
| 1 | `ACCEL` | Bubble level driven by live X/Y tilt, plus X/Y/Z in milli-g. Tilt the watch and the dot moves — the whole path in one gesture, with no strap, no GNSS fix and no warm-up. `NO DATA` when the sample is stale. |
| 2 | `DIAG` | Frames rendered, samples received, age of the newest sample, and live/stale/none. What you actually want while bringing up a rendering stack. |

## Buttons

| Button | Position | Does |
|---|---|---|
| `SW2` / R1 | top right | cycle to the next screen |
| `SW4` / R2 | bottom right | **back — leaves the app** |
| `SW3` / L2, long press | bottom left | dump the framebuffer for the desktop sim |

The screens are a cycle rather than a stack, so back has nothing to go back *to*
and exits instead — which is what the SDK's own apps do with R2. It is not
optional: this app owns the kernel message loop and swallows every button event,
so with nothing handling back there is no way out of it but rebooting the watch.

## Layout

```
Software/
├── Libs/                          # Service half: owns the sensor
│   ├── Header/Commands.hpp        # CustomMessage::AccelValues (Service -> GUI)
│   ├── Header/Service.hpp
│   ├── Sources/Service.cpp        # SDK::Sensor::Connection -> send_msg
│   └── libs.cmake
└── Apps/
    ├── RustGuiPoc-CMake/CMakeLists.txt   # build glue (mirrors an example app)
    └── CustomGUI/
        ├── Gui.hpp / Gui.cpp             # C++ shim: message loop, framebuffer, State
        ├── poc_gui.h                     # C ABI to the Rust core (+ poc_gui_state)
        └── rust/                         # no_std embedded-graphics rendering crate
            ├── Cargo.toml                # device staticlib + optional `sim` bin
            ├── .cargo/config.toml
            └── src/
                ├── lib.rs                # ABGR2222 DrawTarget + render() + tests
                └── bin/sim.rs            # desktop SDL simulator (--features sim)
```

## Build status

| Piece | Status |
|---|---|
| **Rust core** (`libpoc_gui.a`, Cortex-M33) | ✅ **Builds clean** for `thumbv8m.main-none-eabihf` (hard-float, matching the SDK's `-mfpu=fpv5-sp-d16 -mfloat-abi=hard`); exports `poc_gui_render` + `poc_gui_screen_count`. |
| **Host tests** | ✅ **5 pass** (`cargo test --features std`) — ABI struct layout, undersized-buffer no-op, no writes past the stated geometry, stale ≠ live, determinism. |
| **Both screens** | ✅ **Rendered and visually checked** off-device, by calling the same `render()` into a 240×240 buffer and decoding it through the panel's gamut + round mask. |
| **Desktop `sim` bin** | ✅ **Type-checks** (`cargo check --bin sim --features sim`). Linking additionally needs the SDL2 *system* library (`brew install sdl2`). |
| **C++ shim + `.uapp` link** | ⏳ **Not built here** — needs `arm-none-eabi-gcc` + `cmake`. The sources follow the SDK's verbatim idioms; build on a toolchain machine or in CI. |
| **Sensor path on hardware** | ⏳ **Unproven.** The Service → message → shim → render path compiles and is exercised in the sim with synthetic state, but has not been on a wrist. |

Note the split: the *rendering* half is verified off-device several ways, and the
panel-legibility findings below came from earlier hardware runs. The **data path
is the part still owed a hardware run** — treat its numbers as untested until
that happens.

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

## Tests

```sh
cd Software/Apps/CustomGUI/rust
cargo test --features std
```

Six tests, in `src/lib.rs`. They exist to pin the properties the design leans on
rather than to cover drawing code:

| Test | Pins |
|---|---|
| `state_layout_matches_c` | `State` is 28 bytes, 4-aligned — a silent mismatch with `poc_gui_state` would corrupt every reading. |
| `undersized_buffer_is_a_no_op` | A buffer too small for the stated geometry is left untouched, not partly painted. The caller cannot make `render()` overrun. |
| `never_writes_past_the_stated_geometry` | Every screen stays inside `width * height`, even handed a longer buffer — which the device does, since it always passes the whole 240×240 array. |
| `stale_state_renders_differently` | Stale data does not render as live data. This is the behaviour the whole `valid`/age path exists to produce. |
| `hostile_sensor_values_stay_in_bounds` | NaN, ±∞ and absurd magnitudes from a glitching driver neither escape the framebuffer nor panic — and the GUI's panic handler is an infinite loop, so a panic here is a frozen watch. |
| `render_is_deterministic` | Same state in, same pixels out — the property the simulator's fidelity claim rests on. |

## Desktop simulator (develop without the watch)

A host SDL window that renders the GUI, so you can iterate layout/animation/logic
in seconds instead of the reflash loop.

```sh
brew install sdl2                                    # macOS (or the OS SDL2 dev pkg)
cd Software/Apps/CustomGUI/rust
cargo run --bin sim --features sim                   # live animation
cargo run --bin sim --features sim -- fb_dump.bin    # view a raw device fb dump
```

Controls: **arrow keys** = tilt (drives the bubble and the milli-g readout),
**TAB** = next screen, **S** = freeze the sample feed to exercise the `NO DATA`
path, close the window = quit.

The sim stands in for the Service: it synthesises a `State` at the same 10 Hz the
device samples at, and applies the same 500 ms staleness rule as `Gui.cpp`. So
layout, staleness and the diagnostics screen can all be exercised without a
watch — including the stale path, which is awkward to reproduce on a wrist.

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

What the sim **cannot** show without calibration: the on-device **dark-on-light
drop-out**. Verified on hardware — bright text on the dark background renders
crisply, but dark thin glyphs on a light fill vanish (an early
black-text-on-white-box readout showed as a blank white band). That asymmetry is
physical panel behavior, not in the buffer, so the sim will happily show
dark-on-light text that the watch would drop. It is why every label in `lib.rs`
is bright-on-black, and why that is a rule rather than a style choice.

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
   remaining visual difference as *pure panel behavior*. (To compare exactly,
   feed the sim the same `State` the device had: the readouts are a function of
   the live sample, so a dump is only reproducible alongside its numbers.)
3. **Calibrate `emulate_panel()`.** Feed the differences from step 2 into the
   `emulate_panel()` hook (e.g. modelling the dark-on-light drop-out — eroding dark
   thin runs that sit on a light fill). Once calibrated, the sim predicts what the
   watch will actually show.

## Known PoC shortcuts (flagged for a real version)

- **CMake reuses the `TOUCHGFX_PATH` gate** so the SDK's `una_app_build_app()`
  merges our CustomGUI ELF, and **`TOUCHGFX_LIBS`** as the hook to link the Rust
  archive. A clean version would add a dedicated `CUSTOMGUI_PATH` path to
  `cmake/una-app.cmake` instead of borrowing the TouchGFX names.
- **Display config is queried but clamped** to a 240×240 / 8bpp static
  framebuffer. Confirm the real panel's resolution/format via `RequestDisplayConfig`
  on hardware and size the buffer to it.
- **The panic handler is `loop {}`** — on-device a Rust panic freezes the GUI
  thread silently, with no diagnostic. A real app should route it to the SDK
  logger and then exit. Flagged here because this file invites copying.
- **`emulate_panel()` is still an identity hook**, so the sim confirms parity at
  the framebuffer level but does not yet *predict* the panel. See above.
- **No CI**, so none of the build-status claims above are independently
  reproducible yet. A workflow that builds for `thumbv8m` and runs the host tests
  would fix that; a headless render backend (no SDL) would let it check frames too.

## Why this and not Slint

At 8bpp ABGR2222 on a few-bit reflective LCD, Slint's rendering sophistication
(AA text, gradients) can't show, and its runtime/integration cost isn't worth it
for a handful of button-navigated screens. embedded-graphics' blocky primitives
match the panel and keep the toolchain minimal while learning. Because both just
fill a framebuffer, migrating to Slint later reuses this entire shim/loop.
