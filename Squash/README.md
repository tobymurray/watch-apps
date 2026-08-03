# Squash — an activity app, and the recorder it is being built out of

A squash activity app for the UNA Watch. Right now it is the Workout app's
session handling — start/pause/resume/save, live HR and zones, calories, laps,
FIT recording with crash recovery — plus one thing Workout has not got: a
research recorder that streams the raw 100 Hz IMU to CSV.

**It does not detect shots yet.** That is deliberate, and it is worth being
clear about why, because the ordering is the whole point.

## Why a recorder before any metric

Shot detection, stroke classification, swing speed, rally structure — none of
these can be tuned without real, labelled squash recordings, and there is no
public corpus of wrist IMU data from a squash court. So the app ships the means
to collect that data before it ships anything derived from it.

The format matters as much as the data. Recordings are byte-compatible with the
SDK simulator's `Sensor::ImuFusionSource` playback parser, so one session
recorded on the watch replays through the simulator *and* feeds host tests as a
fixture, unchanged. That is the development loop for every later tier: record on
court, replay at a desk, assert in a test.

Two facts about the sensor that any metric built on this has to respect:

- **Both IMU ranges saturate during real strokes.** Accel is ±8 g (4096 LSB/g)
  and gyro ±2000 dps (16.4 LSB/dps) on the BMI270; adult wrist rotation and
  impact shock exceed both. Clipping is signal, not noise — time-spent-saturated
  is a usable intensity feature, and peak-based metrics silently rail.
- **The watch is on the wrist, not the racquet.** Head speed is a proxy at best.
  Anything presented as "speed" is a relative index for one player, never m/s.

Values are recorded in **raw sensor LSB**, unscaled, so the recording keeps the
saturation rather than hiding it behind a conversion.

## Turning the recorder on

There is no on-watch toggle yet — adding one means a TouchGFX Designer change.
It is enabled the same way Barcode takes its id: by writing a file over USB.

1. Run the app once so it writes its settings file, then connect by USB.
2. Open the app's folder on the watch and edit the settings JSON.
3. Set `"imu_research_en": true`.

The flag survives the app's own settings screens — both of them read, modify and
write the whole settings struct, so a GUI with no toggle for it will not clear
it. A settings file written before the field existed reads as `false`.

## What you get

Recordings land under `Imu/YYYYMM/imu_YYYYMMDDTHHMMSS.csv`, next to but
deliberately outside `Activity/`: they are research inputs, not workouts, and
should not ride along with whatever syncs the activity tree. The name matches
the session's `.fit` so the two are easy to pair.

```
t_ms,ax,ay,az,gx,gy,gz
0,102,-198,4103,301,-402,498
10,105,-203,4098,318,-411,502
```

`t_ms` is relative to the first recorded sample and comes from the sensor's own
timestamps, so the cadence in the file is the sensor's, not the message loop's.

Budget honestly: a row is ~44 bytes typical, 53 worst case, so 100 Hz costs
~4.3 KiB/s and 30 minutes is ~7.9 MB. Two hard caps stop it there — 8 MB and 30
minutes by default — and a row is only written if its worst case still fits, so
the file never crosses the budget mid-row. The caps are self-defence, not device
awareness: the SDK exposes no free-space query, so headroom on the activity
partition is still unconfirmed on hardware.

A recording is closed out on discard as well as save. Throwing away the session
does not make the samples less real.

## Building

Needs `$UNA_SDK` pointing at an SDK checkout:

```sh
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/Squash-CMake
mkdir -p build && cd build
cmake .. -DBUILD_VERSION=1.0.0 && make -j"$(nproc)"
```

The `.uapp` lands in `build/`. Building the target ELFs needs an
`arm-none-eabi` toolchain that provides the newlib syscall stubs — ST's GNU
Tools for STM32 (what the SDK's own CI uses) works; a stock Ubuntu
`gcc-arm-none-eabi` fails at link on `_write`/`_read`/`_lseek`/`_close`.

For the Linux simulator instead:

```sh
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/TouchGFX-GUI
make -f simulator/gcc/Makefile -j"$(nproc)"
SDL_VIDEODRIVER=dummy ./build/bin/simulator.out
```

Key `6` in the simulator queues a synthetic racquet swing, and
`IMU_FUSION_SIM_CSV_PATH` in `simulator/ConfigurationSimulator.hpp` replays a
recorded CSV instead.

## Licence

MIT. Derived from the UNA SDK's MIT-licensed Workout example, whose copyright
notice is retained in the files that came from it.
