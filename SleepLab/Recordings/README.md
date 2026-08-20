# Recordings

Two recordings from this watch, kept because they cannot be taken again. They are
what `stillnessCountMax` and `activityCountMin` should be set from, and the pair
is what answers the question a single worn night cannot: whether the activity
count floor is the sensor's own in-band noise or a living wrist's micro-movement.

Both are `watching.csv` — the idle record, one row per 30 s recording epoch,
written while inside the bedtime window with no night open. Neither is a night:
no night opened in either, which is the finding rather than a fault in them.

| | `2026-08-19-worn` | `2026-08-20-table` |
| --- | --- | --- |
| What | Worn, a full night's sleep | On a table, empty room, nobody present |
| Local | 23:16 → 09:40 | 10:06 → 11:00 |
| Rows | 1249 | 107 |
| Schema | **2** | **2** |
| Build | 0.2.0 | 0.2.0 |

## Reading them

**Both are schema 2 and the current schema is 3.** Do not map columns by
position against today's `kEpochHeader`: schema 3 inserts `count_x,count_y,count_z`
after `samples`, so everything from `motion` rightward shifts by three. Each file
carries its own header line; use it.

**The table recording's first row is the set-down transient** — `count` 53647,
the watch being placed. Discard it. The remaining 106 rows are stationary.

**Counts here are per 30 s recording epoch.** The segmenter compares its
thresholds against a 60 s *scoring* epoch, which is the sum of two of these, so
double them before reading anything against `stillnessCountMax`.

`sleeplab.log` covers both runs, cumulative: the first four lines are the worn
night, the last four the table hour. The app is stopped and relaunched by USB
being attached and detached, which is why there are two `launch` lines.

## What they say

Per 60 s scoring epoch:

| | min | p5 | median |
| --- | --- | --- | --- |
| Table | 356 | 360 | **374** |
| Worn night | **370** | 414 | 695 |

The stationary watch's median and the worn night's minimum are the same number.
The floor is the accelerometer, not the wearer — so raising `stillnessCountMax`
off it is a correction rather than a workaround. `stillnessCountMax` shipped at
60 and `activityCountMin` at 250, both below that floor, which is why no night
has ever opened and why every epoch reads as active.

The table floor is also tight — 167 to 202 counts per 30 s across 106 epochs —
which is the part that makes a per-device calibration worth building on.
