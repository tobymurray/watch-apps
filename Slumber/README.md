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

**`sessions.jsonl`**: one line per period, once SleepPeriods has settled it, usually
an hour after its last rest. Naps and table sessions are periods too, so filter on `worn`.
A night broken by an awakening is several lines; worn lines less than 3 h apart are one
sleep, as "Reporting a night" says.

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
| `hr_median_bpm`, `hr_low_bpm`, `hr_low_at` | Sleeping heart rate: the median, the lowest 10-minute mean and the middle of that window in UTC seconds; `null` with too few trusted readings. "Sleeping heart rate" defines them |
| `epoch_s`, `epochs` | One character per 30 s from `start`: `r` rest, `m` movement, `-` no data |

`epochs` is what the wrist saw before any smoothing. `m` is an epoch whose activity count
reached SleepPeriods' rest ceiling, or had 10 or more steps. Neither letter is a claim
about being awake: a wearer lying awake and still is `r`, and the wrist cannot tell sleep
stages apart (see "The screens to come"). Over the 76 SleepLab sessions replayed in
`Tests/`, the longest period was 1,683 epochs, so a line stays under about 2 KB.

**`now.json`**: rewritten with the card, every 5 minutes. Schema 2 dropped schema 1's
`week`, `tonight.own_nights` and the `heart` fields that compared a night with the
wearer's usual; "What was cut" says why.

| Field | Meaning |
| --- | --- |
| `written`, `heartbeat_s`, `utc_off_min` | When, how often, and the offset |
| `asleep_since` | Start of the worn period still running, else `null` |
| `last_night` | `{start, end, rest_min, worn, settled}`, the longest period of the night the card shows, else `null` |
| `last_sleep` | `{start, end, rest_min, periods, settled}`, the whole of the night the card shows: the first period's start, the last one's end, and their rest summed; `null` unless that night was worn |
| `usual` | `{bed_min, wake_min, nights}`: medians, in minutes past local midnight, of the main nights logged, at most six; `null` until seven, or while times vary ("Irregular hours") |
| `rest_goal_min`, `rest_fill` | 420, and last night's rest against it in per mille; past 1000 is a second lap |
| `rhythm_fill`, `rhythm_offset_min` | Last night's onset against the usual of the nights before it; `+` is later |
| `tonight` | `{weekday, bed_min, wake_min, nights}`: the night ahead, which is `usual` on the weekday it begins (0 is Sunday); `null` whenever `usual` is |
| `heart` | `{median_bpm, low_bpm, low_at}` for last night, as "Sleeping heart rate" defines them; `null` unless last night was worn with enough trusted readings |

`asleep_since` comes late. A period exists only once it holds 30 minutes of rest, so it
appears about half an hour after falling asleep, and an app silencing notifications at
bedtime cannot do better from this file. A day's main night is the longest sleep
starting between noon and noon, so a nap does not move the usual, and a night split by
an awakening counts whole.

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
- **A period is final** once nothing later can join it: 60 minutes have passed
  after it, and either movement, a gap or a change of wrist has been seen at
  least 60 minutes after it, or a later period proves it. A period two on
  always does; the next one does once its wrist reads on or off from an epoch
  whose 5.5-minute window is complete. A 30-minute block takes 30 minutes to
  appear, so an hour alone is not enough: a still evening ending at 22:45 with
  sleep from 23:44 would settle at 23:45, be logged, and leave the night that
  joins it at 00:14 never logged. Nor is any later period: back in bed at 07:10
  lying on the watch, the rest reads neither on nor off the wrist until trust
  returns, and then joins the night; and an epoch stamped a second late can
  give a run a wrist the next epochs take back. The cost is a night after which
  the wrist stays unknown: in a synthetic replay, ten hours at count 260 and
  trust 1.0 after waking logged the night 609 minutes after it ended, not 64.

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
36 h and puts one sleep on the card:

- **The latest sleep that ended in the last 20 h, unless it is under half as
  long as the longest one.** A sleep is the worn periods less than 3 h apart,
  and the card shows all of it: from the first period's start to the last
  one's end, with the rest of each summed. SleepLab's 2026-08-25 night split at
  a 65-minute awakening into 5.3 h and 3.2 h; the split nights above were
  64-108 minutes apart. A nap the next afternoon is under half the night, so it
  does not replace it. A shorter night after a long one does: a 9 h night
  ending at 09:00 and a 5 h one ending at 04:00 show the 5 h one.
- **Only UTC durations choose**, so crossing time zones, or a watch whose clock
  loses its zone, cannot bring back an older night. The card shows a logged
  night's times in the offset it was logged with, the wearer's clock when they
  woke, so a flight later that day does not move them.
- **Until SleepPeriods settles it**, usually an hour after its last rest, the
  card says `so far`.
- **Only a table, and no worn period**, reports `No night` / `watch was off`.
  An uncertain wrist is never reported.
- **20 h after it ended**, the night expires and the card says `No night yet`.

The 36 h live in RAM and in `Epochs/`, and a restart reloads them before its
first card, so plugging in to charge in the morning keeps the night on the
card. A day missing from `Epochs/`, as after an upgrade from a build without
it, is rebuilt from its day file and written back. `Software/Libs/Header/Sleep/LastNight.hpp` makes the choice;
`SLUMBER_DAYS_DIR` replays a day file and prints what the card would say.

**The usual learns the same sleep.** The card, the week and the usual once took a
sleep's longest period, so one awakening halved the night everywhere. `Tools/splits.cpp`
runs synthetic broken nights through the headers and prints the Now column; Before is the
same nights through those of `944cacc`:

| Night | Before | Now |
| --- | --- | --- |
| Up 61 min at 03:00, asleep again to 07:30 | `4h00`, `23:00-03:00`, learned 23:00-03:00 | `7h29`, `23:00-07:30`, learned 23:00-07:30 |
| Up 150 min at 03:00 | `4h00`, `23:00-03:00` | `6h00`, `23:00-07:30` |
| Up 200 min at 03:00 | `4h00`, `23:00-03:00` | the same: two sleeps |
| Heart rate silent 30 min from 03:00, to 07:00 | `4h01`, `23:00-03:01` | `7h30`, `23:00-07:00` |
| Heart rate silent 90 min from 03:00 | `4h01`, `23:00-03:01` | `6h30`, `23:00-07:00` |
| A 45-minute doze ending 60, 105 or 170 min before bed at 23:30 | `7h30`, `23:30-07:00` | `8h15`, from the doze's start |
| The same doze ending 190 min before bed | `7h30`, `23:30-07:00` | the same |

The doze is the cost: rest within 3 h of bed is one sleep with it, so the bedtime moves
to the doze. The 3 h is the card's own rule from before, kept so the card, the week and
the usual agree; real nights with dozes would say whether a shorter gap or a minimum
length for the first period serves better. The silent heart rate still loses the minutes
it was silent, because the wrist reads off for them (see "Finding a night").

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

## Irregular hours

A usual is only worth showing if the nights keep one. Three nights at 22:30 and three
day sleeps at 08:30 once had a median of 03:30, a bedtime nobody keeps, and every switch
of shift read as `10h late`. So Slumber says when there is none:

- **Times are taken round the clock.** The usual is the median after cutting the clock
  at its widest empty stretch, so bedtimes of 11:50 and 12:10 are 20 minutes apart, not
  23 h 40. How late or early a night was is the shorter way round, never past 12 h.
- **A usual needs seven nights, and two thirds of the last 14 within 4 h of it.** The
  usual itself is still the median of the last six. Otherwise `now.json` carries `null`
  and the screens say `times vary`.

`Tools/irregular.cpp` runs synthetic sleepers through these headers, 20 seeds each,
evenings 21-120. "Usual" is the share of evenings with one; "kept" the share of those
whose next bedtime fell within 2 h of it.

| Schedule | Usual before | Kept before | Usual now | Kept now |
| --- | --- | --- | --- | --- |
| fixed 23:00 | 100% | 100% | 100% | 100% |
| fixed, Fri/Sat 01:30 | 100% | 71% | 100% | 71% |
| fixed, 2 nights out a week at 03:00 | 100% | 66% | 98% | 66% |
| day sleeper 08:30 | 100% | 100% | 100% | 100% |
| bedtime 11:40-12:20 | 100% | 65% | 100% | 100% |
| Mon-Thu day sleeps, weekends 23:00 | 100% | 0% | 0% | - |
| 3-on-3-off, 4-on-4-off | 100% | 0% | 0% | - |
| alternate weeks of nights and days | 100% | 43% | 0% | - |
| random | 100% | 15% | 1% | 7% |

A permanent switch from 23:00 to 08:30 shows the old usual for two nights, then `times
vary`, then the new usual from night ten. Tonight is the usual, so it follows the same
nights.

The 4 h is the radius that separated these groups: at 3 h a sleeper out until 03:00
Thursday to Saturday kept a usual on only 56% of evenings, and at 5 h the weekly rota
got one on 55%. The radius, two thirds and 14 nights are synthetic. Your own nights would
show how often a regular sleeper is told `times vary`, and a rota worker's whether a
rota ever gets a usual. A setting or a question on the watch was considered and not
built: the nights already answer it, and a wrong answer here costs `times vary` for a
while rather than a confident wrong time.

## What was cut

These reached no screen, and no app in this repository reads `now.json`.

- **A usual for each weekday**, shown as Tonight while it lay within 4 h of the usual,
  with the weekdays that kept their own hours and Wittmann's social jetlag in `now.json`.
  Its thresholds were chosen, not measured, from a recording period holding two of each
  weekday at most. `Tools/irregular.cpp` at `4283b78`, extended to score Tonight against
  the bedtime that followed, put the weekday Tonight within 30 minutes on 83% of evenings
  for a sleeper out until 01:30 on Fridays and Saturdays, against 67% for the usual; for
  the other regular schedules the two were within 4 points. Worth bringing back once real
  nights show weekday effects larger than the spread inside a weekday.
- **A night's heart rate against the usual**, `notable` at 4 bpm; "Sleeping heart rate"
  has the measurement against it.
- **Four colour themes** as a setting: Peak, Smile and Mono beside Nebula, whose
  colours were never photographed on the glass. Nebula is the one theme.

## Sleeping heart rate

Each worn night carries its sleeping heart rate, in `sessions.jsonl` and, for last night,
`now.json`.

- **Only trusted readings count**, those at `SleepPeriods::kWornTrustX10` and between 30
  and 149 bpm. A night with fewer than half its epochs trusted has no heart rate.
- **The median** is to the nearest bpm. **The lowest** is the lowest mean over 10
  minutes with at least 15 of its 20 epochs trusted, so one low reading cannot set it.

Nothing judges a night against the wearer's own. Over 18 of one wearer's nights (SleepLab
and Slumber), a night's median strayed from the median of up to 14 nights before it by
1.6 bpm at the median, 4.1 at p90 and 4.8 at most, and the lowest strayed up to 6.9. A
4 bpm flag would mark about one ordinary night in eight. `Tools/hr_noise.py` reruns the
measurement over any copied-off nights; other wearers' nights would say whether any
threshold separates a night worth a look from noise.

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

## The screens

Three pages, in order: last night, the past week, and tonight. L2 goes to the
next and L1 to the one before, wrapping both ways, and R2 leaves from any of
them. That is the SDK's own reading of the buttons (`EventButton::Id`: L1 up,
L2 down, R1 select, R2 back) and what the stock apps' faces do: Cycling's
`TrackView` pages L1/L2 with wrap-around. R1 only opens the marks picker. Dots
under each page say which one is up.

Slumber opens on last night, except from 4 h before tonight's usual bedtime to
2 h after it, when it opens on tonight; a night under way, or a watch with no
clock, keeps it on last night. Both windows are design choices, not
measurements.

Last night and Tonight are drawn on one 24-hour dial (`dial.rs`): midnight at
the top, a tick each hour and a longer one every six, numbered `00`, `06` and
`18`. Noon has no numeral because the page dots sit there, as far below the
middle as `00` is above it, so the screen balances. Purple is what you did and
cyan is your usual. A night is a band on the dial, cut at every hour it crosses,
so its length can be counted in blocks.

**Last night** is the night as a purple band, with the usual it was scored
against as a thin cyan arc just inside it, so a late bedtime is the band
starting clockwise of the arc. Inside: the night's rest, its times and its
bedtime against the usual, such as `7h42`, `23:29-07:30`, `38m late`. The band
spans the whole sleep, awakenings included, so it can be longer than the rest it
reports. Before
there is a night it says `No night yet` and shows the usual arc once seven
nights give one; a night on a table says `watch was off`. The times are in the
offset the night was logged with.

**The past week** is the main night of each of the seven evenings before
tonight, a floating bar from bedtime to wake in the Rest colours, under dashed
cyan lines at the usual bedtime and wake. Each bar is labelled with the weekday
it began on, the newest in white. Above them, `4 of 6 on time` counts the
bedtimes within 30 minutes of the usual, the window the Rhythm ring is full
for. The plot starts its day after the widest stretch of the clock with no
bedtime, so a day sleeper's 11:50 and 12:10 sit 20 minutes apart, and it runs
in whole hours, at least six, so a single night does not fill it.

**Tonight** is the usual night (see "Irregular hours") as a cyan band on the dial, and a
white dot for now. Inside: the usual bedtime, the usual wake, and how far
bedtime is, such as `in 2h10`, `about now` or `45m past`. Before there is a
usual it says how many more nights it needs, and when the nights keep no one
time it says `no usual` and `times vary`, and the app no longer opens on it.

`Software/Apps/CustomGUI/rust/src/` draws them: `night.rs`, `week.rs` and
`tonight.rs`. The service sends the night, the week and tonight with every
status, so the screens hold no history of their own. Their tests check that
every line fits inside the dial or the glass at the rows it is drawn on, and
`SLUMBER_PREVIEW_DIR=<dir> cargo test --features std previews` writes each
state as a PNG.

**The marks picker is behind R1 R1 R1**, pressed with no more than 1.5 s
between presses on any page; R2 goes back to last night. R1 does nothing else
on the pages, so paging with L1 and L2 never opens it. Long presses are not used: a hold
never reached Spin's screens.

In the picker, L1 and L2 pick what you are doing from now on; R1 logs it, and the label
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

The week of bars and the evening screen are built (see "The screens"). The two
morning rings below were built and then replaced by the dial: on the watch they
were hard to read, because a Rhythm ring is full when on time and empties
either way, so it cannot say early from late. Rest and Rhythm remain the two
things the screen reports, as the night's length and its bedtime against the
usual.

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
  six nights before, once there are seven. Within 30 minutes it is full, and it
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
instead of a full-width band. Amber marks `WRITE FAILS` and nothing else.

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
the ring at its own rows. `POPPINS_SEMIBOLD_24_ASCII` is Slumber's own face,
listed in the renderer's `faces.json` and generated into `src/atlas.rs`.
