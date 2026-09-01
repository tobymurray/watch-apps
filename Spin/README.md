# Spin — a stationary bike ride

A ride that goes nowhere, recorded as a ride that goes nowhere. The clock, your
heart rate and the zone it puts you in, and a FIT file whose `session` says
**`sport = cycling`, `sub_sport = indoor_cycling`**.

![The four screens: ready, riding in zone 3, paused, saved](Docs/screens.png)

The GUI is drawn by **Rust + `embedded-graphics`** through the SDK's
**CustomGUI** entry point, not TouchGFX — the arrangement
[`RustGuiPoc`](../RustGuiPoc/README.md) proved and [`Barcode`](../Barcode/README.md)
ships. Settings come from the phone through **`SDK::AppConfig`**, against the
contract in [`app-manifest.json`](app-manifest.json).

Built against **SDK 1.4.0** (`KERNEL_INTERFACE_VERSION` 3), which is where
`minKernelVersion` comes from — see [Versions](#versions).

## Why the two sport fields are the whole point

Record an indoor ride as plain `sport = cycling` and every consumer downstream
reads it as a bike ride that covered no ground: 45 minutes, 0.0 km, an average
speed of zero, and a place in your distance totals it did not earn. `sub_sport
= indoor_cycling` is the field that says the distance is absent because there
was never any, and it is the difference between a ride Strava files as an
indoor ride and one it files as a broken outdoor one.

Both values come from the SDK's own profile header, which already carries the
pair (`SDK::Fit::Sport::Cycling`, `SDK::Fit::SubSport::IndoorCycling`).

Everything else in the file, all asserted in
[`Tests/ActivityWriter_test.cpp`](Tests/ActivityWriter_test.cpp):

- **At least one lap, and they add up.** Spin has no lap button, so with
  auto-lap off the single lap is the whole ride — a session with no lap at all
  is one many FIT consumers quietly drop. With auto-lap on, the laps sum to the
  session.
- **Where each beat came from.** Every `record` carries the arbitrated
  `heart_rate` plus developer fields `hr_source`, `hr_optical` and
  `hr_external`. Which sensor the kernel believed is not recoverable from the
  arbitrated number afterwards, and on a ride whose point is the strap it is
  the first thing worth checking.
- **Time in each heart-rate zone**, on the session and on every lap, as the
  profile's native `time_in_hr_zone` — in six buckets that account for every
  active second. The two messages do not use the same field number; see
  [below](#time-in-zone-and-the-field-number-that-is-not-shared).
- **Active and resting calories**, as `total_calories` and
  `metabolic_calories`.

A `.json` summary lands beside the `.fit`, saying `"activity_type": "cycling"`.
It is auxiliary and best-effort by contract — the FIT file is what
`ActivityWriter::stop()` reports success on — but it is what the watch's own
activity list reads, so it must not disagree about the sport.

## The strap

Spin asks the kernel for an external heart-rate monitor as soon as the app
opens, not when the ride starts, so the BLE link has the pre-ride screen's
worth of time to come up. Readings arrive through the ordinary
`HEART_RATE_EX` sensor either way: the kernel arbitrates strap against wrist
optical and reports which it chose.

The heart on screen is the whole legend, and it is the only red thing in the
app:

| Heart | Means |
|---|---|
| **Red** | the beat on screen came from the chest strap |
| **White** | it came from the wrist sensor |
| **Grey**, with `---` | no reading the watch is willing to stand behind |

That last row is a deliberate third state. The kernel reports a 0–3 confidence
alongside every reading, and both the screen and the FIT file drop anything
outside 1–3 — so a strap that drops out mid-ride shows `---` rather than
holding the last number it saw, and the file records the gap instead of
inventing a beat.

**Pairing happens in the watch's own settings, not here.** Spin opts in to
whatever heart-rate monitor the firmware already knows about; it cannot scan
for or bond with a new one, and the same firmware limit is why there is no
cadence or power support — see [What it does not do](#what-it-does-not-do).

## Heart-rate zones

![Below zone 1, then zones 1, 3 and 5](Docs/screens-zones.png)

The zones are the rim. A 270-degree scale in the usual ladder — grey, blue,
green, amber, red — with the zone you are in drawn bright **and** thicker,
growing inward while the rest stay thin. Two signals rather than one, because on
a reflective panel in bad light colour alone is thin.

The panel is a circle and this is the one piece of information here that is
naturally a scale, so it gets the perimeter and the middle stays clear for the
numbers. Four levels a channel is the whole palette, so those five colours are
not approximations of nicer ones: they are the colours.

The zone is a position, not a gauge, so the segments below the current one do
not fill — a filling scale would read as progress through the zones, which is
not a thing that happens. Zone 0, below zone 1, lights nothing at all: the scale
is there, you are not on it yet.

The 90 degrees left open at the bottom is where the **target progress arc**
goes, growing left to right to meet the zone scale where it ends. With no target
set the gap stays empty, which is what keeps it reading as a speedometer rather
than a closed circle.

**The thresholds are the wearer's own**, read from the watch with
`SDK::Message::RequestSystemSettings` — the same ones every other activity app
on the watch uses. They are not a Spin setting, because a second place to
configure them is a second place for them to be wrong.

If no thresholds are set, **the bar is not drawn at all**. "Below zone 1" and
"you have not told the watch what your zones are" are different states, and an
all-dim bar would say the first when it means the second.

## Calories, and why not kJ

The estimate is the standard heart-rate-zone MET model: each zone carries a
metabolic equivalent, and energy is that times body mass times time. Body mass
comes from the watch's profile (75 kg if it has none, which scales the estimate
rather than breaking it). Below zone 1 — or with no zones set — the resting
rate is the only honest answer, so that is what is used; a MET plucked for
"some effort" would be a guess stacked on a guess. Resting energy accrues every
active second regardless, and is reported separately as
`metabolic_calories` so the two can be told apart afterwards.

It is a model, not calorimetry. It will be wrong for you by some steady factor,
and it is most useful compared against itself ride to ride.

**There is no kJ-of-work figure, and there will not be one without a power
meter.** In cycling, kJ means mechanical work, which comes from power — and
that it lands close to the kcal you burned is a coincidence of human efficiency
being about a quarter. Deriving kJ from a heart-rate calorie estimate and
labelling it kJ would put a number on screen that a rider would compare against
their power meter and find roughly four times too large, in exactly the unit
that invites the comparison. Silent, familiar and wrong is the worst of the
three.

What **is** offered is kJ as a unit for the same dietary energy — 1 kcal =
4.184 kJ, the unit food labels use across much of the world. That is a
relabelling of the estimate, not a different measurement, and the `Energy in
kJ` setting says so. **The FIT file always records kcal**, because that is the
unit `total_calories` is defined in.

## Settings

Written by the phone into the app's own directory as `app_config.json`, read
through `SDK::AppConfig`, and declared in
[`app-manifest.json`](app-manifest.json). Re-read at the start of every ride, so
a change takes effect on the next ride rather than the next reinstall.

| Setting | Default | Does |
|---|---|---|
| `autoLapMinutes` | 0 (off) | Split the ride into laps this many minutes apart, with a buzz at each. Measured on active time, so a paused ride does not come back to an immediate lap. |
| `targetMinutes` | 0 (off) | Buzz once at this many minutes and say `TARGET MET` on the screen. |
| `keepScreenLit` | off | Hold the backlight on for the whole ride. |
| `energyInKilojoules` | off | Show energy as kJ instead of kcal. Display only. |

![A target set, the target met, and energy in kJ](Docs/screens-config.png)

Both integer settings use 0 as "off" rather than carrying a separate toggle:
there is no useful reading of "auto-lap every 0 minutes", so the value can
carry the switch and the form stays four rows.

`keepScreenLit` exists because of a real problem rather than for completeness:
with your hands on the bars the wrist-tilt gesture almost never fires, so in a
dark room the clock is unreadable for the whole ride. It is off by default
because the panel is reflective and a lit gym needs no front light, so most of
the time it would cost battery for nothing.

The two buzzes are deliberately different — two short beeps for a lap, three
longer ones for the target. A lap is a marker you can ignore; the target is the
thing you were riding for, and they have to be tellable apart without looking
down.

**The binary carries its own copy of the contract**, in
[`AppConfigFields.cpp`](Software/Libs/Sources/AppConfigFields.cpp), because
`app-manifest.json` never reaches the watch. CI checks the two agree
(`validate_app_config.py --check-bounds`), which is what makes it safe for
`SDK::AppConfig` to clamp a value it should never have received.

## Screens and buttons

R1 is the primary action on every screen, so it is the only button you have to
find from the saddle. R2 leaves — but never while the clock is running, where
an accidental exit would cost the ride.

| Screen | Shows | L1 | L2 | R1 | R2 |
|---|---|---|---|---|---|
| Ready | strap status, target if set | | | **START** | exit |
| Riding | clock, heart rate, zone | | | **PAUSE** | |
| Paused | dimmed clock, `PAUSED` | **FINISH** | **DISCARD** (hold) | **RESUME** | |
| Saved / discarded | what happened | | | **DONE** | **DONE** |

The two endings of a ride sit at opposite corners on purpose. Finish is the
button you reach for every time; discard destroys the ride, and it should not be
the neighbour of the one you press without looking.

The clock picks the largest of three faces that fits, so `12:34` gets the big
one and `10:00:00` is not clipped, and it drops to `M:SS` under an hour rather
than padding an hour field nobody is riding into.

Edge cases, all of them scenes in the preview binary — no strap, a mid-ride
dropout, past the hour, and a save that failed:

![No strap, dropout, past the hour, failed save](Docs/screens-edge-cases.png)

`NOT SAVED` is not a hedge. `ActivityWriter::stop()` returns true only once the
`.fit` is flushed and closed, and that boolean is what the screen reports — so
"SAVED" is a fact about the filesystem, and its absence is worth an amber word.

## Throwing a ride away

![Holding, held, and the acknowledgement](Docs/screens-discard.png)

Discard is **held, not tapped** — the same gate the SDK's own activity apps put
on it, because it is the one action in the app that destroys data. Press and
hold L2 on the paused screen and a red ring fills; let go before it is full and
nothing happens.

**The kernel times the hold, not the app.** The countdown fires on the kernel's
own `HOLD_1S` button event, and the filling ring is drawn from the GUI tick
count purely as a picture. If the two ever disagree the ring sits full for a
moment before the event lands — a cosmetic error rather than a ride thrown away
a moment early.

`ActivityWriter::discard()` removes the part-written `.fit` **and the recovery
marker with it**, so the next boot does not finalise a ride that was deliberately
thrown away. There is a test for exactly that.

`DISCARDED` is not the same screen as `NOT SAVED`. One is what the wearer asked
for and the other is a failure, and the screen should agree with them rather
than apologise for something they chose.

## How the two halves fit together

A GUI process cannot reach a sensor: `SDK/Kernel/Kernel.hpp` defines what a GUI
process is handed and `SDK::Sensor::Connection` is not in it. So the clock, the
heart rate, the zones, the energy and the file all live in the Service, and the
GUI draws snapshots:

```
HEART_RATE_EX ─┐
   the clock ──┤→ Service.cpp ──CustomMessage──→ Gui.cpp ──spin_gui_frame──→ lib.rs
 app_config ───┤                                                             (pixels)
               └→ ActivityWriter → .fit
```

Three properties that arrangement buys:

- **The GUI owns no timer.** The number on the screen and the number in the FIT
  file are the same measurement of the same second, not two counters that agree
  until one of them drifts.
- **The Service owns every derived fact**, including whether the target has
  been passed. The screen shows the same flag that fired the buzz, so it can
  never announce the target a second before or after the wrist felt it.
- **`render()` is a pure function of one `Frame`.** The watch, the host preview
  and the unit tests all call it the same way, so a screenshot taken on a
  laptop is evidence about the watch.

The C↔Rust struct is checked in both directions: `spin_gui.h` and `lib.rs` each
compute an FNV-1a fingerprint over the layout their own compiler produced, and
`Gui::run()` refuses to start if they disagree. A stale `libspin_gui.a` linked
against a changed struct is otherwise silent until it draws garbage.

## Time in zone, and the field number that is not shared

`time_in_hr_zone` is not in `SDK/Fit/FitProfile.hpp`, which carries only the
fields UNA's own apps write, so its number had to come from the FIT profile
itself. It is worth spelling out what that lookup found, because the obvious
assumption is wrong:

| Message | `time_in_hr_zone` |
|---|---|
| `lap` | field **57** |
| `session` | field **65** |

**Session field 57 is `avg_temperature`** — a `sint8` in degrees Celsius. Using
the lap number on the session would have declared a temperature field as a
six-element `uint32` array and had every decoder report nonsense for it,
silently, in every file this app ever wrote. The app measures no temperature, so
nothing would have looked wrong from here.

Both numbers were checked against the Garmin FIT SDK profile (21.214.0Release)
and against python-fitparse's independently generated copy, which agree. The
scale is 1000, so the file holds milliseconds and
`ActivityWriter::writeZoneSeconds()` multiplies on the way in.

**Lap resting calories stays a developer field**, and now for a checked reason
rather than a cautious one: the FIT profile has no `lap.resting_calories`. `lap`
has `total_calories` (11) and `total_fat_calories` (12) and nothing else in the
family, so there is nothing to promote it to. Its sibling on the session,
`metabolic_calories` (196), does exist and is used natively.

A developer field is self-describing — it carries its own name, units and base
type in the file — which is what makes it a safe home for a quantity the
profile has no slot for.

## What it does not do

Deliberately, and the first two are firmware limits rather than choices:

- **No cadence and no power.** Both need a BLE sensor the watch would have to
  pair with itself, and an app can only opt in to accessory kinds the firmware
  supports. Today that is heart rate.
- **No broadcasting heart rate** to a trainer or to Zwift, for the same reason:
  that needs the BLE peripheral role.
- **No distance or speed.** There is no honest way to produce either without a
  trainer, and a fabricated distance is worse than an absent one. The FIT file
  omits both rather than writing zeros.
- **No structured intervals.** Reachable with what is already here — the SDK's
  profile even carries `Workout` and `WorkoutStep` — and the obvious next thing
  to build.

## Versions

Spin is built against **SDK 1.4.0**, whose `KERNEL_INTERFACE_VERSION` is 3. An
app carries the interface version it was built against, and the on-device check
in `AppSystem/system.cpp` refuses to launch when the running kernel's version is
*lower* — so building against a newer SDK than the watch's firmware produces an
app that packs identically and simply does not run.

`minKernelVersion` in the manifest is the floor that ABI implies, which
`min_kernel_version.py` derives from the SDK's own header. CI checks it on every
build, so the manifest cannot drift from the SDK the binary was actually
compiled against.

## Building

Needs `$UNA_SDK` pointing at an `apps-v1.4.0` checkout, and `cargo` with the
`thumbv8m.main-none-eabihf` target installed:

```sh
rustup target add thumbv8m.main-none-eabihf
export UNA_SDK=/path/to/una-sdk-apps-v1.4.0
cd Spin/Software/Apps/Spin-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

CMake always invokes cargo and lets it decide whether to rebuild, rather than
restating cargo's own inputs in an `OUTPUT`/`DEPENDS` rule — anything such a
rule missed would silently link a stale archive against a changed ABI. See
[`RustGuiPoc/Docs/FINDINGS.md`](../RustGuiPoc/Docs/FINDINGS.md), "Let cargo
decide when to rebuild".

CI builds it exactly the way it builds `Barcode`: one caller
([`spin.yml`](../.github/workflows/spin.yml)) naming the directory, and the
shared [`app-build.yml`](../.github/workflows/app-build.yml) discovering the
rest — the manifest, the field table, the host tests, the Rust crate and the
CMake project — from what the directory holds.

## Tests

Two suites, because they cover two different things.

**What gets written** — [`Tests/`](Tests), host C++. Encodes a whole ride with
the real `ActivityWriter` against the SDK's in-memory filesystem and decodes it
again with the SDK's independent test FIT reader, then asserts the sport pair,
the CRC, the laps, the per-record heart-rate fields, time-in-zone and the
calorie fields:

```sh
export UNA_SDK=/path/to/una-sdk
cmake -B build-tests -S Spin/Tests && cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

**What gets drawn** — Rust unit tests inside the crate, next to the code they
cover. `--features std` is required: the crate is `no_std` for the watch, and a
test binary cannot link a crate whose panics do not unwind.

```sh
cd Spin/Software/Apps/CustomGUI/rust
cargo test --features std
```

Among them, `nothing_is_drawn_outside_the_bezel` renders every scene and
asserts no lit pixel falls outside the circle. The panel is round and the
framebuffer is square, so a label inset from the buffer's edge rather than the
glass's looks fine in a square simulator and loses its last glyph on the watch
— which is exactly what the first version of the button hints did.

## Looking at it

`preview` writes one PNG per scene and needs nothing but a PNG encoder, so it
runs anywhere, including a machine with no SDL2 — the screenshots above came
out of it. It blacks out everything outside the round bezel, which the device
simulator does not.

```sh
cd Spin/Software/Apps/CustomGUI/rust
cargo run --features preview --bin preview -- /tmp/spin      # PNGs
cargo run --features sim --bin sim                           # a window; TAB cycles
```

The scene list lives in [`src/scenes.rs`](Software/Apps/CustomGUI/rust/src/scenes.rs)
and is shared by both binaries and the tests, so "every screen still draws" is
checked against the same list a human reviews.

## The icons

Two of them, with different rules, both drawn by script rather than committed
as art nobody can regenerate:

- [`make_icon.py`](Resources/make_icon.py) draws the watch icon at 30 and 60 px.
  The watch stores it as ABGR2222 — four alpha levels, four shades — so the
  flywheel's rim is drawn at 8x and downsampled, which is the difference
  between a circle and a polygon.
- [`make_store_icon.py`](Resources/make_store_icon.py) draws the 512 px icon the
  phone shows. None of the watch's constraints apply, so it gets the round
  panel, the flywheel and the app's one red heart.
