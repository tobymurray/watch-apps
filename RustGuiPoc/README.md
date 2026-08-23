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
  saw.

A Rust panic reaches the SDK logger through `poc_gui_host_panic` in `Gui.cpp`,
which logs the message and location and then exits the app. Without that a panic
would hang the GUI thread silently, and the only way out would be a reboot.

## Sample log

Every accelerometer sample that reaches the GUI is recorded, so a run can be left
going and the timing analysed afterwards rather than read off a 240px screen.
Samples buffer in RAM and flush every 512 rows, on screen suspend, on a long-press
of `SW3`, and on exit; each run truncates the file, so one run is one experiment.

Two files land in the app's own folder. `accel_log.csv`, one row per sample:

```
seq,sensor_ts_ms,arrival_ms,batch,x_mg,y_mg,z_mg
```

and `render_log.csv`, one row per frame, which times the framebuffer push:

```
at_ms,push_ms,push_ok
```

`push_ms` is how long `sendMessage` took for `RequestDisplayUpdate`. It matters
because that call blocks the message loop: while it waits, sample messages queue
and then arrive in a burst, so a slow push shows up as bunched arrivals in the
other file rather than as anything obviously display-related.

`sensor_ts_ms` is the driver's own timestamp for the sample and `arrival_ms` is
when the GUI got it, so the true sample interval can be told apart from pipeline
latency. `batch` is how many samples the sensor layer delivered in that event —
if the driver batches, the interval between events is not the sample interval.

```sh
# actual delivery interval, from the driver's own clock
awk -F, 'NR>2 {print $2-p} {p=$2}' accel_log.csv | sort -n | uniq -c
```

## Screens

| Screen | Shows |
|--------|-------|
| `ACCEL` | Bubble level driven by live X/Y tilt, and X/Y/Z in milli-g. `NO DATA` when the sample is stale. |
| `DIAG` | Frames rendered, samples received, age of the newest sample, the largest age seen, how long the last framebuffer push took, and live/stale/none. |
| `DITHER` | One luminance ramp quantised two ways. The panel has four levels a channel, so the left half can only render it as four bands; the right half ordered-dithers it and reads as a smooth gradient. TouchGFX has a gradient painter for this format and no dithering anywhere in the framework, so it draws the left half. |

## Buttons

| Button | Position | Does |
|---|---|---|
| `SW2` / R1 | top right | cycle to the next screen |
| `SW4` / R2 | bottom right | back — leaves the app |
| `SW3` / L2, long press | bottom left | dump the framebuffer and flush the sample log |

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
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.6.0 .
cmake --build build
```

Kira's toolchain image has the ARM tools and the right Rust target already, and
is the shortest path from a machine with neither:

```sh
docker run --rm -v /path/to/una-sdk:/sdk -v "$PWD":/apps -e UNA_SDK=/sdk \
  -w /apps/RustGuiPoc/Software/Apps/RustGuiPoc-CMake \
  ghcr.io/tobymurray/kira-toolchain \
  bash -c 'pip3 install --break-system-packages -r /sdk/Utilities/Scripts/app_packer/requirements.txt
           cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.6.0 . && cmake --build build'
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
.text 28,256   .data 68   .got 68   .bss 69,504   .stack 10,240
total 108,136 of 614,400  =  17.6%
```

`.bss` is the 57,600-byte framebuffer plus the two log buffers. Re-derive the numbers from
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

## The panel drops dark-on-light text

Measured on hardware: bright glyphs on the dark background render crisply, but
dark thin glyphs on a light fill vanish — an early black-text-on-white-box
readout showed as a blank white band. This is the panel, not the framebuffer, so
the simulator will happily show dark-on-light text the watch would drop.
