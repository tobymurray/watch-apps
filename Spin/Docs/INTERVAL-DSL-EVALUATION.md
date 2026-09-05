# Can a workout fit in one text box? — the answer, and what building it found

## Where this document comes from

[`INTERVAL-DSL-PROMPT.md`](INTERVAL-DSL-PROMPT.md) asked for an evaluation of a
workout DSL in one `configFields` string, and said to write the answer into
`Spin/Docs/` under a dated heading. **The evaluation was run on 2026-09-05 and
its result was "build it", but the document itself never reached this
repository** — the implementation brief that followed carries its conclusions
and its numbers, and this file is where they now live so that the next reader
inherits them rather than re-deriving them from a ride.

Two consequences a reader should hold on to:

- The corpus of nineteen sessions the evaluation scored is **not** here. What is
  here is the shape of the answer — build the DSL, not the three numeric fields
  — and the measurements §1 lists, which are the ones the design turns on.
- The per-session table §4 of the prompt asked for is reconstructed only as far
  as [`EffortKit/tests/intervals.rs`](../../EffortKit/tests/intervals.rs)
  needed it: twelve named structures, each written out in the grammar. That is a
  test corpus, not a scored evaluation, and it should not be read as one.

---

## 1. What was decided, and the measurement behind each

These are not open. Re-opening any of them means re-running a ride.

### 1.1 `@n` is an instruction. It is never a compliance check.

`@5` means *go this hard*, in the sense a pace cue or an RPE does. It is **not**
a claim about what the heart rate will do during the step, and nothing in the app
compares the two.

From the ride of **2026-09-04**, at a real maximum of 184
([`Tests/pulled/20260904-intervals-real-max-184/`](../Tests/pulled/20260904-intervals-real-max-184)):

- Of six all-out 20-second sprints, **five spent at most four of their twenty
  seconds above zone 3**, and the rider never reached zone 5 in the whole
  session.
- Taking means rather than peaks, **in nine of the eleven efforts the recovery
  averaged a higher heart rate than the work it was recovering from** — set A's
  floats ran +4.3 bpm above their sprints, set B's +7.0 above their minutes. Only
  the two four-minute efforts come out the right way round.
- Scored against its own targets on the mildest criterion worth the name,
  **nine of eleven correctly ridden efforts fail**.

So: no "you are not in zone 5", no ring coloured against the target, no count of
steps met, in the app or in the JSON. A verdict that is wrong in that pattern is
worse than no verdict.

### 1.2 It is rendered as a word, never as a number

A screen reading `5` beside a dial reading zone 3 is a contradiction the rider
has to resolve while out of breath, and the dial is the honest one — it is a
measurement. A word cannot be misread as a reading.

### 1.3 The scale is the zone ladder, and there is no RPE mode

One intensity vocabulary. `@n` indexes the ladder the rider already configured
with `hrZoneCount` and `hrZone1Min`…`hrZone8Min` — not because zones measure the
effort (§1.1 is the evidence that they do not) but because a zone boundary is a
bpm floor the rider already set, where an RPE point is a perception they would
have to forecast. `wkt_step_target` in the FIT profile has no RPE enumerant
either; `workout_rpe` exists only on `session`.

### 1.4 The word appears at the transition, not throughout

With hands on the bars the wrist-tilt gesture almost never fires. The moment the
rider *does* glance is the moment the wrist buzzes, so the banner slot the lap
split already uses is the home, at the same five-second dwell.

### 1.5 Out of scope, by decision rather than by omission

No ramps, no open-ended steps, no nested repeats, no power or cadence targets, no
recovery-driven steps. Nesting in particular is barred by the 256-character
`pattern` budget — two levels costs 477 — and not by the validator's
backtracking rule, which permits it.

### 1.6 Why this and not three numeric fields

`intervalWorkSeconds` / `intervalRestSeconds` / `intervalRepeats` would cover ten
of the nineteen sessions in the evaluation's corpus for none of the parsing, and
it remains the fallback if this proves unusable. It cannot express

```
5m@2,6x(20s@5,40s@2),6m@2,3x(1m@5,1m@2),7m@2,2x(4m@4,3m@2)
```

which is three unlike blocks, and that is the only interval ride anybody here has
actually recorded. The margin is honestly three or four sessions of nineteen.

---

## 2. 2026-09-05 — what formalising the grammar found

### 2.1 The pattern is 175 characters and the SDK validator accepts it

```
(\d{1,2}x\(\d{1,3}[sm](@[1-8])?(,\d{1,3}[sm](@[1-8])?){1,3}\)|\d{1,3}[sm](@[1-8])?)(,(\d{1,2}x\(\d{1,3}[sm](@[1-8])?(,\d{1,3}[sm](@[1-8])?){1,3}\)|\d{1,3}[sm](@[1-8])?)){0,40}
```

175 of the 256 characters allowed, and `validate_app_config.py --check` passes
with the field in place. The ride of 2026-09-04 is **58 of the 128 bytes**.

### 2.2 The pattern is not the grammar, and the difference is the interesting part

The pattern runs on the phone and never reaches the watch: `app-manifest.json`
does not ship, `AppConfig::stringField` carries only an id, a default and two
lengths, and the values file is plaintext on a FAT volume readable over USB and
BLE. **Anything can be in that string.**
[`effortkit::intervals`](../../EffortKit/src/intervals.rs) is the real grammar,
written as EBNF at the top of the module, and it is total over arbitrary bytes.

Where it is deliberately **stricter** than the pattern:

| Input | Decision | Why |
|---|---|---|
| `0s` alone | no session | The off value, matching `autoLapMinutes` and `targetMinutes`. Not a session of one zero-length step. |
| `5m,0s,3m` | rejected | A step nobody can be in is a boundary with no interval on either side of it — two buzzes in the same second. |
| `0x(1m,1m)` | rejected | The same argument, one level up. |
| a 42nd item | rejected | Where the pattern's `{0,40}` stops. 128 bytes admits 43 of `1s`, so this is reachable. |

Where it is deliberately **not** stricter:

| Input | Decision | Why |
|---|---|---|
| `007s` | 7 seconds | `\d{1,3}` matches it, so the phone would send it, and the watch must not disagree with the phone about what is valid. |
| `1x(1m,1m)` | accepted | Degenerate and harmless. Not rewritten into two plain items: the cursor walks it in place, and a block of one repetition names no repetition on the banner, exactly as a step outside a block does. |
| `10M`, ` 10m`, `10m ` | rejected | The pattern is case-sensitive and the companion app **must not trim** (`Docs/app-config-fields.md` §3.2), so each of these is a real byte in the value. |
| `@8` at `hrZoneCount` 5 | stored as 8 | The parser is a pure function of the text and the zone count is a separate setting that can change between rides. See §2.4. |

### 2.3 MEASURED: the worst case is 2871 steps, and it is why nothing is expanded

The obvious implementation expands a session into a flat array of steps. The
bound that array needs is not the pattern's 41 items × 99 repeats × 4 steps
(16,236) — the binding constraint is the field's **128 bytes**.

Searching every item shape against that cap gives an exact answer: **2871
steps**, in 126 bytes and 8 items.

```
99x(1s,1s,1s,1s) × 6 , 99x(1s,1s,1s) , 99x(1s,1s)
```

A flat array bounded by that costs **8,613 bytes** of `.bss` at three bytes a
step, 11,484 at four — against a Service whose whole `.bss` is 4,160 bytes today.

So the session is **iterated, not expanded**. `Cursor` is `(item, repetition,
step within block)`: **3 bytes**, whatever the repeat counts. `advance()` is a
compare and an increment on each of three `u8`s and touches no heap; `current()`
is two array index operations. The parsed `Session` is **740 bytes** — 41 items
of `{repeats, len, [Step; 4]}` — and would be needed by the flat version too.

`the_worst_case_a_128_byte_value_can_name` is the 2871 as a test, and it walks
the whole thing through the cursor to prove the number is reachable.

### 2.4 `@8` on a five-zone ladder clamps at the word, not at the parse

The parser cannot see `hrZoneCount`, and it should not: it is a pure function of
the string. The clamp happens where the word is chosen, and it clamps to the
**top zone** — which at a count of *N* is *N*.

The consequence worth writing down: the **direction** of a transition is taken
from the `@n` as written, not from the clamped word. On a five-zone ladder
`4x(1m@7,1m@6)` still buzzes "harder" into every work step even though both steps
say `HARD`, because the ordering the rider wrote is the thing the alert is for.
The screen can therefore show the same word on both sides of a transition the
wrist called a change. That is a real contradiction, and it is the lesser one:
the alternative is a buzz that says only "something changed", which is the
metronome `autoLapMinutes` already is.

### 2.5 Where the parser lives

A module in [`EffortKit`](../../EffortKit) rather than a crate of its own.
EffortKit is already the home for "session structure for this watch's activity
apps", Spin already links it through a shim that owns the C ABI, and a second
crate would be a second archive with a second `#[panic_handler]` to keep out of
the same ELF. It depends on nothing else in the crate, which is what its module
doc says.
