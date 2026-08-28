# MagProbe

Can this watch be a compass? Nobody knows, and this is the app that finds out.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

A `Utility` app that subscribes to `MAGNETIC_FIELD` (0x30), reports whether
there is anything behind it at all, discovers what its frames contain, decides
whether the numbers could be a magnetic field, and then computes a
tilt-compensated heading from them. Four screens, four buttons, no recording.

**It has never been run on the watch.** Every verdict below is a thing it is
built to answer, not a thing it has answered.

---

## Why this exists

`SDK::Sensor::Type::MAGNETIC_FIELD` is in the SDK's header, `Docs/SensorsLayer.md`
lists it, and the SDK's own Sensors tutorial subscribes to it and computes a
heading with `atan2f(view.f[1], view.f[0])`. None of that is evidence that a
compass works, for three separate reasons.

**The part is the least confirmed thing in the hardware inventory.** The
[hardware-config-recovery investigation](https://github.com/tobymurray/una-sdk/tree/research/Docs/Investigations/2026-07-29-hardware-config-recovery)
on `una-sdk@research` tags the magnetometer **BMM350, LIKELY, string-only**: a
device answers at I2C4/0x14 and a `CHIP_ID` read did not match, so
[SensorLab's `EXPECTED.md`](../SensorLab/Docs/EXPECTED.md) says outright that the
part may not be a BMM350 at all. Every other sensor in that table is CONFIRMED.

**A type in the enum is not a producer.** `SPO2` and `HEART_BEAT` both sit in
`SensorTypes.hpp` looking exactly like every other type, and both resolve no
driver whatsoever on real hardware: SleepLab's ledger rows S4 and S5, measured
2026-08-18. `MAGNETIC_FIELD` has never been asked the same question.

**There is no parser, so there is no documented frame.** It is one of five types
that ship none, and `Docs/SensorsLayer.md` says of them only "assume layout known
from driver docs". There are no driver docs. So the field count, the units, and
even which member of the sample union the driver fills in are all open.

And it cannot be settled anywhere but on the watch: the simulator resolves no
sensor drivers for a service at all
([SensorLab `FINDINGS.md`](../SensorLab/Docs/FINDINGS.md) section 11).

## What it does that SensorLab does not

[SensorLab](../SensorLab) asks all 37 types whether they resolve, and captures
raw frames for the parser-less ones. Run it first: its layer 1 sweep answers
"does 0x30 have a producer" for free, alongside 36 other types, and if the answer
is no then this app has nothing to measure.

What SensorLab does not do is the magnetometer-specific half, which is where a
compass is actually won or lost:

| | |
| --- | --- |
| **Units, narrowed by magnitude** | Earth's field is 22 to 67 uT everywhere on the surface, so a correct reading has to land in a known band. The bands for microtesla, gauss and raw counts do not overlap, which makes the answer a classification rather than a guess. |
| **Rotation invariance** | A magnitude in the band is weak evidence on its own: a constant offset with no field behind it can sit in the band too. A real field's magnitude barely changes as the watch turns, so the spread of the magnitude across a slow rotation is the actual test. |
| **The dip angle** | The strongest single check available without a datasheet. Inclination at a given place is known to a fraction of a degree from any geomagnetic model, and it is a property of the field rather than of the scale, so it holds whatever the units turn out to be. |
| **Hard-iron calibration** | The watch is a steel case next to a vibration motor and a battery. That offset is usually larger than Earth's field, so an uncorrected heading can be wrong by any amount. The SDK ships nothing for this: `SDK/Calibration` is stride and treadmill speed only. |
| **Tilt compensation** | A wrist is never level, and at 70 degrees of dip the vertical component is nearly three times the horizontal, so a few degrees of tilt moves the tutorial's heading by tens of degrees. |

## The screens

Four, on `L1` and `L2`. `R1` starts and stops a hard-iron sweep; `R2` on the
kernel's back gesture leaves.

| Screen | What it answers |
| --- | --- |
| `VERDICT` | Does this work. `NO COMPASS` in red, `DELIVERING` in green, `PENDING` in yellow, with the resolve state, delivery state, frame shape, unit classification and magnitude spread underneath. |
| `FRAME` | What actually arrived: field count, stride, batch count, the three axes as floats, and the first field read again as a `u32` and an `i32`, because the union does not say which member the driver filled in. |
| `COMPASS` | The needle, the heading, the eight-point cardinal, and the dip. Says why it has no heading rather than drawing one from stale numbers. |
| `CAL` | The sweep: per-axis span, the offsets those imply, and whether the sweep was good enough to use. |

**Absent, silent and stalled are three different findings.** They have three
different causes and the verdict screen keeps them apart, because reading the
second as the first is what made SleepLab report every night as NOT WORN
(ledger row S12) until two minutes of hardware time caught it.

**An unanswered question is never drawn green.** A pixel test pins that.

### Buttons

| Button | Click | Long press |
| --- | --- | --- |
| `L1` (SW1) | previous screen | flip the assumed accelerometer sign convention |
| `L2` (SW3) | next screen | dump the framebuffer to `fb_dump.bin` |
| `R1` (SW2) | start or stop the sweep | reset the calibration |

One button both ways for the sweep, because an instrument with a separate stop
has a way to leave a sweep running by accident.

The convention toggle is there because **which way the accelerometer vector
points at rest is not written down for this watch.** A part measuring specific
force reports +1 g on the axis pointing up, and RustGuiPoc's fixtures assume
that, but nobody has measured it. One press with the watch flat on a table
settles it without a rebuild.

## What is verified, and what is not

Verified by host tests, off the device:

- The heading is correct in a known frame, increases clockwise, and **does not
  move across 60 degrees of roll (plus or minus 30)** while the tutorial's `atan2f(y, x)` swings by
  more than 20 degrees on the same samples.
- Dip is recovered across the range and is independent of the unit scale.
- Hard iron recovers the centre of a swept sphere, and **refuses** an offset from
  a sweep that did not cover every axis, because an offset from a partial
  rotation is wrong and looks calibrated.
- The unit bands do not overlap.
- Non-finite and all-zero readings are distinct findings, not folded into
  "implausible".
- No number reaches the screen through floating-point `printf`.
- Nothing is drawn where the round bezel would hide it, across every verdict,
  frame, unit and calibration state.

Not verified, and only the watch can:

- **Whether `MAGNETIC_FIELD` resolves a driver.** The whole question.
- What its frame contains, how wide it is, or which union member is filled.
- What the units are.
- Whether the axes are the ones assumed here, or in that order, or with those
  signs. The heading's sign convention is right for the frame documented in
  `Heading.hpp` and that frame is a hypothesis.
- Which way the accelerometer points at rest.
- Whether the kernel honours a `Utility` app's glance and widget. Untouched here
  and still untested (SleepLab ledger row T2).

## Building

`$UNA_SDK` must point at an **`apps-v1.4.0`** checkout. An app carries the kernel
interface version it was built against and the launch check is one-directional:
build against a newer SDK than the watch's firmware and the `.uapp` packs
identically and simply does not run.

```sh
export UNA_SDK=/path/to/una-sdk
Tools/docker-build.sh app       # the .uapp
Tools/docker-build.sh tests     # host tests
Tools/docker-build.sh screens   # every screen to Output/screens/*.ppm
```

`screens` is what stands in for the simulator an app with a CustomGUI front end
does not have. It renders through the same `Render::render()` the firmware calls
and then applies what the panel does to those bytes: the two-bits-a-channel
gamut and the round bezel mask. **It is how the layout bug above was found**, and
looking at its output is worth more than reading the layout code.

## Design notes

**No TouchGFX.** The GUI half goes through the SDK's CustomGUI entry point, which
asks only for a class with a kernel constructor and a `run()`. These screens are
rows of text and one needle; the roughly 120 files of generated scaffolding a
TouchGFX front end needs would have been most of the app. Glyphs come from a 5x7
bitmap font in 320 bytes. `RustGuiPoc` is the precedent, without the Rust.

**The renderer is a pure function of a plain struct.** `Render::View` has no
kernel in it, so every screen can be driven from a host test and dumped to a
file. That seam matters more here than usual, because a screen fed by real
samples can only ever be seen on the watch.

**Bright ink on a dark ground, always.** Measured on hardware: the panel renders
bright glyphs on the dark background crisply and drops dark thin glyphs on a
light fill entirely (RustGuiPoc's README). There is no light fill colour in
`Canvas`.

**The Service sends on a timer, not per sample.** The GUI's incoming queue is ten
deep and discards the oldest (SensorLab `FINDINGS.md` section 12), and the
accelerometer delivers about 48 Hz whatever period it is asked for (SleepLab
ledger row S3). A repaint per batch would overrun the queue.

**Messages are packed and asserted.** The kernel's largest pool block is 256
bytes and a message that overflows it falls out of the pool at runtime rather
than failing to compile, so every type in `Commands.hpp` carries a
`static_assert`. SleepLab's finding.

**Nothing here writes to a sensor register.** There is no write path in this
codebase, which is a stronger guarantee than a flag.

## Provenance

The verification convention (CONFIRMED / LIKELY / UNVERIFIED / REFUTED with the
corroborating method) and the hardware inventory come from the
hardware-config-recovery investigation on `una-sdk@research`. The
absent/silent/stalled taxonomy, the message-pool limit, the delivered-rate
findings and the float-`printf` rule are [SleepLab](../SleepLab)'s and
[SensorLab](../SensorLab)'s, reproduced with attribution because those rows were
paid for with hardware time and should not be re-earned.
[RustGuiPoc](../RustGuiPoc) is the precedent for the CustomGUI entry point, the
framebuffer push, the clock-positioned liveness marker and the panel's
dark-on-light behaviour.
