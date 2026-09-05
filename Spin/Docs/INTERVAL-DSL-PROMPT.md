# Can a workout fit in one text box?

You are evaluating a proposal for the **Spin** app of the `watch-apps`
repository — a stationary-bike activity app for the UNA Watch. Read
`Spin/README.md` first; it is the design record and this brief assumes it.

**The proposal:** let the wearer type a structured interval session into a
single `configFields` string, which the watch then drives — buzzing at each
transition, auto-lapping at each boundary, and knowing the target zone for each
step.

**Your job is to evaluate it, not to justify it.** "Do not build this" is a
result, and §7 says what would make it the right one. The design below is a
sketch that survived one afternoon; treat it as the thing under test.

---

## 0. The honesty contract

This repository does not ship numbers it cannot source, and the same rule
applies to claims about other people's file formats.

- **Do not describe a format you have not looked at.** Every claim about Zwift,
  Garmin, TrainingPeaks, Intervals.icu or anyone else needs a citation to their
  documentation or a file you actually decoded. "I believe ZWO supports X" is
  not an input to this decision.
- **Re-derive the platform limits in §2 rather than trusting them.** They were
  measured on 2026-09-05 by reading SDK 1.4.0 and running its own validator.
  They are the constraints the whole question turns on, so check them.
- **A format is only as good as the sessions it can express.** §4 is the real
  work; §3 is a sketch to attack with it.

---

## 1. What the watch can and cannot do

Firmware limits, not choices — see [What it does not
do](../README.md#what-it-does-not-do):

- **Heart rate only.** No cadence, no power, no BLE peripheral role. A step
  cannot target watts or rpm, because the watch cannot measure them.
- **Four buttons, `CLICK` only.** No long press (the system eats `HOLD_1S`), no
  auto-repeat, no touch. While riding, R1 pauses and R2 marks a lap — **there
  is no free button mid-ride.**
- **A 240×240 reflective panel**, four levels a channel.
- **The rider usually cannot see it.** With hands on the bars the wrist-tilt
  gesture almost never fires; that is why `keepScreenLit` exists. The buzzer and
  vibro motor are the output channels that work under effort, and both are
  already parameterised (`playBuzzerPattern(ms, count)`), with two
  distinguishable patterns in use.

The last point is the one most likely to change your conclusion, and it cuts
both ways: it is the argument for the watch driving the session, and it is the
argument against anything the rider must read to follow.

## 2. The platform limits, measured 2026-09-05

Verified by reading SDK 1.4.0's `Libs/Header/SDK/AppConfig/AppConfig.hpp` and by
running `Utilities/Scripts/app_packer/validate_app_config.py --check` on a
candidate manifest. **Re-verify before relying on them.**

| | |
|---|---|
| `configFields` cap | **32** flat scalar fields, one form, no arrays and no repeats |
| Fields Spin already uses | **14** |
| String field value | **128 UTF-8 bytes** (`skMaxStringBytes`) |
| `pattern` regex | **256 characters** (`MAX_PATTERN_LEN`) |
| Field types | `bool`, `int`, `float`, `string` |

The `pattern` is validated **on the phone**, before anything reaches the watch.
Its dialect is deliberately restricted so it behaves identically on iOS and
Android, and the validator rejects:

- `^` and `$` — matching is always a full match
- lookahead, lookbehind, backreferences, named and atomic groups
- any escape outside `dDwWsSbBnrt`
- **a quantified group whose body is also unbounded** — `(a+)+` and anything
  shaped like it, because it can backtrack exponentially

and it requires that **`default` match the field's own `pattern`**.

That last pair is what killed the first draft of §3: an unbounded list inside a
repeated group is forbidden, and an empty default is not allowed. Both are real
and neither is negotiable.

## 3. The sketch to attack

One string field, `intervals`, 128 bytes, defaulting to `0s` — which means off,
matching the app's own convention that 0 is off for `autoLapMinutes` and
`targetMinutes` rather than carrying a second toggle.

```
10m,6x(20s@5,40s),5m,3x(1m@4,1m),5m,2x(4m@4,3m)
```

A duration in `s` or `m`; `@` and a digit for a target zone; `Nx(work,rest)` to
repeat. That example is a real session ridden on 2026-09-04 and recorded in
[`RECOVERY-FIELD-RESULTS.md`](RECOVERY-FIELD-RESULTS.md#2026-09-04--the-interval-ride-and-what-it-settles),
and it is **47 of the 128 bytes**.

The pattern that admits it, 204 of 256 characters, which the SDK validator
accepts:

```
(\d{1,2}x\(\d{1,4}[sm](@[1-8])?,\d{1,4}[sm](@[1-8])?(,\d{1,4}[sm](@[1-8])?)?\)|\d{1,4}[sm](@[1-8])?)(,(\d{1,2}x\(\d{1,4}[sm](@[1-8])?,\d{1,4}[sm](@[1-8])?(,\d{1,4}[sm](@[1-8])?)?\)|\d{1,4}[sm](@[1-8])?))*
```

**Its known weaknesses, so you do not spend time rediscovering them:**

- **No nested repeats.** `3x(4x(1m,1m),5m)` — four minutes on/off, three sets,
  five minutes between sets — cannot be written. The backtracking rule is what
  forbids the obvious grammar, so this is a platform constraint rather than an
  oversight, and it may be fatal.
- **A block holds two or three steps**, spelled out for the same reason.
- **No ramps.** A step is a constant target.
- **Zones only**, 1–8. No bpm, no percentage, no RPE.
- **Nothing open-ended.** No "until failure", no "as many as you can".
- **A pyramid must be written out** — `1m,2m,3m,2m,1m` — with no shorthand.

## 4. The work: what can and cannot be written

**This is the section that decides the question.** Take real prescribed
sessions — from published training plans, coaching literature, and the workout
libraries of the platforms in §5 — and try to write each one. Report the
fraction that fit, and be specific about what breaks.

Cover at least these, and say for each whether it is expressible, expressible
but ugly, or impossible:

- **Norwegian 4×4** — 4 × 4 min hard, 3 min easy
- **Tabata** — 8 × 20 s / 10 s
- **30/30s and 40/20s** — long sets of short efforts
- **Sets of sets** — 3 × (4 × 1 min on/off) with 5 min between sets
- **Pyramids and ladders** — 1/2/3/4/3/2/1 min
- **Over-unders** — 2 min just under threshold, 2 min just over, ×3, ×3 sets
- **Sweet spot and sub-threshold blocks** — long constant steps
- **Endurance with surges** — a long step interrupted by short hard ones
- **Ramp tests and warm-up ramps** — a rising target
- **Recovery-driven intervals** — "go again when your heart rate drops below X"
- **Anything cadence- or power-prescribed**, which this watch cannot follow at
  all, and which you should say so about plainly rather than approximating

Then answer the question that matters more than the count: **which of the
impossible ones do people actually ride?** A format that expresses 90% of the
literature and misses the session this wearer does every Tuesday is worse than
one that expresses 60% including it.

## 5. Prior art, and what to check it against

Look at what exists before inventing more. For each, establish **what it is,
whether it is a standard or one vendor's format, whether it is text a human
would type, and what it can express that §3 cannot**:

- **The FIT profile's own `Workout` and `WorkoutStep` messages.** This matters
  most: the SDK's profile already carries them, so a session could in principle
  be *written into the ride's own file*. Find out what a `WorkoutStep`'s
  duration and target fields can hold, whether the DSL maps onto them without
  loss, and whether that mapping should constrain the DSL's design.
- **Zwift's `.zwo`** — XML, and the reference implementation of structured
  indoor sessions.
- **ERG and MRC files** — the older trainer formats, plain text and simple.
- **Intervals.icu's workout text syntax** — a terse human-typed format for
  exactly this problem, and the closest known prior art to §3. Read its
  documentation rather than guessing at it.
- **TrainingPeaks, TrainerRoad, Wahoo SYSTM, Garmin Connect** — how each
  represents a structured workout, and whether any exposes a text form.
- **Golden Cheetah and WKO** — for their workout languages.
- **Anything in strength or running** that solved the same problem tersely;
  `5x5`, `3x10@70%` and the like are a real notation people already know.

**The question to answer from all of it:** is there an existing notation Spin
should adopt rather than invent? A format a rider already knows how to type is
worth a great deal, and a format a tool already exports is worth more.

## 6. The part that is not about grammar

A DSL is only better than the alternative if the alternative is worse. Argue
it against:

- **Three numeric fields** — `intervalWorkSeconds`, `intervalRestSeconds`,
  `intervalRepeats`. Covers Tabata, 30/30s, 4×4 and most of what people ride.
  Cannot do a warm-up, a pyramid, or varying sets. Costs 3 of the 18 free
  fields and needs no parser, no grammar and no error states.
- **What already ships.** `autoLapMinutes` is a symmetric fixed-interval timer
  with a buzz. Establish honestly how much of the gap is left once that is
  counted.
- **Typing it on a phone.** `10m,6x(20s@5,40s),5m` on a phone keyboard, by a
  person who is about to exercise. The pattern means the phone rejects a typo,
  but rejection is not the same as being easy to get right. If the realistic
  outcome is that people give up and set three numbers, say so.

Also decide, and say why:

- **What starts the block.** No button is free mid-ride. Either it begins with
  the ride, which is awkward with a warm-up, or R2's first press starts it and
  later laps become automatic, which overloads a button that just learned a
  meaning.
- **What the screen shows** during a step, given the rider mostly cannot see it,
  and what the buzzer says instead. Distinct patterns already exist for a lap
  and for the target; a third and fourth are cheap, and a rider under effort can
  only tell a few apart.
- **What happens when the rider pauses**, resumes, or presses R2 mid-block.
- **What the `.fit` records**, and whether a prescribed step and an actual lap
  should be the same thing.

## 7. What would make "do not build it" the right answer

Any one of these should end it, and finding one is a success:

- **The impossible sessions are the ones people ride.** If sets-of-sets and
  ramps are the common case, a format that cannot hold them will be abandoned
  after two uses.
- **Three numeric fields cover most of the value** for none of the complexity.
- **An existing notation is better**, and Spin should adopt it or export to it
  rather than invent a fourth thing.
- **Typing it is the real barrier**, and no grammar fixes that.
- **The platform limits in §2 do not survive re-checking.**

## 8. Definition of done

- A table of the sessions in §4, each marked expressible, ugly, or impossible,
  with the failing ones written out as far as the grammar allows.
- A prior-art section where **every claim carries a citation**, and a
  recommendation on adopting versus inventing.
- A recommendation — build it, build the three-field version, or build neither —
  argued from those two, not from §3.
- If it is worth building: a revised grammar, its `pattern`, and the output of
  `validate_app_config.py --check` on a manifest carrying it. A grammar that has
  not been through that validator is a guess.
- Written into `Spin/Docs/` under a dated heading, so the next reader inherits
  the answer rather than re-deriving it.
