# 2026-09-16 — five sessions, twos and threes, v0.6.103

The first labelled squash in this repository, and the first recordings whose
heart-rate sidecar carries readings: `HrKit`'s connect retry landed between
these and the 2026-09-13 match, whose `_hr.csv` was a header and nothing else.

88 minutes of IMU, 5,088 heart-rate samples, 79 markers.

## What the labels mean here, which is not what they are named

The wearer pressed at game boundaries, not rally boundaries — a press per rally
is 15-25 presses a game and nobody does that. Within that:

- **twos**: played with no rest labelled between points, so `RALLY` covers
  continuous play including the gaps between points.
- **threes**: `REST` pressed when the wearer lost the point and sat the next one
  out at the back of the court.

So `RALLY` here means *in play*, not *in a rally*, and the 1:0.17 play-to-rest
ratio across the five is an artefact of that convention rather than a squash
work:rest figure. Read the rally class as a mixture whose rest fraction is
unknown, and do not use these to calibrate a rally-level segmenter without
saying so.

## The opening marker was repaired

`Service.cpp` stamped each session's opening marker before the recording clock
was set, so its `t_ms` underflowed to ~2^32 and swallowed every labelled stretch
behind it. Three files were affected. The value is provably 0 — the marker is
written at the instant the recording begins — so it was rewritten to 0 here.

**`as-written/` holds those three files exactly as the watch produced them.**
Nothing else was touched; the samples, the heart rate and every other marker are
as recorded.

| file | as written | repaired to |
|---|---|---|
| `imu_20260916T164614_events.csv` | 4292276864 | 0 |
| `imu_20260916T170710_events.csv` | 4291020818 | 0 |
| `imu_20260916T173747_events.csv` | 4289183143 | 0 |

## A correction to this data's commit message

The commit that landed these files says "No gyroscope saturation in any epoch of
the five, against 3.9% of epochs in the 2026-09-13 match. Saturation is not a
constant of the sport." **That is wrong**, and the reasoning behind it was
wrong in a way worth naming.

It was read off A2's percentile table, where `gyro_sat` shows 0.0 from `min`
through `p95` for every state. But a quantity present in ~3% of epochs has a
p95 of exactly 0 by construction; the table was never evidence of absence.
Counting epochs directly gives the opposite answer:

| session | epochs | accel sat > 0 | gyro sat > 0 |
|---|---:|---:|---:|
| `20260913T122111` | 1800 | 13.8% | 3.9% |
| `20260916T164614` | 1252 | 14.0% | 3.4% |
| `20260916T170710` | 1815 | 10.5% | 2.6% |
| `20260916T173747` | 1986 | 10.2% | 2.9% |

So saturation **is** close to a constant of real squash on this watch — 10-14%
of epochs with an accelerometer axis railed and 2.6-3.9% with a gyroscope axis,
across four sessions on two days. The three 2026-09-03 wear tests have
essentially none, which is what makes it a signature of play rather than of
wearing the watch.

That matters for what gets built next: an impact hard enough to rail the sensor
in a tenth of all seconds is a strong, reproducible, discrete event, and
counting discrete events is a different and easier problem than segmenting
continuous states.
