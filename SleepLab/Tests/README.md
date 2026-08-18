# SleepLab host tests

```sh
../Tools/docker-build.sh tests
```

or, with `cmake` and a C++17 compiler on the path:

```sh
export UNA_SDK=/path/to/una-sdk          # apps-v1.4.0
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Five suites plus two round trips, split because they need very different things.

| Suite | Needs | Covers |
| --- | --- | --- |
| `sleeplab-engine-tests` | GoogleTest only | Everything in Tier 2. The engine includes no SDK header, so this builds and runs anywhere. 107 tests. |
| `sleeplab-store-tests` | the kernel doubles, coreJSON | Tier 1: the settings reader and a night on disk. 43 tests. |
| **`sleeplab-pipeline-tests`** | the kernel doubles, coreJSON, `TZ=UTC` | **Whole nights through the real `Service`**, sample to file to summary, at 110 ms a night. 37 scenarios. See below. |
| `sleeplab-probe-tests` | the kernel doubles | The Tier 0 probe's on-disk record. |
| `sleeplab-probe-report-roundtrip` | python3 as well | The probe's real writer parsed by the real host script. |
| `sleeplab-night-report-roundtrip` | python3 as well | The night writer parsed by the real host script, both subcommands. |

## The pipeline suite is the one that matters

Until 2026-08-18 nothing exercised the recorder's own path. The engine had tests
over synthetic scoring inputs, the store over synthetic epochs, and the simulator
had a screen and no sensors. Everything between a sample arriving and a summary
being written — the epoch grid, the 30 s/60 s pairing, the pre-roll ring, the
backdate, the segmenter, the resume classification, the alarm, the files — was
reachable only by wearing the watch for eight hours.

`NightHarness.hpp` runs a night in 110 ms. It drives the real `Service::run()`
**unmodified**, by being the kernel: `StubAppComm::getMessage` is virtual, so the
harness answers the sensor layer's handshake, delivers batches on a schedule,
advances uptime by exactly the timeout the loop asked to sleep for, and finally
hands back an `APP_STOP`. It captures every `SLEEP_REPORT` the service publishes,
so an assertion in this suite is an assertion about what a person would have been
shown.

Extracting a `poll()` seam as `MapManager`'s service has was the alternative and
was rejected: the loop is one of the things under suspicion, and a test that
replaces the loop cannot find a bug in the loop. One was there.

**Its first fifteen scenarios failed eleven times.** See
[`../Docs/ADVERSARIAL-REVIEW.md`](../Docs/ADVERSARIAL-REVIEW.md). If you change
anything in `Service.cpp`, this is the suite that will tell you.

The scenarios are hostile rather than typical, and their amplitudes come from a
**measured** count scale rather than a guess — every threshold in this app lives
inside one decade of it, so a fixture built on a guess would prove nothing. The
table is at the top of `Pipeline_test.cpp`.

## What the evidence actually is

There is no polysomnography here and there never will be. So the engine tests
are built on **synthetic nights whose answers are known by construction** — a
known onset, awakenings of known length, a known final wake. That pins the
arithmetic exactly, and it is the whole of what it does. A passing suite means
the code computes what it says it computes. It says nothing about whether those
numbers correspond to sleep.

The number that would say something about sleep is `SleepWakeScorer::kCountScale`,
and it is currently a guess. See §6 of the implementation brief and the
validation table in [`../Docs/FEASIBILITY-LEDGER.md`](../Docs/FEASIBILITY-LEDGER.md).

## Four tests worth knowing about before you change anything

**`HrvChannel.SupplyingHrvChangesNothingToday`** is meant to *fail* the day
someone wires HRV into the scorer. That day is the day clause A2 of the ledger —
"no stage hypnogram" — has to be revisited in writing, with the literature in
hand, rather than quietly relabelled. Deleting the test to make a change pass is
the exact mistake it exists to prevent.

**`WornGate.ANightstandFailsEvenWhenTouchDetectSaysWorn`** is the one that keeps
the app honest. A watch on furniture is perfectly still, has no awakenings, and
would report 100 % sleep efficiency across eight hours — every number beautiful,
every number about a piece of furniture. The fixture sets `wornPct` to 100
throughout, because the premise is that the capacitive sensor is *wrong* and the
plausibility check has to overrule it.

## No directory enumeration, deliberately

Nothing on the tested path enumerates a directory. The stock
`InMemoryFileSystem` in `Tests/Host/support/KernelTestDoubles.hpp` ships
`EmptyDirectory`, a stub that always reports no entries; the real enumerating
fake (`InMemoryDirectory`) exists only on `una-sdk`'s `poc/athensrun` branch, and
`MapManager/Tests` needs that branch to run at all.

Rather than pin this suite to a particular SDK branch, the history reader probes
the filenames it can derive from a date instead of scanning the folder — which
is also more robust on hardware, and is the same choice
[`FwDump`](../../FwDump/README.md#tests) made for its resume scan.

## Two levels of format checking

`sleeplab-probe-tests` checks the probe's CSV against *this repo's reading* of
the format. `sleeplab-probe-report-roundtrip` checks it against the reader that
actually consumes it: `probe-log-export` writes a synthetic night with the real
`Probe::Log`, and `Tools/probe_report.py` parses it, with assertions that the
report detects the awkward cases the fixture deliberately contains — a
twelve-minute delivery hole, two launches, and the expected `HEART_BEAT`
verdict. Same two-level check `FwDump` uses against `reassemble_dump.py`.

`sleeplab-night-report-roundtrip` does the same for the night format:
`night-log-export` writes three worn nights, one nightstand night and a matching
diary with the real `NightStore`, and `Tools/night_report.py` reads them.

Its assertions are about the *conclusions*, not the parse, because a script that
reads a file and says nothing useful about it has not been tested. It must
separate the wrist from the table on a fixture built to separate; it must pair
all three diary nights; and every error must be two digits of minutes. That last
one exists because the first version of this fixture had its diary dates a year
out and the round trip passed anyway — the assertion at the time was that the
script declined to quote an accuracy figure off three nights, and it declines off
zero too. The diary dates are derived from the night timestamps now, so they
cannot drift again.

Both are skipped, not failed, when there is no python3.

## What is not covered

- **`Service::run()`**, in either app. It blocks on the kernel message queue and
  never returns; the testable seams are the pieces it calls between waits.
- **The GUI.** On-device and simulator verified only.

**`HonestyContract.ANightThatFailedTheWornGateDrawsNoStrip`** is the nightstand
test's other half, and the half that was missing. The numbers were suppressed for
an unworn night and the epoch strip was drawn in full — 100 buckets of per-epoch
sleep, wake and restfulness, under a caption telling the reader it came from their
movement and heart rate. A picture of a claim is the claim. If a future change
makes the strip available earlier or more often, this is what should stop it.

**`PublishedConstants.SleepIsBelowTheThresholdAndWakeAtOrAboveIt`** asserts a
direction rather than a value, because inverting Cole-Kripke's threshold inverts
every verdict in every night and the output would still look like a night. One
widely-read reference implementation's documentation states it the other way round,
which is exactly how somebody could talk themselves into flipping it.
