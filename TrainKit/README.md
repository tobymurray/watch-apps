# TrainKit — heart-rate recovery, and a record that outlives the session

Two things, for any activity app on this watch:

1. **Measure heart-rate recovery** after effort — automatically, with no setting
   and no button.
2. **Keep a bounded record across sessions**, in `../SharedData/`, so training
   load and other longitudinal insight can be built on it later, by the app that
   wrote it or by another one.

Rust, `no_std`, no allocator, no SDK types, no clock and no filesystem — so
everything it decides can be checked by `cargo test` without a kernel. The two
pieces of C++ are [`cpp/SharedLog.cpp`](cpp/SharedLog.cpp), which does the file
I/O the Rust deliberately knows nothing about, and
[`cpp/EventLog.cpp`](cpp/EventLog.cpp), which writes the diagnostics that `LOG_*`
cannot reach without a debug UART adapter.

## Why it is a crate and not a file in one app

`Spin`, `Squash`, `RunMap`, `HikeMap` and `BikeMap` all connect
`HEART_RATE_EX`, all have a pause, and all end a session. Recovery detection is
the same arithmetic in every one of them, and the log's schema has to be *one*
schema or the point of a shared folder is gone. A second copy is a second thing
to keep in step — the same argument `Spin/README.md` already makes for reading
the watch's own zone ladder rather than restating it.

Only Spin links it today. Adopting it elsewhere is four lines of CMake and the
five calls in `Service.cpp`; nothing here is Spin's.

---

## What this cannot tell you

This section is not a disclaimer. It is the part that makes the rest worth
having, and every claim in it is sourced below.

- **It is not a fitness score, and it is not a ranking.** A single HRR number is
  not comparable between people. Nothing in this crate produces a verdict, a
  grade or a colour, and nothing should be built on it that does.
- **It is dominated by what you did next, and this is the biggest trap.** Barak
  et al. (2011) fitted the recovery curve in the *same* athletes under four
  protocols: the time constant was **52.5 s sitting still**, **74.1 s pedalling
  gently in the same seated position** — 41% slower for moving rather than
  sitting — and **32.0 s lying down**. A rider who does a proper cool-down will
  look *worse* than one who sprints and steps off. **The watch cannot tell these
  apart**: it knows the ride was paused, not whether you kept turning the
  pedals, sat up, or lay on the mat. Two measurements taken after different
  things you did are not two measurements of the same thing.
- **It depends on how hard you were going.** Buchheit (2014): "the greater the
  relative exercise intensity, the greater the blood acidosis and metaboreflex
  stimulation, and the slower the HRR". Recovering from 175 bpm and from 120 bpm
  are different measurements, which is why `hr0` and `hr0_pct_max` are stored
  beside every number and why a window below 80% of maximum is discarded rather
  than recorded small.
- **One measurement means nothing.** Buchheit (2014), Table 1, puts HRR₆₀'s
  typical error at a coefficient of variation of about **25%**, with a
  signal-to-noise ratio of **1.3**. A change smaller than a quarter of the
  number is noise. This is the single reason nothing is drawn on the watch.
- **It is confounded by things not recorded here.** Daanen et al. (2012) name
  age, ambient temperature, and the intensity and duration of the preceding
  exercise as what makes studies hard to compare; posture, hydration, caffeine,
  illness, sleep and time of day belong on the same list. The same list that
  makes raw heart rate a poor fitness signal in the first place.
- **A faster fall is not automatically better.** Buchheit (2014) reports HRR
  moving in both directions with training load — "the greater the training load
  … the slower the HRR" — so a number that moved is a number that moved.
- **It is not HRV.** rMSSD and its family are the better-founded measure of
  autonomic status and are **not available**: the SDK's beat-to-beat pathway is
  an unlanded experimental PR, and 1.4.0's `HeartRateEx` gives one arbitrated
  bpm and a confidence, once a second. If that ever lands in a release it
  supersedes most of this file.
- **`edwards_trimp` is a bookkeeping number, not a dose.** Its weights are
  ordinal and were never validated against a physiological response.

---

## The measurement

**HRR₆₀** is the fall in heart rate over the 60 seconds after effort ceases, in
bpm. That is all this crate computes. It does not compute a verdict.

### The window is the pause, and here is why

The textbook protocol is *measure for 60 s after effort ceases*, and on this
watch that is harder than it sounds: an activity Service's `stopTrack()` ends by
releasing the heart-rate sensor, so **there is no heart rate after a session
ends**. Three windows were possible:

| Window | Cost |
|---|---|
| **The pause** | Not a standardised protocol, so the gates below have to do the work of one. |
| After `TRACK_STOP`, before `disconnect()` | Keeps a BLE link and an optical sensor alive after the wearer thinks they are finished, and the result arrives after the `.fit` is closed. |
| The end of a lap | A lap does not mean effort stopped. Recovery between intervals is real, but it is not recovery from a session and the two are not comparable. |

**The pause wins**, and the deciding argument is specific to how these apps
work: the ride stays `PAUSED` through the post-ride screens until `TRACK_STOP`,
so *the end of a ride is already a pause* and no lifecycle change buys anything.
The sensor is connected, the tick keeps running, the wearer has already stopped
pedalling, and it costs nothing. A mid-ride pause to fix a shoe is the same
event, which is exactly what the gates are for.

`TRIGGER_LAP` and `TRIGGER_STOP` exist in the schema so a reader can tell them
apart if some app ever produces one. Nothing does today.

### The gates, and what each one is for

A measurement is **discarded** when its preconditions were not met. A recovery
number from a pause that began at 95 bpm is not a small number — it is not a
measurement. Every gate has a test named after it in
[`tests/recovery.rs`](tests/recovery.rs).

| Gate | Value | Where the number comes from |
|---|---|---|
| The watch knows a maximum heart rate | > 0 | Without it there is no intensity to record the measurement against, and comparison is the whole point. |
| Heart rate at cessation | ≥ **80%** of maximum | Matches Barak et al.'s protocol — 5 min at 80% of peak heart rate — because their time constants are what this is calibrated against. The 70% that circulates for this traces to fitness writing, not to a primary study, so it is not used. |
| Uninterrupted effort before it | ≥ **180 s** | Buchheit (2014): "at least 3–4 min of exercise are generally required for HR to reach a steady state". That is about steady state rather than recovery, so this is the weakest gate here; it exists so the first minute of a ride cannot produce a measurement. Measured on the *current bout*, so ten minutes of riding then a pause then ten seconds does not qualify. |
| Heart rate not already falling | within **10 bpm** of the peak of the preceding 30 s | At Barak et al.'s 52.5 s time constant, a 70 bpm reserve falls **30 bpm** in the first 30 s. Spin measured **0.50** and **0.18 bpm** between consecutive readings over two real rides. 10 sits between the two with room on both sides. |
| Trusted readings in that 30 s | ≥ **20 of 30** | Spin measured 5% of seconds untrusted over two rides, so about 1.5 of 30 is normal and 10 missing is not. |
| A trusted baseline at cessation | within **2 s** | Past that the baseline is already down the curve and understates the fall. |
| Trusted seconds inside the window | ≥ **90%** | Allows 6 untrusted seconds in 61. Measured across 34 minutes of pulled recordings: 5.3% of seconds untrusted, in 89 runs of median length 1 and **maximum 4** — no run has ever been long enough to trip this on its own. **A dropout is a discarded measurement, never an interpolated one.** |
| The sensor did not change | — | **14%** of 60 s windows in those recordings begin and end on different sensors, and where both report at once they differ by a median of 2 bpm and a 95th-percentile **16**. The falls being measured are 8–20 bpm, so those windows are partly a comparison of instruments; they averaged **−2.1 bpm**, an apparent rise. |
| A trusted endpoint | at 60 s, or within **2 s** after | `window_s` records the second actually used, so a stretched window is visible rather than rounded away. |
| Nothing restarted effort | — | Resuming the ride, or ending it, closes the window with nothing. |

Everything is keyed on the **UTC second**, never on the number of calls, for the
reason `SecondsAccrual.hpp` exists: a tick the Service was too busy to serve is
a real second that went past, and counting calls would quietly shorten every
window it happened in.

### What is stored with every measurement

A bare HRR with no context cannot be compared to anything later. Each one
carries the heart rate at cessation both absolutely and as a fraction of
maximum, the window length actually used, how many of its seconds were trusted,
and whether it followed a pause, a lap or a stop.

It also carries a **seven-point curve** — bpm at 0, 10, … 60 s — which is the
input any later exponential fit would need. No fit is computed: a τ needs clean
data and a defensible failure mode, and there is no ride log yet to measure
whether it adds anything over the difference. Storing the curve means that
decision can be revisited *retrospectively* rather than orphaning the history,
which is the same reason a FIT file here carries `hr_source`, `hr_optical` and
`hr_external` rather than only the arbitrated beat.

---

## The record

### Where it goes

`../SharedData/<app>_sessions.json` — a sibling of each app's own directory.
This is the SDK's own convention rather than an invention: the stride
calibration Running and Treadmill share lives at `../SharedData/stride.json`
(`SDK::Calibration::StrideLut::kDefaultPath`).

**One file per app**, named after it. Two apps never write the same file, so
there is no lock to get wrong; a reader wanting every sport globs
`*_sessions.json` and merges on the `"app"` and `"sport"` fields.

### It survives being interrupted

`SharedLog::record()` follows the firmware's own pattern, which
`NotifyToggle/Software/Libs/Sources/SettingsPersist.cpp` also implements:

1. **`mkdir` the parent.** FatFs `f_open` does not create missing parents, and
   "already exists" counts as success.
2. Write the whole new file to `.tmp`, with **`flush()` and `close()` folded
   into the result** — on flash-backed FatFs the buffered tail may only reach
   storage at one of those, so a writer that reported no error can still leave
   an incomplete file.
3. Rotate the live file to `.bak` (**best-effort**: losing the backup does not
   block the commit, because the commit is a rename that does not depend on it).
4. Rename `.tmp` over the live file.

A power loss leaves either the old file or the new one, never half of either. If
the final rename fails the `.tmp` is deliberately **left in place** — the new
content is written, just not live, and deleting it would throw away the only
copy of that session.

**Only when the session's own `.fit` landed.** A ride that failed to save, or
that the wearer discarded, writes nothing here, so the two records can never
disagree about whether a session exists.

### The bound, and what happens at it

Two caps, and the byte cap is the one that decides:

| Cap | Value | At the bound |
|---|---|---|
| Entries | **20** | The oldest is dropped. |
| File | **16 KiB** | `save` drops the oldest and re-serialises until it fits. |
| Recoveries per session | **2** | The newest are kept and `recoveries_dropped` counts the rest. |

The newest session therefore **always lands**; it is the oldest that goes, and
`"dropped"` counts everything ever evicted so a reader can tell a truncated
series from a complete one. The end-of-session pause is the recovery that
happens every session, so it is the one comparable across them, which is why the
*newest* recoveries win rather than the first.

Measured, in [`tests/history.rs`](tests/history.rs), not argued: 20 sessions
with **every field at its widest** serialise to **13,420 bytes**, so the entry
cap is always reachable inside the byte cap. Twenty ordinary sessions come to
**9,046**. At 24 entries the worst case was 16,084 of the 16,384 available — a
margin too thin to add a field to, which is why the cap is 20. Both numbers are
assertions; changing any cap or adding any field means re-running them.

16 KiB is the same cap `SDK::Calibration::StrideLut::kMaxStoreBytes` uses, and
for the same reason: the read is buffered, so the bound is what stops a corrupt
or foreign file asking for a transient allocation this device does not have.
Peak transient cost is one 16 KiB buffer plus a 1,920-byte log, both freed
immediately, and both only at the end of a session — after the `.fit` is closed.

### Versions

The file carries `"version"`. This build writes and reads **1**.

- **A version above 1 is refused**: nothing is written and the file is left
  exactly as found. A writer that knows more than this build does must not be
  clobbered by it, and what it wrote is not recoverable once overwritten.
- **A file that is not JSON, or carries no version**, is rotated to `.bak` and a
  fresh one started — kept as evidence rather than deleted.

### The schema

```json
{"version":1,"app":"Spin","sport":"indoor_cycling","kept":3,"dropped":0,
 "sessions":[ ... ]}
```

| Field | Unit | Means |
|---|---|---|
| `version` | — | Schema version; 1 today. |
| `app` | — | Which app wrote it, as it spells itself. `[A-Za-z0-9_-]` only; the filename is the same slug lowercased. |
| `sport` | — | What the entries are, for a reader merging several apps' logs. |
| `kept` | count | Sessions in this file. |
| `dropped` | count | Sessions evicted over the life of the file. |
| `sessions` | — | Oldest first. |

Each session:

| Field | Unit | Means |
|---|---|---|
| `start_utc` | s, Unix | When it started, and the entry's identity — a session recorded twice replaces itself rather than appearing twice. |
| `active_s` | s | Unpaused time. |
| `elapsed_s` | s | Wall clock, start to stop. |
| `hr_avg` | bpm | Over the session. |
| `hr_max` | bpm | Over the session. |
| `hr_max_setting` | bpm | What the watch calls the wearer's maximum. 0 = it has none. |
| `weight_kg` | kg | What the calorie model used. |
| `kcal` | kcal | Active energy, from the app's own model. |
| `work_kj` | kJ | **Absent** when nobody entered one. Never 0 — zero is a measurement claiming no work was produced. |
| `zone_count` | count | Zones the ladder had. 0 = none set. |
| `zone_floors` | bpm | `[i]` is the lowest heart rate in zone `i+1`. |
| `zone_s` | s | `[0]` is time below zone 1, `[i]` is time in zone `i`. Length is `zone_count + 1`. |
| `edwards_trimp` | minute-weights | **Absent** unless the ladder is the one Edwards' weights are defined over; see below. |
| `recoveries` | — | 0 to 2 measurements. |
| `recoveries_dropped` | count | Measured, but did not fit. |

Each recovery:

| Field | Unit | Means |
|---|---|---|
| `at_active_s` | s | Active seconds into the session that effort ceased. |
| `trigger` | — | `"pause"`, `"lap"` or `"stop"`. Only `"pause"` is produced today. |
| `hr0` | bpm | At cessation. |
| `hr_end` | bpm | At the end of the window. |
| `drop_bpm` | bpm | `hr0 - hr_end`. The measurement. |
| `window_s` | s | Actually spanned. 60 normally, up to 62 when the endpoint was late. |
| `hr0_pct_max` | % | `hr0` over `hr_max_setting`. |
| `trusted_s` | s | Seconds inside the window the sensor was believed. |
| `source` | — | `"optical"` or `"external"`; the sensor every reading in the window came from. Constant by construction — a switch discards the window. |
| `curve` | bpm | At 0, 10, … 60 s from `hr0`. **0 means no trusted reading landed there** — a hole, not a reading of zero. The stream is whole-bpm quantised, so this is evidence rather than a promise that a curve fit would mean anything. |

**Inputs, not conclusions.** Everything a derived figure was derived from is
stored beside it, so a later change to the derivation can be applied
retrospectively. Efficiency Factor is not stored for exactly that reason:
`work_kj`, `active_s` and `hr_avg` are all here and a reader can divide.

**Nothing that belongs in the `.fit` is in here.** The `.fit` is the record of
one session; this is the record of the series.

---

## Training load

### Computable, and computed

- **Edwards' TRIMP** — minutes in each zone times a weight of 1..5, summed.
  Needs no new measurement: an app that already accumulates time in zone has it.

  The weights are defined for **five zones at 50-60, 60-70, 70-80, 80-90 and
  90-100% of maximum heart rate** and for no others. These apps allow anything
  from two to eight zones with floors the wearer can set, which Edwards never
  wrote weights for — so `edwards_trimp` is **emitted only when the ladder is
  actually that ladder** (within 1 bpm of rounding) and is **absent otherwise**,
  rather than being extended to a ladder it is not defined over. The watch's own
  default is 50/60/70/80/90/100% of maximum, so a five-zone ride that left the
  floors alone qualifies; a three-zone polarised split does not.

  Time below zone 1 carries no weight, because Edwards gives it none.

- **Average power** and **Efficiency Factor** (W per bpm), from a work figure
  the wearer entered. `avg_power_w` is the same arithmetic and the same divisor
  the FIT file's `avg_power` uses, so the two can never disagree. Both are 0
  when there is no work figure, and **never imputed** — Efficiency Factor is the
  one number here independent of the heart-rate model, and filling it in would
  make it a function of that model instead.

### Not computable, and not faked

- **TSS, IF and normalized power.** All need a real FTP and a power *stream*.
  One session total for a whole ride is neither, and a constant stream would make
  normalized power ≈ average power so every platform downstream computes a
  confidently wrong TSS.
- **Aerobic decoupling (Pw:Hr).** Needs power for the first and second halves
  separately.
- **Banister's TRIMP.** Needs resting heart rate, which no app here has, and
  sex, which none knows. Approximating both and calling the result Banister's
  would be two guesses inside a formula whose whole claim is physiological
  grounding.
- **ACWR.** Impellizzeri et al. (2020) argue the ratio is mathematically coupled
  and that its conceptual basis does not hold up; a reader is free to compute it
  from this file, but nothing here recommends it.
- **Anything phrased as advice.** "Ready to train", "overreaching", "peaked" and
  "recovery: good" are claims about a person that this data cannot support.

---

## Sources

Everything numeric above traces to one of these. Where two disagree, both are
named rather than one being picked.

### What this repository's own recordings say

Everything above that says "measured" now has a second, independent source:
34 minutes of `HEART_RATE_EX` pulled off this watch by the Squash work
(`Squash/Tests/pulled/*_hr.csv`, six sessions, 2,021 samples). It is squash
rather than cycling and at 64–110 bpm, so it says nothing about the intensity
gate — but the sensor is the same sensor, and it settles four things:

| Measured | Value | What it means here |
|---|---|---|
| Untrusted seconds | **5.3%** | Confirms the 5% Spin measured over two rides, on different sessions and a different app. |
| Untrusted run length | median **1**, max **4**, none ≥ 6 | The 90% gate cannot be tripped by one dropout, only by several. |
| Sample interval | median **1005 ms** | The stream is slightly slower than 1 Hz and occasionally skips to ~2 s. Harmless: the detector is keyed on the Service's UTC tick, and a tick with no new sample repeats the previous reading rather than opening a gap. |
| Consecutive change | mean **0.49 bpm**, median **0**, **65% of steps are zero**, and every value is a whole bpm | The stream is integer-quantised and mostly repeats. |

That last row matters for the curve. A fall of 8–20 bpm over 60 s is 8–20
quantisation steps, which is ample for a **difference** and marginal for a
**fitted time constant** — so the curve is stored as evidence, not as a promise
that a τ fitted from it would mean anything. EffortKit reaches the same caution
from the other direction and carries `NotMeasurableOnThisHardware` for it.

- Cole CR, Blackstone EH, Pashkow FJ, Snader CE, Lauer MS. "Heart-rate recovery
  immediately after exercise as a predictor of mortality." *N Engl J Med*
  1999;341:1351-1357. The 60-second interval, and the ≤ 12 bpm reading of it —
  after a **symptom-limited Bruce treadmill test with a 2-minute walking
  cool-down**. That threshold does not transfer to a wearer sitting on a bike,
  and **nothing here reports it**. (Reported cohort size differs between
  secondary sources; the number is not used.)
- Shetler K, Marcus R, Froelicher VF, Vora S, Kalisetti D, Prakash M, Do D,
  Myers J. "Heart rate recovery: validation and methodologic issues." *J Am Coll
  Cardiol* 2001;38(7):1980-1987. 2,193 male patients; **2-minute** recovery was
  more prognostic than 1-minute, at < 22 bpm, hazard ratio 2.6 (95% CI 2.4-2.8)
  — a different interval on a different cohort, which is why the protocol
  travels with the number here.
- Barak OF, Ovcin ZB, Jakovljevic DG, Lozanov-Crvenkovic Z, Brodie DA, Grujic
  NG. "Heart rate recovery after submaximal exercise in four different recovery
  protocols in male athletes and non-athletes." *J Sports Sci Med*
  2011;10(2):369-375. Table 2: time constants of 52.5 (14.6) s seated inactive,
  74.1 (24.0) s seated active, 32.0 (9.1) s supine, 28.5 (5.8) s supine with
  legs raised, in athletes; 5 min of cycling at 80% of individual peak heart
  rate.
- Buchheit M. "Monitoring training status with HR measures: do all roads lead to
  Rome?" *Front Physiol* 2014;5:73. HRR₆₀ typical error ≈ 25% CV, signal-to-noise
  1.3; intensity dependence; the 3-4 min steady-state figure.
- Daanen HAM, Lamberts RP, Kallen VL, Jin A, Van Meeteren NLU. "A systematic
  review on heart-rate recovery to monitor changes in training status in
  athletes." *Int J Sports Physiol Perform* 2012;7(3):251-260. 3 of 5
  cross-sectional studies found faster HRR in trained subjects; names age,
  ambient temperature and the preceding exercise as confounders.
- Edwards S. *The Heart Rate Monitor Book*. Polar Electro Oy, 1993. The five
  zones and the weights 1..5; cross-checked against the description used in the
  validation literature, which agrees on both.
- Impellizzeri FM, Tenan MS, Kempton T, Novak A, Coutts AJ. "Acute:Chronic
  Workload Ratio: Conceptual Issues and Fundamental Pitfalls." *Int J Sports
  Physiol Perform* 2020;15(6):907-913.

---

## The boundary

`include/trainkit.h` is the whole C ABI. Rust owns when a measurement counts and
what the file says; C++ owns the clock, the sensor and the filesystem.

`Detector` and `History` cross as opaque storage the caller owns — neither has a
field a Service has any business touching. `Session` and `Recovery` cross as
themselves, field for field, and both sides compute an FNV-1a fingerprint over
the layout their own compiler produced. A Service refuses to start on a
disagreement, because a stale `libtrainkit.a` is otherwise silent until it
writes a file whose numbers are in the wrong fields — and that file is one
another app reads.

## What it costs

Spin's Service, built by CI's toolchain image, before and after linking this:

```
             .text    .data     .bss     .uapp
without      68,584    1,732   11,776   121,800
with         91,128    1,732   12,096   144,840
             +22,544       0     +320   +23,040
```

The RAM cost is the 192-byte detector blob in `.bss` and nothing else — the
16 KiB read buffer and the 1,920-byte log are heap, transient, and only alive at
the end of a session.

The flash cost is mostly not this crate's own code: `History::load` and
`History::save` are 1,532 and 1,824 bytes and `Detector::second` is 636, so
about 6 KB is TrainKit, and the rest is `compiler_builtins` (Rust's own
`memcpy`, `memmove` and the integer/float helpers) coming in with the first Rust
archive a Service has ever linked. A second app adopting the crate therefore
pays much less than the first one did.

Measured, so it can be re-measured: build the app in the toolchain image and run
`arm-none-eabi-size` on `build/SpinService.elf`.

## Building and testing

```sh
cd TrainKit
cargo test --features std                              # 50 tests: 23 gates, 24 schema and bounds, 3 ABI
cargo build --release --target thumbv8m.main-none-eabihf
```

`--features std` is required: the crate is `no_std` for the watch, and a test
binary cannot link one whose panics do not unwind.

An app links it by adding four things to its CMake — the crate as a
`add_custom_target` running cargo, `cpp/SharedLog.cpp` to `SERVICE_SOURCES`,
`include/` and `cpp/` to `SERVICE_INCLUDE_DIRS`, and the archive to
`target_link_libraries` — and by defining `trainkit_host_panic`. See
`Spin/Software/Apps/Spin-CMake/CMakeLists.txt`.

The tests also run under `ctest` from `Spin/Tests`, so a Spin developer running
the host suite covers them without knowing they are Rust.
