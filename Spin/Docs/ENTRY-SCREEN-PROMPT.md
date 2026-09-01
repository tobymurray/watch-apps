# Implement post-ride data entry for Spin

You are implementing a new feature in the **Spin** app of the `watch-apps`
repository — a stationary-bike activity app for the UNA Watch. Read this whole
document before writing code. It is dense on purpose: most of it is scar
tissue from bugs that already shipped to a real wrist, and repeating any of
them is the main risk of this task.

---

## 1. What to build

After a ride ends, before the FIT file is written, offer the wearer a screen to
enter the **kilojoules of work their bike's console reports**. Write that into
the FIT file as native fields, and derive average power from it.

**Why kJ and not something else.** Spin measures *strain* (heart rate) but not
*work*. Those are different, and the gap is why the app cannot currently show
anyone getting fitter: heart rate alone is confounded by sleep, heat, caffeine
and stress, so "average 142 bpm again" says nothing about fitness. Work is the
missing half. `kJ ÷ duration = average power`, and `average power ÷ average HR`
is Efficiency Factor — rising EF at constant HR is precisely what getting
fitter looks like.

The longitudinal analysis is **not** your job and must not be built on the
watch. `total_work` and `avg_power` are native FIT fields and the app's
manifest already sets `stravaExport: true`, so Intervals.icu and TrainingPeaks
compute the trend for free once the numbers are in the file. Do not build a
training log on a 240×240 panel.

**Scope.** Entry of kJ only. RPE is a likely follow-up and the screen should
not make it hard, but do not implement RPE now — see §9.

---

## 2. The hardware, and what it forbids

### The panel

- **240 × 240, round.** Sharp LS012B7DD06, reflective memory-in-pixel. It is a
  circle inscribed in a square framebuffer. The corners of the buffer are not
  visible — they are behind the bezel.
- **ABGR2222: two bits per channel.** Four levels per channel and four alpha
  levels, nothing else. Each channel truncates to its top two bits, so the only
  values that survive are **0, 85, 170, 255**. A colour you pick as `#C8C8C8`
  becomes `#AAAAAA`. Design in those four levels or be surprised on the glass.
- **Reflective, not emissive.** It is legible in a bright room and dim in a
  dark one; there is a backlight but it is off unless the wrist-tilt gesture
  fires. On a spin bike the hands are on the bars and that gesture rarely
  fires. Assume the screen is being read in whatever light the room has.
- **No touch.** None. There is no digitiser. Every interaction is a button.

### The buttons — all four of them

From `SDK/Messages/CommandMessages.hpp`:

```
UP      Top Left        L1      SW1 = 0
SELECT  Top Right       R1      SW2 = 1
DOWN    Bottom Left     L2      SW3 = 2
BACK    Bottom Right    R2      SW4 = 3
```

The event enum offers `PRESS, RELEASE, CLICK, LONG_PRESS, HOLD_1S, HOLD_5S,
HOLD_10S`.

**You may only use `CLICK`.** This is the single most important constraint in
this document, and it is not a style preference — see §6.1. There is no
press-and-hold, and therefore **no auto-repeat**.

---

## 3. The input problem you actually have to solve

A spin-class console reports something in the range of roughly **100–800 kJ**.
You must let a wearer enter a three-digit number using **four buttons, click
events only, no auto-repeat, on a round screen they are looking at while
sweaty and out of breath.**

Clicking a `+10` button forty-five times is not an acceptable answer. Neither
is a design that takes more than a few seconds, because the alternative — not
bothering — is always available and must stay pleasant (§5).

Some directions worth weighing. You are not required to pick from this list,
but you must justify whatever you choose:

- **Digit-by-digit.** One button moves the cursor between hundreds/tens/units,
  another increments the selected digit 0→9→0. Three digits, bounded worst
  case, and the wearer can see exactly what they are changing. Costs a cursor
  affordance on screen.
- **Coarse-then-fine.** One button cycles the step size (±100 / ±10 / ±1),
  another applies it. Fewer on-screen elements, but the step size is modal
  state the wearer has to track.
- **Accelerating repeat on successive clicks.** Rapid consecutive clicks grow
  the step. Feels fast, but it is timing-dependent and therefore hard to test
  and easy to get wrong on a wrist.

Whatever you choose:

- The **starting value matters**. Starting at 0 makes every entry maximal.
  Consider seeding from something already known — the app's own kcal estimate
  is roughly 1:1 with kJ for cycling (gross efficiency ~21–25%), which puts the
  wearer near the answer before they touch a button. If you do this, say so on
  screen; a pre-filled number the wearer does not understand is worse than a
  blank one.
- **Four buttons is the hard ceiling** and one of them must remain "I don't
  care" (§5). Budget accordingly.

---

## 4. Architecture: two processes, and which one can do what

Spin is a **Service** (C++, `Spin/Software/Libs/`) plus a **GUI**
(`Spin/Software/Apps/CustomGUI/`, C++ shell around a Rust `no_std` renderer).

- The **Service** owns the clock, the sensors, the app config and the
  filesystem. It writes the FIT file.
- The **GUI** owns none of those. It **cannot reach a sensor and cannot write a
  file.** It draws the last snapshot it was handed and sends button presses
  back.

So the entered value has to travel **GUI → Service**, and the Service writes
it. The protocol is `Spin/Software/Libs/Header/Commands.hpp` — currently nine
messages, five Service→GUI and four GUI→Service.

### The sequencing decision, which you should get right first

The FIT file is finalised when the Service handles `TRACK_STOP`. So the entry
screen **must come before `TRACK_STOP` is sent**, and the entered value should
ride along on that message. `TrackStop` already carries a `bool discard`;
adding the work value beside it is a small, safe extension and keeps "the ride
ended" a single atomic event.

Do **not** write the file and then reopen, amend, or rewrite it. The writer has
a durability contract (`ActivityWriter::stop()` reports `ok`, and the GUI tells
the wearer the ride was saved on the strength of it) and a crash-recovery path
built on `SDK::Fit::RecordingMarker`. Rewriting a closed file puts both at
risk for no benefit.

Note that the crash-recovery path produces a file with **no** entry screen
having run. That is fine and must stay fine: absent work is a normal state, not
an error (§5).

### The ABI fingerprint — read this before touching the frame struct

`spin_gui.h` defines a single `spin_gui_frame` struct passed to the Rust
renderer. It is guarded three ways, and all three must be updated together:

1. `static_assert`s on `sizeof`, `alignof`, and **every field's `offsetof`**.
2. A `constexpr` FNV-1a `fingerprint()` over those same offsets in C++.
3. The **same walk in the same order** in `abi_fingerprint()` in `lib.rs`.

They are checked against each other at GUI startup and the app exits on
mismatch. This exists because a stale `libspin_gui.a` linked against a changed
struct is otherwise silent until it draws garbage. If you add a field to the
frame, update all three or the app will refuse to start — which is the correct
direction for it to fail in, so do not "fix" it by loosening the check.

---

## 5. Skipping must be a first-class outcome, not an escape hatch

Most rides will not have a number entered. The wearer may not care, may be in a
hurry, may be on a bike with no console. **A ride with no work entered is a
completely normal ride** and every part of the system must treat it that way.

Requirements:

- **Skipping is one click, always visible, and labelled.** Not a timeout, not a
  back-button that looks like an error, not a hidden gesture. A wearer who
  never wants this must be able to end a ride as fast as they can today.
- **Nothing apologises.** No "no data", no empty-state warning, no dash where a
  number should be. Compare the existing discard path, which deliberately
  distinguishes "the wearer threw this away" from "this failed to save" so the
  screen never apologises for something that was asked for. Same principle.
- **The FIT file omits the fields entirely.** Do not write
  `total_work = 0` or `avg_power = 0`. Zero is a *measurement* meaning the
  wearer did no work; absent means nobody said. Any downstream platform will
  average a zero into your season. Omit the field.
- **The save path is unchanged when skipped.** Same durability contract, same
  `RideSaved` message, same SAVED screen.
- **Consider whether the screen appears at all.** A config field to turn the
  prompt off entirely is worth considering — the app already has 13 config
  fields and the machinery is cheap (§8). A wearer who never enters kJ should
  be able to stop being asked. Use your judgement; if you add one, default it
  to showing the prompt.

---

## 6. Bugs we have already shipped. Do not re-ship them.

### 6.1 The long-press that never arrived, and the screen with no exit

Discard was originally a press-and-hold on L2, matching the SDK's own activity
apps. **On the watch it never fired once.** Spin sets `enMusicControl = true`
in `Service::setCapabilities()`; the system claims the long press to open the
music overlay, and `HOLD_1S` never reached the app. Worse, the screen it put
the wearer on could only be left by that same event — so there was no way out
of it at all. The wearer reported being stuck on "Discard — keep holding" with
the music player opening underneath.

Two lessons, both binding:

- **Use `CLICK` only.** Nothing intercepts an ordinary click.
- **Every screen must be leavable by an event you have proven arrives.** Before
  you add a screen, name the button that leaves it and confirm that button is
  handled on that screen.

There is a tempting shortcut here: set `enMusicControl = false` and reclaim the
long press for auto-repeat. That *might* work — but it is a hypothesis about
firmware behaviour, it is **unverified**, and it changes what the watch does
outside this app. If you want it, propose it and test it on hardware
explicitly. Do not assume it and build a UI that depends on it.

### 6.2 The round panel ate the labels

Button labels were inset from the **framebuffer's** edge rather than the
**glass's**. In the square simulator they looked fine; on the watch the last
glyph of "START" and "PAUSE" was cut off by the bezel.

There is a test for this — `nothing_is_drawn_outside_the_bezel` in `lib.rs` —
which renders **every scene** and asserts no lit pixel falls outside the
inscribed circle. **Your new screen must be added to the scene list**, or the
test silently does not cover it.

### 6.3 PAUSE sat on top of the clock

The first layout put button hints on horizontal rows, and "PAUSE" landed
directly over the clock where it read as a caption for it. The fix: the four
buttons sit at the corners of the bezel, so their hints sit on the **diagonals**
— `BUTTON_L1_DEG = -45`, `R1 = 45`, `R2 = 135`, `L2 = 225` — as a short thick
arc at the button's own angle, with a word inboard of it.

Follow that family. A hint belongs at its button's angle. And note the existing
rule: **words only where there is a decision.** Mid-ride, R1 does one obvious
thing and gets a mark with no word. Your entry screen *is* a decision, so it
gets words — but keep them short and do not re-label what is obvious.

### 6.4 The arc sampler left holes

`fill_arc` originally stepped 0.9 px and left 171 unpainted pixels inside
shapes it had supposedly filled. The measured threshold is about 1/√2 ≈ 0.707;
the constants are now `ARC_ANGULAR_PX = ARC_RADIAL_PX = 0.65`. If you draw arcs,
use `fill_arc` and do not invent a second sampler.

### 6.5 A block edit silently deleted four tests

A previous change replaced a block in `lib.rs` and removed four tests with it,
unnoticed until later. **Check the test count before and after your change.**
`cargo test --features std` currently reports **25 passed**. If your change
leaves fewer than 25 plus whatever you add, you deleted something.

### 6.6 The heart rate that blanked, and the fix that is not a filter

The wearer reported HR "jumping around". It was not noise — it was blanking:
the kernel reports 0–3 confidence and the GUI dropped the number whenever
confidence hit 0, several times a minute. `HrHold.hpp` now holds the last
trusted reading for 10 s. Read its header comment before touching anything on
the HR path: it explains at length why it deliberately **does not average or
smooth**, backed by measurements from two real rides (mean change between
consecutive samples: 0.50 and 0.18 bpm — there is no jitter to remove and a
filter would only add lag).

You probably do not need to touch this. If you find yourself wanting to, you
have taken a wrong turn.

### 6.7 A commit message that described half of what it contained

A `git add Spin` swept an unrelated change into a commit whose message
described only the intended fix. Stage deliberately. One logical change per
commit.

---

## 7. FIT fields — verified numbers, do not guess

A field number written into a `.fit` file is wire format. Get one wrong and the
value silently lands in whatever field really has that number. There is a
lookup tool in the repo — `Tools/fit-profile/lookup.py` — which fetches
Garmin's profile and cross-checks it against `python-fitparse`, printing
`agrees` or `*** DISAGREES ***`. **Use it for any field not listed below.**

Verified, both sources agreeing:

| Message | Field | Num | Type | Scale | Units |
|---|---|---:|---|---:|---|
| `session` | `total_work` | **48** | uint32 | 1 | J |
| `session` | `avg_power` | **20** | uint16 | 1 | watts |
| `session` | `metabolic_calories` | **196** | uint16 | 1 | kcal |
| `lap` | `total_work` | **41** | uint32 | 1 | J |
| `lap` | `avg_power` | **19** | uint16 | 1 | watts |

Note `total_work` is in **joules**, not kilojoules. The wearer enters kJ;
multiply by 1000.

`SDK/Fit/FitProfile.hpp` carries only the fields UNA's own apps write and has
none of these. That is not a blocker: `FitWriter::Field` is just
`{number, BaseType}` and Spin already declares its own — it writes
`time_in_hr_zone` (session **65**, lap **57**) despite neither being in the SDK
profile. Declare what you need locally; do not modify the SDK.

**The trap that is already documented and already has a regression test:**
`time_in_hr_zone` is **not the same number on both messages** — lap is 57,
session is 65, and *session field 57 is `avg_temperature`*. `ActivityWriter_test.cpp`
asserts the session wrote nothing into field 57. Follow that pattern: if you
add a field, add a test that asserts you did not write into its neighbours.

### Things not to do with these fields

- **Do not synthesise a per-record power stream** from a single total. It is
  tempting because it would make Strava draw a power graph. A constant stream
  makes normalized power ≈ average power, and every platform downstream then
  computes a confidently wrong TSS from it. Session and lap totals only.
- **Do not write `training_stress_score` (35) or `intensity_factor` (36).**
  Both need a real FTP; derived from a fake NP they are precision-shaped
  garbage.
- **Do not put a training-load number in `total_training_effect` (24).** That
  field is Firstbeat's 0–5 aerobic TE with specific semantics.

---

## 8. The phone side: `customMeasures`

`Spin/app-manifest.json` currently declares `"customMeasures": []`. That is the
mechanism by which extra metrics appear in the UNA phone app after an activity
— and because it is empty, the four developer fields Spin already writes
(`resting_calories`, `hr_source`, `hr_optical`, `hr_external`) are **invisible
today**.

`customMeasures` is a UNA manifest concept, **not** a FIT concept. It is the
display layer, independent of whether the underlying number is a native field
or a developer field.

Declare entries for the work and power numbers you add. The schema is in
`una-sdk/Docs/app-config-json.md` (~line 115); each entry needs `id`, `title`,
`icon`, `unitMetric`, `unitImperial`, `unitScalingFactor`, `isTimeBased`,
`visualisation` (`line-chart` / `number` / `gauge`), `preview`, and
`previewAggregation` for time-based measures. Yours are per-activity, so
`isTimeBased: false` and `visualisation: "number"`.

Lighting up the four existing developer fields at the same time is in scope and
cheap. Do it.

---

## 9. Explicitly out of scope

- **RPE.** There is a native field — `session.workout_rpe` (**193**, uint8) —
  and a sibling `workout_feel` (**192**). But the encoding is **unverified**: it
  is unclear from the profile alone whether it is 1–10 or 10–100, and Garmin
  Connect displays 1–10 while likely storing ×10. Do not write a field you are
  guessing at. Leave RPE out, but do not design an entry screen that could only
  ever hold one number.
- **Recovery heart rate.** Wanted, but harder than it looks: the naive version
  ranks a rider who does a proper Z4→Z3→Z2 cooldown as *less* fit than one who
  sprints and steps off, because active recovery blunts the fall and a gradual
  cooldown has already spent the drop before the clock starts. A correct
  version needs peak detection, a preconditions gate (HR ≥ ~70% max, and not
  already declining), and an exponential fit for the time constant. Its own
  release. Not this one.
- **On-watch history, trends, or comparisons.** §1.

---

## 10. Building, testing, looking at it

The toolchain is a Docker image; there is no cmake or ARM toolchain on the host.

```sh
# Rust renderer: unit tests
cd Spin/Software/Apps/CustomGUI/rust
cargo test --features std              # currently 25 passed — see §6.5

# Look at every scene as a PNG. Needs no SDL2, blacks out the bezel corners
# (which the device simulator does not).
cargo run --features preview --bin preview -- /tmp/spin
cargo run --features sim --bin sim     # a window; TAB cycles scenes

# C++ tests
cmake -B build -S Spin/Tests && cmake --build build && ctest --test-dir build

# App config must validate against the manifest
python3 "$UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py" \
    --check Spin/app-manifest.json \
    --check-bounds Spin/Software/Libs/Sources/AppConfigFields.cpp
```

`render()` is a pure function of one `Frame`. The scene list in
`src/scenes.rs` is shared by the preview binary, the simulator and the tests,
so "every screen still draws" is checked against the same list a human reviews.
**Add your screen to it.**

Fonts available in the renderer: `ClockXl/L/M` and `NumberFont` are u8g2 `_tn`
faces — **digits only, no letters**. `TitleFont`, `HeadingFont` and `LabelFont`
are `_tr` (ASCII). A big number can use a `_tn` face; anything with words
cannot.

---

## 11. Definition of done

- [ ] A wearer can enter kJ after a ride, in a few seconds, using `CLICK` only.
- [ ] A wearer can skip in **one labelled click**, and the resulting ride is
      indistinguishable from today's in every respect except the absent fields.
- [ ] When skipped, `total_work` and `avg_power` are **absent** from the FIT
      file, not zero.
- [ ] When entered, `session.total_work` (48, joules) and `session.avg_power`
      (20, watts) are present and correct; verified by decoding a real file,
      not by reading the writer.
- [ ] Every new screen is in `scenes.rs`, and
      `nothing_is_drawn_outside_the_bezel` passes.
- [ ] Every new screen names the button that leaves it, and that button is
      handled on that screen.
- [ ] `cargo test --features std` reports **at least 25** tests, plus yours.
- [ ] The ABI fingerprint's three representations agree and the app starts.
- [ ] `customMeasures` is populated, including the four existing developer
      fields.
- [ ] `validate_app_config.py --check-bounds` passes.
- [ ] Tested **on hardware**, not only in the preview — specifically the skip
      path, the entry path, and backing out of the entry screen.

---

## 12. House style

Read `Spin/README.md` first; it is long and it is the design record. Match what
you find there.

- Comments explain **why**, and especially why-not — the alternative that was
  tried and failed. `HrHold.hpp`, `ZoneLadder.hpp` and the `enMusicControl`
  comment in `Gui.cpp` are the register to aim for.
- Prefer **measurement over assertion**. This codebase settled the arc sampling
  threshold by counting unpainted pixels, chose the HR hold window from dropout
  statistics across two real rides, and picked palette colours against WCAG
  contrast ratios. If you are choosing a number, measure it and record what you
  measured.
- Logic worth testing goes in a **header-only file free of SDK types**, so it
  can be tested without a kernel. `HrHold.hpp` and `ZoneLadder.hpp` are the
  precedent. Whatever arithmetic your entry screen needs — step sizes, clamping,
  the kJ→watts conversion — belongs there, not buried in the Service.
- Conventional commits; CI bumps the version and cuts the release from the
  commit type. One logical change per commit (§6.7).
