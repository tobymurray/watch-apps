# SleepLab host tests

```sh
../Tools/docker-build.sh tests
```

or, with `cmake` and a C++17 compiler on the path:

```sh
export UNA_SDK=/path/to/una-sdk          # apps-v1.4.0
cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)
```

Three suites, split because they need very different things.

| Suite | Needs | Covers |
| --- | --- | --- |
| `sleeplab-engine-tests` | GoogleTest only | Everything in Tier 2. The engine includes no SDK header, so this builds and runs anywhere. |
| `sleeplab-store-tests` | the kernel doubles, coreJSON | Tier 1: the settings reader and a night on disk. |
| `sleeplab-probe-tests` | the kernel doubles | The Tier 0 probe's on-disk record. |
| `sleeplab-probe-report-roundtrip` | python3 as well | The probe's real writer parsed by the real host script. |
| `sleeplab-night-report-roundtrip` | python3 as well | The night writer parsed by the real host script, both subcommands. |

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

## Two tests worth knowing about before you change anything

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
