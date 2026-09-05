# Heart-rate recovery: what is left to test

The remaining work on
[heart-rate recovery and the shared session log](../README.md#how-fast-your-heart-falls-when-you-stop),
and how to read what it produces.

**What has already been run, and what it found, is in
[`RECOVERY-FIELD-RESULTS.md`](RECOVERY-FIELD-RESULTS.md).** Read its coverage
table before running anything here: every discard reason but `source_changed`
has already fired, so most of the original prescription is spent.

The two sessions outstanding are at a desk, and neither needs a bike, a strap,
or any hard effort. Ride A — the one prescribed hard session — is
**done and does not need repeating**, because the measurement happens on every
pause of every ride.

[Session 5](#session-5--an-interval-ride-an-ordinary-workout--done-2026-09-04),
which belonged to a different experiment, **ran on 2026-09-04** and settled it;
the two desk sessions are all that is left here.

> Treat a wrong number as information, not as a failure. Every gate's threshold
> is in [EffortKit's README](../../EffortKit/README.md#the-gates-and-what-each-one-is-for)
> with the source it came from, so if one is wrong you can see what it was
> derived from.

## What you need first

1. **A maximum heart rate set on the watch.** Settings → heart-rate zones. With
   none set, every window is discarded as `no_max_hr` — which is Session 3's
   whole point, so set one for everything else.
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
   this feature being broken. **Pull `recovery.log` and `spin_sessions.json`
   off the watch first if you mean to clear them** — they append across rides
   and are the only copy of every session before this one, which is why Ride A's
   are now [in the repository](../Tests/pulled/20260903-rideA-real-max-184).
   Clearing is not required; the log caps itself at 128 KiB.

## After installing a build, before trusting it

**This is a check on an install, not on the feature.** Every gate below it has
fired on hardware and is recorded in
[`RECOVERY-FIELD-RESULTS.md`](RECOVERY-FIELD-RESULTS.md#what-is-already-proven);
none of that carries over to the next `.uapp` you put on the watch. A stale
binary announces itself in no other way, so this runs once per build and then
never again for that build.

### The three-minute desk ride

Wear the watch, sit down, and do a ride that requires nothing of you:

1. Open Spin, **R1 START**, and leave it alone for **60 seconds**.
2. **Press R1** to pause. Wait **10 seconds**.
3. **L1 SAVE**, put any number in with L1/L2, **R1 SAVE**.

Now plug the watch in and look. All five of these must hold, and each one that
does not is a different fault:

| Check | If it fails |
|---|---|
| The app started at all | The `Engine ABI mismatch` guard in `Service::run()` fired and the Service returned immediately — a stale `libspin_engine.a` against a changed struct. Rebuild. |
| **`recovery.log` exists in Spin's folder** | `IFile::open(write, no-override)` neither appends nor creates on this kernel, and the fallback in `EventLog::open()` missed it. **Stop here** — everything below is blind without it. |
| Its first line reads `start version=<the build you installed> max_hr=<n>`, **n > 0** | A **wrong version** means a stale `.uapp` is still booting, which is the headline failure in [`Docs/INSTALLING.md`](../../Docs/INSTALLING.md) and announces itself in no other way. **A missing `version=` means the same thing** — the field has been in the start line since this document existed, so a line without it is an older build. `max_hr=0` usually means the watch has no maximum set, and every window would then be discarded as `no_max_hr` — but check the next row before believing the watch. |
| `zones=<n>` is one **fewer** than `heartRateZones` in the watch's own `settings.json` | Both numbers come from the same ladder, and the last threshold is the maximum rather than a floor. Equal counts, with `max_hr=0` beside them, is Spin misreading the message and not a watch with no zones — which is what happened on 2026-09-03, below. |
| It contains `cease ... -> too_short` | The pause never reached the detector — `pauseTrack()` is not calling `spin_engine_cease()`. |
| `SharedData/spin_sessions.json` exists, `status=0` on the log's `session` line, and `kept` has **risen by one** — it accumulates across rides and stood at 7 on 2026-09-04, so a fresh install does not reset it | The write path failed. The `status=` number says where: `1` refused, `2` out of memory, `3` commit rename failed, `4` write failed (`SharedLog.hpp`). |

Then do it **once more**. `kept` must rise by one again and leave a
`spin_sessions.json.bak` beside it holding the previous value — that is the
rotate-and-rename commit working, which is the part a power cut would otherwise
ruin.

### What is already checked without you

`ctest --test-dir build`, of which these are this feature:

- **`spin-sharedlog-tests`** — the commit sequence, the `.bak` rotation, the
  refusal to overwrite a newer schema, the rotation of an unreadable one, and
  the diagnostic log's create/append/cap behaviour, against the SDK's in-memory
  filesystem. That is evidence about *this code's logic*, not about the
  kernel's `open()` and `rename()` mapping — which is why the desk check above
  exists at all.
- **`effortkit-tests`** and **`spin-engine-tests`** — every gate, the log's
  schema and bounds, and the C ABI over them.

## Two different things, and the whole feature is the gap between them

This document never says "pause" as an instruction, because the word means two
things here and only one of them reaches the watch:

- **Press R1** — the button. This is the *only* thing the detector sees. It is
  what calls `spin_engine_cease()` and opens the window.
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

## The sessions that are left

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
moved into Session 1, which has run. C4 has nothing to do with the sensor and
ran with it.

Nothing measured in the meantime is wasted. Every recovery records `source` by
name in the shared log and every `record` carries `hr_source`, so wrist-era
measurements are labelled where they are captured and can be filtered out — or
compared against — once strap data exists.

### The technique: lower the maximum

Every intensity gate is a fraction of the watch's own maximum: `MIN_HR0_PCT_MAX`
is 80% **of that setting**. Lower the setting and the whole test scales down onto
an identical code path, because nothing in the engine knows the number is
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

> **Everything recorded this way is a code-path test and nothing else.** A
> session run against a synthetic maximum carries `hr_max_setting: 100` in the
> shared log, and that field is the only thing separating it from a real ride
> afterwards — **filter on it before reading any number as physiology**. Two
> things are wrong with these sessions' numbers, not one: the effort is trivial,
> so a heart rate returns all the way to resting and a *bigger* `drop_bpm` means
> *less* work rather than more; and the wrist sensor is measurably less
> confident at these heart rates, which
> [Session 2](RECOVERY-FIELD-RESULTS.md#2026-09-03--session-2-synthetic-maximum-100) shows corrupting a
> recorded curve. Neither applies to a ride at a real maximum.

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

### Session 5 — an interval ride (an ordinary workout) — **done 2026-09-04**

> **Run, and it answered the question.** See
> [the write-up](RECOVERY-FIELD-RESULTS.md#2026-09-04--the-interval-ride-and-what-it-settles).
> The ride and its same-day steady control are in
> [`Spin/Tests/pulled/20260904-intervals-real-max-184/`](../Tests/pulled/20260904-intervals-real-max-184).
> It does not need repeating; what is below is kept because it is how the ride
> was specified and what it was read for.

Not a prescription, and not part of the recovery feature. This is the one piece
of evidence the
[heart-rate trend experiment](HR-TREND-PROMPT.md#3-the-open-question-which-is-the-actual-job)
has never had: **per-second heart rate through short, hard efforts**. Every
number that experiment argues from comes from Ride A's 4–5 minute ramps, and
the wearer's complaint is about 20-second sprints.

Ride whatever session you were going to ride. Two things only:

| # | Do | Why |
|---|---|---|
| 1 | Ride an **interval** session — short hard efforts with recoveries between them, whatever your usual is | the delta distribution at short efforts is the whole question |
| 2 | **Press R2 (LAP) at each change** — at the start of each effort and at the end of it | a lap is the only mark of structure that costs nothing mid-effort; without one, the efforts can only be read from the pauses, and an interval session ridden straight through has none |

Nothing else changes. Pause and save exactly as usual; the recovery windows
that open on the way are a bonus rather than the point, and no hard effort is
spent on diagnostics.

> **First: put the maximum back to 184 and reopen Spin.** The watch was read on
> 2026-09-04 still carrying `heartRateZones: [30,60,70,80,90,100]`, the
> synthetic maximum. `loadSystemSettings()` runs once in `Service::run()`, so a
> ride started without reopening the app uses the old number — and an interval
> ride recorded at a maximum of 100 is a synthetic session that answers nothing.

Pull the ride's `.fit` into a dated directory beside
[Ride A's](../Tests/pulled/20260903-rideA-real-max-184), which is already here.
Then, with `python-fitparse` installed:

```sh
Tools/hr_trend.py RIDE.fit --max-hr 184 --zones 5 --json interval.json
Tools/hr_trend.py Spin/Tests/pulled/20260903-rideA-real-max-184/activity_20260903T205637.fit \
    --max-hr 184 --zones 5 --json ride-a.json
```

Three things to read off the two tables, and each one changes the design:

1. **Is p90 of the 20-second delta wider than Ride A's 8 bpm (0.43 zone
   widths)?** If it is not, "intervals look dynamic for free" is wrong and a
   fixed scale is the wrong choice.
2. **Do two horizons point to opposite sides more often than on Ride A?** Ride
   A's are 1% for 15 s against 20 s and 6% for 20 s against 45 s. Stay near
   those and one mark is the answer; go well above and a trail at 20/40/60 s
   starts to earn its clutter.
3. **Where does each effort's peak fall relative to its end?** Ride A's two hard
   efforts peaked 17 s and 1 s *before* the wearer stopped. A peak landing at or
   after the end is the physiology, and no display can fix it — which would make
   the honest answer that a 20-second effort has no heart-rate feedback at
   all.

Record what the two tables said in
[`RECOVERY-FIELD-RESULTS.md`](RECOVERY-FIELD-RESULTS.md), under a dated
heading, with the raw numbers.

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

The log **appends across rides** and stops at 128 KiB, so pull it before you
clear it — it is the only copy of every session before this one. Clearing keeps
a session's own lines easy to find; it is not required. It is a diagnostic, not
a feature — once these questions are answered it should come out.

### What to check in any run

1. **`recovery.log` exists at all.** If it does not, `IFile::open(write,
   no-override)` does not create a missing file on this kernel and the fallback
   in `EventLog::open()` did not catch it. Everything else is then blind, so fix
   this first.
2. **The start line names the build you installed**, and its `max_hr` and
   `zones` are what you set. A missing `version=` is a stale `.uapp`; a `zones=`
   equal to the count in the watch's own `settings.json` rather than one fewer
   means the ladder is being split wrong.
3. **Every `cease` was followed by what you expected** — and remember a `cease`
   line can only say `no_max_hr`, `too_short` or `no_baseline_history`. Every
   other reason arrives on the next line as a `discard`, so `armed` followed by
   a `discard` is the gate working rather than failing.
4. **`drop_bpm` is plausible** — 15–45 for most people sitting still after hard
   work, and only where `hr_max_setting` is real. A drop under 5 or over 70
   means look at the `curve`, which is the raw evidence.
5. **The `curve` falls, after it stops rising.** A rise in the first ten or
   twenty seconds is ordinary: heart rate overshoots after effort ceases, and
   wrist optical lags a fast fall on top of that. Check `source` first — only if
   it is `external` and the curve is ragged in the **middle** is this the strap
   arguing with the wrist sensor, which `hr_source` in the `.fit` settles second
   by second. A single point out of line on an otherwise smooth curve is more
   likely a low-confidence sample; the `trust=` on the raw `P` seconds says.
6. **`spin_sessions.json` parses**, and each session's `zone_s` sums to its
   `active_s`. That is the same property `SecondsAccrual` holds for the `.fit`,
   so the two records must agree.
7. **`hr_avg` and `active_s` match the `.fit`'s** `avg_heart_rate` and
   `total_timer_time`. If they do not, one of the two records is wrong about the
   same ride.
8. **`kept` rose by one**, oldest session first, with a `.bak` beside the file
   from the last commit.

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

