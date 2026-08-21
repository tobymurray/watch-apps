# Recordings

Two recordings from this watch, kept because they cannot be taken again. They are
what `stillnessCountMax` and `activityCountMin` should be set from, and the pair
is what answers the question a single worn night cannot: whether the activity
count floor is the sensor's own in-band noise or a living wrist's micro-movement.

Both are `watching.csv` — the idle record, one row per 30 s recording epoch,
written while inside the bedtime window with no night open. Neither is a night:
no night opened in either, which is the finding rather than a fault in them.

| | `2026-08-19-worn` | `2026-08-20-table` | `2026-08-20-pillow` |
| --- | --- | --- | --- |
| What | Worn, a full night's sleep | On a hard table, empty room | Face up on a pillow on the floor |
| Local | 23:16 → 09:40 | 10:06 → 11:00 | 17:09 → 23:19 |
| Rows | 1249 | 107 | 741 |
| Schema | **2** | **2** | **3** |
| Build | 0.2.0 | 0.2.0 | 0.3.0 |

## Reading them

**The first two are schema 2; `2026-08-20-pillow` is schema 3.** Do not map
columns by position across them: schema 3 inserts `count_x,count_y,count_z` after
`samples`, so everything from `motion` rightward shifts by three. Each file
carries its own header line; use it. `night_report.py` handles both and prints
the schema each file declared.

**The table recording's first row is the set-down transient** — `count` 53647,
the watch being placed. Discard it. The remaining 106 rows are stationary. The
pillow recording's **last two rows** are the pick-up, at 20089 and 46826; its
first two are the set-down. The 735 rows between are undisturbed.

**The pillow recording carries a night file as well as the idle record.**
`20260820T171711.csv` is a session the segmenter opened on an empty pillow at
17:32, backdated to 17:17, and it is the point of keeping this recording — see
below. `watching.csv` is the superset and the one to measure from.

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

## What the pillow adds

**The floor is the accelerometer, not the surface** — ledger card E-4. Per-axis
across 735 undisturbed epochs: x 95, y 102, z 118, a ratio of 1.00 : 1.07 : 1.24.
Near-isotropic, which is broadband sensor noise; directed vibration through a
surface would not arrive on three axes within a quarter of each other. And the
two surfaces agree: a hard table gives a median of 187 counts per 30 s, a pillow
on a floor gives 183. Two percent apart across about as different a mechanical
coupling as can be arranged. The mild z excess is consistent with the out-of-plane
axis of a MEMS part being the noisy one, which is a property of the sensor rather
than of the room.

**And the worn gate accepts it as a worn night.** This is the more important half.
`touch_n` is 0 for all 735 epochs, so `worn_pct` sits at its stale fallback of 100
and clears `kMinWornPct`. The micro-movement half of the plausibility check works
exactly as designed — 0.0 % of pillow epochs exceed `kMicroMovementFloor`. But the
check is an **or**, and the pulse half carries it: the PPG reported a heart rate in
98.4 % of epochs, median 63.2 bpm, on a watch nobody was wearing. `WornGate`
returns `Worn`.

`kMinPlausiblePct`'s own comment says the pulse half "separated the two completely
on its own", measured on the table hour where 0 of 106 epochs carried a pulse.
That is true of a hard table and false of a pillow, and a pillow is where a watch
goes at night.

The discriminator that does survive is **trust, not presence**:

| | median `hr_trust_x10` | max | epochs ≥ 20 |
| --- | --- | --- | --- |
| Pillow | 8 | 12 | 0.0 % |
| Worn night | 30 | 30 | 93.8 % |

Nothing overlaps. The firmware was reporting that it did not believe the reading
and nothing was reading that field.

This recording is kept mainly as the negative fixture the worn gate has never had:
a known-unworn six-hour night, at schema 3, that the gate as written passes.
