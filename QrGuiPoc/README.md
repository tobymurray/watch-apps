# QrGuiPoc — does Barcode's QR screen survive a Rust rewrite?

A single-screen proof slice, built to answer one question before committing to
anything larger: what does [Barcode](../Barcode)'s QR screen cost through the
SDK's **CustomGUI** entry point (Rust + `embedded-graphics`, no TouchGFX)
instead of the TouchGFX widget tree it ships with today? See
[RustGuiPoc](../RustGuiPoc) for the architecture this borrows — the split
between a C++ `Gui.cpp` that owns the kernel message loop and framebuffer and a
Rust core that owns only pixels, over a C ABI checked at both compile time and
by a runtime fingerprint.

The id is hardcoded to `GYMWORLD12345678` — [`Docs/QR.md`](../Barcode/Docs/QR.md)'s
own canonical example — because this app exists to measure a render path, not
to be configured. There is no `AppConfig`, no Service ↔ GUI messaging, and no
second screen: those are all real cost centres in Barcode that this slice
deliberately does not carry, so it is not itself a Barcode replacement. It is
the cheapest artifact that answers "does this port, and what does it cost."

## What is actually reused

`Software/Apps/CustomGUI/Gui.cpp` calls
[`Qr::encode()`](../Barcode/Software/Libs/Header/Qr.hpp) — Barcode's own
encoder, included unmodified via a cross-app include path, not copied. The
`Barcode::Matrix` it produces is copied field-for-field into `qr_gui_state`
(`qr_gui.h`), a plain C struct with the identical layout, which crosses into
the Rust core over the C ABI. Everything upstream of the pixel grid — Reed-
Solomon, mask selection, the module grid itself — is exactly what Barcode
draws; only the widget tree that turns modules into pixels was rewritten.

## Building

Targets **`apps-v1.4.0`**, the same as RustGuiPoc.

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/QrGuiPoc-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 .
cmake --build build
```

The `.uapp` lands in `Output/`.

### Footprint

From a real build against `apps-v1.4.0`, comparing `Output/QrGuiPocGUI.elf.elf.map`
against Barcode 0.6.0's own `Output/BarcodeGUI.elf.elf.map` (`arm-none-eabi-size`,
same 600 KiB GUI RAM window, code executing from RAM on both):

| | QrGuiPoc | Barcode (TouchGFX) |
|---|---|---|
| `.text` | 17,200 | 84,712 |
| `.data` | 144 | 2,376 |
| `.bss` | 68,484 | 65,560 |
| total | **85,828 — 14.0%** | 152,648 — 24.8% |

`.bss` lands close on both: it is almost entirely the 57,600-byte framebuffer
either way. The gap is in `.text` — QrGuiPoc's is a fifth of Barcode's, which is
the TouchGFX framework tax RustGuiPoc's own findings already named, not
something specific to QR.

The packaged `.uapp` files tell the same story at the artifact level:
**24,452 bytes** against Barcode 0.6.0's 120,444 — the framework and its two
widgets' worth of Designer-generated glue is most of what a `.uapp` spends,
not the encoder or the pixels.

## What this does not answer

- **Text.** Barcode's id line uses a proportional TouchGFX font that picks
  between two point sizes to fit an id on one line; `embedded-graphics`'s
  `MonoTextStyle` is fixed-width bitmap only. QR has no text on this screen, so
  the gap was sidestepped rather than closed. Porting Code128 or the prompt
  screens means answering it first.
- **Dark-on-light rendering on real glass.** RustGuiPoc's hardware runs found
  that *thin* dark strokes on a light fill drop out on this panel — an early
  black-text-on-white-box readout came back blank. A QR module is a solid
  filled square, not a stroke, and Barcode's own TouchGFX QR screen already
  ships the identical white-quiet-zone-black-modules rendering to real users,
  so this is not expected to reproduce that failure — but it has not been
  confirmed on this app's own output on hardware, only in the simulator below.
- **Multi-screen, Service↔GUI messaging, AppConfig.** Nothing here exercises
  them; RustGuiPoc already does, for the accelerometer case.

## Tests

```sh
cd Software/Apps/CustomGUI/rust
cargo test --features std
```

`quiet_zone_is_white_and_first_module_is_dark` and
`never_writes_past_the_stated_geometry` are the ones worth reading first: the
first is the only check that the geometry constants (copied from
[`BarcodeLayout.hpp`](../Barcode/Software/Libs/Header/BarcodeLayout.hpp)) still
line up with where a module actually lands, and the second is what
`RustGuiPoc/Docs/FINDINGS.md` calls the property a `no_std` renderer has to
hold when nothing else is bounds-checking it.

The C++ half type-checks on a host without the ARM toolchain:

```sh
clang++ -std=c++17 -fsyntax-only -I"$UNA_SDK/Libs/Header" \
  -ISoftware/Libs/Header -ISoftware/Apps/CustomGUI \
  -I../Barcode/Software/Libs/Header \
  Software/Apps/CustomGUI/Gui.cpp Software/Libs/Sources/Service.cpp
```

## Desktop simulator

```sh
brew install sdl2   # or the Linux equivalent
cd Software/Apps/CustomGUI/rust
cargo run --bin sim --features sim
```

Renders the same fixed `GYMWORLD12345678` symbol the watch would, through the
identical `render()` the firmware calls. The `State` it feeds in
(`src/gymworld_state.rs`) was generated once from a host build of Barcode's
real `Qr::encode()` — not hand-transcribed — specifically so a bit-flip in that
literal cannot hide as a plausible-looking QR code; see the comment at the top
of that file for how to regenerate it.
