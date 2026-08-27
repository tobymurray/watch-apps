# Backlight PWM: dimming the light the kernel will not dim

A `Utility` app that modulates the watch's front-light directly, to prove that
the hardware can be dimmed even though the firmware never does.

This is the other half of [Backlight Probe](../BacklightProbe/README.md). That app
asked the kernel for six brightness levels and measured six byte-identical
register states: `brightness` is inert, and the kernel drives PF3 as a plain
on/off enable. This one drives the same pin itself, at the same six numbers, and
produces six different duty cycles.

Two sets of photographs, same screen, same six values, taken the same way. The
difference between them is the whole result.

## It writes registers, and here is the complete list

| Register | Address | Why |
| --- | --- | --- |
| `GPIOF BSRR` bit 3 | `0x42021418` | The light |
| `DEMCR` bit 24 | `0xE000EDFC` | Start the cycle counter |
| `DWT_CTRL` bit 0 | `0xE0001000` | Start the cycle counter |

That is all of it. No `MODER`, no `OTYPER`, no `PUPDR`, no `AFRL`, no `RCC`, no
flash, and nothing anywhere near the option bytes. The two debug registers are
read first and restored on the way out.

**PF3 needs no configuring**, which is what keeps this small: the 2026-08-27
investigation measured it already set up as a general purpose open drain output,
active low. The kernel has done the setup; this app only modulates.

Three properties follow, and they are what make the experiment cheap to reverse:

- **There is no state to restore.** `BSRR` is write only, reads as zero, and self
  clears. Stop writing it and the pin is exactly as the kernel left it.
- **There is no read-modify-write race.** `BSRR` sets or clears individual bits
  atomically, so a write here cannot clobber another pin on port F even if the
  kernel writes one in the same instant. `ODR` would have had that hazard for no
  benefit.
- **A crash cannot leave the hardware misconfigured.** The worst an abrupt kill
  does is leave the pin at whichever level the last write chose, which is the same
  on or off state the kernel itself drives, through the same 82R resistor on the
  same fixed 3V3 rail. The failure mode is a light left on, and the kernel's next
  backlight event clears it.

## Why software PWM and not a timer

PF3 has no timer output. ST's pin table for `STM32U5A5QJI` in UFBGA132 gives it
seven functions, and the only timer among them is `LPTIM3_IN1`, which is a
capture **input**. No `TIMx_CHy`, no `LPTIMx_OUT`. So a duty cycle on this pin has
to come from somewhere other than a compare register.

Two ways to do that, and this app takes the simpler one:

- **Software PWM**, what this app does. The CPU spins on the cycle counter and
  toggles the pin. Costs a busy-waiting thread; jitters with whatever else the
  system is doing.
- **Timer-triggered DMA into `GPIOF->BSRR`**, which is the right production
  answer. A timer's update and compare events drive a DMA channel that writes a
  half-word to `BSRR`, so the hardware generates the timing and the CPU does
  nothing per edge. Jitter-free on any pin, at the cost of a DMA channel and two
  small buffers.

The second is what a vendor should ship. The first is what proves it is worth
shipping, and it is a great deal less machinery for a first answer.

### The bursts, and why the loop returns

The obvious implementation is `while (running) { on; wait; off; wait; }`, and it
would reboot the watch: the kernel watches app threads for liveness, and a long
synchronous loop that never returns to the message queue is what tripped that
watchdog when Map Manager verified a large pack.

So `SoftPwm::runBurst()` does a bounded amount of PWM (40 ms, ten periods at
250 Hz) and returns. The service calls it again from its own poll, with a
zero-length message wait, so the gap between bursts is one pass through an empty
queue and far shorter than a PWM period. If the watch ever does reboot mid-run,
`kBurstUs` in `PwmPlan.hpp` is the number to reach for.

### The clock calibrates rather than assuming

`getTimeMs()` is the only clock the SDK offers an app, and at 250 Hz the edges
need placing to tens of microseconds. So this uses `DWT_CYCCNT`, which counts core
clocks.

It does **not** hardcode a core frequency. The clock tree on this unit has never
been decoded, so an assumed MHz figure would sit underneath every timing number
the app produces. Instead it counts cycles across a known `getTimeMs()` interval
at startup and derives cycles-per-microsecond from what the part is actually
doing. The screen shows the result.

If the counter will not run, the app **refuses to drive anything** and says so.
A PWM timed off a dead counter would produce a waveform nobody could vouch for,
and it would look like a result rather than like a failure.

## Contesting the pin with the kernel

The kernel owns PF3 too. Before touching it, this app sends a normal
`RequestBacklightSet(100, 10 minutes)` so the kernel's own state machine believes
the backlight is on and is not trying to turn it off during the run.

It can still reassert itself: a wrist raise, an idle timeout, a notification, its
own auto-off. When it does, its write lands between two of this app's writes and
is overridden within one PWM period, four milliseconds. **That contest is the
experiment.** A run where the light visibly survives an auto-off that should have
killed it is the finding, not a bug.

Phone notifications are disabled for the run, so at least one source of
kernel-initiated backlight events is out of the way.

### Giving the pin back

Three paths, all ending the same way: duty 0, then a `RequestBacklightSet(0, 0)`
to resync the kernel's view with the pin's actual state.

- The ladder finishes.
- **R1 while running** stops it immediately. Play and stop share a button
  deliberately: giving the pin back is the control that matters mid-run, so it
  goes under the thumb that is already there rather than on a second button
  someone would have to find while the light is doing something unexpected.
- `COMMAND_APP_STOP`, which USB insertion causes without warning.

Leaving the screen with R2 does **not** stop the run. The kernel blanking the
display is one of the events that might make it reassert the pin, and watching
that happen is part of the point.

## Running it

**The run cannot happen with USB connected.** Plugging in stops every app.

1. **Turn off BLE phone-sync** before any USB session, or concurrent sync and host
   writes corrupt the exFAT partition.
2. **Unplug USB.** Battery, or the dev tool, which powers the watch without acting
   as a host.
3. **Get a camera on it**, ideally with fixed exposure, or a light meter at a
   fixed distance. Take the shots the same way you took Backlight Probe's, or the
   comparison is not a comparison.
4. **Launch BL PWM and press play (R1).** About 40 seconds. Each rung shows the
   requested duty large, and the achieved duty next to it.
5. **R1 again** at any point hands the pin straight back.

The screen goes white while driving, for the same reason Backlight Probe's does:
the panel is reflective, so what a meter sees is the light multiplied by what the
framebuffer reflects, and metering a dark screen measures the light through a
near-zero coefficient. A refused run stays dark, so it cannot be mistaken at a
glance for a real one.

## Reading the result

Put the two sets of photographs side by side. Backlight Probe's six are one
brightness; this app's six should be a ladder. If they are, the conclusion is
that `brightness` describes something the board can already do and the firmware
does not, and the vendor ask is one sentence long: honour the field at whatever
granularity is convenient. No board change, no ABI change, no new message.

Watch the **achieved** duty as well as the requested one. It is computed from
microseconds actually spent with the light on, not from the request, so the gap
between the two is the honest cost of a busy-wait PWM sharing a thread with a
message loop. A large gap is an argument for the DMA approach rather than against
dimming.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0
cd BacklightPwm/Tests
cmake -B build -G "Unix Makefiles" . && cmake --build build
./build/backlightpwm-tests
```

`SoftPwm` takes its pin and its clock as interfaces, which is what makes any of
this testable off hardware: a fake clock advances deterministically and a fake pin
records the waveform, so the tests measure the duty that came out rather than
trusting the engine's own arithmetic about it.

Covered: that the requested duty is what appears on the pin; that the six rungs
produce six monotonically dimmer waveforms; that full brightness is held rather
than toggled (a toggle at 100 percent would show any transition glitch as a
dimming at full brightness, exactly the artefact that would discredit the result);
that a burst respects its budget and ends on a period boundary; that the clock
wrapping mid-burst neither stalls nor truncates it. The plan tests assert the
experiment rather than the code, the load-bearing one being that the rungs still
match the six values Backlight Probe sent.

Mutation-checked: ignoring the duty, toggling at full brightness, and overrunning
the burst budget each fail a test.

**Not** covered, and not pretended to be: that `0x42021418` is the backlight
(`PwmPin` is compiled out entirely off ARM, so nothing here writes a register or
could tell you the address was wrong), that the cycle counter runs, what the core
clock is, and what the kernel does when both it and this app write the pin. That
last one is the research question and it exists only on the watch.

`Service` has no test target: it blocks on the kernel message queue and never
returns.

## Building

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0    # not mainline
cd BacklightPwm/Software/Apps/BacklightPwm-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=1.0.0 . && cmake --build build
```

**`$UNA_SDK` must point at `apps-v1.3.0`, not mainline.** The kernel interface
version is baked into the app: 1.3 is `KERNEL_INTERFACE_VERSION 2`, mainline is 3,
and the watch runs the 1.3 line. An app built against 3 exits instantly to an
`App PID` error screen, and nothing catches it at build time.

`AppID` is `8204607DE8646542` =
`sha256("https://github.com/tobymurray/watch-apps#backlightpwm")[0:8]`.

### Simulator

The simulator has no GPIOF and no cycle counter, so it sits in `NO DRIVE`
permanently and says so on screen. It exercises the ladder logic and the display
and tells you nothing whatever about the hardware.

## Deploying

```sh
udisksctl mount -b /dev/sda1
MP=$(findmnt -n -o TARGET /dev/sda1)
mkdir -p "$MP/Apps/BacklightPwm"
rm -f "$MP/Apps/BacklightPwm/"*.uapp
cp build/BL_PWM_*.uapp "$MP/Apps/BacklightPwm/"
sync
udisksctl unmount -b /dev/sda1
```

Then unplug, power-cycle, and launch.

## Credit

The register evidence that made this app possible, and the finding that PF3 is a
plain output with no timer behind it, are from the
`2026-08-26-backlight-control` investigation on `una-sdk@research`. The circuit
comes from `UNAWatch/una-hardware`, published under CC BY 4.0. The app shell
traces back through Backlight Probe and FW Dump to Map Manager and Chrono.
