# The ride that tests heart-rate recovery

A prescribed session for the first hardware run of
[heart-rate recovery and the shared session log](../README.md#how-fast-your-heart-falls-when-you-stop).
It is designed to make **every gate fire at least once** and to leave three
pieces of evidence that can be checked against each other: the `.fit`, the
shared log, and `recovery.log`.

It is not a training session. Do it on a day you were going to ride easy
anyway; there are four hard minutes in it and a lot of sitting still.

> **Before you start**: no window here has ever produced a measurement on
> hardware — only the desk check below has run, on
> [2026-09-03](#2026-09-03--the-desk-check), where every gate that fired was a
> refusal. Treat a
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
3. **Wear the chest strap.** The wrist sensor with your hands on the bars is
   the worst case for dropouts, and Ride C tests that on purpose.
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

## Ride A — the one that should work (about 25 minutes)

The measurement this feature exists for, taken twice, plus the two "not enough
effort" gates.

| # | Do | Expect in `recovery.log` |
|---|---|---|
| A1 | Start the ride. Pedal easy for **2 minutes**. Stop pedalling and **press R1** together. | `cease ... -> too_short` |
| A2 | **R1** to resume. Ride easy — **below 80% of HRmax** — for **5 minutes**. Stop pedalling and **press R1** together. | `cease ... -> too_easy` |
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

Each of these is a window that opens and then correctly produces nothing.

| # | Do | Expect |
|---|---|---|
| B1 | Hard 5 minutes to above 80%. Then **stop pedalling and do NOT press R1** — the clock keeps running. Sit for 40 seconds. **Now press R1.** | `cease ... -> already_falling` |
| B2 | **R1**, hard 5 minutes, stop and **press R1** at the top, then **press R1 again after 30 seconds** and pedal. | `resume -> effort_resumed` |
| B3 | **R1**, hard 4 minutes, stop and **press R1** at the top, sit **30 seconds**, then **L1 SAVE** and finish the ride. | `end -> ride_ended`, and `session … recoveries=0` |

B1 is the subtle one and the most likely to disagree with the design. If your
heart rate falls below 80% of HRmax during those 40 seconds you will get
`too_easy` instead of `already_falling` — **both are correct refusals**, and
which one fires tells you where the two gates meet on your physiology. Write
down which you got.

B3 leaves a session in the shared log with **no recoveries at all**. That is a
real state and the file must show it rather than omitting the session.

## Ride C — the sensor, and the file (about 15 minutes)

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

1. Turn the watch's heart-rate zones **off** in settings.
2. Ride hard 4 minutes, stop pedalling and **press R1**, sit 90 seconds, finish
   and save.
3. Turn the zones back on.

Expect `cease ... -> no_max_hr` and a session in the log with
`hr_max_setting: 0`, `zone_count: 0`, and **no `edwards_trimp`**.

## Ride E — a ladder Edwards never wrote weights for (5 minutes)

1. On the phone, set Spin's `hrZoneCount` to **3**.
2. Ride 4 minutes at any intensity, finish and save.
3. Set it back to **5** (or 0).

Expect the session to have `zone_count: 3`, three `zone_floors`, four `zone_s`
buckets, and **no `edwards_trimp` field at all**. Ride A's session, by contrast,
must have one.

---

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
4. **The `curve` falls monotonically.** A curve that rises in the middle means
   the strap was arguing with the wrist sensor, and `hr_source` in the `.fit`
   will say which one won that second.
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

- **No window has ever survived to produce a measurement.** Every gate that has
  fired on hardware so far has been a refusal — `no_max_hr`, then `too_short`.
  The fastest way to close this is
  [the five-minute trick above](#optional-force-a-real-measurement-without-exercise-5-minutes):
  it works now that the watch's maximum reads back, and it exercises the window,
  the curve, the struct across the C ABI, the JSON and the commit without a bike.
  Rides A–E stay premature until it has.
- `Docs/INSTALLING.md`, linked twice above and once from `Service.cpp`, does not
  exist.
