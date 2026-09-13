# Squash — an instrument for collecting labelled squash recordings

A research recorder for the UNA Watch. It streams the wrist sensor's raw 100 Hz
IMU and your heart rate to files, shows you what the sensor is seeing while you
play, and lets you say what you are doing — rally, rest, off court, warm-up,
drill, idle — with a button, so a recording arrives already annotated.

**It writes no activity file and exports nothing.** Not an oversight: the app is
the instrument that collects the data a squash metric would be built from, and
anything else it did would be a second thing to maintain and a second thing to
be wrong.

## Why a recorder before any metric

Shot detection, stroke classification, swing speed, rally structure — none can
be tuned without real, labelled squash recordings, and there is no public corpus
of wrist IMU data from a squash court. So the app ships the means to collect
that data before it ships anything derived from it.

The format matters as much as the data. Recordings are byte-compatible with the
SDK simulator's `Sensor::ImuFusionSource` playback parser, so one session
recorded on the watch replays through the simulator *and* feeds host tests as a
fixture, unchanged. That is the development loop for every later tier: record on
court, replay at a desk, assert in a test.

Four facts about the sensor that any metric built on this has to respect:

- **Both IMU ranges saturate during real strokes.** Accel is ±8 g (4096 LSB/g)
  and gyro ±2000 dps (16.4 LSB/dps) on the BMI270. Measured over the 1,800
  epochs of `Tests/pulled/20260913-v0.6.0-70min-match`: **13.8% of epochs had an
  accelerometer axis railed and 3.9% a gyroscope axis**, with one epoch at 22% of
  its samples railed. Clipping is signal, not noise — time-spent-saturated is a
  usable intensity feature, and peak-based metrics silently rail. Re-derive with
  `cargo run --features std --bin phase-a`.
- **The watch is on the wrist, not the racquet.** Head speed is a proxy at best.
  Anything presented as "speed" is a relative index for one player, never m/s.
- **100 Hz means a 50 Hz Nyquist, and an impact is faster than that.** The
  transient of ball on strings has energy above 50 Hz, so it aliases rather than
  being resolved. Band-limited features — a gyro envelope, jerk magnitude over a
  window — survive that; a single-sample peak is reading an alias. This one is
  arithmetic and physics, not something a recording here measured.
- **Nothing records which wrist the watch was on.** Per-shot features only exist
  if it is the racquet wrist; on the off wrist the same recording is movement
  and heart rate and nothing else. The app does not ask, so every recording in
  `Tests/pulled` is implicitly one wearer on one wrist and carries no field
  saying which. A corpus that ever holds more than one wearer needs this
  answered before it is worth pooling.

Values are recorded in **raw sensor LSB**, unscaled, so the recording keeps the
saturation rather than hiding it behind a conversion.

## The screen

Six screens, drawn by a `no_std` Rust renderer that is a pure function of one
struct. Look at all of them without a watch:

```sh
cd Software/Apps/CustomGUI/rust
cargo run --features preview --bin preview -- /tmp/squash-screens
```

The one that matters is PROFILE, and what is on it is what the recording needs
you to know:

| Row | Why it is there |
|---|---|
| `● REC`, or why it stopped | A cap stops the samples while the session runs on. Nothing said so on 2026-09-13 and 40 minutes of a match went unrecorded. |
| The **label**, largest thing on the screen | The one fact you are asserting, and the only one no later analysis can recover. |
| `HELD m:ss` | A label left on by accident shows as an implausible duration instead of having to be remembered. |
| Recorded clock, cap bar, `N MIN LEFT` | Recorded seconds, not elapsed. They are the same number until a cap trips. |
| `GYRO` / `ACCL` bars | Whether the sensor is seeing a stroke or the subscription is dead. |
| `SAT n%` | Saturation is signal on this hardware, so it is reported rather than hidden. |
| Heart rate, dim when untrusted | The recorder keeps untrusted readings either way; dim says which. |
| Markers and megabytes | Checked between games rather than during one. |

The bars are laddered by a table in [`lib.rs`](Software/Apps/CustomGUI/rust/src/lib.rs),
not a logarithm, so the scale can be read off the source. The features behind
them are computed by [`EffortKit`](../EffortKit)'s `epoch` module through the
shim in [`Software/Libs/rust`](Software/Libs/rust) — **the same code `phase-a`
runs over the file afterwards**, so the screen cannot disagree with the
analysis.

## Buttons

CLICK only, on every screen. Nothing depends on a long press arriving.

| Screen | L1 | L2 | R1 | R2 |
|---|---|---|---|---|
| Ready | | | **START** | **EXIT** |
| Profile | rally ⇄ rest | label picker | pause | **MARK** |
| Label picker | | next | **SET** | cancel |
| Paused | **SAVE** | **DISCARD** | resume | |
| Saved / discarded | | | **DONE** | |

L1 toggles rally and rest because that is the pair a match alternates between,
so the common change is one press. The other four states are behind L2.

## Labelling, and why there is no labels file

A label change writes a marker, and the label *is* the marker's `kind` column.
The format does not change — same three files, same `t_ms,seq,kind`, still
byte-compatible with the playback parser — because that column was reserved for
exactly this.

```
t_ms,seq,kind
0,1,1          <- rally, from the first sample
18420,2,2      <- rest
41200,3,1      <- rally again
```

A label holds until the next marker that changes it, so N markers delimit the
stretches and you press once per change rather than once per stretch. `kind 0`
is R2's plain marker: "note this instant", asserting no state, and it does not
end the stretch it sits in.

`effortkit::fixture` reads those intervals straight off the markers, so a
recording made this way needs no `_labels.txt`. Recordings made before the watch
could label carry kind 0 throughout and read back **unlabelled** rather than as
rallies — which is why MANUAL stayed 0. A hand-written `imu_<stamp>_labels.txt`
still wins when present; it is the only way to label those older recordings, and
the only way to correct a session you mislabelled on court.

## Turning it on

There is no on-watch toggle: recording is declared configuration, so the phone
renders the form. Tick **Record raw IMU** on the app's card and that is the whole
of it.

Over USB instead, create `input.json` in `Apps/Squash/` on the watch —
[`input.example.json`](input.example.json) is that file, ready to copy:

```json
{
  "schema": 1,
  "values": {
    "recordImu": true,
    "maxMinutes": 90,
    "maxMegabytes": 32
  }
}
```

A JSON `true` or `false`, not a word: the field is declared `bool`, and
`SDK::AppConfig` treats a value of the wrong type as absent, so `"on"` reads as
off. The file is re-read at the start of every session, so a change takes effect
on your next session without reinstalling or restarting.

**Not `settings.json`.** That file is the app's own and is rewritten whole every
time, so a key the app did not put there would not survive. Keeping
externally-written data in its own file also makes "this came from outside,
validate it" a property of the filename. Reading it is `SDK::AppConfig`'s job —
a size ceiling, a `schema` that must match exactly, per-field type checking and
clamping, and a fall back to the declared default on every failure. A config
file somebody else wrote must never stop the app starting. The binary carries
its own copy of the field table in
[`AppConfigFields.cpp`](Software/Libs/Sources/AppConfigFields.cpp), because
`app-manifest.json` never reaches the watch; CI checks the two agree.

### The caps

**90 minutes and 32 MB**, and both are settings because the right value is the
session you are about to record, which the binary cannot know.

30 minutes was the old default, set on the belief that it was "about one squash
match's worth of play". The 2026-09-13 session ran 4,233 s — 70.5 minutes — and
stopped on that cap with 40 minutes unrecorded, so the belief was wrong by more
than a factor of two. The size cap is matched to the duration cap: that
recording wrote 179,158 samples in 6,650,901 bytes, or 37.1 B/row, so 90 minutes
is ~22 MB.  A host test asserts the two caps stay matched.

They are self-defence limits, not device-aware ones: the SDK exposes no
free-space query. The watch had 1.6 GB free on 2026-09-13, so the partition is
not what either cap is protecting.

## What you get

Recordings land under `Imu/YYYYMM/`, deliberately outside `Activity/`: they are
research inputs, and should not ride along with whatever syncs the activity
tree.

```
imu_20260913T122111.csv          t_ms,ax,ay,az,gx,gy,gz
imu_20260913T122111_events.csv   t_ms,seq,kind
imu_20260913T122111_hr.csv       t_ms,bpm_x100,trust,source,optical_x100,external_x100
```

All three share one clock, begun from the same sensor tick, so a marker row and
a sample row with the same `t_ms` are the same instant and nothing has to be
correlated. `t_ms` comes from the sensor's own timestamps, so the cadence in the
file is the sensor's, not the message loop's.

A recording is closed out on discard as well as save. Throwing away the session
does not make the samples less real.

## Reading one back

```sh
cd ../EffortKit
cargo run --features std --bin phase-a -- ../Squash/Tests/pulled/*/imu_*.csv \
    --report /tmp/phase-a.md --epochs /tmp/epochs.csv
```

A1 asks what the heart-rate signal is doing. A2 asks whether the movement states
are separable, and answers with distributions and an overlap rather than a
threshold — which is why the labels are the point. Until A2 has run,
`effortkit::segment` has no constructible calibration and reports
`NotCalibrated`: nothing in this repository decides what a rally is except the
wearer, with a button.

## Building

Needs `$UNA_SDK` pointing at an SDK checkout, `cargo` with the
`thumbv8m.main-none-eabihf` target, and an `arm-none-eabi` toolchain that
provides the newlib syscall stubs — ST's GNU Tools for STM32 (what the SDK's own
CI uses) works; a stock Ubuntu `gcc-arm-none-eabi` fails at link on
`_write`/`_read`/`_lseek`/`_close`.

```sh
export UNA_SDK=/path/to/una-sdk
cd Software/Apps/Squash-CMake
cmake -B build -DBUILD_VERSION=1.0.0 . && cmake --build build -j"$(nproc)"
```

The `.uapp` lands in `build/`. CMake drives both cargo builds; the GUI's crate
needs `--features device`, which is what turns on PanicKit's `#[panic_handler]`.

Two Rust archives, two ELFs, on purpose: `libsquash_gui.a` in the GUI and
`libsquash_engine.a` in the Service, because two archives each carrying a
`#[panic_handler]` collide if they land in one binary. Each computes an FNV-1a
fingerprint over its own struct layout — `offsetof` on the C++ side,
`offset_of!` on the Rust side — and the process refuses to start on a
disagreement. A literal copied from one side to the other would agree forever
and catch nothing.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk
cmake -S Tests -B Tests/build -DCMAKE_BUILD_TYPE=Debug
cmake --build Tests/build -j"$(nproc)"
cd Tests/build && ctest --output-on-failure
```

`squash-recorder-tests` covers `ImuCsvRecorder`'s byte format, its caps, and the
span it reports; `squash-marker-tests` covers the sidecar. Both need nothing but
GoogleTest.

`squash-filesink-tests` asserts the round trip — recorder to storage through
`SDK::Kernel`, then back out through the simulator's `ImuFusionSource` playback
parser — so it needs an SDK checkout carrying the IMU fusion sensor source. That
is not in the SDK mainline yet; on a mainline SDK the suite is skipped at
configure time with a message rather than failing the build.

There is no suite here for reading the config file. That is `SDK::AppConfig`,
which the SDK tests in `Tests/Host/appconfig/`; a copy of those assertions in
this tree would only test the SDK twice and rot separately.

The renderer has its own:

```sh
cd Software/Apps/CustomGUI/rust
cargo test --features std      # unit tests, and a byte hash of every screen
```

A changed screen hash is not a failure to silence: look at the screen with
`preview`, decide whether the change was wanted, and regenerate
`tests/scene-goldens.txt` if it was.

## Licence

MIT. Derived from the UNA SDK's MIT-licensed Workout example, whose copyright
notice is retained in the files that came from it.
