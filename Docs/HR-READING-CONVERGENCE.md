# Seven apps read the heart-rate sensor — what should exist instead

> **Implemented.** This design was carried out on `squash-rust-instrument` as
> `HrKit/` plus four changes across the apps. Where building it settled a
> question or corrected one of the conclusions below, the section says so, and
> [§9](#9-what-changed-in-the-implementing) lists the five things that changed in
> the doing — one of which is a recommendation in §5 that turned out to be wrong.

A design pass over the HR read in `BikeMap`, `GpsLab`, `HikeMap`, `RunMap`,
`Spin`, `SleepLab` and `Squash`, answering
[`HR-READING-CONVERGENCE-PROMPT.md`](HR-READING-CONVERGENCE-PROMPT.md). No code
was written. The tree read is `squash-rust-instrument` at `8647834`, with
`origin/feat/effortkit` read for the build that made the pulled recordings.

Everything marked **measured** was derived in this pass and the command or the
file is given. Everything marked **asserted** is a judgement and says what would
settle it.

The prompt asked to be distrusted, and it was right to. Its §2.3 is answerable
from evidence already in this repository, and the answer is the one that removes
most of the motivation: **`HEART_RATE_EX` alone works, the five apps are not
broken, and there is no silent HR loss to fix.**

What is left is smaller and different from what the prompt expected. Its two
"freshness implementations" turn out to guard different sensors; a third one it
does not mention is the interesting one; and the likeliest cause of the failure
that prompted the whole exercise is a sensor-subscription race already
documented in a comment in `GpsLab`, which has nothing to do with which HR
sensor an app reads.

---

## 0. What this pass measured

### 0.1 §2.3, answered: `HEART_RATE_EX` alone produces, and the five apps are fine

The prompt offers four candidate explanations for why a build subscribing to EX
only showed nothing. Three are refuted by recordings already in the tree, and
the fourth is refuted by a date.

**Spin is the whole answer.** Spin subscribes to `HEART_RATE_EX` and nothing
else — no `HEART_RATE`, and, unlike the four map apps and unlike the 0.6.0
Squash, **no `BATTERY_LEVEL` and no `BATTERY_METRICS`** (measured:
`grep -n 'BATTERY_LEVEL' Spin/Software/Libs/Sources/Service.cpp` returns
nothing). Spin has produced three real rides on hardware:

| Recording | Duration | `hr_avg` |
|---|---:|---:|
| `Spin/Tests/pulled/20260903-rideA-real-max-184/activity_20260903T205637.json` | 1142 s | **116.43** |
| `.../20260904-intervals-real-max-184/activity_20260904T212623.json` | 2627 s | **117.56** |
| `.../20260904-intervals-real-max-184/activity_20260904T223146.json` | 2700 s | **133.55** |

`Spin/Docs/RECOVERY-FIELD-RESULTS.md` § "2026-09-03 — Ride A" records that ride
as *"wrist optical throughout, no strap"*, with two 60-second recovery windows
that each counted **61 of 61 seconds trusted** and produced seven-point
per-second curves (`161,162,157,154,147,144,144` and
`161,162,159,154,147,144,138`), both stamped `src=1` (optical).

That single fact kills three candidates at once:

| Candidate (prompt §2.3) | Verdict | Why |
|---|---|---|
| The battery subscriptions were making EX produce | **refuted** | Spin subscribes to neither and gets a continuous per-second optical HR. |
| EX is produced only when another subscriber is present | **refuted** | Spin's Service subscribes to no other cardio type; its GUI process subscribes to no sensors at all. |
| EX needs a strap — it is really an external-HR companion | **refuted** | Ride A ran with no strap, `src=1`, 61/61 trusted seconds. Independently, `Squash/Tests/pulled/20260913-v0.6.0-70min-match` is 4,087 samples of which **3,650 are `source=1` with `external_x100 = 0`**. |
| A firmware or kernel change between 2026-09-03 and 2026-09-13 | **refuted** | The 70-minute match recording *is* 2026-09-13 (`imu_20260913T122111_hr.csv`, 12:21). The rewrite failed the same evening — its three fix commits are timestamped 18:02, 19:40 and 20:55 on 2026-09-13. The same watch produced 4,087 EX samples that morning. |

And EX is not new: `SensorDataParserHeartRateEx.hpp` landed in the SDK on
2026-06-17 (`49c01b2a`), before every build discussed here.

**So the prompt's own conditional resolves.** It said: *"If EX alone is fine,
the five apps have no bug, the motivation drops to ordinary duplication, and the
right answer may be a documented rule rather than code."* EX alone is fine. The
five apps have no bug. Nothing here is urgent.

### 0.2 The likeliest cause is a documented failure mode one app already defends against

Why the rewritten Squash showed `-- BPM` is not proven. But the best candidate
is not on the prompt's list at all, and it is written down in this repository in
a comment, by the one author who hit it on a different sensor.

`GpsLab/Software/Libs/Sources/Service.cpp:296-298`:

> *"a subscribe that lost the ~100 ms ack race at track start is retried (pumped
> from `processTrack` each tick) instead of dropped for the whole session"*

and, on the GPS connection at line 231:

> *"GPS_LOCATION is subscribed once at GUI start so acquisition begins on the
> pre-activity screen. **That first attempt can lose a ~100 ms connect race
> during app startup, which would strand position logging for the entire
> session** … Retry until it takes."*

**`SDK::Sensor::Connection::connect()` can silently fail on this platform, and a
connection made once at app startup is exactly where it happens.** The symptom is
a sensor that produces nothing for the whole session while everything downstream
looks correct — which is, word for word, what the rewritten Squash did.

The two facts fit together:

| | Connects HR | Retries |
|---|---|---|
| BikeMap, HikeMap, RunMap, Spin | in `startTrack()` | no |
| Squash 0.6.0 (`origin/feat/effortkit`) | in `startTrack()` | no |
| **GpsLab** | in `startTrack()` | **yes — `connectSensors()` is idempotent and pumped from `processTrack()`** |
| **Squash, rewritten** | **in `onStartGUI()`, at app startup** | **no** |

The rewritten Squash was the only HR app in the repository that connected the
sensor at the moment GpsLab's comment names as racy, and the only one with no
retry. Adding `HEART_RATE` in `b0fd780` also added a *second* `connect()` call in
the same function, which would mask the race if that is what it was.

This was written as **asserted**, and implementing it turned up the SDK's own
source saying so outright. `Libs/Source/SensorLayer/SensorConnection.cpp:63-69`:

> *"Connected only when the kernel actually returned SUCCESS. `sendMessage()`
> returns true even when only the response timed out (not just on a real reply),
> so the old `send() || ok()` latched `mIsConnected` on a timed-out ack —* **the
> field failure mode** *— as well as on FAIL, then never retried. Gating on
> `ok()` leaves `mIsConnected` false on timeout/FAIL* **so the caller can
> retry.**"

So `connect()` is a 100 ms synchronous call that can fail, the SDK expects the
caller to retry, and six of seven apps ignored its return value and had no path
that would ever call it again. That the race is real is the SDK's statement, not
an inference; what remains asserted is only that it is what bit Squash on
2026-09-13. See [§9](#9-what-changed-in-the-implementing).

One measurement narrows it further and is already on disk. SleepLab's Tier 0
probe ran two minutes on 1.4 firmware on 2026-08-18 as a `Utility` app with **no
activity session at all**, and its screen block read `ATMRHXbpoSLCE`
(`SleepLab/Docs/FEASIBILITY-LEDGER.md`). Upper case means the driver resolved,
so **`HEART_RATE_EX` resolves outside any session** — which rules out "EX needs a
session" as the mechanism and leaves the connect race. What that run does *not*
say is whether EX ever *delivered*: the `hr 60` on screen is `mAcc.hrN`,
incremented only in the `mHr.matchesDriver(handle)` branch, i.e. `HEART_RATE`
(`SleepLab/Probe/Software/Libs/Sources/Service.cpp:321-339`). EX's own counter
`hrExN` goes to `probe_log.csv` and **has never been read** — ledger row S8 is
still UNVERIFIED and names exactly those columns. See §7.

### 0.3 The prompt's §1.1 reproduces; its §1.4 does not

§1.1's table is correct. The naive grep in §7 of the prompt is not — `HEART_RATE`
is a prefix of `HEART_RATE_EX`, so `grep -oE 'Type::HEART_RATE[_A-Z]*'` reports
every app as subscribing to both. Read the constructor lines instead.

§1.4 presents `RustGuiPoc`'s `kStaleAfterMs = 2500` and `Squash`'s
`Freshness.hpp` 3 s as "two implementations, neither aware of the other" of one
rule. **They guard different sensors.** `RustGuiPoc/Software/Libs/Sources/Service.cpp:19`
subscribes to `ACCELEROMETER` and nothing else; `grep -rn HEART_RATE RustGuiPoc/`
returns nothing. Its 2500 ms comes from `RustGuiPoc/Docs/FINDINGS.md` §5 —
*"Worst observed gaps: 1,272 ms at the default config, 2,038 ms with a 2 s driver
latency"* — which is a statement about accelerometer transport, not about heart
rate.

So the prompt's trap "a threshold is not a preference — if they converge, say
which evidence wins" has a third answer: **they should not converge, because
they are not measurements of the same thing.** What *is* shared between those two
files is the rule, not the number, and `RustGuiPoc/README.md` states it well:
`Gui.cpp` decides whether a sample is fresh; the renderer never guesses.

And the prompt's table omits the one that does guard heart rate.
`Spin/Software/Libs/Header/HrHold.hpp` is header-only, SDK-free, host-tested
(`Spin/Tests/HrHold_test.cpp`) and holds a reading for **10 seconds**. So there
are two HR freshness primitives in this repository — Spin's and Squash's — and
they answer different questions, which is the finding (§0.6, §2).

### 0.4 The recordings time the delivery, not the sensor

Every number in the prompt's §2.1 is a measurement of **when the app was handed a
batch**, quantised to the IMU batch clock, not of when the sensor produced a
reading. `Squash/Software/Libs/Sources/Service.cpp:252` writes the HR row's
`t_ms` as `mLastImuTs` — the timestamp of the last fusion sample seen.
`parser.getTimestamp()` exists on both parsers and is used by no app in this
repository.

`mLastImuTs` is itself stale by up to one fusion batch (~100 ms; recorded in
commit `1a97964`). So the sidecar's clock has ~100 ms of quantisation from a
source unrelated to the heart.

Re-derived over all 6,101 gaps in `Squash/Tests/pulled/*/imu_*_hr.csv`:

| Gap | Count | Share |
|---|---:|---:|
| ~1005 ms (nominal) | 5885 | 96.46% |
| ~2010 ms | 189 | 3.10% |
| 1100–1500 ms | 15 | 0.25% |
| **exactly 0 ms** | **11** | 0.18% |
| < 900 ms | 1 | 0.02% |
| max observed | 2117 ms | |
| ≥ 3000 ms | 0 | |

The eleven zero-gaps are the interesting ones, and they are not noise: **all 11
are immediately preceded by a gap longer than 1500 ms.** Two HR deliveries landed
inside one IMU tick, right after a late one. Since the app writes one row per
delivery (it reads `data[0]` only), two rows means two deliveries — this is
delivery *bunching*, not a sample that went missing and a coincidental double.

Consequence, stated carefully:

- **The 3-second threshold survives.** Freshness is a question about delivery,
  and delivery is what these files measure. No gap of 3 s or more has occurred
  in 6,101 observations.
- **The story attached to it does not.** `Freshness.hpp` says 3.0% of gaps are
  "one tick missed". At least some of them are the transport running late and
  catching up, and these recordings cannot tell you which — the sensor's own
  timestamps were discarded at the point of writing.
- The fix is one column. See §5.

### 0.5 A latent defect the prompt does not mention

Six of the seven apps request a **1000 ms sample latency** on the HR connection
and then read **`data[0]` only**:

| App | Requested latency | Reads |
|---|---:|---|
| BikeMap, GpsLab, HikeMap, RunMap, Spin, Squash | 1000 ms | `data[0]` |
| SleepLab | 0 (default) | `batch[i]`, whole batch |

`RustGuiPoc/Docs/FINDINGS.md` measured, on this hardware, what that costs:
finding 3 — *"At 2000 ms the driver batches ten samples into one `DataBatch`, so
the parameter is plainly not ignored"*; finding 6 — *"Read the whole `DataBatch`.
Keeping only the newest entry silently discarded nine samples in ten as soon as a
latency was configured."*

At 1 Hz with a 1 s latency the exposure is bounded to one or two samples per
batch rather than ten, so this is **latent, not live** — and the eleven
zero-gaps show HR does sometimes arrive twice in quick succession. It is one line
per app and it is not free: for the four map apps, iterating would feed every
sample to `mHrCounter.add()` and therefore change the session average that
reaches the `.fit`. That is a behaviour change, not a no-op, and it is why this
is listed as a migration item and not a bug fix.

### 0.6 What is actually duplicated, counted

The prompt frames the duplication as "the heart-rate read". The read is four
lines. The part that is genuinely copied, and that carries undocumented numbers,
is the *interpretation*:

**The validity predicate, four verbatim copies.** In BikeMap, GpsLab, HikeMap and
RunMap, character for character:

```cpp
bool hasHeartRate = (mHrCounter.getCurrent() > 20 && mTrackData.hrTrustLevel >= 1 && mTrackData.hrTrustLevel <= 3);
```

Spin has the same predicate three times with the numbers named (`skHrMinValid`
`= 20.0f`, `skHrTrustMin` `= 1.0f`, `skHrTrustMax` `= 3.0f`). SleepLab uses a
different rule entirely (`bpm <= 0.0f` → skip) and no trust gate at all.

**The counter bounds, five copies.** `mHrCounter.init(20.0f, 300.0f)` appears as
a literal in BikeMap, GpsLab, HikeMap and RunMap, and named in Spin. Nothing
anywhere says where 20 and 300 came from.

**The staleness rule, two incompatible halves.** This is the finding worth
acting on:

| | Spin (`HrHold`) | Squash (`Freshness`) |
|---|---|---|
| Guards against | a reading the kernel marked **untrusted** | a reading that **stopped arriving** |
| Window | 10 s hold, then blank | 3 s age, then blank |
| Tested on host | yes (`Spin/Tests/HrHold_test.cpp`) | yes (header-only, SDK-free) |
| Has the *other* half | **no** | **no** |

Each app implements exactly the half the other is missing, and neither author
saw the other's. The two failure modes are genuinely different: a frame can
arrive carrying a value nobody believes, or no frame can arrive at all.

How far the gap in Spin actually reaches, stated conservatively. `mHrCounter`
and `mTrackData.hrTrustLevel` are written in one place only — the sensor
handler. If frames stop arriving entirely, `getCurrent()` keeps returning the
last bpm, `trusted` stays true, `HrHold` is refreshed every second and never
expires, and `prepareRecordData()` writes `fitRecord.heartRate` with
`hasHeartRate = true` into every subsequent record. A stale heart rate would
reach the `.fit`, not just the screen.

**But this is latent, not observed.** The realistic case — the watch coming off
the wrist — does *not* stop the stream: `20260903-v0.6.0-5s-smoke` contains rows
of `bpm_x100=0, trust=0, source=1`, so the kernel keeps publishing zeros. And
`SDK::Metric::VariableCounter::add()` assigns `mCurrentValue` **before** its
range check (`VariableCounter.hpp:239-243`), so a zero frame does reach
`getCurrent()`, the gate opens, and the hold expires in 10 s. The exposure needs
delivery to stop dead for more than 10 s, and no gap longer than **2117 ms** has
ever been recorded across 6,101 observations. Asserted, not measured: that a
disconnected or failed driver goes silent rather than publishing zeros. The
recording that would settle it does not exist — see §7.

### The fact that outranks the rest

**Two of the seven apps have never had their heart rate observed working on
hardware.** SleepLab has no pulled night and no captured `sleeplab.log` anywhere
in the tree; the rewritten Squash is the app whose failure started this. Both
read HR outside any activity session. The five that the prompt worried about are
exactly the five with field evidence.

If anything here is a priority, it is SleepLab — not the map apps.

---

## 1. The recommendation

**No `HrReader`, and no shared subscription code. One small header-only C++ file
that owns the two things already written twice, adopted per-app on touch rather
than as a campaign; the subscription rule written down at the one place a new
app will actually read it; and one two-minute hardware run that is already
built and has never been done.** Concretely: add **`HrKit`**, a header-only C++
kit on the `MapKit`/`SettingsKit` pattern (`Header/`, `Tests/`, `hrkit.cmake`,
`README.md`), holding exactly two things —
(a) `HrGate::isPlausible(bpm, trust)`, the four-times-copied predicate with its
20/300 and 1–3 numbers named and their provenance stated as *unknown, inherited
from the SDK examples*; and (b) `HrGate::Hold`, the union of `Spin::HrHold` and
`Squash::Freshness` — a trust hold *and* an arrival gate, because each app today
has one and needs both. It is header-only, free of SDK types, host-testable, and
crosses no ABI. It does **not** subscribe, does **not** parse, and does **not**
choose a sensor type. Separately and more urgently, **copy GpsLab's idempotent
retry into the other six Services** — two lines each, no hardware needed to be
safe, and it closes the failure mode in §0.2 whether or not that is what bit
Squash. A whole kit's scaffolding for ~80 lines is a real cost and
worth stating plainly: it is justified because there are two adopters *today* —
Spin and Squash each ship half of the freshness rule — and four more for the
predicate. Leave all seven subscriptions exactly as they are; change
an app's subscription only when that app is already open for another reason, and
then for the durability reason in §2, not because it is broken. Write the rule
into `Docs/SensorsLayer.md`-shaped prose upstream (§6) and into one comment in
`HrKit/Header/HrGate.hpp` pointing at `ExternalSensors.md`, because the pattern
propagates from the SDK's sample apps and no rule in this repository's
`CLAUDE.md` can reach them.

The argument for this rather than a reader is in §2. The argument for C++
rather than Rust is in §3. The short version: the duplication that costs
something is arithmetic over two floats, four apps that would have to adopt it
link no Rust at all, and a C ABI for a three-term boolean is indefensible.

---

## 2. What it owns, and what it does not

The prompt lists four candidate responsibilities. Each gets an argument, and two
are refused.

### Owns: the plausibility predicate

**In, because it is copied four times verbatim with unexplained literals, and
because it is pure arithmetic over two floats that a host test can pin.** The
one sentence that must go in the header is the one nobody has written: *where 20,
300, 1 and 3 came from is not recorded anywhere in this repository or in the
SDK's documentation; they arrived with the sample apps.* Naming them does not
make them measured, and the header must not pretend otherwise. What would settle
them: the distributions are already on disk. Measured over all 6,108 rows in
`Squash/Tests/pulled/*/imu_*_hr.csv` — trust takes only the values
{0: 424, 1: 3738, 2: 1126, 3: 820}, so the `<= 3` ceiling has never been
approached and the `>= 1` floor is the only half doing work; **4 rows carry
exactly 0 bpm** and every one of the other 6,104 lies between **64 and 160**. So
the 300 ceiling has never been exercised at all, and the 20 floor has only ever
had to separate zero from a real reading — a job `bpm > 0` would do equally well.
The untrusted fraction ranges from 0.0% to 44.4% *per recording* (the 44.4% is
the five-second smoke test, 4 rows of 9) and is 6.94% overall.

### Owns: freshness, as two gates rather than one number

**In, because the two existing implementations are each missing the other's
half** (§0.6), and because a wrong answer here is the failure this repository
cares most about: a plausible number that is not true.

Can one threshold serve a 1 Hz instrument and a once-a-second FIT record? **No,
and it should not try.** They are different questions and the answer is the split
Spin already wrote down in a comment at `Service.cpp:552`:

> *What the screen should believe, which is not the same question as what was
> measured this second: a momentary loss of confidence holds the last reading
> rather than blanking it. `prepareRecordData()` below still applies the strict
> gate, so the file records the second as having no reading.*

That is correct and it is the rule the header should encode: **the display may
hold, the file may not.** So `HrGate::Hold` exposes two results from one update,
not one — what to show, and whether this second has a reading — and the two
windows stay separate constants with separate derivations:

| Constant | Value | Provenance |
|---|---:|---|
| trust hold | 10 s | Spin's, asserted: *"long enough that a real artifact never shows, short enough that a watch taken off blanks while the wearer is still looking at it."* Never measured. |
| arrival gate | 3 s | Squash's, **measured** over 6,101 delivery gaps: max 2117 ms, none ≥ 3 s. Re-derive with `Tools/hr_analyse.py`. |

They do not converge and the header must say why: one bounds the transport, the
other bounds a judgement about the wearer.

### Does not own: which sensors to subscribe to

**Out.** This is the prompt's first candidate and the obvious thing to put in a
shared reader, and it is the one thing that should stay in each `Service.cpp`.

Three reasons. First, the seven Services differ in real ways that a subscription
helper would have to model: SleepLab duty-cycles its HR connection on and off
through the night (`pumpHrDuty`), GpsLab reconnects opportunistically from
`processTrack()`, Spin connects once per track, the map apps connect alongside
GPS. A helper covering all four is the sensor framework the prompt's §4
correctly warns against. Second, there is nothing to fix: EX alone works (§0.1),
so a shared subscriber would migrate five working apps to buy nothing
measurable. Third, and decisively — the subscription is four lines. The
duplication people actually trip over is the interpretation, and that is what the
header takes.

What every Service *should* gain, and it is not shared code: GpsLab's
`connectSensors()` shape — test `isConnected()` before each `connect()`, and pump
the whole function from the per-second tick while the track is active. It is
idempotent, it is a no-op once connected, and it is the only defence in this
repository against the ~100 ms ack race that §0.2 blames. Six Services should
copy it. It stays per-app because *when* to pump differs (SleepLab's duty cycle
must not be fought by a retry loop), which is the same reason the subscription
itself stays per-app.

What the map apps *would* gain by subscribing to both, and the honest reason to
do it when one is open anyway: `HeartRateEx::isDataValid()` requires
`getFieldCount() >= 7` where `HeartRate::isDataValid()` requires `== 2`. If a
future kernel ever trims or reshapes the EX frame, every EX getter returns
`0.f`, and an app reading BPM from EX cannot distinguish "no frame" from "a heart
rate of zero". That is a silent, permanent failure shape, which is the category
this repository's `CLAUDE.md` singles out. It is a durability argument, not a bug
report, and it does not justify five hardware sessions on its own.

### Does not own: the trust floor as a calibration knob

**Out, and it already has an owner.** `EffortKit/src/window.rs` treats the trust
floor as `Calibration { min_trust: Gate<u8> }` with `min_trusted_pct` derived
from measurement (*"5.3% of seconds are untrusted across 34 minutes of pulled
recordings, in runs of median length 1 and maximum 4 — so 90% allows six
untrusted seconds"*). A second, C++-side notion of a trust floor that could
disagree with it is exactly the "two implementations of one measurement" this
whole exercise exists to stop. `HrGate::isPlausible` takes the kernel's 0–3
confidence and answers *is this a reading at all*; anything about how far to
believe it stays in Rust.

### Does not own: per-source readings

**Out.** Only apps writing a sidecar or an extra FIT series want them — Squash's
`_hr.csv`, SleepLab's per-epoch provenance, and the map apps' `hr_source` field.
Three consumers, three shapes, all of them two lines of assignment from the EX
parser. `EffortKit::HrSource` already names the values and the map is the
kernel's own.

**So: a reader that owns all four is a framework; one that owns the first is
fifteen lines.** The line falls in a third place — it owns neither the
subscription nor the calibration, only the two pure decisions that were each
written twice.

---

## 3. The language and the seam

**C++, header-only, no new ABI. The seam does not move.**

The repository's split is real and the prompt states it correctly: pure
decisions testable without a kernel are Rust (`EffortKit`, `TextKit`,
`InscribedDisc`, `PanicKit`); anything holding SDK types is C++ (`MapKit`,
`SettingsKit`). An HR read straddles it. But the thing being shared here is not
the read — it is arithmetic over two floats and a countdown, and four of the
seven candidate adopters link no Rust at all.

The price of the alternative, measured against what the repository already
records: `EffortKit/README.md` documents why each app owns its own staticlib and
its own shim rather than sharing one, and `Docs/RECOVERY-CONVERGENCE.md` §3.3
settled that the C ABI lives in the per-app shim. Putting `isPlausible` in Rust
would mean **BikeMap, GpsLab, HikeMap and RunMap each grow a `rust/` directory,
a `Cargo.toml`, a `crate-type = ["staticlib"]`, a `#[panic_handler]`, a
fingerprint pair and a CI rebuild rule** — to call a three-term boolean. Spin and
Squash already have all of that, so for them it would be nearly free; for the
other four it is the entire cost of adoption, and they are the four that share
the duplicated predicate. Asserted, and it is not close.

Two constraints on the C++ side, so this does not become a second definition of
anything:

1. **Zero keeps one meaning.** `EffortKit/src/hr.rs` is explicit: `UNTRUSTED`
   is `0`, `clamp_bpm` maps NaN and anything below 0.5 to it, and zero is never a
   heart rate. `HrGate` must return a *boolean* and never a `float` that could be
   `0.f` for both "no frame" and "no beats" — the failure the prompt names in its
   §4 and the one that put `-- BPM` on a worn watch.
2. **The header names `EffortKit/src/hr.rs` as the owner of the vocabulary**, in
   one line, and defines no source enum of its own. `HrCsvLog::Source` already
   duplicates the three values a third time; it should be the last.

Where it lives: a new top-level `HrKit/`, **not** inside `EffortKit`. The
precedent runs the other way — `Docs/RECOVERY-CONVERGENCE.md` §2 moved
`TrainKit/cpp/SharedLog.{hpp,cpp}` *out* of the Rust kit into
`Spin/Software/Libs/`, and `EffortKit/` today holds `Cargo.toml`, `src`, `tests`
and nothing else. Shared C++ here lives in a top-level kit with `Header/`,
`Tests/` and a `.cmake`; `MapKit` and `SettingsKit` are both that shape.

---

## 4. What is given up

Four things, and the last two are the ones that matter.

**A single place to change the subscription.** If the SDK's advice changes, or a
kernel release reshapes `HEART_RATE_EX`, seven files still need editing. That is
the direct cost of refusing the reader in §2, and it is accepted because the
edit is four lines and the alternative is a framework over seven Services that
genuinely differ.

**The four map apps stay on a sensor the SDK tells them not to read BPM from.**
Under this recommendation BikeMap, GpsLab, HikeMap and RunMap keep gating display
*and* the FIT record on `HeartRateEx::isDataValid()` and its `>= 7` field count.
They work today, measurably. They are one kernel frame-shape change away from
silently writing no heart rate, and this design chooses to accept that rather
than spend five unverifiable hardware sessions now.

**Spin's hold semantics and SleepLab's batch read do not unify.** SleepLab gates
on `bpm > 0` with no trust term at all, deliberately — its ledger row A12 says
HR is reported for a night that failed the worn gate *because* it is a
measurement of the sensor rather than a claim about sleep. If SleepLab adopted
`isPlausible`, a night's `hrMin` would start excluding readings it currently
records, and row A12's reasoning would need redoing. **SleepLab should not adopt
the predicate.** It may adopt the arrival gate.

**The 10-second hold is imported unmeasured.** Its justification is a sentence of
taste in a header comment, and this design promotes it into shared code without
measuring it, which is precisely what `CLAUDE.md` says not to do. It is carried
because Spin ships it and removing it is a behaviour change nobody asked for —
but the header must carry the word *asserted* and name the recording that would
settle it (§7), or it will read as measured within a release.

---

## 5. The migration, app by app

**Do the last two rows first.** The connect retry is the only item that might be
fixing a live fault and needs no hardware to be safe; the `data.size()` log costs
one line and decides whether six apps have a silent sampling bug. Everything
above them is cleanup and can wait for an app to be open anyway. Nothing here is
a campaign; every row says what would verify it, and three rows say "do not".

| App | Change | What verifies it on hardware |
|---|---|---|
| **Squash** | Adopt `HrGate::Hold` in place of `Freshness.hpp`, gaining the trust hold it lacks. Add a `sensor_ts` column to `_hr.csv` from `parser.getTimestamp()`, and an `hrex_n` counter to `Debug/squash.log`. | One worn session: `squash.log` shows `hrex_n > 0` and the screen blanks within 3 s of the watch coming off. The new column is checkable offline against `t_ms` on the same file. |
| **Spin** | Adopt `HrGate::Hold`, gaining the arrival gate it lacks (§0.6). Replace the three copies of the predicate with `isPlausible`. | One ride: `hr_avg` and the zone seconds match a recording made before the change within the 1 bpm quantisation step. The gate is only observable if delivery stalls, which has never been recorded — so this is verified as *no regression*, not as a fix. |
| **SleepLab** | ~~Arrival gate only.~~ **Nothing.** This row was wrong — see [§9](#9-what-changed-in-the-implementing). **Do not** adopt the predicate either (§4). Run the probe night that reads `hrex_n` (§7). | The probe's `probe_log.csv`: `hrex_n`, `hrex_opt`, `hrex_ext`, `hrex_unk` over one night. This is ledger row S8 and it is the highest-value row in this whole pass. |
| **BikeMap, HikeMap, RunMap** | Replace the copied predicate with `isPlausible`. **Leave the subscription alone.** Consider adding `HEART_RATE` only when the app is next open for another reason. | One activity each, comparing `hr_avg`, `hr_max` and the record count in the `.fit` against a pre-change ride. A pure predicate swap is verifiable as byte-identical FIT output on the same inputs if a recording is replayed. |
| **GpsLab** | Replace the predicate. **Change nothing about its connect path** — it is the app the other six should be copying. | As above. |
| **All six except GpsLab** | Adopt GpsLab's idempotent `connectSensors()` and pump it from the per-second tick while a session is active. Do this **first**; it is the only item here that could be fixing a live fault. | Nothing new is required: an app that works today still works, because the retry is a no-op once connected. What it would prove is negative — that no session ever again reports a sensor that never produced. Pair it with a one-line log when a retry actually takes, which is how GpsLab knows its own race is real. |
| **All six `data[0]` readers** | Iterate the batch. **Not** a no-op: for the map apps it changes what reaches `mHrCounter.add()` and therefore the session average. | Log `data.size()` for one session first. If it is always 1, the change is free and should be made anyway on RustGuiPoc finding 6; if it is ever 2, the current code has been dropping readings and the average was already wrong. |

**Note what is not in this table.** No row changes which sensor a working app
subscribes to, and no row is justified by the prompt's premise that five apps are
losing heart rate. §0.1 removed that premise, and the migration shrank
accordingly: what is left is one possible live fault (the connect race), one
cheap measurement (`data.size()`), and a predicate and a gate that are tidying.

---

## 6. Upstream, because the pattern is not ours to stop

The prompt's §1.2 is right that this is one template propagating, not seven
mistakes, and its trap "upstream is not ours" is right that nothing in
`CLAUDE.md` can reach it. Two things should go upstream as PRs against the SDK,
and both are documentation:

1. **`Docs/SensorsLayer.md` does not document `HEART_RATE_EX` at all.** Its
   sensor-type table — the canonical one a developer reads first — lists
   `HEART_RATE` (0x41) and `HEART_RATE_METRICS` (0x42) and stops. 0x43 exists
   only in `ExternalSensors.md`, a document about BLE chest straps, under a
   heading that reads "Reading the HR source (optional)". An app author who never
   pairs a strap has no reason to open that file. Measured:
   `grep -n HEART_RATE Docs/SensorsLayer.md`.
2. **The five activity examples contradict the rule.** `Cycling`, `Hiking`,
   `Running`, `Treadmill` and `Workout` subscribe to `HEART_RATE_EX` only and
   read BPM from it, which `ExternalSensors.md` says in bold not to do. The
   HR-display examples `GlanceHR` and `HRMonitor` get it right. A one-line
   change to five examples stops the next seven apps inheriting it.

Neither is a bug report — EX alone demonstrably works — and both should say so,
or the PR will read as alarm about a defect that is not there.

---

## 7. What could not be settled, and what would settle each

| Question | Status | What settles it |
|---|---|---|
| Why the rewritten Squash showed `-- BPM` on EX only | **open**; best candidate is GpsLab's documented ~100 ms connect-ack race (§0.2) | Rebuild that revision with `LOG_INFO` on `mSensorHr.connect()`'s return and on `isConnected()` one tick later, and wear it. One session says whether the subscription ever took. Cheaper than the prompt's suggested experiment and aimed at the candidate with actual precedent. |
| How often `connect()` loses its ack race | **open**; GpsLab's comment asserts ~100 ms and its recovery log line exists to catch it | `grep` GpsLab's `"subscription recovered after a lost startup connect"` in any pulled log. **No GpsLab log has ever been pulled**, so the one app instrumented for this has never been read. |
| Whether `HEART_RATE_EX` delivers outside an activity session | **open** — it *resolves* (probe block `…X…`, 2026-08-18), delivery unknown | The same run. This is ledger row S8. |
| Whether the HR `DataBatch` ever holds more than one sample | **open** | `LOG_INFO("%u", data.size())` on the HR branch, one session. Decides whether six apps drop readings. |
| Whether HR delivery ever stops dead for >10 s | **open**; never observed in 6,101 gaps (max 2117 ms) | A recording with the watch deliberately removed mid-session for a minute, and again with the strap disconnected. Neither exists in `Tests/pulled`. Decides whether Spin's missing arrival gate is reachable. |
| Where 20, 300, 1 and 3 came from | **open**, and probably unknowable | They are absent from the SDK docs and from every commit message in this tree. Ask upstream, or treat them as inherited and say so in the header. |
| What the sensor's own cadence is, as distinct from delivery | **open** — every recording discarded it | The `sensor_ts` column in §5. One session then answers it retrospectively for everything already measured. |
| Whether the 10-second hold is right | **asserted**, never measured | The off-wrist recording above, plus the untrusted-run lengths already in `hr_analyse.py` (median 1, max 4 over 34 minutes). A hold shorter than the longest real untrusted run flickers; 10 s is 2.5× the longest observed. |

---

## 8. The diagnostic log

One paragraph, as asked. **Separate question, and the answer is probably no —
but not for the reason the prompt assumes.** `Debug/mapmanager_verify.log`,
`Debug/sleeplab.log` and `Debug/squash.log` do share a genuinely generic core —
append, bounded, restart at a cap, never fail the session, every write failure
ignored — and `SquashLog.hpp` and `Diag.hpp` open with near-identical paragraphs
about `LOG_INFO` needing a dev tool nobody has at 03:00 or on court. That is
three authors reaching one conclusion, which is the signature this repository
treats as evidence for convergence. But the tree holds **eight** of these, not
three — `SettingsKit/Header/DebugLog.hpp`, `MapKit/Header/MapKit/TileRequestLog.hpp`,
`Spin/EventLog.hpp`, `Spin/SharedLog.hpp`, `Squash/SquashLog.hpp`,
`Squash/ImuMarkerLog.hpp`, `SleepLab/Diag.hpp`, `SleepLab/Probe/ProbeLog.hpp` —
and one of them, `SettingsKit`'s, is already a shared append-to-a-file helper
that three apps outside SettingsKit include. It is not a drop-in for the other
seven: it compiles to nothing unless `SETTINGS_KIT_DEBUG_LOG=1`, it is scoped to
a kit's own sandbox, and it has no cap and no restart. So the question is not
"should a shared bounded appender exist" but "why did five authors write past the
one that half-exists", which is a different investigation with a different answer
and should not be smuggled into this one. It wants its own pass, and the first
thing that pass should do is establish what `DebugLog.hpp` would need in order to
become the other seven.

---

## 9. What changed in the implementing

Five things. One is a correction to a recommendation above, one upgrades a
finding from asserted to quoted, and one is a defect nobody had looked for.

### 9.1 The connect race is the SDK's own statement, not an inference

§0.2 argued from a comment in `GpsLab` that `connect()` can silently fail.
Implementing the retry meant reading `SensorConnection.cpp`, which says it
directly: a timed-out ack used to latch `mIsConnected` — *"the field failure
mode"* — and the gating was changed so that `mIsConnected` stays false
*"so the caller can retry"*.

So `connect()` is a synchronous call with a 100 ms budget that returns `false` on
a lost ack, and the SDK expects a retry. Six of seven apps ignored the return
value and had no second call site. That is now a confirmed defect class rather
than a hypothesis, and the fix is independent of whether it is what bit Squash.

### 9.2 Five apps could write a heart rate nobody had into the FIT file

Not looked for, and found while moving the FIT gate onto `HrGate`. Every map
app and Spin computed `hasHeartRate` from `mHrCounter.getCurrent()` and
`mTrackData.hrTrustLevel` — both written *only* inside the sensor handler. If
delivery stops, both keep saying the last thing they said, `hasHeartRate` stays
true, and **every remaining record of the activity carries the last heart rate
the watch saw**, indefinitely.

§0.6 identified this for Spin's screen. It reaches the file, and it reaches four
more apps than that section said.

It stays **latent, not observed**, on the same evidence as before: no gap longer
than 2117 ms exists in 6,101 recorded ones, and an off-wrist watch keeps
publishing zero-bpm frames rather than going quiet (`20260903-v0.6.0-5s-smoke`),
which the gate already handles. It needs delivery to stop dead. The recording
that would settle it still does not exist (§7).

All six now gate the record on `Hold::View::measured`.

### 9.3 SleepLab's migration row was wrong

§5 told SleepLab to adopt the arrival gate. It should not, and the reason is
structural: SleepLab **holds no latest reading**. It accumulates `hrSum` and
`hrCount` per epoch and emits `hrMeanX10` only when `hrCount > 0`, so an epoch
in which nothing arrived already reports absent. There is no stale value to
blank and a gate there would be dead code.

The row was written by analogy with the six activity apps without checking that
the analogy held. SleepLab keeps the duty-aware connect retry and nothing else.

### 9.4 `Hold::View` needed a third fact

The design had `View` carrying `bpm` and two flags. Spin's `EffortKit` feed
turned out to need a third: it passes the kernel's raw 0–3 confidence through
rather than collapsing it, deliberately, so it needs *"the stream is alive but
this second was not believed"* separated from *"there is no sensor any more"*.
`View::live` is that, and it is what the feed is now gated on — previously that
path had no staleness guard at all.

### 9.5 The sidecar's new column is backward-compatible, checked rather than assumed

`sensor_ms` is appended rather than inserted, and both existing readers were read
before relying on it: `EffortKit`'s `parse_hr` takes the first four fields
positionally and ignores the rest, and `Tools/hr_analyse.py` reads by header
name. The six-column recordings in `Tests/pulled` still parse unchanged, which is
what makes the column safe to add to a format the repository treats as a
contract.

### What did not get done

- **The two upstream PRs in §6.** They are against `una-sdk`, not this
  repository, and publishing them is a decision for whoever owns that
  relationship.
- **Everything in §7.** Every open question there needs the watch. The
  instruments are now in place for three of them: `Squash`'s `hr` line reports
  frames taken from each subscription and flags any batch carrying more than one
  sample, its `sensors` line reports which drivers resolved, and every app logs a
  heart-rate connect that only succeeded on a retry.
