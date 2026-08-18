# Sleep Lab — an actigraphy recorder that says what it does not know

A background `Utility` app that records a night of wrist data and turns it into
a defensible sleep report. It runs from boot, opens a session when you settle
inside your bedtime window, writes one row per 30 seconds of the night, and in
the morning scores it with a published actigraphy algorithm.

It also refuses to tell you things it cannot know, which is most of what a
consumer sleep tracker tells you.

> Unofficial. Not affiliated with, endorsed or sponsored by UNA Watch Ltd.

## What it does not do, and why

This is the first section on purpose. A sleep app's failures are silent — the
numbers always look plausible, and nobody has ground truth in their bedroom.

**No sleep stages.** No light, no deep, no REM, no minutes-in-stage, ever.
Staging rests on heart-rate variability plus multi-channel PPG and this device
has neither: `HEART_BEAT` (0x40) **emits no events at all** — UNA's own answer,
because heart-rate detection here is a frequency-domain algorithm rather than
per-beat detection — so there are no RR intervals to build HRV from. The PPG
waveform is single-channel at 20 Hz. There *is* a four-level band drawn across
the night, and it is labelled, captioned on screen, and carries its own method
string into every file, because anything four-level drawn across a night looks
exactly like a hypnogram and is not one.

**No SpO2, no skin temperature, no respiratory rate.** The `SPO2` type exists in
the SDK and whether the firmware produces a single sample is unverified — so
nothing is built on it. `AMBIENT_TEMPERATURE` is ambient; it is never labelled
as body temperature. Respiration from a wrist accelerometer at these rates is
not defensible and is not shipped.

**No absolute physiological judgements.** Nothing here says your resting heart
rate is good. Population norms describe distributions; you are one draw from
one. Every heart-rate figure is reported against *your own* recorded nights, and
no delta is shown until there are at least five of them — before that the screen
says how many more are needed rather than showing a small number with a caveat
attached.

**No sleep numbers at all for a night the watch was not on your wrist.** Not
annotated, not caveated: suppressed. See [the nightstand
problem](#the-nightstand-problem).

**No phone.** There is no supported way for a third-party watch app to receive
companion data and there will not be one, so settings come from a JSON file you
write over USB.

## The honest accuracy statement

**This app has never been compared against polysomnography, and it never will
be.** There is no sleep laboratory here. So:

| | |
| --- | --- |
| **What the arithmetic is validated against** | Synthetic nights with answers known by construction — a known onset, awakenings of known length, a known final wake. 126 host tests. This proves the code computes what it says it computes and **nothing about sleep**. |
| **What the sleep correspondence is validated against** | *Nothing yet.* The constant that bridges this device's activity counts to the units Cole-Kripke's coefficients were fitted for is currently a guess (`SleepWakeScorer::kCountScale`). Until ten diary-validated nights exist, every sleep/wake figure here is **synthetic-only**. |
| **The bias, and its direction** | Wrist actigraphy has high sensitivity to sleep (~85–95 %) and poor specificity to wake (often 40–60 %). It reliably notices sleep and **systematically mistakes lying still for sleeping**, so estimated sleep time is biased **high**. |

That last row is why the report shows two numbers side by side:

```
est 7h04  still 6h41
```

`est` is the scorer's estimate of sleep, biased high. `still` is minutes below
the movement floor — what was actually *measured*. Showing only the first is the
overclaim this whole app exists to avoid.

Every claim, and what earned it, is in
[`Docs/FEASIBILITY-LEDGER.md`](Docs/FEASIBILITY-LEDGER.md), tagged CONFIRMED /
LIKELY / UNVERIFIED / REFUTED with the method behind the tag.

**[`Docs/ROLLOUT.md`](Docs/ROLLOUT.md) is the ordered path from here to numbers
that mean something** — which probe nights answer what, which constants each
phase sets, and where the diary comes in.

## Status: two minutes on hardware, no night yet

Everything below builds and the host tests pass. **No night has been recorded
on hardware.** But the [Tier 0 probe](Probe/README.md) has been run for two
minutes on a real watch, and that alone settled four ledger rows and found one
bug that would have broken every night:

| | |
| --- | --- |
| `SPO2` | **No driver at all.** Not "produces nothing" — `connect()` is refused. Nothing is built on it and now nothing can be. |
| `HEART_BEAT` | **No driver at all**, so no RR intervals, so no HRV, so no stages. UNA's 1.3-line answer holds on 1.4. |
| Accelerometer rate | **~48 Hz delivered against 25 Hz requested** — nearly double, and the opposite direction to the thinning the simulator does. Heart rate honoured its period exactly in the same minute. Nothing downstream is wrong, because the count derivation is rate-independent by construction; the cost is roughly double the sample-path power that was budgeted. |
| `TOUCH_DETECT` | **An event sensor**, not a clocked one: zero samples in a minute while perfectly happily subscribed. SleepLab read that as 0 % worn, which would have reported **every night as NOT WORN**. Fixed — see [the nightstand problem](#the-nightstand-problem). |

Still unknown, and what a full night answers:

- Does delivery survive eight hours, or stop at 02:00?
- What does continuous optical heart rate cost overnight?
- Does `TOUCH_DETECT` flicker on a loosely-strapped sleeping wrist? Load-bearing:
  every sleep number is gated on it.
- Does anything contend for the heart-rate sensor?

Run the probe for two nights before trusting this app with anything.
[`Docs/ROLLOUT.md`](Docs/ROLLOUT.md) is the order.

## The nightstand problem

**A watch on a nightstand is perfectly still and reports a flawless night.**
Zero movement, no awakenings, 100 % efficiency, eight hours of the soundest
sleep the app has ever recorded. Every number beautiful, every number about a
piece of furniture.

It is the failure that would discredit everything else, because it is silent, it
is common — people take watches off — and the output looks *better* than a real
night rather than worse.

So every sleep claim is gated on worn detection **plus a plausibility check**,
and the plausibility check tests for the two things a living wrist produces that
a table cannot: micro-movement (a sleeping human is never perfectly still;
respiration alone moves the wrist) and a heart rate. Either alone is defeatable.
Both absent together, across most of a night, is a table — and the gate overrules
`TOUCH_DETECT` when it disagrees, because a capacitive sensor can report "worn"
for a watch face-down on a duvet.

A night that fails is reported as *not worn* with its numbers suppressed, and it
never reaches the personal baseline — one nightstand in the baseline would
poison it for four weeks and make every real night afterwards look bad.

With heart rate switched off, half the check is unavailable. The verdict is then
`Uncertain`, which suppresses the numbers exactly as `NotWorn` does. "Probably
worn" is not a basis for printing a sleep efficiency.

**Worn state is sticky, and that is not a refinement.** `TOUCH_DETECT` turned
out on hardware to be an event sensor — it publishes when the state *changes*,
not on a clock, and it delivered nothing at all across a whole minute while
subscribed and working. Reading a sample-less epoch as 0 % worn put every epoch
below the worn floor, made every epoch unscorable, and would have reported every
night as not worn. The last known state is carried forward now; and a sensor
that says nothing for a whole night yields `Uncertain` with its own wording,
because telling somebody their watch was not worn would send them to put on a
watch they are already wearing.

## Charge before bed, not during

**Plugging in terminates every running app**; autostart relaunches on unplug. A
watch charging overnight records nothing. This is not a footnote — it is the
single most likely way to lose a night.

The app subscribes to `BATTERY_CHARGING` and marks any night that saw the
charger as **interrupted**, prominently, as the first line of the report. It
cannot do better than that: while the app is stopped it cannot observe how long
it was stopped for.

It is also why [`Tools/pull_nights.py`](Tools/pull_nights.py) exists. Pulling
the night's files over BLE leaves the service running; plugging in to fetch them
kills every app on the device.

## How a night works

```
21:00              you enter the bedtime window
   ...             15 minutes of sustained stillness, worn
23:12  OPEN        the session opens, backdated to 22:57
   ...             one 30 s row per epoch, flushed, state file rewritten
06:41  CLOSE       sustained activity, or steps, or the window ends
                   -> score -> gate -> summarise -> index -> baseline
```

Two epoch lengths. Recording epochs are **30 s** and are what reaches the CSV.
Scoring epochs are **60 s** — pairs, summed — because that is the epoch length
Cole-Kripke's coefficients were derived for, and coefficients do not transfer
across epoch lengths. Recording finer and scoring at the validated resolution
means the file keeps the detail without the algorithm being quietly reused at
half its epoch.

A night opens once stillness has been *sustained*, which is by definition some
minutes after it began, so a ring of recent epochs is kept even while idle and
flushed into the night when it opens. Without that backdating every night would
lose its first quarter hour and every onset latency would be wrong by the same
amount.

### Surviving a restart

A USB connection kills the process without warning. So every epoch is appended,
flushed, and its handle closed, and `night_state.txt` is rewritten after each
one carrying **both clocks**. On start, a state file means a night was in
progress and it is resumed into rather than replaced.

The pair of clocks is what makes the stitch honest:

| What the clocks say | What happened |
| --- | --- |
| uptime went backwards | the device rebooted |
| uptime climbed | the app was relaunched inside one boot — usually USB |
| the wall clock moved further than uptime says it should have | the clock was changed under us |

All three mark the night interrupted and the summary says which. `getTimeMs()`
is device uptime: it survives an app restart, resets only on a device reboot,
and wraps at ~49.7 days — so every duration in this app is an unsigned or signed
difference, and **no duration anywhere comes from two wall-clock readings**.

## The algorithm

**Cole-Kripke (1992)** for sleep/wake scoring, with **Webster's (1982)
rescoring rules**, which belong with it rather than being an optional extra —
the published accuracy figures were measured with them.

- Cole RJ *et al.*, "Automatic sleep/wake identification from wrist activity."
  *Sleep* 1992;15(5):461-469.
- Webster JB *et al.*, "An activity-based sleep monitor system for ambulatory
  use." *Sleep* 1982;5(4):389-399.

The coefficients are transcribed from the literature and gathered in one
constant block; **nobody here has checked them against the primary source**
(ledger row A8).

Activity counts are derived the way the published methods derive them, adapted
for one platform fact: **the delivered sample rate is not the requested one**.
The per-listener gate thins delivery on a boundary at *half* the expected
period, an exact ratio falls on the thinner side, and the thinning is quantised
into bands. So:

- the band-pass filters are re-coefficiented **per sample** from the sensor's
  own timestamps, and
- the integral is **dt-weighted**, so a count is an area in g·s and not a
  function of how many samples happened to arrive.

Without the second one, halving the delivered rate would halve every count in
the night and the scorer would faithfully read it as a quieter night. A host
test pins the count as rate-independent across a 4× span.

Counts are computed **per axis** and combined as the vector magnitude of the
three integrals — Actigraph's own "vector magnitude counts". Filtering `|a|`
instead was tried and measured wrong in two ways: it is blind to rotation (a
roll-over changes which axis carries gravity without changing `|a|` at all) and
quadratically insensitive to movement across gravity, at about one fifteenth the
response of the same movement along it.

## Files

Everything lands in `Apps/SleepLab/` on the USB-MSC volume.

| Path | What it is |
| --- | --- |
| `Nights/<start>.csv` | One row per 30 s recording epoch. The record. |
| `Nights/<start>.json` | The summary, written when the night closes. |
| `Nights/index.csv` | One row per completed night. The history and the baseline. |
| `Raw/raw_<start>.csv` | Raw accelerometer, only if you asked for it. |
| `night_state.txt` | Present only while a night is in progress. |
| `settings.json` | Yours to write. |

`<start>` is `YYYYMMDDTHHMMSS` **local**, from the session's start — so a night
is named for the evening it began, not the morning it ended.

The normative spec is the file comment on
[`Software/Libs/Header/NightStore.hpp`](Software/Libs/Header/NightStore.hpp).
Three rules that run through all of it:

- **`-1` means not measured. It never means zero.** Zero delivered heart-rate
  samples is a finding; a sensor that was never subscribed is not, and an
  average that folds the second into the first is wrong.
- **Every schema is declared and must match.** A reader that does not recognise
  a schema must refuse the file rather than map columns by position and
  confidently report the wrong sensor's numbers.
- **The summary JSON says how it was made.** The scorer, the epoch lengths, the
  band's method string, the HR mode, and — in the file itself, not only here —
  `"validated_against": "synthetic fixtures only; no polysomnography"`.

### Storage arithmetic

| | per night | per decade |
| --- | --- | --- |
| epochs (always) | ~46 KB | ~17 MB |
| raw at 25 Hz (opt-in) | **~31 MB** | — |

So epochs always, raw never by default. Raw is capped twice — bytes and minutes
— and a row is only written if its worst case still fits, so the file never
crosses its budget mid-row. The caps are self-defence, not device awareness:
**the SDK exposes no free-space query**.

Raw values are integer **microgravities**, not floats. The MCU's newlib may not
link `%f`, and a recorder that silently writes empty fields for its own samples
is worse than one that scales.

## Settings

Write `settings.json` into `Apps/SleepLab/` over USB.
[`settings.example.json`](settings.example.json) is ready to copy.

```json
{
  "schema": 1,
  "values": {
    "bedtime": "21:00",
    "wake_by": "11:00",
    "hr": "continuous",
    "alarm": "off",
    "alarm_at": "07:00",
    "raw_recording": "off"
  }
}
```

| Key | Values | Default |
| --- | --- | --- |
| `bedtime`, `wake_by` | `"HH:MM"` local. May cross midnight. | `21:00`, `11:00` |
| `min_night_min` | 30–960. Shorter sessions are discarded, not reported. | 90 |
| `hr` | `continuous`, `off`, `duty` | `continuous` |
| `hr_duty_on_sec`, `hr_duty_per_sec` | 5–3600, 10–3600 | 60, 300 |
| `alarm` | on/off | **off** |
| `alarm_at` | `"HH:MM"` — the hard deadline | `07:00` |
| `alarm_window_min` | 5–120 — how long before the deadline the smart window opens | 30 |
| `raw_recording` | on/off | **off** |
| `raw_max_mb`, `raw_max_min` | 1–512, 1–1440 | 64, 120 |

`on`/`yes`/`true`/`1`/`enabled` all mean on, in any case.

**An out-of-range value is refused, not clamped.** `"min_night_min": 99999` is a
typo, and clamping it would record nights nobody asked for while the log looked
perfectly healthy. Every rejection is logged with the value and the range, and
the default stands.

Every failure — absent, over 4 KB, unparseable, wrong `schema` major — falls
back to usable defaults. This app is autostart: a settings file that could stop
it running would stop it running *for ever*, with no screen to say why.

Three combinations that parse cleanly and cannot work are caught and reported: a
duty cycle whose on-time meets its period, a zero-width bedtime window, and an
alarm outside the bedtime window (which could never fire, and a silent alarm is
worse than none).

## The screen

`R1` toggles between the report and the history. `R2` leaves — and leaving does
not stop anything; that is what autostart is for.

```
LAST NIGHT
23:12 - 06:41
est 7h04  still 6h41
eff 89%  awake 3x
HR low 51  (-2 vs you)
[////▂▁▁▂▃▁▁▂██▁▁▁▂▃▂▁▁]
movement & heart rate - not sleep stages
```

The first line is the honesty line. If the night was interrupted, that is what
it says, before anything else — a night with a hole in it that looks like a whole
night is the second-worst thing this app could produce. If the watch was not
worn, it says that and there are no numbers below it.

The strip is 100 buckets at two pixels, which is what divides the panel's usable
width evenly. A bucket takes the **worst** verdict in its range rather than the
majority: a five-minute bucket containing one minute awake should show that you
woke, and a majority vote would hide every short awakening in the night — which
is exactly what a restless night is made of. Awake is drawn in a warm accent
against the cool sleep tones, so it reads as an interruption rather than as
another level.

The caption is a sibling widget, not a comment. It travels with the picture.

**Reachability.** On 1.3 firmware the quick-access menu showed only three
`Utility` apps, filled alphabetically, so a new one could be installed and
running while being impossible to open. 1.4 replaced that with a scrolling list.
The app still does not *rely* on being opened — the report is a file whatever
happens, and the glance and home widget surface it without a launch.

### Glance and home widget

Both, because a sleep report is glanced at once, half awake. The glance shows
last night's headline with `est` in front of it; the home widget shows it in the
morning only — from a night closing until the next bedtime window opens, because
a widget still showing Tuesday's efficiency on Thursday afternoon is clutter.

Neither required changing the app type: `una-app.cmake` passes `-glance_capable`
unconditionally, so a `Utility` app is already glance-capable and keeps
autostart. A `Glance`-type app's service is driven by the carousel and returns
from `run()` on `EVENT_GLANCE_STOP`, which is the opposite of what an all-night
recorder needs.

## The smart alarm

**Off by default, and test it on a weekend before trusting it.**

Inside a window before your hard deadline, it fires on the first epoch that
scores as wake; otherwise it fires at the deadline. Both the vibro and the
buzzer, from the service, with no GUI attached.

Two honest limitations:

- The verdict used is the **raw Cole-Kripke one, without Webster rescoring**.
  Rescoring is a whole-night pass and at 06:20 there is no whole night. The
  epoch judged is also two minutes old, because Cole-Kripke needs two epochs of
  look-ahead.
- As of the 1.4 kernel, mute does not silence app-requested alerts — muting
  covers alerts the watch raises at you, not feedback an app produces in a
  session it owns. **Unverified on this unit** (ledger row T1), and an alarm
  that fails silently is worse than no alarm.

## Getting the files off

```sh
python3 Tools/pull_nights.py E8:DF:D5:49:4C:40 --out ./nights
```

Over BLE, so the service keeps running. It is a thin wrapper around
`prototype/una_ble_client.py` from the `2026-07-29-hardware-config-recovery`
investigation on `una-sdk@research` — validated against a real watch — which is
referenced rather than copied so there is one copy to keep correct. Needs Linux
with BlueZ, `dbus_fast`, and a watch already bonded via `bluetoothctl`; the File
Transfer Service requires an encrypted link.

It is slower than USB and it does not stop the recorder. That is the trade.

USB works too, and is fine once the night is over: copy `Apps/SleepLab/` off.
`Utilities/Scripts/Update-Watch-Apps.ps1` (new in 1.4) updates apps over USB
while preserving their data, which for this app means not destroying accumulated
nights.

**Turn off BLE phone-sync before any USB-MSC session.** Concurrent watch BLE
sync and host writes to the same exFAT partition corrupt files.

## No FIT export

`SDK/Fit/FitProfile.hpp` on 1.4 has no monitoring or sleep messages and its
`File` enum holds only `Activity = 4`. Sleep export needs the monitoring file
type and the sleep message definitions from the official FIT profile, and those
have to be looked up and verified rather than invented.

Until then the CSV and JSON **are** the export — and the mobile companion has no
sleep concept to receive them anyway (`config.json` type is `utility`), so the
UI does not imply otherwise.

## Building

Needs `$UNA_SDK` pointing at an **`apps-v1.4.0`** checkout. Built in containers,
so the result does not depend on what happens to be installed:

```sh
Tools/docker-build.sh app       # the .uapp
Tools/docker-build.sh probe     # the Tier 0 probe
Tools/docker-build.sh tests     # host tests
Tools/docker-build.sh sim       # the simulator
Tools/docker-build.sh sim-run   # build it and run it headless
```

Three images, because they are three different jobs — the recipes are in
[`Tools/docker/`](Tools/docker) and the script's header says how to build them.
The `.uapp` one is layered on the toolchain Kira publishes binaries with, pinned
by digest, so a `.uapp` built here is the artifact the catalogue would carry.

or directly:

```sh
export UNA_SDK=/path/to/una-sdk          # apps-v1.4.0
cd Software/Apps/SleepLab-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

`AppID` is `E4782CD726259DC6` =
`sha256("https://github.com/tobymurray/watch-apps#sleeplab")[0:8]`, the repo
convention — the anchor is the app's purpose, not its folder.

**1.4, not 1.3.** An app carries the kernel interface version it was built
against: `apps-v1.4.0` is 3, and a v3 app on a v2 kernel exits instantly to an
`App PID` error screen with nothing catching it at build time. Confirmed
2026-08-18 that this watch runs the 1.4 line. On 1.3 you would need `SendMsg.hpp`
backported (1.3's `allocateMessage` cannot forward constructor arguments), you
would lose the home widget entirely, you would need the
`-fcyclomatic-complexity` probe `MapManager` carries, and the three-`Utility`-app
menu cap would come back.

Building the target ELFs needs an `arm-none-eabi` toolchain that provides the
newlib syscall stubs — ST's GNU Tools for STM32 works; a stock Ubuntu
`gcc-arm-none-eabi` fails at link on `_write`/`_read`/`_lseek`/`_close`. The
image `docker-build.sh` uses is the one Kira publishes binaries from.

Or with Kira:

```sh
kira build-app --app SleepLab --sdk /path/to/una-sdk --version 0.1.0 --out SleepLab.uapp
```

Deploy by copying the `.uapp` into `Apps/SleepLab/` on the USB-MSC volume; the
kernel rebuilds `Apps/app_list.json` from the `.uapp` headers on boot. If a
rebuild changes `APP_USER_NAME` the filename changes with it, and the old
`.uapp` must be **deleted**, not just overwritten.

## The simulator

```sh
cd Software/Apps/TouchGFX-GUI
UNA_SDK=/path/to/una-sdk make -f simulator/gcc/Makefile -j8
./build/bin/simulator.out
```

Buttons are keys `1`=L1, `2`=L2, `3`=R1, `4`=R2, and the window is 240×240 — the
panel's real size.

Or, in a container, with a seeded history so the screens have something to draw:

```sh
Tools/docker-build.sh sim-run
```

It needs SDL2, Ruby (the TouchGFX asset generators are Ruby scripts) and amd64
(some of them are amd64-only binaries), which is why that target uses a
different image from the host tests.

Two caveats worth stating plainly.

**The simulator has no sensors and no battery.** It exercises the screen, the
message contract and the file reading, and proves nothing whatever about whether
an eight-hour recording survives on hardware. That is what the
[probe](Probe/README.md) is for.

**It does not deliver `COMMAND_APP_NOTIF_GUI_RUN`.** Found by running it: the
service never learned a GUI was attached, never published, and the screen sat on
"waiting for service..." for the whole run. The service now treats a
`SLEEP_REQUEST` as that evidence instead — only a GUI sends one, so it is
better evidence than the notification, and nothing depends on the notification
any more. Every simulator run also ends with `pure virtual method called` after
the app has stopped; that is the SDK's own teardown order (fixed on
`una-sdk`'s `fix/simulator-shutdown-pure-virtual`, PR #214), not this app's, but
it does mean a simulator run's exit status says nothing.

## Tests

```sh
Tools/docker-build.sh tests
```

126 tests across four suites — see [`Tests/README.md`](Tests/README.md), which
also explains what the evidence actually is and the two tests worth knowing
about before changing anything.

## Turning the guesses into measurements

Two host scripts, and between them they set every unjustified constant in the
app. [`Docs/ROLLOUT.md`](Docs/ROLLOUT.md) says when to run each.

```sh
# where the movement thresholds belong, from nights you recorded
python3 Tools/night_report.py thresholds --worn ./nights/worn --table ./nights/table

# the honest accuracy number, against times you wrote down by hand
python3 Tools/night_report.py diary ./nights --diary diary.csv
```

The first prints the count distribution for a worn night against a nightstand
night and suggests a value for each threshold — and says so plainly when the two
distributions overlap and *no* value separates them, which is a finding rather
than a failure. The second reports mean signed error and spread on onset and
final wake, refuses to quote an accuracy figure off fewer than ten nights, and
excludes nights the worn gate suppressed, because folding those in as zero error
would flatter the result.

Both are checked against the real writer: `night-log-export` writes synthetic
nights with the real `NightStore` and a ctest parses them with the real script.

## Known rough edges

- **No night has been recorded on hardware.** Every sensor claim in the ledger's
  §2 is UNVERIFIED.
- **`kCountScale` is a guess**, so the sleep/wake correspondence is unvalidated.
  It carries a TODO naming the recording that would settle it, and so does every
  other unjustified threshold in the app.
- **A resumed night is scored from the epochs recorded since the restart.** The
  earlier ones are on disk but not in RAM; time in bed comes from the state file
  so the total is right, but the scoring array is short. Re-reading the CSV back
  in was considered and rejected — it would put a parser for this app's own
  output on the recording path, and a resumed night is already flagged as one
  whose numbers are not clean.
- **The GUI tree is a fork of `MapManager`'s**, itself a fork of `Chrono` ←
  `Stopwatch`. The Designer-generated stopwatch widgets are hidden rather than
  removed, to avoid editing the fragile packed string table; the real widgets
  are hand-built alongside them. The layout therefore lives in code rather than
  in the `.touchgfx` file.
- **The history is an index file, not a directory listing.** Deliberate — see
  `NightStore.hpp` — but it means a night whose index row was lost is invisible
  to the app even though its CSV is right there.
- **`Service::run()` is not host-tested** in either app. It blocks on the kernel
  message queue and never returns.

## Provenance

The service shell and the deadline-bounded sleep idiom come from the SDK's
`Alarm` and `Timer` examples. The GUI tree is forked from
[`MapManager`](../MapManager), which forked it from
[`Chrono`](../Chrono) ← the SDK's `Stopwatch`; none of MapManager's pack-verifier
logic came with it. The bounded JSON config reader follows
[`Barcode`](../Barcode)'s, which follows `SDK::Variant::Config`. The
chunk-flush-and-manifest discipline and the two-level format checking are
[`FwDump`](../FwDump)'s. The recorder-before-classifier ordering is
[`Squash`](../Squash)'s, and this app's raw capture is the same argument applied
to a different sensor.

The `HEART_BEAT` / PPG answer, the BLE File Transfer client, and the
CONFIRMED / LIKELY / UNVERIFIED / REFUTED convention are all from
`una-sdk@research` — referenced, not copied.

## Installing

**Sleep Probe is submitted to [Kira](https://kira.technicallyrural.com)**
([registry PR](https://github.com/tobymurray/kira/pull/new/registry/sleep-probe)).
Once that lands you can install it from there and skip the toolchain entirely;
until then, build it below. Either way, start with the probe whatever you intend
to do next: it answers whether an all-night recording works on your watch, and
that question is upstream of everything SleepLab claims.

SleepLab itself is **not** on the catalogue yet, deliberately. The two must not
be installed at once — both autostart and both subscribe the accelerometer and
the heart-rate sensor, and what the sensor layer does with two claimants is
unverified (ledger row S8). Build SleepLab yourself for now; its manifest is
prepared at [`Docs/kira-sleep-lab.toml`](Docs/kira-sleep-lab.toml) and
[`Docs/ROLLOUT.md`](Docs/ROLLOUT.md) says when it is ready to submit.

## Licence

MIT.
