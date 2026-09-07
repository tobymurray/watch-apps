# Units — switch the watch between metric and imperial, on the watch

The launcher name is `Units`; the directory, the binary and the CMake
`APP_NAME` stay `UnitToggle`.

A utility app that does one thing: show which units the watch is set to, and
let R1 change it — immediately, and durably. There is no second screen and
nothing else on the display. Built on the same shape as
[`NotifyToggle`](../NotifyToggle) — a C++ `Gui.cpp` that owns the kernel
message loop and framebuffer, a Rust core (`embedded-graphics`, no TouchGFX)
that owns only pixels over a checked C ABI, and
[`SettingsKit`](../SettingsKit) underneath both for everything that touches the
watch's real settings.

> **Run on a watch on 2026-09-07**, on the author's unit, kernel 1.4.0, in both
> modes: the gate accepted, the offset held against the kernel's own report, and
> with saving switched on the commit wrote `2:/settings.json` and verified it on
> flash. [What has not been proved](#what-has-not-been-proved) is what is left,
> and it is now short.

## What it actually changes

Not a per-app preference, and not a copy of the setting — the real, watch-wide
`units` field in `2:/settings.json`, the same one the paired UNA phone app
edits, and the byte the kernel parses it into.

**Unlike `phone.notifications`, this setting can be read the supported way.**
`RequestSystemSettings` (`Libs/Header/SDK/Messages/CommandMessages.hpp`) carries
`bool imperialUnits`, and the simulator's own handler
(`Libs/Source/Simulator/App/KernelMessageDispatcher.cpp`) copies it straight out
of the `WatchSettings` struct. That one difference shapes the whole app:

- **Showing the units needs no raw access at all.** On a firmware this app
  refuses to write to, it still displays the truth and says the switch will not
  move. `NotifyToggle` cannot do that; its flag appears in no app-facing message,
  so a refused gate leaves it with nothing to draw.
- **A write has a second, independent witness.** After flipping the live byte,
  the app re-asks the kernel. If `imperialUnits` does not follow, the byte it
  wrote was not the units field — so it puts the byte back and reports nothing
  rather than claiming a change it cannot corroborate. `NotifyToggle`'s readback
  could only ever prove that some byte accepted a value.
- **But the units may not witness themselves in the gate.** `FirmwareGate`'s
  live-struct check still uses `dailyGoals.activityMinutes` and `steps`, fields
  this app never writes, for the reason `NotifyToggle/README.md` records: in
  live-only mode the app deliberately diverges the live value from the file, so
  anything it writes disagrees with something by design.

There is no supported *setter*. `REQUEST_SYSTEM_INFO` and
`REQUEST_SYSTEM_SETTINGS` are the only two `REQUEST_SYSTEM_*` messages in the
headers and both are reads; the only `SET` an app gets is
`RequestSetCapabilities`, whose entire surface is `enPhoneNotification`,
`enUsbChargingScreen` and `enMusicControl`. So changing the units goes the same
route `NotifyToggle` documents at length — a raw pointer into the kernel's live
struct on a part with no MPU, and a hand-driven splice of `settings.json` for
persistence. **Read that app's README before changing anything here**; it is the
design record for the mechanism, and `SettingsKit/README.md` says which parts
are shared.

### What was measured, and how

**The file, on a watch, on 2026-09-06.** Read over USB mass storage, flipped
from the phone, read again:

```
metric    245 bytes  sha256 999af4f830a8c5ddcae501c42fdc13290469ffeda826a71cf509f51b524a5aa4
imperial  247 bytes  sha256 2fb702bad14142721fe6625cad8f4404b53d4e93c1cb35f83897ca677a8628b1
```

- The two spellings are lowercase `metric` and `imperial`, the key is first in
  the object, and the value token starts at byte 9 in both. The file is
  minified — no whitespace anywhere — so the splice's whitespace tolerance is
  defensive rather than exercised by this unit.
- The delta is exactly **+2 bytes**, against the one byte a `true`/`false`
  rewrite moves. `SettingsPersist::kBufferCapacity` is sized for both.
- Every other field came back byte for byte.

The two captures are **not committed**: they are a real personal settings file,
and `.gitignore` keeps `DeviceBackups/` out of this repository for that reason.
The hashes above are what you check a fresh capture against. The host tests use
the same file *shape* with every personal value replaced by a neutral one of
identical byte length, so 245, 247 and byte 9 all still hold.

**The struct offset, from the firmware image.** `settingsStructBase + 4`, one
byte, `0` = metric. Derived in
[`SettingsKit/Docs/2026-09-06-units-offset.md`](../SettingsKit/Docs/2026-09-06-units-offset.md)
from the settings parser at `0x080abcd6` rather than the constructor — metric is
the default, so the constructor's `memset` is the only thing that writes it and
there is no store to find. Both match branches converge on
`strb r0, [r4, #4]`. The firmware's own comparison table gives the two spellings
lengths 6 and 8, which is the same +2 delta arrived at from the other direction.

Only one derivation from the image, and the standard in this repository is two.
**The second is the gate, and it ran on the watch on 2026-09-07.** The app
compares its raw read against `RequestSystemSettings` at launch and after every
write, and refuses if they disagree. They agreed on every read across three app
launches, and — the part that matters — the kernel's own report *followed the
write* each time:

```
LiveSettings: unitsImperial raw=0x01 (addr=0x20010CB4) watchFaceId=0
witness: raw=1 kernel=1 (agree)
R1 pressed
LiveSettings: writing unitsImperial raw=0x00 to addr=0x20010CB4
LiveSettings: readback raw=0x00
witness: raw=0 kernel=0 (agree)
```

`0x20010CB4` is `settingsStructBase + 4`. A byte that merely accepted a write
would have left the message reporting the old value; this one moved the setting.
Three presses, both directions, and the gate's own cross-check on fields this app
never writes agreed too (`activityMinutes` 30/30, `steps` 5000/5000, all nine
signatures matched). Falsified by a run where the message does not follow the
write. The full log is in `DeviceBackups/2026-09-07-first-run/`, which is
local-only.

That run also confirms the default is inert: with no config file on the watch,
`saveToSettings` read false, the log says `saving is off, leaving settings.json
alone`, and `2:/settings.json` came back byte-identical to the capture taken the
previous day — with no scratch file of any kind left on the volume.

**The commit, with saving switched on.** Same day, same unit, a
`unit_config.json` carrying `saveToSettings: true`. `validatePrimitives` ran for
the first time and all fourteen checks passed — including `rename onto an
OCCUPIED name -> 0`, which is the FatFs behaviour the whole move-aside dance
exists for. Then:

```
persist: read 247 bytes, hash=0x9BA64962
persist: spliced at offset 9, now 245 bytes, hash=0xAAD0F819
write: open(2:/settings.json.uttmp, CREATE) -> 1
write: write(245) -> 1 bytesWritten=245
commit: exists(real) -> 1
commit: exists(prev) -> 0
commit: rename(real -> prev) -> 1
commit: rename(tmp -> real) -> 1
commit: delete(prev) after success -> 1
persist: readback 245 bytes, hash=0xAAD0F819
persist: verified OK -- the file on flash is what we wrote
```

Checked afterwards over USB, against the capture taken minutes before:

- The file is **245 bytes and byte-identical to the metric file read off this
  watch on 2026-09-06**, before any of this code existed. Not merely valid JSON:
  the same bytes the firmware itself writes.
- Only the `units` token differs from the pre-write file. Height, weight,
  gender, date of birth and all six heart-rate zones came back exactly.
- No `.uttmp`, `.utprev` or probe file was left anywhere on the volume. Those
  were the scratch names at the time; they are shared and shorter now, for the
  reason [`SettingsKit/README.md`](../SettingsKit/README.md) gives, and the log
  above is quoted as it was written.
- The firmware's own `settings.json.bak` is byte-identical to before — untouched,
  which is the one thing this app must never spend.
- `0xAAD0F819` is the hash [`NotifyToggle`](../NotifyToggle) independently
  recorded for this file's metric state on 2026-09-05, and it is what this Mac
  computes for the file now. Two apps, two sessions, one number.

The next launch read `metric` from the live struct, which is the kernel having
reloaded the written file. Falsified by a run where the readback hash differs
from the spliced hash, or where a scratch file survives a successful commit.

**The file passes through zero bytes mid-write.** In the same session,
`2:/settings.json` was observed at 0 bytes with a null FAT timestamp while a
change from the phone was in flight; a later read returned the completed
247-byte file, and nothing had faulted. `SettingsPersist` already refuses a
reported size of 0, and its recovery keys on a file *existing* rather than
having length, so that window cannot be mistaken for an interrupted commit.
Two things this did not establish: how long the window lasts, and whether the
truncation comes from the phone's BLE overwrite or the watch's own write.

## Screen and buttons

One screen: the word `UNITS`, a two-position control naming both choices, and a
button hint. A pill switch reading ON/OFF would be the wrong metaphor — neither
unit is "off" — so the control is segmented and both sides are always legible.

`METRIC` is 67 px and `IMPERIAL` 80 px in Poppins SemiBold 18, so equal halves
of 102 px leave the wider label 22 px of padding; the control spans x 18..222,
and the round mask's chord at its corner rows is 233 px. The footer is 118 px at
Regular 12 against a 129 px chord at row 220. Measured with `textkit`'s
`measure` example, not estimated.

The launcher icon names the two units rather than drawing one. Pictures of
measuring do not survive 30 px: a dual-scale ruler reads as a fence, a notched
bar reads as piano keys, and a plain baseline-and-ticks ruler — which does read
at 60 px — collapses into a letter W. `Resources/make_icon.py` records what was
tried, and its ink census is what checks the type survives the panel's four
levels a channel: at 30 px the caps stand 7 and 8 rows with strokes 1–3 px
solid.

| What happened | What it draws |
|---|---|
| Read, and saved | White outline, the set half filled, no status line |
| Saving is switched off (the default) | The same, footer `REVERTS ON REBOOT` |
| Changed, but the file was not written | Amber outline, the set half still filled, `NOT SAVED`, footer `REVERTS ON REBOOT` |
| Firmware refused, units still readable | Grey outline, the set half filled, `VIEW ONLY`, footer `NEEDS WATCH 1.4.0` |
| The units could not be read at all | No control, `UNKNOWN` |

The fourth row is the one that differs from `NotifyToggle`, which draws no
switch at all on a firmware it cannot write to. Here the units are true whatever
the gate decided, so hiding them would be the lie; the control is drawn and
marked read-only instead. The last row draws no control because there is nothing
to show — inventing one of two answers is worse than admitting neither.

Every way a save can fail draws `NOT SAVED`, which is what all of them mean to
the wearer: the change is real now and gone at the next reboot. The firmware
gate never opens a file — it is a flash read and a message — so a gate refusal
says nothing about `settings.json` and does not claim to.

Whatever a press leaves behind is sticky until the next press. A failed press
is invisible to a fresh read — the revert puts the byte back, so the next read
agrees again — and without that stickiness the screen would return to a
confident answer about a change that did not happen.

A change that took effect live but never reached `settings.json` is real right
now and gone at the next reboot, and must not draw the same confident white as
one that saved; `not_saved_is_visually_distinct_from_a_saved_choice` is what
holds that line, and `unsupported_is_visually_distinct_from_a_changeable_choice`
holds the other.

| Button | Does |
|---|---|
| R1 (`SW2`) | Switch. Flips the live value, confirms it against the kernel's own report, writes `settings.json`, and re-reads. Does nothing on a firmware the gate refused. |
| R2 (`SW4`) | Back — exits the app. |

Closing the app changes nothing: the watch-wide setting stays where R1 left it.
Whether it survives a reboot depends on the write having succeeded, which is why
the screen says so either way.

## What has not been proved

The offset, the live write and the commit are settled (above). These are not.

- **That the app's own recovery ever runs on hardware.** Tested on 2026-09-07 by
  renaming `2:/settings.json` away by hand and rebooting, and the answer is that
  it does not get the chance: kernel 1.4.0 rewrites a missing settings file, and
  its own `.bak`, at boot with every personal field intact. The app then found
  the file present and did nothing — the debug log has no `recover:` line at all.
  So the recovery is a backstop for the narrower case where the firmware's backup
  is gone too, and that case is untested; so is a real power loss inside the
  two-rename window, which is the only way to produce a strand for real. What the
  test did settle is that an interrupted commit is not the data-loss event this
  app assumed. [`SettingsKit/README.md`](../SettingsKit/README.md) records what
  it costs instead.
- **That the gate ever refuses.** It accepted on the only firmware it has seen. A
  gate that has never said no is a gate whose refusal path is untested.
- **What the change actually does to the rest of the watch, and when.** This is
  the open question that most affects what the screen should say.
  `phone.notifications` is a behaviour the kernel acts on continuously, so
  "immediate" was provable. Units is a *display* setting, and apps read it once
  at start through `RequestSystemSettings`. Whether the watch face repaints,
  whether an activity app started afterwards picks it up, whether one already
  running does, and whether the native summary screens do — all unknown. **The
  screen deliberately claims none of it.** It says what the setting is and
  whether it will last, and nothing about what else will change.

Saving stays a decision made in the companion app rather than a default,
because the commit is the only part of this that can cost a wearer anything.

## Building

Targets **`apps-v1.4.0`**, the same as `NotifyToggle`, `RustGuiPoc`, `QrGuiPoc`
and `Spin`.

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/UnitToggle-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 .
cmake --build build
```

The `.uapp` lands in `Output/`; deploy it per the SDK's `Docs/deploy.md`.

`-DUNIT_TOGGLE_DEBUG_LOG=ON` adds diagnostic logging to a file in the app's own
directory on the watch. It is off by default and belongs off in anything a
wearer installs; it never logs file contents, because `settings.json` holds
height, weight, gender and date of birth.

### Footprint

From a real build against the pinned toolchain image and SDK revision
(`arm-none-eabi-size -A`, 600 KiB GUI RAM window, code executing from RAM):

```
GUI      .text 49,512   .data 572   .bss 58,536   .stack 10,240   .uapp 57,820
Service  .text  2,180   .data  36   .bss    556   .stack 10,240
```

`.bss` is mostly the 57,600-byte framebuffer, as on every CustomGUI app here.
`SDK::AppConfig` and the JSON reader it needs are most of the difference between
this app's `.text` and something with no declared setting — the price of the
saving switch being something the companion app can present and explain rather
than a build-time constant. Re-derive the numbers rather than trusting this
table.

## Tests

The splice is shared, and so are its tests — they live in
[`SettingsKit`](../SettingsKit) because both apps rewrite the same file:

```sh
cmake -B build -S ../SettingsKit/Tests && cmake --build build && ctest --test-dir build
```

The renderer's own tests run at a desk:

```sh
cd Software/Apps/CustomGUI/rust
cargo test --features std
```

`filled_half_follows_state` and `unsupported_still_shows_which_units_are_set`
are the two worth reading first: they sample specific pixels rather than diffing
whole frames, so each states the one thing that has to be true of this screen —
the filled half is the one the setting says, and a refused gate still shows it.
`the_filled_half_stays_inside_the_rounded_outline` exists because it did not:
the chosen half is a rectangle inside a rounded control, and its square corners
showed outside the curve until the corners were rounded to match. Every pixel
probe passed straight over that; it was found by looking.

The C++ half type-checks on a host without the ARM toolchain, which catches
renames across the ABI:

```sh
clang++ -std=c++17 -fsyntax-only -Wall -I"$UNA_SDK/Libs/Header" \
  -I../SettingsKit/Header -ISoftware/Libs/Header -ISoftware/Apps/CustomGUI \
  Software/Apps/CustomGUI/Gui.cpp Software/Libs/Sources/Service.cpp
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

`SPACE` or `ENTER` switches units (matching R1), `ESC` or `BACKSPACE` quits
(matching R2). `U`, `N`, `L`, `S` and `F` preview the unreadable, not-saved,
live-only, unreadable-settings and unsupported screens, which a wrist only
reaches by something going wrong. It calls the same `unit_toggle_gui::render()`
the firmware calls, into an identical buffer, so the framebuffer matches the
device's by construction.

Where SDL is not available, the same frames can be written out and looked at:

```sh
cargo run --example dump --features std -- /tmp/frames
python3 ../../../../Tools/frames_to_png.py /tmp/frames
```
