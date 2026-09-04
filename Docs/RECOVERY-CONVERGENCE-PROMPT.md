# Two crates measure heart-rate recovery. Work out what should exist instead.

This repository now contains **two independent implementations of the same
measurement**, written in parallel without either knowing about the other:

- [`TrainKit`](../TrainKit) — on `feat/hr-recovery`, serving `Spin`.
- [`EffortKit`](../EffortKit) — on `feat/effortkit`, serving `Squash`, and whose
  own README already names Spin as an intended consumer.

Both are Rust, `no_std`, allocation-free, no SDK types. Both detect a fall in
heart rate across a rest window, gate it on preconditions, and persist a bounded
per-session history. Neither is obviously wrong. They disagree on almost every
design question that matters, and each has caught defects the other still has.

**Your job is not to pick a winner.** It is to work out, from the evidence, what
this repository should end up with — which may be one of them, a merge, or a
third thing neither author saw. Then to say what it would cost and what would
have to be given up.

---

## 0. The honesty contract, which outranks everything here

This repository's standing rule, from `CLAUDE.md`:

> **Prefer measurement over assertion.** If you are choosing a number, measure it
> and record what you measured.

and EffortKit's sharper restatement of it:

> Nothing reaches the screen, the FIT file or the summary that cannot be defended
> with a measurement taken from this repository's own recordings.

Apply both to your own conclusions. A recommendation of the form "X is cleaner"
is worth nothing here. A recommendation of the form "X, because the recordings in
`Squash/Tests/pulled` show Y" is worth something. Where you cannot measure, say
you are asserting, and say what would settle it.

**Do not trust this document's framing.** It was written by the author of
`TrainKit`, immediately after reading `EffortKit` for the first time and finding
two defects in their own work. That is a conflict of interest in the direction of
over-correcting *towards* EffortKit as much as away from it. Read both crates'
source before you accept any comparison below.

---

## 1. The two case studies

Read `TrainKit/README.md` and `EffortKit/README.md` in full first. They are both
design records, not summaries. What follows is a map, not a substitute.

### Where they agree, without having consulted each other

Convergent design under the same constraints is itself evidence, so note it:

- A fall in bpm over a stated interval, **never a fitted slope**, on this
  hardware.
- Windows that fail their preconditions are **discarded and counted**, never
  recorded as small numbers.
- Short rests and long ones are **different measurements** and are never
  averaged together.
- A bounded rolling history of **20 sessions**, oldest evicted.
- A `.tmp` written then renamed over the live file, with `flush()` and `close()`
  folded into the result.
- An **FNV-1a fingerprint** over the C ABI struct layout, computed on both sides,
  refusing to start on a mismatch.
- Everything keyed on a timestamp rather than a count of calls.

Both authors arrived at all seven independently. Treat any proposal that
abandons one of them as needing an argument.

### Where they disagree

| Question | `TrainKit` | `EffortKit` |
|---|---|---|
| **What opens a window** | The wearer pressing pause. A button. | A hysteresis segmenter over IMU epochs deciding `Rally`/`Rest`/`OffCourt`. |
| **Uncalibrated behaviour** | Ships thresholds from published protocols with citations; produces numbers on the first ride. | `Calibration::Absent` → `Err(NotCalibrated)`; produces nothing until local recordings have set the thresholds. |
| **Threshold provenance** | `pub const` with the citation in a doc comment. | A `Provenance` struct that the threshold constructor *cannot be called without*. |
| **Where the C ABI lives** | In the shared crate (`crate-type = ["staticlib","lib"]`), with detector state as an opaque blob the C++ side sizes and aligns. | In a thin **per-app shim crate** (`Squash/Software/Libs/rust/`, 812 lines) depending on the shared crate as a pure `lib`, with all state `static` in the shim. |
| **Where the record goes** | `../SharedData/<app>_sessions.json` — cross-app by design, schema documented as a contract for other readers. | A profile file **owned and written by the app**, explicitly not shared. |
| **At the size bound** | Drops the **oldest** entry and re-serialises until it fits, so the newest session always lands. | **Refuses the write whole** unless everything fits. |
| **Numeric precision on the wire** | `hr_avg` as a rounded `u8`. | `hr_mean_x100`, fixed point. |
| **Discards** | Counted only in a diagnostic text log that gets deleted. | `Discarded` — a struct of per-reason counts, "an output in its own right rather than a debug field". |
| **Comparison across sessions** | None. Stores inputs; shows nothing. | Median and MAD over 20 sessions, ≥5 before comparing, ≤10% movement per session. |

### The third copy nobody mentions

`SleepLab/Software/Libs/Header/Engine/` already ships `BaselineStore.hpp`,
`EpochCounter.hpp` and `WornGate.hpp` — epoch reduction and robust per-user
baselines, written independently in C++ before either crate existed. It works and
ships. EffortKit's README declines to migrate it, on the grounds that rewriting a
shipped sleep engine buys nothing.

**Three independent implementations of baseline machinery is the actual finding.**
Any recommendation should say what stops a fourth.

---

## 2. Settled by measurement — do not re-derive these

Each of these cost a session to establish. They are inputs, not open questions.

### The heart-rate stream, from `Squash/Tests/pulled/*_hr.csv`

34 minutes, 2,021 samples, six sessions, one wearer, squash. Re-derive with
`TrainKit/Tools/hr_analyse.py` and `hr_source.py`.

| Measured | Value |
|---|---|
| Untrusted seconds | **5.3%**, confirming the 5% Spin measured independently over two rides |
| Untrusted run length | median **1**, max **4**, none ≥ 6 |
| Sample interval | median **1005 ms**, occasionally ~2 s, sometimes duplicate timestamps |
| Consecutive change | mean **0.49 bpm**, median **0**, **65% of steps are exactly zero**, every value a whole bpm |
| 60 s windows beginning and ending on **different sensors** | **14%** |
| Optical vs external where both report | median **2 bpm**, p95 **16**, max **23** |
| Largest 60 s falls observed | 8–20 bpm, at 64–110 bpm |

Two consequences already acted on, and a third still open:

- The stream is **integer-quantised and mostly repeats**. A fall of 8–20 bpm is
  8–20 quantisation steps — ample for a difference, marginal for a fitted time
  constant. EffortKit's `Formulation::NotMeasurableOnThisHardware` exists for
  this; TrainKit stores a 7-point curve and has stopped promising a fit from it.
- A window spanning a **sensor switch** measures the gap between two instruments
  as well as the fall. TrainKit discards those; EffortKit records the source and
  offers `SourcePolicy::ExternalOnly`. **Neither approach has been validated
  against a recording where the wearer was actually working hard.**
- These recordings are **squash at 64–110 bpm**. They say nothing about any
  intensity gate. Do not use them to justify one.

### The filesystem

- **FatFs refuses a rename onto an existing path.** Proven by Squash
  (`SquashEngine.cpp:89`); TrainKit had the latent bug and fixed it after reading
  that line.
- **The SDK's in-memory test double is more permissive than FatFs** — it lets a
  rename overwrite. A suite running against it passes code the watch refuses.
  `Spin/Tests/SharedLog_test.cpp` narrows the double for that one call.
- `Docs/INSTALLING.md` is not optional: `Apps/app_list.json` is the kernel's
  **output**, a stale `.uapp` keeps the old build booting, a USB replug is not a
  reboot, and every one of those fails silently.

### What the first desk check found

`Spin/Docs/RECOVERY-FIELD-TEST.md`, 2026-09-03. `heartRateCount` was misread, so
`mSystemMaxHr` came back 0, so **every window in both runs was discarded
`no_max_hr`** and the whole planned field test would have produced nothing. Also:
the `.fit` and the shared log disagreed about the same ride's mean heart rate,
65 against 66, because one truncated and the other rounded.

Both are the kind of defect a design review does not catch and an hour on
hardware does. Weight your recommendation accordingly.

---

## 3. The questions that are actually open

For each, the strongest case for both sides is given. Do not treat the order as a
hint.

### 3.1 One crate or two?

**For one:** the seven convergences above say this is one problem. Three copies
of baseline machinery already exist. A schema that two apps write must be one
schema or `SharedData` is pointless.

**For two:** the window sources are genuinely different — a button press and an
IMU segmenter share no code path. Squash needs epoch reduction Spin will never
run; Spin needs a cross-app file Squash does not want. Merging may produce a
crate whose every consumer uses half of it, which is how `UNA_SDK_SOURCES_SERVICE`
became something `Spin/Software/Apps/Spin-CMake/CMakeLists.txt` had to work
around.

Whatever you conclude, name the **module boundary** precisely, and say which
existing file moves where.

### 3.2 Calibrated-or-nothing, or published-definition?

**For EffortKit's rule:** it is the repository's own stated rule, mechanically
enforced by a type. A threshold with a citation in a comment can be separated
from its evidence by a refactor; one that cannot be constructed without a
`Provenance` cannot.

**Against applying it everywhere:** a segmentation threshold and a measurement
interval are not the same kind of number. Nobody can know what accelerometer
magnitude means "rally" without recording it. But HRR₆₀'s 60 seconds is not a
tunable — it is the definition of the quantity, fixed by the literature. Refusing
to report it until a local recording "sets" it may be a category error.

Decide whether `Provenance` should distinguish **a number this repository
measured** from **a number a paper defines**, and if so, how a reader tells them
apart.

### 3.3 Where does the C ABI live?

Per-app shim (EffortKit) versus in the shared crate (TrainKit). Consider: which
accumulates every consumer's ABI in one place; where `static` state can live given
no allocator and one thread; how many `#[panic_handler]`s each arrangement
implies; and whether an opaque blob sized and aligned by a runtime check is
carrying its weight.

### 3.4 App-private profile, or cross-app `SharedData`?

The brief that started this (`Spin/Docs/RECOVERY-PROMPT.md`) asked for a record
another app could build on. EffortKit's profile deliberately is not one. Is
cross-app persistence a requirement, a nice-to-have, or a mistake? If it is a
requirement, what stops two apps writing the same file, and what does a reader do
with sessions from four sports?

### 3.5 Discards: log, stored output, or both?

TrainKit built per-reason counters into the schema, then reverted them for a text
log on instruction. EffortKit made them a first-class output independently. They
may not be alternatives: a log is a diagnostic that gets deleted, counts are a
permanent property of the session. Say which question each answers, and whether a
session that measured nothing should be able to say why **a year later**.

### 3.6 The migration

Whatever you propose, files already exist on at least one watch. Say what happens
to `spin_sessions.json` and to Squash's profile: read-and-convert, refuse, or
discard. Both formats already carry a `version`/`schema` integer and both already
specify refusing a version they do not know — which constrains you.

---

## 4. Traps

- **Do not converge on the union.** A crate carrying both a hysteresis segmenter
  and a pause-driven window, both an app-private profile and a shared one, is
  worse than either. If a feature survives, say which consumer needs it.
- **Do not confuse "no C++" with "better".** Rust cannot call the SDK's C++
  virtual interfaces without a shim written in C++ anyway. Both crates put file
  I/O in C++ for that reason and both are right to.
- **Do not propose a rewrite of `SleepLab`.** It ships. If your design has a
  place for its baseline machinery, say so and stop there.
- **Beware the test double.** Anything you conclude from a host test is a claim
  about logic, not about FatFs. Say which of your recommendations only hardware
  can confirm.
- **Comments obey `CLAUDE.md`'s ownership test.** Do not propose comments naming
  what another file does.
- **A measurement without its protocol is not a measurement.** Both crates
  already record window length, trusted-sample count and intensity beside every
  number. Anything you drop from that list, justify.

---

## 5. What to produce

1. **A recommendation**, in one paragraph, that someone could act on.
2. **A table of what moves where** — every file in both crates, and its fate.
3. **What is given up.** Every design in §1 that loses, and the cost of losing
   it. If the answer is "nothing is given up", you have not understood one of
   them.
4. **The questions you could not settle**, and the recording or the hardware
   session that would settle each.
5. **A migration**, concrete enough to implement.

Do not write code. This is a design pass, and both branches are live —
`feat/hr-recovery` and `feat/effortkit` are both being pushed to.

---

## 6. Out of scope

- HRV and rMSSD, until the beat-to-beat pathway lands in a released SDK.
- Any on-watch trend view. The record is for a later reader.
- Changing what the `.fit` contains. Its field numbers are verified against two
  independent copies of the FIT profile and a wrong one is silent and permanent.
- Population norms, rankings, and anything phrased as advice about a person.

## 7. Where the evidence is

```sh
# The two crates
TrainKit/README.md            EffortKit/README.md
TrainKit/src/recovery.rs      EffortKit/src/recovery.rs
TrainKit/cpp/                 Squash/Software/Libs/rust/src/lib.rs

# What the hardware actually did
Squash/Tests/pulled/*/imu_*_hr.csv
Spin/Docs/RECOVERY-FIELD-TEST.md      # the 2026-09-03 desk check
Docs/INSTALLING.md                     # four failed installs, written up

# Re-derive every number in section 2
python3 TrainKit/Tools/hr_analyse.py
python3 TrainKit/Tools/hr_source.py

# Both suites must still pass whatever you propose
cd EffortKit && cargo test
cd TrainKit  && cargo test --features std
cmake -B build -S Spin/Tests && ctest --test-dir build
```

## 8. Definition of done

- [ ] Both crates read in full, not just their READMEs.
- [ ] Every claim in §2 either used or explicitly disputed with evidence.
- [ ] A recommendation that names what it gives up.
- [ ] Every file in both crates accounted for.
- [ ] The unsettled questions listed, each with the experiment that settles it.
- [ ] Nothing recommended that a `no_std`, no-allocator, one-thread Service
      cannot run.
