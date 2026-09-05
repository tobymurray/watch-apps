# Showing where the heart rate is going, not just where it is

You are continuing an experiment in the **Spin** app of the `watch-apps`
repository — a stationary-bike activity app for the UNA Watch. Read
`Spin/README.md` first; it is the design record and this brief assumes it. Then
read [`RECOVERY-FIELD-RESULTS.md`](RECOVERY-FIELD-RESULTS.md), which is the
evidence everything below rests on, and
[`RECOVERY-FIELD-TEST.md`](RECOVERY-FIELD-TEST.md), which is what is still
unrun.

**The problem, in the wearer's words:** the heart rate on screen is "sticky and
then jumps", it "really lags" going into a hard effort, and for 20-second
sprints it is "basically useless".

Two things are being asked for, and the second is the larger:

1. **Decide whether a trend indicator earns its place**, and if so design it.
2. **Get the one piece of evidence nobody has**, which is per-second heart rate
   through an interval session.

---

## 0. The honesty contract, which outranks everything else here

This repository does not ship numbers it cannot source. `EffortKit`'s
`Provenance` type makes that structural: every tuned number is either
`Measured` — carrying the recordings, the date, and where the derivation is
written up — or `Defined`, carrying the citation and the quantity it fixes.
There is no third variant and no `Default`.

Three consequences for this work:

- **Do not invent a threshold.** If you need one, measure it and record what you
  measured. "8 bpm" is not a threshold; "the 90th percentile of this wearer's
  20-second deltas, measured on 2026-09-03" is.
- **Do not show a number the sensor never reported.** The screen is allowed to
  be *stale* — `HrHold` holds a reading for up to ten seconds — but never
  *invented*. `HrHold` "deliberately does not average anything; a held second
  reports exactly what the sensor last produced."
- **One rider is not a population.** Every measurement below comes from one
  person on one afternoon. Say so wherever you use it.

---

## 1. What has already been measured

All of this is on hardware, wrist optical, and is recorded with its raw data in
`RECOVERY-FIELD-RESULTS.md`. Do not re-derive it; do check it if you doubt it.

### The display pipeline is not the problem

- `HrHold` engaged for **6 seconds out of 1143** in Ride A (0.5%), longest run
  2 s. Only one gap produced a visible jump, at startup acquisition.
- `mGuiSender.trackData()` runs once per second inside `processTrack()`. The
  screen gets a fresh number every second.
- During sustained efforts the raw signal is smooth: 1 bpm steps, the same value
  repeating for a mean of 2.7 s while rising.

**Nothing in Spin is adding lag or stickiness.** Any fix that filters, smooths,
or averages makes it worse.

### The lag is the body, mostly

Ride A's **maximum heart rate of 162 occurred inside the recovery windows** —
after the effort stopped. The fastest sustained rise anywhere in the ride was
17 bpm per 30 s. Cardiac response to a step in workload has a time constant
around 30 seconds.

**For a 20-second sprint this is unfixable at the display.** A zero-lag sensor
would still show the heart rate climbing while the rider freewheels.

### The stickiness is real but belongs to low heart rates

Measured across 927 paused seconds:

| | n | mean move | max move |
|---|---:|---:|---:|
| trust=3 throughout | 361 | 0.57 bpm | **5 bpm** |
| trust=1 throughout | 118 | 0.72 bpm | **15 bpm** |

The means are nearly identical; the difference is a **fat tail** at low
confidence. And the regimes differ sharply:

| | Ride A (87–162 bpm) | synthetic (59–114 bpm) |
|---|---:|---:|
| trust=1 | **9%** of seconds | **24%** |
| largest one-second move | **3 bpm** | **15 bpm** |

A stronger pulse gives the optical sensor more to work with. The excursions
belong to the desk regime, not to the sensor generally.

### The horizon, and its floor

Over Ride A's riding seconds, pause boundaries excluded:

| window | p50 \|Δ\| | p90 | reads flat (\|Δ\|<2) |
|---:|---:|---:|---:|
| 5 s | **1 bpm** | 3 | **58%** |
| 10 s | 2 | 5 | 40% |
| **20 s** | **3** | **8** | **23%** |
| 45 s | 5 | 13 | 19% |

Heart rate quantises at 1 bpm and the fastest sustained ramp was 0.57 bpm/s.
**That sets a hard floor of roughly 15 seconds** on any horizon carrying
information. A 5-second caret is a noise display: its median reading is the
quantisation step itself.

Horizons barely disagree — 20 s and 45 s agreed **94%** of the time, 5 s and
20 s **87%**. Stacked multi-horizon carets are redundant nine times in ten.

---

## 2. The design that came out of it

Not implemented. Argued from the data above, and open to being overturned by
the data in §3.

**A ghost tick on the zone ring.** The dial already draws a white needle at the
current heart rate. Draw a second, thinner, dimmer tick where the wearer was 20
seconds ago. The gap is the trend: which side gives direction, how far gives
rate.

Why this rather than an icon:

- No new vocabulary — the ring already means "heart rate, here".
- Spatial, not symbolic: a gap is read at a glance where a glyph is decoded.
- **No thresholds at all**, which is the real prize.
- Nothing appears when nothing is happening, matching the app's existing
  restraint — with no target set, the progress arc stays empty.
- No colour semantics. Same white as the needle, one level dimmer and thinner:
  position and weight, which is the app's own answer to a reflective panel with
  four levels a channel.

**Draw two marks and no line between them.** The needle's position is a
fraction *within the lit zone*, not along the whole ring — `Spin/README.md`
explains why. A connecting tail would have to cross the inter-segment gaps and
would look broken.

**The unit is the zone width**, `(max − max/2) / zones`, which the ladder
already derives from the wearer's own maximum. Two rides with maxima 84 bpm
apart landed on nearly the same median in zone widths — 0.16 zw and 0.20 zw —
while the genuine behavioural difference survived in the tail. From one ride,
half a zone width per 20 s is a defensible full-scale point (p90 was 0.43 zw).
The constant to argue about becomes "half a zone", not "8 bpm".

It degrades exactly as the dial does: no maximum set, no zones, no needle, no
ghost.

### Two ideas that were considered and rejected

**Per-session adaptive normalisation inverts the behaviour the wearer asked
for.** Scaling to the session's own variability stretches a steady ride's small
moves to full scale and compresses an interval session's large ones — the
opposite of "intervals swing, steady state collapses". It also makes the caret
move when the heart rate did not, because the scale moved underneath it. A
fixed, personalised scale gets the wanted behaviour for free, because an
interval session genuinely has larger deltas.

**Lead compensation** — `current + k × slope` — is the only thing that would
genuinely reduce perceived lag, and it overshoots exactly when the ramp stops,
which is the top of an interval. It can only correct the sensor's share of the
lag, which the data suggests is the smaller half. And it shows a number the
sensor never reported. See §0.

---

## 3. The open question, which is the actual job

**There is no per-second data from an interval session.** Every number above
comes from one ride of 4–5 minute ramps plus desk sessions at a synthetic
maximum. The wearer's complaint is specifically about short, hard efforts, and
that behaviour has never been observed.

Get it, then decide. The `.fit` already records heart rate every second and
carries `hr_source`, `hr_optical` and `hr_external` as developer fields, so a
normal interval workout with the watch on is the whole experiment. No prescribed
test session, no hard effort spent on diagnostics.

What that ride answers:

1. **Is the 20-second delta distribution genuinely wider than Ride A's?** That
   is the entire "intervals look dynamic for free" claim, and it is what makes
   a fixed scale correct rather than adaptive.
2. **Do the horizons diverge on short efforts** where they did not on sustained
   ramps? If they do, a fading trail of marks at 20/40/60 s becomes worth its
   clutter; if they do not, one ghost is the answer.
3. **Is the lag-then-jump visible against the effort timing at all?** Nothing
   measured so far shows it. It may be entirely the physiology, in which case
   the honest answer is that a 20-second effort has no heart-rate feedback and
   the screen should show the clock.

`Tools/hr_analyse.py` and `Tools/hr_source.py` are the existing analysis
scripts; extend them or write beside them. `python-fitparse` decodes the `.fit`
and shares no code with the writer, which is why it is the reader of record
here.

---

## 4. If a learned scale is wanted later

Not first. The zone-width scale needs no learning and no storage, and should be
tried before anything adaptive.

If measurement later shows that half a zone is this wearer rather than everyone,
the mechanism already exists: `EffortKit`'s `baseline.rs` keeps a median and
median absolute deviation over a rolling 20-session window, per wearer, with
`MIN_SESSIONS_FOR_COMPARISON` of 5 below which it reports
`Unavailable::WarmingUp` and shows the raw measurement, and a cap of 10% movement
per session. `Profile::baseline_of(metric)` is the accessor and `Metric` in
`session.rs` is the family a trend metric would join.

Do not adapt the **horizon**. The 15-second floor is physical, longer windows
added almost nothing, and a moving window has the same "meaning changes
underneath you" problem as moving thresholds.

---

## 5. Where things are now

The branch was rebased onto a new base on 2026-09-04. **TrainKit is gone.**

- **`EffortKit/`** — the shared engine, on `main`. TrainKit and EffortKit
  measured the same thing from opposite ends and were merged.
- **`Spin/Software/Libs/rust/`** — a per-app C-ABI shim that is the staticlib,
  so the archive carrying a `#[panic_handler]` is Spin's own and cannot collide
  with Squash's. The detector is a static here, not an opaque run-time-sized
  blob.
- **`Spin/Software/Libs/`** — the two C++ halves that were TrainKit's
  (`EventLog`, `SharedLog`), where they always belonged.
- **The Service passes the kernel's 0–3 confidence**, not a boolean, so the
  calibration decides how far the sensor is believed. That change was made
  *because of* the 15 bpm trust=1 excursion recorded in the results document.
- **The shared log is schema 2.**
- `Tools/hr_analyse.py`, `Tools/hr_source.py`, `Tools/fit-profile/`.
- `Docs/INSTALLING.md` now exists — it did not when the field test was written.

---

## 6. What is still unrun in the field test

Two desk sessions, both in `RECOVERY-FIELD-TEST.md`, neither needing a bike, a
strap, or hard effort. They are independent of this experiment and can be done
in any order.

- **Session 3 — no zones**, about 3 minutes. `zone_count: 0`,
  `hr_max_setting: 0`, no `edwards_trimp`.
- **Session 4 — `hrZoneCount` 3**, about 6 minutes. The only session that
  exercises the app-config path at all.

`source_changed` is the one discard reason that has never fired, and it needs a
strap that holds a connection. As of 2026-09-03 the strap disconnects after
about two minutes, with a firmware fix expected the following week. **Do not
wear the strap until it lands** — one that drops mid-window puts a sensor change
inside it, manufacturing the exact failure the gate exists to catch.

---

## 7. Scar tissue — do not re-ship these

- **A `cease` line can only say `no_max_hr`, `too_short` or
  `no_baseline_history`.** Everything else arrives on the next line as a
  `discard`. `armed` followed by a `discard` is the gate working. Two ride
  tables predicted lines that cannot occur.
- **The clock stops when R1 pauses and the effort gate counts unpaused seconds
  only.** A wait taken while paused counts for nothing; 33 such seconds cost a
  test its window.
- **A changed maximum needs Spin restarted.** `loadSystemSettings()` runs once
  in `Service::run()`, where `loadConfig()` and `applyZoneConfig()` re-run in
  `startTrack()`.
- **A custom ladder on the watch is only honoured at `hrZoneCount: 0`.** Any
  declared count sends `applyZoneConfig()` down the spread-from-maximum path and
  the watch's own floors are never read.
- **A rising curve is ordinary.** Heart rate overshoots after cessation and
  wrist optical lags on top of that. It is not sensor arbitration unless
  `source` is `external`.
- **Sessions recorded against a synthetic maximum are labelled
  `hr_max_setting`** and their heart-rate numbers are artefacts. A *bigger*
  `drop_bpm` there means a *smaller* effort.

---

## 8. Definition of done

- One interval ride pulled, decoded, and its 20-second delta distribution
  compared against Ride A's, written up in `RECOVERY-FIELD-RESULTS.md` under a
  dated heading with the raw numbers.
- A decision on the ghost tick — one mark, a trail, or nothing — argued from
  that comparison rather than from this document.
- If built: no new threshold without a `Provenance`, no invented number on
  screen, and the zone-width unit rather than bpm.
- If not built: say why, in the results document, so the next reader does not
  re-derive it.
