# Prompt: Build a sensor profiler for the UNA Watch

You are an expert embedded C++ engineer who is also a competent metrologist. Your task
is to design and build **SensorLab**, a `Utility` app for the UNA Watch that measures
every property of every sensor an app can reach, and turns those measurements into a
document somebody else can act on. It lives in this repository (`watch-apps`), out of
tree, built against an SDK checkout found through `$UNA_SDK`.

The app is an instrument, not a product. Nobody wears it to learn something about
themselves. It exists so that four questions have written answers:

1. **What are the limitations?** Which sensor types have no producer at all, which
   deliver a frame that does not match the parser shipped to read it, which honour the
   period they are asked for and which ignore it, and what resolution the values
   actually have.
2. **Does the platform conform to its own spec?** The SDK's headers, `Docs/SensorsLayer.md`
   and the parsers each make claims. They disagree with each other already, and at
   least one of them disagrees with the hardware. Every disagreement is a finding.
3. **What changed with this firmware?** Two profiles of the same device on two firmware
   versions must diff into a change list, mechanically, with no judgement required to
   read it.
4. **How much of this do we actually know?** Every claim carries a confidence, every
   sensor carries a completeness, and the report is honest about the parts nobody has
   measured yet.

Three things make this harder than it looks, and they set the shape of the work:

1. **A profiler that guesses is worse than no profiler.** A wrong number in a document
   titled "sensor profile" propagates into every app built on it, and it propagates
   with a false pedigree. Every figure this app emits must carry its method and its
   sample size, or not be emitted.
2. **Almost nothing can be tested off-device.** The simulator has exactly four sensor
   sources — battery level, GPS/step counter, heart rate and pressure
   (`Libs/Header/SDK/Simulator/Components/Simulator/`). So the simulator can exercise
   the screens, the statistics and the report writer, and can tell you nothing whatever
   about a sensor. Every sensor claim comes from hardware or it does not exist.
3. **Measuring a sensor changes it.** Subscribing costs power and IPC; two apps
   subscribing to the same type contend (ledger row S8); a sweep that reconnects
   repeatedly is not measuring a steady state. Design the probes so the act of
   measuring is part of the recorded method.

Work in tiers. Each tier ships working, tested and honest before the next one starts.

---

## 0. Context: read these first

Do not skip this section. Most of the difficult findings this app would otherwise
rediscover are already written down, and rediscovering them costs nights.

### In `una-sdk`, on the `research` branch

`git show research:RESEARCH-INDEX.md` — **read this first.** Two of its entries are
load-bearing here:

- **`Docs/Investigations/2026-07-29-hardware-config-recovery/`** — the hardware
  inventory. This is what tells you which silicon each sensor type is a view onto,
  which is the only way "conformance to spec" means anything at the datasheet level.
  It also established **the verification convention this app must adopt**: every claim
  tagged CONFIRMED / LIKELY / UNVERIFIED / REFUTED with the corroborating method that
  earned the tag. Its `service-cpp-instrumentation-sweep7.cpp` is the register/I²C read
  primitive, relevant to the optional Tier 5.
- **`Docs/Investigations/2026-06-15-heart-beat-vs-ppg/`** — a UNA maintainer's
  authoritative answer on what the optical path can produce (PR #167), plus
  `BeatProbe.hpp`, a runnable 90-second diagnostic. `HEART_BEAT` emits nothing because
  HR detection is frequency-domain rather than per-beat; the PPG waveform is described
  as 20 Hz single channel. Both statements have an expiry date, and re-checking them is
  a SensorLab probe, not a separate app.

`Docs/companion-data-channel-analysis.md` on the same branch is why settings come from
a JSON file in the app's folder and not from a phone.

### In `una-sdk`, on `main` / `apps-v1.4.0`

- `Libs/Header/SDK/SensorLayer/SensorTypes.hpp` — **the authoritative type list.**
  `Docs/SensorsLayer.md`'s table is already behind it: the doc is missing
  `HEART_RATE_EX` (0x43), `STEP_COUNTER_DAILY` (0x52), `RUNNING_CADENCE` (0x53),
  `FLOOR_COUNTER_DAILY` (0x61), the `ACTIVITY_TIME` / `ACTIVITY_TIME_DAILY` split
  (0xE0/0xE1) and `GRADE` (0x150). That gap is itself finding number one, and it is the
  reason this app derives its type list from the header at build time rather than from
  a table somebody typed.
- `Libs/Header/SDK/SensorLayer/DataParsers/` — 29 parsers covering 32 of the 37 types.
  Read `SensorDataParserRunningCadence.hpp`'s comment before anything else: *"This is a
  breaking wire-format change: Field::COUNT shrank from 4 to 2."* A frame's field count
  has already changed once between firmware lines. That is the single best argument for
  this app existing.
- `Libs/Header/SDK/SensorLayer/SensorData.hpp`, `SensorDataView.hpp`,
  `SensorDataBatch.hpp`, `SensorConnection.hpp` and
  `Libs/Header/SDK/Messages/SensorLayerMessages.hpp` — the whole app-facing contract,
  and small enough to read in full. Do that.
- `Docs/ExternalSensors.md` and `Libs/Header/SDK/Messages/AccessoryMessages.hpp` — the
  BLE strap acquisition path and `HEART_RATE_EX`'s provenance fields.
- `Examples/Apps/Stopwatch/` for the minimal documented shell, `Examples/Apps/Timer/`
  plus `Docs/Examples/Timer-Architecture.md` for the deadline-bounded sleep idiom,
  `Docs/Tutorials/Sensors/` for the subscribe-and-parse loop, `Docs/unit-testing.md`,
  `Tests/Host/support/KernelTestDoubles.hpp`, `Tests/Host/support/FakeFileSystem.hpp`,
  `Docs/Simulator.md`, `Docs/app-config-json.md`.

### In this repository

- **`SleepLab/Docs/FEASIBILITY-LEDGER.md` — read all of it, twice.** It is the closest
  thing that exists to a sensor profile of this device, it was produced as a side
  effect of building something else, and §2 of it is the starting corpus this app
  inherits. Rows S1–S18 are quoted throughout this document. Note especially which of
  them are REFUTED, and that three of them were refuted by *reading code*, not by
  measurement.
- `SleepLab/Probe/README.md` and `SleepLab/Docs/POST-MORTEM.md` — the Sleep Probe is
  SensorLab's direct ancestor: a one-line-per-minute delivery counter over thirteen
  sensor types. The post-mortem's "Should the probe and SleepLab stay separate?" section
  contains the argument that its unique value is *a screen you read before bed rather
  than a log you read after*. SensorLab supersedes the probe's log and must keep its
  screen.
- `SleepLab/Docs/IMPLEMENTATION-PROMPT.md` — the prompt style this document follows.
- `FwDump/README.md` — resumable long-running background work, chunk-and-manifest file
  discipline, and the demonstration that a service keeps working with the display
  blanked. Also the precedent for privileged direct hardware access from an app, which
  is Tier 5's whole basis.
- `MapManager/README.md` — the only written account of what a long-lived service does
  on this device: USB termination, uptime semantics, sleeping to next due work, and
  doing a *budget* of I/O per wake rather than one chunk.
- `Barcode/README.md` — the JSON-file-in-the-app-folder settings pattern.

---

## 1. The evidence model

This section is the app. Everything else is plumbing for it. Build it first, in host
tests, before a single sensor is subscribed.

### 1.1 Two axes, kept separate

**Confidence** is a property of one claim. It uses the four tags the hardware-recovery
investigation established, plus one this app needs:

| Tag | Means |
| --- | --- |
| **CONFIRMED** | Measured directly on this hardware, with n recorded, by a probe whose method is written down. |
| **LIKELY** | Inferred from another measurement, or from documentation that is itself unverified. Good enough to design against; not good enough to state as fact. |
| **UNVERIFIED** | Nobody has checked. The row names the probe that would check it. |
| **REFUTED** | Checked and found false. Kept on the record, because a claim that was believed and is now known wrong is worth more than a deleted one. |
| **INAPPLICABLE** | The claim cannot exist for this sensor — a dt distribution for an event sensor that publishes on state change, a range check on an enum. Distinct from UNVERIFIED, and the report must not count it as missing. |

**Completeness** is a property of a *sensor*, and it is a fraction, not a tag. Each
sensor type has a fixed checklist of applicable probes (§3). Completeness is
`answered / applicable`, where answered means any tag other than UNVERIFIED, and
applicable excludes INAPPLICABLE. Report it per sensor, per probe layer, and once
overall. A profile that is 40 % complete and says so is a useful document; one that is
40 % complete and looks finished is a liability.

**Never combine the two into a single score.** A sensor with one CONFIRMED row out of
twelve is not "8 % confident"; it is 8 % complete and confident about one thing.

### 1.2 Every claim is a row

One row, one claim, one method. The row is the unit of storage, the unit of display and
the unit of diff:

```
claim_id        stable, e.g. "0x10.timing.dt_median" — never renamed, never reused
sensor_type     0x10
metric          dt_median
verdict         CONFIRMED | LIKELY | UNVERIFIED | REFUTED | INAPPLICABLE
value           20.4              (numeric, or absent)
unit            "ms"
n               148231            (samples, or runs, that produced it)
spread          p05/p50/p95, or stddev, where a distribution exists
method_id       "P4.dt-histogram" — indexes the probe catalogue in §3
run_id          which run produced it
observed_at     uptime_ms and wall clock, both
expected        the spec's claim, where a spec makes one
expected_source "SensorTypes.hpp:41" | "SensorsLayer.md table" | "BMI270 DS §4.2" | absent
conformance     MATCHES | DIFFERS | NO_CLAIM | UNTESTABLE
notes           free text, short
```

`claim_id` stability is what makes firmware diffing possible, so it is a naming
decision with consequences. Fix the scheme once, write it down, and never renumber.

### 1.3 Rules that keep the document defensible

- **No claim without a run.** A row with no `run_id` cannot be written.
- **No distribution below its minimum n.** State the minimum per probe (§3). Below it,
  the verdict is UNVERIFIED with a note saying how many samples were seen.
- **A negative result is a result.** "`SPO2` does not resolve a driver" is a CONFIRMED
  row, not an absent one. Ledger row S4 is exactly this, and it closed a design
  question permanently.
- **Distinguish "no producer" from "produced nothing".** A type whose
  `RequestDefault` fails has no driver to subscribe to. A type that resolves and then
  emits nothing in an hour is a different finding with a different cause. The Sleep
  Probe's screen encoded this as case: upper-case letter = driver resolved, lower-case
  = did not. Keep that distinction everywhere.
- **Record the act of measuring.** Every run's manifest carries what else was
  subscribed at the time, what the requested period and latency were, and whether the
  screen was on. A dt distribution measured while eight other types were streaming is
  not the same measurement as one taken alone, and both are worth having as long as
  which is which is recorded.
- **Quote the spec, don't paraphrase it.** `expected_source` is a file and line, a doc
  section, or a datasheet section and page. If you cannot cite it, `expected` is absent
  and `conformance` is NO_CLAIM.
- **Datasheet figures get looked up, not remembered.** §4 lists the parts. Fetch the
  actual datasheets, cite section and page, and tag anything you could not source as
  UNVERIFIED rather than writing a number you are fairly sure of.

---

## 2. Platform constraints (hard requirements)

Everything here is CONFIRMED in `SleepLab/Docs/FEASIBILITY-LEDGER.md` unless marked
otherwise, and each is cheap to violate by accident.

**SDK and firmware.** Target `apps-v1.4.0` (`main` is currently the same commit — row
P2). A `.uapp` built against 1.4 carries `KERNEL_INTERFACE_VERSION` 3 and the kernel
refuses to run it on a 1.3 device, with no build-time warning and an instant `App PID`
error screen as the only symptom. **Confirm the watch's firmware version before writing
code, and record it in every profile manifest** — for this app the firmware version is
not context, it is the primary key.

**App shape.** `APP_TYPE "Utility"`. **`APP_AUTOSTART Off`** — and this is a design
decision, not an omission. Two autostart apps that both claim the accelerometer
contend (row S8), SleepLab is the one that has to run every night, and a profiler that
silently competes with it would corrupt the thing it exists to measure. SensorLab runs
when opened. Long unattended runs are started deliberately by the user and are the
exception. Note that a `Utility` app cannot be service-only — the merger requires a GUI
ELF for every type except `Glance` (row P3) — and that every app is built
glance-capable regardless of type (row P4), so a glance is available without changing
type if a Tier 4 wants one.

**Runtime.** Cortex-M33 (STM32U5A5), hard-float, `-Os -fPIC -fno-exceptions -fno-rtti`,
C++17. **No exceptions, no RTTI.** Service 500 KB / 10 KB stack, GUI 600 KB / 10 KB
stack. Apps do not create threads: two single-threaded blocking message loops, timers
polled. Fixed-size buffers; no allocation in the sample path after init. **This app's
sample path is hotter than any other app in the repo** — measured 308 accelerometer
batches a minute at 9.6 samples each (row S17) for one sensor, and this app may have a
dozen subscribed. Size the statistics accumulators so that per-sample work is a handful
of adds and comparisons, and never a division.

**IPC.** The largest pool block is 256 bytes (row P10, LIKELY), `MessageBase` is 40 of
them (row P13), and `MessageBase` deletes copy-assignment so a payload the GUI needs to
keep must be a separate plain struct (row P12). `#pragma pack(push, 4)` and
`static_assert(sizeof(T) <= 256)` on every custom message. A 37-row roster does not fit
one message; decide deliberately how it reaches the GUI — indexed burst, or the GUI
reading the file itself — and write the reason down.

**Two clocks, and you need both.**

- `kernel.sys.getTimeMs()` is device uptime: 32-bit, wraps at ~49.7 days, survives an
  app restart, resets only on device reboot (row P9). Unsigned subtraction everywhere.
  This is the monotonic clock every duration comes from.
- Wall clock comes from `time(nullptr)` + `localtime_r` (see `Examples/Apps/Alarm`). It
  can jump. Never derive a duration from two readings of it.
- **Sample timestamps are a third clock**, and reconciling them with the first two is
  itself a probe (§3, layer 4). `DataView::getTimestampUs()` computes
  `mTimeStamp * 1000 + mTimeStampUs`, which encodes an assumption — that `mTimeStamp`
  is milliseconds and `mTimeStampUs` is a sub-millisecond remainder under 1000. Test
  it. If `mTimeStampUs` ever exceeds 999, every microsecond timestamp in every app on
  this platform is wrong, and nobody has checked.

**USB terminates everything.** Plugging in terminates every running app; autostart
relaunches on unplug (row P8). So **a profiling run cannot be watched over USB**, and a
run that ends at a plug-in event must be marked as truncated rather than complete.
Subscribe to `BATTERY_CHARGING` so "it was on the cable" is in the data rather than in
someone's memory.

**Storage.** `open(write, override=false)` positions the handle at **offset 0, not
end-of-file** (row P6) — appending requires an explicit `seek(size())`. This cost
SleepLab a silent data-loss bug found only by a host test. Sustained one-row-a-minute
open-seek-write-flush-close survived 8.45 h with no failure (row S9); this app writes
far more, so cap every writer by bytes and by duration, and measure your own throughput
as part of the profile.

**Three builds that disagree** (row P14, confirmed twice): the ARM build globs
`Sources/*.cpp` while the simulator's Makefile enumerates them, so a new source packs
into a `.uapp` and fails to link the simulator; and `timegm` exists in glibc (host tests
and simulator) but not in the watch's newlib. **All three builds must be run before a
branch is believed.**

**Simulator caveats.** It does not deliver `COMMAND_APP_NOTIF_GUI_RUN` to the service
(row T5) — treat the GUI's first message as the evidence a GUI is attached. Its shutdown
path calls a pure virtual method, so a simulator run's exit status is meaningless
(row T6, fixed on `una-sdk`'s `fix/simulator-shutdown-pure-virtual`). And its sample-rate
adapter thins delivery on a half-period boundary in quantised bands — pinned by a test
on `una-sdk`'s `feat/sample-rate-adapter-rule` — which the hardware demonstrably does
not do (row S3). **Do not calibrate anything against the simulator.**

### 2.1 Hazards in the sensor API itself

Found by reading the headers for this document. Each is a landmine for the profiler
specifically, because the profiler is the first thing that will exercise these paths
across all 37 types rather than the two or three an app needs.

- **`SDK::Sensor::Connection` stores the handle as `uint8_t`** while
  `RequestDefault::handle` is `uint32_t` and `matchesDriver()` takes a `uint16_t`. Any
  handle above 255 truncates silently. Record every handle you are given at full
  width, from the raw message, and do not let `Connection` be your only record of it.
- **`Connection::connect(period, latency)` rejects parameter updates while connected.**
  A period sweep must `disconnect()` between points, and each point therefore includes
  a resubscribe — which is part of the method, and must be in the notes.
- **`Connection` only ever sends `RequestDefault`.** `RequestList` (up to 10 handles per
  type) and `RequestGetDesc` (a 32-char descriptor string) have **never been used by
  any app in either repository** — they are implemented in the simulator's dispatcher
  and nowhere else. They are the cheapest existence and identity probe available and
  this app is what they were for. Send them yourself, as raw messages.
- **`GpsLocation::isDataValid()` reads `mData.u[COORDS_VALID]` before checking the
  field count**, so a short frame is an out-of-bounds read. Generally: **derive the
  field count from the batch's stride and validate it yourself before constructing any
  parser.** The profiler's whole job is to meet frames that do not match their parser,
  which is precisely the input the parsers were not written for.
- **`isDataValid()` is exact field-count equality in 28 of the 29 parsers.**
  `HeartRateEx` is the one exception, using `>=` deliberately so a future kernel can
  append fields without breaking apps; three others (`MotionDetect`,
  `ActivityRecognition`, `GpsLocation`) add a range check on top of the equality. So for
  every type but one, a single appended field silently invalidates every sample. That asymmetry is a
  conformance finding in its own right and belongs in the report.

---

## 3. The probe catalogue

Eight layers. Each is a method with an id, a minimum n, a set of claims it produces,
and a rule for which sensors it applies to. **The catalogue is the spec of the app**:
the roster screen, the completeness fraction and the report are all generated from it,
so define it in one place in code and derive everything else.

### Layer 1 — Existence and identity  (`P1`, applies to all 37 types)

For every type in `SensorTypes.hpp`, in one pass, taking seconds:

- `RequestDefault` → resolves or does not. Record the handle at full 32-bit width.
- `RequestList` → how many drivers exist for this type, and their handles. Nobody has
  ever seen this answer. More than one driver for a type would be news.
- `RequestGetDesc` per handle → the 32-char descriptor. This is the kernel naming its
  own driver, and it is the closest thing to an authoritative part identification an
  app can obtain. Expect it to corroborate or contradict the hardware inventory in §4.
- Whether `connect()` then succeeds, which is a separate question from resolving.

Deliverable: a 37-row existence table, which is the first thing the app should be able
to produce and the first thing worth publishing. Ledger rows S4 and S5 are two cells of
it, obtained the hard way.

### Layer 2 — Frame structure  (`P2`, all types that resolve)

- Delivered field count, derived from `EventData::stride`:
  `1 + (stride - sizeof(Data)) / sizeof(Data::Field)`, which is `DataBatch`'s own
  arithmetic. Assert the stride's divisibility rather than trusting it.
- Compare against the parser's `getFieldsNumber()` where a parser exists →
  `MATCHES` / `SHORT` / `EXTENDED`.
- Whether the shipped `isDataValid()` accepts the frame the device actually sends.
- **For the five types with no parser** — `MAGNETIC_FIELD` (0x30), `HEART_BEAT` (0x40),
  `GESTURE_RECOGNITION` (0xD0), `PPG` (0xF0), `ECG` (0x100) — the delivered field count
  and the per-field value behaviour are the *only* description of the frame that
  exists anywhere. Publishing a discovered layout for any of them is a genuine
  contribution to the SDK's documentation, and the honest way to publish it is as a
  measured layout with an explicit "field semantics inferred, not documented" caveat.
- Stride stability: does the field count ever change *within* a run, or between runs?
  `RunningCadence`'s 4→2 shrink says the answer is not automatically no.

### Layer 3 — Liveness  (`P3`, all types that connect)

- Time from `connect()` to first sample. An event sensor may legitimately never produce
  one; a streaming sensor taking 40 s to start is a finding.
- Samples, batches and samples-per-batch, per minute, with the distribution of
  samples-per-batch rather than only its mean.
- Longest gap. **Do not report a rate without also reporting the longest gap** — a
  sensor delivering its nominal average in two bursts an hour apart is not delivering
  at that rate, and an epoch-based consumer would be silently wrong (SleepLab rows S14
  and S15 are the consequence of exactly this).
- Classify each type as **streaming** (periodic samples) or **event** (publishes on
  change), *from the data*, and record the classification. This is not cosmetic:
  `TOUCH_DETECT` was assumed streaming, delivered zero samples in a minute, and read as
  "not worn", which would have suppressed every night SleepLab ever recorded (row S12).
  One touch sample in 507 minutes is the measured behaviour (row S7). For an event
  sensor, dt statistics are INAPPLICABLE and the rate to report is events per hour.

### Layer 4 — Timing  (`P4`, streaming types; minimum n = 10 000 samples)

- Inter-sample dt from the sample timestamps: min, p05, p50, p95, max, and a histogram
  coarse enough to store. Delivered rate is `1/p50`, and it is reported alongside its
  spread, never alone.
- The `mTimeStampUs < 1000` invariant from §2. Count violations.
- Monotonicity: does any sample timestamp go backwards, within a batch or across
  batches?
- Skew: sample timestamp against `getTimeMs()` at batch arrival, tracked over hours.
  Do they drift apart? A constant offset is a fact worth knowing; a growing one means
  they are different oscillators and no app should mix them.
- Batch arrival jitter, separately from sample dt. Measured 195 ms apart against a
  requested 5000 ms latency (row S17).
- **Wrap behaviour.** `getTimeMs()` wraps at 49.7 days and nobody has observed it. This
  app cannot wait for it, but it can and must prove its own arithmetic against a
  synthetic wrap in host tests, the way SleepLab did (row P9a).

### Layer 5 — Value domain  (`P5`, per field of every type that produces data)

Per field, streaming or event:

- min, max, mean, and a histogram with the bin width recorded.
- Count of NaN, ±Inf and denormals. One non-finite sample once poisoned every
  subsequent SleepLab epoch to exactly zero; they are not hypothetical.
- **Effective resolution**: the smallest non-zero absolute difference between
  consecutive distinct values, over the run. For a float field fed from an integer ADC
  this recovers the LSB, and the LSB recovers the configured range — which is how you
  learn whether the accelerometer is in ±2 g or ±16 g without reading a register.
  Caveat it properly: it is a *lower bound* on quantisation and only meaningful if the
  value actually varied.
- Stuck-value detection: the longest run of byte-identical values, and whether the
  field ever changed at all. This is what caught `BATTERY_LEVEL` reading 100.0 % at
  both ends of an 8.45 h night in which the fuel gauge lost 10 mAh (row S18) — a sensor
  that is not broken enough to be absent and not working enough to be usable, which is
  the hardest failure mode to notice and the most important to publish.
- Rail behaviour: does the value saturate at a suspiciously round bound?
- For enum fields (`MOTION_DETECT`, `ACTIVITY_RECOGNITION`, `HEART_RATE_EX`'s source):
  which values were ever observed, and any value outside the documented set.
- For boolean-in-a-u32 fields: any value other than 0 and 1.

### Layer 6 — Control-surface response  (`P6`, streaming types)

The generalisation of ledger row S3a, which asks whether the requested accelerometer
period does anything at all, to every sensor:

- Sweep requested period over a grid (say 5, 10, 20, 40, 80, 160, 320, 1000 ms),
  disconnecting between points, several minutes per point, and plot delivered rate
  against requested. **Three outcomes and they matter differently:** honoured (the
  period is a lever, so power is tunable), ignored (the rate is a given, and every
  comment in every app that budgets from a requested period is wrong), or partially
  honoured with a floor or a quantisation. Row S3 measured ~48 Hz delivered against
  25 Hz requested for the accelerometer — *nearly double, in the opposite direction to
  the simulator's documented thinning*. Nobody has checked a second sensor. Heart rate,
  in the same minute, delivered exactly its requested 1 Hz.
- Same sweep for latency. Row S17: 5000 ms requested, 195 ms delivered, REFUTED.
- Reconnect behaviour: does `disconnect()` then `connect()` return the same handle,
  restart cleanly, or leak? Do it 100 times and see.
- Contention, deliberately: two connections to the same type from the same app; and if
  it can be arranged safely, a second app subscribed at the same time. Row S8 is
  CONFIRMED only for the no-other-app case, and the half that governs whether two
  utilities can be published together is untested. Measure it here, once, properly, so
  no future app has to.
- Whether `connect()` on an already-connected handle fails the way the header says.

### Layer 7 — Physical truth  (`P7`, interactive, guided)

This is where the app earns "profiling" rather than "counting", and it is why the app
is interactive. Each protocol is a short on-screen script with timed steps, a live
readout, and a pass/fail plus an error figure per step. The user is the reference
instrument, so the instructions must be unambiguous and the tolerances stated on screen
before the step runs.

- **Accelerometer**: six-face static test. For each of the six axis-aligned
  orientations, hold still for 5 s. Extract per-orientation mean vector. Deliverables:
  bias per axis, scale error per axis, cross-axis sensitivity, noise density at rest,
  and total vector magnitude against 1 g. This is the standard six-position calibration
  and it takes ninety seconds. Also: does the *raw* variant scale to the float variant
  by a constant, and what is that constant (→ configured range, cross-checked against
  layer 5's LSB estimate)?
- **Gyroscope**: still-bias (zero-rate offset per axis, and drift of it over minutes);
  then a rotation test against a countable reference — a full manual 360° turn about
  each axis, integrating rate to angle, error reported as a percentage. Crude, and
  crude with a stated tolerance is worth more than nothing.
- **Magnetometer** (no parser, so layer 2 must discover the frame first): field
  magnitude against the local field strength from a public geomagnetic model at the
  recorded location, and the classic figure-eight rotation to see whether magnitude
  stays constant — which is the calibration state, and is the actual question.
- **Barometer / altimeter / temperature**: pressure against a nearby weather station's
  QNH at a recorded time (and record the station and the time, so the comparison is
  reproducible). Altitude against a surveyed reference point. A known altitude change —
  a stairwell of counted, measured flights — as a differential test, which is far more
  defensible than an absolute one. Temperature against a room thermometer, with the
  watch off the wrist for ten minutes first, and **labelled ambient, never body**.
  Note the standing puzzle: the hardware investigation exhausted all six I²C buses
  looking for the MS5837's fixed address and never found it (§4). If these three types
  resolve drivers and produce plausible values, that is evidence about the hardware
  question, and it belongs in both documents.
- **Step counter / detector / cadence**: walk a hand-counted 100 steps, three times.
  Report bias and spread against the count. Then the negative tests, which are the
  useful ones: 60 s of arm-waving while seated, 60 s of a car or bus ride, 60 s of
  hand-washing. False-positive rate per minute per activity.
- **Floor counter**: a counted number of real flights up and down.
- **Heart rate**: against a chest strap worn simultaneously, at rest, walking, and
  recovering from exertion — three regimes, because optical HR fails differently in
  each. Report mean signed error and spread per regime, and correlate error with the
  reported `TRUST_LEVEL`, which tells you whether trust is informative. Use
  `HEART_RATE_EX` so provenance is recorded rather than assumed. Also: strap on and
  strap off, to see arbitration switch source (§3, layer 8).
- **`TOUCH_DETECT`**: worn tight, worn loose, off-wrist on a table, and off-wrist held
  in a hand. Transitions per hour in each. The measured flicker rate on a sleeping
  wrist is zero over 507 minutes (row S7) — remarkable, and worth re-confirming on new
  firmware because so much rests on it.
- **`ACTIVITY_RECOGNITION` / `MOTION_DETECT` / `WRIST_MOTION` / `GESTURE_RECOGNITION`**:
  a labelled protocol — 2 minutes each of sitting still, typing, walking, running,
  cycling, driving — producing a confusion matrix against the label the user selected.
  For `WRIST_MOTION`, twenty deliberate wrist-raises: detection rate and latency. For
  `GESTURE_RECOGNITION` (no parser), whether it produces anything at all, and if so
  what the frame looks like and which gestures trigger it, which is currently
  undocumented anywhere.
- **GPS**: a stationary open-sky soak — position scatter (CEP50 / CEP95) against the
  run's own mean, and against a surveyed point where one is available; reported
  `PRECISION` against measured scatter, which is the question that matters, because an
  error estimate nobody has checked is decoration; time to first fix from cold, warm
  and hot starts; a walked closed loop for `GPS_DISTANCE` against a measured route, and
  against the haversine sum of its own `GPS_LOCATION` fixes; `GPS_SPEED` at a
  hand-timed known distance. Indoor behaviour: does it report a fix it should not?
- **`SPO2`**: nothing to test until it resolves a driver, which it does not (row S4).
  Keep the probe in the catalogue, tagged INAPPLICABLE with the reason, so that a
  firmware update turns it back on automatically.
- **`HEART_BEAT` / `PPG` / `ECG`**: existence probes only, re-run every firmware
  version. `HEART_BEAT` resolves no driver today (row S5). `PPG` has never been
  observed with `"ppg": "on"` on hardware, and given the other two, there is a real
  chance it has no app-facing driver either — which would close the on-device HRV route
  and is worth knowing definitively. `ECG` on a device with no electrodes is expected
  absent; confirm it, so the report can say so rather than leaving a hole.

Every protocol writes its raw samples alongside its verdict. A protocol whose raw data
was discarded cannot be re-analysed when the analysis turns out to be wrong, and it
will be.

### Layer 8 — Cross-sensor consistency  (`P8`, automated, no reference equipment)

**This is the highest-value layer per unit of effort, and it is unattended.** Every
check here is internal to the device, so it needs no strap, no weather station and no
surveyed point — and it tests exactly the derived-value plumbing that has no other
oracle.

- `ACCELEROMETER` vs `ACCELEROMETER_RAW`, and `GYROSCOPE` vs `GYROSCOPE_RAW`: is the
  ratio constant, and what is it? (→ the configured full-scale range.) Do the sample
  timestamps line up, or are they separate pipelines?
- `FUSION` vs `ACCELEROMETER` + `GYROSCOPE`: same values, or a different pipeline with
  different rates and different timestamps? `FUSION`'s doc comment says "accel+gyro+mag"
  while its parser has six fields, which is accel+gyro only. Which is right?
- `FUSION_RAW` vs the two `_RAW` types.
- `ALTIMETER` vs `PRESSURE` through the barometric formula, with the sea-level pressure
  the `PRESSURE` frame itself reports. Does `PRESS_SEA_LEVEL` ever differ from a fixed
  1013.25 hPa — i.e. is it a real input or a constant?
- `GRADE` vs the time derivative of `ALTIMETER` over horizontal distance from GPS.
  `GRADE` is undocumented in `SensorsLayer.md` and this is the only description of it
  that will exist.
- `STEP_COUNTER` vs counted `STEP_DETECTOR` events over the same interval. They should
  agree exactly. If they do not, that is a finding with immediate consequences for
  every activity app.
- `STEP_COUNTER` vs `STEP_COUNTER_DAILY`, `FLOOR_COUNTER` vs `FLOOR_COUNTER_DAILY`,
  `ACTIVITY_TIME` vs `ACTIVITY_TIME_DAILY`: do the daily variants reset at local
  midnight, and to what — and what happens to them on a timezone change? **This needs a
  run across midnight**, which makes it the one automated probe with a scheduling
  constraint. It is also the only test of a contract the header states and nothing
  verifies.
- Monotonicity of the "since boot" counters: across an app restart (they should
  survive) and across a device reboot (they should reset). Both are cheap to arrange.
- `RUNNING_CADENCE` vs step-detector rate over the same window.
- `HEART_RATE` vs `HEART_RATE_EX`'s arbitrated field — the parser's own comment says
  they are the same value. Confirm it. And when a strap is connected, that
  `getSource()` moves to EXTERNAL and the optical field keeps reporting independently.
- `HEART_RATE_METRICS_DAILY`'s AHR/RHR against the day's own `HEART_RATE` samples.
- `BATTERY_LEVEL` vs `BATTERY_METRICS`: percent against `CAPACITY / DESIGN_CAPACITY`.
  Row S18 says the percent gauge did not move at all across 8.45 h while `mAh` fell by
  ten. Establish the relationship properly, over a full discharge if patience allows,
  and settle the sign convention of `CURRENT` and `AVG_CURRENT`, which the ledger flags
  as an unverified firmware contract.
- `TOUCH_DETECT` vs accelerometer micro-movement: does a "worn" verdict coincide with a
  wrist that is actually moving?
- Uptime vs wall clock over many hours: relative drift, which is a measurement of the
  RTC against the systick and is free.

---

## 4. Conformance: against what, exactly

"Conformance to spec" needs a named spec per claim, and this platform has four, which
disagree. Rank them explicitly in the report:

1. **The headers**, which are authoritative for the app-facing contract: type values,
   field counts, field order, units in comments, enum ranges.
2. **`Docs/SensorsLayer.md`**, which is behind the headers and is therefore mostly a
   source of *documentation* findings rather than *behaviour* findings. It is missing
   six types; its `ACTIVITY` row describes "active minutes" and then gives the field as
   `u32 ms`, contradicting both itself and the parser, whose comment says minutes; its
   `AMBIENT_TEMPERATURE` unit carries a literal question mark. Report the doc/header
   divergences as their own small table. They are the cheapest findings in this project
   and the easiest for UNA to act on.
3. **The maintainer answers** recorded on `una-sdk@research` (PR #167 on the optical
   path) — authoritative about intent and about firmware, with dates, and with expiry
   dates.
4. **The silicon datasheets**, via the hardware inventory. This is where "limitation"
   becomes quantitative: the difference between what the part can do and what the SDK
   exposes is the headroom, and headroom is what a feature request is made of.

The parts, from `Docs/Investigations/2026-07-29-hardware-config-recovery/` (tags are
that investigation's, not yours):

| Function | Part | Tag there | Why SensorLab cares |
| --- | --- | --- | --- |
| IMU (accel + gyro) | **BMI270** (Bosch) | **CONFIRMED** twice — `CHIP_ID` reg 0x00 = 0x24 at I2C4/0x68, and driver strings in the dumped kernel | 16-bit, with selectable ranges and ODRs far above the ~48 Hz the SDK delivers. Look up the exact figures and cite them: the gap between the part's capability and `ACCELEROMETER`'s delivered rate is the single most useful number in this whole document. |
| Magnetometer | **BMM350** (Bosch) | **LIKELY**, string-only; a real device answers at I2C4/0x14 but a `CHIP_ID` read did not match | Explains why `MAGNETIC_FIELD` has no parser and whether it has a producer. |
| Barometer / temperature | **MS5837** (TE) | part **CONFIRMED** from driver strings; **bus and address UNKNOWN** — its fixed address never ACKed on any of the six I²C buses | So `PRESSURE` / `ALTIMETER` / `AMBIENT_TEMPERATURE` producing real data is *evidence about the hardware question*, and producing nothing corroborates the negative. Report the correlation either way. |
| PPG / optical HR | **PAH8316LS** (PixArt) | part **CONFIRMED** from driver strings; address never meaningfully tested | The ceiling on everything HR- and HRV-shaped. Its datasheet is what says whether a higher-rate PPG mode is a firmware choice or a hardware limit. |
| Fuel gauge | **MAX17262** (Maxim ModelGauge m5) | **CONFIRMED**, I2C1/0x36, 16-bit register access | This is what `BATTERY_METRICS` is a view onto, and what row S18's broken percentage is a view onto. |
| GNSS | **Airoha AG3335M** | part **CONFIRMED** from driver strings; UART pairing not verified | Multi-constellation capability and update rate against what `GPS_*` delivers. |
| BLE radio | **BlueNRG-2** (ST) | part **CONFIRMED** | The external-strap path. |
| Haptic | **DRV2625** (TI) | **CONFIRMED**, I2C1/0x5A | Not a sensor; relevant if a protocol needs feedback. |
| Display | **LS012B7DD06A** (Sharp/JDI memory LCD) | **CONFIRMED** | Not a sensor. Recorded because the investigation also found **no touch controller**, which is why this app is button-driven. |

**Do not restate any datasheet figure from memory.** Fetch each datasheet, cite section
and page in `expected_source`, and where you cannot obtain one, say so and leave the
row's `expected` absent. A profile that says "BMI270 max ODR: not sourced" is honest; a
profile with a plausible wrong number in that cell is worse than useless, because it
will be believed.

---

## 5. Product specification, in tiers

App identity: `APP_TYPE "Utility"`, `APP_AUTOSTART Off`, `DEV_ID "UNA"`,
`APP_NAME "SensorLab"`, `APP_USER_NAME "Sensor Lab"`. Derive `APP_ID` the way the other
apps here do — the first 8 bytes of
`sha256("https://github.com/tobymurray/watch-apps#sensorlab")`. Standard out-of-tree app
root: `Software/Libs/{Header,Sources}`,
`Software/Apps/SensorLab-CMake/CMakeLists.txt`, `Software/Apps/TouchGFX-GUI/` with
`simulator/gcc/Makefile`, `Resources/icon_{60x60,30x30}.png` — flat two colours, no
gradients, because the framebuffer is 8 bpp ABGR2222 and a subtle design will band.

### Tier 0 — The evidence core, on the host

No sensors, no SDK. Pure C++17, fully host-tested, and the thing every later tier
depends on:

- The probe catalogue as data: layers, methods, applicability rules, minimum n.
- The claim row (§1.2), the verdict rules, and the completeness arithmetic.
- The streaming statistics: dt histogram, running min/max/mean, quantisation-step
  estimator, stuck-run detector, non-finite counter, monotonicity checker. Fixed-size,
  no allocation, no division per sample. **Test each against synthetic input with a
  known answer** — a stream at exactly 20 ms, one with an injected 4 s gap, one with a
  timestamp that goes backwards, one quantised to a known LSB, one that never changes,
  one containing a NaN. These tests are the reason to believe any number the app ever
  prints.
- The report writer and the profile schema (§6).

Ship this with green tests before touching a sensor.

### Tier 1 — Existence, structure, liveness  (layers 1–3)

The whole 37-type sweep, on hardware, in one run of a few minutes, plus the screen that
shows it. This tier alone produces the first publishable artifact: an existence and
structure table for every sensor type on this firmware. It is also the tier that
supersedes the Sleep Probe's log.

Keep the Sleep Probe's screen idea and improve on it: a scrolling roster of all 37
types, each row showing type name, existence, streaming/event classification, delivered
rate, field-count conformance, and a completeness glyph. Case-encoding or an explicit
glyph — but **a resolved-driver-versus-emitting-driver distinction must be visible at a
glance**, because that distinction caught two of the ledger's most consequential rows in
two minutes of hardware time.

### Tier 2 — Timing and value domain  (layers 4–5)

Longer unattended runs, per sensor group, with the statistics from Tier 0 running over
them. This is where the run-length question becomes real: state the minimum run length
per probe, and have the app refuse to promote a claim out of UNVERIFIED until it has
that much data. Displaying "needs 40 more minutes" is a feature.

### Tier 3 — Sweeps and consistency  (layers 6 and 8)

Automated, unattended, and the part with the most findings per hour. Layer 8 in
particular needs no user at all beyond the midnight-crossing run. Do layer 8 before
layer 7: it is cheaper, it is fully repeatable, and half of what it finds changes what
layer 7's protocols should measure.

### Tier 4 — Guided protocols  (layer 7)

The interactive tiers, one screen per protocol, each with: a plain-language
instruction, a stated tolerance, a countdown, a live readout, and a verdict with an
error figure. Requirements that make these trustworthy rather than theatrical:

- The user selects the protocol; the app never assumes what the user is doing.
- Every step can be aborted, and an aborted step records nothing rather than something
  partial.
- Raw samples are written for every protocol run.
- A protocol that depends on an external reference (strap, weather station, surveyed
  point) prompts for that reference value **and records what was entered**, because a
  comparison whose reference is not in the file cannot be checked later.
- Re-running a protocol appends a run; it never overwrites. Spread across runs of the
  same protocol is real information about the sensor.

### Tier 5 — Optional, and read-only: registers

Apps on this device run with no isolation at all — MPU disabled, CPU privileged,
TrustZone off, settled in the hardware investigation's sweep #3 — and `FwDump` in this
repository already demonstrates direct hardware access from an app, with
`service-cpp-instrumentation-sweep7.cpp` on `una-sdk@research` as the I²C read
primitive. So the BMI270's configuration registers can, in principle, be read
directly, which would turn the *configured* range and ODR from a layer-5 inference into
a CONFIRMED fact, and would settle several rows at once.

If you build this: **reads only. No writes, ever — not to a sensor register, not to an
option byte, not anywhere.** A write could reconfigure a sensor under the kernel's
driver, and the failure would look like a sensor fault rather than like this app. Gate
it behind an explicit setting that is off by default, document the exact registers read
and why, and keep it out of every default run. If that discipline cannot be guaranteed,
do not build this tier — the inference from layer 5 is good enough to publish with a
LIKELY tag, and a LIKELY row is much cheaper than a bricked watch.

---

## 6. The output, which is the actual deliverable

The app's product is not a screen. It is a set of files that survive the device, and a
document somebody outside this repository can read.

**On the watch**, in the app's folder:

- `profile.json` — the current profile: a manifest plus the claim rows of §1.2, written
  with `SDK::JSON::JsonStreamWriter`. One file per firmware version, named with it.
- `runs/<run_id>.csv` — the raw log per run, with a documented header, appended with the
  `seek(size())` discipline from row P6.
- `runs/<run_id>.json` — that run's manifest: firmware version, kernel interface
  version, SDK tag and commit, app version, catalogue version, device identity if any
  is reachable, requested parameters, which types were subscribed simultaneously,
  start and end in both clocks, and how the run ended (completed / aborted / truncated
  by USB / truncated by reboot).
- `state.json` — enough to resume a long run across a restart, following `FwDump`.

**The manifest is not optional and not boilerplate.** It is the primary key of the
whole exercise. A profile whose firmware version is unknown cannot be diffed, and a
profile that cannot be diffed answers none of the four questions in the opening.

**On the host**, in `SensorLab/Tools/`:

- `pull_profile.py` — retrieval. Prefer BLE over USB: `Docs/BLE-File-Transfer-Service.md`
  publishes the protocol in 1.4, and `prototype/una_ble_client.py` on
  `una-sdk@research` is a validated phone-free client that already pulls files off this
  watch with matching CRC. Plugging in kills every running app, which for this app means
  killing the run you are collecting.
- `profile_report.py` — renders `profile.json` into a Markdown report: per-sensor
  sections, the claim table, the conformance findings, the completeness fractions, and
  a "what is still UNVERIFIED and what would settle it" section which is the reader's
  to-do list.
- `profile_diff.py` — **the firmware-drift tool.** Two profiles in, one change list out,
  keyed on `claim_id`: appeared, disappeared, verdict changed, value moved by more than
  its own spread, conformance changed. This is what makes "profiling changes over time
  with firmware updates" a command rather than a project.

**In the repository**, committed:

- `SensorLab/Profiles/<firmware>-<date>/` — the profile, its report and its run
  manifests, so `git log` on that directory is the platform's measured history and
  `git diff` between two of them is free.
- `SensorLab/SENSOR-PROFILE.md` — the current human-readable profile, regenerated, and
  **written to be read by someone who does not use this repository**: an UNA engineer, or
  the author of the next app. State the device, the firmware, the date, the method, the
  completeness, and the limitations. No in-jokes, no unexplained references, nothing
  that requires reading five other documents first.
- `SensorLab/Docs/LEDGER.md` — the CONFIRMED / LIKELY / UNVERIFIED / REFUTED ledger in
  the house convention, seeded from `SleepLab/Docs/FEASIBILITY-LEDGER.md` §2 with
  attribution to it, since those rows were paid for and should not be re-earned.
- `SensorLab/Docs/FINDINGS.md` — anything that looks like a platform defect, each with a
  minimal reproduction: the doc/header divergences, the `GpsLocation` out-of-bounds read,
  the `Connection` handle truncation, and whatever the measurements add. Write these so
  each could be pasted into an issue by the repository owner. **Do not post them
  anywhere yourself** (§8).

---

## 7. Architecture requirements

- Service owns sensors, probes, statistics, files and settings. GUI is a thin MVP over
  custom messages in `Commands.hpp`, each packed and size-asserted, sent with
  `SDK::send_msg`. The per-app `Sender` pattern is retired upstream — do not
  reintroduce it.
- Everything in Tier 0, and every statistic and verdict rule, is reachable from host
  tests with no simulator and no hardware, using `KernelTestDoubles.hpp` and
  `FakeFileSystem.hpp`. Note the stock filesystem fake's `dir()` does not enumerate — a
  real enumerating fake exists only on `una-sdk`'s `poc/athensrun` branch — so either
  keep enumeration off the tested path or bring your own double, and say which in the
  tests' README.
- Follow SleepLab's `NightHarness.hpp` and build a `RunHarness` that scripts the kernel
  message queue and drives whole runs through the real `Service` in milliseconds. **This
  is the highest-leverage test infrastructure in the project.** SleepLab's harness found
  shipped bugs on the day it was written, in code that had passed every unit test —
  ledger row T2 is two of them, a glance that was never sent anything at all and a home
  widget that was never claimed — and this app's service loop is more complex than
  SleepLab's.
- Idle behaviour: compute the wait to the next due work and pass it to `getMessage`, the
  way `Timer` and `Alarm` do. When doing bulk I/O, do a *budget* of work per wake rather
  than one chunk (MapManager's lesson).
- Long runs must survive an app restart: append, flush and bound the cadence, keep a
  small state file rewritten after each flush, and on start detect an in-progress run
  and either resume it or close it as truncated — explicitly, never silently.
- Settings from a JSON file in the app folder plus the watch UI, following `Barcode`.
  Every failure — absent, oversized, unparseable, wrong schema major, out of range —
  falls back to a documented default and logs that it did. Ship a
  `settings.example.json`.
- Tests in `SensorLab/Tests/` with their own `CMakeLists.txt`, following
  `MapManager/Tests` and `SleepLab/Tests`.

---

## 8. Repository conventions and delivery

- Work the tiers in order. After each: host tests green
  (`cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)`),
  a simulator run, a hardware run, and the ledger updated with what that run measured.
- All three builds before a branch is believed (row P14): ARM `.uapp`, host tests,
  TouchGFX simulator.
- Build the way the repo does and check it with Kira:
  `kira build-app --app SensorLab --sdk /path/to/una-sdk --version 0.1.0 --out SensorLab.uapp`.
  Deploy by copying the `.uapp` into `Apps/SensorLab/` on the USB-MSC volume; the kernel
  rebuilds `app_list.json` itself.
- Write `SensorLab/README.md` in this repository's house voice: what it is, why it
  exists, what it does not do, the current completeness, how to run each protocol, the
  file formats as a normative spec, buttons, building, the simulator's irrelevance to
  sensor claims, tests, known rough edges, and provenance. Add its row to the root
  `README.md` table.
- Conventional-commit titles scoped like the rest of the repo (`feat(sensorlab): …`),
  one concern per commit, commits authored as `toby.murray@protonmail.com`, and **no
  mention of Claude or AI assistance** in commits, PR bodies, code comments or docs.
- SDK-side gaps — a documented sensor contract, the doc/header divergences, an
  enumerating filesystem fake — are separate single-concern branches against `una-sdk`.
  The app itself stays entirely inside `SensorLab/` with zero SDK modifications, so it
  rebases trivially.
- **Never post comments on `UNAWatch/una-sdk` PRs or issues.** Read and push branches
  only; upstream communication is the repository owner's.

---

## 9. Non-negotiables

- No number without a method, a run and an n. A row that cannot say how it was measured
  does not get written.
- No claim promoted out of UNVERIFIED by reading code or documentation. Reading settles
  what the *spec* says; only measurement settles what the device does. (Reading can
  REFUTE — three ledger rows were refuted that way — but it can never CONFIRM.)
- A rate is never reported without its spread and its longest gap.
- Never infer elapsed time from a sample count, or a duration from two wall-clock
  readings.
- Absent, silent and stuck are three different findings, and the report distinguishes
  them. `SPO2` resolving no driver, `HEART_BEAT` resolving no driver, and
  `BATTERY_LEVEL` reporting a value that never changes are three different bugs with
  three different consequences.
- The simulator never sources a sensor claim.
- Completeness is always displayed alongside results. No screen and no document shows
  findings without showing how much is missing.
- Every threshold and tolerance is a named constant with a comment stating what
  justified it, or a TODO naming the run that would.
- Raw samples are kept for every protocol run. An analysis without its inputs cannot be
  corrected.
- Tier 5 reads registers and never writes them.
- The app does not autostart and does not run unless asked, so that it never becomes
  the reason another app's measurements are wrong.
