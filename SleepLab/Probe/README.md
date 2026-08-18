# Sleep Probe — measuring whether an all-night app is possible at all

A background `Utility` app that subscribes to every sensor a sleep tracker
might want, counts what actually arrives, and appends one line per minute to a
CSV. It computes nothing and decides nothing. It exists to answer eight
questions about this platform that nobody has answered, before
[SleepLab](../README.md) is built on top of the assumption that the answers are
good.

**Nothing on this device has ever run all night.** Every app in this repository
and in the SDK's `Examples/` is either foreground-interactive or a background
task measured in minutes. Whether a Service keeps its sensor subscriptions, its
file handles and its battery across eight hours of screen-off operation is
unmeasured — and a sleep app that discovers in month two that its sensors
stopped at 02:00 has wasted month one.

## The questions

Each maps to a row in [`../Docs/FEASIBILITY-LEDGER.md`](../Docs/FEASIBILITY-LEDGER.md),
and `Tools/probe_report.py` prints them grouped the same way.

| | Question | Answered by |
| --- | --- | --- |
| S1 | Does sensor delivery survive a whole night, and if not, where does it stop? | gaps in `uptime_ms` |
| S2 | What does continuous optical HR cost overnight, in percent and mA? | `batt_pct_x10`, `batt_avg_ma_x10`, two nights |
| S3 | What is the *delivered* rate per sensor over hours? | `acc_n` against `acc_ts_span_ms` |
| S4 | Does `SPO2` produce a single sample? | `spo2_n` |
| S5 | Does `HEART_BEAT` still emit nothing on 1.4? | `beat_n` |
| S7 | Does `TOUCH_DETECT` hold "worn" on a sleeping wrist? | `touch_edges` |
| S8 | Does anything else contend for the HR sensor? | `hrex_opt` / `hrex_ext` / `hrex_unk` |
| S9/S10 | Free space, append throughput, and how often an idle loop wakes | `wakes`, file size |

S5 is the highest-value one. UNA answered it authoritatively against the 1.3
line — `HEART_BEAT` emits **no events at all**, because HR detection is a
frequency-domain algorithm rather than per-beat detection ([PR
#167](https://github.com/UNAWatch/una-sdk/pull/167), recorded on
`una-sdk@research`). Firmware moved to 1.4 on 2026-08-17, and UNA describe both
a higher-rate PPG mode and on-chip HRV as things they are working on. So the
answer has an expiry date, and **a non-zero `beat_n` reopens overnight HRV —
which reopens sleep staging**. The probe subscribes to `HEART_BEAT` in every
mode, including `"hr": "off"`, because a stream that emits nothing costs nothing
to listen to.

The canonical 90-second version of that diagnostic is `BeatProbe.hpp` on
`una-sdk@research` under `Docs/Investigations/2026-06-15-heart-beat-vs-ppg/`,
with a usage guide and a patch that applies cleanly to the Sensors tutorial. It
is deliberately referenced rather than copied, so there is one copy to keep
correct. This probe asks the same question over eight hours instead of ninety
seconds, which is strictly more evidence for the same cost.

## Running a night

**Unplugged.** Plugging in terminates every running app and autostart relaunches
it on unplug, so a watch on the charger records nothing. `BATTERY_CHARGING` is
subscribed anyway, because "it was on the cable" has to be visible in the data
rather than remembered.

1. Build and deploy (below). The app autostarts; there is nothing to launch.
2. Open its screen once before bed and check the sensor block — see
   [The screen](#the-screen). Then leave it.
3. Sleep, wearing the watch, off the charger.
4. In the morning, plug in, copy `Apps/SleepProbe/probe_log.csv` off, and run
   the report.

```sh
python3 ../Tools/probe_report.py /path/to/Apps/SleepProbe/probe_log.csv
```

**Run at least two nights**, because half the questions are comparative:

| Night | `probe.json` | Answers |
| --- | --- | --- |
| 1 | `"hr": "continuous"` | S1, S3, S4, S5, S7, S9, S10, and the expensive half of S2 |
| 2 | `"hr": "off"` | the cheap half of S2 — the difference is what HR costs |
| 3 (optional) | on a table, not worn | the false-worn half of S7. A nightstand is perfectly still and would otherwise score a flawless night |

The log is appended across launches and boots, so all three nights can live in
one file; the report splits it into launches and reports the longest by default.

## Settings

`probe.json`, in the app's own folder, written over USB. There is no companion
channel to a third-party watch app and there will not be one
(`Docs/companion-data-channel-analysis.md` on `una-sdk@research`), which is why
every configurable app in this repository reads a file instead —
[`Barcode`](../../Barcode/README.md) is the precedent.
[`probe.example.json`](probe.example.json) is ready to copy.

```json
{
  "schema": 1,
  "values": {
    "hr": "continuous",
    "accel_period_ms": 40,
    "accel_latency_ms": 5000,
    "spo2": "on",
    "ppg": "off"
  }
}
```

| Key | Values | Default | Notes |
| --- | --- | --- | --- |
| `hr` | `continuous`, `off`, `duty` | `continuous` | `duty` also reads `hr_duty_on_sec` and `hr_duty_per_sec`. |
| `hr_duty_on_sec` | 5–3600 | 60 | Must be less than the period, or the mode falls back to `continuous` and says so. |
| `hr_duty_per_sec` | 10–3600 | 300 | |
| `accel_period_ms` | 5–1000 | 40 | A *request*. What is delivered is what the log measures, and the two are not the same number. |
| `accel_latency_ms` | 0–60000 | 5000 | Batch latency. Without it, 25 Hz is 25 IPC wakes a second for eight hours. |
| `spo2` | on/off vocabulary | `on` | One night settles S4. |
| `ppg` | on/off vocabulary | `off` | 20 Hz all night is a lot of IPC to answer a question a short run answers. |

`on`, `yes`, `true`, `1`, `enabled` all mean on, in any case; `off`, `no`,
`false`, `0`, `disabled` mean off. Anything else keeps the default and logs a
warning — these files are typed by hand.

Every failure falls back to defaults: absent, oversized (over 4 KB, refused
before anything is allocated), unparseable, wrong `schema` major, or a value out
of range. A config somebody else wrote must never stop the probe starting, and
a night that records the wrong mode is recoverable because the mode is stamped
into the log's own `R` row.

An out-of-range value is treated as **absent, not clamped**. Clamping turns a
typo into a silently different experiment: `"accel_period_ms": 4000` meaning
4 seconds would clamp to the ceiling and record a night at a rate nobody chose,
and the log would look perfectly healthy.

## The screen

Six lines, and it exists for one moment: the thirty seconds before bed, when the
only question is *is this actually going to record anything?*

```
run 0h04m  hr:cont
ATMRHXBoOSLCE
rows 4  1k
last acc 1421  hr 58
worn 60/60  beat 0 spo2 0
batt 88.1%
```

The second line is the subscription block, one letter per sensor, **upper case
for resolved and lower for not** — `o` above means SPO2 did not resolve a
driver. Case rather than presence, so the block is the same letters in the same
places every time and a missing sensor is a change you can see rather than one
you have to read. Order matches the log's columns: `A`ccel, `T`ouch, `M`otion,
activity `R`ecognition, `H`R, hr e`X`, `B`eat, `P`PG, sp`O`2, `S`teps, battery
`L`evel, battery `C`harging, battery m`E`trics.

If the cable is in, the last line says `UNPLUG USB TO RECORD` instead of the
battery level, because that is the one state where nothing else on the screen
matters.

`R2` leaves. Nothing else is bound: the probe records until the app is removed,
and a button that could stop it would only add a way to lose a night.

**It has a screen only because it has to.** A service-only `.uapp` is refused
for every type except `Glance` (`app_merging.py:205`), and `Glance` would cost
autostart — a Glance app's service is driven by the carousel and returns from
`run()` on `EVENT_GLANCE_STOP`. Ledger rows P3 and P5.

## The log format

Normative spec: the file comment on
[`Software/Libs/Header/ProbeLog.hpp`](Software/Libs/Header/ProbeLog.hpp). The
short version:

- Line-oriented CSV with a `kind` first column. `H` header (once per file), `R`
  run start (once per app launch), `M` minute.
- Every numeric field is an integer. Nothing formats a float — the MCU's newlib
  may not link `%f`, and a diagnostic that silently prints empty strings for its
  own measurements is worse than one that scales by ten. `_x10` and `_x100`
  suffixes mean the value times that factor, truncated.
- **A missing measurement is `-1`, never `0`.** Zero delivered HR samples in a
  minute is a finding; a sensor that was never subscribed is not. An average
  that folds the second into the first is wrong, and the report script drops the
  sentinel rather than averaging it in.

### Launch boundaries

`MapManager`'s log reads as one confused process until you know that USB
restarts everything; the boundaries had to be recovered afterwards by hand. This
log marks them. Every launch writes an `R` row carrying both clocks:

- uptime climbs across the `R` row → app restart within one boot (a USB session,
  or a service crash)
- uptime jumps backwards → device reboot
- wall clock moves without uptime agreeing → the wall clock changed under us

That last case is why every row carries both clocks. Uptime is monotonic but
says nothing about "23:00"; wall clock knows what 23:00 means but jumps on a
timezone change, a host sync or DST. **No duration anywhere is derived from two
wall-clock readings.**

## Building

Needs `$UNA_SDK` pointing at an `apps-v1.4.0` checkout. The repo builds in
containers rather than depending on what happens to be installed:

```sh
../Tools/docker-build.sh probe
```

or directly:

```sh
export UNA_SDK=/path/to/una-sdk          # apps-v1.4.0
cd Software/App/SleepProbe-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=0.1.0 . && cmake --build build
```

The `.uapp` lands in `build/`. `AppID` is `999A630E7F105A07` =
`sha256("https://github.com/tobymurray/watch-apps#sleepprobe")[0:8]`, the repo
convention — the anchor is the app's purpose, not its folder.

Building the target ELFs needs an `arm-none-eabi` toolchain that provides the
newlib syscall stubs. ST's GNU Tools for STM32 works; a stock Ubuntu
`gcc-arm-none-eabi` fails at link on `_write`/`_read`/`_lseek`/`_close`. The
image `docker-build.sh` uses is the one Kira publishes binaries from, so a
`.uapp` built there is the artifact the catalogue would carry.

## Deploying

Copy the `.uapp` into `Apps/SleepProbe/` on the USB-MSC volume; the kernel
rebuilds `Apps/app_list.json` from the `.uapp` headers on boot. Two warnings,
both learned the hard way elsewhere in this repo:

- **Turn off BLE phone-sync before any USB-MSC session.** Concurrent watch BLE
  sync and host writes to the same exFAT partition corrupt files.
- If a rebuild changes `APP_USER_NAME` the filename changes with it, and the old
  `.uapp` must be **deleted**, not just overwritten.

To stop the probe, delete `Apps/SleepProbe/`. It is autostart with no stop
button, which is the correct trade for a diagnostic whose job is to still be
running at 04:00.

## Tests

```sh
../Tools/docker-build.sh tests
```

`sleeplab-probe-tests` covers the writer: that the header is written once and
only for a new file, that a relaunch appends an `R` row carrying its HR mode,
that a row has exactly as many fields as the header names, that an unfilled row
is all sentinels rather than all zeroes, that negative and 64-bit values survive
the round trip, and that every row is flushed with no handle left open.

`sleeplab-probe-report-roundtrip` is the one that matters. The tests above pin
the writer to *this repo's reading* of the format; this one writes a real
synthetic night with the real `Probe::Log` and parses it with the real
`Tools/probe_report.py`, then asserts the report actually detects the awkward
cases the fixture contains — a twelve-minute delivery hole, two launches, and
the expected `HEART_BEAT` verdict. Same two-level check
[`FwDump`](../../FwDump/README.md#verifying-the-contract-against-the-real-script)
uses against `reassemble_dump.py`. It is skipped, not failed, without python3.

The service's own `run()` loop is not host-tested: it blocks on the kernel
message queue and never returns.

## Known rough edges

- **The GUI tree is a fork of `MapManager`'s**, itself a fork of `Chrono` ←
  `Stopwatch`. The Designer-generated stopwatch widgets are hidden in
  `setupScreen()` rather than removed, to avoid editing the fragile packed
  string table; the real widgets are hand-built alongside them. Supported, but
  it means the layout lives in code rather than in the `.touchgfx` file.
- **No free-space query.** The SDK exposes none, so S9 is answered by watching
  the file grow rather than by asking. The log has no size cap: a night is
  ~70 KiB and the app is meant to be removed after a few of them.
- **The simulator is not useful here.** It has no sensors to deliver and no
  battery to drain, so a simulator run proves the writer and the screen work and
  proves nothing whatever about the questions this app exists to ask.
