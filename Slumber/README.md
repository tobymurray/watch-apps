# Slumber — sleep tracking with nothing to start at bedtime

Slumber is what [`SleepLab`](../SleepLab) becomes once it is a product. It ships
as two installs:

1. **Slumber.** A `Utility` app that starts at boot and never stops. It does the
   recording, keeps the history in its own folder, and owns the screen you open
   from the Utility list.
2. **[SlumberGlance](../SlumberGlance).** A `Glance` app. It owns the carousel
   card, and it only reads what Slumber publishes.

It logs a row every 30 s all day, and the wearer's own marks beside it, so the
questions below can be answered from what is on the watch. From the same
epochs it finds last night and puts it on the card.

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

## Sharing sleep with other apps

Slumber writes what it finds to `../SharedData/health/sleep/` for any other app on the
watch to read. An aggregator might, for example, set nights against training load.
`Docs/SHARED-HEALTH.md` owns the conventions: the directory, how a line is appended, how
the state file is replaced, and what `null` means. This section is what Slumber puts there.

The card above stays the glance's private contract.

**`sessions.jsonl`**: one line per period, once SleepPeriods has settled it, an hour
after its last rest. Naps and table sessions are periods too, so filter on `worn`.

| Field | Meaning |
| --- | --- |
| `schema`, `source` | `1`, `"Slumber"` |
| `start`, `end`, `utc_off_min` | The period, in UTC seconds, and the offset at the time |
| `rest_min` | Minutes of rest inside it, after the 5-minute median |
| `worn` | `"worn"`, `"not_worn"` (rest on a table) or `"uncertain"` |
| `start_local`, `end_local` | The same edges as `yyyy-mm-dd hh:mm` local, for people |
| `span_min`, `missing_min` | Its length, and the minutes inside it with no data |
| `longest_rest_min`, `restless_spells` | "Reporting a night" defines them |
| `worn_pct`, `hr_cover_pct` | The heart-rate evidence the `worn` verdict came from |
| `hr_median_bpm`, `hr_low_bpm`, `hr_low_at` | Sleeping heart rate: the median, the lowest 10-minute mean and the middle of that window in UTC seconds; `null` with too few trusted readings. "Heart rate against your normal" defines them |
| `epoch_s`, `epochs` | One character per 30 s from `start`: `r` rest, `m` movement, `-` no data |

`epochs` is what the wrist saw before any smoothing. `m` is an epoch whose activity count
reached SleepPeriods' rest ceiling, or had 10 or more steps. Neither letter is a claim
about being awake: a wearer lying awake and still is `r`, and the wrist cannot tell sleep
stages apart (see "The screens to come"). Over the 76 SleepLab sessions replayed in
`Tests/`, the longest period was 1,683 epochs, so a line stays under about 2 KB.

**`now.json`**: rewritten with the card, every 5 minutes.

| Field | Meaning |
| --- | --- |
| `written`, `heartbeat_s`, `utc_off_min` | When, how often, and the offset |
| `asleep_since` | Start of the worn period still running, else `null` |
| `last_night` | `{start, end, rest_min, worn, settled}`, the night the card shows, else `null` |
| `usual` | `{bed_min, wake_min, nights}`: medians, in minutes past local midnight, of the main nights logged, at most six; `null` until three |
| `rest_goal_min`, `rest_fill` | 420, and last night's rest against it in per mille; past 1000 is a second lap |
| `rhythm_fill`, `rhythm_offset_min` | Last night's onset against the usual of the nights before it; `+` is later |
| `tonight` | `{weekday, bed_min, wake_min, own_nights, nights}`: the usual for the night ahead, learned for its weekday (0 is Sunday); `null` until three nights. "Learning the week" says how |
| `week` | `{later_bed, earlier_bed, later_wake, earlier_wake}`, arrays of the weekdays that keep their own hours, and `social_jetlag_min`, or `null` until there are enough nights |
| `heart` | `{median_bpm, low_bpm, low_at}` for last night, against `{usual_median_bpm, usual_low_bpm, usual_nights}`, with `median_vs_usual_bpm` and `notable`; `null` unless last night was worn with enough trusted readings, and its `usual_` fields `null` until three earlier nights had them |

`asleep_since` comes late. A period exists only once it holds 30 minutes of rest, so it
appears about half an hour after falling asleep, and an app silencing notifications at
bedtime cannot do better from this file. A night's main night is the longest worn
period starting between noon and noon, so a nap does not move the usual.

The files outlive Slumber, as `shared-data.md` says every shared file does. The sessions
file grows by about 1 KB a night, and nothing trims it yet.

## Finding a night

`Software/Libs/Header/Sleep/SleepPeriods.hpp` finds sleep in 30 s epochs at
any hour. It uses no bedtime window.

- **Rest** means the 5.5-minute rolling median of the activity count is under
  1100, and the epoch has fewer than 10 steps.
- **A block** is at least 30 minutes of rest.
- **Blocks less than 60 minutes apart are merged**, so a bathroom trip stays
  inside its night, and a night that runs past noon is not cut at a clock.
- **Coming off the wrist ends a period.** Off the wrist means two thirds of the
  nearby heart-rate epochs have trust under 2.0 with the count at the sensor
  floor, or no reading at all while heart rate was subscribed. A watch left on
  the table before or after a night is its own `NotWorn` period.
- **A period is final** once 60 minutes have passed after it.

The 30-minute block, the 60-minute merge and the rolling median are van Hees et
al. 2018's (Sci Rep 8:12975). The other constants were measured on 33 sessions
SleepLab recorded on one watch between 2026-08-20 and 2026-10-06; each constant
carries its numbers in the header.

**Replayed against those sessions:**

| | SleepLab's segmenter | SleepPeriods |
| --- | --- | --- |
| Worn nights split in two | 7 | 3. The rest were 64, 102 and 108 min apart, more than the 60-minute merge |
| Worn nights split in three | 0 | none |
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

**Low trust in the middle of a night is an arm on the sensor**, not the watch
off. On 2026-10-11 the wearer lay on the watch from 01:33 to 02:32, with no
`Watch off` mark: heart rate read at 56 bpm in all 118 epochs with trust under
2.0 in 112, and the count sat at 420-500. Off the wrist the count is the
accelerometer's own noise, 167-205 on a table or a pillow, so a low-trust
reading above `kSensorFloorCount` (250) is no evidence either way. Over the
SleepLab sessions, quiet low-trust epochs on a nightstand or pillow exceeded
250 0.28% of the time and those in worn sessions 92%. The rule rejoined that
night and SleepLab's 2026-09-07, which had split in three, and changed no
table's verdict. Heart rate alone cannot make this call: the pillow read a
plausible 46-76 bpm.

## Reporting a night

Every 5 minutes, with the heartbeat, Slumber runs SleepPeriods over the last
36 h and puts one period on the card:

- **The latest sleep that ended in the last 20 h, unless it is under half as
  long as the longest one.** A sleep is the worn periods less than 3 h apart,
  and the card shows its longest period. SleepLab's 2026-08-25 night split at
  a 65-minute awakening into 5.3 h and 3.2 h, and the later half is not the
  night; the split nights above were 64-108 minutes apart. A nap the next
  afternoon is under half the night, so it does not replace it. A shorter
  night after a long one does: a 9 h night ending at 09:00 and a 5 h one
  ending at 04:00 show the 5 h one.
- **Only UTC durations choose**, so crossing time zones, or a watch whose clock
  loses its zone, cannot bring back an older night. The card shows a logged
  night's times in the offset it was logged with, the wearer's clock when they
  woke, so a flight later that day does not move them.
- **Until SleepPeriods settles it**, an hour after its last rest, the card says
  `so far`.
- **Only a table, and no worn period**, reports `No night` / `watch was off`.
  An uncertain wrist is never reported.
- **20 h after it ended**, the night expires and the card says `No night yet`.

The 36 h live in RAM and in `Epochs/`, and a restart reloads them before its
first card, so plugging in to charge in the morning keeps the night on the
card. A day missing from `Epochs/`, as after an upgrade from a build without
it, is rebuilt from its day file and written back. `Software/Libs/Header/Sleep/LastNight.hpp` makes the choice;
`SLUMBER_DAYS_DIR` replays a day file and prints what the card would say.

**Every period is logged once it settles**, one line in the shared
`sessions.jsonl` (see "Sharing sleep with other apps"), so the nights outlive
the 36 h window. Besides its edges and rest, each line carries
`Software/Libs/Header/Sleep/NightSummary.hpp`'s two numbers:

- **Longest rest**, the longest stretch at rest unbroken by movement or missing
  data.
- **Restless spells**, a minute or more of movement above the rest ceiling.
  Not awakenings: over 23 worn nights, SleepLab's and Slumber's, movement came
  in runs of 1-2 minutes, 198 runs of a minute or more and none of 5. A wrist
  cannot see someone lying awake and still, so nothing here claims to count
  awakenings.

## Learning the week

Slumber learns a usual for each night of the week from the main nights of the last 56
days, so a routine that changes is relearned within two months. `Sleep/Schedule.hpp`
holds it, and `Tests/Schedule_test.cpp` drives it through synthetic weeks.

- **A night belongs to the evening it began on.** Asleep at 00:40 on a Thursday is
  Wednesday night. Before 04:00, tonight is still the night begun the evening before.
- **A weekday's usual starts as the rest of the week's and moves toward its own.** It is
  `(n × own median + 2 × rest-of-week median) / (n + 2)`, so one late Wednesday moves the
  next Wednesday's usual a third of the way, and four of them move it two thirds.
- **A weekday differs only once it has shown it.** It needs three nights of its own and
  three of the rest of the week, a median 45 minutes or more from the rest of the week's,
  and three in four of its nights at least 22 minutes the same way. Two late Wednesdays
  out of the last six do not make a late Wednesday.
- **Social jetlag** is the median sleep midpoint of Friday and Saturday nights minus that
  of the other nights, the free-day and work-day comparison of Wittmann et al. (2006).
  The weekday arrays find other free days, such as a shift worker's, without assuming them.

None of this is on the watch's screen yet. `now.json` carries it for other apps, and the
screens to come decide how it is shown. One rule for them: the Rhythm ring may score
tonight against tonight's own usual, but a regularity measure must not, or a habit of
late Wednesdays would score as perfectly regular.

These thresholds are chosen, not measured. Each needs weeks of nights per wearer before
it can be checked, and the recording period gives two of each weekday at most.

## Heart rate against your normal

Each worn night carries its sleeping heart rate, and `now.json` sets last night's
against the wearer's own usual. It says how this night compares, never what a heart
rate means: 60 bpm is not good or bad, 4 above your usual is worth a look.

- **Only trusted readings count**, those at `SleepPeriods::kWornTrustX10` and between 30
  and 149 bpm. A night with fewer than half its epochs trusted has no heart rate.
- **The median** is to the nearest bpm. **The lowest** is the lowest mean over 10
  minutes with at least 15 of its 20 epochs trusted, so one low reading cannot set it.
- **The usual** is the median of up to 14 earlier main nights that had one, and needs
  three.
- **A night is `notable` when its median is 4 bpm or more from the usual.** Over 18 of one
  wearer's nights (SleepLab and Slumber), a night's median strayed from the nights
  before it by 1.6 bpm at the median, 4.1 at p90 and 4.8 at most, so 4 flags about one
  ordinary night in eight. The lowest strayed up to 6.9 and is never judged.
  `Tools/hr_noise.py` reruns the measurement over any copied-off nights.

The threshold needs other wearers' nights before anything on the screen is built on it.

None of this is on the screen yet.

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
| `Days/<yyyymmdd>.csv` | One row per 30 s, named for the local day. Schema in `Software/Libs/Header/Recorder/EpochRow.hpp`; -1 means not measured. A day file begun under another schema's header is renamed `<yyyymmdd>-<n>.csv` first, so no file mixes two |
| `Marks.csv` | `utc,utc_off_min,uptime_ms,local,code,label`, one line per R1 press |
| `Nights.csv` | What builds before the shared sessions logged per settled period; nothing writes it now |
| `Epochs/<yyyymmdd>.bin` | SleepPeriods' inputs, 14 bytes an epoch, so a restart can reload the last 36 h. A cache of the day CSV, rebuilt from it when missing or under another header; header in `Software/Libs/Header/Recorder/EpochHistory.hpp` |
| `Debug/lifecycle.log` | `start`, `sensors` (upper case = resolved), `history` (epochs reloaded, days rebuilt from their day file, and the ms it took), `day`, `late`, `clock`, `night` (each change to the reported night), `mark`, `write-fail`, `card-fail`, `gui-open`, `gui-close`, `stop` |
| `../SharedData/slumber/card` | The glance's card, every 5 minutes: liveness, and last night once there is one |
| `../SharedData/health/sleep/sessions.jsonl`, `now.json` | What other apps read; "Sharing sleep with other apps" |

Rows are buffered and written ten at a time, so a crash loses at most five
minutes; a USB unplug stops the app cleanly and flushes. A day file is about
0.5 MB.

**Reading a pull on a desk.** Copy the watch's `Apps/Slumber/` somewhere
outside this repository, then run
`python3 Slumber/Tools/analyse.py <that copy>`. It needs `c++` and Python 3
only. It compiles `Tools/nights.cpp` from the headers the watch runs, so the
rest, wrist, periods and card it prints are the watch's own. Around them it
prints recording health, the lifecycle log, `Nights.csv`, the night's edges
against the marks, each marked state's movement and heart rate, every
low-trust stretch against `kSensorFloorCount`, and battery drain.

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
  wakes a row. SleepLab's nights show the same back to August.
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

## What the second day settled

23.25 hours of schema 2 on 2026-10-10 and 11, worn with 27 marks, Slumber
alone on the watch.

- **The rate is still 50 Hz without SleepLab**: 1,500 samples and 156 batches
  in every row. Nothing another app asked for set it.
- **Watching TV does not look like sleep by movement alone.** Over 2.6 hours
  marked `Sitting, TV`, the longest rest run was 10.5 minutes, against the
  30-minute block a period needs; the gravity vector turned a median 24-35° an
  epoch, against 0.2° asleep. Heart rate did not separate them (54-61 bpm
  watching, 47-60 asleep). One evening, one wearer.
- **Heart rate can keep reading on a table.** Set down at 10:46, the watch
  reported 70-100 bpm with trust up to 2.9 for ten minutes before going
  silent; set down at 17:59 and 18:40, it was silent at once.
- **Recording all day cost about 1.4 mA, the whole watch**: `batt_mah` fell
  from 204 to 172.
- **The night matched the marks**: rest from 23:54 (`Lights out` 23:54) to
  09:05 (`Awake in bed` 09:07), once an arm on the sensor stopped splitting it
  (see "Finding a night").

## The screen

L1 and L2 pick what you are doing from now on; R1 logs it, and the label
turns green once the service has written it. Below the picker: the last mark
and how long ago, then rows written today and whether the last row had
accelerometer and heart-rate data. `WRITE FAILS` in amber means rows are not
reaching the volume.

The picker offers four marks: `Lights out` (trying to sleep), `Awake in bed`
(woken, still lying there), `Up and about` (any other waking time) and `Watch
off`. Each answers one question detection must get right: asleep against awake
and still, awake against asleep, and worn against off. Earlier builds also
offered `Out of bed`, `Sitting, TV`, `Sitting, other` and `Moving about`; their
codes stay reserved so old marks still read, and none of their rest runs reached
the 30 minutes a night needs except one `Moving about` stretch of 33.5, most
likely a mark left unchanged on sitting down.

Marks are states, not events: `Lights out` holds until the next mark. Press
`Watch off` before taking the watch off, and pick what you are doing when it
goes back on.

## The screens to come

The redesign is agreed but not built. It waits until the recording period
ends, so nothing on the watch changes while nights are being collected.

**Two rings, not a score.** A 0–100 sleep score is the most criticised thing
wrist trackers show, and wrist movement cannot score sleep stages: across six
devices against polysomnography, staging agreement was κ 0.21–0.53 and only
29–52% of wake was caught (Schyvens et al. 2025). What a wrist can see is
when, and for how long. So the morning screen has two goal rings:

- **Rest**, the night's rest against 7 h, the AASM/SRS adult recommendation
  of "7 or more hours" (Watson et al. 2015). A night past the goal laps the ring
  in the same colour, and only the second lap's leading end and the shadow under
  it mark the overlap, as Apple's rings do. A lighter second lap was tried on the
  watch and read as a heavy unfilled track.
- **Rhythm**, tonight's bedtime against the usual one: the median of up to the
  six nights before, once there are three. Within 30 minutes it is full, and it
  empties linearly to 2 h. Sleep regularity predicted mortality better than
  duration in about 61,000 UK Biobank wearers (Windred et al. 2023), and bedtime
  is the part of it a wearer controls. The 30-minute window and 2 h floor are
  design choices, not measurements.

`Software/Libs/Header/Sleep/Rhythm.hpp` holds both, and `Tests/` checks them.
Around the rings: a week of floating bars, bedtime to wake, and an evening
screen that says the usual bedtime.

**Colours chosen for a reflective panel.** The panel is a Sharp LS012B7DD06A
reflective memory LCD. Its datasheet gives 8.4% white reflectance, a contrast
ratio of about 25:1 and 18% NTSC gamut, so saturated colours land as pastels
and black is dark grey; slippypack's
`Docs/Investigations/2026-08-07-watch-cartography` turns that into a colour
model. Through that model, purple against cyan stays ΔE 39 apart, while purple
against magenta collapses to 21. So Rest is a violet→purple→magenta gradient,
codes (1,0,3)→(2,0,3)→(3,0,3), and Rhythm is cyan, (0,2,3)→(0,3,3). Purples
keep their colour only with green at 0. Only 3 of the 64 codes are darker than
L\* 40, so there is no dim purple for a track. Unfilled ring is a 3 px line
instead of a full-width band. Amber marks restless spells and nothing else.

**What the watch showed.** A test app drew the 64 colours, brightness ramps and
the mockups on the glass, and they were photographed:

- **The four levels per channel are roughly equal steps,** as the model
  assumes. In direct sun, normalised, level 1 measured 0.29–0.44 and level 2
  measured 0.60–0.83.
- **A 1-px checkerboard of two adjacent levels lands about halfway between
  them** (0.4–0.6), so dithered ring edges and gradients blend on the glass.
- **In direct sun, glare off the front glass cut black/white contrast to
  about 3–5:1.**
- **Under the backlight, the purple and cyan rings stayed clearly distinct to
  the eye;** phone photos blow out there and cannot judge it.

Rerun: photograph the same ramp page and normalise each row's four solid cells.

**Text.** SemiBold only, because Regular's 1.5 px stems go soft at 16–20 px
(`Docs/TEXT.md`). Main lines are 22–24 px, the size confirmed readable on the
glass, and 18 px is the floor. Every string is checked against the chord inside
the ring at its own rows. TextKit carries `SEMIBOLD_22_ASCII` and
`SEMIBOLD_24_ASCII` for this.

**Themes.** The colours are a setting, one of four. Ring geometry, thin
tracks, the same-colour second lap and the text rules are shared by all four.
The ΔE figures are the Rest and Rhythm fills compared through the panel model.

- **Nebula**, the default, as above: purple and cyan, ΔE 39.
- **Peak** uses the colours of UNA's own Peak face, which already puts two
  goal rings on the rim. Rest is cyan (0,3,3) and Rhythm is orange (3,2,0), on
  (1,1,1) tracks, ΔE 36. Restless spells are white, because orange already
  means Rhythm.
- **Smile** uses UNA's Smile face: teal (0,2,2) Rest and white Rhythm, ΔE 23.
  It is the calmest, with white text at 25:1.
- **Mono**, after UNA's Retro face, and the one theme that changes shape. Two
  greys cannot carry the rings: white against (2,2,2) is ΔE 8.5, under the 10
  needed to tell them apart at a glance. So both rings are white, and Rhythm is
  cut into 60 ticks, like a dial's minute track, while Rest stays solid.
  Restless spells are black notches in the white band. A dim (1,1,1) Rhythm
  measures ΔE 22. It was passed over because it is the dimmest ink, and glare
  flattens that first.

The SDK's setting fields have no pick-list. So the theme is a text field limited
by a pattern, as Barcode's format field is. The watch reads it once at launch,
and anything it cannot read means Nebula:

```json
{"id": "theme", "type": "string", "label": "Colours", "default": "Nebula",
 "maxLength": 6, "pattern": "(?:[Nn]ebula|[Pp]eak|[Ss]mile|[Mm]ono)?",
 "validationMessage": "Nebula, Peak, Smile or Mono."}
```

Rerun: the Peak, Smile and Mono colours come from the SDK's screenshots of the
faces, and the swatch test app has not photographed them. Photograph them
before shipping.
