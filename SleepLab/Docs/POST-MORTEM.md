# Can a night be diagnosed after the fact?

The question this document answers is not "is the app correct". It is: **the night
behaved oddly and all you have is what it left on the volume — can you tell what
happened, or do you have to spend another night?**

That third failure mode is the one people under-weight. A crash is cheap: you see
it and fix it. A wrong number is expensive but at least it is one bug. A night that
ran correctly and left you unable to answer the question you have is a night you
have to run again, and the eight hours are gone either way.

Every row below names the artifact, the column and the value to look for. Where the
answer is "you could not tell", that is recorded as a gap with what it would cost
to close, not smoothed over.

## What a night leaves behind

| Artifact | When | What it is |
| --- | --- | --- |
| `Nights/<start>.csv` | one row per 30 s, all night | the record. 33 columns, ~110 KB |
| `Nights/<start>.json` | once, when the night **closes** | the summary, the method, the constants, the delivered rate |
| `Nights/index.csv` | once per completed night | the history, and the only thing the baseline is built from |
| `night_state.txt` | rewritten every 30 s, removed at close | present ⇒ a night was in progress when the app stopped |
| `Nights/watching.csv` | one row per 30 s while idle **inside** the window | what the segmenter was looking at before a night opened, or instead of one. Same format as a night, so the same tooling reads it. ~9 KB |
| `Debug/sleeplab.log` | ~20 lines per launch | why a night did not happen. New; see below |
| the report screen | on demand | the honesty line, the numbers or their absence |

`<start>` is `YYYYMMDDTHHMMSS` **local**, from the session's own start. A night is
named for the evening it began.

## The matrix

Verdict column: **YES** — an artifact says so unambiguously. **PARTLY** — you can
infer it, with work or with ambiguity. **NO** — you could not tell.

| # | What went wrong overnight | Where you look | Verdict |
| --- | --- | --- | --- |
| 1 | **Delivery stopped partway** | `csv`: `samples` drops to 0 and `hr_samples` with it, while `uptime_ms` keeps advancing on the 30 s grid. `json`: `provenance.data_gap` true, `acc_samples_min` 0. Screen: `INTERRUPTED - sensor gap` | **YES** |
| 2 | **Delivery degraded rather than stopping** | `csv`: `samples` × 1000 / `span_ms` is the delivered rate, per epoch. `json`: `provenance.acc_hz_x10` for the night's median, `acc_samples_min` for its worst epoch. Compare with the previous night's `acc_hz_x10` | **YES**, and this is new. Before it, a tenth of the samples passed both thin-epoch guards silently while costing 26 % of every count — see the caveat below |
| 3 | **The app restarted** | `json`: `provenance.resumed` true. `log`: `resume present=1 … app-restarted`, with `gap=Nmin`. `csv`: a jump in `uptime_ms` larger than one epoch with `wall_utc` moving by the same amount. Screen: `INTERRUPTED - app restarted` | **YES** |
| 4 | **The device rebooted** | `log`: `resume … device-rebooted`. `csv`: `uptime_ms` **goes backwards**, which nothing else does. `step_delta` becomes `-1` for one epoch, because the step counter reset | **YES** |
| 5 | **The clock changed** | `json`: `provenance.clock_jump` true. `csv`: `wall_utc` moves further than `uptime_ms` says it should between adjacent rows. Screen: `INTERRUPTED - clock changed` | **YES** |
| 6 | **The charger was connected** | `json`: `provenance.charging` true. `csv`: `charging` column 1. Screen: `INTERRUPTED - was charging`, first line. Plus a `stop`/`launch` pair in the `log` | **YES** |
| 7 | **Storage filled** | `json`: `provenance.write_failed` true. Screen: `INCOMPLETE - could not write`, ahead of everything else. `log`: `fail epoch write refused … after N epochs` — which tells you *where* the record stops. `csv`: shorter than `json`'s `epochs` | **PARTLY.** Where the volume is genuinely full the log cannot be written either, because it is on that volume. The screen and the CSV-vs-`epochs` mismatch are what cover it; the log covers the scoped case (one directory, one failing handle) |
| 8 | **The worn sensor went silent** | `csv`: `worn_pct` holds one value with `worn_edges` 0 for the whole night — the recorder carries the last state forward, deliberately. `json`: `sleep.reported` false, `provenance.worn` `uncertain`. Screen: `UNCONFIRMED - no sleep data` | **YES**, and this is the distinction ledger row S12 was about: total silence is `Uncertain`, not `not-worn` |
| 9 | **The worn sensor flickered** | `csv`: `worn_edges` per row. Sum it and divide by the night's hours. A worn *fraction* cannot tell a flicker from a removal and only one of those means tighten the strap | **YES** |
| 10 | **Heart rate produced nothing** | `csv`: `hr_samples` 0 all night, `hr_mean_x10` `-1`. `json`: `heart_rate.epochs` 0, `method.hr_mode` says whether it was even asked for. Screen: `heart rate was off - cannot confirm it was worn` | **YES** |
| 11 | **Heart rate produced nonsense** | `csv`: `hr_mean_x10` and `hr_min_x10` per epoch, and `hr_source` — 1 optical, 2 strap, 3 both, 0 none | **PARTLY.** The *values* are there and their provenance is there. The kernel's own **trust** value is not recorded, so "40 bpm at low trust" and "40 bpm at high trust" are indistinguishable. See gap G1 |
| 12 | **The segmenter opened a night late** | `watching.csv`: every idle minute inside the window, with its `count` and `worn_pct` — so the epochs it *declined* to open on are readable, which is what says whether it was late and why. Plus `log`: `open … backdated=15min recovered=15`, and `json`: `sleep.onset_latency_min` | **YES** |
| 13 | **The segmenter opened a night early** | `csv`: the first rows' `count` and `worn_pct` — a night opened on a still, worn quarter hour that was not sleep looks like fifteen quiet epochs followed by activity. `index.csv`: a spurious short night | **YES** |
| 14 | **The segmenter closed a night early** | `csv`: the file simply ends. The epoch before the end carries the reason — `step_delta` ≥ 20 (walked) or `count` above the activity floor for ten consecutive scoring epochs | **YES** |
| 15 | **The segmenter never opened a night** | `Debug/sleeplab.log` says the app ran and under what settings — a `launch` line with no `open` line. **`Nights/watching.csv` says which precondition failed on which minute**: `count` against `stillnessCountMax`, `worn_pct` against the worn floor, one row per 30 s. If every row's `count` sits above 60, the stillness ceiling is below the sensor's noise floor and the file is the distribution that fixes it | **YES** |
| 16 | **The scorer disagreed with the movement index** | `json`: `sleep.total_sleep_min` against `sleep.movement_index_pct` and `sleep.still_in_bed_min`. These are independent by construction: the movement index does not use the scorer. A large disagreement is the calibration of `kCountScale` being wrong, and is what `Tools/night_report.py thresholds` is for | **YES** |
| 17 | **The alarm fired at the wrong time** | `log`: `alarm fired why=deadline\|smart-window epoch=N local_min=M`. The wall clock at the moment it fired is the thing a wearer disputes | **YES** |
| 18 | **The alarm did not fire** | `log`: no `alarm` line, with `open`/`close` lines present. Distinguishes "never requested" from "requested and you slept through it", which used to be the same observation | **PARTLY.** It tells you the app never raised it. It cannot tell you the kernel swallowed one it did raise — that is ledger row T1, and only sitting and watching settles it |
| 19 | **The loop woke late and lost minutes** | `csv`: `span_ms` far above 30 000 on one row. `json`: `provenance.data_gap`. `log`: nothing yet | **YES** for the fact; the lost minutes are counted into time in bed rather than silently shortening it, which is the part that used to be wrong |
| 20 | **Two nights were spliced into one file** | `csv`: `wall_utc` jumping by hours mid-file with `uptime_ms` doing the same. `json`: `provenance.resumed` | **YES** |
| 21 | **The night was cut at the engine's bound** | `json`: `provenance.truncated`. Screen: `INTERRUPTED - too long, cut` | **YES** |
| 22 | **A number on the screen disagrees with the file** | Both carry the same fields; the summary is written from the same struct the report is published from | **YES** |
| 23 | **An old night cannot be re-scored after a threshold moves** | `json`: `method.constants` carries `kCountScale`, P, the threshold, both worn floors, both movement floors and the onset run — the numbers that scored *that* night. Plus `method.app_version` | **PARTLY.** The constants are recorded, so re-scoring is now *possible*; no tool does it. See gap G4 |

## The gaps, with what closing them costs

**G1 — heart-rate trust is not recorded.** `HEART_RATE` carries a trust value and
`HEART_RATE_EX` carries one per source; the recorder reads neither. So a night of
implausible heart rates cannot be separated into "the sensor was struggling" and
"the wearer's heart rate was genuinely that". Cost: one `int16_t` in `Epoch`, one
CSV column, an accumulator field. This matters most for the worn gate, half of
which is "a heart rate was present" — and a present-but-untrusted reading is
exactly what a watch on a warm surface produces.

**G2 — the idle minutes were discarded. Closed.** The pre-roll ring held thirty
minutes of epochs while no night was open and threw them away, so two questions had
no answer on the volume: "why did no night open" and "why did it open at 23:40
rather than 23:20".

The second is the one that mattered, and the reason is not diagnostic curiosity.
`SegmenterConfig::stillnessCountMax` is a guess at about **2 mg** of band-limited
wrist movement — the same order as a wrist IMU's own in-band noise — and if the
noise is above it then **no night ever opens**, which from the outside is
indistinguishable from a wearer who did not go to bed. There was no way to measure
that in advance, because the probe records no activity counts (S13) and SleepLab
recorded nothing at all until a night opened.

`Nights/watching.csv` now carries one row per 30 s while the segmenter is idle
**inside** the bedtime window, in the same format as a night's own log so
`night_report.py thresholds` reads it unchanged:

```sh
night_report.py thresholds --worn ./nights --table ./nights/watching.csv
```

It is restarted on entering the window, so it holds one window rather than every
evening since install, and capped at 1 MB in case the clock never reads as
in-window. A 14-hour window with no night is ~1 700 rows, ~200 KB; a night that
opens after twenty minutes leaves ~9 KB of run-up.

What this changes about a first night: **there is no longer an outcome that wastes
one.** Either a night opens, and the run-up is on the volume too, or it does not,
and the whole window's counts *are* the noise-floor measurement the threshold should
have been set from in the first place. The threshold gets moved once, from a
distribution, rather than being guessed at twice.

It also retires the last thing only the probe could do — see the section below.

**G3 — the alarm's trace stops at the app boundary. Closed, as far as it can be.**
`playAlarm()` now writes one `log` line with the wall clock, the epoch whose
verdict fired it, and which of the two paths took it. So "it did not go off" and
"it went off and you slept through it" are no longer the same observation, and
neither are "at the deadline" and "forty minutes early on a wake epoch".

What remains outside reach: whether the kernel *delivered* what the app requested.
Ledger row T1 — whether mute silences an app-requested alert — cannot be settled
from any file, because the app's own record ends at `send()`. It needs somebody
awake with the watch muted and `alarm_at` two minutes ahead. That is a five-minute
experiment rather than a night, and it should be done before the alarm is trusted
on a weekday.

**G4 — nothing re-scores a night.** `method.constants` makes it possible; the
counts are in the CSV; there is no `night_report.py rescore`. The calibration
sweep the whole app is waiting on — sweep `kCountScale` against ten diary nights
and report the mean signed error on onset and final wake at each value — is
precisely a re-scoring loop. Cost: one subcommand, maybe 120 lines of Python, and
it turns ten recorded nights into an unlimited number of experiments. **This is the
highest-value item in this document**, because it is the difference between one
calibration attempt and as many as you like.

**G5 — the epoch CSV's provenance. Closed.** The settings, the build, the delivered
rate and the constants were all in the summary JSON, which is written only when a
night *closes* — so a night the USB cable ended left a record that could not say
what produced it, and that is the night most likely to need explaining. The CSV
header now carries a comment line with the build, the window, the heart-rate mode
and both epoch lengths: ~120 bytes once, and a `#` line every existing reader
already skips. `Debug/sleeplab.log` carries the same and more, so the two
corroborate each other.

**G6 — per-sensor delivery is not recorded.** The epoch row has accelerometer
`samples` and `hr_samples`. It does not say whether touch, motion, activity
recognition or steps delivered anything at all, and it records battery percent
rather than current. The probe records all of it — which is why the probe's nights
are still needed rather than optional. See the next section: the recommendation is
that these columns *move*, and until they do, both sets of nights have to be run.

## Should the probe and SleepLab stay separate?

They cannot be installed together: both autostart and both claim the accelerometer
and heart rate (ledger row S8). So every question that needs the probe's diagnostic
depth *and* real scoring costs two nights instead of one.

**The separation should eventually be collapsed, but not yet, and not for the
reason this section first gave.**

Each app records something the other cannot, and until that is fixed both sets of
nights are needed:

**Update: the last thing only the probe could do is gone.** The nightstand night
was the holdout — a session only opens when the epoch is *worn*, so a watch the
touch sensor correctly reports as off the wrist produced no night, no CSV and no
counts. `watching.csv` records those minutes whether or not a session opens, so the
table night is now a SleepLab night. What remains below is battery telemetry and
loop counters.

| Only the probe records | Only SleepLab records |
| --- | --- |
| `batt_mv`, `batt_ma_x10`, `batt_avg_ma_x10`, `batt_mah` — so **S2** (what continuous HR costs) is precise only here; SleepLab has battery *percent* | `count`, `peak` — so the four movement thresholds' distributions exist only here |
| `wakes`, `msgs` — **S10** in its entirety; SleepLab records nothing about the loop | the scored night: verdicts, the gate, the summary, the index, the baseline |
| per-sensor delivery counts: `touch_n`, `motion_n`, `ar_*`, `hrex_opt/ext/unk`, `beat_n`, `ppg_n`, `spo2_n` | `worn_edges` — the flicker rate S7 needs — and `hr_source` per epoch for S8 |

**One correction to the first version of this section**, which claimed the probe
"cannot answer the questions the ledger assigns to it" and named four constants.
Only `WornGate::kMicroMovementFloor`'s TODO actually named the probe (with
`kMinPlausiblePct` inheriting it); the other two said "a diary-validated recording"
and named nothing. And `ROLLOUT.md` phases 3 and 4 *already* took those
distributions from SleepLab nights. So the plan was right and one comment was wrong.
Ledger rows S13 and S16.

The merge direction is still clear, because the asymmetry is: SleepLab cannot be
replaced by the probe for anything, and the probe's unique columns are per-sensor
counters and battery telemetry that are mechanical to add.

**Recommendation: add the probe's delivery columns to SleepLab's epoch row behind
a `"diagnostics": "on"` setting, and keep the probe only for the one job that is
genuinely its own — the sixty-second screen check.** That screen is what caught
S12 and S4 in two minutes, and it is a *screen*, not a log: it answers "will
tonight record at all" before the night rather than after it. `Debug/sleeplab.log`
now carries the same resolved-driver block, so even that job is duplicated — but a
line in a file you read in the morning is not the same instrument as a block on a
screen you read before bed.

### The arithmetic

*Storage.* Twelve extra columns at ~5 bytes and a comma is ~72 bytes a row. At
1 920 rows a night that is 138 KB a night against today's 46 KB — three times, and
17 MB a decade becomes 51 MB, on a volume `MapManager` CRC-verified 160.5 MiB of map
packs on. Not a constraint.

*Power.* The cost of a row is dominated by the open-seek-write-flush-close cycle,
and there are 1 920 of those either way. Three times the bytes in the same number
of transactions is not three times the energy; it is very nearly none of it.
Unmeasured, and the probe's `batt_avg_ma_x10` column is what would measure it.

*Complexity.* SleepLab's `Accum` has ten fields; the probe's `MinuteRow` has about
forty. Adding the delivery counters is additive and touches nothing that scores.

*Nights bought.* The plan as it stands is three probe nights (worn, HR-off, table)
plus a SleepLab worn night, a SleepLab table night and nine more diary nights —
**fourteen**, every one of which answers something, once the TODO in S13 is
corrected. Merged, probe nights 1–3 collapse into the SleepLab nights: a worn night
with diagnostics on, an HR-off night, a table night, and nine diary nights —
**twelve**. The saving is two nights.

That is a smaller prize than this section first claimed, because the first version
counted two nights as *wasted* rather than as merely separate. They are not wasted;
they answer S2 and S10, which nothing else here can. Two nights is still two nights,
and the diagnostic columns are worth having for their own sake — a night that can
report its own per-sensor delivery is a night that can be diagnosed without a second
one, which is what this whole document is about.

## A caveat this document should carry

Row 2 is answerable now, and what it will tell you is worse than it looks. Measured
in host tests against a 0.5 Hz sinusoid, counts per 60 s scoring epoch are 291 at
96 Hz, 286 at 48, 277 at 25, 259 at 12.5 and 212 at 4.8. So a delivered rate that
halves costs about 9 % of every count in the night, and a tenth of the rate costs
26 % — in the direction that reads as a quieter night, which reads as more sleep.

`EpochCounter` is rate-*insensitive* rather than rate-independent, and the ledger's
"independence across a 4× span" is true only in the upper half of the range. The
consequence for diagnosis is the one that matters: **two nights are only comparable
if their `acc_hz_x10` values are close**, and that is now in every summary so it can
be checked rather than assumed.
