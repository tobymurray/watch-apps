# Backlight Probe: what can an app actually make the light do?

A `Utility` app that runs a scripted experiment on the watch's backlight and
writes down everything observable about it: what was requested, what the message
layer reported back, and what the peripheral registers looked like at each step.

It exists because of a gap that is already measured: **`brightness` is inert.**
`REQUEST_BACKLIGHT_SET` carries a documented 0-100 field and a request for 70
percent does not produce 70 percent. The open question is not *whether* but
*where*: a kernel that reads the field and discards it, or hardware with no duty
cycle to set at all, and those lead to opposite conclusions about whether any
workaround exists.

The investigation this belongs to, its question list and its verdicts live on
`una-sdk@research` in
`Docs/Investigations/2026-08-26-backlight-control/`.

## What it produces

Everything lands in `Apps/BacklightProbe/`:

| File | What it is |
| --- | --- |
| `backlight_probe.txt` | The record: every request, its result code, and when each step ran. |
| `sweep_dark.txt`, `sweep_lit_b100.txt` and so on | One labelled register sweep per backlight state. |

The sweeps are the point. `diff sweep_dark.txt sweep_lit_b100.txt` names the pin
that drives the light and says whether it is a plain output or an alternate
function; `diff sweep_lit_b100.txt sweep_lit_b001.txt` says whether anything
downstream scales with the request. Both are in the address-labelled form the
2026-07-29 investigation used, so they diff line-for-line with `diff` and decode
with that investigation's existing Python, with no extraction step.

## The thing this app cannot do

**It cannot see the light.** There is no `isOn()` an app can reach: `IBacklight`
is declared in the SDK and no `IID` exists for it, so `queryInterface` cannot
return it, and `REQUEST_BACKLIGHT_SET` carries no state back.

So the app is only half the instrument. Every auto-off timing in Suite 2 is read
off **a video of the watch**, and the app's whole job there is to make the moment
unambiguous: during those steps the screen counts milliseconds since the request,
so the frame where the light dies carries its own timestamp.

Point a phone at the watch before pressing play. Without it, Suite 2 produces a
file full of correctly recorded requests and not one answer.

## The screen is part of the measurement

Two things about it are deliberate and neither is cosmetic.

**It goes white while the plan runs.** The panel is a reflective memory LCD with
no integrated backlight: the "backlight" is a discrete front-light shining onto
it. What a camera or a meter sees is the front-light's output multiplied by
whatever the framebuffer reflects, so metering a mostly-black screen measures the
light through a near-zero coefficient and makes a real difference look like none.
A full white field is the maximum-reflectance target, held constant across every
rung so the only thing varying between two photographs is the light itself.

**It sometimes refuses to repaint.** A repaint is a `REQUEST_DISPLAY_UPDATE` to
the same kernel that owns the backlight, and a kernel that counts display
activity as user activity would extend the very auto-off timer being measured.
That confound cannot be ruled out from an app, so the app does the next best
thing: it holds completely still during the metering steps (`HOLD`) and moves
only where a moving clock is the point (`OBSERVE`). Which mode each step ran in is
recorded in the results file next to its finding, so a reader can see where the
confound applies.

Phone notifications are disabled for the run, for the same reason: a notification
raises the backlight on the kernel's own initiative, and it would be invisible in
the results afterwards.

## The plan

Three suites, back to back, about four minutes. It is a table in `ProbePlan.cpp`,
not a sequence of calls, so it can be read, diffed between runs, and walked on a
host with no watch attached.

**Suite 1: the brightness ladder.** 0, then 100, 75, 50, 25, 10, 1, then 0
again. Each rung settles, sweeps the registers, and holds a static frame long
enough to photograph or meter.

This is not a rediscovery of the known negative. It is the **register-level**
version of it. A photograph showing no visible difference between 25 and 100 is
weak evidence; eyes are bad at this and a reflective panel's appearance depends on
ambient light more than on its front-light. Two sweeps that are byte-identical
across the whole GPIO and RCC space are strong evidence, and they say something a
photograph cannot: that nothing downstream even tried.

The dark sweep is taken first and again at the end. If those two differ, something
drifted during the run and every diff in between is suspect.

**Suite 2: what the timeout actually does.** `t` = 100 ms, 1 s, 5 s, 60 s, 0, and
`0xFFFFFFFF`, each watched against the on-screen counter. Then two interaction
tests: whether a second request cancels the first one's timer, and whether
`brightness = 0` beats a timer already running.

`t = 0` is the one worth being there for. `IBacklight`'s header and the message's
own comment both say 0 disables auto-off; the SDK simulator's mock starts a
zero-length timer and blanks within about 50 ms, the exact opposite. One of them is
wrong about the device.

**Suite 3: context.** The six unallocated interface IDs (`0x00050000` through
`0x000A0000`), and one request sent from the **GUI** process rather than the
service: `REQUEST_BACKLIGHT_SET` sits directly below a block commented "Display
control (GUI only)" and both shipped callers happen to be services, so nobody has
checked whether that comment reaches it.

Calibrate the IID expectation downwards before reading the result. The SDK
declares at least eight kernel-service-shaped interfaces with no ID (backlight,
buzzer, vibro, settings, time, mutex, semaphore, sensor data) competing for six
slots. Six nulls is the expected answer and a perfectly good one: it closes the
question rather than leaving it for somebody to redo.

**Nothing is called through a pointer the walk finds.** A non-null pointer is
logged and left alone. The vtable layout of whatever lives there is unknown, and
calling slot 0 because `IBacklight::on` is at slot 0 of `IBacklight` is how a
curiosity becomes a HardFault. The follow-up is to recover `queryInterface`'s
switch from the firmware image first.

## What it writes, and what it does not

FwDump can call itself read-only outright. This app cannot, and the difference is
worth stating rather than glossing:

- **Memory access is reads only.** Peripheral registers are read; none are
  written. Nothing here writes a register, a flash address, or an option byte;
  in particular not `FLASH_OPTR`, `FLASH_OPTKEYR` or `MPU_RNR`. Like FwDump, it
  deliberately does not walk the MPU region table, which would need a write to
  select each region.
- **Filesystem access is writes into the app's own sandbox** and nowhere else.
- **The one outward effect is `REQUEST_BACKLIGHT_SET`**: a normal app message
  that two shipped apps already send. The kernel remains the thing driving the
  hardware. This app never touches the pin.

That last point is the boundary. If the sweep ends up showing a timer channel
behind the light, driving it directly is a different category of thing and does
not belong in this app.

## The timer bases are opt-in, but they are not a gamble

Every base in the default sweep was read successfully on this unit by the
2026-07-29 investigation. The **timer** bases were originally carried as an
inference from the classic STM32 APB1/APB2 layout, and are now **confirmed**
against ST's own CMSIS device header for this part
([`cmsis_device_u5`](https://github.com/STMicroelectronics/cmsis_device_u5),
`Include/stm32u5a5xx.h`): `PERIPH_BASE_NS` is `0x40000000` and
`APB1PERIPH_BASE_NS` equals it, so `TIM3` is `0x40000400` and `TIM6` is
`0x40001000`, exactly as guessed. The same header puts `GPIOF` at `0x42021400`,
which is where this app measured it.

They stay out of the default sweep because the backlight question does not need
them, not because the addresses are doubtful: the GPIO diff already says whether
the pin is a plain output or an alternate function, which is the coarse form of
"can this hardware dim at all".

To enable them for a second run, drop an empty file named `sweep_timers.enable`
into `Apps/BacklightProbe/` over USB. A marker file rather than a parsed config:
there is one thing to decide, it has real consequences, and a flag that can be got
wrong by a typo inside JSON is worse than one that cannot be got wrong at all.

Blocks are written and flushed in order, so a run that does fault has committed
everything up to the block that killed it, and the last block named in the file
is then the one to blame. That is the only diagnostic available for a fault
nothing can catch.

## Running it

**The run cannot happen with USB connected.** Plugging in puts the watch into
charge/mass-storage mode and the kernel stops every running app.

1. **Turn off BLE phone-sync.** Concurrent watch BLE sync and host USB writes to
   the same exFAT partition corrupt files: byte-identical from the page cache,
   divergent after remount, then `Input/output error`.
2. **Unplug USB.** Run on battery, or on the dev tool, which supplies power
   without acting as a USB host.
3. **Get a camera on it.** A phone propped up facing the screen, ideally with
   fixed exposure. A light meter app at a fixed distance is better.
4. **Launch BL Probe and press play (R1).** About four minutes. It says `HOLD`
   when it wants a still frame metered, and counts milliseconds when it wants the
   light watched.
5. **Wait for `DONE`.** It also says whether the record is intact; a truncated one
   is a run to repeat.
6. **Plug in USB** and copy `Apps/BacklightProbe/` to the host.
7. **Eject and remount before checksumming** anything you copied, or you are
   verifying the page cache rather than the medium.

Leaving the screen (R2) does not stop the run: Suite 1 deliberately spends time
with the light off, and a service that died when the user looked away would
abandon the experiment halfway. Leaving for good is stopping the app.

## Reading the results

1. **`diff sweep_dark.txt sweep_lit_b100.txt`.** Whatever changed is the light.
   `ODR` says which pin; `MODER` and `AFRL`/`AFRH` say whether it is a plain
   output or an alternate function; `RCC` says which peripheral clocks are on.
   - A **timer channel** with a `CCRx`/`ARR` pair means a duty cycle exists and
     dimming is physically available.
   - A **plain output bit** in `ODR`, with no timer clocked anywhere near it,
     means the light is a binary enable, and no app-side trick produces 70
     percent from a switch.
2. **`diff sweep_lit_b100.txt sweep_lit_b001.txt`.** Identical means the field is
   dead at the register level, which is far stronger than any photograph.
   Anything different refutes the premise and changes everything downstream.
3. **`diff sweep_dark.txt sweep_dark_after.txt`.** Should be empty. If it is not,
   distrust the rest.
4. **Read the `SET` lines in `backlight_probe.txt`.** A column of `result=` that
   never varies with `brightness` is the message layer's own account of the same
   finding. `PENDING` against a non-zero `send_timeout_ms` means nothing signalled
   completion, i.e. no handler ran at all.
5. **Fill in the blank times from the video**, against the `OBSERVE` steps.

If nothing anywhere in the sweep differs between dark and lit, then either the
actuator is off-chip (the `PCA9420` PMIC, or an I2C part) or the sweep is missing
the block that matters. Both are real findings. Say which blocks were covered.

## Suite 2 results: the blanks filled in from video (2026-08-27)

One run, watched on the wrist. The on-screen counter (zeroed at each `OBSERVE`
step, not at the `SET` before it) was read against the screen's colour: the
front-light lit state reads distinctly bluer than the ambient-lit panel once
it's off, which turned out to be a cleaner signal off a single frame than raw
brightness.

| Step | Requested | Observed |
| --- | --- | --- |
| `t=100ms` | 100 ms | Dark already at the first observable frame. Near-instant. |
| `t=1s` | 1000 ms | Lit to 0.69s, dark by 0.89s (~0.96-1.16s since the `SET`, gap-corrected). Matches. |
| `t=5s` | 5000 ms | Lit to 4.84s, dark by 5.04s. Matches within ~250ms. |
| `t=60s` | 60000 ms | Lit to 59.92s, dark by 60.13s. Matches within ~400ms. |
| `t=0` | 0 (header says disabled) | **Lit for the entire 30s window.** |
| `t=MAX` (`0xFFFFFFFF`) | ~4.29B ms | Lit for the entire 30s window. |
| cancel test | arm 1s, re-arm 60s ~250ms later | Lit for the entire 20s window: no dim at the ~1s mark. |
| off test | arm 60s, then `brightness=0` | Dark from the start of the 8s window. |
| GUI-sent | 5000 ms, from the GUI process | Lit at 3.33s, dark by 7.94s. Same as a service-sent request. |

Every finite timeout fired within a few hundred milliseconds of what was
requested — no scaling bug anywhere in Suite 2. The one real finding is `t=0`:
**the header is right and the simulator is wrong.** `auto_off_ms=0` disables
auto-off on hardware, holding the light for the full 30-second window, where
the SDK simulator's mock blanks within about 50ms of the same request (see
"Suite 2" above). Anyone validating this path against the simulator alone
would conclude the opposite of what the device does.

The cancel and off tests both came back as documented: a second `SET` replaces
a running timer rather than racing it, and an explicit `brightness=0` beats
whatever timer is already running instead of waiting for it.

Full per-frame reasoning and the raw counter brackets are in
`Output/backlight_probe.txt`'s `VIDEO READINGS` block, appended by hand next to
the run it describes.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0
cd BacklightProbe/Tests
cmake -B build -G "Unix Makefiles" . && cmake --build build
./build/backlightprobe-tests
```

Two kinds of thing are covered, and the second is the more valuable.

The **runner** tests are ordinary: the cursor advances when it should, an action
happens exactly once, a wait actually waits. Worth having because the failure mode
 a step advancing before the light settled: produces a sweep that looks
perfectly well-formed and records the wrong state under the right label.

The **plan** tests assert properties of the experiment rather than of the code:
sweep labels are unique (a collision silently overwrites a file), every request
arms its completion semaphore (a zero send timeout reports `PENDING` forever), the
ladder's auto-off outlasts the metering that depends on it, every sweep gets
settle time, the ladder spans a wide enough range to see anything, and the run
leaves the backlight off. None of those break a build or throw; every one of them
quietly turns the run into a directory of files that cannot answer the question.

Two of these exist because mutation testing found them. Shortening the settle hold
to zero left every other test green; so did removing the runner's
already-acted guard, which turned out to be unreachable behind a second guard
that agreed with it by coincidence. `currentDurationMs()` was simplified so one
rule always applies rather than two agreeing accidentally, which is what made the
guard both real and testable.

What is **not** covered, and is not pretended to be: that any peripheral base
decodes (`RegisterSweep` is compiled out entirely off ARM, so nothing here reads a
register or could tell you a base was wrong), that the kernel handles
`REQUEST_BACKLIGHT_SET` at all, and that a bare relative filename resolves into
the app's own sandbox. All hardware-only claims.

`Service` has no test target: it blocks on the kernel message queue and never
returns, exactly as FwDump's does.

## Building

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0    # not mainline; see below
cd BacklightProbe/Software/Apps/BacklightProbe-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=1.0.0 . && cmake --build build
```

**`$UNA_SDK` must point at an `apps-v1.3.0` checkout, not at mainline.** The
kernel interface version is baked into the app: `apps-v1.3.0` is
`KERNEL_INTERFACE_VERSION 2`, mainline is `3`, and the watch runs the 1.3 line. An
app built against `3` exits instantly to an `App PID` error screen on a v2 kernel,
and nothing catches the mistake at build time. Same pinning
[FW Dump](../FwDump/README.md#building), Chrono and Map Manager describe.

`apps-v1.3.0` passes `-fcyclomatic-complexity`, which only ST's CubeIDE GCC
accepts; mainline `arm-none-eabi-gcc` rejects it outright. The `CMakeLists.txt`
probes for it and drops it when unsupported, the same guard the others carry.

`AppID` is `8ECF8E50561C5F7C` =
`sha256("https://github.com/tobymurray/watch-apps#backlightprobe")[0:8]`,
following the repo convention. Recompute it before changing the anchor.

### Simulator

```sh
cd BacklightProbe/Software/Apps/TouchGFX-GUI
make -f simulator/gcc/Makefile
./build/bin/simulator.out
```

**The simulator cannot answer a single question this app asks**, and it says so
on screen and at the top of the results file. There are no peripheral registers to
read, so no sweep is taken; its `queryInterface` answers only the five IDs it
implements, so the IID walk means nothing; and its backlight mock collapses
`brightness` to a boolean and signals `SUCCESS` unconditionally. What it is good
for is exercising the plan machinery, the screen and the results file: and
nothing whatever about the watch.

One trap worth recording, because it costs a segfault with no output to explain
it: **the service must not log anything before the TouchGFX HAL exists.**
`simulator/main.cpp` constructs `Service` before it calls `setupSimulator`, and
the SDK's mock logger routes `LOG_INFO` through `touchgfx_printf`, which
dereferences the HAL singleton. Everything is logged from `run()` for that reason.

## Deploying

```sh
udisksctl mount -b /dev/sda1
MP=$(findmnt -n -o TARGET /dev/sda1)
mkdir -p "$MP/Apps/BacklightProbe"
rm -f "$MP/Apps/BacklightProbe/"*.uapp
cp build/BL_Probe_*.uapp "$MP/Apps/BacklightProbe/"
sync
udisksctl unmount -b /dev/sda1
```

Then unplug, power-cycle, and launch. The watch regenerates `Apps/app_list.json`
from the `.uapp` headers on boot, so there is no manifest to edit, but it
regenerates from whatever `.uapp` files it finds, so if a rebuild changes
`APP_USER_NAME` the filename changes with it and the old file must be **deleted**,
not just overwritten. Turn off BLE phone-sync first.

## Credit

The register sweep, its address-labelled format, and the two-phase off-the-cable
workflow are all from the `2026-07-29-hardware-config-recovery` investigation on
`una-sdk@research`, by way of [FW Dump](../FwDump/README.md), whose shell this app
is built on.
