# The ride that tests heart-rate recovery

A prescribed session for the first hardware run of
[heart-rate recovery and the shared session log](../README.md#how-fast-your-heart-falls-when-you-stop).
It is designed to make **every gate fire at least once** and to leave three
pieces of evidence that can be checked against each other: the `.fit`, the
shared log, and `recovery.log`.

It is not a training session. Do it on a day you were going to ride easy
anyway; there are four hard minutes in it and a lot of sitting still.

> **Before you start**: read
> [what is left](#what-is-left-and-the-cheapest-way-to-get-it) before riding any
> of A–E. Ride A is [done](#2026-09-03--ride-a), Ride C is deferred on a strap
> firmware bug, and B, D and E have cheaper forms that reach the same gates at a
> desk. Treat a wrong number as information, not as a failure — the point is to
> find out. Treat a
> wrong number as information, not as a failure — the point of the ride is to
> find out. Every gate's threshold is written down in
> [TrainKit's README](../../TrainKit/README.md#the-gates-and-what-each-one-is-for)
> with the source it came from, so if one is wrong you can see what it was
> derived from.

## What you need first

1. **A maximum heart rate set on the watch.** Settings → heart-rate zones. If
   the watch has none, every measurement is discarded as `no_max_hr` — which is
   Ride D's job, so set it for A–C.
2. **Write down your maximum**, call it `HRmax`, and work out **80% of it**.
   That is the intensity floor. For 190 bpm it is 152.
3. **Do not wear the chest strap** while the two-minute disconnect is
   outstanding — see [the strap](#the-strap-and-why-ride-c-is-deferred). A strap
   that drops mid-window is discarded as `source_changed`, which looks like the
   gate misfiring. Wrist optical is the worst case for dropouts *while riding*,
   but no gate measures trust there: the window runs while you sit still with
   your hands off the bars, and Ride A took two windows at 61 trusted seconds
   out of 61.
4. **Install this build following [`Docs/INSTALLING.md`](../../Docs/INSTALLING.md)**,
   which is not optional reading: the `.uapp` goes in, every other one comes
   out, and the watch is **power-cycled** — a USB replug is not a reboot, and a
   stale binary or an unregistered app both fail silently, looking exactly like
   this feature being broken. Then **delete `recovery.log`** from Spin's folder
   if one is already there.

## Before you leave the house

Seventy minutes of riding is a lot to spend finding out that the log file was
never created. Two checks first, and neither needs a bike.

### On the desk: does the plumbing work? (3 minutes)

Wear the watch, sit down, and do a ride that requires nothing of you:

1. Open Spin, **R1 START**, and leave it alone for **60 seconds**.
2. **Press R1** to pause. Wait **10 seconds**.
3. **L1 SAVE**, put any number in with L1/L2, **R1 SAVE**.

Now plug the watch in and look. All five of these must hold, and each one that
does not is a different fault:

| Check | If it fails |
|---|---|
| The app started at all | The `TrainKit ABI mismatch` guard in `Service::run()` fired and the Service returned immediately — a stale `libtrainkit.a` against a changed struct. Rebuild. |
| **`recovery.log` exists in Spin's folder** | `IFile::open(write, no-override)` neither appends nor creates on this kernel, and the fallback in `EventLog::open()` missed it. **Stop here** — everything below is blind without it. |
| Its first line reads `start version=<the build you installed> max_hr=<n>`, **n > 0** | A **wrong version** means a stale `.uapp` is still booting, which is the headline failure in [`Docs/INSTALLING.md`](../../Docs/INSTALLING.md) and announces itself in no other way. **A missing `version=` means the same thing** — the field has been in the start line since this document existed, so a line without it is an older build. `max_hr=0` usually means the watch has no maximum set, and Rides A–C would produce nothing but `no_max_hr` — but check the next row before believing the watch. |
| `zones=<n>` is one **fewer** than `heartRateZones` in the watch's own `settings.json` | Both numbers come from the same ladder, and the last threshold is the maximum rather than a floor. Equal counts, with `max_hr=0` beside them, is Spin misreading the message and not a watch with no zones — which is what happened on 2026-09-03, below. |
| It contains `cease ... -> too_short` | The pause never reached the detector — `pauseTrack()` is not calling `trainkit_recovery_cease()`. |
| `SharedData/spin_sessions.json` exists and ends `"kept":1` with `status=0` on the log's `session` line | The write path failed. The `status=` number says where: `1` refused, `2` out of memory, `3` commit rename failed, `4` write failed (`SharedLog.hpp`). |

Then do it **once more**. The second run must give `"kept":2` and leave a
`spin_sessions.json.bak` beside it — that is the rotate-and-rename commit
working, which is the part a power cut would otherwise ruin.

### Optional: force a real measurement without exercise (5 minutes)

The gates need 80% of your maximum, which you cannot reach at a desk — unless
you move the maximum. On the watch, **temporarily set your maximum heart rate to
just below your resting rate** (if you sit at 65, set 75). Then:

1. Start a ride and sit still for **4 minutes** — past the 180 s effort gate.
2. **Press R1.** Sit still, not moving, for **90 seconds**.
3. Save.

You should get a real `recovery` line with a `curve`, and a session carrying one
recovery. **The number is physiologically meaningless** — it is a resting heart
rate drifting by a beat or two — but it proves the entire path end to end: the
window, the curve, the struct across the C ABI, the JSON, and the commit.

Afterwards: **put your real maximum back**, and delete both
`spin_sessions.json` and `recovery.log` so the field test starts clean.

### What is already checked without you

`ctest --test-dir build` covers four suites, two of which are this feature:

- **`spin-sharedlog-tests`** — the commit sequence, the `.bak` rotation, the
  refusal to overwrite a newer schema, the rotation of an unreadable one, and
  the diagnostic log's create/append/cap behaviour, against the SDK's in-memory
  filesystem. That is evidence about *this code's logic*, not about the
  kernel's `open()` and `rename()` mapping — which is why the desk check above
  exists at all.
- **`trainkit-tests`** — every gate, and the log's schema and bounds.

## Two different things, and the whole feature is the gap between them

This document never says "pause" as an instruction, because the word means two
things here and only one of them reaches the watch:

- **Press R1** — the button. This is the *only* thing the detector sees. It is
  what calls `trainkit_recovery_cease()` and opens the window.
- **Stop pedalling** — the physical act. The watch **cannot see this at all**.
  There is no cadence sensor and no way to tell a stationary rider from a
  soft-pedalling one.

A valid measurement needs **both, at the same moment**. Get them out of step and
you measure something else:

| Press R1 | Stop pedalling | What you get |
|---|---|---|
| yes | yes, at the same time | the measurement this feature is for |
| yes | no — you keep spinning | a window over *active* recovery, which Barak et al. found is 41% slower. The file cannot tell, and neither can you afterwards. |
| no | yes | nothing at all happens; 40 s later, R1 gives `already_falling` |
| no | no | nothing |

That second row is the trap the README calls the biggest one, and it is why the
instructions below are always spelled out as a button and a body separately.

The buttons, throughout: **R1 while riding pauses the clock. R1 again resumes
it. L1 while paused is SAVE.** Nothing below ever asks you to "pause" — it asks
for a button, or for your legs, or for both.

---

## Ride A — the one that should work (done, 2026-09-03)

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

## Ride B — the ones that should be thrown away (about 20 minutes)

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
[the low-maximum sessions](#the-technique-lower-the-maximum) exploit.

B3 leaves a session in the shared log with **no recoveries at all**. That is a
real state and the file must show it rather than omitting the session — though
the three desk sessions of 2026-09-03 have already demonstrated it, so only
`end -> ride_ended` is still unrun here.

## Ride C — the sensor, and the file (about 15 minutes)

> **C1–C3 deferred** on the strap's two-minute disconnect — see [the strap](#the-strap-and-why-ride-c-is-deferred). C2's dropout half and C4 have moved into [Session 1](#session-1--the-refusals-about-25-minutes-at-a-desk).

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

## Ride D — no zones (5 minutes)

> **Superseded by [Session 3](#session-3--no-zones-about-3-minutes)**: `no_max_hr` is tested before the effort gate, so the four hard minutes below buy nothing.

1. Turn the watch's heart-rate zones **off** in settings.
2. Ride hard 4 minutes, stop pedalling and **press R1**, sit 90 seconds, finish
   and save.
3. Turn the zones back on.

Expect `cease ... -> no_max_hr` and a session in the log with
`hr_max_setting: 0`, `zone_count: 0`, and **no `edwards_trimp`**.

## Ride E — a ladder Edwards never wrote weights for (5 minutes)

> **Superseded by [Session 4](#session-4--three-zones-about-6-minutes)**, which is the same test at a desk.

1. On the phone, set Spin's `hrZoneCount` to **3**.
2. Ride 4 minutes at any intensity, finish and save.
3. Set it back to **5** (or 0).

Expect the session to have `zone_count: 3`, three `zone_floors`, four `zone_s`
buckets, and **no `edwards_trimp` field at all**. Ride A's session, by contrast,
must have one.

---

## What is left, and the cheapest way to get it

Rides A–E above are the original prescription. Ride A has been done and does not
need doing again; what follows replaces B–E with sessions that reach the same
gates without hard effort, and says which gate each one is for.

### The strap, and why Ride C is deferred

Reported 2026-09-03: **the strap disconnects after about two minutes**, with a
firmware fix expected the following week. Until it lands, everything here is
wrist optical.

C1–C3 defer, because their mechanism is removing a *working* strap mid-window.
Meanwhile **do not wear the strap at all**: one that connects and drops at two
minutes puts a sensor change inside any window spanning it, which is discarded
as `source_changed` — manufacturing precisely the failure that gate exists to
catch, and reading as a bug in the gate rather than in the firmware. Wrist-only
makes the source constant by construction. `source_changed` is therefore the one
gate nothing below can reach, and it stays unrun.

C2's **dropout** half needs no strap. The 90% gate counts untrusted seconds
inside the window, and lifting the watch off the wrist produces them, so it
moves into Session 1. C4 has nothing to do with the sensor and folds into any
ending.

Nothing measured in the meantime is wasted. Every recovery records `source` by
name in the shared log and every `record` carries `hr_source`, so wrist-era
measurements are labelled where they are captured and can be filtered out — or
compared against — once strap data exists.

### The technique: lower the maximum

Every intensity gate is a fraction of the watch's own maximum: `MIN_HR0_PCT_MAX`
is 80% **of that setting**. Lower the setting and the whole test scales down onto
an identical code path, because nothing in TrainKit knows the number is
arbitrary.

Two facts make it free rather than a compromise:

- **The 180-second gate is not an intensity gate.** It compares unpaused clock
  seconds, and the watch cannot see pedalling at all — which is the distinction
  [this document is built around](#two-different-things-and-the-whole-feature-is-the-gap-between-them).
  Three minutes of sitting satisfies it.
- **Every session below but one produces no measurement by design.** They are
  refusals, so there is no physiology to lose. The exception is Session 2, whose
  measurements are meaningless and are stamped `hr_max_setting: 100` in the
  shared log for exactly that reason.

Set the maximum so that 80% of it is a heart rate you reach by standing up and
marching on the spot. **100 is the suggestion**: the gate lands at 80 bpm, and
the already-falling strip is `(peak − 10) − 80` — ten bpm wide at a peak of 100,
against the 3.8 measured at a real maximum of 184.

**Write down 184 first, and put it back at the end.**

### What is already proven

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

### Before any session: the clock, and the app

Two things fail silently, and both have already cost a run.

**Every R1 that pauses needs an R1 that resumes.** The clock stops the moment
you press R1 and does not move again until you press it a second time, and
`MIN_EFFORT_S` counts **unpaused** seconds only. Session 1's last part failed on
exactly this: 202 seconds of wall clock passed between two `cease` lines, but 33
of them were spent paused after a refusal, so the ride counted 169 and the
180-second gate refused it. Every table below therefore names the clock's state
after each step, and no step assumes a resume that the step before it did not
make.

**A changed maximum needs Spin restarted.** `loadSystemSettings()` runs once in
`Service::run()`, not per ride — `loadConfig()` and `applyZoneConfig()` are the
ones that re-run in `startTrack()`. Change the watch's maximum while Spin is
open and every ride in that session still uses the old one. Close the app and
reopen it, and confirm from the dial before spending twenty minutes: at a
maximum of 100 the floors are 50/60/70/80/90, so sitting still at ~68 bpm lights
zone 2, where at 184 the rim stays dark.

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

### Session 3 — no zones (about 3 minutes)

| # | Do | Clock after | Expect |
|---|---|---|---|
| 1 | Turn the watch's heart-rate zones **off**, then close and reopen Spin | | the start line reads `max_hr=0 zones=0` |
| 2 | **START**, then **press R1** | **paused** | `cease ... -> no_max_hr` |
| 3 | **L1 SAVE** | saved | `hr_max_setting: 0`, `zone_count: 0`, **no** `edwards_trimp` |

`cease()` tests `max_hr == 0` **before** the effort gate, so it short-circuits
and no wait is needed at all — the original Ride D's four hard minutes bought
nothing. Turn the zones back on afterwards, and reopen Spin again.

### Session 4 — three zones (about 6 minutes)

| # | Do | Clock after | Expect |
|---|---|---|---|
| 1 | Set Spin's `hrZoneCount` to **3**. This one needs no restart — `loadConfig()` re-runs every ride | | — |
| 2 | **START**, then sit **4 minutes** at any intensity | running | — |
| 3 | **Press R1** to pause, then **L1 SAVE** | saved | `zone_count: 3`, three `zone_floors`, four `zone_s` buckets, **no** `edwards_trimp` |
| 4 | Set `hrZoneCount` back to 5 or 0 | | — |

The only session that exercises the app-config path at all: `app_config.json`
has never carried anything but `energyInKilojoules`.

**A custom ladder on the watch is only honoured at `hrZoneCount: 0`.** Session 1
was run with the watch set to `[30,60,70,80,90,100]` and recorded
`zone_floors: [50,60,70,80,90]` — because a declared count sends
`applyZoneConfig()` down the spread-from-maximum path and the watch's own floors
are never read. On the standard 50–100% ladder the two coincide exactly, which
is why it has never shown before.

### Ride A never needs running again

The measurement happens on **every pause of every ride**, with no setting and no
button. A prescribed hard session was the way to get the first one; ordinary
training is the way to get the rest, and each arrives labelled with its `source`,
its `hr0_pct_max` and the ladder it was taken against. The open questions —
whether `drop_bpm` is stable, whether the 60-second window is longer than anyone
waits — are answered by accumulating rides, not by riding hard on purpose.

## Reading it back

Mount the watch over USB. Three files matter:

| File | Where | What it proves |
|---|---|---|
| `recovery.log` | Spin's own folder | Why each window did what it did, second by second |
| `spin_sessions.json` | `SharedData/` | What survived into the record |
| the `.fit` | the activity folder | That the ride itself is unharmed |

`recovery.log` is plain text, one event per line, `<utc> <event> key=value`:

```
1756800000 start version=0.9.0 max_hr=184 zones=5 weight=90
1756800030 A hr=131 trust=3 zone=2 pct=69
...
1756800612 cease hr=171 pct=90 active=600 -> armed
1756800613 P hr=171 trust=3 zone=5 pct=90
1756800614 P hr=170 trust=3 zone=5 pct=89
...
1756800673 recovery hr0=171 hr_end=118 drop=53 window=60 trusted=61 pct=90 src=2 curve=171,158,147,138,131,124,118
1756800690 session status=0 recoveries=1 dropped=0 active=1500 hr_avg=142 work_kj=430 trimp=112
```

`A` is a riding second (one every 30 s), `P` is a paused second (one every
second, because that is where a window runs). `status=0` on the session line is
`SharedLog::Status::OK`; anything else is in `SharedLog.hpp`.

The log **appends across rides** and stops at 128 KiB, so pull it and delete it
between sessions. It is a diagnostic, not a feature — once these questions are
answered it should come out.

### The eight things to check

1. **`recovery.log` exists at all.** If it does not, `IFile::open(write,
   no-override)` does not create a missing file on this kernel and the fallback
   in `EventLog::open()` did not catch it. Everything else in this document is
   then blind, so fix this first.
2. **Ride A produced two `recovery` lines**, and `spin_sessions.json` has both.
3. **`drop_bpm` is plausible** — somewhere in the 15–45 range for most people
   sitting still after hard work. A drop under 5 or over 70 means look at the
   `curve`, which is the raw evidence.
4. **The `curve` falls, after it stops rising.** A rise in the first ten or
   twenty seconds is ordinary: heart rate overshoots after effort ceases, and
   wrist optical lags a fast fall on top of that. Both of Ride A's curves rise
   161 → 162 before turning over, on a ride with no strap in it at all, and the
   raw `P` seconds behind them agree — so the curve is faithful to what the
   sensor reported. Check `source` first: only if it is `external` and the
   curve is ragged in the **middle** is this the strap arguing with the wrist
   sensor, which `hr_source` in the `.fit` will then settle second by second.
5. **Every gate in Rides A–E fired**, and each was the reason predicted above.
   A gate that never fires is a gate that has never been tested.
6. **`spin_sessions.json` parses**, and each session's `zone_s` sums to its
   `active_s`. That is the same property `SecondsAccrual` holds for the `.fit`,
   so the two records must agree.
7. **`hr_avg` and `active_s` match the `.fit`'s** `avg_heart_rate` and
   `total_timer_time`. If they do not, one of the two records is wrong about the
   same ride.
8. **Five sessions are in the file after Ride E**, oldest first, `kept: 5`,
   `dropped: 0`, and a `.bak` beside it from the last commit.

### What would change the design

- **Nothing at all was measured across A and C.** The 80%-of-maximum floor is
  the most likely cause, and it is the gate with the weakest provenance — it is
  matched to Barak et al.'s protocol rather than derived from a threshold study.
  `recovery.log` has your actual `pct=` at every R1, so the right number can
  be re-derived from the ride instead of argued.
- **`already_falling` fires on A3 or A5**, where it should not. The 10 bpm
  threshold is derived, not measured on this wearer; the `A` lines in the 30 s
  before you pressed R1 are exactly what it should be re-measured from.
- **`dropout` fires on a window you did not sabotage.** Spin measured 5%
  untrusted seconds over two rides with a strap; if yours is much worse, the 90%
  gate is set for someone else's sensor.
- **The window almost never survives**, because 60 seconds of sitting
  still is longer than anyone waits. That would be the finding that matters
  most, and the honest response is not to shorten the window — a 30 s number is
  not comparable to a 60 s one — but to say so on the paused screen so a wearer
  who wants the measurement knows to wait.

Record what happened here, in this file, under a dated heading. A field test
whose results live only in a chat window has not been done.

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
  [the eight](#the-eight-things-to-check), failing. The file rounds now.
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

- Closed by [Ride A](#2026-09-03--ride-a), later the same day: two windows
  survived and the whole path past `armed` ran.
- `Docs/INSTALLING.md`, linked twice above and once from `Service.cpp`, does not
  exist.

---

## 2026-09-03 — Ride A

On `0.8.0-14-165ab48`, wrist optical throughout, no strap (see
[the strap](#the-strap-and-why-ride-c-is-deferred)). **Both windows produced a
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

## 2026-09-03 — Session 1, in two runs

Maximum temporarily set to 100 so the intensity gates were reachable by marching
on the spot. Both sessions carry `hr_max_setting: 100`, which is what keeps them
out of Ride A's real numbers.

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

