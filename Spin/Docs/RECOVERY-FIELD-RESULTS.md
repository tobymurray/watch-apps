# Heart-rate recovery: what has been tested

> **The symbol names below are the ones the build under test actually called.**
> Every run here predates the merge onto the shared `EffortKit` crate, so they
> read `trainkit_*`; the code that runs now calls `spin_engine_*` through the
> shim in `Software/Libs/rust`. The findings are unaffected — a record of what
> happened is not improved by renaming it afterwards.

The record of what has actually been run on hardware, dated, with what each run
found. The work still outstanding is in
[`RECOVERY-FIELD-TEST.md`](RECOVERY-FIELD-TEST.md).

**Read the maximum-heart-rate table first.** Several sessions here were run
against a deliberately fake maximum so the intensity gates would engage at a
desk. Their heart-rate numbers are artefacts and mean nothing physiologically;
`hr_max_setting` in the shared log is the only thing that separates them from a
real ride afterwards.

## Which maximum each recorded session ran against

| Recorded, local | `hr_max_setting` | Maximum | Evidence for |
|---|---:|---|---|
| 11:11:56, 19:35:44 | `0` | **none read** — the zone-count bug | the plumbing, and the bug itself |
| 20:10:56 | `184` | **real** | the desk re-run on the fixed build |
| 20:56:37 | `184` | **real** | **Ride A — the only physiological measurements here** |
| 21:51:35, 22:49:32 | `100` | **synthetic** | Session 1's refusals |
| 23:17:32 | `100` | **synthetic** | Session 2's recovery cap |

Only the two rows marked **real** carry heart-rate numbers that mean anything
about a body. Everything at `100` exists to make a code path execute.

## What is already proven

Every discard reason but one has now fired on hardware.

| Gate or claim | Where it fired |
|---|---|
| `no_max_hr` | the desk checks, twice |
| `too_short` | Ride A's A1 and A7; and at `active=0` in Session 1's retry |
| `no_baseline_history` | Session 1 |
| `too_easy` | Ride A's A2, as a `discard` |
| `already_falling` | Session 1 |
| `dropout` | Session 1's retry |
| `effort_resumed` | Session 1 |
| `ride_ended` | Session 1's retry |
| `source_changed` | **nowhere — deferred on the strap firmware** |
| `armed` → `recovery` | Ride A, twice |
| a session with two recoveries, and one with none | Ride A; the desk sessions |
| `work_kj` present, and `avg_power` from it | Ride A and before |
| `work_kj` **absent** on SKIP, and 48/20 out of the FIT definition | Session 1, both runs |
| `edwards_trimp` on a five-zone ladder | the desk re-run, Ride A, Session 1 |
| the `.bak` rotation and commit rename | `kept` 1 → 2 → 3 → 4 → 5 → 6 |

What is left is `recoveries_dropped` (Session 2), `zone_count: 0` (Session 3),
`zone_count: 3` (Session 4), and `source_changed` when the strap can hold a
connection.

## The original prescription, and why it was revised

Rides A–E were the first plan. Ride A ran and is recorded below; B, D and E
were replaced by desk sessions after review found most of their effort bought
nothing, and C waits on the strap. They are kept because the corrections to
them are themselves findings.

### Ride A — the one that should work (done, 2026-09-03)

The measurement this feature exists for, taken twice, plus the two "not enough
effort" gates.

| # | Do | Expect in `recovery.log` |
|---|---|---|
| A1 | Start the ride. Pedal easy for **2 minutes**. Stop pedalling and **press R1** together. | `cease ... -> too_short` |
| A2 | **R1** to resume. Ride easy — **below 80% of HRmax** — for **5 minutes**. Stop pedalling and **press R1** together. | `cease ... -> armed`, then `discard too_easy` |
| A3 | **R1** to resume. **Hard, 5 minutes**, ending at or above 80% of HRmax and still climbing. At the top, stop pedalling and **press R1 in the same moment**. | `cease ... -> armed` |
| A4 | **Stay on the bike. Feet off or still. Do not pedal, do not get off, do not stand up. 90 seconds by the clock.** | `recovery hr0=… drop=…` after ~60 s |
| A5 | **R1** to resume. Easy 2 minutes, then **hard 4 minutes**. At the top, stop pedalling and **press R1** together again. | `cease ... -> armed` |
| A6 | **Sit still, 90 seconds** again. | a second `recovery` line |
| A7 | **R1** to resume, easy 2 minutes, stop pedalling and **press R1**, then **L1 SAVE**. Enter the bike's kJ with L1/L2. **R1 SAVE**. | `session … recoveries=2` |

**A4 and A6 are the whole ride.** Everything else is scaffolding.

**A3 and A5 are where it is won or lost.** The button and the legs have to stop
together. Press R1 while still turning the pedals and the window opens over
active recovery, which Barak et al. measured as 41% slower — you will get a
number, it will look fine, and it will be a measurement of something else. Stop
pedalling a few seconds before pressing R1 and you have started the fall early,
which either shows up as `already_falling` or quietly understates `hr0`.

If in doubt: **stop, then press within a second.** A baseline up to 2 s late is
allowed and `window_s` records it; a baseline taken while you were still working
is not detectable at all.

A3 and A5 must be **separated by real effort**, not just by a resume: the effort
gate measures the current bout, so resuming and pressing R1 ten seconds later
gives `too_short` (which A1 already covers).

### Ride B — the ones that should be thrown away (about 20 minutes)

> **Superseded by [Session 1](#session-1--the-refusals-about-25-minutes-at-a-desk)**, which reaches all three gates at a desk with no hard effort. Kept for the reasoning below it.

Each of these is a window that opens and then correctly produces nothing.

| # | Do | Expect |
|---|---|---|
| B1 | Hard 5 minutes to above 80%. Then **stop pedalling and do NOT press R1** — the clock keeps running. Watch the number, and press R1 while it reads between 80% of HRmax and 11 below its peak. | `cease ... -> armed`, then `discard already_falling` |
| B2 | **R1**, hard 5 minutes, stop and **press R1** at the top, then **press R1 again after 30 seconds** and pedal. | `resume -> effort_resumed` |
| B3 | **R1**, hard 4 minutes, stop and **press R1** at the top, sit **30 seconds**, then **L1 SAVE** and finish the ride. | `end -> ride_ended`, and `session … recoveries=0` |

### A `cease` line carries only three of the reasons

Both rows above used to predict the wrong line, and Ride A caught it. `cease()`
checks exactly three things — a maximum the watch knows, `MIN_EFFORT_S` of
unpaused clock, and enough trusted history for a baseline — and then arms.
**`too_easy` and `already_falling` are checked against the baseline sample, one
second later, in `second()`**, because `hr0` is the number the measurement is
built on rather than whatever the heart rate happened to be at the instant of
the button press. It is the same reasoning as the two-second baseline grace.

So a `cease` line can only ever say `no_max_hr`, `too_short` or
`no_baseline_history`. Every other reason arrives on the next line as a
`discard`, and **`armed` followed by a `discard` is the gate working**, not the
gate failing to fire.

B1 is the subtle one. Its strip is bounded below by 80% of maximum and above by
11 bpm under the preceding 30 seconds' peak, so its width in bpm is
`(peak − 10) − 0.8 × HRmax`. On Ride A's two measured curves that was
**3.8 bpm — about four seconds of curve** — and a blind 40-second wait landed on
`already_falling` for one and `too_easy` for the other. Both are correct
refusals, but if you want to see a particular one, **aim by the number on the
screen rather than by a stopwatch**, and note that the strip widens as the
maximum falls, which is what
[the low-maximum sessions](RECOVERY-FIELD-TEST.md#the-technique-lower-the-maximum) exploit.

B3 leaves a session in the shared log with **no recoveries at all**. That is a
real state and the file must show it rather than omitting the session — though
the three desk sessions of 2026-09-03 have already demonstrated it, so only
`end -> ride_ended` is still unrun here.

### Ride C — the sensor, and the file (about 15 minutes)

> **C1–C3 deferred** on the strap's two-minute disconnect — see [the strap](RECOVERY-FIELD-TEST.md#the-strap-and-why-ride-c-is-deferred). C2's dropout half and C4 have moved into [Session 1](#session-1--the-refusals-about-25-minutes-at-a-desk).

| # | Do | Expect |
|---|---|---|
| C1 | Start with the strap on. Hard 5 minutes, stop pedalling and **press R1** at the top. | `cease -> armed` |
| C2 | About 20 seconds into the window, **take the strap off** and leave it off for 15 seconds, then put it back. Stay seated for the rest of the 90 s. | `untrusted` lines, then `discard dropout` — or a `recovery` with `trusted=` below 61 if the gap was short |
| C3 | **R1**, hard 4 minutes, stop and **press R1**, sit 90 s **without touching the strap**. | a clean `recovery` |
| C4 | Finish, but this time **SKIP** the kilojoule screen (R2). | `session … work_kj=0` |

C2 also exercises the newest gate: taking the strap off makes the kernel fall
back to the wrist, and a window that changes sensor is discarded as
`source_changed` rather than reporting a fall that is partly the gap between two
instruments. Expect that reason rather than `dropout` if the strap comes back
before the window ends.

C2 is a judgement call in the moment: fewer than about 6 untrusted seconds in
the window survives, more does not. Either outcome is evidence — note how long
you actually had it off.

C4 checks that `work_kj` is **absent** from the JSON, not zero.

### Ride D — no zones (5 minutes)

> **Superseded by [Session 3](RECOVERY-FIELD-TEST.md#session-3--no-zones-about-3-minutes)**: `no_max_hr` is tested before the effort gate, so the four hard minutes below buy nothing.

1. Turn the watch's heart-rate zones **off** in settings.
2. Ride hard 4 minutes, stop pedalling and **press R1**, sit 90 seconds, finish
   and save.
3. Turn the zones back on.

Expect `cease ... -> no_max_hr` and a session in the log with
`hr_max_setting: 0`, `zone_count: 0`, and **no `edwards_trimp`**.

### Ride E — a ladder Edwards never wrote weights for (5 minutes)

> **Superseded by [Session 4](RECOVERY-FIELD-TEST.md#session-4--three-zones-about-6-minutes)**, which is the same test at a desk.

1. On the phone, set Spin's `hrZoneCount` to **3**.
2. Ride 4 minutes at any intensity, finish and save.
3. Set it back to **5** (or 0).

Expect the session to have `zone_count: 3`, three `zone_floors`, four `zone_s`
buckets, and **no `edwards_trimp` field at all**. Ride A's session, by contrast,
must have one.

---

## The sessions as they were run

### Session 1 — the refusals (about 25 minutes, at a desk)

Maximum set to 100, Spin reopened. One ride. Each `3½ minutes` is deliberate:
the gate is a hard 180 seconds and the wait only counts while the clock runs.

| # | Do | Clock after | Expect in `recovery.log` |
|---|---|---|---|
| 1 | **START**, then sit **3½ minutes** | running | — |
| 2 | March on the spot to about 100. Stop marching and **press R1** together | **paused** | `cease ... -> armed` |
| 3 | Wait 15 seconds. **Press R1** and march | running | `resume -> effort_resumed` |
| 4 | Sit **3½ minutes** | running | — |
| 5 | March to about 100. Stop marching and **do not press R1**. Watch the number, and **press R1** when it reads 80 up to your peak minus 11 | **paused** | `cease ... -> armed`, then `discard already_falling` |
| 6 | **Press R1** to resume | running | — |
| 7 | Sit **3½ minutes** | running | — |
| 8 | March to about 100. Stop marching and **press R1** together | **paused** | `cease ... -> armed` |
| 9 | Sit **75 seconds and press nothing**. At about 20 seconds in take the watch off for 15, then put it back and keep sitting | **paused** | `untrusted` lines, then `discard dropout` |
| 10 | **Press R1** to resume | running | — |
| 11 | Sit **3½ minutes** | running | — |
| 12 | Take the watch off for 15 seconds, put it on and **press R1** immediately | **paused** | `cease ... -> no_baseline_history` |
| 13 | **Press R1** to resume | running | — |
| 14 | Sit **3½ minutes** | running | — |
| 15 | March to about 100. Stop marching and **press R1** together | **paused** | `cease ... -> armed` |
| 16 | Sit **30 seconds**, then **L1 SAVE**, then **SKIP** with R2 | saved | `end -> ride_ended`, and `work_kj` **absent** |

Step 9 is the one to be patient through: the window is 60 seconds and **any**
resume ends it first as `effort_resumed`, which is what happened on the first
attempt at 56 seconds. Step 12 reaches a gate the original rides never mentioned
— `cease()` needs 20 of the preceding 30 seconds trusted, and a watch off the
wrist supplies the gap. Step 16 folds in C4: `work_kj` must be **absent** from
`spin_sessions.json`, not zero.

### Session 2 — the recovery cap (about 15 minutes)

Maximum still 100. Three windows that all succeed, in one ride.

| # | Do | Clock after | Expect |
|---|---|---|---|
| 1 | **START**, then sit **3½ minutes** | running | — |
| 2 | March to about 100. Stop marching and **press R1** together | **paused** | `cease ... -> armed` |
| 3 | Sit **75 seconds and press nothing** | **paused** | `recovery hr0=… drop=…` |
| 4 | **Press R1** to resume | running | — |
| 5 | Repeat steps 1–4 **twice more** | | a second and third `recovery` |
| 6 | After the third window closes, **L1 SAVE** | saved | `session ... recoveries=2 dropped=1` |

`TRAINKIT_MAX_RECOVERIES` is 2 and Ride A returned exactly 2, so the
shift-and-drop in `Service::keepRecovery()` has never run. The shared log must
keep the **last two** and report one dropped. The numbers themselves are
meaningless and say so, in `hr_max_setting`.

---

## 2026-09-03 — the desk check

Two desk runs, 11:11 and 19:35 local, on `Spin_0.8.0.uapp`. Rides A–E have not
been done; nothing below is evidence about the gates, only about the plumbing
under them.

### The five checks

| Check | |
|---|---|
| The app started at all | pass |
| `recovery.log` exists in Spin's folder | pass |
| First line `start version=… max_hr=<n>`, n > 0 | **fail, both halves** |
| Contains `cease … -> too_short` | reason differed, but the path it tests works |
| `spin_sessions.json`, `"kept":1`, `status=0` | pass, and `kept:2` with a `.bak` on the second run |

The line, both runs:

```
1788448315 start max_hr=0 zones=6 weight=90
```

**No `version=` at all**, because the installed binary predated the commit that
added it — the stale-`.uapp` failure this check exists to catch, caught on its
first outing.

**`max_hr=0` was not the watch.** Its own `settings.json` held
`"heartRateZones":[92,110,129,147,166,184]`, which is 50/60/70/80/90/100% of
184. Spin misread the message: `heartRateCount` counts zones rather than
thresholds, so it came back as 7 for six values and
`heartRateTh[count - 1]` read an unfilled slot. That put 0 in the maximum and
left 184 standing as a sixth floor. Every window in both runs was discarded
`no_max_hr`, and `pct=` was 0 on every line, so the 80% gate could never have
armed on this watch — Rides A–E would all have produced nothing.
`ZoneLadder::fromWatch` owns the split now and is tested against the ladder
the watch actually sent.

`cease hr=67 pct=0 active=178 -> no_max_hr` appeared in both runs with `P` lines
behind it, so `pauseTrack()` does reach `trainkit_recovery_cease()`. The reason
differed from the predicted `too_short` only because `no_max_hr` is gated ahead
of it.

### What the `.fit` got right

Decoded with python-fitparse, which shares no code with the writer or the SDK's
test reader: `sport=cycling` with `sub_sport=indoor_cycling`; `total_work` at
48 in joules; `avg_power` at 20 and equal to work over active seconds; nothing
in 19, 21 or 41; session `time_in_hr_zone` at 65 against the lap's 57; the laps
summing to the session on timer, elapsed and every zone bucket; the four
developer fields declared; and untrusted seconds carried as an invalid
`heart_rate` rather than an invented one.

### Three things it got wrong

- **The two records disagreed about the same ride.** A mean of 65.7159 bpm went
  into the `.fit` as 65 and into `spin_sessions.json` as 66, because one
  truncated and the other rounded — check 7 of
  [the checks](RECOVERY-FIELD-TEST.md#what-to-check-in-any-run), failing. The file rounds now.
- **The `session` line carried the ride's start time** where every other line
  carries the event's own, so it sorted before the `P` lines above it in a log
  whose whole format is `<utc> <event>`.
- **`avg_power` saturated at 65535**, which is the uint16 invalid value — a
  reader would have seen the field as absent rather than pinned. It stops at
  65534 now.

### The re-run, on `0.8.0-11-0ed81df`

One desk run on a build carrying the four fixes. Every check above passes, and
the three numbers the checks are actually about:

```
1788480655 start version=0.8.0-11-0ed81df max_hr=184 zones=5 weight=90
1788480735 cease hr=65 pct=35 active=79 -> too_short
1788480855 session status=0 recoveries=0 dropped=0 active=79 hr_avg=66 work_kj=200 trimp=0
```

The ladder is the watch's own five floors — `[92,110,129,147,166]` with 184 as
the maximum rather than a sixth floor — so `time_in_hr_zone` is six buckets,
`hr_max_setting` is 184, and `edwards_trimp` is emitted for the first time
(0 here, since a desk run never leaves zone 0). `pct=` is a real fraction of
maximum on every line where it used to be 0, which is the 80% gate having
something to compare against at last.

`too_short` is the reason this document predicts, and the pause was 119 s — the
window was declined on the 180 s effort gate, not on its length.

The two records now agree: a mean of 66.3846 bpm is 66 in the `.fit` and 66 in
`spin_sessions.json`. The `session` line sorts last, `kept` rose to 3, and the
`.bak` beside it holds 2.

### Still open

- Closed by [Ride A](#2026-09-03--ride-a-real-maximum-184), later the same day: two windows
  survived and the whole path past `armed` ran.
- Closed: [`Docs/INSTALLING.md`](../../Docs/INSTALLING.md), linked twice above
  and once from `Service.cpp`, exists now.

---

---

## 2026-09-03 — Ride A (real maximum, 184)

> **`hr_max_setting: 184`.** A real ride at a real maximum: the two measurements
> below are the only ones in this document that mean anything physiologically.

On `0.8.0-14-165ab48`, wrist optical throughout, no strap (see
[the strap](RECOVERY-FIELD-TEST.md#the-strap-and-why-ride-c-is-deferred)). **Both windows produced a
measurement**, which is the first time the path past `armed` has ever run on
hardware: the 60-second window, the seven-point curve, the `trainkit_recovery`
struct across the C ABI, the JSON, and the commit.

19 minutes active of 23.5 elapsed. Average 116 bpm, maximum 162, 129 kcal,
160 kJ entered by hand for **140 W**. Edwards TRIMP 35. Time in zone
162 / 321 / 341 / 173 / 145 / 0 seconds, which sums to the active time and
matches `time_in_hr_zone` in the `.fit` bucket for bucket.

The whole ride, with the per-second lines stripped:

```
1788483397 start version=0.8.0-14-165ab48 max_hr=184 zones=5 weight=90
1788483517 cease hr=88 pct=48 active=120 -> too_short
1788483850 cease hr=96 pct=52 active=420 -> armed
1788483851 discard too_easy hr=97 pct=53
1788484182 cease hr=161 pct=88 active=720 -> armed
1788484243 recovery hr0=161 hr_end=144 drop=17 window=60 trusted=61 pct=88 src=1 curve=161,162,157,154,147,144,144
1788484581 cease hr=161 pct=88 active=1021 -> armed
1788484642 recovery hr0=161 hr_end=138 drop=23 window=60 trusted=61 pct=88 src=1 curve=161,162,159,154,147,144,138
1788484802 cease hr=114 pct=62 active=1142 -> too_short
1788484810 session status=0 recoveries=2 dropped=0 active=1142 hr_avg=116 work_kj=160 trimp=35
1788484810 stop saved=1 discard=0
```

### The two measurements

| | at | hr0 | % max | hr_end | drop | trusted | source |
|---|---:|---:|---:|---:|---:|---:|---|
| A4 | 720 s | 161 | 88 | 144 | **17** | 61/61 | optical |
| A6 | 1021 s | 161 | 88 | 138 | **23** | 61/61 | optical |

Both drops fall in the 15–45 band this document calls plausible, at the low
end — expected on wrist optical, which lags a fast fall.

Not one untrusted second in either window, so the 90% gate was never in
question. That is the wrist sensor at its **best**, not its worst: the window
runs while the wearer sits still with their hands off the bars, and no gate
measures trust during the riding portion at all.

**The accidental control is the useful part.** Both windows opened at exactly
hr0 = 161 and 88% of maximum, and returned drops 6 bpm apart. On identical
starting conditions that spread is a first measurement of the method's own
noise on this wearer, and it sits inside Buchheit's ~25% typical error. It is
n = 2, and whether the gap holds is the thing to watch.

The ride's maximum of 162 occurred **inside** the recovery windows rather than
during the effort.

### Ride A produced exactly the cap

`TRAINKIT_MAX_RECOVERIES` is 2, and this ride returned 2. A ride with a third
qualifying pause would exercise the shift-and-drop in `Service::keepRecovery()`
and set `recoveries_dropped`, and nothing has ever run that path.

### Where a late press actually lands

`already_falling` needs a baseline at or above 80% of maximum **and** more than
10 bpm below the preceding 30 seconds' peak. Those two bound a narrow strip,
and both curves here measure it — pressing R1 this many seconds after ceasing
effort:

| | `already_falling` | `too_easy` from |
|---|---|---|
| A4's curve | 35–40 s | 41 s |
| A6's curve | 36–39 s | 40 s |

At a maximum of 184 the strip is **3.8 bpm wide** — `(peak − 10) − 0.8 × max`,
or 151 down to 147.2 — which is about four seconds of a real recovery curve.
This is measured from the two curves above, not derived.

---

---

## 2026-09-03 — Session 1, in two runs (synthetic maximum, 100)

> **`hr_max_setting: 100`.** A code-path test. Every heart-rate number below is
> an artefact of a maximum set to 100 so the gates would engage while marching
> on the spot; none of it is physiology.

Both runs carry `hr_max_setting: 100`, which is what keeps them out of Ride A's
real numbers.

**The maximum is settable**, which had been an open question: the watch took
`[30,60,70,80,90,100]` and Spin's start line read `max_hr=100 zones=5`.

### The first run — four of six

```
1788486694 start version=0.8.0-14-165ab48 max_hr=100 zones=5 weight=90
1788486913 cease hr=101 pct=101 active=218 -> armed
1788486929 resume -> effort_resumed
1788487201 cease hr=87 pct=87 active=490 -> armed
1788487202 discard already_falling hr=87 pct=87
1788487477 cease hr=100 pct=100 active=715 -> armed
1788487533 resume -> effort_resumed
1788487747 cease hr=0 pct=0 active=929 -> no_baseline_history
1788487949 cease hr=100 pct=100 active=1098 -> too_short
1788487994 session status=0 recoveries=0 dropped=0 active=1098 hr_avg=76 work_kj=0 trimp=56
```

`effort_resumed`, `already_falling` and `no_baseline_history` all fired for the
first time, and `work_kj` came back **absent** from `spin_sessions.json` rather
than zero — with 48 and 20 absent from the FIT session's definition too, which
is the strong form of the claim.

Two parts missed, and **both were faults in the instructions rather than in the
watch**:

- **`dropout` was pre-empted four seconds early.** The window armed at
  `...477`, the watch came off at `...501` for 29 seconds, and a resume landed at
  `...533` — 56 seconds into a 60-second window, so it ended as
  `effort_resumed`. Thirty untrusted of 61 would have refused comfortably. The
  table had no step telling the wearer to sit the window out, because the two
  parts before it end *with* a resume by design.
- **`ride_ended` never got its window.** Between the `no_baseline_history`
  refusal and the next press, 202 seconds of wall clock passed but only **169**
  reached the ride: 33 were spent paused, because a refusal leaves the ride
  paused and the table's next line said "sit 3 minutes" as though the clock were
  running. The gate is a hard 180.

Both are fixed above — every step now names the clock's state, and no step
assumes a resume the step before it did not make.

### The retry — both, cleanly

```
1788490172 start version=0.8.0-14-165ab48 max_hr=100 zones=5 weight=90
1788490172 cease hr=69 pct=69 active=0 -> too_short
1788490479 cease hr=98 pct=98 active=199 -> armed
1788490540 discard dropout hr=68 pct=68
1788490825 cease hr=106 pct=106 active=469 -> armed
1788490861 end -> ride_ended
1788490861 session status=0 recoveries=0 dropped=0 active=469 hr_avg=71 work_kj=0 trimp=20
```

`dropout` on 30 untrusted seconds of 61 — 51% trusted against a 90% gate — and
`ride_ended` on a window torn down 36 seconds in. `work_kj` absent again.

The stray `cease ... active=0 -> too_short` in the first second is an R1 pressed
the instant the ride began, and it is worth keeping: it is `MIN_EFFORT_S`
refusing a **zero-length bout**, which nothing had tested.

### What the ladder did

Session 1 recorded `zone_floors: [50,60,70,80,90]` against a watch set to
`[30,60,70,80,90,100]`. A declared `hrZoneCount` sends `applyZoneConfig()` down
the spread-from-maximum path, so the watch's own floors are never read and the
custom 30 was dropped. It coincides exactly on the standard 50–100% ladder,
which is why it had never shown before.

---

---

## 2026-09-03 — Session 2 (synthetic maximum, 100)

> **`hr_max_setting: 100`.** A code-path test. The three drops below are 38–50
> bpm where [Ride A](#2026-09-03--ride-a-real-maximum-184)'s were 17 and 23, and
> that is not a better result — it is a *worse* effort. Marching on the spot
> then sitting returns a heart rate the whole way to resting, which a real
> interval does not. Nothing here is physiology.

```
1788491851 start version=0.8.0-14-165ab48 max_hr=100 zones=5 weight=90
1788492113 cease hr=108 pct=108 active=261 -> armed
1788492174 recovery hr0=107 hr_end=68 drop=39 window=60 trusted=61 pct=107 src=1 curve=107,96,81,74,68,70,68
1788492525 cease hr=103 pct=103 active=591 -> armed
1788492586 recovery hr0=103 hr_end=65 drop=38 window=60 trusted=61 pct=103 src=1 curve=103,96,80,76,85,71,65
1788492892 cease hr=114 pct=114 active=889 -> armed
1788492953 recovery hr0=114 hr_end=64 drop=50 window=60 trusted=61 pct=114 src=1 curve=114,104,94,81,78,67,64
1788492967 session status=0 recoveries=2 dropped=1 active=889 hr_avg=69 work_kj=0 trimp=37
```

**The cap works, and drops the right end.** Three windows, `recoveries=2
dropped=1`, and the shared log kept the last two — `at_active_s` 591 and 889,
with 261 discarded. That is the shift-and-drop in `Service::keepRecovery()`
running for the first time. `work_kj` absent again, and the `.fit` agrees with
the shared log on active time, average heart rate and every zone bucket.

### A low-confidence excursion, recorded into a curve

Curve 2 is `[103, 96, 80, 76, 85, 71, 65]` — it rises 9 bpm at the
forty-second point. The raw seconds say why:

```
1788492562 P hr=73 trust=1
1788492563 P hr=88 trust=1     <- +15 bpm in one second
1788492564 P hr=88 trust=1
1788492565 P hr=85 trust=1
1788492566 P hr=85 trust=1     <- the curve samples here
1788492567 P hr=79 trust=2
1788492568 P hr=72 trust=3
```

A five-second excursion of up to 15 bpm, entirely at trust=1, bracketed by
trust=3 readings of 77 and 72 on a smoothly falling signal. `drop_bpm` is
untouched, since that is `hr0` minus `hr_end`; what carries the artefact is the
curve, which `trainkit.h` calls "the input any later curve fit would need". And
`trusted_s: 61` of 61 is a weaker claim than it looks, because trust=1 counts as
trusted.

### The excursion belongs to this regime, not to the sensor

The obvious conclusion — that `PRE_MAX_FALL_BPM` of 10 sits inside the wrist
sensor's noise — **does not survive comparing the two regimes**:

| | Ride A, real maximum | these sessions, synthetic |
|---|---:|---:|
| heart rate range, paused | 87–162 | 59–114 |
| paused seconds | 271 | 656 |
| **trust=1** | **9%** | **24%** |
| trust=3 | 54% | 34–54% |
| largest one-second move | **3 bpm** | **15 bpm** |
| trust=1 → trust=1 moves | n=8, mean 0.25, max **1** | n=110, mean 0.75, max **15** |

At real intensity the sensor is low-confidence a third as often, and the largest
move across Ride A's whole 271 paused seconds was 3 bpm. A stronger pulse gives
the optical sensor more to work with.

**And the gate can only ever run in the good regime.** `already_falling` is
tested against a baseline at or above 80% of maximum — 147 bpm on a real 184 —
so it never sees these heart rates unless the maximum is fake, which only ever
happens in a test. No gate change is warranted, and none is proposed.

**The limit, in the other direction**: 271 paused seconds and 8 trust=1 pairs
cannot rule out a rare excursion at real intensity; a 1-in-927 event need not
appear in that sample. This shows the confidence *distribution* is much better
where the gates operate. It does not show the tail is absent there.

---

---

## 2026-09-04 — Ride A's seconds, pulled at last, and the instrument for the ride still missing

> **No interval session has been ridden.** The watch was read on 2026-09-04;
> its newest activity is 2026-09-04 00:20, a desk ride at the synthetic
> maximum. The design in
> [`HR-TREND-PROMPT.md`](HR-TREND-PROMPT.md#2-the-design-that-came-out-of-it)
> is therefore **not built**, for the reason at the bottom of this section.

### Ride A's per-second stream was on the watch, and is now in the repository

Until 2026-09-04 the numbers in
[`HR-TREND-PROMPT.md` §1](HR-TREND-PROMPT.md#the-horizon-and-its-floor) rested
on a file that existed nowhere but the watch: this document kept Ride A's
`cease`, `recovery` and `session` lines and the two seven-point curves, and
nothing else. The comparison the trend experiment turns on is *against Ride A*,
so the baseline was one factory reset from being gone.

It is now [`Spin/Tests/pulled/20260903-rideA-real-max-184/`](../Tests/pulled/20260903-rideA-real-max-184),
alongside the `recovery.log` covering every session to date and the shared log
at `kept: 7`.

### The published horizon table reproduces exactly

`Tools/hr_trend.py` decoding the `.fit` through python-fitparse — which shares
no code with the writer — against the table `HR-TREND-PROMPT.md` §1 published:

| window | p50 \|Δ\| | p90 | reads flat | §1 said |
|---:|---:|---:|---:|---|
| 5 s | 1 | 3 | 58% | 1 / 3 / 58% |
| 10 s | 2 | 5 | 40% | 2 / 5 / 40% |
| 20 s | 3 | 8 | 23% | 3 / 8 / 23% |
| 45 s | 5 | 13 | 19% | 5 / 13 / 19% |

Every cell, from a different reader on a different file. The delta method and
the choice of seconds are settled, and the 15-second floor stands: the median
is the 1 bpm quantisation step at 5 s and first clears it at 15 s.

The two horizons §1 did not publish, now filled in — 1137 seconds with a
reading over 1404 elapsed, four pauses, 64–162 bpm, zone width 18.4 bpm:

| window | p50 | p90 | max | flat | p50 zw | p90 zw |
|---:|---:|---:|---:|---:|---:|---:|
| 15 s | 3 | 6 | 32 | 32% | 0.16 | 0.33 |
| 30 s | 4 | 10 | 25 | 21% | 0.22 | 0.54 |

### The one number that does not reproduce, and it is the one carrying a design claim

§1 reports 20 s and 45 s agreeing **94%** of the time and 5 s and 20 s **87%**,
and concludes that "stacked multi-horizon carets are redundant nine times in
ten". No definition tried reproduces those, on the very file they came from:

| "the two horizons agree" means | 5 s vs 20 s | 20 s vs 45 s |
|---|---:|---:|
| the signs match, a zero delta being its own state | 58% | 75% |
| three-way down/flat/up, flat below 2 bpm | 48% | 67% |
| the signs match, over seconds where neither is zero | 76% | 88% |
| they do not point to **opposite** sides | 83% | 90% |
| **§1's figure** | **87%** | **94%** |

The horizon table matching cell-for-cell rules out a different ride or a
different delta method, so the gap is the agreement statistic alone — and it is
not written down anywhere. The loosest reading, "never actually contradict each
other", is the closest and still falls 4 points short at both horizons.

**What Ride A actually supports** is narrower than "redundant nine times in
ten": over the seconds where both horizons are moving, 20 s and 45 s point to
**opposite sides 6%** of the time, and adjacent horizons much less — 15 s
against 20 s disagree outright on **1%**. That is a good argument for one mark
rather than a trail. It is not the argument §1 made, and the difference matters
because §1's version is the one that would justify never revisiting the
question.

### Where Ride A's peaks actually fell

A pause is where the wearer stopped pedalling, and Spin writes a `record` only
while the clock runs, so the gaps in the stream mark the efforts without
needing a lap press. Ride A's five riding blocks:

| block | length | start → peak | peak, relative to the moment riding stopped |
|---|---:|---|---:|
| 1 | 119 s | 64 → 96 | −105 s |
| 2 | 299 s | 88 → 102 | −31 s |
| **3** | 299 s | 89 → **161** | **−17 s** |
| **4** | 300 s | 129 → **162** | **−1 s** |
| 5 | 120 s | 121 → 125 | −100 s |

Blocks 3 and 4 are A3 and A5, the two hard efforts. Both peak within seconds of
the button, and block 4's peak *is* the ride's maximum of 162 — reached one
second before riding stopped, then held into the recovery window. On a
five-minute ramp the heart rate is still climbing when the effort ends. **This
is the lag, measured**, and it is the body: no display can show a peak that has
not happened yet.

Blocks 1 and 5 peak early with nothing behind them; block 1's 96 bpm at 14 s is
startup acquisition.

### The instrument: `Tools/hr_trend.py`

Given a Spin `.fit`, or an `_hr.csv` of the shape Squash pulls, it reports the
three things the experiment asks: the horizon table in bpm and zone widths;
what a second mark would add, as both "same state at all" and "point to
opposite sides"; and each effort's peak against its own end, using the laps
when the ride was lapped and the riding blocks between pauses when it was not.
`--json` writes the numbers out so two rides can be put side by side.

A window is counted only when the wearer was riding through all of it — every
window that would straddle a pause is dropped rather than bridged.

Checked four ways: on a synthetic 1 bpm/s ramp every horizon reads exactly its
own width and returns exactly `n − horizon` windows; on a synthetic 50 bpm step
across a pause no window straddles the gap; on the 10-minute Squash recording
the `.fit` and the `_hr.csv` — two independent records of the same seconds —
agree within 1 bpm at every horizon; and on Ride A it reproduces the published
table above.

### The control, and what it is worth

The [`Squash/Tests/pulled/`](../../Squash/Tests/pulled) recordings of
2026-09-03 are the only other per-second `HEART_RATE_EX` here — 64 to 110 bpm,
which against a real maximum of 184 is 35–60% and below every gate in this
feature. Their p50 at 5 s is 1 bpm in all three, the same quantisation floor
Ride A shows 52 bpm higher up, on mostly the strap where Ride A was wrist
optical. That is the floor holding across regimes and sensors, and it is all
these recordings are good for; they say nothing about intervals.

### The watch's maximum is still 100

`settings.json` reads `heartRateZones: [30,60,70,80,90,100]`. That is the
synthetic maximum
[the low-maximum technique](RECOVERY-FIELD-TEST.md#the-technique-lower-the-maximum)
calls for, left in place since 2026-09-03 — the instruction to "write down 184
first, and put it back at the end" was not completed.

**Put 184 back, and reopen Spin, before riding anything.** A changed maximum is
read once in `Service::run()`, so a ride started without reopening the app uses
the old one, and an interval ride recorded against a maximum of 100 is a
synthetic session whose heart-rate numbers mean nothing.

### Why the ghost tick is still not built

The experiment's terms are that the design is decided from the interval
comparison, and no interval session has been ridden. Building now would fix a
horizon and a full-scale point from one ride of 4–5 minute ramps, against a
complaint that is specifically about 20-second sprints.

What today changed is that the comparison is now *possible*: the baseline is in
the repository, the instrument reproduces its published numbers, and the only
missing half is one ordinary interval workout — [Session
5](RECOVERY-FIELD-TEST.md#session-5--an-interval-ride-an-ordinary-workout--done-2026-09-04).

Nothing found today argues against the design. The 15-second floor got
independent support, and the case for one mark over a trail got a real number
from Ride A rather than a desk session — 1% outright disagreement between
adjacent horizons. What it does argue against is §1's **94%**, which no longer
has a derivation anyone can point at.

---

---

## 2026-09-04 — the interval ride, and what it settles

> **`hr_max_setting: 184`.** A real ride at a real maximum, on
> `0.8.0-43-88d17f2`, wrist optical. Kept with its same-day steady control in
> [`Spin/Tests/pulled/20260904-intervals-real-max-184/`](../Tests/pulled/20260904-intervals-real-max-184).

45 minutes, 2700 active seconds, 84–163 bpm, average 134, 330 kJ entered,
Edwards TRIMP 126, lapped at the start of every hard effort:

| Set | Structure | Laps |
|---|---|---|
| A | 6 × 20 s hard / 40 s easy | 2–7 |
| B | 3 × 60 s hard / 60 s easy | 9–11 |
| C | 2 × 4 min hard / 3 min easy | 13–14 |

A steady ride 65 minutes earlier — 44 minutes, 82–128 bpm, no laps — is the
control, and it is a *within-day* one: same body, same sleep, same caffeine,
same sensor placement, differing only in how the work was arranged.

### The 20-second delta distribution, against Ride A

| | n | p50 | p90 | rising | falling | flat | largest fall |
|---|---:|---:|---:|---:|---:|---:|---:|
| Ride A — 4–5 min ramps | 1035 | 3 | 8 | 53% | 23% | 23% | **−9** |
| steady, same day | 2607 | 2 | 6 | 36% | 31% | 33% | −11 |
| **intervals** | 2676 | **3** | **9** | 40% | 33% | 27% | **−24** |

**Two things Ride A could not show.** Its record is 53% rising against 23%
falling, with a largest fall of 9 bpm, because
[its descents happened while paused](#2026-09-03--ride-a-real-maximum-184) and a
`record` is written only while the clock runs. The interval ride recovers
between efforts *while pedalling*, so the falling limb is in the file at last:
33% of seconds, and a fall nearly three times the largest Ride A ever recorded.

**And "intervals look dynamic for free" is only half true.** Against Ride A the
20-second p90 moves 8 → 9, which is nothing. Against the same day's steady ride
it moves 6 → 9, which is real. So the fixed scale does separate a hard session
from an easy one; it does not separate intervals from ramps.

### Half a zone width survives

`HR-TREND-PROMPT.md` proposed half a zone width per 20 s as full scale, from
Ride A's p90 of 0.43 zw. This ride's 20-second p90 is 9 bpm — **0.49 zw** — and
the steady control's is 0.33 zw. The constant needs no change.

### Where the peak falls, which is the whole complaint

Each effort's peak, relative to the moment the hard part **ended**:

| Set | effort | peak, relative to the effort's end |
|---|---|---|
| **A** | 20 s | **+15, +28, +18, +25, +15, +18 s** — median **+18** |
| B | 60 s | +3, +3, +2 s — median **+3** |
| C | 4 min | +34, −36 s — median −1 |

**Six sprints out of six peaked after the sprint was over**, between 15 and 28
seconds late. For a 20-second effort the entire heart-rate response arrives
after the rider has stopped. That is the lag, measured, and no display can take
it back.

At 60 seconds the peak is essentially real-time.

### What the ghost tick would actually have shown

Simulating a 20-second ghost second by second, **while each effort was on**:

| Set | reads rising | reads falling | flat | median gap |
|---|---:|---:|---:|---:|
| **A — 20 s** | **37%** | **42%** | 21% | 0.22 zw |
| B — 60 s | **72%** | 16% | 12% | 0.33 zw |
| C — 4 min | 60% | 5% | 35% | 0.11 zw |

**During a sprint the mark points down more often than up.** Not because it is
wrong — the heart rate genuinely is falling, decaying from the previous rep
whose peak landed 18 seconds into this one. The mark is accurate and the rider
would still misread it, because at that timescale heart rate has come loose from
effort altogether.

### The decision: build it, and say what it is not for

**The ghost tick earns its place.** At 60-second efforts it reads rising 72% of
the time with a median gap of a third of a zone, and the 20-second p90 of 0.49
zw confirms the half-a-zone scale without a new number. Horizons still barely
disagree — 15 s against 20 s conflict on **1%** of moving seconds, as on Ride A —
so **one mark, not a trail**. The 45-second horizon's conflict rose from 6% to
11%, which argues against a second mark rather than for one.

**And 20-second sprints are out of its scope, permanently.** The peak arrives
15–28 seconds after the effort ends; that is physiology, and the honest response
is to add nothing for it rather than to tune a horizon that cannot exist. A
rider doing 20-second efforts should watch the clock, which the screen already
shows at the largest size that fits.

Nothing here overturns
[the design](HR-TREND-PROMPT.md#2-the-design-that-came-out-of-it): no threshold
was invented, the unit stays the zone width, and the horizon stays fixed at
20 s. What changed is that its limit is now measured rather than suspected.
