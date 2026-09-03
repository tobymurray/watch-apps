# Heart-rate recovery, and a record that outlives the ride

You are implementing a feature in the **Spin** app of the `watch-apps`
repository — a stationary-bike activity app for the UNA Watch. Read this whole
document before writing code, and read `Spin/README.md` first; it is the design
record and this brief assumes it.

Two things are being asked for, and the second is the larger one:

1. **Measure heart-rate recovery** after effort, automatically, with no setting
   and no button.
2. **Keep a record across sessions**, in the shared data folder, so that
   training load and other longitudinal insight can be built later — by this
   app or by another.

---

## 0. The honesty contract, which outranks everything else here

This app has a standing rule that it does not write a number it cannot stand
behind. It refuses to write a FIT field whose number has not been cross-checked
against two independent copies of the profile (`Tools/fit-profile/lookup.py`).
It refuses to synthesise a per-record power stream from a session total, because
a constant stream makes normalized power ≈ average power and every platform
downstream then computes a confidently wrong TSS. It shows `---` rather than a
stale heart rate.

**Apply the same rule to physiology.** Everything in §3 below is written from
memory and is *unverified*. Before any of it reaches the glass, a file, or a
comment stated as fact:

- Find the primary source. Record author, year, journal and the actual number.
- If two sources disagree, say so in the comment rather than picking one.
- If a claim cannot be sourced, **do not make it** — drop the feature that
  needed it rather than shipping a number with no provenance.

The failure mode to avoid is a screen that says something like "Recovery: good"
or "Fitness improving". Those are claims about a person, and this app cannot
support them. It can report *what it measured* and *how it compares to the same
measurement before*. That is all.

Write down, in the README, the list of things this feature **cannot** tell the
wearer. That section is not a disclaimer; it is the part that keeps the rest
trustworthy.

---

## 1. The platform, and the two facts that shape everything

### The sensor is gone the moment the ride stops

`Service::stopTrack()` ends with `disconnect()`, which releases the heart-rate
sensor. There is **no heart rate after a ride ends**. Nothing arrives on the
SAVED screen.

This is the single hardest constraint in the feature, because the textbook
protocol for heart-rate recovery is *measure for 60 s after effort ceases*. You
cannot do that after `TRACK_STOP` without changing the lifecycle, and changing
it has costs:

- Deferring `disconnect()` keeps a BLE link and the optical sensor alive after
  the wearer thinks they are finished, on a device where that costs battery.
- The FIT file is finalised inside `stopTrack()`. Anything measured after it is
  too late for that file. **Do not reopen a closed `.fit`** — see §5.
- The wearer can press DONE and leave at any moment. A measurement that needs
  them to stand still watching a screen for two minutes will mostly not happen.

**Candidate windows, none of them free.** You are not required to pick from this
list, but you must justify whatever you choose:

- **The pause.** When the wearer pauses, effort stops and the sensor is still
  connected — `processTrack()` keeps running while `PAUSED` and heart rate keeps
  arriving. Costs nothing, needs no lifecycle change, and is genuinely
  automatic. But a pause is not a standardised protocol: someone pausing to
  adjust a shoe at low intensity is not recovering from anything, which is
  exactly what the preconditions in §3 are for.
- **A window after `TRACK_STOP`, before `disconnect()`.** Truest to the
  protocol, but it fights the wearer's intent to be finished, and the result
  arrives after the FIT file is closed.
- **The end of a lap.** Now that R2 marks laps mid-ride, an interval session has
  explicit boundaries. Recovery between intervals is a real and interesting
  measurement — but it is recovery from an interval, not from a session, and
  the two are not comparable.

Whichever you choose, the measurement must be **discarded** when its
preconditions were not met. A recovery number from a pause that began at 95 bpm
is not a small number; it is not a measurement.

### There are no RR intervals, so there is no HRV

Heart-rate variability (rMSSD and friends) is the better-founded measure of
autonomic status, and it is **not available**. The SDK's beat-to-beat pathway
lives in an unlanded experimental PR (`feat/rr-interval-contract`, PR #220 on
`UNAWatch/una-sdk`); Spin builds against SDK 1.4.0, whose `HeartRateEx` parser
exposes only `BPM`, `TRUST_LEVEL`, `SOURCE`, `OPTICAL_BPM`, `OPTICAL_TRUST`,
`EXTERNAL_BPM`, `EXTERNAL_TRUST` — all floats, one sample a second.

Do not design around HRV arriving. Do note in the README that if that pathway
ever lands, it supersedes much of this.

### What you do have

- **One arbitrated heart rate per second**, plus a 0–3 confidence. Only 1–3 is
  trusted; `HrHold` holds a reading for up to 10 s for the *screen* while the
  file records the gap honestly. Read `HrHold.hpp` before touching the HR path.
- **The wearer's maximum heart rate**, as `mSystemMaxHr` — the top of the
  watch's own threshold ladder, which is the maximum rather than a floor.
  It can be 0 if the watch has no zones set.
- **Zones and time in each**, already accumulated per session and per lap.
- **Body mass**, from the watch's profile, defaulting to 75 kg.
- **The kilojoules the wearer typed in**, when they did, and the average power
  derived from it.

---

## 2. Architecture, and where the work goes

Spin is a **Service** (C++, `Spin/Software/Libs/`) plus a **GUI**
(`Spin/Software/Apps/CustomGUI/`, a C++ shell around a Rust `no_std` renderer,
drawing text through the shared `TextKit` crate).

- The **Service** owns the clock, the sensors, the config and the filesystem.
  Everything here is Service work.
- The **GUI** cannot reach a sensor or a file. It draws the last snapshot it was
  handed and sends button presses back.
- **New logic goes in Rust where practicable**, and the GUI already links a Rust
  staticlib so anything GUI-side is free. The Service links no Rust today, and
  adding it drags a `#[panic_handler]` into the Service's crate graph and forces
  a host cargo build into `Spin/Tests`. Weigh that; this feature is Service-side,
  so C++ is likely right.
- **Logic worth testing goes in a header-only file free of SDK types**, so it can
  be checked without a kernel. `HrHold.hpp`, `ZoneLadder.hpp` and
  `SecondsAccrual.hpp` are the precedent, each with a suite in
  `spin-hrhold-tests`. The recovery detector and the load arithmetic both belong
  there. The kernel-free suite is where this feature earns its confidence,
  because **`Service.cpp` has no host tests at all** — a gap that has already let
  two accounting bugs ship.

---

## 3. The physiology — all of it unverified, all of it to be checked

Treat this section as a set of leads, not as facts. See §0.

### What heart-rate recovery is

**HRR₆₀** is the fall in heart rate in the 60 seconds after effort ceases:
`HR at cessation − HR 60 s later`, in bpm. HRR₁₂₀ is the same over 120 s.

The first ~30–60 s are believed to be dominated by **parasympathetic
reactivation** (vagal tone returning), with sympathetic withdrawal mattering
more later. This is why the 60 s figure is the one usually quoted, and why it is
sometimes treated as a window onto autonomic function rather than fitness as
such.

Leads to verify:

- **Cole et al., NEJM 1999** — HRR at 1 minute after graded exercise testing,
  with an active cooldown; a fall of **≤12 bpm** was associated with higher
  all-cause mortality. Check the exact protocol: the threshold is meaningless
  detached from it.
- **Shetler et al., JACC 2001** — 2-minute recovery, different cohort and
  protocol.
- **Buchheit** has published extensively on HRR's dependence on the preceding
  intensity, and on its reliability within an individual when that intensity is
  matched.

### What it cannot tell the wearer, and must not imply

- **It is not a fitness score.** A single HRR number is not comparable between
  people, and this app must never present it as a ranking.
- **It is dominated by what the wearer did next.** Active recovery blunts the
  fall; standing still exaggerates it. A rider who does a proper Z4→Z3→Z2
  cooldown will look *worse* than one who sprints and steps off. This is the
  single most important trap in the feature, and any naive implementation walks
  straight into it.
- **It depends on the intensity at cessation.** Recovering from 175 bpm is not
  the same measurement as recovering from 120 bpm, and the numbers are not
  interchangeable.
- **It is confounded** by posture, ambient temperature, hydration, caffeine,
  illness, sleep and time of day — the same list that makes raw heart rate a
  poor fitness signal in the first place, which is why the kilojoule entry
  exists at all.
- **One measurement means nothing.** Whatever is reported must be framed as a
  point in a series, and the series is only meaningful under matched conditions.

### The preconditions gate

A measurement should be recorded only when it can mean something. Candidates to
justify and tune:

- Heart rate at cessation ≥ some fraction of maximum (**~70%** is the number
  usually cited; verify) — and the watch must actually know the maximum.
- Heart rate **not already falling** when the window opens: recovery from a
  plateau after a two-minute soft-pedal is not recovery from effort.
- A minimum effort duration before it, so the first minute of a ride cannot
  produce one.
- Enough trusted samples across the window. A dropout in the middle of a 60 s
  window is a discarded measurement, not an interpolated one.
- Nothing else that would restart effort inside the window.

**Record the context alongside the number**, always: heart rate at cessation, as
a bpm and as a fraction of maximum; the window length actually used; how many
samples were trusted; and whether the wearer was paused or stopped. A bare HRR
with no context cannot be compared to anything later, and comparison is the
entire point.

### An exponential fit, if it earns its place

A single difference over 60 s throws away the shape of the curve. Recovery is
often modelled as an exponential decay toward a resting asymptote, with a time
constant τ that is more robust to *when* you sampled than a fixed-interval
difference. That is attractive, and it is also more machinery: a fit needs
enough clean samples and a defensible failure mode when the fit is poor.

Do the simple thing first and measure whether the fit adds anything. If τ is
computed, report the fit quality with it, and discard fits that do not converge
rather than reporting a number nobody can interpret.

---

## 4. The record that outlives the ride

This is the part the wearer never sees directly, and the part most worth getting
right.

### Where it goes

`../SharedData/` — a sibling of each app's own directory, so from Spin's working
directory the path is literally `../SharedData/<name>.json`.

This is an established SDK convention, not an invention: the stride calibration
that Running and Treadmill share lives at `../SharedData/stride.json`
(`SDK::Calibration::StrideLut::kDefaultPath`), and the SDK writes it with
`SDK::JsonStreamWriter`.

Read `Libs/Source/Calibration/OutdoorStrideCalibrator.cpp::finalise()` in the
SDK before writing anything. It is the worked example, and it carries three
things you will otherwise rediscover:

- **`mkdir` the parent first.** FatFs `f_open` does not create missing parents,
  and "already exists" counts as success.
- **Fold `flush()` and `close()` into the result.** On flash-backed FatFs the
  buffered tail may only reach storage at flush or close, so a writer that
  reported no error can still leave an incomplete file.
- **Only persist when there is something to persist.** It writes nothing if no
  sample was accepted.

### It must survive being interrupted

A shared file that a second app depends on cannot be left half-written by a
power loss mid-write. `NotifyToggle/Software/Libs/Sources/SettingsPersist.cpp`
implements the firmware's own pattern — write to `.tmp`, rotate `.bak`, rename
into place — and its comments explain which steps are best-effort and which are
load-bearing. Follow it, or justify something better.

The activity writer's own crash-recovery contract (`SDK::Fit::RecordingMarker`)
is worth reading for the register: the marker is only advanced when a flush
durably landed, never optimistically.

### It is a shared namespace, so behave like a guest

- **Version the schema.** The stride file writes a `"version"` integer. Do the
  same, and decide what a reader does with a version it does not know — refuse,
  or read what it recognises. Say which in a comment.
- **Own your filename.** Do not write into another app's file, and do not pick a
  name so generic that another app will collide with it.
- **Bound the growth.** This is flash, and there is no allocator. A history that
  appends forever will eventually fail to parse, fail to write, or fill the
  card. Decide the cap — a fixed number of sessions, a rolling window, or
  aggregates plus a short tail — and say what happens at the boundary. A cap
  that silently drops the oldest entry is fine; one that silently stops
  recording is not.
- **Assume another app reads it.** The point of `SharedData` is that a future
  training-load view, in this app or another, can build on it. That makes the
  schema a contract: document it in the README, in a table, with units.
- **Do not put anything in it that belongs in the FIT file.** The `.fit` is the
  record of the ride. This is the record of the *series*.

### What to store

At minimum, per session: when, how long the active time was, average and maximum
heart rate, time in each zone, the work in kilojoules if the wearer entered it,
and every recovery measurement with the context from §3.

Store **the inputs, not just the conclusions**. If a derived figure is stored,
store what it was derived from too, so a later change to the derivation can be
applied retrospectively rather than orphaning the history. This is the same
reason the FIT file carries `hr_source`, `hr_optical` and `hr_external` rather
than only the arbitrated beat.

---

## 5. Training load — what is honestly computable, and what is not

The user's interest is longitudinal insight. Be precise about which of these the
data can actually support.

### Computable from what Spin already records

- **Edwards' TRIMP** — Σ (minutes in zone *i* × weight *i*), weights typically
  1..5 for five zones. Spin already accumulates time in every zone, per session
  and per lap, so this needs no new measurement at all. Verify the weighting and
  the original citation (Edwards, 1993) before using it, and note that the
  weights assume a particular zone definition — Spin's zones are configurable
  from two to eight, which the formula does not anticipate. **Say what you do
  about that**, or restrict the metric to the zone counts it is defined for.
- **Banister TRIMP** — duration × ΔHR ratio × e^(b × ΔHR ratio), with b commonly
  given as 1.92 for men and 1.67 for women. Needs resting heart rate, which this
  app does not have, and sex, which it does not know. Probably out of reach —
  say so rather than approximating both.
- **Efficiency Factor** — average power ÷ average heart rate, which the kilojoule
  entry made possible and which was the reason it was built. Only for sessions
  where the wearer entered a figure; absent otherwise, never imputed.

### Not computable, and must not be faked

- **TSS / IF / normalized power.** All need a real FTP and a power *stream*.
  Spin has neither: one kilojoule total for a whole ride. The README already
  forbids `training_stress_score` and `intensity_factor` for exactly this reason.
- **Aerobic decoupling (Pw:Hr).** Needs power for the first and second halves
  separately. A single session total cannot produce it. If the lap data were
  used instead, note that laps are wearer-chosen and not halves.
- **ACWR (acute:chronic workload ratio).** Widely used and widely criticised —
  Impellizzeri and colleagues have argued the ratio is mathematically coupled
  and the evidence for the "sweet spot" does not hold up. If it appears at all,
  it appears with that caveat attached, not as a recommendation.
- **Anything phrased as advice.** "Ready to train", "overreaching", "peaked" are
  claims this data cannot support.

---

## 6. Scar tissue from this repository — do not re-ship these

- **Only `CLICK` reaches the app.** Spin sets `enMusicControl = true` in
  `setCapabilities()`, so the system claims the long press and `HOLD_1S` never
  arrives. A screen that could only be left by a long press stranded a wearer
  once. Every screen must name the button that leaves it.
- **Durations are not tick counts.** `SecondsAccrual` exists because banking a
  flat second per tick overstated every ride by one — N ticks span N−1 seconds.
  Anything accrued per second must go through it.
- **A wrong FIT field number is silent and permanent.** `session.avg_power` is
  20 while `lap.avg_power` is 19, and session field 19 is `max_cadence`. Use
  `Tools/fit-profile/lookup.py`, and add a test asserting nothing landed in the
  neighbours.
- **Sizes drift.** `Track::Data::zoneSeconds` was left at 6 elements when the
  dial grew to eight zones, and wrote past itself into the calorie accumulators.
  If you add a fixed array, `static_assert` it against whatever else indexes it.
- **The round panel eats what the square framebuffer allows.**
  `nothing_is_drawn_outside_the_bezel` renders every scene in `scenes.rs`; a new
  screen must be added to that list or it is not covered.
- **Check the test count before and after.** A block edit once deleted four
  tests unnoticed.
- **Comments obey the ownership test in `CLAUDE.md`.** Hardware behaviour proven
  on the watch, frozen wire format, and a measurement with its numbers are kept
  and must name what would falsify them; everything else that this file cannot
  keep true is deleted.

---

## 7. Explicitly out of scope

- **HRV / rMSSD**, until the beat-to-beat pathway lands in a released SDK.
- **Any on-watch chart or trend view.** The record is for a later reader, and a
  240×240 reflective panel is not where a longitudinal view belongs. If the
  wearer sees anything at all, it is at most a single measurement with its
  context, on a screen they already visit.
- **RPE.** `session.workout_rpe` is field 193 and `workout_feel` is 192, both
  `uint8`, but both come back `(not in cross-check)` from `lookup.py` — only one
  source carries them — and the profile's `scale=1` does not settle whether the
  semantic range is 1–10 or 10–100. Blocked until a file from a device that
  records RPE can be decoded and compared against what it displays.
- **Rewriting a closed `.fit`.** `ActivityWriter::stop()` reports a durability
  contract that the SAVED screen repeats to the wearer as a fact about the
  filesystem, and the recovery marker is dropped on the strength of it.
- **Changing the calorie model or the zone ladder.**

---

## 8. Building and checking

There is no cmake or ARM toolchain on the development host; the toolchain is a
Docker image. See `Spin/README.md` for the invocations, and note that
`Service.cpp` is compiled only by the app build, not by the host test suite —
so a `static_assert` in `Service.hpp` fires in CI's app build, not in `ctest`.

```sh
cd Spin/Software/Apps/CustomGUI/rust && cargo test --features std
cmake -B build -S Spin/Tests && cmake --build build && ctest --test-dir build
python3 "$UNA_SDK/Utilities/Scripts/app_packer/validate_app_config.py" \
    --check Spin/app-manifest.json \
    --check-bounds Spin/Software/Libs/Sources/AppConfigFields.cpp
```

---

## 9. Definition of done

- [ ] A recovery measurement is taken automatically, with no setting and no
      button, and the window it uses is justified against §1.
- [ ] Measurements that fail their preconditions are **discarded**, not recorded
      as small numbers, and the preconditions are tested.
- [ ] Every stored measurement carries its context: heart rate at cessation
      absolutely and as a fraction of maximum, window length, trusted-sample
      count, and whether it followed a pause, a lap or a stop.
- [ ] Every physiological claim in code, comments or on screen has a source
      recorded, or has been removed.
- [ ] The README gains a section saying plainly what this feature **cannot**
      tell the wearer, including the active-versus-passive recovery trap.
- [ ] The record lands in `../SharedData/`, versioned, written through a
      tmp/rename commit, with `mkdir` of the parent and `flush`/`close` folded
      into the result.
- [ ] Growth is bounded, the bound is stated, and the behaviour at the bound is
      deliberate.
- [ ] The schema is documented in the README as a table with units, as a
      contract for whatever reads it next.
- [ ] The detector and the load arithmetic live in header-only files free of SDK
      types, with suites in `spin-hrhold-tests`; every test is checked to fail
      when the behaviour it pins is reverted.
- [ ] Nothing is written into the `.fit` that was not verified with
      `lookup.py`, and a test asserts the neighbouring field numbers are empty.
- [ ] Tested on hardware, and the resulting `SharedData` file read back and
      decoded — not merely believed because the writer returned true.
