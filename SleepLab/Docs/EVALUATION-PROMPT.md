# Prompt: adversarially evaluate SleepLab before it costs a night

You are an expert embedded C++ engineer and a hostile reviewer. Your job is to
find the ways **SleepLab** is wrong, fragile, or under-instrumented — and to
find as many of them as possible **without sleeping**, because the feedback loop
for anything you miss is eight hours plus a morning.

The app lives in `SleepLab/` in this repository (`watch-apps`), built out of tree
against an SDK checkout found through `$UNA_SDK` at `apps-v1.4.0`.

Read [`README.md`](../README.md), [`Docs/FEASIBILITY-LEDGER.md`](FEASIBILITY-LEDGER.md),
[`Docs/ROLLOUT.md`](ROLLOUT.md) and [`Tests/README.md`](../Tests/README.md)
before you start. Then treat all four as **claims to be tested, not context to be
trusted** — several are the author's own assertions about their own code.

---

## 0. The economics, which determine everything below

One night of evidence costs a night. There is no fast path: the app records for
eight hours, and you find out in the morning.

So there are exactly three ways to waste a day, and your job is to eliminate the
first two entirely and minimise the third:

1. **The night doesn't run.** A crash, a wedged loop, a night that never opens, a
   file that never gets written. Every one of these is findable at a desk.
2. **The night runs and records the wrong thing.** An off-by-one, an overflow, a
   threshold on the wrong side, a state machine that desynchronises. Also
   findable at a desk, and *harder* to notice afterwards because the output looks
   plausible.
3. **The night runs correctly and you still can't tell what happened.** The
   artifacts don't contain enough to diagnose the night, so you have to run
   another one to answer the question you should have been able to answer from
   the first. This is the failure mode people under-weight, and §3 is about it.

**Front-load ruthlessly.** If a question can be answered by a host test, a fault
injection, a replay, or a careful read, it must not be answered by a night.
Prefer building a tool that answers a class of questions over answering one.

---

## 1. What "adversarial" means for this app specifically

This is a sleep tracker. Its failures are *quiet*: the numbers always look
plausible, nobody has ground truth in their bedroom, and a wrong answer is
indistinguishable from a right one by inspection. That shapes what you should
hunt for.

**Rank findings by how invisible they are, not by how severe they sound.** A
crash is cheap — you see it and fix it. A silent one-minute bias in every night
is expensive, because it survives review and contaminates the calibration that
the whole app's credibility rests on. One such bug has already been found and
fixed (final wake was reported at the start of the last sleep epoch rather than
its end); assume there are more of that shape.

**Attack the honesty contract directly.** The app's central claim is that it
refuses to report what it cannot know. Find a path where it reports anyway:

- a night that failed the worn gate but still yields a number somewhere — the
  screen, the summary JSON, the index, the glance, the home widget, the baseline;
- a `0` that should be an absent, or an absent rendered as `0` downstream;
- a figure whose provenance string says one method and whose value came from
  another;
- the restfulness band leaking into anything that reads like a stage or a
  duration;
- a delta against the personal baseline shown before the baseline was earned.

**Do not relax a check to make something pass.** If a threshold is wrong, say
which measurement would set it. If the gate suppresses something it shouldn't,
that is a finding about the gate, not permission to loosen it.

**Re-derive the ledger.** `Docs/FEASIBILITY-LEDGER.md` tags claims CONFIRMED /
LIKELY / UNVERIFIED / REFUTED. Check the CONFIRMED ones against the artifacts
that supposedly earned them. A tag that outran its evidence is itself a finding.

---

## 2. Tier A — everything answerable at a desk

The bulk of your effort. Run:

```sh
Tools/docker-build.sh tests      # host tests, 5 suites
Tools/docker-build.sh app        # the .uapp
Tools/docker-build.sh probe      # the Tier 0 probe .uapp
Tools/docker-build.sh sim-run    # the simulator, headless
```

### 2.1 The pipeline, end to end, offline — build this first

**There is no way today to run a whole night through the real code at a desk.**
The engine has unit tests over synthetic `ScoringInput`s, and the simulator has
no sensors. Nothing exercises `Service`'s actual path — sample → `EpochCounter`
→ recording epoch → scoring epoch → segmenter → store → scorer → gate →
summary — as one thing.

**Build that harness.** A host binary that feeds a synthetic (or recorded)
accelerometer, heart-rate, touch and step stream through the real `Service`
logic against the kernel test doubles, and emits the real files. Then a night is
seconds, not a night, and every question below becomes cheap.

`Tests/night_log_export.cpp` and `Tests/probe_log_export.cpp` are the existing
pattern for a host binary that drives real app code through
`SDK::TestSupport::KernelFixture`. `Service::run()` blocks on the kernel queue
and cannot be called directly — decide whether to extract the seams, drive it
through a scripted `StubAppComm`, or restructure. Say which you chose and why.

Once it exists, generate nights that are hostile rather than typical: a sleeper
who never settles, one who settles instantly, a night of nothing but movement, a
night at the 16-hour truncation bound, an epoch stream with gaps, a stream whose
timestamps jump backwards.

### 2.2 Arithmetic and state machines

Specific places worth attacking. **This list is a starting point, not the scope
— findings outside it are more valuable, not less.**

- **Epoch pairing desynchronisation.** Two 30 s recording epochs make one 60 s
  scoring epoch (`mPendingHalves` in `Service.cpp`). What happens when the loop
  oversleeps and skips a recording epoch? Is a scoring epoch ever built from two
  non-adjacent halves, and does anything notice?
- **The uptime wrap.** `getTimeMs()` is 32-bit and wraps at ~49.7 days. Every
  comparison should be a signed or unsigned difference, never a magnitude
  compare. Audit *every* one — the epoch grid advance, the HR duty cycle, the
  resume classification, the alarm. Construct the wrap in a test.
- **`EpochCounter` under hostile input.** dt of 0, dt at `kMaxGapMs` exactly, dt
  of one millisecond, wildly jittering dt, NaN or infinity from a bad parse,
  saturation at the `4.0e9f` clamp. The filters are re-coefficiented per sample
  from dt: prove they are stable across the whole delivered range, including the
  ~48 Hz the hardware actually produces rather than the 25 Hz requested.
- **Integer division and truncation.** `wornPct` averaging across halves, the
  strip's bucket boundaries (`n * b / used`), efficiency, the baseline median on
  even counts, `hrMinAtPct`.
- **Segmenter boundaries.** Windows that cross midnight are tested; now attack
  the ones that aren't — a window one minute wide, a session that opens in the
  last minute of the window, backdating past the start of the pre-roll ring,
  `resumeOpen` with an epoch count larger than the array.
- **Webster rescoring.** The two passes are tested for non-cascade
  independently. Test them composed, and against patterns designed to make the
  order matter.
- **Cole-Kripke's constants** are transcribed and unverified (ledger A8). Check
  them against the primary source, or state clearly that you could not and what
  it would take.

### 2.3 Fault injection — the storage path

`SDK::TestSupport::InMemoryFileSystem` already carries fault hooks that
**nothing in this repo currently uses**: `failWritesAfterBytes`,
`failWriteOpenSuffix`, and a `closeGate` callback that models FatFs keeping a
lock slot when a close fails. Use all three.

- A volume that fills at 03:00. `NightStore::appendEpoch` returns a bool —
  **trace who checks it.** If a failing write silently drops epochs while the
  night continues, what does the morning report say, and should it?
- A close that fails and leaks a handle. FatFs has a finite lock table; a
  service that leaks one per epoch for eight hours will exhaust it. Does it?
- The summary JSON failing while the index row succeeds, and the reverse. The
  ordering is deliberate (state cleared last) — prove the deliberate ordering
  actually holds under each failure.
- `night_state.txt` truncated, half-written, containing a path outside the
  sandbox, or naming a file that exists but is not a night.
- The index growing to a decade, and the tail read landing mid-row.

### 2.4 Concurrency and lifecycle

- `COMMAND_APP_STOP` arriving mid-epoch, mid-flush, mid-night-close.
- The GUI attaching and detaching repeatedly; the glance starting while the GUI
  is up; the home widget claimed twice or released twice.
- Message allocation failing — every `make_msg` can return null. Trace what each
  caller does.
- A `SLEEP_REQUEST` arriving before the first epoch has closed.

---

## 3. Tier B — can a night be diagnosed after the fact?

This is the section most likely to be skimped and most likely to cost a second
night. **Assume the night behaves oddly and you have only the artifacts.**

Produce a **post-mortem matrix**: one row per thing that can go wrong overnight,
one column for the artifact that would reveal it, and an honest verdict on
whether it actually would.

Rows should include at least: delivery stopping partway; delivery degrading
rather than stopping; the app restarting; the device rebooting; the clock
changing; the charger being connected; storage filling; the worn sensor going
silent; the worn sensor flickering; heart rate producing nothing; heart rate
producing nonsense with low trust; the segmenter opening a night late; opening
early; closing early; never opening; the scorer disagreeing with the movement
index; the alarm firing at the wrong time; the alarm not firing.

For each: **name the file, the column, and the value you would look for.** Where
the answer is "you could not tell", that is a finding and the deliverable is the
instrumentation that would fix it.

Known gaps to evaluate — again, a starting point:

- **There is no on-device diagnostic log.** `MapManager` writes
  `Debug/mapmanager_verify.log`; `FwDump` writes a manifest and a context file.
  SleepLab logs only through `LOG_INFO`, which requires a UART capture and a dev
  tool. If a night produces no epoch file at all, is there *anything* on the
  volume that says why? Should there be, and what would it cost?
- **The epoch CSV carries no provenance.** The settings, the delivered rate, the
  app version and the HR mode are written into the summary JSON — which is only
  written when the night *closes*. An interrupted night leaves a CSV that cannot
  say what produced it. Is that acceptable? What is the cheapest fix?
- **Per-epoch decisions are not recorded**, only inputs. The sleep/wake verdict,
  the worn verdict and the band are recomputed at close and never stored per
  epoch. They *can* be re-derived offline from the counts — verify that they
  genuinely can, bit for bit, with the same constants. If a threshold changes
  later, can an old night still be re-scored? Should the constants that scored a
  night be written into its summary so that it can?
- **Sensor delivery is not recorded per sensor.** The epoch row has accelerometer
  `samples` and `hr_samples`. It does not record whether touch, motion or steps
  delivered anything. The probe records all of it; SleepLab does not.

### 3.1 The question behind Tier B

The probe and SleepLab **cannot be installed together** — both autostart and
both claim the accelerometer and heart rate (ledger S8). That doubles the night
budget for anything needing both diagnostic depth and real scoring.

Evaluate whether that separation should persist. Could SleepLab record the
probe's diagnostic columns itself, behind a setting, and retire the probe after
the first night? What would that cost in storage, power and complexity, and what
would it buy in nights? Recommend, with the arithmetic.

---

## 4. Tier C — make the first night pay for itself

Only after Tiers A and B.

- **A pre-flight.** Something an operator can check in under a minute that
  establishes the night will record: sensors resolved, settings as intended,
  storage writable and roomy, clock plausible, charger out, no competing app.
  Some of this the probe's screen already does. Decide what SleepLab needs of
  its own, and whether it should refuse to start a night it cannot record.
- **Order the unknowns by cost.** Given the ledger's remaining UNVERIFIED rows,
  which can share a night and which need their own? Produce the shortest
  sequence of nights that settles the most, and say what each one is for.
- **Define each night's success criterion before it runs**, so a night can be
  called wasted the moment it is, rather than after a morning of analysis.

---

## 5. Deliverables

1. **A findings list**, ranked by *invisibility × consequence*, not by severity
   label. Each finding: what is wrong, the input that demonstrates it, what it
   would do to a real night, and how you know.
2. **A failing test for every correctness finding**, committed before the fix.
   That is the house discipline and it is what makes a finding durable. Tests go
   in `SleepLab/Tests/` following the existing suites.
3. **The offline pipeline harness** from §2.1, with the hostile nights it
   generates kept as fixtures.
4. **The post-mortem matrix** from §3, as a document in `SleepLab/Docs/`.
5. **Ledger updates** for anything you confirm, refute or newly doubt, with the
   method that earned the tag and the date.
6. **A one-page verdict**: is this app ready to spend a night on, and if not,
   what is the shortest path to it.

## 6. Rules

- **Do not weaken the honesty contract to make a test pass.** No sleep stages,
  no numbers for an unworn night, no absolute physiological thresholds, no
  duration derived from two wall-clock readings, no elapsed time inferred from a
  sample count. If one of these is in the way, the design is the finding.
- **Every threshold you touch keeps a comment naming the data that justifies it**,
  or a TODO naming the recording still needed.
- **Distinguish "I verified this" from "I read that it is so."** The ledger's
  whole value is that distinction; your report inherits it.
- **Say what you could not check, and why.** An honest gap is worth more than a
  confident guess, and this app's entire posture depends on that being true of
  its documentation as much as its output.
- Conventional-commit titles scoped `fix(sleeplab):` / `test(sleeplab):` /
  `docs(sleeplab):`, one concern per commit, authored as
  `toby.murray@protonmail.com`, and **no mention of AI assistance** in commits,
  PR bodies, code comments or docs.
- SDK-side gaps are separate single-concern branches against `una-sdk`; the app
  stays entirely inside `SleepLab/` with zero SDK modifications.
- **Never post comments on `UNAWatch/una-sdk` PRs or issues.** Read and push
  branches only.
