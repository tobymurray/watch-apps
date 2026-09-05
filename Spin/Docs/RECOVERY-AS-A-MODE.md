# What a recovery mode would have to promise

A design note, written 2026-09-04, after two ordinary rides at a real maximum
produced [no recovery measurement at
all](RECOVERY-FIELD-RESULTS.md#what-the-two-rides-gave-the-recovery-feature-a-negative-result).
Nothing here is built. It exists so the next person to consider the feature
starts from what it costs rather than from what it was hoped to cost.

## The premise that failed

[The README](../README.md#how-fast-your-heart-falls-when-you-stop) argues the
measurement is free:

> A pause costs nothing: effort has stopped, the sensor is connected,
> `processTrack()` keeps running, and because the ride stays paused through the
> kilojoule screen until `TRACK_STOP`, **the end of a ride is already a pause**.

The end of a ride *is* a pause. It is also always too easy, because a rider
finishes by cooling down. Two rides of 45 minutes each, at a maximum of 184,
ceased at **67%** and **57%** of it — and neither rider paused mid-ride even
once. Ride A produced two measurements because its protocol told the rider to
stop dead at the peak and press R1 in the same second. That is not what ordinary
training looks like.

So the feature is not a passive by-product of pausing. **It is a protocol**, and
a mode is the honest shape for it — not because a mode adds anything, but
because it forces the contract below to be stated instead of assumed.

## What one measurement actually asks of the rider

Every number is from the shipped calibration in
[`EffortKit/src/window.rs`](../../EffortKit/src/window.rs), not from this note.

| Before the button | |
|---|---|
| a maximum heart rate set on the watch | else `no_max_hr` |
| **180 s** of unpaused clock in this bout | else `too_short` |
| 20 of the previous 30 s carrying a trusted reading | else `no_baseline_history` |

| At the button, checked one second later | |
|---|---|
| heart rate at or above **80% of maximum** | else `too_easy` |
| not already **10 bpm** below the previous 30 s peak | else `already_falling` |

| For the 60 seconds after | |
|---|---|
| no resume | else `effort_resumed` |
| ride not ended | else `ride_ended` |
| **90%** of seconds trusted | else `dropout` |
| one sensor throughout | else `source_changed` |

In plain terms: **reach 80% of maximum, stop dead rather than winding down,
press R1 in the same second, and then sit still for a full minute.** Wind down
first and it is `too_easy`; press a few seconds late and it is
`already_falling`; resume at 55 seconds and it is `effort_resumed`.

None of those thresholds is arbitrary and none should be loosened to catch more
windows. 80% is the intensity Barak et al. measured their time constants at and
180 s is Buchheit's plateau duration; the measurement is comparable to the
literature *because* it matches their conditions. A looser gate does not produce
more of this measurement, it produces a different one.

## The three questions a mode has to answer on screen

### How many per session

**Two.** `MAX_RECOVERIES` is 2 and a third is dropped, oldest first.

One is not enough, and there is a measurement to say so. Ride A took two windows
under accidentally identical conditions — both opening at hr0 = 161 and 88% of
maximum — and returned drops of **17 and 23 bpm**. Six apart, about 30% of the
value, which sits on top of Buchheit's ~25% typical error and a signal-to-noise
ratio of 1.3. A single number is noise. Two is the least that is worth keeping,
and it is still not enough to read a session on its own.

### How often is too often

The engine enforces one spacing rule: `min_effort_s` counts the **current
bout**, so a second window needs another 180 s of riding after the first
resumes. With the window itself that is about **four minutes** between
measurements.

**Beyond that, nothing here knows.** No measurement in this repository says what
a 60-second stop at 147 bpm costs a training session. It is plainly not free
during interval work — it interrupts the stimulus mid-set — but the size of that
cost is a coaching question nobody has answered, and a mode should not imply
otherwise.

### What the rider gets

Per session, honestly: **nothing they can read**. The engine says so itself, in
[`EffortKit/src/baseline.rs`](../../EffortKit/src/baseline.rs):

| | |
|---|---|
| `MIN_SESSIONS_FOR_COMPARISON` | **5** — below this it reports `WarmingUp` and shows the raw number |
| `WINDOW` | **20** — a rolling median over that many sessions |
| `MAX_BASELINE_STEP_FRAC` | **0.10** — so "a genuine halving takes at least seven sessions" |

So the contract is **five sessions before any comparison exists, and seven or
more before a real change has fully arrived**. At two windows a session that is
roughly ten minutes of interrupted training spent before the first number means
anything at all.

That is a defensible bargain for someone tracking fitness across a training
block. It is an indefensible one to enter without being told, which is what the
feature currently does.

## The confounder a mode does not fix

Barak et al. measured a recovery time constant of **52.5 s sitting still against
74.1 s pedalling gently** — 41% slower for moving. Spin knows the ride was
paused. **It does not know whether the rider kept turning the pedals.**

This is worse than noise. Noise averages out over a 20-session window; a rider
who sits still on some windows and soft-pedals on others produces a series that
drifts with their discipline and reads exactly like a fitness trend. A mode that
says "sit still" makes the instruction explicit and still cannot verify it.

**The watch can see wrist motion.** `SensorLab` and `Squash` both read the IMU,
and a rider sitting with their hands off the bars is plausibly separable from
one soft-pedalling with hands on them. Nothing here has measured that — it is a
hypothesis, and the honest test is a session of each with the IMU logged. But it
is the piece that would turn [the README's stated fundamental
limit](../../EffortKit/README.md#what-this-cannot-tell-you) into a gate, and
without something like it the series carries a bias no amount of gating removes.

## What would have to be decided

Not by this note, and not from the data behind it:

- **Whether a measurement needing a deliberate mid-session interruption belongs
  in an app whose stated value is asking nothing of the wearer.** That is a
  product judgement, and the evidence above is only the price tag.
- **Whether the IMU can separate still from soft-pedalling**, which decides
  whether the series is trustworthy or merely well-gated.
- **What the screen says during the 60 seconds.** Nothing is drawn today,
  deliberately. A mode inverts that: a rider who has been asked to wait needs to
  see the wait, and the number at the end still must not read as a verdict.
