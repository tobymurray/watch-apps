# Squash — an activity app, and the recorder it is being built out of

A squash activity app for the UNA Watch. Right now it is the Workout app's
session handling — start/pause/resume/save, live HR and zones, calories, laps,
FIT recording with crash recovery — plus one thing Workout has not got: a
research recorder that streams the raw 100 Hz IMU to CSV.

It also carries an engine for rally structure and heart-rate recovery, and a
profile that remembers what past sessions measured — **none of which displays
anything**, because no recording exists to set a threshold with. See
[what it does not show, and why](#what-it-does-not-show-and-why).

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

There is no on-watch toggle — adding one means a TouchGFX Designer change.
`recordImu` is a **declared configuration field**: the app names it in
[`app-manifest.json`](app-manifest.json), the phone renders a switch for it, and
the answer is written to `input.json` in the app's own folder. Tick **Record raw
IMU** on the app's card and that is the whole of it.

Over USB instead:

1. Connect the watch and wait for the drive to mount.
2. Open `Apps/Squash/` on it — the same folder the `.uapp` lives in.
3. Create `input.json`:

   ```json
   {
     "schema": 1,
     "values": {
       "recordImu": true
     }
   }
   ```

[`input.example.json`](input.example.json) is that file, ready to copy.

A JSON `true` or `false`, not a word: the field is declared `bool`, and
`SDK::AppConfig` treats a value of the wrong type as absent, so `"on"` reads as
off. Off is the default and the safe direction for a flag whose only effect is
to start filling flash. The file is re-read at the start of every session, so
flipping it takes effect on your next activity without reinstalling or even
restarting the app.

**Not `settings.json`.** That file is the app's own and is rewritten whole every
time a setting changes on the watch, so a key the app did not put there would not
survive. An early version shipped the flag there and was unusable for exactly
that reason: an install had no such file, and nothing but hand-editing could make
one. Keeping externally-written data in its own file also makes "this came from
outside, validate it" a property of the filename.

Reading it is `SDK::AppConfig`'s job, not this app's. It is bounded on purpose —
a size ceiling, a `schema` that must match exactly, per-field type checking and
clamping, and a fall back to the declared default on every failure. A config
file somebody else wrote must never stop the app starting. The binary carries
its own copy of the field table in
[`AppConfigFields.cpp`](Software/Libs/Sources/AppConfigFields.cpp), because
`app-manifest.json` never reaches the watch; CI checks the two agree.

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

Two sidecars sit beside it, sharing that clock exactly — all three are begun
from the same sensor tick, so a row in any of them with the same `t_ms` is the
same instant, and nothing has to be correlated:

| File | What it carries |
| --- | --- |
| `imu_<stamp>_events.csv` | `t_ms,seq,kind` — every R2 press. A press is a boundary; what the stretch between two of them *was* comes from the labels file you write, since the watch has one button and cannot say what a press meant. |
| `imu_<stamp>_hr.csv` | `t_ms,bpm_x100,trust,source,optical_x100,external_x100` — the arbitrated heart rate and both per-source readings behind it. |

Hundredths of a bpm, as an integer, because there is no float formatter here and
whole-bpm rounding would discard what the file exists to collect: consecutive
readings have been measured differing by **0.50 and 0.18 bpm** over two real
rides, which is what kernel smoothing looks like and what tells a physiological
fall from a filter settling.

Both per-source columns are kept alongside the arbitrated reading so that
"is wrist optical usable during play?" is answerable from one recording rather
than two — the same reason the FIT file carries `hr_source`, `hr_optical` and
`hr_external` rather than only the beat.

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

## Tests

Host tests for the recorder path, in [`Tests`](Tests):

```sh
export UNA_SDK=/path/to/una-sdk
cmake -S Tests -B Tests/build -DCMAKE_BUILD_TYPE=Debug
cmake --build Tests/build -j"$(nproc)"
cd Tests/build && ctest --output-on-failure
```

`squash-recorder-tests` covers `ImuCsvRecorder`'s byte format and its size and
duration caps against an in-memory sink, and needs nothing but GoogleTest.

There is no suite here for reading the config file. That is `SDK::AppConfig`,
which the SDK tests in `Tests/Host/appconfig/`; a copy of those assertions in
this tree would only test the SDK twice and rot separately.

`squash-hrlog-tests` covers `HrCsvLog` the same way, against the same in-memory
sink.

`squash-filesink-tests` asserts the round trip — recorder to storage through
`SDK::Kernel`, then back out through the simulator's `ImuFusionSource` playback
parser — so it needs an SDK checkout carrying the IMU fusion sensor source. On
an SDK without it the suite is skipped at configure time with a message rather
than failing the build.

**Getting an SDK that has it**, along with the `SDK::AppConfig` the simulator
needs and the `apps-v1.4.0` tag does not, is one cherry-pick;
[`Tools/docker-build.sh`](Tools/docker-build.sh)'s header carries the recipe and
drives all four builds:

```sh
Tools/docker-build.sh tests   # host tests
Tools/docker-build.sh rust    # both crates: test, clippy, and the watch target
Tools/docker-build.sh sim     # the TouchGFX simulator
Tools/docker-build.sh app     # the .uapp
```

The Rust crates are `EffortKit` and `Squash/Software/Libs/rust`, and their tests
run with plain `cargo test --features std` — no container and no SDK.

## The engine, and the profile

The rally/rest segmenter, the recovery analyser and the cross-session baselines
live in [`EffortKit`](../EffortKit), a shared `no_std` Rust crate — shared
because [`Spin`](../Spin/Docs/RECOVERY-PROMPT.md) asks for the same recovery and
the same cross-session record, and because `SleepLab` already grew its own copy
of the baseline machinery for want of anywhere to put the first one.
`Software/Libs/rust` is this app's C ABI over it, linked into the **Service**
ELF — the first Rust in a Service in this repository, though the GUI-side apps
had already established the toolchain.

The wearer's own file is `profile.json` in the app's directory, separate from
`settings.json` (which the watch rewrites whole whenever a setting changes) and
`input.json` (which comes from the phone). It holds the last twenty sessions and
is committed through a temporary and a rename, so a battery pull mid-write
leaves the previous one intact. Absent, damaged, or from a schema this build
does not read, it is a warm-up and never a reason not to start. The schema is a
table with units in [EffortKit's README](../EffortKit/README.md#the-profile-file).

Baselines are derived from those stored sessions on load, never persisted, so
changing how a figure is computed applies to the whole history instead of
orphaning it.

## When something went wrong

`LOG_INFO` needs a UART capture and a dev tool attached to the watch, which
nobody has on court. So two files are written to the volume instead, and they
are the first place to look:

| File | What it holds |
| --- | --- |
| `Debug/squash.log` | One line per event that changes what a session contains — the profile that loaded, whether the two languages agree about their shared structs, whether a calibration exists, and how the recording and its sidecars ended. |
| `Debug/sessions.csv` | One row per saved session: every field the profile stores, plus the recorder's own counters. Load it into something else to ask whether a metric is behaving across sessions. |

A fresh install's log reads:

```
0 1788409442 launch v<version> abi=3384192379 expect=3384192379 ok=1 calibration=0
10 1788409442 profile load=1 sessions=0
```

`ok=1` is the two languages agreeing about `SquashSessionRecord`'s layout,
`calibration=0` is there being no threshold in this build, and `load=1` is
`Load::ABSENT` — no profile yet, which is what a first run should say.

Both files restart at 64 KiB with a line saying they did. An unbounded
diagnostic that fills the volume the recording then cannot be written to would
be a poor trade.

## What it does not show, and why

Nothing from the engine reaches the screen, the FIT file or the summary. Not
because it is unfinished, but because there is no recording to defend a number
with.

Every threshold in `EffortKit` is behind a type that cannot be constructed
without naming the recordings that set it, and **no such constant exists in this
repository**. So `segment::Calibration::Absent` and
`window::Calibration::absent()` are the only values the watch build can reach,
every cessation is counted `not_calibrated` rather than measured, and a saved
session records `segmented: 0` — zero because nothing ran, not because nothing
happened.

The heart-rate figures are real, because they need no threshold, which makes the
heart-rate baselines the one family that can start warming up today. No
comparison is offered until five qualifying sessions exist, and below that the
raw measurement is shown with the comparison reported as unavailable rather than
a percentile invented from three.

This app will never tell you:

- **How far you moved, in metres.** Wrist dead reckoning on a 6.4 m court is not
  credible, and one fabricated number discredits the honest ones beside it.
- **Who won a point.** A wrist IMU cannot know.
- **How you compare to anyone else.** Every baseline here is your own.
- **A recovery figure from a rest that did not qualify.** It is not a small
  number; it is not a measurement.

[`Docs/PHASE-A.md`](Docs/PHASE-A.md) is the verdict and the method,
[`Docs/RECORDING-PROTOCOL.md`](Docs/RECORDING-PROTOCOL.md) is the nine
recordings that would change it, and
[`Docs/FEASIBILITY-LEDGER.md`](Docs/FEASIBILITY-LEDGER.md) has one row per
metric. A metric with no row does not ship; a row that does not say validated is
not displayed.

## Licence

MIT. Derived from the UNA SDK's MIT-licensed Workout example, whose copyright
notice is retained in the files that came from it.
