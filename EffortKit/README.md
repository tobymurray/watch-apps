# EffortKit

Session structure for the UNA Watch's activity apps: what the wearer was doing
minute to minute, how far their heart rate fell when they stopped, and what
either of those looks like next to their own past.

`no_std`, allocation-free, no SDK types, clock and samples injected. Same shape
as [`TextKit`](../TextKit) and the same reason for existing — the code was about
to be written twice.

## Why it is a shared directory and not a module in one app

Three consumers, two of them already written down:

| Consumer | What it asks for |
| --- | --- |
| `Squash` | Rally/rest segmentation, inter-rally heart-rate recovery, per-user baselines across sessions. |
| `Spin` | [`Spin/Docs/RECOVERY-PROMPT.md`](../Spin/Docs/RECOVERY-PROMPT.md) — heart-rate recovery after effort, and a record in `../SharedData/` that outlives the ride. |
| `SleepLab` | Already ships `Engine/BaselineStore`, `Engine/WornGate` and `Engine/EpochCounter`: epoch reduction and robust per-user baselines, written independently in C++. |

The third is the argument. Nobody set out to write the baseline machinery
twice; it happened because there was nowhere for the first copy to live. This
directory is that place. `SleepLab`'s copy is not migrated here — it works, and
a rewrite of a shipped sleep engine is not a change this buys anything — but the
next app that needs a baseline has somewhere to get one.

What is deliberately **not** here: anything that knows what a squash court is,
anything that reads a file, and anything that knows the SDK exists. Feeding the
engine and persisting what it says are the Service's jobs.

## The rule the crate is built around

> Nothing reaches the screen, the FIT file or the summary that cannot be
> defended with a measurement taken from this repository's own recordings.

That is a discipline, and disciplines rot. So here it is a type.

[`segment::Thresholds`](src/segment.rs) and [`recovery::Thresholds`](src/recovery.rs)
have no `Default`, no public fields, and no constructor that does not take a
[`Provenance`](src/lib.rs) naming the recordings that set them and the date they
were derived. A threshold and its evidence cannot be separated by a refactor,
because there is no way to build the one without the other.

There is **no calibrated constant in this repository**. The only value the watch
build can construct is `Calibration::Absent`, and with it the segmenter and the
recovery analyser return `Err(Unavailable::NotCalibrated)` rather than a
plausible number. Tests build thresholds from `epoch::UNMEASURED`, which is
`#[cfg(any(test, feature = "std"))]` and cannot reach a watch binary — so the
state machine is exercised without any of its numbers meaning anything.

When the recordings in
[`Squash/Docs/RECORDING-PROTOCOL.md`](../Squash/Docs/RECORDING-PROTOCOL.md)
exist and `phase-a` has measured them, a calibration is one `const` with its
provenance filled in, and everything downstream starts reporting.

## The session is not a match

The constraint that shapes the state machine. A session is any of: a match; two
hours of threes, where whole games are spent off court; drills, which are
minutes of continuous hitting with no rest at all; a knock-up, which is neither
rally nor rest; and time worn but not playing.

Two states would be wrong. `RALLY`/`REST` alone scores every sit-out in a game
of threes as a magnificent recovery and every drill as one enormous rally. So
there are three — `Rally`, `Rest`, `OffCourt` — and `OffCourt` is separated from
`Rest` by a **level of its own**, not by duration, because a long rest between
games is still a rest.

Whether such a level exists is a question only a recording answers, so it is not
assumed: `OffCourtRule` is either `Level { below, for_epochs }` or
`Indistinguishable`. Under `Indistinguishable` the machine never enters
`OffCourt`, `off_court_epochs` is zero, and `off_court_separable` is false — the
state is reported as absent rather than silently folded into rest.

Nothing in the engine knows about games, PAR-11, a fixed rest length, or a
session that starts with a knock-up. Structure is an output.

### Which outputs are comparable across sessions

A 25-minute match and a two-hour session of threes are not comparable on totals,
so [`session::Metric`](src/session.rs) is explicit about which is which:

| Metric | Kind | Comparable across sessions? |
| --- | --- | --- |
| `HrMean`, `HrMax` | level | Yes. |
| `RallyRate` (rally seconds per minute on court) | rate | Yes — off-court time is excluded from the denominator. |
| `WorkRestRatio` | ratio | Yes. |
| `RecoveryShort`, `RecoveryLong` | level | Yes, within a source and window length. |
| `RallyCount` | total | **No.** Baselined anyway, because the wearer's own typical session length is itself stable, but it is the one row a reader should distrust when session shapes change. |
| Longest rally, total rally time, total rest time | total | Within a session only. |

## Heart-rate recovery, and what it is not

A fall in bpm over a stated number of seconds, never a fitted slope — unless
Phase A1 shows the heart-rate signal supports one, which it has not been asked
yet. `CLAUDE.md` records consecutive `HEART_RATE_EX` samples differing by 0.50
and 0.18 bpm over two real rides, which is not the behaviour of a raw
physiological signal; if the kernel's own smoothing has a time constant
comparable to the rests being measured across, then a gradient measured over one
is the filter settling, not the wearer recovering. `Formulation` carries
`NotMeasurableOnThisHardware` for exactly that verdict, and a calibration
carrying it discards every window of that kind and counts them.

Short rests and long ones are separate measurements and are never averaged
together: `mean_drop(WindowKind::BetweenRallies)` and
`mean_drop(WindowKind::OffCourt)` are different numbers with different meanings.

A window contributes only if it lasted the full length, carried a trusted
reading throughout with no gap longer than the calibration allows, met the
minimum trusted-sample count, and came from a source the policy accepts.
Everything else is discarded and counted by reason in `Discarded`, which is an
output in its own right rather than a debug field — a session that produced no
recovery figure should be able to say why.

Wrist optical during squash is the adversarial case: grip tension, impact shock
and the watch moving on the wrist all corrupt it, and a smoothed corrupted
signal still looks smooth. `SourcePolicy` therefore has `ExternalOnly` as a
first-class option, and under `EitherWithSourceRecorded` every window carries
the source it came from so a reader can filter. Which of the two is right is
A1's to decide.

The analyser holds no history. The heart rate at the start of a window is
captured when the window opens, so nothing is buffered to look back at.

## Baselines that survive real sessions

Median and MAD over a rolling window of 20 sessions, not mean and standard
deviation. One two-hour session of threes, one twenty-minute knock-up, or one
session where the strap fell off must not redefine what normal looks like.

- **Twenty sessions** is roughly two months at one to three a week — long enough
  that one bad session is outvoted, short enough to follow a training block
  rather than average it away.
- **Five sessions** before any comparison is offered. Below three the MAD is
  degenerate; at five a single outlier cannot move the median past its
  neighbours. Under that, `compare()` returns `Unavailable::WarmingUp { have,
  need }` — the raw measurement is still shown, the comparison is not invented.
- **Ten percent** is the most one session may move a baseline. The median
  already resists a single session; this is the second lock. It means a genuine
  halving takes at least seven sessions (0.9⁷ = 0.48) — longer than any artefact
  lasts and shorter than a training block, so a real change still arrives. A
  test pins both halves of that: the bound is not skipped, and a sustained
  change still converges.
- **Sessions that should not vote** do not. Under ten minutes of active time, or
  with a trusted heart rate covering less than 80% of it, a session is recorded
  and can be shown but contributes to no baseline. Rally-derived metrics
  additionally need a segmented session with at least ten rallies, so a drill
  session does not vote on rally structure.

Every comparison is against the wearer's own history. There are no population
norms in this crate and nothing that ranks one person against another.

## The profile file

Owned and written by the app, distinct from `settings.json` (the watch's own)
and `input.json` (written from the phone). Read it with `Profile::parse_json`,
which **cannot fail**: absent, truncated, garbage or from a schema this build
does not know all yield an empty profile and a `Load` saying which, so a file
somebody else wrote can never stop the app starting.

A file naming a schema this build does not read is **refused**, not partially
read. These are bare integers whose meaning a later schema could change, and a
wrong baseline is worse than a warm-up.

Baselines are **derived on load, never stored**. The file keeps what was
measured, so changing how a figure is derived applies to the whole history
instead of orphaning it.

Every value on the wire is an integer — there is no float formatter in `no_std`,
and a fixed-point hundredth is exact where a hand-rolled decimal would not be.

| Key | Units | Meaning |
| --- | --- | --- |
| `schema` | — | Exactly `1`, or the file is refused. |
| `sessions` | — | Oldest first, at most 20. |
| `utc` | s | Unix time the session started. |
| `active_s` | s | Activity running, pauses excluded. |
| `hr_mean_x100` | bpm × 100 | Mean over trusted readings. |
| `hr_max_x100` | bpm × 100 | Highest trusted reading. |
| `hr_covered_s` | s | Seconds a trusted reading was available. |
| `hr_source` | — | 0 unknown, 1 optical, 2 external. |
| `segmented` | — | 0 when no calibration existed, in which case every field below is zero because nothing was segmented, not because nothing happened. |
| `rally_count` | — | Rallies found. |
| `rally_s`, `rest_s`, `off_court_s` | s | Time in each state. |
| `rec_short_x100`, `rec_long_x100` | bpm × 100 | Mean fall over qualifying rests of each kind. |
| `rec_short_n`, `rec_long_n` | — | Qualifying windows of each kind. |

Twenty full records is about 4 KB; the writer refuses past 8 KiB and writes
nothing at all unless the whole file fits, so a caller committing only on `Ok`
cannot leave a truncated file behind. Past twenty sessions the oldest falls out,
which is deliberate: a session older than the rolling window can no longer
affect any baseline, so keeping it would grow the file for nothing.

## Bounds, and what happens at them

| Thing | Bound | At the bound |
| --- | --- | --- |
| Rally detail | `segment::MAX_RALLIES` = 256 | `rally_count` keeps rising; per-rally detail stops being kept and `rallies_truncated` says so. |
| Recovery windows | `recovery::MAX_WINDOWS` = 256 | Same, reported by `windows_truncated`. |
| Sessions in the profile | `profile::MAX_SESSIONS` = 20 | Oldest drops out. |
| Profile file | `profile::MAX_BYTES` = 8192 | The write is refused whole. |

## Self-consistency the tests assert

Two checks that catch whole classes of error for free, and both are pinned:

- `SessionSegmentation::accounts_for_every_epoch()` — rally plus rest plus
  off-court plus dropped equals the epochs classified.
- Every rally record's window lies inside the session.

## Tests

```sh
cd EffortKit
cargo test
cargo clippy --all-targets
cargo build --release --target thumbv8m.main-none-eabihf   # the watch target
```

The last one is the one that matters most: it is what proves the crate is
`no_std` and that nothing testable crept in behind `feature = "std"`.

## Licence

MIT OR Apache-2.0.
