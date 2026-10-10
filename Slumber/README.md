# Slumber — sleep tracking with nothing to start at bedtime

Slumber is what [`SleepLab`](../SleepLab) becomes once it is a product. It ships
as two installs:

1. **Slumber.** A `Utility` app that starts at boot and never stops. It does the
   recording, keeps the history in its own folder, and owns the screen you open
   from the Utility list.
2. **[SlumberGlance](../SlumberGlance).** A `Glance` app. It owns the carousel
   card, and it only reads what Slumber publishes.

**This build records; it does not report.** It logs a row every 30 s all day,
and the wearer's own marks beside it, so the questions below can be answered
from what is on the watch.

## Why two apps, not one

Settled on the watch on 2026-10-09, on firmware 1.4.0, by Slumber 0.1.0. That
build was one `Utility` that also tried to serve a glance.

- **The kernel never offered it a card.** `Debug/lifecycle.log` covers a whole
  boot. It has `start` at 9 s, then a `gui-open` and a `gui-close`, then `stop`
  at 135 s. It has no `glance-start` at all, though the carousel was scrolled in
  that time.
- **The kernel's registry records one type per app.** `Apps/app_list.json`
  listed Slumber as `type 1`. Every card in the carousel belongs to a `type 2`
  app. The file has no field for "glance-capable".
- **The header cannot ask for both.** In `app_merging.py` the type is a number
  (`Activity 0`, `Utility 1`, `Glance 2`, `Clockface 3`), not a set of bits.
  The packer sets the "glance-capable" bit `0x20` on every app. Stopwatch,
  Timer, Alarm and SleepLab all carry it, and none of them has a card.
- **The card order file is not a whitelist.** `Apps/app_sorting.json` held
  `"glances":[]`, yet the stock cards still showed.

The SDK's `Docs/app-config-json.md` says an app "can combine several types". On
1.4.0 the kernel does not do this.

**To overturn this:** an app with `-type Utility` and the `0x20` bit set logs
`EVENT_GLANCE_START` on some later firmware.

## The card

Slumber publishes one 48-byte record to `../SharedData/slumber/card`.
`Software/Libs/Header/Card/` holds its format and the code that reads and writes
it. Both apps compile that same header-only code, and `Tests/` covers it on a
desk.

The record carries a magic number, a schema version, a sequence number, a
heartbeat time, the state, the night's numbers and a CRC-32. The rules it
follows come from una-sdk PR #270, which documents how `SharedData` behaves:

- **`rename()` will not replace an existing file.** So a write goes: temp file,
  remove the `.bak`, rename the card to `.bak`, rename the temp to the card. A
  crash at any step leaves one intact copy. The reader falls back to the `.bak`,
  and the CRC means it never accepts half a record.
- **`open()` returning `false` is ambiguous.** It means the same for "absent",
  "locked by a writer" and "out of handles". So the reader uses `exist()` to
  tell `Absent` (Slumber never ran) from `Busy` (keep the last card and try
  again).
- **The watch has ten open files in total.** Every open is closed straight away.

Slumber refreshes the heartbeat every `SlumberCard::kHeartbeatS` (5 min). After
two missed heartbeats and a minute's grace, the glance says Slumber has stopped.
Five minutes is a probe's pace. The flash cost of keeping it in a shipping
build is not yet measured.

## Finding a night

`Software/Libs/Header/Sleep/SleepPeriods.hpp` finds sleep in 30 s epochs at
any hour. It uses no bedtime window.

- **Rest** means the 5.5-minute rolling median of the activity count is under
  1100, and the epoch has fewer than 10 steps.
- **A block** is at least 30 minutes of rest.
- **Blocks less than 60 minutes apart are merged**, so a bathroom trip stays
  inside its night, and a night that runs past noon is not cut at a clock.
- **Coming off the wrist ends a period.** Off the wrist means two thirds of the
  nearby heart-rate epochs have trust under 2.0, or no reading at all while
  heart rate was subscribed. A watch left on the table before or after a night
  is its own `NotWorn` period.
- **A period is final** once 60 minutes have passed after it.

The 30-minute block, the 60-minute merge and the rolling median are van Hees et
al. 2018's (Sci Rep 8:12975). The other constants were measured on 33 sessions
SleepLab recorded on one watch between 2026-08-20 and 2026-10-06; each constant
carries its numbers in the header.

**Replayed against those sessions:**

| | SleepLab's segmenter | SleepPeriods |
| --- | --- | --- |
| Worn nights split in two | 7 | 3. The rest were 64, 102 and 108 min apart, more than the 60-minute merge |
| Worn nights split in three | 0 | 1, at a stretch of low-trust heart rate (see below) |
| A table session judged worn | 1 | none |
| A table session with heart rate silent | uncertain | not-worn |

Silence counting against the wrist is SleepLab's `WornGate` rule: heart rate
that was subscribed and delivered nothing is evidence of a table, and heart
rate never subscribed is evidence of nothing. Trust rather than presence comes
from SleepLab's pillow recording, where the optical sensor read a pulse in 98%
of epochs on an empty pillow, all of them under 2.0.

Recorded nights are personal data and are never committed. Set
`SLUMBER_REPLAY_DIR` to a copy of a watch's `Apps/SleepLab/Nights/`, or
`SLUMBER_DAYS_DIR` to a copy of `Apps/Slumber/Days/`, and
`Tests/SleepPeriodsReplay_test.cpp` replays them. Otherwise each skips.

**Not yet known:** whether a 35-minute stretch of low-trust heart rate in the
middle of a worn night is the watch off the wrist or the arm pressed against
the sensor. One recorded night has one, and it splits that night in three.
Only a diary can say which it was.

## What the log answers

Each question is answered from files on the watch's volume under
`Apps/Slumber/`, after the fact, on a desk. Without the marks, nothing here
separates sitting still from sleeping, so the screen exists to collect them.

| Question | Answered by |
| --- | --- |
| Does sitting still (TV, reading, a desk) look like sleep by movement alone? | Marks `Sitting, TV`/`Sitting, other` against `count`, `count_x/y/z`, `peak`, `steps` |
| Does wrist angle separate them where movement does not? | `turn_sum_dx10`, `turn_max_dx10`: degrees the gravity vector turned between 5 s blocks |
| How far does heart rate fall at sleep onset, against sitting awake? | `hr_mean_x10`, `hr_min_x10`, `hr_max_x10` around `Lights out` and the sitting marks |
| Is a low-trust stretch in a worn night the watch off, or an arm on the sensor? | `hr_n` (0 = subscribed and silent, -1 = not subscribed), `hr_trust_x10`, `temp_x10`, `grav_x/y/z` (a table holds one orientation), `count` floor, the `Watch off` mark |
| What does recording all day cost? | `batt_mah`, `batt_mv`, `batt_ma_x10`, `batt_avg_ma_x10` per row; `wakes`, `msgs` for the loop |
| Did recording ever stop, stall or lose data? | `uptime_ms` continuity, `span_ms`, `late_ms`, `slots_lost`, `acc_samples`, `acc_max_gap_ms`, `write_fails`; `start`/`stop`/`late`/`write-fail` in the lifecycle log |
| Did the clock or the time zone move? | `utc_off_min` per row; `clock` lines in the lifecycle log |
| Where does SleepPeriods put each period, and does it agree with the marks? | `utc`, `count`, `steps`, `hr_n`, `hr_trust_x10`: every input it takes, on SleepLab's scale. `SLUMBER_DAYS_DIR` replays them |

**Files.**

| Path | What |
| --- | --- |
| `Days/<yyyymmdd>.csv` | One row per 30 s, named for the local day. Schema in `Software/Libs/Header/Recorder/EpochRow.hpp`; -1 means not measured |
| `Marks.csv` | `utc,utc_off_min,uptime_ms,local,code,label`, one line per R1 press |
| `Debug/lifecycle.log` | `start`, `sensors` (upper case = resolved), `day`, `late`, `clock`, `mark`, `write-fail`, `card-fail`, `gui-open`, `gui-close`, `stop` |
| `../SharedData/slumber/card` | The glance's card, every 5 minutes |

Rows are buffered and written ten at a time, so a crash loses at most five
minutes; a USB unplug stops the app cleanly and flushes. A day file is about
0.5 MB.

**The counts are SleepLab's.** `ActivityCount` is SleepLab's counter, and a
host test holds the two equal sample for sample, so SleepPeriods' measured
thresholds apply to these rows unchanged.

## What the first day settled

One 15.75-hour day of schema 1, worn with marks, with SleepLab 0.4.0 still
installed and recording beside it. Schema 2 is what it changed.

- **The counts agree on the watch too.** Over the 1,012 epochs both apps
  recorded, Slumber's count over SleepLab's had a median ratio of 0.996, and
  heart rate matched to 0.1 bpm.
- **The accelerometer reads m/s², not g.** At rest the gravity vector's
  magnitude is about 9,900 in `grav_x/y/z`, so those columns are m/s² x1000.
  The counts are unaffected: SleepLab's thresholds were measured on the same
  raw values.
- **The rate is the firmware's.** Both apps ask for 40 ms with 5 s batching;
  both got 50 Hz (1,500 samples a row) in batches about every 190 ms, about 190
  wakes a row. SleepLab's nights show the same back to August. With SleepLab
  removed, `acc_samples` and `acc_batches` say whether that was it.
- **Off the wrist, heart rate is silent, not untrusted.** On a table for over
  three hours, not one of 354 rows had a reading. The count sat at 163-205,
  the sensor's floor, and the gravity vector did not turn. This is why silence
  now counts against the wrist.
- **`ACTIVITY_RECOGNITION` does not help.** It said `STILL` on 1,850 of 1,891
  rows, including rows with 1,672 steps between them. Schema 2 drops it.
- **`BATTERY_LEVEL` does not move.** It read 99.9-100% all day while
  `BATTERY_METRICS` capacity fell from 206 to 181 mAh and the cell from 4,395
  to 4,267 mV. Read `batt_mah`.
- **Skin temperature separated the table from the wrist that day**, but it
  depends on the room and the wearer, so nothing reads it; it stays in the log.
- **Not answered yet:** whether sitting still looks like sleep. No sitting marks
  were made.

## The screen

L1 and L2 pick what you are doing from now on; R1 logs it, and the label
turns green once the service has written it. Below the picker: the last mark
and how long ago, then rows written today and whether the last row had
accelerometer and heart-rate data. `WRITE FAILS` in amber means rows are not
reaching the volume.

Marks are states, not events: `Sitting, TV` holds until the next mark. Press
`Watch off` before taking the watch off, and pick what you are doing when it
goes back on.
