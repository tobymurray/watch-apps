# Two crates measure heart-rate recovery — what should exist instead

A design pass over [`TrainKit`](../TrainKit) (`feat/hr-recovery`) and `EffortKit`
(`feat/effortkit`), answering [`RECOVERY-CONVERGENCE-PROMPT.md`](RECOVERY-CONVERGENCE-PROMPT.md).
No code was written. Both branches were live when this was taken; the commits
read are `5d9f1aa` and `dcb397b`.

Everything below marked **measured** was re-derived in this pass and the command
is given. Everything marked **asserted** is a judgement, and says what would
settle it. The flash-cost question was built on the real ARM toolchain in the CI
image rather than argued (§0.6).

---

## 0. What this pass measured

Before any comparison, the prompt's §2 was re-derived rather than accepted, and
both crates were probed rather than only read.

### §2 reproduces exactly

`python3 TrainKit/Tools/hr_analyse.py` and `hr_source.py`, over the six
recordings in `Squash/Tests/pulled/`:

| Claim in §2 | Re-derived | Verdict |
|---|---|---|
| 2,021 samples, 34 min, six sessions | 2,021 / 34 min / 6 | confirmed |
| Untrusted seconds 5.3% | 5.3% (107 of 2,021) | confirmed |
| Untrusted runs median 1, max 4, none ≥ 6 | n=89, median 1, max 4, ≥6: 0 | confirmed |
| Sample interval median 1005 ms | median 1005, p95 1007, max 2117 | confirmed |
| Consecutive change mean 0.49, median 0, 65% zero | 0.49 / 0.00 / 65% | confirmed |
| 60 s windows crossing sensors: 14% | 236 of 1,666 = 14% | confirmed |
| Optical vs external median 2, p95 16, max 23 | 2.0 / 16.0 / 23.0 (479 pairs) | confirmed |
| Source-crossing windows averaged −2.1 bpm | −2.1 (min −14, max +7) | confirmed |
| "every value a whole bpm" | all 2,017 `bpm_x100` values ≡ 0 mod 100 | confirmed |

Nothing in §2 is disputed. One thing is added: the *arbitrated* stream, the
optical stream and the external stream are **each** whole-bpm. The quantisation
is not an artefact of arbitration.

### Both suites pass, both crates build for the watch

```
TrainKit  cargo test --features std        53 passed  (3 ABI, 24 history, 26 recovery)
EffortKit cargo test                       54 passed
both      cargo build --release --target thumbv8m.main-none-eabihf    ok
```

TrainKit's README says "50 tests: 23 gates, 24 schema and bounds, 3 ABI". It is
26 gates, not 23. `CLAUDE.md` names counts of tests as a thing a file cannot
keep true, and this is why.

### Four probes against EffortKit, three of which found defects

Written against the crate as it stands, run, then deleted. Reproduce by pointing
a `tests/` file at `effortkit` with `Calibration::Measured` and a 60 s window.

| Probe | Result |
|---|---|
| A window whose first half is optical at 100 bpm and second half external at 116 bpm — **no instrument moves at all** | **ACCEPTED.** Reported `drop = −16 bpm`, `source = External`. |
| One `NaN` bpm arriving trusted at the end of a window | **ACCEPTED.** `drop = NaN`, `mean_drop = NaN`. |
| A rest entered at 70 bpm falling 8 bpm | **ACCEPTED**, indistinguishable in the profile from 8 bpm from 175. |
| 20 s of untrusted readings at the start of the window | **discarded** as `hr_gap`. Correct — `last_trusted_ms` is seeded at window open. |

The fourth is worth stating because it clears a suspicion: EffortKit does *not*
have a late-baseline hole. TrainKit's `BASELINE_GRACE_S` and EffortKit's
`max_gap_ms` cover the same failure by different means, and both work.

The first three matter, and the first two are defects by this repository's own
standard — silent, permanent, and they look fine locally.

**The NaN reaches the file.** Probed end to end: a NaN mean goes through
`hundredths()` → `roundf(NaN * 100.0) as i64`, which in Rust saturates to `0`,
and is written as `"rec_short_x100":0` beside `"rec_short_n":5`. It parses back
as `Some(0.0)`. The file then claims *five qualifying windows measured a mean
fall of zero*. There is no way to tell that from a real zero, ever.

TrainKit is immune: `clamp_bpm` maps NaN to `0`, which reads as "no trusted
reading" everywhere in the file.

**The source-change window is the 14% case.** This is not hypothetical — 236 of
1,666 real 60 s windows in the pulled recordings begin and end on different
sensors, and those windows averaged an apparent *rise*. EffortKit records the
last source seen, so a window that was half optical is stamped `External`. The
recorded source is not merely incomplete; it is wrong about the window.
TrainKit discards these (`DISCARD_SOURCE_CHANGED`) and its README cites the same
14%.

### Two more, found by probe, not by reading

**EffortKit records a duplicate session twice.** `Profile::record` appends
unconditionally; probed, two records with the same `started_utc` both land. A
write retried after a failure therefore votes twice in every baseline — 5–10% of
a 20-session window. TrainKit's `History::add` replaces on `start_utc` identity,
with the reasoning written down: "a retry cannot make one ride into two."

**EffortKit's 10% baseline step bound protects nothing that ships.** Probed:
after twenty sessions at 100 and twenty at 10, `baseline()` returns `31.38` and
`median()` returns `10.0` — the bound works. But `compare()` uses `raw_median()`,
and `squash_profile_compare` — the only path the C++ side can reach — calls
`compare()`. So the bound is live in a function no consumer reads. The README
spends a paragraph justifying it as "the second lock" and computing that a
genuine halving takes seven sessions; that paragraph describes a value the watch
never sees.

### One measured number in TrainKit is stale

`tests/history.rs` asserts the widest 20-session log is **14,220 bytes** and the
doc comment on `MAX_SESSIONS` agrees. `README.md` says **13,420**. The test
passes, so the README is wrong by 800 bytes.

It matters because the entry cap rests on it. The README's "at 24 entries the
worst case was 16,084 of 16,384" is arithmetically consistent with 13,420
(671 B/session) and **not** with 14,220 (711 B/session, which puts 24 sessions at
about 17,060 — over the cap). The conclusion "the cap is 20" survives. The stated
margin does not, and anything added to the schema has to re-run that test rather
than trust the README.

### The evidence is split across the two branches and neither is self-sufficient

- `Squash/Tests/pulled/*_hr.csv` exists **only on `feat/effortkit`**.
- `TrainKit/Tools/hr_analyse.py`, which reads those files, exists **only on
  `feat/hr-recovery`**.
- `Docs/INSTALLING.md` exists **only on `feat/effortkit`**. The field test links
  it three times and `Service.cpp` references it; its own "Still open" section
  records that it does not exist.

§7 of the prompt ("Re-derive every number in section 2") is not runnable on
either branch alone. This pass had to combine them. That constrains the migration
order, below.

### 0.6 The merge's flash cost, measured on the real toolchain

The prompt's §3.1 warns that merging may produce "a crate whose every consumer
uses half of it". That is answerable with a number rather than an opinion, and
the ARM toolchain is in the CI image, so it was.

Built in `ghcr.io/tobymurray/watch-apps-toolchain@sha256:cca44e2c…` against the
SDK at its pinned `8cdb7314`, `arm-none-eabi-size build/SpinService.elf`:

| Spin's Service | `.text` | `.data` | `.bss` |
|---|---|---|---|
| As it ships today (TrainKit only) | 91,624 | 1,732 | 12,120 |
| With EffortKit linked, **never called** | **91,624** | 1,732 | 12,120 |
| With `epoch` + `segment` + `baseline` **reachable** | 95,600 | 1,732 | 12,120 |

**The unused half costs zero bytes** — byte-for-byte identical, not
approximately. **The control proves it is the linker and not an empty module:**
making the same three modules reachable from an exported entry point costs
**+3,976 bytes** of `.text`. `.bss` is unchanged in all three, because the probe
holds no static state.

Reproduce: add `effortkit` as a path dependency of TrainKit, build Spin in the
image above, then force reachability by calling into `epoch`/`segment`/
`baseline` from a function Spin's C++ already calls — an unreferenced
`#[no_mangle]` export is dropped at ELF link and measures nothing.

This settles §3.1's cost objection and gives Squash a budget figure: the
segmenter, epoch reduction and baseline together are about 3.9 KB of flash.

**One correction it produced.** TrainKit's README records the "with" row as
91,128 / 1,732 / 12,096 / 144,840 `.uapp`. Today it is 91,624 / 1,732 / 12,120 /
145,348 — the four fix commits since (`ZoneLadder`, the rounding fix, the log
timestamp, the saturating `avg_power`) account for the drift. The shape of the
claim holds; the numbers are stale, like the byte figure above.

### 0.7 CI does not rebuild Spin when TrainKit changes

Found while locating the build command, and it compounds the merge.
`.github/workflows/spin.yml`'s paths filter lists:

```yaml
- 'Spin/**'
# The renderer's text comes from the shared crate; a change there is a
# change to this app's binary.
- 'TextKit/**'
```

`TrainKit/**` is absent — although `app-build.yml` already detects TrainKit
(`grep -rq 'TrainKit' "$cmake"`) and runs its `cargo test`, and although §0.6
just demonstrated that a TrainKit change moves `SpinService.elf`'s `.text`. The
comment justifying `TextKit/**` applies to TrainKit word for word.

So today a TrainKit-only commit ships without Spin being built or its host suite
run. **After the merge this gets worse, not better**: one crate would back two
apps, so the shared directory has to appear in *both* `spin.yml` and
`squash.yml`. Fixable in two lines per app and worth doing on
`feat/hr-recovery` now, independently of any merge.

### The fact that outranks the rest

`Spin/Docs/RECOVERY-FIELD-TEST.md`, 2026-09-03: **no recovery window has ever
armed on hardware, in either crate.** Both runs discarded every window
`no_max_hr` because `heartRateCount` was misread. Rides A–E have not been done.

So every gate in TrainKit and every threshold in EffortKit is, today, a claim
about logic. Whatever is recommended here is a claim about logic too.

---

## 1. The recommendation

**One crate, with the boundary drawn at the window's source rather than at the
sport.** Fold both into a single `no_std` library — call it EffortKit, which is
the better-scoped name — organised as: a shared *measurement* layer (the gated
fall, the trusted-sample arithmetic, the discard reasons), a shared *record*
layer (the bounded versioned store and the median/MAD baseline), and *producer*
modules that decide when a window opens, of which there are two and each app
links one — a button for Spin, the IMU segmenter for Squash. The measurement
takes `cease(trigger)` / `resume()` as input and does not know which produced it,
which is what makes this a merge rather than a union: the segmenter is in the
crate but not on Spin's path, and LTO drops it. Take **TrainKit's gates,
NaN handling, source-change rule, `start_utc` identity, drop-oldest-and-retry
bound and two-sided ABI fingerprint**; take **EffortKit's `Discarded` as a
first-class output, its median/MAD baseline, its per-app shim holding all state
`static`, and its `Provenance` type** — but split `Provenance` into `Measured`
and `Defined`, because HRR₆₀'s 60 seconds is the definition of the quantity and
not a tunable, and refusing to report it until a local recording "sets" it is a
category error. Keep **both files**, with different jobs that neither author
separated: `../SharedData/<app>_sessions.json` carries the *series* — one
comparable measurement per session with its protocol beside it, cross-app,
versioned, documented — and the app-private profile carries the *session's
interior*, the 256 rally windows and the within-session trend that no other app
will ever read. EffortKit put the series in the private file; TrainKit had no
interior at all.

---

## 2. What moves where

Every file in both crates. Target layout is `EffortKit/` for the shared crate
and `<App>/Software/Libs/rust/` for each app's shim.

### TrainKit

| File | Fate |
|---|---|
| `src/recovery.rs` | **Merge → `src/window.rs`.** The gates are the asset: intensity floor, effort bout, already-falling, pre-history, baseline grace, dropout, source-change, endpoint grace. `Detector` keeps its shape. `Step::Discarded` gains EffortKit's per-reason struct instead of a single `u8`. |
| `src/record.rs` | **Move → `src/record.rs`.** `Recovery` unchanged. `Session` gains discard counters (§3.5) and loses nothing. |
| `src/history.rs` | **Move → `src/history.rs`.** The bounded, versioned, `start_utc`-identified store. Schema goes to 2. |
| `src/json.rs` | **Move → `src/json.rs`.** It has a reader *and* a writer; EffortKit's `profile.rs` has a weaker ad-hoc pair (`find_quoted` scans the whole buffer per key). This one wins and `profile.rs`'s serialiser is deleted in its favour. |
| `src/load.rs` | **Move → `src/load.rs`.** Edwards' TRIMP and the power arithmetic. Spin-shaped but sport-neutral; no other consumer today. |
| `src/lib.rs` | **Dissolve.** The `abi_fingerprint()` walk moves to the Spin shim (§3.3). The `#[panic_handler]` moves to the shim. The shared crate carries neither. |
| `src/ffi.rs` | **Delete, re-authored as `Spin/Software/Libs/rust/src/lib.rs`.** Same functions, state `static` rather than in a caller-owned blob. |
| `include/trainkit.h` | **Move → `Spin/Software/Libs/Header/SpinEngine.hpp`.** The `constexpr fingerprint()` and the `static_assert` block come with it unchanged — they are the strongest thing in either crate's ABI and Squash's side needs them too. |
| `cpp/SharedLog.cpp`, `.hpp` | **Move → `Spin/Software/Libs/`.** File I/O in C++ is right (both crates agree). The `.bak` rotation before the commit rename is kept and Squash adopts it. |
| `cpp/EventLog.cpp`, `.hpp` | **Move → `Spin/Software/Libs/`, and mark for deletion.** The field test already says "it is a diagnostic, not a feature — once these questions are answered it should come out." It cannot come out until a window has armed on hardware. |
| `tests/recovery.rs` | **Merge → the shared crate's tests.** 26 gate tests; the most valuable artefact in either branch. |
| `tests/history.rs` | **Move.** Re-run and re-record the widest-log assertion after the schema change. |
| `tests/abi.rs` | **Move → the Spin shim's tests.** |
| `Tools/hr_analyse.py`, `hr_source.py` | **Move → `Tools/`** at the repository root. They read `Squash/Tests/pulled/` and belong to neither app. |
| `README.md` | **Merge into the shared crate's README.** The sources section, the "what this cannot tell you" section and the gate table are the design record and survive whole. The stale byte and test counts are corrected. |
| `Cargo.toml` | **Delete.** `crate-type` becomes `["lib"]` on the merged crate. |

### EffortKit

| File | Fate |
|---|---|
| `src/recovery.rs` | **Partly merge, partly delete.** `HrSource`, `HrSample`, `SourcePolicy`, `Discarded`, `WindowKind`, `Formulation` and `Trend` survive. `RecoveryAnalyser`'s state machine is **replaced** by TrainKit's `Detector`: it is the one with the gates, and the probes show why. `MAX_WINDOWS = 256` moves to the app-private profile. |
| `src/segment.rs` | **Move unchanged → `src/segment.rs`.** Squash's producer. Nothing in Spin calls it. |
| `src/epoch.rs` | **Move unchanged → `src/epoch.rs`.** Same. |
| `src/baseline.rs` | **Move → `src/baseline.rs`,** with one change: `compare()` must read `baseline()` or the step bound must be deleted. Shipping both is shipping a paragraph of README that is not true. |
| `src/session.rs` | **Move → `src/session.rs`.** `Metric` and its level/rate/ratio/total distinction is the answer to "what does a reader do with sessions from four sports" and is promoted into the shared schema. |
| `src/profile.rs` | **Split.** The `Profile` container and `MAX_SESSIONS` survive as the app-private interior record. Its serialiser is deleted in favour of `json.rs`. The recovery *aggregates* (`rec_short_x100`, `rec_long_x100`) are deleted from the wire — see §3. |
| `src/lib.rs` | **Move → `src/lib.rs`.** `Provenance` gains two variants (§3.2). `Unavailable` unchanged. |
| `src/fixture.rs` | **Move unchanged.** Reads a recording as a host-test input; `std`-gated; the right shape. |
| `src/bin/phase_a.rs` | **Move unchanged.** The Phase A measurement tool. Still unrun against a step change in effort, which is open question 3 below. |
| `Cargo.toml` | **Becomes the merged crate's.** `crate-type = ["lib"]` already; keeps `libm`, keeps the `phase-a` binary behind `std`. |
| `README.md` | **Merge.** "The session is not a match", the comparability table and the profile-file rationale survive. The 10% paragraph is corrected or the code is. |
| `Squash/Software/Libs/rust/src/lib.rs` | **Stays where it is.** 812 lines, and the arrangement is the recommendation (§3.3). Gains a `constexpr`-computed fingerprint on the C++ side. |
| `Squash/Software/Libs/rust/Cargo.toml` | **Stays.** `crate-type = ["staticlib","lib"]`, `effortkit` as a dependency. This is the shape Spin copies. |
| `Squash/.../SquashEngine.{hpp,cpp}` | **Stays.** The commit sequence adopts TrainKit's `.bak` rotation. |

### SleepLab

| File | Fate |
|---|---|
| `Engine/BaselineStore.hpp`, `EpochCounter.hpp`, `WornGate.hpp`, and the rest | **Untouched.** It ships. EffortKit's README already declined to migrate it and was right. |

**What stops a fourth copy** is the thing to write down, because it is the actual
finding. Not a rule in `CLAUDE.md` — the third copy was written by an author who
had already written the first. It is that `EffortKit/src/baseline.rs` is now the
only place in the repository where a *new* rolling median baseline can be added
without deleting a shipped one, and `SleepLab/Software/Libs/Header/Engine/README`
should say so in one line pointing at it. The convergence is already striking:
SleepLab's `kMinNights` is 5, EffortKit's `MIN_SESSIONS_FOR_COMPARISON` is 5,
both are medians not means, both refuse to report before the threshold, both
compare a wearer only against themselves. Two authors, two languages, one
answer.

---

## 3. The open questions, answered

### 3.1 One crate or two — one, and here is the boundary

The seven convergences are evidence. So is a thing the prompt does not list:
**each crate has caught a defect the other still has**, and the count is not
symmetric. TrainKit catches four in EffortKit (source change, NaN, duplicate
session, dead step bound). EffortKit catches two in TrainKit (discards are not
persisted, no cross-session comparison exists at all). Six defects that a merge
removes and a fork keeps.

The "for two" argument is real but it is an argument about *one module*, not
about the crate. The window sources genuinely share no code path — but they do
not need to, because neither is the measurement. Drawn as:

```
lib.rs        Provenance{Measured|Defined}, Unavailable
hr.rs         HrSample, HrSource, clamp/trust           every consumer
window.rs     the gated fall + Discarded                every consumer
record.rs     Recovery, Session on the wire             every consumer
history.rs    the bounded versioned SharedData store    every consumer
json.rs       reader and writer                         every consumer
baseline.rs   rolling median/MAD                        Squash today
session.rs    Metric, admission rules                   Squash today
epoch.rs      IMU → one feature vector a second         Squash only
segment.rs    hysteresis Rally/Rest/OffCourt            Squash only
fixture.rs    recording → host-test input      std only
bin/phase_a   the measurement tool             std only
```

Spin calls `window.rs` from a button. Squash calls it from `segment.rs`. The
`UNA_SDK_SOURCES_SERVICE` trap the prompt warns about is a crate whose consumers
must *work around* what they do not use; here they simply do not link it.

**Measured, in this pass** (§0.6): the flash cost to Spin of the modules it does
not call is **zero bytes**, and the modules are not trivially empty — calling
them costs 3,976. The "crate whose every consumer uses half of it" objection is
answered by the linker.

### 3.2 Calibrated-or-nothing, or published-definition — both, distinguished by type

This is the question where neither author is wrong and the answer is a third
thing.

EffortKit's rule is correct for a **segmentation threshold**. Nobody can know
what accelerometer magnitude means "rally" without recording it, and a `const`
with a comment is separable from its evidence by a refactor.

It is a category error for a **measurement interval**. HRR₆₀'s 60 seconds is not
tuned to this hardware or this wearer; it is what the quantity *is*, and it
travels with its protocol through Cole et al. and Shetler et al. A local
recording cannot "set" it — a recording that suggested 47 s would not have
measured a better HRR₆₀, it would have measured something else.

So:

```rust
pub enum Provenance {
    /// A number this repository measured, and the recordings that set it.
    Measured  { recordings: &'static str, measured_on: &'static str, method: &'static str },
    /// A number a published protocol defines, and the quantity it defines.
    Defined   { citation: &'static str, defines: &'static str },
}
```

A threshold still cannot be constructed without one. `Calibration::Absent →
Err(NotCalibrated)` **stays for segmentation** and **goes for the fall**: Spin
produces numbers on its first ride, Squash segments nothing until A2 has run.

A reader tells them apart by the variant, and the discipline that "nothing
reaches the screen that cannot be defended" is preserved with its scope
corrected — a `Defined` number is defended by its citation, which is a
defence, just not a local one.

**What is given up, plainly:** the discipline stops being mechanical. Today
EffortKit is safe because *there is no constructible calibration in the
repository at all* — the type system alone enforces it. With `Defined` available,
an author can write `Defined { citation: "well known", defines: "recovery" }` and
the compiler will accept it. That is a real loss and no type recovers it; it
becomes a review question. The trade is worth taking only because the
alternative — Spin shipping nothing until squash recordings have "calibrated"
a number from the cardiology literature — is worse.

### 3.3 Where the C ABI lives — the per-app shim, with TrainKit's fingerprint

**Per-app shim, decisively.** The evidence is in the repository already:
`squash_engine/Cargo.toml` carries "A dependency, never a second staticlib: two
archives each carrying a `#[panic_handler]` collide at link time" — a collision
they hit, not one they imagined.

TrainKit's `crate-type = ["staticlib","lib"]` works today because Spin's Service
links exactly one Rust archive. It makes TrainKit the **only** Rust archive that
Service can ever link: anything else Rust-shaped that Spin's Service wants must
go inside TrainKit or not exist. The shim moves that chokepoint to a file the app
owns, where growing it is a local decision. One `#[panic_handler]` per Service
binary either way — but in the shim it sits beside the app-specific panic hook it
has to call anyway.

EffortKit also found the constraint that decides where state lives, and it is
not allocation — it is the stack. `Session` is 12,656 bytes and the Service stack
is 10,240; the watch took an `STKOF` UsageFault (`CFSR=0x00100000`) building one
as a value. State must be `static`, in `.bss`, constructed in place. That is what
the shim does and it is right.

**The opaque blob does not carry its weight.** With state `static` in the shim
there is nothing for a caller to size, so `trainkit_detector_bytes()` and
`trainkit_detector_align()` and the runtime check against
`TRAINKIT_DETECTOR_WORDS` all go. They exist to protect against a hazard the
other arrangement does not have.

**But take TrainKit's fingerprint, not EffortKit's.** They are not the same
mechanism and the prompt's convergence table is wrong to list them together:

- TrainKit: `include/trainkit.h` has a `constexpr fingerprint()` that walks
  `sizeof`/`alignof`/`offsetof` **as the C++ compiler computed them**, compared
  at start-up against what the Rust archive returns. Plus 30 `static_assert`s.
  A drift on *either* side is caught.
- Squash: `SquashEngine.hpp` has `static constexpr uint32_t kAbiFingerprint =
  524638087u` — a literal a human copied from a test's output. If the C++ struct
  drifts and the Rust one does not, the constant does not change, the Rust value
  does not change, and the check passes while the fields are misread.

Squash's header is honest that its `METRICS` ordering is uncovered. It is not
aware that its struct layout is uncovered too. This is cheap to fix and should
be, on `feat/effortkit`, independently of any merge.

### 3.4 App-private profile or cross-app SharedData — both, with different jobs

Cross-app persistence is a **requirement**, and `Spin/Docs/RECOVERY-PROMPT.md`
asked for it. But EffortKit is right that a profile full of rally counts and
work:rest ratios is nobody else's business. The two authors were solving
different problems and each wrote one file for both.

- **`../SharedData/<app>_sessions.json` — the series.** One row per session. The
  session totals, and the *comparable* recovery measurement with its full
  protocol beside it (`hr0`, `hr0_pct_max`, `window_s`, `trusted_s`, `source`,
  `trigger`, `curve`). Versioned, documented as a contract, one file per app so
  there is no lock to get wrong. A reader wanting every sport globs
  `*_sessions.json` and merges on `app` and `sport`.
- **`<app>/profile.json` — the interior.** Rally structure, up to 256 windows,
  the within-session `Trend`, whatever else that app segments. Never read by
  another app, so its schema is the app's own business.

**What a reader does with four sports** is already answered by a type EffortKit
built and did not export: `session::Metric`'s level / rate / ratio / total
distinction. A total is comparable within a session only; a level and a rate
cross sessions. Promote that into the schema so each field declares its kind,
and a reader merging Spin and Squash knows which columns it may put side by side.

**TrainKit's `MAX_RECOVERIES = 2` and EffortKit's `MAX_WINDOWS = 256` are not in
conflict once the files are split.** SharedData keeps 2 — the end-of-session
recovery is the one that happens every session and is therefore the comparable
one, which is TrainKit's stated reason for keeping the *newest*. The interior
file keeps 256.

**One defect this exposes in EffortKit.** Its profile persists
`rec_short_x100` — a *mean over qualifying windows* — with no per-window detail.
Its own README says baselines are "derived on load, never stored... so changing
how a figure is derived applies to the whole history instead of orphaning it."
It holds that rule for baselines and breaks it for recovery: change the window
criteria and every stored `rec_short_x100` is orphaned, because the windows it
averaged are gone. TrainKit stores each window with its inputs. TrainKit is right,
and this is the single strongest argument that the *record* layer should be
TrainKit's.

### 3.5 Discards — both, and they answer different questions

Not alternatives. The prompt's own framing is the answer and the evidence is the
desk check.

- **`recovery.log` answers "what happened in this ride, second by second".** It
  is a diagnostic, it appends across rides, it caps at 128 KiB, and the field
  test tells you to delete it. Correct, and it should still be deleted once a
  window has armed on hardware.
- **Per-reason counts answer "why does this session have no number".** That is a
  permanent property of the session and it belongs in the schema.

The desk check is the argument. Every window in both runs was discarded
`no_max_hr` — and the *only* record of that is the text log the field test
instructs you to delete. A year later, that session is indistinguishable from a
session in which nobody paused. TrainKit built these counters and reverted them
on instruction; the instruction was wrong, and EffortKit reached the opposite
conclusion independently, which is itself evidence.

**Cost, measured rather than waved at:** the widest 20-session log is 14,220 of
16,384 bytes — 2,164 spare. Six `u8` counters serialised as JSON keys is roughly
40 bytes a session, about 800 for twenty, leaving ~1,364. That fits, and it makes
the entry cap's margin thin enough that `the_widest_possible_log_fits_the_buffer`
has to be re-run and re-recorded rather than reasoned about. Emit counters only
when non-zero and the ordinary case costs nothing.

### 3.6 The migration

Both formats carry a version integer and both specify refusing a version they do
not know, which rules out silent reinterpretation. It also makes the upgrade
easy, because the constraint cuts the right way.

**`spin_sessions.json`: version 1 → 2, no conversion pass.**

The v2 schema is v1 plus fields. So:

1. `SCHEMA_VERSION` becomes 2. `load` accepts `version <= 2` and reports
   `Load::Newer` above it — the existing rule, unchanged.
2. A v1 file read by a v2 build yields sessions with **no discard counters**,
   which is truthful: they were not recorded. Absent is distinct from zero, and
   `work_kj` already establishes that convention in this schema.
3. A v2 file read by a v1 build is refused and left untouched — `Load::Newer`
   already writes nothing. A wearer who downgrades loses nothing.

No file is rewritten, no data is converted, and both directions are safe. This
works only because TrainKit specified the refusal correctly in the first place.

**Squash's profile: refuse and restart, and say what it costs.**

`Profile::parse_json` already returns `(empty, Load::UnknownSchema)` for a schema
it does not know, and the reasoning — "these are bare integers whose meaning a
later schema could change, and a wrong baseline is worse than a warm-up" — is
sound. Bump the schema and let the existing rule fire.

The cost is bounded and small **today**, and this is the window in which to do
it: no calibration constant exists, so `segmented` is false on every stored
session, so every rally-derived baseline is already empty. The only live values
in any existing profile are `hr_mean` and `hr_max`. Discarding them costs
`MIN_SESSIONS_FOR_COMPARISON` = 5 sessions of warm-up on two metrics — at one to
three sessions a week, two to five weeks — during which the raw measurement is
still shown and only the comparison is withheld. Writing a converter for two
floats costs more than that. **If a calibration lands first, this stops being
true and a converter becomes the right answer.**

**Order of operations, forced by the split evidence.**

The two branches each hold half of what the other needs, so the merge order is
not free:

1. **Land `Docs/INSTALLING.md` and `Squash/Tests/pulled/` on `main` first**,
   from `feat/effortkit`. Until then `feat/hr-recovery`'s field test links three
   times to a file that does not exist and its analysis tools have nothing to
   read. This step is independently useful and blocks nothing.
1b. **Add the shared crate to both apps' CI paths filters** (§0.7). Two lines per
   app, and it has to happen before step 3 or the merge lands untested.

2. **Fix the two probed defects on `feat/effortkit` in place**, before any merge
   — NaN and the source-change window. They are small, they are in the module
   that is about to be replaced anyway, and fixing them there means the merge
   does not have to carry "and also this was broken". Fix the hand-copied
   fingerprint at the same time.
3. **Merge the record layer**: `history.rs`, `record.rs`, `json.rs`, `load.rs`
   into EffortKit, schema at 2. No app changes yet.
4. **Merge the measurement layer**: TrainKit's `Detector` replaces
   `RecoveryAnalyser`'s state machine, keeping `Discarded`. Both test suites run
   against it; the 26 gate tests are the acceptance criterion.
5. **Re-author Spin's shim** from `squash_engine`'s shape. This is the only
   genuinely new code and it is mechanical.
6. **Delete `TrainKit/`.**
7. **Re-run the desk check on hardware** before anything else is believed.

---

## 4. What is given up

Nothing here is free, and the prompt is right that a claim of no cost means one
of the designs was not understood.

| Lost | Cost |
|---|---|
| **TrainKit's opaque detector blob and its runtime size/align check** | The C++ side stops owning detector storage. In exchange there is no size to get wrong — but the check was also the only thing that would catch a header and archive built from different revisions of the *storage* contract, and that now rests entirely on the fingerprint. |
| **TrainKit as a `staticlib`** | Spin's Service gains a shim file it does not have today, ~800 lines. Spin's *GUI*, which takes TrainKit as a `lib` dependency, is unaffected. |
| **EffortKit's mechanically-unforgeable calibration** | The strongest thing in either crate, and the merge weakens it. With `Provenance::Defined` available, discipline becomes reviewable rather than impossible to violate. Stated at length in §3.2 because it is the single largest concession here. |
| **EffortKit's "refuse the write whole" at the byte cap** | Replaced by drop-oldest-and-retry. EffortKit guaranteed that a new write never costs old data; TrainKit guarantees the newest session always lands. For a shared record another app reads, the newest landing matters more and `dropped` tells a reader the series is truncated — but a pathological session can now evict history to make room for itself, bounded only by the entry and per-session recovery caps. |
| **EffortKit's `RecoveryAnalyser`** | 717 lines, deleted. Its `Formulation`, `SourcePolicy`, `Discarded` and `WindowKind` survive; its state machine does not, because it has no intensity gate, no already-falling gate, no effort-duration gate and no source-change rule, and the probes show what that costs. |
| **EffortKit's `rec_short_x100` on the wire** | Deleted in favour of per-window records. Any existing profile's recovery aggregates are lost — see §3.6 for why that is nearly free today and will not be later. |
| **TrainKit's `recovery.log`** | Not lost yet, but marked. It is the only reason anyone knows what the desk check found, so it survives until a window has armed on hardware and then it goes. |
| **Two independent implementations** | Genuinely a loss. Two crates disagreeing is how five of the six defects in this document were found; one crate cannot cross-check itself. The 26 gate tests and `phase-a` are what has to replace that, and they are weaker. |

---

## 5. What could not be settled, and what would settle it

| Open | Why this pass could not close it | The experiment |
|---|---|---|
| **The 80%-of-maximum intensity gate** | The recordings are squash at 64–110 bpm and say nothing about intensity — §2 says so and it is right. The gate is matched to Barak et al.'s *protocol*, not derived from a threshold study, and TrainKit's own field test names it as the weakest gate. | Ride A of `RECOVERY-FIELD-TEST.md` on a build carrying the `ZoneLadder` fix, reading `pct=` at every R1 out of `recovery.log`. Re-derive the floor from the ride rather than argue it. |
| **Whether 60 s of sitting still ever survives** | No window has armed on hardware, ever. | The same ride. Count `no_endpoint` and `effort_resumed` against `recovery`. The field test already names this as "the finding that matters most" and pre-commits to the right response: say so on the paused screen, do not shorten the window. |
| **Whether the kernel's HR smoothing has a time constant comparable to the window** | This decides whether the 7-point curve is worth storing at all, and whether `Formulation::NotMeasurableOnThisHardware` is the true answer. The six pulled recordings are 64–110 bpm with no step change in effort, so they cannot answer it. | `phase-a` A1 over a recording containing a deliberate step: hard effort to abrupt stop, strap and wrist both live. This recording does not exist and is cheap to make. |
| **`ExternalOnly` vs `EitherWithSourceRecorded`** | Measured here: optical and external differ by median 2, p95 16, max 23 bpm — but at 64–110 bpm and largely at rest. The adversarial case EffortKit names (grip tension, impact shock, watch moving) is under load and unmeasured. | The same step-change recording, with both sensors reporting throughout hard play. |
| ~~The flash cost of the merged crate to a consumer using half of it~~ | **Settled in this pass — see §0.6.** Zero bytes for the half not called; 3,976 for the half that is. | — |
| **Everything about FatFs** | Every host result in this document, including all six probe findings, is a claim about *logic*. The SDK's in-memory double lets a rename overwrite; FatFs does not. | The desk check in `RECOVERY-FIELD-TEST.md`, which exists precisely for this, plus its second run for the `.bak` rotation. |

---

## 6. Out of scope, unchanged

HRV and rMSSD; any on-watch trend view; the `.fit`'s field numbers; population
norms and anything phrased as advice about a person. Nothing above touches them.

`SleepLab` is not rewritten.
