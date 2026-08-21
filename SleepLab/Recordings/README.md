# Recordings

Two recordings from this watch, kept because they cannot be taken again. They are
what `stillnessCountMax` and `activityCountMin` should be set from, and the pair
is what answers the question a single worn night cannot: whether the activity
count floor is the sensor's own in-band noise or a living wrist's micro-movement.

Both are `watching.csv` — the idle record, one row per 30 s recording epoch,
written while inside the bedtime window with no night open. Neither is a night:
no night opened in either, which is the finding rather than a fault in them.

| | `2026-08-19-worn` | `2026-08-20-table` | `2026-08-20-pillow` | `2026-08-21-worn` |
| --- | --- | --- | --- | --- |
| What | Worn, no night opened | Hard table, empty room | Face up on a pillow on the floor | Worn, **a night that opened and scored** |
| Local | 23:16 → 09:40 | 10:06 → 11:00 | 17:09 → 23:19 | 23:38 → 09:24 |
| Rows | 1249 | 107 | 741 | 1173 idle + 910 night |
| Schema | **2** | **2** | **3** | **3** |
| Build | 0.2.0 | 0.2.0 | 0.3.0 | 0.3.0 |

## Reading them

**The first two are schema 2; the two from 20–21 August are schema 3.** Do not map
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


## What `2026-08-21-worn` adds

The first night this app ever opened, scored and reported: 455 min in bed, 386 min
asleep, 84 % efficiency, worn verdict `worn`, `flags=0x0`, 0 unscorable epochs. It
is kept as the positive fixture — the shape a good night has — and for three
things it says that the earlier recordings could not.

**The constants work.** Against `stillnessCountMax` 900 and `activityCountMin`
8000: 50.5 % of scoring epochs still, 6.8 % active, 42.7 % in the dead zone
between them. That is hysteresis doing its job rather than chattering on one
boundary.

**`onset_latency_min` is a tautology, not a measurement.** The summary reports 0,
and it cannot report anything else: a night opens only after 15 consecutive still
epochs and is backdated to the *start* of that run, so epoch 0 is always the first
epoch of a still stretch and the scorer will call it sleep. The first twenty
scoring epochs here are 747, 704, 747, 755, … all under 900. Either suppress the
field or measure it from `watching.csv`, which now covers the hours before the
night opened.

**The night started about 45 minutes late.** `watching.csv` has the evening, and
it settles well before the night does:

    00:15   count 13835   still  0/30   HR 75.5    last activity
    00:30   count  1945   still  7/30   HR 57.1    settling
    00:45   count   375   still 28/30   HR 55.0    settled
    01:30   ── night opens, backdated ──

Both movement and heart rate say 00:45. The cause is the *consecutive* rule: a
settling wrist is intermittently still (28/30, then 19/30, then 28/30) and one
epoch over threshold resets the run. So time in bed and total sleep are both
underestimates and efficiency is computed over the wrong window. A tolerance —
13 of 15 rather than 15 straight — would have caught it.

### Two things it confirms about the worn gate

`hr_trust_x10` separates worn from unworn completely, on a second and larger
sample: median 30 here with **97.7 %** of epochs at or above 20, against median 8
and **0.0 %** on the pillow. Nothing overlaps.

And `touch_n` was **0 for all 910 epochs of a genuinely worn night**. TOUCH_DETECT
never fires in either direction, so `worn_pct` reads 100 by stale fallback on a
wrist and on a pillow alike. It carries no information and belongs out of the
gate rather than re-tuned.

### Per-axis, worn against unworn

| | x | y | z |
| --- | --- | --- | --- |
| Pillow (noise) | 95 | 102 | **118** |
| Worn, quiet epochs | 168 | **209** | 203 |

Different shapes: noise is z-dominant, a wrist is y-dominant with x lowest.
Subtracting the floor in quadrature gives movement components of 139 / 182 / 165.
The sobering companion number is that this night's quietest scoring epoch was
**362**, below the pillow's 366 — at the bottom of the range the signal is at or
under the noise.
