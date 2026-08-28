# UNA Watch sensor profile -- firmware 1.4.0

Measured on one physically-owned UNA Watch by **SensorLab**, an instrument app built out of tree against the UNA SDK. Unofficial; not affiliated with, endorsed or sponsored by UNA Watch Ltd.

Generated 2026-08-28 from `profile-1.4.0.json`.

## Completeness -- how much of this is known

**54 % complete**: 764 of 1395 applicable claims have an answer. 763 confirmed, 1 likely, 0 refuted, 631 still unverified. A further 620 claims cannot apply to this device and are excluded from the denominator rather than counted as gaps.

Every figure below carries the method that produced it and the number of samples behind it. A figure with neither is not in this document.

| Probe layer | Applicable | Answered | Complete |
| --- | --- | --- | --- |
| existence | 142 | 142 | 100 % |
| frame | 124 | 113 | 91 % |
| liveness | 190 | 132 | 69 % |
| timing | 180 | 79 | 43 % |
| value | 492 | 297 | 60 % |
| control | 186 | 0 | 0 % |
| physical | 47 | 0 | 0 % |
| consistency | 34 | 1 | 2 % |

## The device this describes

| | |
| --- | --- |
| Firmware | `1.4.0` *(declared in settings.json, not read from the kernel)* |
| Hardware | `unknown` |
| Kernel interface version | 3 (the version this app was built against) |
| SensorLab version | `0.1.0` |
| Catalogue version | 2 |
| Sensor type table | version 2, generated from `kernel-interface` |
| Run | 15, 0 s, ended `completed` |
| Requested period / latency | 40 ms / 5000 ms |
| Screen attached during the run | yes |

The requested period and latency are what the app *asked for*. What the device delivered is in the tables below, and on this platform the two are known to differ.

## Findings

- **The firmware version was declared, not read.** `1.4.0` came from `settings.json`; the kernel did not answer `RequestSystemInfo`. Every comparison keys on this field, so treat it as a label rather than as evidence.
- **6 sensor types are absent from `Docs/SensorsLayer.md` entirely**, though `SensorTypes.hpp` declares them: `HEART_RATE_EX` (0x43), `STEP_COUNTER_DAILY` (0x52), `RUNNING_CADENCE` (0x53), `FLOOR_COUNTER_DAILY` (0x61), `ACTIVITY_TIME` (0xe0), `GRADE` (0x150). This is a documentation gap rather than a behavioural one, and it is the cheapest thing in this report to act on.
- **5 types ship no data parser**: `MAGNETIC_FIELD` (0x30), `HEART_BEAT` (0x40), `GESTURE_RECOGNITION` (0xd0), `PPG` (0xf0), `ECG` (0x100). For these, the delivered field count and per-field behaviour below are the only description of the frame that exists anywhere -- and the field *semantics* are inferred, not documented.
- **`GpsLocation::isDataValid()` reads a field before it checks the field count.** On a short frame that is an out-of-bounds read: `&&` short-circuits left to right and `DataView`'s bounds assert is compiled out at `-Os`. Affects `GPS_LOCATION` (0x110).
- **28 of 29 shipped parsers test the delivered field count for exact equality**, so a single appended field silently invalidates every sample they read. The exception is `HeartRateEx`, which uses `>=` deliberately so a future kernel can extend the frame. The asymmetry is the finding: one appended field would be a no-op for some apps on this platform and total data loss for the rest.
- **`HEART_RATE_EX` field f5 is stuck.** It never changed across 22945 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x43.value.f5_stuck_max_run` for how long the run was.
- **`HEART_RATE_EX` field f6 is stuck.** It never changed across 22945 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x43.value.f6_stuck_max_run` for how long the run was.
- **`STEP_DETECTOR` field f0 is stuck.** It never changed across 630 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x50.value.f0_stuck_max_run` for how long the run was.
- **`FLOOR_COUNTER` field f0 is stuck.** It never changed across 2 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x60.value.f0_stuck_max_run` for how long the run was.
- **`PRESSURE` field f1 is stuck.** It never changed across 115093 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x80.value.f1_stuck_max_run` for how long the run was.
- **`WRIST_MOTION` field f0 is stuck.** It never changed across 19 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0xa0.value.f0_stuck_max_run` for how long the run was.
- **`GPS_LOCATION` field f0 is stuck.** It never changed across 22885 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x110.value.f0_stuck_max_run` for how long the run was.
- **`GPS_LOCATION` field f1 is stuck.** It never changed across 22885 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x110.value.f1_stuck_max_run` for how long the run was.
- **`GPS_LOCATION` field f2 is stuck.** It never changed across 22885 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x110.value.f2_stuck_max_run` for how long the run was.
- **`GPS_LOCATION` field f3 is stuck.** It never changed across 22885 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x110.value.f3_stuck_max_run` for how long the run was.
- **`GPS_LOCATION` field f4 is stuck.** It never changed across 22885 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x110.value.f4_stuck_max_run` for how long the run was.
- **`GPS_SPEED` field f0 is stuck.** It never changed across 22880 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x111.value.f0_stuck_max_run` for how long the run was.
- **`GPS_SPEED` field f1 is stuck.** It never changed across 22880 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x111.value.f1_stuck_max_run` for how long the run was.
- **`GPS_SPEED` field f2 is stuck.** It never changed across 22880 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x111.value.f2_stuck_max_run` for how long the run was.
- **`GPS_DISTANCE` field f0 is stuck.** It never changed across 22895 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x112.value.f0_stuck_max_run` for how long the run was.
- **`BATTERY_METRICS` field f4 is stuck.** It never changed across 22985 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x122.value.f4_stuck_max_run` for how long the run was.
- **`GRADE` field f0 is stuck.** It never changed across 22915 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x150.value.f0_stuck_max_run` for how long the run was.
- **`GRADE` field f1 is stuck.** It never changed across 22915 samples. A sensor that is not absent and not working is the hardest kind to notice: check `0x150.value.f1_stuck_max_run` for how long the run was.
- **6 types resolve no driver at all.** `RequestDefault` returned nothing to subscribe to, so there is no producer for them on this firmware: `ECG` (0x100), `GESTURE_RECOGNITION` (0xd0), `HEART_BEAT` (0x40), `MAGNETIC_FIELD` (0x30), `PPG` (0xf0), `SPO2` (0xf1). These are measured negatives, not untested rows -- which is the distinction that makes them useful.

## Every sensor type, and whether it exists

`resolves` is whether `RequestDefault` returned a handle -- whether there is a producer at all. `connects` is a separate question. `drivers` is `RequestList`'s answer, which no app had ever asked for before this one. `descriptor` is `RequestGetDesc`: the kernel naming its own driver.

| Type | Name | Resolves | Connects | Drivers | Descriptor | Delivered fields | Parser fields | Complete |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `0x10` | ACCELEROMETER | yes | yes | 1 | `BMI270 accelerometer` | 3 | 3 | 70 % |
| `0x11` | ACCELEROMETER_RAW | yes | yes | 1 | `BMI270 accelerometer raw` | 3 | 3 | 84 % |
| `0x20` | GYROSCOPE | yes | yes | 1 | `BMI270 accelerometer` | 3 | 3 | 75 % |
| `0x21` | GYROSCOPE_RAW | yes | yes | 1 | `BMI270 gyroscope raw` | 3 | 3 | 86 % |
| `0x30` | MAGNETIC_FIELD | no | no | 0 | -- | -- | *none shipped* | 100 % |
| `0x40` | HEART_BEAT | no | no | 0 | -- | -- | *none shipped* | 100 % |
| `0x41` | HEART_RATE | yes | yes | 1 | `Heart Rate sensor` | 2 | 2 | 73 % |
| `0x42` | HEART_RATE_METRICS_DAILY | yes | yes | 1 | `Heart rate metrics` | 2 | 2 | 34 % |
| `0x43` | HEART_RATE_EX | yes | yes | 1 | `Heart Rate (multi-source) sensor` | 7 | 7 | 88 % |
| `0x50` | STEP_DETECTOR | yes | yes | 1 | `BMI270 step detector` | 1 | 1 | 51 % |
| `0x51` | STEP_COUNTER | yes | yes | 1 | `BMI270 step counter` | 1 | 1 | 30 % |
| `0x52` | STEP_COUNTER_DAILY | yes | yes | 1 | `Daily step counter` | 1 | 1 | 31 % |
| `0x53` | RUNNING_CADENCE | yes | yes | 1 | `Running cadence` | 2 | 2 | 80 % |
| `0x60` | FLOOR_COUNTER | yes | yes | 1 | `Virtual floor counter` | 2 | 2 | 30 % |
| `0x61` | FLOOR_COUNTER_DAILY | yes | yes | 1 | `Daily floor counter` | 2 | 2 | 30 % |
| `0x70` | AMBIENT_TEMPERATURE | yes | yes | 1 | `MS5837 temperature` | 1 | 1 | 76 % |
| `0x80` | PRESSURE | yes | yes | 1 | `MS5837 pressure` | 2 | 2 | 76 % |
| `0x90` | ALTIMETER | yes | yes | 2 | `Virtual altimeter` | 1 | 1 | 68 % |
| `0xa0` | WRIST_MOTION | yes | yes | 1 | `BMI270 wrist motion` | 1 | 1 | 32 % |
| `0xb0` | MOTION_DETECT | yes | yes | 1 | `BMI270 motion detection` | 1 | 1 | 50 % |
| `0xc0` | ACTIVITY_RECOGNITION | yes | yes | 1 | `BMI270 activity recognition` | 2 | 2 | 30 % |
| `0xd0` | GESTURE_RECOGNITION | no | no | 0 | -- | -- | *none shipped* | 100 % |
| `0xe0` | ACTIVITY_TIME | yes | yes | 1 | `Activity time since boot` | 1 | 1 | 27 % |
| `0xe1` | ACTIVITY_TIME_DAILY | yes | yes | 1 | `Daily activity time` | 1 | 1 | 33 % |
| `0xf0` | PPG | no | no | 0 | -- | -- | *none shipped* | 100 % |
| `0xf1` | SPO2 | no | no | 0 | -- | -- | 2 | 100 % |
| `0x100` | ECG | no | no | 0 | -- | -- | *none shipped* | 100 % |
| `0x110` | GPS_LOCATION | yes | yes | 1 | `Gps location` | 5 | 5 | 78 % |
| `0x111` | GPS_SPEED | yes | yes | 1 | `Gps speed` | 3 | 3 | 82 % |
| `0x112` | GPS_DISTANCE | yes | yes | 1 | `Gps distance` | 1 | 1 | 73 % |
| `0x120` | BATTERY_LEVEL | yes | yes | 1 | `Battery level` | 1 | 1 | 38 % |
| `0x121` | BATTERY_CHARGING | yes | yes | 1 | `Battery charging` | 2 | 2 | 23 % |
| `0x122` | BATTERY_METRICS | yes | yes | 1 | `Battery metrics` | 5 | 5 | 86 % |
| `0x130` | FUSION | yes | yes | 1 | `BMI270 Fusion` | -- | 6 | 5 % |
| `0x131` | FUSION_RAW | yes | yes | 1 | `BMI270 accelerometer raw` | -- | 6 | 5 % |
| `0x140` | TOUCH_DETECT | yes | yes | 1 | `Touch detect sensor` | 1 | 1 | 24 % |
| `0x150` | GRADE | yes | yes | 1 | `Terrain grade` | 2 | 2 | 80 % |

## Per sensor, in full

### `0x10` ACCELEROMETER

> Acceleration (3-axis).

- Parser `Accelerometer`, 3 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp`).
- 70 % complete (41 of 58 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `X` | float | X axis |
| 1 | `Y` | float | Y axis |
| 2 | `Z` | float | Z axis |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 3 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 3 (`SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 120494 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 304.346 1/min | -- | 120494 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 1158830 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 2839 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 5048 ms | -- | 1158830 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 10.5 samples | 5.5 / 10.5 / 10.5 | 120494 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 2926.62 1/min | -- | 1158830 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 0.5 ms | 0.5 / 0.5 / 8119 | 120493 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | CONFIRMED | 3.50338 ppm | -- | 1158830 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 48.4849 Hz | 48.4849 / 48.4849 / 48.4849 | 1158829 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 20.625 ms | 20.625 / 20.625 / 20.625 | 1158829 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 1158830 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 1158830 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 1158830 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 0.00239372 | -- | 1158830 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 24.7783 | -- | 1158830 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 0.475843 | -- | 1158830 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | -23.7608 | -- | 1158830 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 1158830 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 5 samples | -- | 1158830 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 1158830 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | CONFIRMED | 0.00239372 | -- | 1158830 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 25.365 | -- | 1158830 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | -3.82979 | -- | 1158830 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | -30.5917 | -- | 1158830 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 1158830 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 6 samples | -- | 1158830 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 1 | -- | 1158830 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | CONFIRMED | 0.00239372 | -- | 1158830 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 25.6858 | -- | 1158830 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 4.76102 | -- | 1158830 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | -17.4567 | -- | 1158830 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | CONFIRMED | 0 samples | -- | 1158830 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 6 samples | -- | 1158830 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0x11` ACCELEROMETER_RAW

> Acceleration raw samples (implementation-defined units).

- Parser `AccelerometerRaw`, 3 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp`).
- 84 % complete (38 of 45 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `X` | i32 | X axis |
| 1 | `Y` | i32 | Y axis |
| 2 | `Z` | i32 | Z axis |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 3 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 3 (`SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 121037 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 305.717 1/min | -- | 121037 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 1163935 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 2838 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 3037 ms | -- | 1163935 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 10.5 samples | 5.5 / 10.5 / 10.5 | 121037 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 2939.52 1/min | -- | 1163935 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 0.5 ms | 0.5 / 0.5 / 7624 | 121036 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | CONFIRMED | 1.7728 ppm | -- | 1163935 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 48.4849 Hz | 48.4849 / 48.4849 / 48.4849 | 1163934 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 20.625 ms | 20.625 / 20.625 / 20.625 | 1163934 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 1163935 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 1163935 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | CONFIRMED | 16 values | -- | 1163935 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 1163935 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 10349 | -- | 1163935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 201.344 | -- | 1163935 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | -9924 | -- | 1163935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 5 samples | -- | 1163935 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | CONFIRMED | 16 values | -- | 1163935 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 1163935 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 10594 | -- | 1163935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | -1596.61 | -- | 1163935 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | -12777 | -- | 1163935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 6 samples | -- | 1163935 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | CONFIRMED | 16 values | -- | 1163935 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 1 | -- | 1163935 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 10728 | -- | 1163935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 1991.17 | -- | 1163935 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | -7291 | -- | 1163935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 6 samples | -- | 1163935 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x20` GYROSCOPE

> Angular rate (3-axis).

- Parser `Gyroscope`, 3 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserGyroscope.hpp`).
- 75 % complete (41 of 54 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `X` | float | X axis |
| 1 | `Y` | float | Y axis |
| 2 | `Z` | float | Z axis |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 3 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 3 (`SDK/SensorLayer/DataParsers/SensorDataParserGyroscope.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 59839 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 151.165 1/min | -- | 59839 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 576205 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5786 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 10093 ms | -- | 576205 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 10.5 samples | 5.5 / 10.5 / 10.5 | 59839 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 1455.31 1/min | -- | 576205 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 0.5 ms | 0.5 / 0.5 / 15259 | 59838 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | CONFIRMED | -12.4022 ppm | -- | 576205 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 24.6154 Hz | 24.6154 / 24.6154 / 24.6154 | 576204 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 40.625 ms | 40.625 / 40.625 / 40.625 | 576204 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 576205 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 576205 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 576205 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 0.0610352 | -- | 576205 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 637.898 | -- | 576205 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | -0.704709 | -- | 576205 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | -537.736 | -- | 576205 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 576205 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 7 samples | -- | 576205 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 576205 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | CONFIRMED | 0.0610352 | -- | 576205 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 374.218 | -- | 576205 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | -0.271057 | -- | 576205 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | -408.277 | -- | 576205 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 576205 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 8 samples | -- | 576205 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 1 | -- | 576205 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | CONFIRMED | 0.0610352 | -- | 576205 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 336.131 | -- | 576205 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 0.0247714 | -- | 576205 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | -366.955 | -- | 576205 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | CONFIRMED | 0 samples | -- | 576205 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 8 samples | -- | 576205 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0x21` GYROSCOPE_RAW

> Angular rate raw samples.

- Parser `GyroscopeRaw`, 3 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp`).
- 86 % complete (38 of 44 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `X` | i32 | X axis |
| 1 | `Y` | i32 | Y axis |
| 2 | `Z` | i32 | Z axis |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 3 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 3 (`SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 61057 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 154.242 1/min | -- | 61057 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 587195 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5783 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 5070 ms | -- | 587195 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 10.5 samples | 5.5 / 10.5 / 10.5 | 61057 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 1483.06 1/min | -- | 587195 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 0.5 ms | 0.5 / 0.5 / 10255 | 61056 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | CONFIRMED | -12.7353 ppm | -- | 587195 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 24.6154 Hz | 24.6154 / 24.6154 / 24.6154 | 587194 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 40.625 ms | 40.625 / 40.625 / 40.625 | 587194 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 587195 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 587195 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | CONFIRMED | 16 values | -- | 587195 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 587195 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 10451 | -- | 587195 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | -11.6895 | -- | 587195 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | -8810 | -- | 587195 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 7 samples | -- | 587195 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | CONFIRMED | 16 values | -- | 587195 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 587195 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 6131 | -- | 587195 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | -4.47757 | -- | 587195 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | -6689 | -- | 587195 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 8 samples | -- | 587195 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | CONFIRMED | 16 values | -- | 587195 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 1 | -- | 587195 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 5507 | -- | 587195 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 0.403333 | -- | 587195 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | -6012 | -- | 587195 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 8 samples | -- | 587195 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x30` MAGNETIC_FIELD

> Magnetic field (3-axis).

- **No parser ships for this type.** Any frame description below is measured; the field *semantics* are inferred, not documented.
- 100 % complete (3 of 3 applicable claims).

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `control.handle_stable_on_reconnect` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.latency_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 4 | `P6.latency-sweep` | NO_CLAIM |
| `control.period_floor_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.period_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.reconnect_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.second_connection` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P6.contention` | NO_CLAIM |
| `existence.descriptor` | INAPPLICABLE -- RequestDefault resolved no driver, so there is nothing to describe | -- | -- | 0 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `frame.field_count` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-arithmetic` | NO_CLAIM |
| `frame.parser_accepts_frame` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-count` | NO_CLAIM |
| `physical.field_magnitude_ut` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 3 | `P7.vs-geomag-model` | NO_CLAIM |
| `physical.magnitude_stability_pct` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 3 | `P7.figure-eight` | NO_CLAIM |
| `timing.batch_jitter_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f5_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f5_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f5_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f6_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f6_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f6_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f6_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f7_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f7_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f7_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f7_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |

### `0x40` HEART_BEAT

> Beat peak event.

- **No parser ships for this type.** Any frame description below is measured; the field *semantics* are inferred, not documented.
- 100 % complete (3 of 3 applicable claims).

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `control.handle_stable_on_reconnect` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.latency_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 4 | `P6.latency-sweep` | NO_CLAIM |
| `control.period_floor_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.period_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.reconnect_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.second_connection` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P6.contention` | NO_CLAIM |
| `existence.descriptor` | INAPPLICABLE -- RequestDefault resolved no driver, so there is nothing to describe | -- | -- | 0 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `frame.field_count` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-arithmetic` | NO_CLAIM |
| `frame.parser_accepts_frame` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-count` | NO_CLAIM |
| `physical.driver_exists` | INAPPLICABLE -- HEART_BEAT resolves no driver (ledger row S5); UNA state HR detection is frequency-domain rather than per-beat (PR #167). Re-checked every run. | -- | -- | 0 / 1 | `P7.existence-recheck` | NO_CLAIM |
| `timing.batch_jitter_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f5_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f5_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f5_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f6_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f6_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f6_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f6_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f7_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f7_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f7_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f7_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |

### `0x41` HEART_RATE

> Current (arbitrated) heart rate (bpm) + trust. 2 fields.

- Parser `HeartRate`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp`).
- 73 % complete (33 of 45 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `BPM` | float | Heart rate in bpm (float) |
| 1 | `TRUST_LEVEL` | float | Trust level (float) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserHeartRate.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4596 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.6073 1/min | -- | 4596 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22980 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5273 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 6552 ms | -- | 22980 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4596 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 58.0367 1/min | -- | 22980 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4826.94 / 5154.19 / 5481.44 | 4595 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22979 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22979 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22980 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22980 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 22980 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 1 | -- | 22980 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 91 | -- | 22980 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 57.7884 | -- | 22980 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 50 | -- | 22980 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22980 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 54 samples | -- | 22980 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 22980 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | CONFIRMED | 1 | -- | 22980 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 3 | -- | 22980 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 2.80622 | -- | 22980 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 0 | -- | 22980 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 22980 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 1026 samples | -- | 22980 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0x42` HEART_RATE_METRICS_DAILY

> Aggregated metrics for the current day (e.g., AHR, RHR).

- Parser `HeartRateMetrics`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserHeartRateMetrics.hpp`).
- 34 % complete (14 of 41 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `AHR` | float | Average Heart Rate (float, bpm) |
| 1 | `RHR` | float | Resting Heart Rate (float, bpm) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserHeartRateMetrics.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 14 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.0342349 1/min | -- | 14 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 207 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 14 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 14 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 14 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 14 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0x43` HEART_RATE_EX

> Opt-in multi-source HR: arbitrated + source + raw optical + raw external. 7 fields.

- Not listed in `Docs/SensorsLayer.md`.
- Parser `HeartRateEx`, 7 fields, field-count test `at_least` (`SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp`).
- 88 % complete (66 of 75 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `BPM` | float | Arbitrated heart rate (bpm) |
| 1 | `TRUST_LEVEL` | float | Arbitrated trust level |
| 2 | `SOURCE` | float | Which source was chosen (Source) |
| 3 | `OPTICAL_BPM` | float | Raw optical (PPG) bpm (0 if none) |
| 4 | `OPTICAL_TRUST` | float | Raw optical trust |
| 5 | `EXTERNAL_BPM` | float | Raw external (strap) bpm (0 if none) |
| 6 | `EXTERNAL_TRUST` | float | Raw external trust |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 7 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 7 (`SDK/SensorLayer/DataParsers/SensorDataParserHeartRateEx.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4589 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.5896 1/min | -- | 4589 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22945 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5274 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11292 ms | -- | 22945 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4589 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 57.9483 1/min | -- | 22945 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4826.94 / 5154.19 / 5645.06 | 4588 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22944 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22944 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22945 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22945 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 1 | -- | 22945 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 91 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 57.7835 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 54 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | CONFIRMED | 1 | -- | 22945 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 3 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 2.80641 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 1026 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 1 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | CONFIRMED | 1 | -- | 22945 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 1 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 0.999956 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 21870 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_ever_changed` | CONFIRMED | 1 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | CONFIRMED | 1 | -- | 22945 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | CONFIRMED | 91 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | CONFIRMED | 57.7835 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | CONFIRMED | 54 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_ever_changed` | CONFIRMED | 1 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_lsb` | CONFIRMED | 1 | -- | 22945 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_max` | CONFIRMED | 3 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | CONFIRMED | 2.80641 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | CONFIRMED | 1026 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_ever_changed` | CONFIRMED | 0 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_max` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_mean` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f5_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f5_stuck_max_run` | CONFIRMED | 22945 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_ever_changed` | CONFIRMED | 0 | -- | 22945 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_max` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_mean` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f6_min` | CONFIRMED | 0 | -- | 22945 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_nonfinite` | CONFIRMED | 0 samples | -- | 22945 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f6_stuck_max_run` | CONFIRMED | 22945 samples | -- | 22945 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f6_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f6_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |

### `0x50` STEP_DETECTOR

> Step event.

- Parser `StepDetector`, 1 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp`).
- 51 % complete (15 of 29 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `STEP_DETECTED` | u32 | Step is detected (always 1) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 630 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 1.66298 1/min | -- | 630 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 2 | -- | 630 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 16053 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 3388740 ms | -- | 630 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 1.5 samples | 1.5 / 1.5 / 1.5 | 630 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 1.66298 1/min | -- | 630 / 60 | `P3.delivery-count` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 630 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- classified as an event sensor: dt statistics do not apply | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- classified as an event sensor: dt statistics do not apply | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- classified as an event sensor: dt statistics do not apply | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x51` STEP_COUNTER

> Step count since boot (monotonic).

- Parser `StepCounter`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp`).
- 30 % complete (12 of 40 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `STEP_COUNT` | u32 | Step count (uint32_t) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 40 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.290693 1/min | -- | 40 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 207 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 1.5 samples | 1.5 / 1.5 / 1.5 | 40 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 40 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x52` STEP_COUNTER_DAILY

> Step count for the current day.

- Not listed in `Docs/SensorsLayer.md`.
- Parser `StepCounter`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp`).
- 31 % complete (11 of 35 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `STEP_COUNT` | u32 | Step count (uint32_t) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 11 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.074228 1/min | -- | 11 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 206 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 11 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x53` RUNNING_CADENCE

> Running cadence (steps/min); step length is derived SDK-side.

- Not listed in `Docs/SensorsLayer.md`.
- Parser `RunningCadence`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserRunningCadence.hpp`).
- 80 % complete (33 of 41 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `CADENCE_SPM` | float |  |
| 1 | `CADENCE_VALID` | u32 |  |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserRunningCadence.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4587 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.5844 1/min | -- | 4587 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22935 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | CONFIRMED | 1 | -- | 22935 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5272 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 6538 ms | -- | 22935 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4587 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 57.9221 1/min | -- | 22935 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4990.56 / 5154.19 / 5481.44 | 4586 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22934 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22934 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22935 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22935 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 22935 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 0.913963 | -- | 22935 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 114.998 | -- | 22935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 0.0790091 | -- | 22935 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 0 | -- | 22935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22935 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 21438 samples | -- | 22935 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | CONFIRMED | 2 values | -- | 22935 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 22935 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 1 | -- | 22935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 0.000784827 | -- | 22935 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 0 | -- | 22935 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 21438 samples | -- | 22935 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x60` FLOOR_COUNTER

> Floor counter since boot (monotonic).

- Parser `FloorCounter`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp`).
- 30 % complete (12 of 40 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `FLOORS_UP` | i32 | Floors up counter (int32_t) |
| 1 | `FLOORS_DOWN` | i32 | Floors down counter (int32_t) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 2 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.282042 1/min | -- | 2 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 204 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 2 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 2 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x61` FLOOR_COUNTER_DAILY

> Floor counter for the current day.

- Not listed in `Docs/SensorsLayer.md`.
- Parser `FloorCounter`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp`).
- 30 % complete (12 of 39 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `FLOORS_UP` | i32 | Floors up counter (int32_t) |
| 1 | `FLOORS_DOWN` | i32 | Floors down counter (int32_t) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserFloorCounter.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 2 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.0209515 1/min | -- | 2 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 204 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 2 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 2 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x70` AMBIENT_TEMPERATURE

> Ambient temperature.

- Parser `Temperature`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp`).
- 76 % complete (26 of 34 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `TEMP` | float | Temperature value (units are device-specific) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 6891 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 17.4127 1/min | -- | 6891 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 114851 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 10202 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11002 ms | -- | 114851 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 17.5 samples | 16.5 / 17.5 / 17.5 | 6891 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 290.133 1/min | -- | 114851 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 0.5 ms | 0.5 / 0.5 / 20981 | 6890 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 4.85032 Hz | 4.85032 / 4.85032 / 5.0043 | 114850 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 206.172 ms | 199.828 / 206.172 / 206.172 | 114850 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 114851 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 114851 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 114851 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 0.00999832 | -- | 114851 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 37.07 | -- | 114851 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 34.4128 | -- | 114851 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 29.89 | -- | 114851 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 114851 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 82 samples | -- | 114851 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0x80` PRESSURE

> Atmospheric pressure.

- Parser `Pressure`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp`).
- 76 % complete (32 of 42 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `PRESS` | float | Station pressure, Pa |
| 1 | `PRESS_SEA_LEVEL` | float | QNH / sea-level pressure, Pa |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 9207 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 23.2659 1/min | -- | 9207 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 115093 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 10202 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11002 ms | -- | 115093 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 13.5 samples | 11.5 / 13.5 / 13.5 | 9207 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 290.744 1/min | -- | 115093 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 0.5 ms | 0.5 / 0.5 / 20996 | 9206 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 4.85032 Hz | 4.85032 / 4.85032 / 5.0043 | 115092 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 206.172 ms | 199.828 / 206.172 / 206.172 | 115092 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 115093 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 115093 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 115093 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 1 | -- | 115093 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 100341 | -- | 115093 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 100300 | -- | 115093 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 100254 | -- | 115093 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 115093 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 7 samples | -- | 115093 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 0 | -- | 115093 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 101618 | -- | 115093 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 101618 | -- | 115093 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 101618 | -- | 115093 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 115093 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 115093 samples | -- | 115093 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |

### `0x90` ALTIMETER

> Altimeter.

- Parser `Altimeter`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp`).
- 68 % complete (26 of 38 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `ALTITUDE` | float | Altitude in meters |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 2 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 6876 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 17.3748 1/min | -- | 6876 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 114602 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 10231 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11002 ms | -- | 114602 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 17.5 samples | 16.5 / 17.5 / 17.5 | 6876 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 289.504 1/min | -- | 114602 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 1.01562 ms | 0.015625 / 1.01562 / 21205 | 6875 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 4.85032 Hz | 4.85032 / 4.85032 / 5.0043 | 114601 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 206.172 ms | 199.828 / 206.172 / 206.172 | 114601 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 114602 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 114602 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 114602 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 7.62939e-06 | -- | 114602 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 89.0867 | -- | 114602 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 85.6958 | -- | 114602 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 82.6899 | -- | 114602 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 114602 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 1 samples | -- | 114602 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0xa0` WRIST_MOTION

> Wrist-motion event

- Parser `WristMotion`, 1 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp`).
- 32 % complete (11 of 34 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `WRIST_MOTION` | u32 | Wrist motion event flag |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 19 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.0505507 1/min | -- | 19 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 1342392 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 19 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0xb0` MOTION_DETECT

> Motion state events. NO_MOTION, MOTION, SIG_MOTION

- Parser `MotionDetect`, 1 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp`).
- 50 % complete (15 of 30 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `ID` | u32 | Motion identifier (see @ref Motion) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 246 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.635374 1/min | -- | 246 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 2 | -- | 246 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 200 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 3257955 ms | -- | 246 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 1.5 samples | 1.5 / 1.5 / 1.5 | 246 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 0.635369 1/min | -- | 246 / 60 | `P3.delivery-count` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 246 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- classified as an event sensor: dt statistics do not apply | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- classified as an event sensor: dt statistics do not apply | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- classified as an event sensor: dt statistics do not apply | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0xc0` ACTIVITY_RECOGNITION

> Activity state classification. STILL, WALKING, RUNNING, UNKNOWN

- Parser `ActivityRecognition`, 2 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp`).
- 30 % complete (12 of 39 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `ID` | u32 | Activity identifier (see @ref Activity) |
| 1 | `CONFIDENCE` | u32 | Confidence in percent [0..100] |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 25 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.178821 1/min | -- | 25 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 200 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 25 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 25 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0xd0` GESTURE_RECOGNITION

> Discrete gesture events.

- **No parser ships for this type.** Any frame description below is measured; the field *semantics* are inferred, not documented.
- 100 % complete (3 of 3 applicable claims).

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `control.handle_stable_on_reconnect` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.latency_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 4 | `P6.latency-sweep` | NO_CLAIM |
| `control.period_floor_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.period_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.reconnect_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.second_connection` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P6.contention` | NO_CLAIM |
| `existence.descriptor` | INAPPLICABLE -- RequestDefault resolved no driver, so there is nothing to describe | -- | -- | 0 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `frame.field_count` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-arithmetic` | NO_CLAIM |
| `frame.parser_accepts_frame` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-count` | NO_CLAIM |
| `physical.produces_anything` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P7.gesture-hunt` | NO_CLAIM |
| `timing.batch_jitter_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f5_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f5_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f5_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f6_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f6_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f6_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f6_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f7_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f7_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f7_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f7_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |

### `0xe0` ACTIVITY_TIME

> Active minutes since boot (monotonic).

- Not listed in `Docs/SensorsLayer.md`.
- Parser `Activity`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp`).
- 27 % complete (9 of 33 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `DURATION` | u32 | Activity duration in minutes (uint32_t) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0 1/min | -- | 1 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 198 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0xe1` ACTIVITY_TIME_DAILY

> Active minutes for the current day.

- Parser `Activity`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp`).
- 33 % complete (11 of 33 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `DURATION` | u32 | Activity duration in minutes (uint32_t) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserActivity.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 2 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.0209515 1/min | -- | 2 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 198 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 2 / 2 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0xf0` PPG

> Photoplethysmogram data.

- **No parser ships for this type.** Any frame description below is measured; the field *semantics* are inferred, not documented.
- 100 % complete (3 of 3 applicable claims).

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `control.handle_stable_on_reconnect` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.latency_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 4 | `P6.latency-sweep` | NO_CLAIM |
| `control.period_floor_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.period_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.reconnect_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.second_connection` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P6.contention` | NO_CLAIM |
| `existence.descriptor` | INAPPLICABLE -- RequestDefault resolved no driver, so there is nothing to describe | -- | -- | 0 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `frame.field_count` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-arithmetic` | NO_CLAIM |
| `frame.parser_accepts_frame` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-count` | NO_CLAIM |
| `physical.driver_exists` | INAPPLICABLE -- PPG has never been observed to resolve a driver on hardware. Whether the raw waveform is available to apps at all is the open question. | -- | -- | 0 / 1 | `P7.existence-recheck` | NO_CLAIM |
| `timing.batch_jitter_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f5_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f5_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f5_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f6_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f6_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f6_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f6_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f7_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f7_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f7_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f7_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |

### `0xf1` SPO2

> Blood-oxygen saturation (SpO2), derived from the optical PPG path (%).

- Parser `Spo2`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserSpo2.hpp`).
- 100 % complete (3 of 3 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `SATURATION` | float | Blood-oxygen saturation in percent (float) |
| 1 | `TRUST_LEVEL` | float | Trust level (float) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `control.handle_stable_on_reconnect` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.latency_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 4 | `P6.latency-sweep` | NO_CLAIM |
| `control.period_floor_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.period_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.reconnect_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.second_connection` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P6.contention` | NO_CLAIM |
| `existence.descriptor` | INAPPLICABLE -- RequestDefault resolved no driver, so there is nothing to describe | -- | -- | 0 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `frame.field_count` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-arithmetic` | NO_CLAIM |
| `frame.parser_accepts_frame` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-count` | NO_CLAIM |
| `physical.driver_exists` | INAPPLICABLE -- SPO2 resolves no driver on this firmware (ledger row S4), so there is nothing to compare against a reference. Re-checked every run. | -- | -- | 0 / 1 | `P7.existence-recheck` | NO_CLAIM |
| `timing.batch_jitter_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |

### `0x100` ECG

> Electrocardiogram data.

- **No parser ships for this type.** Any frame description below is measured; the field *semantics* are inferred, not documented.
- 100 % complete (3 of 3 applicable claims).

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 0 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `control.handle_stable_on_reconnect` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.latency_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 4 | `P6.latency-sweep` | NO_CLAIM |
| `control.period_floor_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.period_honoured` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 8 | `P6.period-sweep` | NO_CLAIM |
| `control.reconnect_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 100 | `P6.reconnect-soak` | NO_CLAIM |
| `control.second_connection` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P6.contention` | NO_CLAIM |
| `existence.descriptor` | INAPPLICABLE -- RequestDefault resolved no driver, so there is nothing to describe | -- | -- | 0 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `frame.field_count` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-arithmetic` | NO_CLAIM |
| `frame.parser_accepts_frame` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 60 | `P3.delivery-count` | NO_CLAIM |
| `physical.driver_exists` | INAPPLICABLE -- ECG on a device with no electrodes is expected absent. Confirmed rather than assumed, so the report can say so instead of leaving a hole. | -- | -- | 0 / 1 | `P7.existence-recheck` | NO_CLAIM |
| `timing.batch_jitter_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.clock_skew_ppm` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 500000 | `P4.skew-regression` | NO_CLAIM |
| `timing.delivered_hz` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f5_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f5_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f5_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f5_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f5_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f5_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f6_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f6_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f6_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f6_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f6_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f6_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_distinct_observed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f7_ever_changed` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f7_lsb` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f7_max` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_mean` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f7_min` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f7_nonfinite` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f7_stuck_max_run` | INAPPLICABLE -- RequestDefault resolved no driver: nothing to subscribe to | -- | -- | 0 / 1000 | `P5.stuck-run` | NO_CLAIM |

### `0x110` GPS_LOCATION

> GNSS location.

- Parser `GpsLocation`, 5 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp`).
- 78 % complete (50 of 64 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `PRECISION` | float | Precision (in meters) |
| 1 | `COORDS_VALID` | u32 | Coordinates are valid |
| 2 | `LAT` | float | Latitude,m (float) |
| 3 | `LON` | float | Longitude,m (float) |
| 4 | `ALT` | float | Altitude,m (float) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 5 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 5 (`SDK/SensorLayer/DataParsers/SensorDataParserGpsLocation.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4577 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.5591 1/min | -- | 4577 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22885 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | CONFIRMED | 0 | -- | 22885 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5260 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11285 ms | -- | 22885 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4577 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 57.7958 1/min | -- | 22885 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4826.94 / 5154.19 / 5645.06 | 4576 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22884 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22884 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22885 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22885 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 22885 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22885 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 22885 samples | -- | 22885 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | CONFIRMED | 1 values | -- | 22885 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 0 | -- | 22885 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 22885 samples | -- | 22885 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 0 | -- | 22885 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | CONFIRMED | 0 samples | -- | 22885 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 22885 samples | -- | 22885 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_ever_changed` | CONFIRMED | 0 | -- | 22885 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_max` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | CONFIRMED | 0 samples | -- | 22885 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | CONFIRMED | 22885 samples | -- | 22885 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_ever_changed` | CONFIRMED | 0 | -- | 22885 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_max` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | CONFIRMED | 0 | -- | 22885 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | CONFIRMED | 0 samples | -- | 22885 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | CONFIRMED | 22885 samples | -- | 22885 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |

### `0x111` GPS_SPEED

> GNSS speed.

- Parser `GpsSpeed`, 3 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp`).
- 82 % complete (38 of 46 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `SPEED` | float | Speed, m/s (float) |
| 1 | `SPEED_VALID` | u32 | 1 when the fix is current (uint) |
| 2 | `DEAD_RECKONING` | u32 | 1 when the fix is estimated / dead-reckoning (uint) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 3 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 3 (`SDK/SensorLayer/DataParsers/SensorDataParserGpsSpeed.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4576 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.5566 1/min | -- | 4576 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22880 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | CONFIRMED | 0 | -- | 22880 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5258 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11285 ms | -- | 22880 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4576 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 57.7832 1/min | -- | 22880 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4826.94 / 5154.19 / 5645.06 | 4575 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22879 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22879 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22880 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22880 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 22880 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22880 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 22880 samples | -- | 22880 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | CONFIRMED | 1 values | -- | 22880 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 0 | -- | 22880 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 22880 samples | -- | 22880 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_distinct_observed` | CONFIRMED | 1 values | -- | 22880 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 0 | -- | 22880 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | 0 | -- | 22880 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 22880 samples | -- | 22880 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x112` GPS_DISTANCE

> GNSS distance / odometer.

- Parser `GpsDistance`, 1 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp`).
- 73 % complete (25 of 34 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `DISTANCE` | float | Distance, m(float) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4579 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.5637 1/min | -- | 4579 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22895 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5257 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11285 ms | -- | 22895 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4579 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 57.8186 1/min | -- | 22895 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4826.94 / 5154.19 / 5481.44 | 4578 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22894 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22894 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22895 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22895 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 22895 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 0 | -- | 22895 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 0 | -- | 22895 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 0 | -- | 22895 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22895 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 22895 samples | -- | 22895 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |

### `0x120` BATTERY_LEVEL

> Charge level (%).

- Parser `BatteryLevel`, 1 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp`).
- 38 % complete (13 of 34 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `LEVEL` | float | Battery charge level in percent (float, 0..100) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 48 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0.119208 1/min | -- | 48 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 190 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 1.5 samples | 1.5 / 1.5 / 1.5 | 48 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 48 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 48 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |

### `0x121` BATTERY_CHARGING

> Charging state.

- Parser `BatteryCharging`, 2 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp`).
- 23 % complete (9 of 38 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `CONNECTED` | u32 | USB cable connect status |
| 1 | `CHARGING` | u32 | Charging status |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserBatteryCharging.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0 1/min | -- | 1 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 190 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x122` BATTERY_METRICS

> Voltage/current/capacity metrics.

- Parser `BatteryMetrics`, 5 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp`).
- 86 % complete (53 of 61 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `VOLTAGE` | float | Battery voltage (V) |
| 1 | `CURRENT` | float | Instantaneous current (mA); sign per firmware contract |
| 2 | `AVERAGE_CURRENT` | float | Averaged/filtered current (mA) |
| 3 | `CAPACITY` | float | Remaining capacity (mAh) |
| 4 | `DESIGN_CAPACITY` | float | Full charge (design) capacity (mAh) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 5 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 5 (`SDK/SensorLayer/DataParsers/SensorDataParserBatteryMetrics.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4597 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.6098 1/min | -- | 4597 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22985 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5256 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 6552 ms | -- | 22985 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4597 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 58.0493 1/min | -- | 22985 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4990.56 / 5154.19 / 5481.44 | 4596 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22984 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22984 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22985 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22985 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 1 | -- | 22985 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_lsb` | CONFIRMED | 0.000155926 | -- | 22985 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 4.39883 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 4.15874 | -- | 22985 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 3.89664 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22985 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 17 samples | -- | 22985 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 1 | -- | 22985 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_lsb` | CONFIRMED | 0.15625 | -- | 22985 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 5.625 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | -15.7554 | -- | 22985 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | -83.5938 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_nonfinite` | CONFIRMED | 0 samples | -- | 22985 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 17 samples | -- | 22985 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_ever_changed` | CONFIRMED | 1 | -- | 22985 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f2_lsb` | CONFIRMED | 0.15625 | -- | 22985 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f2_max` | CONFIRMED | -2.96875 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_mean` | CONFIRMED | -16.0661 | -- | 22985 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f2_min` | CONFIRMED | -49.375 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f2_nonfinite` | CONFIRMED | 0 samples | -- | 22985 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f2_stuck_max_run` | CONFIRMED | 17 samples | -- | 22985 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_ever_changed` | CONFIRMED | 1 | -- | 22985 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f3_lsb` | CONFIRMED | 0.5 | -- | 22985 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f3_max` | CONFIRMED | 230.5 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_mean` | CONFIRMED | 178.526 | -- | 22985 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f3_min` | CONFIRMED | 123.5 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f3_nonfinite` | CONFIRMED | 0 samples | -- | 22985 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f3_stuck_max_run` | CONFIRMED | 165 samples | -- | 22985 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_ever_changed` | CONFIRMED | 0 | -- | 22985 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f4_max` | CONFIRMED | 230.5 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_mean` | CONFIRMED | 230.5 | -- | 22985 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f4_min` | CONFIRMED | 230.5 | -- | 22985 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f4_nonfinite` | CONFIRMED | 0 samples | -- | 22985 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f4_stuck_max_run` | CONFIRMED | 22985 samples | -- | 22985 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f2_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f3_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f4_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |

### `0x130` FUSION

> Fused IMU (accel+gyro+mag).

- Parser `Fusion`, 6 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserFusion.hpp`).
- 5 % complete (4 of 77 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `ACCEL_X` | float | Accelerometer X axis |
| 1 | `ACCEL_Y` | float | Accelerometer Y axis |
| 2 | `ACCEL_Z` | float | Accelerometer Z axis |
| 3 | `GYRO_X` | float | Gyroscope X axis |
| 4 | `GYRO_Y` | float | Gyroscope Y axis |
| 5 | `GYRO_Z` | float | Gyroscope Z axis |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |

### `0x131` FUSION_RAW

> Raw fusion inputs.

- Parser `FusionRaw`, 6 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp`).
- 5 % complete (4 of 75 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `ACCEL_X` | i32 | Accelerometer X axis |
| 1 | `ACCEL_Y` | i32 | Accelerometer Y axis |
| 2 | `ACCEL_Z` | i32 | Accelerometer Z axis |
| 3 | `GYRO_X` | i32 | Gyroscope X axis |
| 4 | `GYRO_Y` | i32 | Gyroscope Y axis |
| 5 | `GYRO_Z` | i32 | Gyroscope Z axis |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |

### `0x140` TOUCH_DETECT

> Touch detection, worn / unworn

- Parser `Touch`, 1 fields, field-count test `exact`, plus a value range check (`SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp`).
- 24 % complete (9 of 37 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `TOUCH` | u32 | Touch flag (expected value: 0 or 1) |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 1 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 1 (`SDK/SensorLayer/DataParsers/SensorDataParserTouch.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 0 1/min | -- | 1 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 186 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.ever_valid` | INAPPLICABLE -- this frame carries no field saying whether its contents are valid | -- | -- | 0 / 60 | `P3.validity-flag` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f0_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

### `0x150` GRADE

> Barometric terrain grade (%) + validity.

- Not listed in `Docs/SensorsLayer.md`.
- Parser `Grade`, 2 fields, field-count test `exact` (`SDK/SensorLayer/DataParsers/SensorDataParserGrade.hpp`).
- 80 % complete (32 of 40 applicable claims).

| Field | Name | Read as | Parser's own comment |
| --- | --- | --- | --- |
| 0 | `GRADE_PCT` | float | Terrain grade in percent; valid only when GRADE_VALID. |
| 1 | `GRADE_VALID` | u32 | 1 when grade_pct is a reliable current estimate. |

| Claim | Verdict | Value | Spread | n / min | Method | Conformance |
| --- | --- | --- | --- | --- | --- | --- |
| `existence.connect_succeeds` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-connect` | NO_CLAIM |
| `existence.default_resolves` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-default` | NO_CLAIM |
| `existence.descriptor` | CONFIRMED | -- | -- | 1 / 1 | `P1.request-get-desc` | NO_CLAIM |
| `existence.driver_count` | CONFIRMED | 1 | -- | 1 / 1 | `P1.request-list` | NO_CLAIM |
| `frame.field_count` | CONFIRMED | 2 fields | -- | 1 / 1 | `P2.stride-arithmetic` | MATCHES vs 2 (`SDK/SensorLayer/DataParsers/SensorDataParserGrade.hpp`) |
| `frame.parser_accepts_frame` | CONFIRMED | 1 | -- | 1 / 1 | `P2.is-data-valid` | NO_CLAIM |
| `frame.parser_agreement` | CONFIRMED | 0 | -- | 1 / 1 | `P2.stride-vs-parser` | NO_CLAIM |
| `frame.stride_stable` | CONFIRMED | 1 | -- | 4583 / 2 | `P2.stride-watch` | NO_CLAIM |
| `liveness.batches_per_min` | CONFIRMED | 11.5733 1/min | -- | 4583 / 1 | `P3.delivery-count` | NO_CLAIM |
| `liveness.classification` | CONFIRMED | 1 | -- | 22915 / 60 | `P3.classify` | NO_CLAIM |
| `liveness.ever_valid` | CONFIRMED | 0 | -- | 22915 / 60 | `P3.validity-flag` | NO_CLAIM |
| `liveness.first_sample_ms` | CONFIRMED | 5253 ms | -- | 1 / 1 | `P3.connect-to-first` | NO_CLAIM |
| `liveness.longest_gap_ms` | CONFIRMED | 11285 ms | -- | 22915 / 60 | `P3.delivery-gap` | NO_CLAIM |
| `liveness.samples_per_batch` | CONFIRMED | 5.5 samples | 5.5 / 5.5 / 5.5 | 4583 / 30 | `P3.batch-histogram` | NO_CLAIM |
| `liveness.samples_per_min` | CONFIRMED | 57.8666 1/min | -- | 22915 / 60 | `P3.delivery-count` | NO_CLAIM |
| `timing.batch_jitter_ms` | CONFIRMED | 5154.19 ms | 4826.94 / 5154.19 / 5481.44 | 4582 / 1000 | `P4.batch-arrival` | NO_CLAIM |
| `timing.delivered_hz` | CONFIRMED | 0.968157 Hz | 0.968157 / 0.968157 / 0.998892 | 22914 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.dt_ms` | CONFIRMED | 1032.89 ms | 1001.11 / 1032.89 / 1032.89 | 22914 / 10000 | `P4.dt-histogram` | NO_CLAIM |
| `timing.ts_monotonic` | CONFIRMED | 1 | -- | 22915 / 10000 | `P4.monotonicity` | NO_CLAIM |
| `timing.ts_us_over_999` | CONFIRMED | 0 samples | -- | 22915 / 10000 | `P4.us-invariant` | NO_CLAIM |
| `value.f0_ever_changed` | CONFIRMED | 0 | -- | 22915 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_max` | CONFIRMED | 0 | -- | 22915 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_mean` | CONFIRMED | 0 | -- | 22915 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f0_min` | CONFIRMED | 0 | -- | 22915 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f0_nonfinite` | CONFIRMED | 0 samples | -- | 22915 / 1 | `P5.nonfinite-count` | NO_CLAIM |
| `value.f0_stuck_max_run` | CONFIRMED | 22915 samples | -- | 22915 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_distinct_observed` | CONFIRMED | 1 values | -- | 22915 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f1_ever_changed` | CONFIRMED | 0 | -- | 22915 / 2 | `P5.stuck-run` | NO_CLAIM |
| `value.f1_max` | CONFIRMED | 0 | -- | 22915 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_mean` | CONFIRMED | 0 | -- | 22915 / 1000 | `P5.field-mean` | NO_CLAIM |
| `value.f1_min` | CONFIRMED | 0 | -- | 22915 / 1000 | `P5.field-extrema` | NO_CLAIM |
| `value.f1_stuck_max_run` | CONFIRMED | 22915 samples | -- | 22915 / 1000 | `P5.stuck-run` | NO_CLAIM |
| `value.f0_distinct_observed` | INAPPLICABLE -- a continuous field: a distinct-value set is only meaningful for an enum or a boolean | -- | -- | 0 / 1000 | `P5.distinct-values` | NO_CLAIM |
| `value.f0_lsb` | INAPPLICABLE -- the value never varied, so a quantisation step is meaningless | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_lsb` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 5000 | `P5.quantisation-step` | NO_CLAIM |
| `value.f1_nonfinite` | INAPPLICABLE -- the field carries an integer: it has no non-finite states and its quantisation step is 1 by construction | -- | -- | 0 / 1 | `P5.nonfinite-count` | NO_CLAIM |

## Claims that are not about one sensor

| Claim | Verdict | Value | n / min | Method |
| --- | --- | --- | --- | --- |
| `platform.consistency.skew_spread_across_streams_ppm` | LIKELY | 16.2386 ppm | 23 / 2 | `P8.skew-spread` |

## What is still UNVERIFIED, and what would settle it

This is the reader's to-do list, generated from the claims' own method identifiers. Grouped by method, because a method run once answers every claim under it.

| Method | Claims waiting | Examples |
| --- | --- | --- |
| `P5.field-extrema` | 62 | `0x120.value.f0_max`, `0x120.value.f0_min`, `0x121.value.f0_max` (+59 more) |
| `P6.period-sweep` | 62 | `0x10.control.period_floor_ms`, `0x10.control.period_honoured`, `0x11.control.period_floor_ms` (+59 more) |
| `P6.reconnect-soak` | 62 | `0x10.control.handle_stable_on_reconnect`, `0x10.control.reconnect_stable`, `0x11.control.handle_stable_on_reconnect` (+59 more) |
| `P5.stuck-run` | 47 | `0x120.value.f0_stuck_max_run`, `0x121.value.f0_ever_changed`, `0x121.value.f0_stuck_max_run` (+44 more) |
| `P5.field-mean` | 31 | `0x120.value.f0_mean`, `0x121.value.f0_mean`, `0x121.value.f1_mean` (+28 more) |
| `P6.contention` | 31 | `0x10.control.second_connection`, `0x11.control.second_connection`, `0x110.control.second_connection` (+28 more) |
| `P6.latency-sweep` | 31 | `0x10.control.latency_honoured`, `0x11.control.latency_honoured`, `0x110.control.latency_honoured` (+28 more) |
| `P4.dt-histogram` | 28 | `0x120.timing.delivered_hz`, `0x120.timing.dt_ms`, `0x121.timing.delivered_hz` (+25 more) |
| `P5.distinct-values` | 28 | `0x121.value.f0_distinct_observed`, `0x121.value.f1_distinct_observed`, `0x130.value.f0_distinct_observed` (+25 more) |
| `P4.skew-regression` | 25 | `0x110.timing.clock_skew_ppm`, `0x111.timing.clock_skew_ppm`, `0x112.timing.clock_skew_ppm` (+22 more) |
| `P3.delivery-count` | 16 | `0x120.liveness.samples_per_min`, `0x121.liveness.samples_per_min`, `0x130.liveness.batches_per_min` (+13 more) |
| `P4.batch-arrival` | 16 | `0x120.timing.batch_jitter_ms`, `0x121.timing.batch_jitter_ms`, `0x130.timing.batch_jitter_ms` (+13 more) |
| `P4.monotonicity` | 16 | `0x120.timing.ts_monotonic`, `0x121.timing.ts_monotonic`, `0x130.timing.ts_monotonic` (+13 more) |
| `P4.us-invariant` | 16 | `0x120.timing.ts_us_over_999`, `0x121.timing.ts_us_over_999`, `0x130.timing.ts_us_over_999` (+13 more) |
| `P5.quantisation-step` | 15 | `0x120.value.f0_lsb`, `0x130.value.f0_lsb`, `0x130.value.f1_lsb` (+12 more) |
| `P3.classify` | 14 | `0x120.liveness.classification`, `0x121.liveness.classification`, `0x130.liveness.classification` (+11 more) |
| `P3.delivery-gap` | 14 | `0x120.liveness.longest_gap_ms`, `0x121.liveness.longest_gap_ms`, `0x130.liveness.longest_gap_ms` (+11 more) |
| `P3.batch-histogram` | 12 | `0x121.liveness.samples_per_batch`, `0x130.liveness.samples_per_batch`, `0x131.liveness.samples_per_batch` (+9 more) |
| `P5.nonfinite-count` | 12 | `0x130.value.f0_nonfinite`, `0x130.value.f1_nonfinite`, `0x130.value.f2_nonfinite` (+9 more) |
| `P7.six-face-static` | 9 | `0x10.physical.bias_x_g`, `0x10.physical.bias_y_g`, `0x10.physical.bias_z_g` (+6 more) |
| `P2.stride-watch` | 5 | `0x121.frame.stride_stable`, `0x130.frame.stride_stable`, `0x131.frame.stride_stable` (+2 more) |
| `P7.still-bias` | 4 | `0x20.physical.bias_drift_dps_per_min`, `0x20.physical.zero_rate_x_dps`, `0x20.physical.zero_rate_y_dps` (+1 more) |
| `P7.vs-chest-strap` | 4 | `0x41.physical.err_recovery_bpm`, `0x41.physical.err_rest_bpm`, `0x41.physical.err_walk_bpm` (+1 more) |
| `P7.worn-protocol` | 4 | `0x140.physical.transitions_per_h_in_hand`, `0x140.physical.transitions_per_h_loose`, `0x140.physical.transitions_per_h_table` (+1 more) |
| `P8.fusion-vs-parts` | 4 | `0x130.consistency.carries_magnetometer`, `0x130.consistency.vs_accel_gyro_equal`, `0x130.consistency.vs_accel_gyro_rate` (+1 more) |
| `P8.midnight-crossing` | 4 | `0x52.consistency.reset_value`, `0x52.consistency.resets_at_local_midnight`, `0x61.consistency.resets_at_local_midnight` (+1 more) |
| `P8.raw-ratio` | 4 | `0x10.consistency.vs_0x11_ratio`, `0x10.consistency.vs_0x11_ts_aligned`, `0x20.consistency.vs_0x21_ratio` (+1 more) |
| `P7.open-sky-soak` | 3 | `0x110.physical.cep50_m`, `0x110.physical.cep95_m`, `0x110.physical.precision_vs_scatter` |
| `P7.ttff` | 3 | `0x110.physical.ttff_cold_s`, `0x110.physical.ttff_hot_s`, `0x110.physical.ttff_warm_s` |
| `P8.barometric-solve` | 3 | `0x90.consistency.reference_is_field1`, `0x90.consistency.reference_pressure_pa`, `0x90.consistency.vs_pressure_formula_m` |
| `P8.restart-watch` | 3 | `0x51.consistency.survives_app_restart`, `0x60.consistency.survives_app_restart`, `0xe0.consistency.survives_app_restart` |
| `P2.is-data-valid` | 2 | `0x130.frame.parser_accepts_frame`, `0x131.frame.parser_accepts_frame` |
| `P2.stride-arithmetic` | 2 | `0x130.frame.field_count`, `0x131.frame.field_count` |
| `P2.stride-vs-parser` | 2 | `0x130.frame.parser_agreement`, `0x131.frame.parser_agreement` |
| `P3.connect-to-first` | 2 | `0x130.liveness.first_sample_ms`, `0x131.liveness.first_sample_ms` |
| `P7.closed-loop` | 2 | `0x112.physical.loop_err_pct`, `0x112.physical.vs_haversine_pct` |
| `P7.counted-100` | 2 | `0x51.physical.count_bias_pct`, `0x51.physical.count_spread_pct` |
| `P7.counted-flights` | 2 | `0x60.physical.floor_count_err`, `0x90.physical.stairwell_diff_err_m` |
| `P7.labelled-activity` | 2 | `0xb0.physical.confusion_accuracy_pct`, `0xc0.physical.confusion_accuracy_pct` |
| `P7.twenty-raises` | 2 | `0xa0.physical.detect_latency_ms`, `0xa0.physical.detect_rate_pct` |
| `P8.gauge-vs-coulomb` | 2 | `0x120.consistency.vs_metrics_capacity_pct`, `0x122.consistency.current_sign_convention` |
| `P8.strap-arbitration` | 2 | `0x43.consistency.optical_reports_with_strap`, `0x43.consistency.source_switches_to_external` |
| `P7.indoor-check` | 1 | `0x110.physical.reports_indoor_fix` |
| `P7.manual-360` | 1 | `0x20.physical.rotation_err_pct` |
| `P7.negative-armwave` | 1 | `0x51.physical.fp_per_min_armwave` |
| `P7.negative-handwash` | 1 | `0x51.physical.fp_per_min_handwash` |
| `P7.negative-vehicle` | 1 | `0x51.physical.fp_per_min_vehicle` |
| `P7.raw-vs-scaled` | 1 | `0x11.physical.raw_to_g_scale` |
| `P7.timed-distance` | 1 | `0x111.physical.speed_err_pct` |
| `P7.vs-surveyed-point` | 1 | `0x90.physical.vs_surveyed_point_m` |
| `P7.vs-thermometer` | 1 | `0x70.physical.vs_room_thermometer_c` |
| `P7.vs-weather-station` | 1 | `0x80.physical.vs_station_qnh_pa` |
| `P8.cadence-vs-events` | 1 | `0x53.consistency.vs_step_detector_rate` |
| `P8.counter-vs-events` | 1 | `0x51.consistency.vs_step_detector_count` |
| `P8.daily-vs-samples` | 1 | `0x42.consistency.vs_day_hr_samples` |
| `P8.grade-derivation` | 1 | `0x150.consistency.vs_altimeter_and_gps` |
| `P8.hr-vs-hrex` | 1 | `0x41.consistency.vs_hrex_arbitrated` |
| `P8.profile-diff` | 1 | `0x80.consistency.sea_level_stable_across_runs` |
| `P8.reboot-watch` | 1 | `0x51.consistency.resets_on_device_reboot` |
| `P8.sea-level-watch` | 1 | `0x80.consistency.sea_level_is_constant` |
| `P8.timezone-change` | 1 | `0x52.consistency.survives_timezone_change` |
| `P8.two-clock-regression` | 1 | `platform.consistency.uptime_vs_wall_drift_ppm` |
| `P8.worn-vs-movement` | 1 | `0x140.consistency.worn_vs_accel_movement` |

## Limitations of this document

- **One device.** Everything here was measured on a single physically-owned watch. Nothing distinguishes a property of the platform from a property of this unit.
- **One firmware.** The profile is named with its firmware version for that reason. `profile_diff.py` is what turns two of these into a change list.
- **No reference instruments in this run.** Layer 7 -- the guided protocols against a chest strap, a weather station, a surveyed point, a counted flight of stairs -- is what would give any of these values an external check. Rows waiting on it appear in the to-do list above.
- **The simulator sources nothing here.** It has four sensor sources, and its sample-rate adapter thins delivery in a way the hardware demonstrably does not. Every sensor claim above came from the device.
- **Reading cannot confirm.** Rows marked `LIKELY` were inferred from another measurement or from documentation that is itself unverified. Rows marked `CONFIRMED` were measured on this hardware, with the sample count and method stated.
