# BatteryLab — what the watch's battery readings actually say

BatteryLab is a probe, as SleepLab was before Slumber. It logs every battery
message the firmware sends, as it arrives, so that a battery app can be built on
what the gauge does rather than on what it is assumed to do. Its first reader is
meant to be a watch face that shows a percentage the wearer can trust.

It is one `Utility` app that starts at boot. Its screen shows the latest value of
every battery field, so the one number nobody has seen yet, the design capacity,
can be read off the watch the moment it is installed.

## What Slumber's day files already show

Slumber logs the battery every 30 s. `Tools/slumber_battery.py` measures six of
its day files, from 2026-10-10 10:32 to 2026-10-15 11:41: 14,384 rows on one
watch, firmware 1.5.0. The day files hold sleep and wear times, so they are not in
the repository; the numbers below are what the script printed. Run it on a copy
of `Apps/Slumber/Days/` to check them.

Slumber stores the percentage ×10 and the remaining charge as whole mAh, both
truncated, so its file cannot show anything finer than that.

### The percentage is clamped at 100 for the top quarter of the charge

| Remaining | Percentage |
| --- | --- |
| 204 down to 148 mAh | 99.9 to 100 |
| Below 148 mAh | remaining / 147.6 mAh (145.7 to 149.1 over 6,442 rows) |

A full charge counts 204 mAh. The percentage stays at 100 for the first 56 mAh,
about 28 % of the charge and a day and a half at this watch's drain, then follows
the remaining charge against about 148 mAh.

It is not a pure function of the remaining charge even inside the clamp: on 10-12
it read 98.9 then 99.5 at 177 mAh for four minutes, and 98.9 at 167 mAh. Its tenth
digit takes 0, 3, 5, 7 and 9, so it is not a whole percentage either.

**To overturn:** a row with the percentage below 99 and remaining above 160 mAh
for more than a few minutes, or a ratio below 145 or above 150 under 148 mAh.

### A restart that finds more charge was a real charge

Every rise in remaining charge happened across a gap in the log. Besides the six
below, three gaps gained 1 or 2 mAh.

| Before | After | Gap | Change |
| --- | --- | --- | --- |
| 172 mAh, 4,213 mV | 198 mAh, 4,347 mV | 17 min | +26 |
| 192 mAh, 4,332 mV | 204 mAh, 4,379 mV | 21 min | +12 |
| 175 mAh, 4,240 mV | 179 mAh, 4,263 mV | 12 min | +4 |
| 179 mAh, 4,262 mV | 185 mAh, 4,302 mV | 4 min | +6 |
| 138 mAh, 4,025 mV | 149 mAh, 4,071 mV | 4 min | +11 |
| 118 mAh, 3,920 mV | 130 mAh, 3,997 mV | 6 min | +12 |

The voltage rose with the count each time and, once it had relaxed, sat on the
discharge curve below at the new count. On 10-14 it fell from 3,997 to 3,970 mV in
the first half hour while the count held at 130, and the curve puts 3,970 mV at
130 mAh. So the jump is charge put in over USB, not the gauge re-estimating
itself at boot. `charging` was set in one row of 14,384: apps rarely run while
the watch charges.

**To overturn:** a restart with no USB that still finds more charge, or a jump
whose voltage does not settle onto the curve.

### Voltage tracks the count to within a few millivolts

More than an hour after any restart, voltage against remaining is one smooth
curve, 4,375 mV at 200 mAh down to 3,832 mV at 96 mAh, at 4 to 6 mV per mAh. At
almost every whole mAh the middle 80 % of readings span 2 to 6 mV, so the voltage
places the count to about 1 mAh. Two bands are wider: 140 to 144 mAh (up to 24 mV),
where two discharges overlap, and 196 to 200 mAh (up to 26 mV), just after a charge.

This says the two agree with each other. It does not say either is right: if the
gauge corrects its count from the voltage, the curve is tight by construction and
the voltage is not a second opinion at all.

### The current does not add up to the count's fall

Over each stretch without a restart, the average current integrated over time
should equal the fall in remaining charge. It does not.

| Stretch | Count fell | Current integrates to | Ratio |
| --- | --- | --- | --- |
| 10-10, 23.2 h | 32 mAh | 39.1 mAh | 1.22 |
| 10-11, 11.7 h, overnight | 20 | 27.1 | 1.35 |
| 10-12, 22.7 h | 47 | 60.2 | 1.28 |
| 10-13, 23.0 h | 31 | 40.0 | 1.29 |
| 10-14, 26.8 h | 34 | 40.1 | 1.18 |

The gap depends on the hour. From 01:00 to 08:00 the current reads 2.0 to 2.4 mA
while the count falls at 1.4 to 1.8 mA; from 10:00 to 12:00 the current reads
less than the count. Any of three things would do this, and the recordings
cannot tell them apart:

1. the current reading has an offset or gain error;
2. the gauge moves its count by something other than the current, such as a
   voltage model;
3. a 30 s sample of the average current aliases against a periodic load, such as
   Slumber's own 1 Hz heart rate at night.

Until this is settled, a learned capacity cannot be cross-checked against the
current: the cross-check is the thing in doubt.

**To settle:** BatteryLab's 10 s readings against the same hours, and a full
discharge to shutdown, where the count, the current and the time all end at the
same moment.

### Drain by hour

Counted from the remaining charge, over about five hours per hour of the day:

| Hours | Drain |
| --- | --- |
| 18:00 to 23:00 | 0.8 to 1.4 mA |
| 00:00 to 09:00 | 1.4 to 1.8 mA |
| 10:00 to 17:00 | 1.2 to 2.4 mA, highest at noon |

One mAh over five hours is 0.2 mA, so each hour's figure is good to about that.
The evening is the quiet time on this watch, not the working day.

## What the SDK says that callers get wrong

`BatteryMetrics::getCurrent()` and `getAverageCurrent()` return `0.0f` when the
message is invalid, not `-1`. A 0 mA reading and no reading look the same, so
check `isDataValid()` before reading any field.

`BatteryCharging` has two flags, `isUsbConnected()` and `isCharging()`. Slumber
ORs them into one `charging` column. BatteryLab logs them apart.

## What BatteryLab logs

One CSV a UTC day in `Apps/BatteryLab/Raw/`, a row per message:

| Column | Meaning |
| --- | --- |
| `uptime_ms`, `sensor_ts_ms` | When the service saw it, and the sensor's own stamp |
| `utc`, `utc_off_min` | Wall clock, and local minus UTC |
| `kind` | `L` level, `E` metrics, `C` charging, `B` service start, `S` service stop |
| `fields` | The message's field count, whatever `isDataValid()` said |
| `pct_x1000` | `getCharge()` × 1000 |
| `mv` | `getVoltage()` × 1000 |
| `ma_x1000`, `avg_ma_x1000` | `getCurrent()`, `getAverageCurrent()`, × 1000, sign as given |
| `mah_x1000`, `design_mah_x1000` | `getCapacity()`, `getDesignCapacity()`, × 1000 |
| `connected`, `charging` | The two USB flags |

A field the message did not carry is empty. Everything is truncated toward zero
at three decimals, so a counter that moves in whole mAh shows as `…000`.

Level and metrics are requested every 10 s, a third of Slumber's period, and
charging every second. Each row goes to disk at once for the first ten minutes
after a start, because the first readings after a boot are the point, and a USB
unplug kills the process without a stop. After that, rows are written 32 at a
time or every ten minutes, whichever comes first.

At 10 s that is about 17,000 rows and 1.2 MB a day. Nothing deletes old files:
this is a probe, to be removed once it has answered its questions.

## The experiments

1. **Install it and read the screen.** `design` is `getDesignCapacity()`. If it
   says about 148 mAh, the clamp is the gauge working out the percentage against
   the design capacity while counting from a full charge 56 mAh larger.
2. **Restart without USB.** Hold the button to restart, off the cable. The first
   `E` rows after the `B` row show whether a boot alone moves the count.
3. **Charge on a wall plug, not a computer.** If apps keep running on power without
   a data connection, the `C` rows and the count across the whole charge show
   what a charge looks like from the inside.
4. **Two subscribers.** Slumber asks for battery every 30 s, BatteryLab every 10 s.
   If Slumber's `msgs` column per row rises once BatteryLab is installed, the
   firmware serves the faster period to both.
5. **Run it down.** Wear it off the charger until it shuts down. The last rows give
   the count, the voltage and the current at the end, and the time from the last
   full charge gives what the cell actually delivered.
