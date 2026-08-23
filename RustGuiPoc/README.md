# RustGuiPoc — a Rust frontend for a UNA Watch app

A watch app whose GUI is drawn by **Rust + embedded-graphics** instead of
TouchGFX, through the SDK's **CustomGUI** entry point, which asks only for a
`Gui { Gui(kernel); run(); }` class. It renders whole frames into the watch's
8bpp **ABGR2222** framebuffer and pushes them over the kernel message bus.

The GUI half links no TouchGFX. `Software/Apps/CustomGUI/Gui.cpp` owns the
kernel message loop and the framebuffer; the Rust crate under
`Software/Apps/CustomGUI/rust` owns only pixels, over the C ABI in `poc_gui.h`.

## Why there is a Service half

A GUI process cannot reach a sensor: `SDK::Sensor::Connection` works only from
the Service half (`SDK/Kernel/Kernel.hpp` defines what a GUI process is handed).
So an accelerometer reading travels:

```
accelerometer
  → Service.cpp        SDK::Sensor::Connection
  → Commands.hpp       CustomMessage::AccelValues
  → Gui.cpp            ages the sample, fills poc_gui_state
  → lib.rs             render(&State)
  → RequestDisplayUpdate
```

Two properties that path relies on:

- **`render()` is a pure function of `(buffer, geometry, screen, state)`.** The
  device, the simulator and the tests all call it through one `State`, so they
  cannot disagree about what a given reading looks like.
- **`Gui.cpp` decides whether a sample is fresh; the renderer never guesses.**
  Past `kStaleAfterMs` the screen shows `NO DATA` rather than the last number it
  saw. That threshold is set from measurement, not from the sample period: the
  sensor layer aggregates on a ~1 s timer that no app-side setting moves.
- **Every screen carries a heartbeat, positioned from the clock.** It only moves
  when a frame is drawn, so a stalled loop freezes it — but because the position
  is a function of uptime rather than of the frame count, a lap is a known 3.2 s
  and dropped frames show as a visible jump rather than as a silently slower
  orbit. A frame-counted marker proves liveness and nothing else: timing a run by
  it would assume the frame rate it is meant to reveal.

A Rust panic reaches the SDK logger through `poc_gui_host_panic` in `Gui.cpp`,
which logs the message and location and then exits the app. Without that a panic
would hang the GUI thread silently, and the only way out would be a reboot.

## Screens

| Screen | Shows |
|--------|-------|
| `ACCEL` | Bubble level driven by live X/Y tilt, and X/Y/Z in milli-g. `NO DATA` when the sample is stale. |
| `DIAG` | Frames rendered, samples received, age of the newest sample, and live/stale/none. |
| `DITHER` | One luminance ramp quantised two ways. The panel has four levels a channel, so the left half can only render it as four bands; the right half ordered-dithers it and reads as a smooth gradient. TouchGFX has a gradient painter for this format and no dithering anywhere in the framework, so it draws the left half. |

## Buttons

| Button | Position | Does |
|---|---|---|
| `SW2` / R1 | top right | cycle to the next screen |
| `SW4` / R2 | bottom right | back — leaves the app |
| `SW3` / L2, long press | bottom left | write the framebuffer to `fb_dump.bin` |

The screens are a cycle, not a stack, so back exits. This app owns the kernel
message loop and swallows every button event: with nothing handling back, the
only way out is rebooting the watch.

## Building

Targets **`apps-v1.4.0`**. Needs the ARM embedded toolchain, CMake, Python 3 with
`pyelftools`, and a Rust toolchain with the Cortex-M33 target.

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/RustGuiPoc-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.12.0 .
cmake --build build
```

Kira's toolchain image has the ARM tools and the right Rust target already, and
is the shortest path from a machine with neither:

```sh
docker run --rm -v /path/to/una-sdk:/sdk -v "$PWD":/apps -e UNA_SDK=/sdk \
  -w /apps/RustGuiPoc/Software/Apps/RustGuiPoc-CMake \
  ghcr.io/tobymurray/kira-toolchain \
  bash -c 'pip3 install --break-system-packages -r /sdk/Utilities/Scripts/app_packer/requirements.txt
           cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.12.0 . && cmake --build build'
```

That image ships neither `pyelftools` nor `Pillow`, both of which the SDK's
packing scripts import, hence the `pip3` line. Installing them in the job is at
odds with what the image is for, so they belong in the image.

The `.uapp` lands in `Output/`; deploy it per the SDK's `Docs/deploy.md`.

### Footprint

The GUI process gets a 600 KiB RAM window and runs its code from RAM, so
framework size is charged against the same budget as the framebuffer. From
`Output/RustGuiPocGUI.elf.elf.map`:

```
.text 26,844   .data 68   .got 68   .bss 58,204   .stack 10,240
total 95,428 of 614,400  =  15.5%
```

`.bss` is almost entirely the 57,600-byte framebuffer. Re-derive the numbers from
the map's `Memory Configuration` block and section headers rather than trusting
this table.

## Tests

```sh
cd Software/Apps/CustomGUI/rust
cargo test --features std
```

The tests in `src/lib.rs` pin the ABI struct layout and `render()`'s refusal to
write outside the geometry it was given, including when handed hostile sensor
values. Their names say what each one holds.

The C++ half type-checks on a host without the ARM toolchain, which catches
renames across the ABI:

```sh
clang++ -std=c++17 -fsyntax-only -Wall -I"$UNA_SDK/Libs/Header" \
  -ISoftware/Libs/Header -ISoftware/Apps/CustomGUI \
  Software/Apps/CustomGUI/Gui.cpp Software/Libs/Sources/Service.cpp
```

## Desktop simulator

```sh
brew install sdl2
cd Software/Apps/CustomGUI/rust
cargo run --bin sim --features sim                   # interactive
cargo run --bin sim --features sim -- fb_dump.bin    # view a device dump
```

Controls: arrow keys tilt, `TAB` changes screen, `S` freezes the sample feed to
reach the `NO DATA` path, closing the window quits.

The simulator calls the same `poc_gui::render()` the firmware calls, into an
identical buffer, then applies what the panel does to those bytes: the
2-bits-per-channel gamut and a round mask for the bezel. So the framebuffer
matches the device's by construction, and a dump pulled off the watch can be
compared against it — feeding the simulator the same `State` the device had,
since the readouts are a function of the live sample.

`emulate_panel()` in `sim.rs` is where device-only behaviour goes. It is an
identity function, so the simulator confirms the framebuffer matches; it does not
predict the glass.

## What the hardware runs established

Five runs of instrumented captures went into this app and have been taken back
out. What they found — the sensor pipeline's real cadence, the ODR ladder, the
tick rate, the push cost, the TouchGFX comparison and the traps — is in
[`Docs/FINDINGS.md`](Docs/FINDINGS.md), with the raw logs in
[`Captures/`](Captures).

## The panel drops dark-on-light text

Measured on hardware: bright glyphs on the dark background render crisply, but
dark thin glyphs on a light fill vanish — an early black-text-on-white-box
readout showed as a blank white band. This is the panel, not the framebuffer, so
the simulator will happily show dark-on-light text the watch would drop.
