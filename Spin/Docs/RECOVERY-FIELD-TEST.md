# Heart-rate recovery: what is left to test

The remaining work on
[heart-rate recovery and the shared session log](../README.md#how-fast-your-heart-falls-when-you-stop),
and how to read what it produces.

**What has already been run, and what it found, is in
[`RECOVERY-FIELD-RESULTS.md`](RECOVERY-FIELD-RESULTS.md).** Read its coverage
table before running anything here: every discard reason but `source_changed`
has already fired, so most of the original prescription is spent.

Two sessions are outstanding, both at a desk, neither needing a bike, a strap,
or any hard effort. Ride A — the one hard session — is **done and does not need
repeating**, because the measurement happens on every pause of every ride.

> Treat a wrong number as information, not as a failure. Every gate's threshold
> is in [TrainKit's README](../../TrainKit/README.md#the-gates-and-what-each-one-is-for)
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

## The two sessions that are left

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

