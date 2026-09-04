# EffortKit — heart-rate recovery, session structure, and a record that outlives the session

Three things, for any activity app on this watch:

1. **Measure heart-rate recovery** after effort, only when it can mean something.
2. **Keep a bounded cross-app record** in `../SharedData/`, so training load and
   other longitudinal insight can be built on it later, by the app that wrote it
   or by another one.
3. **Give a segmenting app what it needs** — epoch features, a hysteresis
   segmenter, and robust per-user baselines — without a non-segmenting app
   paying for any of it.

Rust, `no_std`, no allocator, no SDK types, no clock and no filesystem, so
everything it decides can be checked by `cargo test` without a kernel.

This crate is the merge of two independent implementations of the same
measurement, `TrainKit` (serving Spin) and the first `EffortKit` (serving
Squash), written in parallel without either knowing about the other. The design
pass that produced it is [`Docs/RECOVERY-CONVERGENCE.md`](../Docs/RECOVERY-CONVERGENCE.md);
it records what each caught in the other, what was given up, and what is still
open. Read it before changing anything structural here.

## What opens a window is an input, not a component

The measurement is told that effort ceased and told when it restarted. It does
not know what said so:

| App | What calls `cease()` | Window kinds it measures |
|---|---|---|
| Spin | the wearer pressing pause | `Pause` |
| Squash | [`segment`](src/segment.rs) over IMU epochs | `BetweenRallies`, `OffCourt` |

That is what makes this one crate rather than two. `epoch` and `segment` are
Squash's producer and Spin never calls them; `history` is Spin's cross-app
record and Squash may never write one.

**A consumer pays for its own half and almost nothing for the other.** Measured
on the CI toolchain image against the pinned SDK, `arm-none-eabi-size` on
`SpinService.elf`, with a shim forcing symbols in so this measures the code
rather than measuring `--gc-sections`:

| Spin's Service | `.text` | `.data` | `.bss` |
|---|---:|---:|---:|
| No crate linked | 68,584 | 1,732 | 11,776 |
| Spin's half reachable | 81,416 | 2,812 | 14,240 |
| Squash's half also compiled, never called | 81,440 | 2,812 | 14,240 |
| Squash's half reachable too | 86,072 | 10,100 | 17,324 |

Spin's half costs **+12,832** of `.text`; carrying Squash's half without calling
it costs **24 bytes** and nothing in RAM; calling it would cost a further
**+4,656** `.text` and **+7,288** `.data`, which Spin never pays. That settles
the "a crate whose every consumer uses half of it" objection with a number
rather than an argument.

---

## What this cannot tell you

This section is not a disclaimer. It is the part that makes the rest worth
having, and every claim in it is sourced below.

- **It is not a fitness score, and it is not a ranking.** A single HRR number is
  not comparable between people. Nothing here produces a verdict, a grade or a
  colour, and nothing should be built on it that does.
- **It is dominated by what you did next, and this is the biggest trap.** Barak
  et al. (2011) fitted the recovery curve in the *same* athletes under four
  protocols: the time constant was **52.5 s sitting still**, **74.1 s pedalling
  gently in the same seated position** — 41% slower for moving rather than
  sitting — and **32.0 s lying down**. A wearer who does a proper cool-down will
  look *worse* than one who sprints and steps off. **The watch cannot tell these
  apart**: it knows the session was paused, not whether you kept turning the
  pedals, sat up, or lay on the mat.
- **It depends on how hard you were going.** Buchheit (2014): "the greater the
  relative exercise intensity, the greater the blood acidosis and metaboreflex
  stimulation, and the slower the HRR". Which is why `hr0` and `hr0_pct_max` are
  stored beside every number, and why a window below 80% of maximum is discarded
  rather than recorded small.
- **One measurement means nothing.** Buchheit (2014), Table 1, puts HRR₆₀'s
  typical error at a coefficient of variation of about **25%**, with a
  signal-to-noise ratio of **1.3**. A change smaller than a quarter of the
  number is noise. This is the single reason nothing is drawn on the watch.
  Spin's Ride A on 2026-09-03 measured this directly: two windows opening at an
  identical `hr0` of 161 at 88% of maximum returned **17 and 23 bpm**, a 6 bpm
  spread on identical starting conditions.
- **It is confounded by things not recorded here.** Daanen et al. (2012) name
  age, ambient temperature, and the intensity and duration of the preceding
  exercise; posture, hydration, caffeine, illness, sleep and time of day belong
  on the same list.
- **A faster fall is not automatically better.** Buchheit (2014) reports HRR
  moving in both directions with training load, so a number that moved is a
  number that moved.
- **It is not HRV.** rMSSD and its family are the better-founded measure of
  autonomic status and are **not available**: the SDK's beat-to-beat pathway is
  an unlanded experimental PR, and `HeartRateEx` gives one arbitrated bpm and a
  confidence, once a second.
- **`edwards_trimp` is a bookkeeping number, not a dose.** Its weights are
  ordinal and were never validated against a physiological response.

---

## The measurement

**HRR₆₀** is the fall in heart rate over the 60 seconds after effort ceases, in
bpm. That is all this computes. It does not compute a verdict.

### Every number says where it came from, and they do not all come from the same place

A single provenance over a whole calibration would be a lie. The interval a
measurement is defined over comes from a paper; the fraction of seconds that
must be trusted comes from this repository's own recordings. So
[`Provenance`](src/lib.rs) has two variants and every threshold is a
[`Gate`](src/window.rs) carrying its own:

| | Means | Cannot be set by |
|---|---|---|
| `Measured` | a recording here set it, and names which | a paper |
| `Defined` | a published protocol fixes it, and names which | a recording |

This is the one place the merge changed a rule rather than picking one. The
first EffortKit made *every* threshold require a local recording, which is right
for a segmentation level — nobody can know what accelerometer magnitude means
"rally" without recording it — and a category error for a measurement interval.
HRR₆₀'s 60 seconds is what the quantity *is*; a recording suggesting 47 s would
not have measured a better HRR₆₀, it would have measured something else.

So [`window::HRR60`](src/window.rs) ships and Spin reports on its first ride,
while [`segment`](src/segment.rs) still has no constructible calibration at all
and reports `Unavailable::NotCalibrated` until Phase A2 has run.

**What this gives up**, and it is the largest concession in the merge: the
discipline is no longer mechanical. The first EffortKit was safe because there
was *no* constructible calibration in the repository, so the type system alone
enforced it. With `Defined` available an author can write a citation that does
not support the number. That is now a review question, not an impossibility.

### The gates, and what each one is for

A measurement is **discarded** when its preconditions were not met. A recovery
number from a pause that began at 95 bpm is not a small number — it is not a
measurement. Every gate has a test named after it in
[`tests/window.rs`](tests/window.rs).

| Gate | Value | Kind | Where the number comes from |
|---|---|---|---|
| The watch knows a maximum | > 0 | — | Without it there is no intensity to record the measurement against. |
| Heart rate at cessation | ≥ **80%** of max | `Defined` | Barak et al.'s protocol — 5 min at 80% of peak — because their time constants are what this is read against. The 70% that circulates traces to fitness writing, not a primary study. |
| Uninterrupted effort first | ≥ **180 s** | `Defined` | Buchheit (2014): "at least 3–4 min of exercise are generally required for HR to reach a steady state". About steady state rather than recovery, so the weakest gate here. Measured on the *current bout*. |
| Not already falling | within **10 bpm** of the preceding 30 s peak | `Measured` | At Barak's 52.5 s time constant a 70 bpm reserve falls 30 bpm in the first 30 s; steady effort moves 0.49 bpm between consecutive readings. 10 sits between the two. |
| Trusted readings in that 30 s | ≥ **20 of 30** | `Measured` | 5.3% of seconds untrusted, so about 1.6 of 30 is normal and 10 missing is not. |
| A trusted baseline at cessation | within **2 s** | `Measured` | Past that the baseline is already down the curve and understates the fall. |
| Trusted seconds inside the window | ≥ **90%** | `Measured` | Allows 6 untrusted seconds in 61. Across 34 minutes of recordings: 5.3% untrusted, in 89 runs of median length 1 and **maximum 4** — no run has ever been long enough to trip this alone. **A dropout is a discarded measurement, never an interpolated one.** |
| The reading is one the kernel stands behind | trust ≥ **1** | `Defined` | `HEART_RATE_EX` uses 0 for a reading it does not vouch for. The floor is a calibration knob, for the reason below. |
| The sensor did not change | — | `Measured` | **14%** of 60 s windows begin and end on different sensors, and where both report they differ by a median of 2 bpm and a 95th-percentile **16** — against falls of 8–20 bpm. Those windows averaged **−2.1 bpm**, an apparent rise. |
| A trusted endpoint | at 60 s, or within **2 s** after | — | `window_s` records the second actually used. |
| Nothing restarted effort | — | — | Resuming, or ending the session, closes the window with nothing. |

### Confidence is a level, not a boolean

The detector takes the kernel's 0–3 confidence rather than a yes/no the caller
has already collapsed, so a calibration decides how far the sensor is believed
and `trusted_s` counts seconds that met *that* floor.

Spin's Session 2 of 2026-09-03 is why. A five-second excursion of up to 15 bpm,
entirely at trust=1 and bracketed by trust=3 readings on a smoothly falling
signal, reached a stored curve. The fall itself was untouched — that is `hr0`
minus `hr_end` — but `trusted_s: 61 of 61` was a stronger claim than the sensor
had made.

**The shipping floor is still 1, deliberately.** The same document measured that
regime against a real one:

| | Real maximum (184) | Synthetic (100) |
|---|---:|---:|
| trust=1, of paused seconds | **9%** | **24%** |
| largest one-second move | **3 bpm** | **15 bpm** |

The gates only run above 80% of a *real* maximum, which is the good regime, so
they never see the bad one — and that regime only exists because a test set a
fake maximum to make the code paths execute. The floor is a knob because the
difference was measured, not because it needs turning. What that sample cannot
show is that the tail is absent at real intensity: 271 paused seconds and eight
trust=1 pairs cannot rule out a rare excursion.

The intensity gate and the already-falling gate interact, and Ride A measured
how: with both configured the band where `already_falling` fires rather than
`too_easy` is `(peak − 10)` down to `0.8 × max`, which at that ride's numbers is
**3.8 bpm wide**, about four seconds of curve. A late press lands in `too_easy`
far more often.

Everything is keyed on the **UTC second**, never the number of calls, for the
reason `SecondsAccrual.hpp` exists: a tick the Service was too busy to serve is
a real second that went past.

### Discards are an output, not a debug field

Every refusal is counted by reason in [`window::Discarded`](src/window.rs), and
the counts reach the file. A diagnostic log answers "what happened this session,
second by second" and gets deleted; the counts answer "why does this session
carry no number" and are a permanent property of it.

The argument is the desk check of 2026-09-03: every window in both runs was
discarded `no_max_hr`, and the only record of that was a text log the field test
instructs you to delete. A year later that session would be indistinguishable
from one where nobody paused.

Counters are emitted only when non-zero and the block is omitted entirely when
nothing was discarded, so the ordinary session pays nothing for the ability.

---

## Two files, two jobs

The merge's other structural change. Both source crates wrote one file and asked
it to do both of these.

### `../SharedData/<app>_sessions.json` — the series

One row per session, the comparable measurements, the protocol beside each one.
Cross-app, versioned, documented as a contract. **One file per app**, named after
it, so two apps never write the same file and there is no lock to get wrong; a
reader wanting every sport globs `*_sessions.json` and merges on `app` and
`sport`. The path convention is the SDK's own —
`SDK::Calibration::StrideLut::kDefaultPath` is `../SharedData/stride.json`.

Bounded at **20 entries** and **16 KiB**, the byte cap deciding: `save` drops the
oldest and re-serialises until it fits, so **the newest session always lands**
and `dropped` counts everything ever evicted. `start_utc` is the entry's
identity, so a write retried after a failure replaces rather than appending.

Measured in [`tests/history.rs`](tests/history.rs), not argued: twenty ordinary
sessions are **9,306 bytes**; twenty as wide as a session can reachably be are
**16,340** of the 16,384 available. With all fourteen discard reasons non-zero at
once — a state the gates cannot actually reach — only 16 of the 20 fit, and the
test pins that degradation too.

### `<app>` profile — the interior

Every window a session measured and whatever structure its sport has. Owned and
written by one app, read by nobody else, so its schema is that app's business.

Window **detail** is bounded at 4 per session; the **count and the mean are
not**. Every qualifying window updates a per-kind sum and count whether or not
its own row fits, so a mean is over all of them and `windows_dropped` says how
much detail went. No curve is stored here — seven numbers per window, wanted
only for a fit this hardware cannot support, and the series file already keeps
one per measurement.

**Why not store the mean directly**, as the first EffortKit did: a mean is a
conclusion about windows that are no longer present. Store the sum and the count
and a change to how the figure is computed applies to the whole history instead
of orphaning it — which is the rule that crate stated for baselines and broke
for recovery.

---

## Baselines that survive real sessions

Median and MAD over a rolling window of 20 sessions, not mean and standard
deviation. One two-hour session, one twenty-minute knock-up, or one session where
the strap fell off must not redefine what normal looks like.

- **Twenty sessions** is roughly two months at one to three a week.
- **Five sessions** before any comparison is offered; below three the MAD is
  degenerate. Under that, `compare()` returns `WarmingUp { have, need }` — the
  raw measurement is still shown, the comparison is not invented.
- **Ten percent** is the most one session may move a baseline, and
  **`compare()` reads the bounded baseline rather than the bare median**. In the
  first EffortKit it read the median, so the step limit protected nothing any
  consumer could see; `a_comparison_is_taken_against_the_bounded_baseline_not_the_raw_median`
  pins the fix.
- **Sessions that should not vote** do not: under ten minutes of active time, or
  a trusted heart rate covering less than 80% of it. Rally-derived metrics
  additionally need a segmented session with at least ten rallies.

Every comparison is against the wearer's own history. There are no population
norms here and nothing that ranks one person against another.

`session::Kind` marks each metric `Level`, `Rate`, `Ratio` or `Total`, so a
reader merging several apps' logs knows which columns may be laid side by side —
a total is comparable within one session only.

**`SleepLab` already ships this machinery in C++** (`Engine/BaselineStore.hpp`),
independently arrived at: median not mean, a rolling window, a minimum of five
before reporting, wearer against themselves. It is not migrated — it works, and
rewriting a shipped sleep engine buys nothing — but the next app that needs a
baseline has somewhere to get one, and that somewhere is
[`baseline.rs`](src/baseline.rs).

---

## What the recordings say

34 minutes of `HEART_RATE_EX` pulled off this watch (`Squash/Tests/pulled`, six
sessions, 2,021 samples, one wearer, squash). Re-derive every row with
`python3 Tools/hr_analyse.py` and `python3 Tools/hr_source.py`.

| Measured | Value | What it means here |
|---|---|---|
| Untrusted seconds | **5.3%** | Confirms the 5% Spin measured over two rides, on different sessions and a different app. |
| Untrusted run length | median **1**, max **4**, none ≥ 6 | The 90% gate cannot be tripped by one dropout, only by several. |
| Sample interval | median **1005 ms** | Slightly slower than 1 Hz, occasionally ~2 s. Harmless: the detector is keyed on the Service's UTC tick. |
| Consecutive change | mean **0.49 bpm**, median **0**, **65% of steps exactly zero**, every value a whole bpm | The stream is integer-quantised and mostly repeats. |
| 60 s windows crossing sensors | **14%** | Why `source_changed` exists. |
| Optical vs external where both report | median **2**, p95 **16**, max **23** bpm | The size of the whole measurement. |

**These are squash at 64–110 bpm.** They say nothing about any intensity gate.

**A correction the merge produced.** `Formulation::NotMeasurableOnThisHardware`
was motivated by consecutive readings differing by 0.50 and 0.18 bpm, read as
evidence of sub-bpm kernel smoothing. Those are *mean* steps, not step sizes:
across all six recordings the smallest non-zero step is exactly **1 bpm** and
**0 of 691** are smaller, so no sub-bpm smoothing is visible. The variant stays,
because the question it exists for is still unanswered — `phase-a` reports that
no labelled transition out of effort carried enough heart rate to measure a step
response across — but its stated reason was wrong.

---

## Sources

- Cole CR, Blackstone EH, Pashkow FJ, Snader CE, Lauer MS. "Heart-rate recovery
  immediately after exercise as a predictor of mortality." *N Engl J Med*
  1999;341:1351-1357. The 60-second interval, after a symptom-limited Bruce
  treadmill test with a 2-minute *walking* cool-down — a protocol that does not
  transfer to a wearer sitting on a bike, and **nothing here reports its
  threshold**.
- Shetler K, Marcus R, Froelicher VF, Vora S, Kalisetti D, Prakash M, Do D,
  Myers J. "Heart rate recovery: validation and methodologic issues." *J Am Coll
  Cardiol* 2001;38(7):1980-1987. 2,193 male patients; 2-minute recovery more
  prognostic than 1-minute, at < 22 bpm, hazard ratio 2.6 (95% CI 2.4-2.8).
- Barak OF, Ovcin ZB, Jakovljevic DG, Lozanov-Crvenkovic Z, Brodie DA, Grujic
  NG. "Heart rate recovery after submaximal exercise in four different recovery
  protocols in male athletes and non-athletes." *J Sports Sci Med*
  2011;10(2):369-375. Table 2: time constants of 52.5 (14.6) s seated inactive,
  74.1 (24.0) s seated active, 32.0 (9.1) s supine, 28.5 (5.8) s supine with
  legs raised, in athletes; 5 min of cycling at 80% of individual peak.
- Buchheit M. "Monitoring training status with HR measures: do all roads lead to
  Rome?" *Front Physiol* 2014;5:73. HRR₆₀ typical error ≈ 25% CV,
  signal-to-noise 1.3; intensity dependence; the 3-4 min steady-state figure.
- Daanen HAM, Lamberts RP, Kallen VL, Jin A, Van Meeteren NLU. "A systematic
  review on heart-rate recovery to monitor changes in training status in
  athletes." *Int J Sports Physiol Perform* 2012;7(3):251-260.
- Edwards S. *The Heart Rate Monitor Book*. Polar Electro Oy, 1993. The five
  zones and the weights 1..5.
- Impellizzeri FM, Tenan MS, Kempton T, Novak A, Coutts AJ. "Acute:Chronic
  Workload Ratio: Conceptual Issues and Fundamental Pitfalls." *Int J Sports
  Physiol Perform* 2020;15(6):907-913.

---

## Training load

**Computed:** Edwards' TRIMP, emitted **only when the ladder is the five zones
at 50-60, 60-70, 70-80, 80-90 and 90-100% of maximum that the weights are
defined over** (within 1 bpm of rounding), and absent otherwise rather than
extended to a ladder Edwards never wrote weights for. Average power and
Efficiency Factor from a work figure the wearer entered, both 0 when there is
none and **never imputed**.

**Not computed, and not faked:** TSS, IF and normalized power all need a real
FTP and a power *stream*. Aerobic decoupling needs power for each half.
Banister's TRIMP needs resting heart rate, which no app here has, and sex, which
none knows. ACWR is mathematically coupled (Impellizzeri et al. 2020). And
anything phrased as advice — "ready to train", "overreaching", "recovery: good"
— is a claim about a person this data cannot support.

---

## The boundary

Rust owns when a measurement counts and what the file says. C++ owns the clock,
the sensor and the filesystem — and it has to, because Rust cannot call the
SDK's C++ virtual interfaces without a shim written in C++ anyway.

**The C ABI lives in a per-app shim**, `<App>/Software/Libs/rust/`, which is the
staticlib; this crate is a plain `lib` and carries no `#[panic_handler]`. That
is what lets two apps each link their own archive without two panic handlers
colliding in one binary — a collision the Squash work hit and recorded. It also
puts the chokepoint where the app owns it, rather than making this crate the
only Rust archive a Service may ever link.

Each shim holds its state `static`, in `.bss`, constructed in place. Not for
want of an allocator but for want of stack: the Squash session type is 12,656
bytes against a 10,240-byte Service stack, and building one as a value to move
into place took an `STKOF` UsageFault (`CFSR=0x00100000`) on the watch.

Each shim must compute an **FNV-1a fingerprint over its own struct layout on
both sides** — `offsetof` in the C++ header, `offset_of!` in Rust — and refuse
to start on a disagreement. [`fnv1a`](src/lib.rs) is here so both walks use the
same function. A fingerprint the C++ side asserts as a *hand-copied literal*
does not catch a C++ struct that drifts, which is what the first Squash shim
did.

## Building and testing

```sh
cargo test --features std                              # 112 tests
cargo clippy --all-targets --features std
cargo build --release --target thumbv8m.main-none-eabihf   # the watch target
cargo run --features std --bin phase-a -- ../Squash/Tests/pulled/*/imu_*.csv
```

The `thumbv8m` build is the one that matters most: it is what proves the crate
is `no_std` and that nothing testable crept in behind `feature = "std"`.

## Licence

MIT OR Apache-2.0.
