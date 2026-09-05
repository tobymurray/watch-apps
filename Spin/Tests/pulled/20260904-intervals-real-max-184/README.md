# The interval ride, pulled 2026-09-04

The evidence
[the heart-rate trend experiment](../../../Docs/HR-TREND-PROMPT.md#3-the-open-question-which-is-the-actual-job)
was blocked on: per-second heart rate through short, hard efforts, at a real
maximum, lapped at the efforts. What it found is in
[`RECOVERY-FIELD-RESULTS.md`](../../../Docs/RECOVERY-FIELD-RESULTS.md#2026-09-04--the-interval-ride-and-what-it-settles).

| File | What it is |
|---|---|
| `activity_20260904T223146.fit` | **the interval ride** — 45 min, 14 laps, 84–163 bpm |
| `activity_20260904T212623.fit` | a steady ride 65 minutes earlier, same day, same sensor, no laps — the control |
| `recovery.log` | every session on the watch to 2026-09-04, both of these included |
| `spin_sessions.json` | the shared log as it stood, `kept: 9` |

Both ran on `0.8.0-43-88d17f2` at `max_hr=184`, which their `start` lines record.
The two rides an hour apart are what make the comparison a within-day one: the
same body, sleep, caffeine and sensor placement, differing only in how the work
was arranged.

The lap structure, from the ride's own laps — Set A is laps 2–7, Set B 9–11,
Set C 13–14:

| Set | Structure | Laps |
|---|---|---|
| A | 6 × 20 s hard / 40 s easy | 2–7 |
| B | 3 × 60 s hard / 60 s easy | 9–11 |
| C | 2 × 4 min hard / 3 min easy | 13–14 |

```sh
Tools/hr_trend.py Spin/Tests/pulled/20260904-intervals-real-max-184/activity_20260904T223146.fit \
    --max-hr 184 --zones 5
```
