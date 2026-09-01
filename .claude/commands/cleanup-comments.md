---
argument-hint: [path...]
description: Delete the comments AI added, fixing any code that leaned on them.
---

# Cleanup Comments

Delete the comments AI added, and fix the code that needed them.

**Scope.** Given paths, those files are the scope, whole file each. Given nothing, diff the branch
(`git diff main...HEAD`, plus staged/unstaged) and work the diff.

**Delete every comment in scope that does not survive one of the two tests below.** The same bar
applies to a pre-existing comment sitting in a hunk you touch; do not hunt outside the scope.

## The ownership test

Take one comment at a time, ignoring how true or well-written it reads:

> Could someone change another file, another class, the firmware, or a spec and make this comment
> wrong, with nothing here changing?

If yes, delete it: this code cannot keep that claim true and will never notice it rot. Never owned:
what a caller does; what another class, screen or message handler does or used to do; the name of a
test, scene or constant that lives elsewhere; counts of tests or fields; ticket numbers; planned
work; dates and "as of".

When the reason genuinely lives elsewhere, name the one place that owns it — a file, function,
constant or test — and stop. A pointer, never a retelling.

A surviving comment is one sentence. If deleting one leaves a reader lost, that is a code problem you
fix in this pass — rename, extract a function, name the constant, add a predicate — not a reason to
keep it.

## The carve-out: facts the code cannot state

This is firmware for one piece of hardware, writing a frozen wire format. Some facts are unrecoverable
from the code and were paid for in shipped bugs. **These are kept, and are not held to one sentence.**
Exactly three kinds qualify:

1. **Hardware behaviour proven on the watch.** `enMusicControl` makes the system eat `HOLD_1S`, so a
   long press never reaches the app. The panel holds four levels a channel. The wrist-tilt gesture
   does not fire with hands on the bars.
2. **Frozen wire format.** `session` field 57 is `avg_temperature`, not `time_in_hr_zone`;
   `session.avg_power` is 20 while `lap.avg_power` is 19 and session 19 is `max_cadence`. A wrong
   number here is silent and permanent, and nothing local ever looks wrong.
3. **A measurement, with its numbers.** The arc sampler leaves 95 unpainted pixels at 0.85 and 4 at
   0.75. Consecutive heart-rate samples differ by 0.50 and 0.18 bpm across two real rides. Two-place
   kJ entry costs 9.2 clicks against 10.2 for every scheme with a mode.

**Every carved-out comment must name what would falsify it** — the setting, the field number, the
test, the rerun. That is what stops the carve-out becoming a licence to narrate, and it is the
ownership test satisfied rather than waived: a comment that says how to prove it wrong is a comment
someone can notice rotting.

A "why-not" — the alternative that was tried and failed — qualifies only under one of those three. An
alternative rejected on taste is not a measurement; delete it.

## Before and after

**Before** — the same claim twice, the second a worse copy of the first (`Gui.cpp`):
```cpp
// Nothing on any screen animates, and the Service publishes a
// snapshot every second while a ride is running, so a redraw per
// tick would be the same pixels at the tick rate. Ticks are
// acknowledged and dropped; the messages below are what redraw.
// Nothing animates. The Service publishes a snapshot every second
// while a ride runs, so a redraw per tick would be the same pixels
// at the tick rate.
case SDK::MessageType::EVENT_GUI_TICK:
```
**After** — one sentence, kept because "nothing animates" is a fact about this app that the `case`
cannot state:
```cpp
// Nothing animates, and the Service publishes a snapshot a second, so a
// redraw per tick would be the same pixels at the tick rate.
case SDK::MessageType::EVENT_GUI_TICK:
```

**Before** — a measurement worth keeping, welded to the name of a test that no longer exists
(`lib.rs`, where `the_confirm_ring_has_no_gaps` was deleted by an unrelated edit and nobody noticed):
```rust
/// Measured rather than reasoned: 0.85 leaves 95 unpainted pixels in the ring,
/// 0.75 leaves 4, and 0.70 is clean -- which lands on the geometry exactly.
/// 0.65 is that with a little margin.
///
/// These were 0.4 and 0.5, which painted every pixel of the ring three to nine
/// times over. `the_confirm_ring_has_no_gaps` is what makes tuning them safe:
/// it asserts the ring is solid, not merely present, which is the one thing
/// that goes wrong here and the one thing no other test would notice.
```
**After** — the measurement survives under carve-out 3; the retelling of what a test asserts does not,
because that test owns its own reason and can be deleted without this file changing. The pointer stays
only if the test does — check, and if it is gone, restore it or drop the sentence:
```rust
/// Measured: 0.85 leaves 95 unpainted pixels in the ring, 0.75 leaves 4, 0.70
/// is clean -- the 1/sqrt(2) diagonal, exactly. 0.65 is that with margin.
/// Re-measure by counting unpainted pixels inside a filled arc.
```

## Units and sentinels are the one signature exception

A C++ or Rust signature shows the type but not the unit, and not what a magic value means. Document
that — one line, on the field or parameter:

```cpp
uint16_t workKilojoules;   ///< kJ; 0 = nobody said
std::time_t duration;      ///< active seconds, not elapsed
```

It describes the value only, never the workflow, the callers or anything downstream. `@param` beyond
a unit is not covered: the name is already in the signature.

No jargon ("seam", "surface", "orchestrate"), and never restate the function name.
