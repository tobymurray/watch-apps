# Rollout

How to get from "it builds" to "the numbers mean something", in order, with
what each step actually settles.

The ordering is not arbitrary and it is not padding. Each phase produces the
input the next one needs, and the app is deliberately *unable* to tell you it
is wrong — a sleep app's failures are silent, the numbers always look
plausible, and nobody has ground truth in their bedroom.

**Do not install SleepLab and Sleep Probe at the same time.** Both start
themselves at boot and both subscribe the accelerometer and the heart-rate
sensor. What the sensor layer does when two apps want the same sensor is ledger
row S8 and it is unverified, so running both is the one way to get a result
neither app's log will explain.

---

## Phase 0 — before anything · 5 minutes

**Confirm the firmware.** A `.uapp` built against `apps-v1.4.0` carries kernel
interface version 3, and a v3 app on a v2 kernel exits instantly to an
`App PID` error screen with nothing catching it at build time. Read the BLE DIS
Firmware Revision String (UUID 0x2A26) or the watch's own Settings screen.

Promotes ledger row **P1** from LIKELY to CONFIRMED.

**Start the paper diary tonight.** Two times a night, to the nearest five
minutes, in a notebook or a phone note:

```
date,lights_out,woke
2026-08-19,22:45,06:10
```

`date` is the evening you went to bed. It costs nothing and it is the long
pole: ten nights of it is the only thing that can turn any metric in the ledger
from *synthetic-only* into *diary-validated*, and it can be collected during
every phase below. Start it now and it is done when you need it.

Do not write it finer than five minutes. You do not know when you fell asleep.
What you do know is when the light went out and when you got up, and those are
the two numbers anything compares against.

---

## Phase 1 — the probe · 2–3 nights

Answers whether an all-night app is possible on this device at all. If sensor
delivery stops at 02:00, everything after this is wasted, and that is precisely
why it comes first.

```sh
Tools/docker-build.sh probe
# copy Sleep_Probe_0.1.0.uapp into Apps/SleepProbe/ on the USB volume
```

Eject, unplug, power-cycle. It autostarts; there is nothing to launch.

| Night | `probe.json` | Wearing | Settles |
| --- | --- | --- | --- |
| 1 | none (defaults) | worn, unplugged | S1, S3, S4, S5, S7, S9, S10 — and the expensive half of S2 |
| 2 | `"hr": "off"` | worn, unplugged | the cheap half of S2. The difference is what heart rate costs |
| 3 | defaults | **on a nightstand** | the false-worn half of S7 |

Before bed, open the probe's screen once and read the sensor block — upper case
is resolved, lower case is not. Thirty seconds, and it is the difference
between a night recorded and a night wasted.

**Charge before bed, not during.** Plugging in stops every app on the watch.

Each morning:

```sh
python3 Tools/probe_report.py /path/to/Apps/SleepProbe/probe_log.csv
```

The log is appended across launches, so all three nights can live in one file;
the report splits it and reports the longest run by default.

**Then fill in ledger §2.** Every row there is UNVERIFIED today and each names
the column that settles it. This is the step people skip, and skipping it means
the numbers exist and nobody can find them in six months.

**Stop here if the answer is bad.** If delivery dies at 02:00, or continuous
heart rate costs most of the battery, or worn-detection flickers thirty times
an hour — that is a finding, it is what the probe is for, and the design above
it has to change before it is built on.

When you are done, **delete `Apps/SleepProbe/`**. It is autostart with no stop
button, and it must not be running when SleepLab is.

---

## Phase 2 — the delivery constants · 10 minutes at a keyboard

Three constants are guesses about *delivery* and the probe just measured them.
Set them from the report's `delivered rates` section:

| Constant | In | From |
| --- | --- | --- |
| `kAccelLatencyMs` | `Service.cpp` | the `loop` section's wakes/row, against the battery cost |
| `kMinSamplesPerRecordingEpoch` | `Service.cpp` | a fifth of the measured delivered rate, not the requested one |
| `SleepWakeScorer::kMinSamplesPerEpoch` | `SleepWakeScorer.hpp` | likewise, for the 60 s scoring epoch |

If continuous heart rate turned out to cost more than roughly a third of the
battery, this is where `"hr": "duty"` becomes the documented default rather
than an option — the app already supports it and the probe already measured the
alternative.

Every one of these carries a TODO naming exactly this step. Delete the TODO
when you set the value, and **put the measurement in the ledger**, or the next
person inherits another unexplained number.

---

## Phase 3 — SleepLab, unvalidated · 3–4 nights

```sh
Tools/docker-build.sh app
# copy Sleep_Lab_0.1.0.uapp into Apps/SleepLab/ on the USB volume
```

Optionally write `settings.json` (see [`settings.example.json`](../settings.example.json))
or use Kira's install page, which writes it for you.

Record **three worn nights and one on a nightstand**, still keeping the diary.

The app will print sleep figures during this phase and **they are not yet
trustworthy** — the constant relating this watch's movement counts to
Cole‑Kripke's units is still an estimate. What the nights are for is the
`count` column in the epoch CSVs, which the app records honestly regardless of
what it reports on screen. A night that fails the worn gate still writes every
count.

Pull them off — over BLE, so the recorder keeps running:

```sh
python3 Tools/pull_nights.py <watch-ble-address> --out ./nights/worn
```

---

## Phase 4 — the movement constants · 20 minutes

```sh
python3 Tools/night_report.py thresholds \
    --worn ./nights/worn --table ./nights/table
```

It prints the count distribution for each and suggests a value for each
movement threshold, with the reasoning:

| Constant | In | Placed at |
| --- | --- | --- |
| `WornGate::kMicroMovementFloor` | `WornGate.hpp` | between the table night's 95th percentile and the worn nights' 5th |
| `NightAnalyser::kMovementFloor` | `NightSummary.hpp` | worn p75 |
| `SegmenterConfig::stillnessCountMax` | `NightSegmenter.hpp` | worn p50 |
| `SegmenterConfig::activityCountMin` | `NightSegmenter.hpp` | worn p99 — and see below |

Two things it will tell you that are worth acting on rather than working around:

- **"NO CLEAN VALUE — the distributions overlap."** Then no floor separates a
  wrist from a table on the evidence you have, and the honest options are more
  nights or accepting that the plausibility check rests on heart rate alone.
  Writing a number in anyway is how the nightstand gets through.
- **"These two are close."** `activityCountMin` and `stillnessCountMax` bracket
  the segmenter's hysteresis, and close together means a night that opens and
  closes on adjacent epochs. A night spent asleep does not contain the evidence
  for "active enough to be up" — record a few minutes of getting up and walking
  about, and take the value from that.

The suggestions are suggestions. Put each value in the code **and** the
reasoning in the ledger.

---

## Phase 5 — the calibration · 10 nights

The one that turns *synthetic-only* into *diary-validated*, and the only one
that cannot be shortened.

Keep recording, keep the diary. Then:

```sh
python3 Tools/night_report.py diary ./nights --diary diary.csv
```

It reports mean signed error and spread on sleep onset and final wake. To
calibrate, sweep `SleepWakeScorer::kCountScale`, rebuild, re-run the report
against the same nights, and take the value that minimises the error.

Nights that failed the worn gate are excluded and said so — they have nothing
to compare, and folding them in as zero error would flatter the result.

Then, and only then:

- put the mean signed error and spread in **README's accuracy table**;
- change those metrics in the ledger's validation table from *synthetic-only*
  to *diary-validated*;
- keep saying what it is measured against. A diary knows lights-out and final
  wake. It does not know when you fell asleep, so onset error is error against
  a proxy — and no amount of nights makes it polysomnography.

---

## Phase 6 — the adversarial nights · 6 nights, any order

Each becomes a regression fixture. The synthetic tests already cover these
*shapes*; what a real night adds is whether the thresholds put real data on the
right side of them.

| Night | What it must produce |
| --- | --- |
| Worn, deliberately lying awake and still for 30 minutes | that stretch scored as sleep — the known bias, measured rather than asserted |
| Not worn, on a table | `NOT WORN`, no sleep numbers, absent from the baseline |
| Worn while charging | `INTERRUPTED - was charging` as the first line |
| A device reboot mid-night | one night, resumed and flagged — not two half nights |
| A timezone or clock change mid-night | `INTERRUPTED - clock changed`, and the duration still right |
| A deliberate early get-up | the night closes on steps, not on the window ending |

The first is the important one. Actigraphy mistakes lying still for sleeping,
and this is the night that measures *your* version of that error rather than
quoting a range from the literature.

---

## Then it is a 0.2.0

At that point the constants come from measurements, the accuracy statement
comes from your own nights, and the ledger's validation column says something
other than *synthetic-only*. Bump the version, add a `[[versions]]` row to the
Kira manifest saying what changed, and the notes can finally drop
"not yet run on hardware".

Everything before that point is honest software with unvalidated numbers, which
is exactly what the README and the card both say it is.
