# SensorLab

A `Utility` app for the UNA Watch that measures every property of every sensor an
app can reach, and turns those measurements into a document somebody else can act
on.

**It is an instrument, not a product.** Nobody wears it to learn something about
themselves. It exists so that four questions have written answers:

1. **What are the limitations?** Which of the 37 sensor types have no producer at
   all, which deliver a frame that does not match the parser shipped to read it,
   which honour the period they are asked for and which ignore it, and what
   resolution the values actually have.
2. **Does the platform conform to its own spec?** The SDK's headers,
   `Docs/SensorsLayer.md` and the 29 parsers each make claims. They disagree with
   each other already. Every disagreement is a finding.
3. **What changed with this firmware?** Two profiles of the same device on two
   firmware versions diff into a change list, mechanically, with no judgement
   required to read it.
4. **How much of this do we actually know?** Every claim carries a confidence,
   every sensor carries a completeness, and the report is honest about the parts
   nobody has measured yet.

Its product is not a screen. It is `profile-<firmware>.json` and the run logs
beside it, and [`SENSOR-PROFILE.md`](SENSOR-PROFILE.md) rendered from them.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

---

## Where this actually is

**Tiers 0 and 1 are built, and SensorLab has been run on the watch.** The profile
sitting on the device carries run 13, on 2026-08-26, on firmware 1.4.0, which
answered the existence layer for all 37 sensor types. What it found is below.

| | Status |
| --- | --- |
| **Tier 0** — the evidence core: catalogue, claim store, verdict rules, completeness, statistics, report writer | built, 68 host tests |
| **Tier 1** — layers 1–3: existence, frame structure, liveness, and the roster screen | built, 58 pipeline tests |
| **Raw capture** — every sample verbatim, plus the full histograms behind every quantile | built, decoder and round-trip test |
| **Tier 2** — layers 4–5: timing and value domain over long runs | built and answered: timing 43 %, value 60 %, off fifteen runs including two multi-hour soaks |
| **Tier 3** — layers 6 and 8: period/latency sweeps, cross-sensor consistency | **not built.** Claims exist, UNVERIFIED, each naming its method |
| **Tier 4** — layer 7: the guided physical protocols | **not built.** Same |
| **Tier 5** — direct register reads | **not built, and may never be.** See below |
| All three builds | green — ARM `.uapp` 235 412 bytes, host tests, TouchGFX simulator |
| First hardware run | **done.** The profile on the watch carries run 13, 2026-08-26, firmware 1.4.0, with existence answered for all 37 types; `runs/` shows earlier runs behind it |

So the honest summary is: the instrument has been pointed at the device, and the
existence layer is answered. The completeness fraction is still computed over
a denominator fixed before any measurement was taken, which is what stops a
Tier 1 profile reading as a finished one, and the great majority of
[`Docs/LEDGER.md`](Docs/LEDGER.md) §2 is still inherited from SleepLab or
UNVERIFIED.

The profile is committed, at
[`Profiles/1.4.0-2026-08-28`](Profiles/1.4.0-2026-08-28), with the rendering that
was actually read alongside it and every run manifest and interval log. `raw/` is
not: 172 MB of verbatim samples stays on the device.

**It is 54 % complete**, 764 of 1395 applicable claims answered, and the layers
are not evenly covered:

| Layer | Applicable | Answered | |
| --- | --- | --- | --- |
| existence | 142 | 142 | 100 % |
| frame | 124 | 113 | 91 % |
| liveness | 190 | 132 | 69 % |
| value | 492 | 297 | 60 % |
| timing | 180 | 79 | 43 % |
| consistency | 34 | 1 | 2 % |
| control | 186 | 0 | 0 % |
| physical | 47 | 0 | 0 % |

763 confirmed, 1 likely, **0 refuted**. A further 620 claims cannot apply to this
device and are excluded from the denominator rather than counted as gaps.

What *has* come out of building it is eleven entries in
[`Docs/FINDINGS.md`](Docs/FINDINGS.md), two of them measured defects in the SDK's
JSON writer, and a datasheet-sourced number that puts the accelerometer's
delivered rate 33× below what the silicon does.

### What run 13 found

**Six of the 37 types have no producer at all.** `RequestDefault` resolved
nothing, `RequestList` reported zero drivers, and `connect()` failed, each
CONFIRMED with its own method:

| Type | | |
| --- | --- | --- |
| `0x30` | `MAGNETIC_FIELD` | **no compass on this firmware** |
| `0x40` | `HEART_BEAT` | as SleepLab row S5 predicted, still nothing on 1.4 |
| `0xD0` | `GESTURE_RECOGNITION` | |
| `0xF0` | `PPG` | so no waveform, and nothing HRV-shaped |
| `0xF1` | `SPO2` | as SleepLab row S4 |
| `0x100` | `ECG` | |

The other **31 resolved a driver and connected**, which is what makes the six a
finding rather than a failed run: a sweep where nothing resolved would say
something about the sweep instead.

`MAGNETIC_FIELD` is the consequential one, because unlike the other five it had
never been asked. Its descriptor claim reads "RequestDefault resolved no driver,
so there is nothing to describe", and its frame claims are INAPPLICABLE for the
same reason. The hardware investigation's open question about the part therefore
stays open and stops mattering: whatever is at I2C4/0x14, no app can reach it.
`FUSION` (0x130) does resolve, and its parser carries six fields rather than
nine, so the "accel+gyro+mag" comment in `SensorTypes.hpp` is stale rather than
aspirational and there is no fused heading either.

This is the result [MagProbe](../MagProbe) was built to establish independently,
and it is why that app will only ever draw `NO COMPASS`. Re-check after any
firmware bump: a producer appearing is exactly what `profile_diff.py` is for.

### The kernel names its own drivers, and that settles a hardware question

`RequestGetDesc` is one of the two messages nothing else in the SDK ever calls
(`Docs/FINDINGS.md` section 5). Asked, it answers with a driver string per type,
and two of those strings are worth more than the rest of the layer:

> `MS5837 pressure` (0x80) and `MS5837 temperature` (0x70)

The hardware investigation had the barometer as **part CONFIRMED from driver
strings, bus and address UNKNOWN**: its fixed I²C address never ACKed on any of
the six buses that were swept. `Docs/EXPECTED.md` calls that the standing puzzle
and predicts that plausible pressure values would be evidence the part is there
on a bus the sweep missed. It resolves, connects, delivers two fields and is
60 % characterised, so **that is now the answer.** The part is present and
reachable; the sweep, not the part, was wrong.

Nothing names the optical part: 0x41 is just `Heart Rate sensor`, so the
PAH8316LS is still string-only, and `PPG` (0xf0) has no producer to ask.

Two descriptors are mislabelled, which matters because a descriptor is the
kernel's own account of what a handle is:

| Type | Descriptor | Should be |
| --- | --- | --- |
| `0x20` `GYROSCOPE` | `BMI270 accelerometer` | a gyroscope |
| `0x131` `FUSION_RAW` | `BMI270 accelerometer raw` | fused raw |

Both are the neighbouring type's string, and both types deliver their own correct
frames, so this is a labelling defect rather than a wiring one. `0x21`
`GYROSCOPE_RAW` gets it right, which is what makes the other two look like a
copy-paste rather than a convention.

---

## What it does not do

- **It does not autostart**, and that is a design decision rather than an
  omission. Two autostart apps that both claim the accelerometer contend
  (SleepLab ledger row S8); SleepLab is the one that has to run every night; and a
  profiler that silently competed with it would corrupt the thing it exists to
  measure. **The app does not run unless asked**, so that it never becomes the
  reason another app's measurements are wrong.
- **It does not throw its inputs away.** Every sample it receives goes to
  `raw/` verbatim before anything interprets it, and every quantile it reports is
  accompanied by the histogram it came from. A statistic in the profile can be
  recomputed and disagreed with.
- **It does not guess.** Every figure it emits carries its method and its sample
  size, or it is not emitted. A claim below its metric's minimum n stays
  UNVERIFIED with the value recorded and the verdict withheld — because the
  verdict is the only thing a reader acts on.
- **It does not promote a claim by reading code.** Reading settles what the *spec*
  says; only measurement settles what the device does. That rule is in the type
  system, not in a review comment: a claim recorded with `Source::SpecRead` can be
  REFUTED and can never be CONFIRMED.
- **It does not take a number from the simulator.** The simulator has four sensor
  sources against the device's 37, and it resolves none of them for a service
  anyway (`Docs/FINDINGS.md` §11). It exercises the screens, the statistics and
  the report writer, and it tells you nothing whatever about a sensor.
- **It does not write to a sensor register.** There is no write path anywhere in
  this codebase, which is a stronger guarantee than a flag.

---

## The screen

Four buttons, two views. The roster is the instrument; the summary is what you
read before starting a twelve-hour run.

```
idle 4/37 46% r2                 <- phase, resolved/total, completeness, run id
*ACCELEROMETER 2769.2s g0        <- delivering, streaming, 2769.2/min, 0 s gap
oTOUCH_DETECT   40 12%           <- resolved, silent; type 0x40, 12% complete
-SPO2           f1 8%            <- asked for, no producer
!GPS_LOCATION  60.0s g1          <- delivering a frame its parser rejects
.MAGNETIC_FIELD 30 0%            <- not asked for this run
?ECG          100 0%             <- no burst has arrived for this row yet
```

| Marker | Means |
| --- | --- |
| `.` | the run did not ask for this type |
| `-` | asked for, and `RequestDefault` resolved nothing — **no producer** |
| `o` | resolved a driver, and nothing has arrived — **silent** |
| `*` | resolved and delivering |
| `!` | delivering a frame whose width does not match its parser |
| `?` | no roster burst has arrived for this row yet |

**Absent, silent and stuck are three different findings with three different
causes**, and the markers keep them apart. That distinction, encoded as letter
case on the Sleep Probe's thirteen-character block, caught two of SleepLab's most
consequential ledger rows in two minutes of hardware time. `?` is not padding: a
row that has not arrived is drawn as unknown rather than as a zeroed measurement,
because zeroed memory reads as "resolves nothing, delivers nothing" — which is a
finding, and inventing one would be the worst thing this screen could do.

The cadence letter after the rate is measured, not assumed: `s` streaming,
`e` event, `-` not yet classifiable. `TOUCH_DETECT` was assumed streaming by
SleepLab, delivered zero samples in a minute, and read as "not worn" — which would
have suppressed every night it ever recorded.

**Buttons.** `L1` scroll up (and, at the top, to the summary). `L2` scroll down.
`R1` start a soak, or stop the one that is running — the same button both ways,
because an instrument with a separate stop has a way to leave a run open by
accident. `R2` leave the screen; **an open soak keeps recording.**

---

## How to run it

### The existence sweep — minutes, and the first publishable artifact

1. Build: `UNA_SDK=... Tools/docker-build.sh app`
2. Copy `Software/Apps/SensorLab-CMake/build/Sensor_Lab_*.uapp` into
   `Apps/SensorLab/` on the USB-MSC volume. The kernel rebuilds `app_list.json`
   itself.
3. **Unplug.** Plugging in terminates every running app, so a profiling run
   cannot be watched over the cable.
4. Open the app. Layer 1 runs unprompted — 37 × (`RequestDefault`,
   `RequestList`, `RequestGetDesc`, `RequestConnect`) — and the roster fills in.

That alone produces an existence and structure table for every sensor type on this
firmware, which is the first thing here worth publishing.

It was also supposed to read the firmware version out of the kernel and promote
SleepLab's ledger row P1 from LIKELY to CONFIRMED with a quoted string. **It did
not.** Run 15's manifest records `firmware_read: "rejected"` after
`firmware_read_attempts: 4`, with `firmware_read_from_kernel: false`, so the
`1.4.0` in `profile-1.4.0.json` came from the configured fallback and not from
the device. P1 stays LIKELY, and the kernel's answer to
`RequestSystemInfo` is now a finding of its own rather than a step in this
procedure. Note what that means for a firmware comparison: two profiles could
carry the same name while running different firmware, because the name is not
currently evidence of anything.

### A soak — layers 2 to 5

Press `R1`. Twelve hours is the default cap; `settings.json` changes it. The soak
subscribes every type that resolved, counts what arrives, and writes one interval
row per sensor per minute plus one per field.

**Do not plug in.** If you do, the run is marked `truncated_by_usb` rather than
completed, and the report says its distributions are shorter than they look.

### Collecting it

```sh
python3 Tools/pull_profile.py <BLE-address> --out Profiles/1.4.0-2026-08-21
python3 Tools/profile_report.py Profiles/1.4.0-2026-08-21/profile-1.4.0.json \
    -o SENSOR-PROFILE.md
```

Over BLE, not USB, for the reason above. `--profiles-only` skips the raw logs,
which are the bulk.

### Comparing two firmware versions

```sh
python3 Tools/profile_diff.py \
    Profiles/1.4.0-2026-08-21/profile-1.4.0.json \
    Profiles/1.5.0-2026-11-02/profile-1.5.0.json
```

Keyed on `claim_id` and nothing else. Five kinds of change: conformance changed,
verdict changed, value moved beyond its own spread, appeared, disappeared. Exits 1
when it finds something, so it works in a gate.

---

## The file formats, as a normative spec

Each of these is specified in the header that writes it, and each has a host test
that runs the real writer against the real reader.

| File | Spec | What it is |
| --- | --- | --- |
| `profile-<firmware>.json` | `Software/Libs/Header/Profile/ProfileWriter.hpp` | The conclusions. Manifest plus **every** claim in the catalogue, answered or not. |
| `runs/<run_id>.csv` | `Software/Libs/Header/Profile/RunLog.hpp` | The per-interval evidence: delivery, timing, per-field domain, **and the full histograms** behind every quantile. |
| `raw/<run_id>-<seq>.bin` | `Software/Libs/Header/Profile/RawLog.hpp` | **Every sample, as the wire carried it.** Binary, chunked, verbatim. |
| `runs/<run_id>.json` | `Software/Libs/Header/Profile/Manifest.hpp` | That run's manifest. Written twice — when the run opens and when it closes. |
| `state.json` | `Software/Libs/Header/Profile/RunLog.hpp` | Enough to close a run explicitly across a restart. |
| `settings.json` | `Software/Libs/Header/Settings.hpp`, and `settings.example.json` | What the operator asked for. |

### Three levels, and only the bottom one is not a summary

`profile.json` holds conclusions. `runs/<id>.csv` holds per-interval statistics.
Both embed the question somebody thought to ask, and on a first profile of an
undocumented platform that question will be wrong. **An analysis without its
inputs cannot be corrected**, so `raw/` holds the samples.

Verbatim means verbatim: a record is the `EventData` header plus the frame bytes
exactly as they arrived. Nothing is interpreted on the way in — not the field
count, not the timestamps, not the field types. A frame whose stride is not a
whole number of fields is *rejected for parsing and still recorded*, because it
is the single most interesting frame this app will ever meet.

Things you can get from `raw/` and from nowhere else: the *shape* of a delivery
gap (did it stop, thin, or arrive in one burst? a `longest_gap_ms` of 4020 says
none of those); whether two sensors' samples are simultaneous, which is layer
8's whole subject; a field's whole trajectory rather than its min, max and mean;
what a frame from one of the five parser-less types actually contains; and
whether the profile's statistics are *right*, by recomputing them.

Middle level, same principle: an `S` row reports dt as five quantiles **chosen
before anyone knew what the distribution looked like**, so every `S` row is
accompanied by `B` rows carrying the histogram itself, sparsely. The round-trip
fixture's accelerometer is bimodal — 1224 samples at 21 ms and 152 at 27 ms — and
a single p50 describes neither mode.

```sh
python3 Tools/raw_decode.py Profiles/1.4.0-2026-08-21/raw     --verify Profiles/1.4.0-2026-08-21/runs/2.json      # cross-check the counts
python3 Tools/raw_decode.py .../raw --type 0x10 --kinds --csv accel.csv
```

`--csv` emits every field three ways — `fN_f`, `fN_u`, `fN_i` — because the frame
does not say which member of the union it is, and choosing for you would be
interpreting. `--kinds` labels what the SDK's own parser says each field is
*meant* to be.

### What it costs, and what it does to the measurement

A 3-field sample is 24 bytes on the wire. At the accelerometer's measured ~48 Hz
that is **4.15 MB an hour for one sensor**; a dozen subscribed types is roughly
6–10 MB an hour, so a twelve-hour soak is **70–120 MB**. Capped at 256 MB by
default, chunked at 512 KB, and **the manifest records how many batches were
dropped when the cap was reached** — a capture that stopped silently would leave a
file that still decoded cleanly and was missing eleven hours.

And it is not free of the thing it measures: ~1 MB/s to flash costs power and
CPU, so a dt distribution taken with capture on is not the same measurement as
one taken with it off. The manifest records which. `"raw_capture": "off"` is a
legitimate experiment, not a degraded mode.

Three more things in those specs are worth knowing before reading a file:

**Every claim is written, including the unanswered ones.** A profile that carried
only the rows it had answers for would be a third of the size and would read as
finished. Writing the UNVERIFIED rows — each naming the probe that would settle it
— turns the file into its own to-do list.

**Every number that could be fractional, negative or wider than 32 bits is a
decimal *string*.** Not a stylistic choice: the watch's newlib may not link
floating-point `printf`, and two of the SDK's integer JSON paths are broken on
64-bit builds — `add(int32_t)` wrote -5 as `4294967291`, and `add(int64_t)` wrote
a UNIX timestamp as `1.75555e+09`. Both measured; see `Docs/FINDINGS.md` §9 and
§10. `float()` in python reads a string either way.

**A missing measurement is `-1` in the CSV, never `0`.** Zero samples delivered in
an interval is a finding; a sensor that was never subscribed is not.

---

## Where the numbers come from

```
Docs/LEDGER.md      the platform and this app: build shape, storage, clocks, IPC.
                    ~40 rows, maintained by hand, CONFIRMED/LIKELY/UNVERIFIED/
                    REFUTED with the method that earned each tag.

Docs/FINDINGS.md    eleven things that look like platform defects, each with a
                    minimal reproduction, written so any one could be pasted
                    into an issue. Nothing here has been posted anywhere.

Docs/EXPECTED.md    the silicon. What each part can do, cited to a datasheet
                    section and page -- and, for the five parts nobody has
                    fetched a datasheet for, an explicit "not sourced".

Docs/CLAIM-IDS.md   the claim_id scheme, fixed once. Renaming one silently
                    breaks every firmware comparison spanning the rename.

profile-*.json      ~1974 claims about the sensors, generated.
```

### The one number that most justifies this app existing

The BMI270 supports accelerometer output data rates from **0.78 Hz to 1.6 kHz**
(*BMI270 Datasheet*, BST-BMI270-DS000-08 rev 1.6, Key features p. 2, and
`ACC_CONF.acc_odr` §5.2.41 pp. 101–102). SleepLab measured the SDK delivering
**~48 Hz**. That is **~33× of headroom, and it is not in the silicon.**

`Docs/EXPECTED.md` turns that into two testable predictions: `ACC_RANGE`'s reset
value is ±8 g at 4096 LSB/g, so layer 5's recovered quantisation step should be
**244 µg** if the kernel leaves the register alone — a register read done with
arithmetic, which is why Tier 5 is optional rather than necessary.

---

## Building

All three builds, and **all three must be run before a branch is believed**
(SleepLab ledger row P14, confirmed twice — the ARM build globs `Sources/*.cpp`
while the simulator's Makefile enumerates them, and `timegm` exists in glibc but
not in the watch's newlib):

```sh
export UNA_SDK=/path/to/una-sdk           # apps-v1.4.0; main is the same commit
Tools/docker-build.sh app                 # the .uapp
Tools/docker-build.sh tests               # host tests, configure + build + ctest
Tools/docker-build.sh sim                 # the TouchGFX simulator
Tools/docker-build.sh sim-run             # and run it headless
Tools/docker-build.sh catalogue           # regenerate the sensor type table
```

Or with Kira, which additionally checks the result against what the
`CMakeLists.txt` declares:

```sh
kira build-app --app SensorLab --sdk /path/to/una-sdk --version 0.1.0 \
    --out SensorLab.uapp
```

`$UNA_SDK` must point at **`apps-v1.4.0`**. An app carries the kernel interface
version it was built against: 1.4 is 3, and a `.uapp` carrying 3 exits instantly
to an `App PID` error screen on a v2 kernel with no build-time warning.

### The type table is generated, and there is an alarm on it

`Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp` comes from
`SensorTypes.hpp` and the 29 parser headers, via `Tools/gen_catalogue.py`. It is
committed, because the simulator's Makefile has no place to run a generator — and
`sensorlab-catalogue-current` re-runs the parse at test time and fails if the
committed file is stale.

That test fails loudly on a new sensor type, a renamed parser, a changed field
count, or a parser appearing for one of the five types that currently ship none.
Every one of those is news. **This is the mitigation for finding number one**: the
SDK's own documentation is six types behind its headers, and an app that typed its
own table would inherit whichever version its author read.

---

## Tests

126 tests, five suites, about four seconds. See
[`Tests/README.md`](Tests/README.md), which is worth reading for what they are
*not* evidence about.

`Tests/RunHarness.hpp` is the highest-leverage thing in the project: it scripts
the kernel message queue and drives whole runs through the real `Service` in about
70 ms each. SleepLab's equivalent found two shipped bugs on the day it was
written, in code that had passed every unit test.

`Tests/Stats_test.cpp` is the reason to believe any number this app prints — the
six streams with known answers, plus a synthetic uptime wrap.

---

## Known rough edges

- **`Docs/LEDGER.md` §2 has not been reconciled against the profile.** The
  profile is committed and 54 % answered, and the ledger still reads as though it
  were not: rows inherited from SleepLab now have a measured counterpart sitting
  next to them and nobody has walked the two lists against each other. That is
  the largest one now, and it is reading rather than measurement.
- **`raw/` is not committed**, so the profile's statistics can be recomputed only
  by someone holding the watch. 172 MB across 347 chunks is the reason, and the
  three levels argument above is the cost of that decision.
- **Two layers are at zero.** `control` (186 claims) and `physical` (47) are
  Tiers 3 and 4, unbuilt. `consistency` has one answer out of 34.
- **The firmware version has never been read from the kernel.** Four attempts,
  all rejected. Every profile is keyed on a string that came from a fallback.
- **Tiers 3, 4 and 5 are not built.** Their claims are in the catalogue so the
  completeness fraction counts them, which is the honest arrangement, but that is
  ~150 UNVERIFIED rows. Layer 8 is the highest value per unit of effort and needs
  no user at all beyond one run across local midnight; do it before layer 7,
  because half of what it finds changes what layer 7's protocols should measure.
- **Five datasheets unsourced.** BMM350, MS5837, PAH8316LS, MAX17262, AG3335M.
  `Docs/EXPECTED.md` says what each would settle. Two of those entries have moved:
  the BMM350's `CHIP_ID` no longer decides anything, because the type resolves no
  driver whatever the part turns out to be, and the MS5837's I²C address is no
  longer the open question either. What is left for both is resolution and range,
  which is layer 5's business rather than the inventory's.
- **The 10 KB service stack is an argument, not a measurement.** Nothing recurses
  and the sample path allocates nothing. That is a good argument and it is not a
  high-water mark.
- **`Tools/pull_profile.py` has still never been run against a watch.** Same as
  SleepLab's `pull_nights.py` and the same ledger row: the underlying BLE client
  is validated for `.fit` files under `Apps/GpsLab/` with matching CRC-16, and
  nothing in the protocol is path-specific, but neither wrapper has met a device.
  Run 13's profile was read straight off the USB-MSC volume, which exercises none
  of that path, and which is also why it is quoted here rather than committed.
- **The roster's pixels are untested.** Its *content* is asserted through the
  harness; what the view does with a `TextAreaWithOneWildcard` has only ever been
  looked at in the simulator.
- **`kLsbMinimumN` is a guess**, and `Docs/LEDGER.md` §3 lists every other
  threshold that is reasoned rather than measured, with the run that would settle
  each.

---

## Provenance

SensorLab's direct ancestor is [SleepLab](../SleepLab)'s Tier 0 probe — a
one-line-per-minute delivery counter over thirteen sensor types. Its post-mortem
argued that its unique value was *a screen you read before bed rather than a log
you read after*. This app supersedes the probe's log and keeps its screen, for all
37 types.

Everything in `Docs/LEDGER.md` §2 is
[SleepLab's ledger](../SleepLab/Docs/FEASIBILITY-LEDGER.md) §2, reproduced with
attribution because those rows were paid for with hardware nights and should not
be re-earned.

The verification convention — CONFIRMED / LIKELY / UNVERIFIED / REFUTED with the
corroborating method — and the hardware inventory both come from the
[hardware-config-recovery investigation](https://github.com/tobymurray/una-sdk/tree/research/Docs/Investigations/2026-07-29-hardware-config-recovery)
on `una-sdk@research`. What the optical path can and cannot produce comes from a
UNA maintainer's answer recorded in
[2026-06-15-heart-beat-vs-ppg](https://github.com/tobymurray/una-sdk/tree/research/Docs/Investigations/2026-06-15-heart-beat-vs-ppg)
(PR #167) — including that `HEART_BEAT` emits nothing because HR detection is
frequency-domain rather than per-beat, which has an expiry date and is therefore a
probe here rather than an assumption.

`FwDump` is the precedent for reading the firmware version out of the kernel, and
for resumable long-running background work. `MapManager` is the only written
account of what a long-lived service does on this device. `Barcode` is the
precedent for the settings file.

`Docs/IMPLEMENTATION-PROMPT.md` is the brief this was built from.
