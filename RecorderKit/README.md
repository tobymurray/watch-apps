# RecorderKit — putting a raw sensor session on the volume

The four classes an app needs to record a session of raw 100 Hz IMU, the labels
a wearer put on it, and the heart rate that went with it. Extracted from
`Squash` when a second app — `SquashLab` — needed to write the same three files
in the same format.

## The three files, and why they are three

```
imu_<stamp>.csv          t_ms,ax,ay,az,gx,gy,gz
imu_<stamp>_events.csv   t_ms,seq,kind
imu_<stamp>_hr.csv       t_ms,bpm_x100,trust,source,optical_x100,external_x100
```

All three share one clock, begun from the same sensor tick, so a marker row and
a sample row carrying the same `t_ms` are the same instant and nothing has to be
correlated afterwards.

They are three files rather than three columns because **the sample file's
format is load-bearing**: it is byte-compatible with the SDK simulator's
`Sensor::ImuFusionSource` playback parser, so a session recorded on court
replays through the simulator and lands in a host test as a fixture, unchanged.
A seventh column would break that round trip for the sake of a field that is
empty on 99.99% of rows.

| | |
|---|---|
| `ImuCsvRecorder` | the samples, the caps, the row format |
| `ImuMarkerLog` | what the wearer said they were doing, and from when |
| `HrCsvLog` | the heart rate on the recording's own clock |
| `ImuFileSink` | the one that knows about `SDK::Kernel` |

Three of the four are SDK-free and write to an injected `ISink`. That is what
lets the same code write to the watch and to a memory buffer in a host test, and
it is why `ImuFileSink` exists at all: so the other three do not have to know
where bytes go.

## The marker `kind` column is a frozen wire format

`ImuMarkerLog::Kind` values are written into the file and read back by mapping
them onto `effortkit::fixture::Label`. `MANUAL` is 0 and stays 0, so every
recording made before the watch could label reads back as unlabelled markers
rather than as rallies. A wrong number here is silent and permanent.

An app with its own vocabulary — a drill protocol, say — takes values above the
ones defined here rather than redefining them, so one reader can tell a match
recording from a drill recording without being told which it is looking at.

## Using it

```cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../RecorderKit/recorderkit.cmake)
# then fold ${RECORDERKIT_SOURCES} into SERVICE_SOURCES
# and   ${RECORDERKIT_INCLUDE_DIRS} into SERVICE_INCLUDE_DIRS
```

## Tests

```sh
export UNA_SDK=/path/to/una-sdk
cmake -S Tests -B Tests/build -DCMAKE_BUILD_TYPE=Debug
cmake --build Tests/build -j"$(nproc)"
cd Tests/build && ctest --output-on-failure
```

`recorderkit-recorder-tests`, `recorderkit-marker-tests` and
`recorderkit-hrlog-tests` need nothing but GoogleTest, because the classes they
cover take a sink.

`recorderkit-filesink-tests` asserts the round trip — recorder to storage
through `SDK::Kernel`, then back out through the simulator's `ImuFusionSource`
playback parser — so it needs an SDK checkout carrying the IMU fusion sensor
source. That is not in the SDK mainline; on a mainline SDK the suite is skipped
at configure time with a message rather than failing the build.
