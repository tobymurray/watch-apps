# NotifyToggle — turn phone notifications on or off, on the watch

A utility app that does one thing: show a single centered switch, and let R1
flip it — immediately, and durably. There is no second screen, no
configuration to type in, and nothing else on the display. Built the way
[RustGuiPoc](../RustGuiPoc), [QrGuiPoc](../QrGuiPoc) and [Spin](../Spin) are —
a C++ `Gui.cpp` that owns the kernel message loop and framebuffer, and a Rust
core (`embedded-graphics`, no TouchGFX) that owns only pixels, over a checked
C ABI.

## What it actually toggles

Not a per-app capability, and not a copy of the setting — the real,
watch-wide `phone.notifications` flag inside `2:/settings.json`, the same
file the paired UNA phone app edits. Two things had to be reverse-engineered
on this exact watch unit (kernel 1.4.0) to make that possible at all; the
full derivation, address by address, lives in
[`Docs/Investigations/2026-08-31-live-settings-persistence/`](Docs/Investigations/2026-08-31-live-settings-persistence)
and, canonically kept current, in `una-sdk`'s `research` branch. Short
version:

- **The SDK's own sandboxed filesystem API cannot reach `settings.json` at
  all.** `FileSystemGuard::getFullPath` implements exactly one relative-path
  escape (a hardcoded match on `"../SharedData"`) and rejects every other
  `..` path outright — confirmed both by disassembly and by testing every
  spelling of a two-`..` path on the device. An earlier `SettingsFile.cpp`/
  `SettingsPatch.cpp` attempt at that route proved this conclusively; it's
  gone from this tree now (git history and
  [`Docs/Investigations/2026-08-31-live-settings-persistence/`](Docs/Investigations/2026-08-31-live-settings-persistence)
  have the record), and `Gui.cpp` never called it for the live toggle logic.
- **This hardware has no memory isolation for apps** (no MPU, apps run
  privileged, no TrustZone — confirmed from the watch's own boot-time
  register state). A running app can read and write arbitrary memory,
  including the kernel's own live settings struct, with a plain pointer.
  `LiveSettings.cpp` uses that to read/write the live, in-RAM copy of
  `phone.notifications` directly — immediate effect, the moment R1 is
  pressed.
- **Persisting that change to flash** turned out not to go through the
  settings class's own `save()` method at all — it has zero callers
  anywhere in the firmware; even the real phone app doesn't call it, it
  just overwrites the whole file over Bluetooth. `SettingsPersist.cpp`
  instead calls the kernel's internal, non-virtual `File` class directly
  (the same open/read/write/close primitives the firmware's own
  settings-backup logic already uses for this exact file) to read the real
  current file content, splice in only the `notifications` field, and write
  the whole file back — verified with an immediate readback, and confirmed
  live on the real device to survive a full power cycle.

Everything above is specific to kernel 1.4.0 on this one unit — addresses
that would need re-deriving, not assuming, on another watch or firmware
version. This app never assumes it, either: `Software/Libs/Header/SettingsAddresses.hpp`
holds a small table, one entry per firmware version this investigation has
actually reverse-engineered and cross-validated (today: just `1.4.0`).
`Gui.cpp` queries the watch's real running firmware version at startup
(`SDK::Message::RequestSystemInfo`, a supported message — not the manifest's
`minKernelVersion`, which is only a floor the phone's install flow checks, not
an exact match), looks it up, and refuses to touch a single raw address —
not just persistence, the live RAM read too — on anything not in the table.
Growing this to support a new firmware version means doing that
version's own RE pass and adding a row, never extrapolating from a
neighboring one.

## Screen and buttons

One screen: the word `NOTIFICATIONS`, a pill switch (green and right when on,
grey and left when off), the current state as text, and a button hint.

| Button | Does |
|---|---|
| R1 (`SW2`) | Toggle. Flips the live value, persists it to `settings.json`, and re-reads to confirm. |
| R2 (`SW4`) | Back — exits the app. |

Unlike a capability scoped to "while this app runs," closing the app changes
nothing: the watch-wide flag stays exactly where R1 left it, because that's
genuinely what got written to flash.

## Building

Targets **`apps-v1.4.0`**, the same as RustGuiPoc, QrGuiPoc and Spin.

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/NotifyToggle-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 .
cmake --build build
```

The `.uapp` lands in `Output/`; deploy it per the SDK's `Docs/deploy.md`.

### Footprint

From a real build against `apps-v1.4.0` (`arm-none-eabi-size`, 600 KiB GUI
RAM window, code executing from RAM):

```
GUI     .text 30,320   .data 188   .bss 68,668   total 99,176 of 614,400  =  16.1%
Service .text  3,724   .data 176   .bss 11,324   (a near-empty stub — see below)
```

`.bss` is mostly the 57,600-byte framebuffer, as on every CustomGUI app in
this repo. There's no `SDK::AppConfig`/JSON dependency linked in at all —
this app never reads or writes an app-scoped config file of its own, only
the kernel's real settings struct and the real `settings.json`, both via raw
pointers and a hand-derived internal `File` class. Re-derive the numbers
from the map's own `Memory Configuration` block rather than trusting this
table.

## Why persistence lives in the GUI process

Reading, drawing, flipping and persisting the toggle are all GUI-side,
because R1 is a GUI-side event and there's no reason to hop to the Service
process and back for a change that finishes in microseconds.
`Software/Libs/Sources/Service.cpp` is not a pure stub — it runs a battery
of read-only filesystem probes at startup (drive roots, `SharedData`
spellings, two-hop `..` resolution) left over from the investigation that
found the sandboxed API dead-ended, logged to `service-debug.log`. It never
touches `settings.json` or the live struct itself.

## Tests

```sh
cd Software/Apps/CustomGUI/rust
cargo test --features std
```

`knob_moves_side_with_state` and `pill_fill_reflects_state` are the ones
worth reading first: they sample specific pixels rather than diffing whole
frames, so each one states the one thing that has to be true of this screen
— the knob is on the side the state says, and the fill colour matches it —
rather than merely asserting that toggling `enabled` changes *something*.

The C++ half type-checks on a host without the ARM toolchain, which catches
renames across the ABI:

```sh
clang++ -std=c++17 -fsyntax-only -Wall -I"$UNA_SDK/Libs/Header" \
  -ISoftware/Libs/Header -ISoftware/Apps/CustomGUI \
  Software/Apps/CustomGUI/Gui.cpp Software/Libs/Sources/Service.cpp \
  Software/Libs/Sources/SettingsAddresses.cpp Software/Libs/Sources/LiveSettings.cpp \
  Software/Libs/Sources/SettingsPersist.cpp Software/Libs/Sources/DebugLog.cpp
```

`app-manifest.json` is checked the same way every other app's is:

```sh
python3 $UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py \
    --check app-manifest.json
```

## Desktop simulator

```sh
brew install sdl2   # or the Linux equivalent
cd Software/Apps/CustomGUI/rust
cargo run --bin sim --features sim
```

`SPACE` or `ENTER` toggles (matching R1), `ESC` or `BACKSPACE` quits
(matching R2). It calls the same `notify_toggle_gui::render()` the firmware
calls, into an identical buffer, so the framebuffer matches the device's by
construction.
