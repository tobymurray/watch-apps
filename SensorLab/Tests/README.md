# SensorLab host tests

```sh
export UNA_SDK=/path/to/una-sdk           # apps-v1.4.0
cmake -S SensorLab/Tests -B SensorLab/Tests/build -DCMAKE_BUILD_TYPE=Debug
cmake --build SensorLab/Tests/build -j
cd SensorLab/Tests/build && ctest --output-on-failure
```

or, in the container the app is built in:

```sh
UNA_SDK=/path/to/una-sdk SensorLab/Tools/docker-build.sh tests
```

Four suites, 113 tests, about four seconds.

| Suite | What it covers | SDK needed |
| --- | --- | --- |
| `sensorlab-core-tests` | The evidence core: the catalogue, the claim store's verdict rules, the completeness arithmetic, the histograms and every per-field statistic. | none |
| `sensorlab-catalogue-current` | `Tools/gen_catalogue.py --check` against the configured SDK: the committed type table is not stale. | headers only |
| `sensorlab-pipeline-tests` | Whole runs through the real `Service`, by scripting the kernel message queue. | test doubles + coreJSON |
| `sensorlab-report-roundtrip` | The real writers produce files the real python tools read, and `profile_diff.py` finds a change it was given. | as above + python3 |

---

## What these tests are, and are not, evidence about

**Nothing here is evidence about a sensor.** The streams are generated, so a
scenario proves that the code computes what it claims to compute from a stream of
a known shape, and that is all it proves. Every sensor claim in
`Docs/LEDGER.md` comes from hardware or does not exist, and the profile's own
verdict rules enforce that: a claim recorded with `Source::SpecRead` can be
REFUTED and can never be CONFIRMED, which is `Evidence_test.cpp`'s
`ASpecReadCannotProduceAConfirmedRow`.

What they replace is the desk-answerable half of the work, which is most of the
code and none of the findings: the statistics, the promotion rules, the file
formats, the burst contract, the resume path, the wrap arithmetic.

---

## `sensorlab-core-tests` — zero SDK headers, by construction

The catalogue, the claim store and the statistics include no SDK header at all,
so this target links against GoogleTest and nothing else and runs anywhere. That
is not tidiness: it is what lets every verdict rule be argued about at a desk,
against fixtures whose answers are known.

`Stats_test.cpp` is the file to read first, because **it is the reason to believe
any number this app ever prints**. It contains the six streams the implementation
prompt asked for, each asserting the *answer* rather than merely that nothing
crashed:

- a stream at exactly 20 ms → 50 Hz, every quantile the same bin;
- one with an injected 4 s gap → **the median is untouched and the longest gap is
  exact**, which is the whole reason a rate is never reported without its gap;
- one whose timestamp goes backwards → counted, never corrected, and contributing
  no dt;
- one quantised to a known LSB → the recovered step is that LSB, from below;
- one that never changes → no LSB reported at all, and the whole run as a stuck
  run;
- one containing a NaN → counted, excluded from the count *and* the sum, and the
  mean survives.

Plus a synthetic uptime wrap, because `getTimeMs()` wraps at ~49.7 days and
nobody has observed it. This app cannot wait for it and does not have to.

`Evidence_test.cpp` is §9 of the implementation prompt turned into assertions:
no claim without a run, no distribution below its minimum n, a negative result is
a result, INAPPLICABLE is excluded from the denominator and UNVERIFIED is not,
reading can refute and cannot confirm.

---

## `sensorlab-catalogue-current` — the alarm on the generated table

`Software/Libs/Header/Catalogue/SensorTypeTable.generated.hpp` is generated from
`SensorTypes.hpp` and the 29 parser headers, and **committed**. Committed because
the TouchGFX simulator's Makefile has no place to run a generator and requiring
python3 for every build would be worse than a checked-in artifact.

The cost of committing it is that it can fall behind the headers it came from —
which is precisely what `Docs/SensorsLayer.md` did, and the reason the table is
generated at all. So this test re-runs the parse against the SDK the tests were
configured with and fails if the committed file is not what it would write now.

It fails loudly on a new sensor type, a renamed parser, a changed field count, or
a parser appearing for one of the five types that currently ship none. **Every
one of those is news rather than noise.** To fix a failure:

```sh
UNA_SDK=... SensorLab/Tools/docker-build.sh catalogue
```

and commit the result — and open a `Docs/FINDINGS.md` row if the change is a
divergence rather than an addition.

Skipped, not failed, without python3.

---

## `sensorlab-pipeline-tests` — the highest-leverage thing here

`RunHarness.hpp` scripts the kernel message queue and drives whole runs through
the real `Service`. Read its header comment for why the alternatives were
rejected; the short version is that the loop is one of the things under
suspicion, so a test that replaced the loop could not find a bug in the loop.

A run costs about 70 ms. The 41 pipeline scenarios include a type with no producer, an
event sensor that speaks once in an hour, a frame wider than its parser, a frame
*narrower* than its parser, a handle above 255, a kernel that will not name its
firmware, a stuck field, a NaN stream, a microsecond field over 999, a run
truncated by the cable, a run left open by a crash, a device reboot, a volume
that fills mid-run, four settings failure modes, and a whole run across the
uptime wrap compared against its twin away from it.

Three of those exist because of a specific defect in something this app does not
control, and each is documented in `Docs/FINDINGS.md`:

- `AHandleAboveTwoHundredAndFiftyFiveIsNotTruncated` — `SDK::Sensor::Connection`
  stores the handle as a `uint8_t` (§4);
- `AFrameNarrowerThanItsParserIsAlsoRecordedRatherThanParsedBlind` —
  `GpsLocation::isDataValid()` reads a field before checking the count (§3);
- `EveryNumberIsWrittenWithoutTouchingAFloatOrASignedSdkPath` — two defects in
  `JsonStreamWriter`'s integer paths (§9, §10), both of which this test caught in
  the act.

`TZ=UTC` is set on the test rather than assumed: the manifest carries a wall
clock and the report renders times of day from it.

Skipped, not failed, when the SDK's coreJSON submodule is not initialised.

---

## `sensorlab-report-roundtrip` — the format is a contract

`profile_export.cpp` drives two scenarios through the real `Service` and dumps
everything they wrote to a directory, using the real writers. Then
`RunReportRoundtrip.cmake` runs the real `profile_report.py` over one and the real
`profile_diff.py` over both.

The two scenarios are the *same device on two firmware versions*, with a
deliberate difference: the second delivers the accelerometer at half the rate and
in a four-field frame where the first delivered three. So the diff has something
real to find rather than two identical files — and what it must find is the
field-count change keyed on `0x10.frame.field_count`, which is exactly the shape
of `RUNNING_CADENCE`'s documented 4 → 2 shrink.

The assertions are not "it parsed". A script that parses a file and says nothing
useful about it has not been tested, so the round trip checks that the report
names the firmware, states completeness, surfaces the stuck field, surfaces the
type with no producer, surfaces the doc/header divergence, and lists what is still
UNVERIFIED. And that a profile diffed **against itself** reports no changes and
exits 0 — without which a tool that reported everything as changed would pass
every other assertion.

`profile_diff.py` exits 1 when it finds changes, so it works in a gate; the
round trip treats anything above 1 as a real failure.

Skipped, not failed, without python3.

---

## What is deliberately not here

**Any test that enumerates a directory.** The stock `InMemoryFileSystem` ships
`EmptyDirectory`, a stub that always reports no entries; the real enumerating
fake (`InMemoryDirectory`) exists only on `una-sdk`'s `poc/athensrun` branch.
Rather than require a particular SDK branch to run the suite, **nothing on the
tested path enumerates**: the app derives every filename it reads from a run id or
a firmware string rather than scanning a folder, which is also more robust on
hardware.

`Tools/pull_profile.py` does enumerate, on the host, over BLE — and it is not
tested at all, for the same reason SleepLab's `pull_nights.py` is not: it needs
Linux with BlueZ and a bonded watch. SleepLab's ledger row T4 covers both.

**Any test of the screen's pixels.** The roster's *content* is asserted through
the harness — the flags, the markers, the rates, the burst indices — because the
service computes all of it and the view only formats it. What the view does with
a `TextAreaWithOneWildcard` is not asserted anywhere, and a simulator run is the
only thing that has looked at it.
