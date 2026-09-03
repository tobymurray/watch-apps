# Phase A — what the signals are doing, before anything models them

Two questions. Neither is answered yet, and the honest consequence of that is
the whole of this document's verdict.

- **A1.** What is the heart-rate signal actually doing — its update cadence, its
  quantisation, how long it takes to settle when effort stops, and how any of
  that differs between the wrist optical sensor and a strap.
- **A2.** Are the movement states of §2 separable at all, and by which feature.

Both are measured by `EffortKit`'s `phase-a` analyser over recordings taken with
this app. [`RECORDING-PROTOCOL.md`](RECORDING-PROTOCOL.md) says which recordings,
how many, and what each one settles.

---

## The verdict, 2026-09-03

**No usable recording exists.**

Stated more carefully than it was on first writing, because the first version of
this paragraph claimed none existed anywhere and had only checked the
development machine with the watch unmounted. Mounting it found seven
recordings and one marker sidecar under `Apps/Squash/Imu/`, about twelve minutes
in total — five bench runs of seconds to a minute from 2026-08-03, two desk runs
of five and six minutes, and a 33-second run with two markers that was testing
the marker feature.

**They are not to be used.** They were produced by a superseded build, so
nothing about their contents can be attributed to the code that would read them,
and none of them is court data or carries a labels file. A recording whose
provenance is a version nobody can reconstruct is not evidence, and the point of
this document is to not accept things that are not evidence.

So the position is unchanged in substance: **no labelled court recording
exists**, which is what both A1 and A2 need.

So:

| Phase | Supported? |
| --- | --- |
| B — rally and rest segmentation | **No.** No feature has a measured distribution, so no level and no dwell time can be set. `segment::Calibration::Absent` is the only value that can be constructed and `Segmenter::finish()` returns `Unavailable::NotCalibrated`. |
| C — heart-rate recovery | **No**, and doubly so. It needs A1's settling time before a formulation can even be chosen, and it needs Phase B's rest windows to measure across. |
| Baselines and the profile | **Yes, in part.** The storage, the admission gate, the robust statistics and the warm-up behaviour are exercisable without any recording, because none of them depends on a threshold. A session is recorded with `segmented: false` and votes only on the heart-rate metrics. |
| Presentation | **Nothing.** No row of the ledger says validated, so nothing is displayed. |

This is not a failure state. It is the state the honesty rule produces when the
evidence is missing, and it is the state the app should be in until the
recordings exist. A blank with a reason is the output.

**What was built instead** is everything that does not need a recording: the
engine, with its thresholds unconstructible; the storage and the baselines; the
analyser that turns recordings into these numbers; the heart-rate sidecar
without which A1 could not be measured at all; and the desk loop, which was
broken.

---

## What *was* measured

Three things, all about the apparatus rather than about squash.

### The desk loop was broken in two ways, one of them unrecorded

`Squash/README.md` said `squash-filesink-tests` self-skips because
`ImuFusionSource` is not in the pinned SDK, and that is true — it is one commit
on the SDK's `simulator-imu-source` branch, which is 46 commits behind and 1
ahead of `apps-v1.4.0`.

The simulator's failure had a **different** cause that nothing recorded: it
needs `SDK::AppConfig`, which exists on the SDK's main line and not on the
`apps-v1.4.0` tag. Building against the tag fails at
`AppConfigFields.hpp:30: SDK/AppConfig/AppConfig.hpp: No such file or directory`
— nothing to do with the IMU source at all.

Cherry-picking `5df2033e` onto `8cdb7314`, the revision
`.github/workflows/app-build.yml` already pins, fixes both:

| Build | Before | After |
| --- | --- | --- |
| `squash-recorder-tests` | pass | pass |
| `squash-marker-tests` | pass | pass |
| `squash-hrlog-tests` | — | pass |
| `squash-filesink-tests` | **skipped at configure** | **pass** |
| TouchGFX simulator | **fails at compile** | **links, 9 245 896 bytes** |
| `.uapp` | pass | pass, 404 384 bytes, CRC `0x0FF911D8` |

[`../Tools/docker-build.sh`](../Tools/docker-build.sh) drives all four and its
header carries the SDK recipe.

### The analyser computes what it claims

Exercised end to end on a synthetic labelled session — a knock-up, twenty
rallies with their rests, and five minutes off court, 1120 epochs — generated in
a scratch directory and deliberately not committed. It produced the inventory,
the cadence histogram, the quantisation table, a settling time across twenty
labelled transitions, per-state distributions for all four features, and the
pairwise overlap table.

**This says nothing whatever about squash.** The data was generated to be
separable, so it was separated. What it establishes is that the tool runs, that
a constant feature reports a coin flip and total overlap rather than a spurious
threshold, and that the labels file, the marker sidecar and the sample clock
line up as designed.

### The heart-rate sidecar exists, so A1 is now measurable

Before this branch there was no heart rate on the recording's clock at all. The
`.fit` file has a heart-rate series, but on its own timebase, so a labelled
effort transition and the readings around it could not be lined up without
correlating two clocks. `HrCsvLog` writes `imu_<stamp>_hr.csv` from the same
tick as the samples and the markers, in hundredths of a bpm so the sub-bpm steps
survive.

That last detail is the whole point. `CLAUDE.md` records consecutive
`HEART_RATE_EX` samples differing by **0.50 and 0.18 bpm** over two real rides.
A raw beat-derived rate at 1 Hz cannot step by a fifth of a beat; that is the
kernel smoothing. Rounding the sidecar to whole bpm would have discarded exactly
the evidence A1 exists to weigh.

---

## First hardware numbers, from a 5-second smoke recording

`Squash/Tests/pulled/20260903-v0.6.0-5s-smoke/`, off v0.6.0 with the launch line
confirming `ok=1 calibration=0`. **Provenance is a bench smoke test, not squash**,
so none of it validates a metric — but the signals themselves are the same
signals, so what it says about cadence and quantisation is real.

| Measurement | Value |
| --- | --- |
| IMU effective rate | **99.6 Hz** — 1 090 samples over 10 942 ms |
| IMU interval jitter | 1 040 gaps at 10 ms, 48 at 11 ms, 1 at 14 ms |
| IMU sample loss | 5 of 1 095 expected, **0.46%** |
| Saturation | **none**, on either range. Gyro peaked at 1 949 LSB — 119 dps, two orders below the ±2000 dps rail, so this recording is nowhere near stroke intensity |
| HR cadence | **~1005 ms**, not the nominal 1000; seven gaps at 1005 ± 4 ms and one dropout of 2 011 ms in nine samples |
| HR acquisition | **four samples of `trust=0, bpm=0`** before the first reading — about four seconds of nothing at the start of a session |
| HR trust, while stationary on optical | fluctuated 2, 2, 3, 1, 3 across five consecutive readings |
| Epoch `accel_var`, wrist near-still | 306 – 1 060 |
| Epoch `accel_var`, hand waved | 2 909 – 31 979 |

### The quantisation finding, which changes A1's premise

Every optical reading was a whole bpm: 70, 68, 68, 67, 67. **Zero sub-bpm steps
in the sample.**

`CLAUDE.md` records consecutive `HEART_RATE_EX` samples differing by **0.50 and
0.18 bpm** across two real rides, and this document was written assuming that
smoothing applies generally — which is why it warns that a slope over a rest
could be the filter settling. On this recording it does not apply: wrist optical
delivered integer bpm.

The straightforward reading is that the sub-bpm behaviour is the **external
strap** path and optical is quantised to whole bpm. That would mean the two
sources need different `Formulation`s, not one policy. **Five samples is far too
few to conclude it**, and no recording here has carried a strap yet, so this is
recorded as the first evidence rather than as the answer. Group S1 settles it,
because it records both channels at once.

### What it says about a 5-second activity

Nothing usable. Four seconds of acquisition plus five readings is not a heart
rate, and the app's own summary said so — `"hr_avg":0`, because `Service.cpp`
gates the FIT field on more than 20 samples. The engine disagreed and reported
68.00 bpm from five readings, which is the defect this recording found; it now
applies the same gate, so the two numbers the app produces about one session
agree.

## A1 — the method, and the tables it fills

`phase-a` reads every `imu_<stamp>_hr.csv` and reports four things.

### Update cadence

A histogram of the interval between consecutive readings, in 100 ms buckets.
Nominal is 1000 ms; what matters is the spread and the tail, because a window
that requires "no gap longer than N seconds" is set from this and nothing else.

| Gap (ms) | Count |
| --- | --- |
| *(awaiting S1)* | |

### Quantisation

The smallest non-zero step, the median step, and the proportion of steps below
1 bpm.

| Smallest non-zero step | Median step | Steps below 1 bpm |
| --- | --- | --- |
| *(awaiting S1)* | | |

A high proportion below 1 bpm is smoothing, and smoothing is what makes a fitted
slope across a rest a measurement of the filter rather than of the wearer.

### Settling time

At every labelled transition out of effort, the lag until the rate moves at all,
and the time to fall 63.2% of the way to the floor of the next two minutes —
which is the exponential's own definition of a time constant, taken without
fitting one and therefore without a fit to defend.

| Source | Transitions | Median lag | Median time constant |
| --- | --- | --- | --- |
| External | *(awaiting S1)* | | |
| Optical | *(awaiting S1, S2)* | | |

### The verdict A1 owes Phase C

**If the median time constant is comparable to the rest windows recovery would
be measured over, recovery as a slope is not measurable and must not be
reported as physiology.** The rests in question are 10–20 s between rallies and
minutes off court, so:

| Time constant | What it means | The calibration |
| --- | --- | --- |
| A few seconds | The filter settles well inside a between-rallies rest | `FixedWindowDrop` for both kinds |
| ~10–20 s | The filter and the short rest are the same duration | `NotMeasurableOnThisHardware` for `short`, `FixedWindowDrop` for `long` |
| Longer | Nothing shorter than a whole game is measurable | `NotMeasurableOnThisHardware` for both, and the feature is withheld |

`recovery::Formulation` carries all three outcomes, so whichever it is gets
written down as a calibration rather than argued about.

### And separately: is optical usable at all

Wrist optical during squash is the adversarial case — grip tension, impact shock
and the watch moving on the wrist all corrupt it, and a smoothed corrupted
signal still looks smooth. S1 records both channels simultaneously, so the
question is answerable by comparing them within one recording:

| Measure | Optical | External |
| --- | --- | --- |
| Median absolute difference from the strap, during rallies | *(awaiting S1)* | — |
| Same, during rests | *(awaiting S1)* | — |
| Settling time | *(awaiting S1)* | *(awaiting S1)* |
| Trust level distribution | *(awaiting S1)* | *(awaiting S1)* |

The outcome is `recovery::SourcePolicy`: `ExternalOnly`, or
`EitherWithSourceRecorded` with every window carrying its source. Both are
defensible; silently reporting a strap-quality number from wrist optical is not.

---

## A2 — the method, and the tables it fills

`phase-a` reduces every recording to one feature vector a second
(`epoch::EPOCH_MS` = 1000) and groups them by the label the wearer gave the
stretch they fell in. An epoch is attributed by its midpoint, so one straddling
a marker goes to whichever state held it longer.

Four candidate features, all in raw sensor LSB or a fraction, none scaled:

| Feature | What it is | Why it is a candidate |
| --- | --- | --- |
| `accel_var` | Variance of accelerometer vector magnitude | The obvious starting point, and the one the brief names |
| `accel_jerk` | Mean absolute change in that magnitude between samples | Band-limited by construction, and the feature `WristTiltDetector` already uses in a different form |
| `gyro_mean` | Mean gyroscope vector magnitude | Rotation rather than translation; a rally is wrist rotation, walking is not |
| `gyro_sat` | Fraction of the epoch with any gyro axis railed | The README's own point that clipping is signal — time-spent-saturated is an intensity feature where a peak silently rails |

### Distributions

One table per feature, `n / min / p5 / p25 / median / p75 / p95 / max` per
state, for `rally`, `rest`, `off_court`, `knockup`, `drill` and `idle`.

*(awaiting M1–M3, T1–T2, D1–D2, N1)*

The percentiles are what the two levels come from: entry above the rest
distribution's upper tail, exit above nothing much, and the gap between them is
the hysteresis band.

### Overlap

For each pair the segmenter has to separate, the single threshold that
misclassifies the fewest epochs, its balanced error rate, and the histogram
overlap of the two distributions.

| Pair | Feature | Best threshold | Error rate | Overlap |
| --- | --- | --- | --- | --- |
| rally vs rest | *(awaiting M1–M3)* | | | |
| rest vs off_court | *(awaiting T1–T2)* | | | |
| rally vs drill | *(awaiting D1–D2)* | | | |

**The effective sample size is the number of rallies, not the number of
epochs.** Epochs inside one rally are highly correlated, so 2 000 rally epochs
from 150 rallies is 150 observations, not 2 000. That is why
`RECORDING-PROTOCOL.md` asks for three matches against three opponents rather
than one long one.

### The verdicts A2 owes Phase B

1. **Which feature.** The one with the lowest error rate on rally vs rest, which
   becomes `segment::Feature` in the calibration.
2. **Whether rally and rest separate at all.** If the best error rate is high, a
   hysteresis machine over that feature will not do better, and the answer is to
   find a better feature — band-limited energy, step cadence, wrist-motion event
   rate — before proceeding, not to ship the machine anyway.
3. **Whether off court is a state.** If T1 and T2 disagree, or the rest/off_court
   overlap is large, then `OffCourtRule::Indistinguishable` is the calibration
   and the app reports the two merged with `off_court_separable` false. Saying
   so is the requirement; collapsing them silently is what §2 forbids.
4. **Whether drills are separable.** If not, a drill session must report that it
   was not rally-structured rather than a rally count, per §7 of the brief.

---

## How this document gets finished

1. Collect the recordings in `RECORDING-PROTOCOL.md`, group S first.
2. `cargo run --features std --bin phase-a -- <files> --epochs epochs.csv`.
3. Paste its tables into the placeholders above and write the verdicts.
4. Write the calibration constants, each with a `Provenance` naming the
   recordings and this date.
5. Promote the rows in [`FEASIBILITY-LEDGER.md`](FEASIBILITY-LEDGER.md) that the
   measurements support, and only those.
6. Only then, present anything.
