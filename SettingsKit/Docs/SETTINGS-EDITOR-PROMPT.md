# One app that edits the watch's settings. Work out what it should be.

`watch-apps` currently ships two apps that each edit exactly one field of
`2:/settings.json` from the wrist: [`NotifyToggle`](../../NotifyToggle) owns
`phone.notifications`, [`UnitToggle`](../../UnitToggle) owns `units`. The shared
half of them is [`SettingsKit`](..) — the firmware gate, the address table, the
live-struct read/write, and the commit that replaces the settings file without
disturbing a byte the app does not own.

The file holds twelve fields. Two of them are editable today.

**Your job is to work out what the general app should be** — one app that edits
everything in that file worth editing, with a control that suits each field's
type — then to say what it costs, what it gives up, and which parts of it cannot
honestly be built yet. Then build the parts that can be settled without a watch,
because two of them can.

Do not assume the answer is "widen `bool` to a union and carry on". It might be.
Cost it against the alternatives.

---

## 0. The honesty contract, which outranks everything below

This repository's standing rule, from [`CLAUDE.md`](../../CLAUDE.md):

> **Prefer measurement over assertion.** If you are choosing a number, measure it
> and record what you measured.

Apply it to your own conclusions. "A stepper feels better here" is worth nothing.
"A stepper, because ±1 kg from 90 to 75 is 15 presses and a commit per press is 15
flash writes" is worth something. Where you cannot measure, say you are asserting,
and say what would settle it.

Two more, both load-bearing here:

- **Every raw write in this kit goes into kernel RAM on a part with no MPU.** The
  `raw != 0 && raw != 1` refusal in `LiveSettings.cpp` is not defensive
  programming; it is the only thing between a wrong offset and a corrupted
  kernel struct. Widening the value type deletes that check. Whatever replaces it
  is the single most important decision in this task, and it is not a UI decision.
- **Do not trust this document's framing.** It was written by the person who wants
  the app, from a reading pass over the kit — not by anyone who has built it. It
  may well have the field tiers, the interfaces or the heart-rate design wrong.
  Read the source before accepting any table below.

The comment rule in `CLAUDE.md` and `/cleanup-comments` governs everything you
write. It is not a cleanup step; it is how the comments get written in the first
place.

---

## 1. Read first, in this order

1. [`SettingsKit/README.md`](../README.md) — what the kit is, what an app supplies,
   why it is shared rather than copied.
2. [`NotifyToggle/README.md`](../../NotifyToggle/README.md) — the long-form design
   record for the whole mechanism: how each address was derived, why the gate has
   the steps it has and in that order, what was proved on a watch.
3. [`UnitToggle/README.md`](../../UnitToggle/README.md) and
   `UnitToggle/Software/Apps/CustomGUI/Gui.cpp` — the second consumer, and the one
   that added the witness. `Gui::toggle()` is the shape a general write path has to
   generalise: read raw, write raw, ask the kernel, revert if it did not follow.
4. `SettingsKit/Header/SettingsField.hpp`, `SettingsSplice.hpp`,
   `SettingsPersist.hpp`, `LiveSettings.hpp` — the four headers the value type runs
   through.
5. `SettingsKit/Sources/LiveSettings.cpp` — specifically `readChecked`. Read it
   before you propose anything.
6. `SettingsKit/Tests/SettingsSplice_test.cpp` — 723 lines against real files
   pulled off a watch. Whatever you propose must keep passing it, or must
   explicitly retire a case and say why.
7. [`NotifyToggle/Docs/Investigations/2026-08-31-live-settings-persistence/FINDINGS.md`](../../NotifyToggle/Docs/Investigations/2026-08-31-live-settings-persistence/FINDINGS.md)
   — the struct-offset picture, with its confidence levels stated per field. The
   `HANDOFF-PROMPT.md` beside it is the earlier, less careful pass; FINDINGS
   corrects it in several places, so read FINDINGS as authoritative where they
   disagree.
8. [`SettingsKit/Docs/2026-09-06-units-offset.md`](2026-09-06-units-offset.md) — how
   one offset was derived from the parser rather than the constructor. The method
   is reusable and you will probably need it.
9. [`Spin/README.md`](../../Spin/README.md), the heart-rate zone section around
   "By default the zones are the watch's own". **This changes the heart-rate part
   of the task completely.** See §5.

Two prompts in this repository are the house style for what you are producing:
[`Docs/RECOVERY-CONVERGENCE-PROMPT.md`](../../Docs/RECOVERY-CONVERGENCE-PROMPT.md)
and [`Docs/TEXT-PROMPT.md`](../../Docs/TEXT-PROMPT.md). Read one for register, not
for content.

---

## 2. What already generalises — do not rebuild it

Establish this for yourself, then leave it alone:

- `FirmwareGate`, `SettingsAddresses::resolve`, the signature check.
- The commit: tmp write, prev rename, rename into place, readback, rollback.
  Field-agnostic today.
- `recoverInterruptedCommit` — deliberately takes no `Field` at all, because the
  app that stranded a file is not necessarily the app that next runs.
- `SettingsSplice::detail::replaceToken` — already shifts the tail correctly in
  both directions for an arbitrary length delta, with
  `MovesTheTailCorrectlyInBothDirections` covering it.
- `findValueAtDepth` / `findObjectAtDepth` — already depth-scoped, string-aware and
  escape-aware. `dailyGoals.steps` is the same two-level lookup
  `phone.notifications` already exercises.
- `spliceWithinReadCap` — the cap, and the reason it exists, are unchanged by
  anything here.

`kMaxSettingsFileSize` is 512 against a real file of 245–247 bytes, so length
growth is not the binding constraint on any single field.

---

## 3. The blocker, stated plainly

Every signature in the kit is `bool`, in four places:

```
SettingsSplice::Result (*splice)(char*, size_t&, size_t, bool value, size_t*)
LiveSettings::Flag { size_t offset; const char* name; }
LiveSettings::readFlag(..., const Flag&, bool& out) / writeFlag(..., bool)
SettingsPersist::persistFlag(..., const Field&, bool newEnabled)
```

And `SettingsPersist::Field` conflates two things a general app separates: the
**field descriptor** (which key, which offset, which type, which range) and the
**app's scratch identity** (`probeAPath`, `probeBPath`, `probeText`, which exist
only for `validatePrimitives` and are per-app, not per-field). One app with twelve
fields needs one of the second and twelve of the first.

Note also that a field needs **two type tags, not one**. `weight` proves it: the
file holds `"weight":90`, an integer, and the live struct holds a **float** at
`+0x28`. JSON representation and live representation are independent.

---

## 4. The field census

Twelve fields. Provenance and confidence differ per row and that is the point of
the table — treat the confidence column as part of the data.

| Field | In the file | Live struct | Confidence | Reported by `RequestSystemSettings` |
|---|---|---|---|---|
| `units` | `"metric"` / `"imperial"` | `+4`, u8, 0 = metric | derived from the parser, **confirmed on a watch** 2026-09-07 | `imperialUnits` |
| `dailyGoals.activityMinutes` | int | `+0x18`, u32 | confirmed, 3 sources | `activityMin` |
| `dailyGoals.steps` | int | `+0x1c`, u32 | confirmed, 3 sources | `steps` |
| `dailyGoals.floors` | int | `+0x20`, u32 | confirmed, 3 sources | `floors` |
| `height` | int (cm) | `+0x24`, u32 | **UNVERIFIED** — "likely by elimination" | `heightCm` |
| `weight` | int (kg) | `+0x28`, **f32** | confirmed via a distinct float-taking writer in `save()` | `weightKg` (float) |
| `heartRateZones` | 6-element int array | `+0x10..+0x15`, 6 × u8 | FINDINGS flags **CONTRADICTORY**; see §5.3 | `heartRateTh[]` + `heartRateCount` |
| `phone.notifications` | nested bool | `+5`, u8 | confirmed, 3 sources | **no** |
| `watchFaceId` | int | `+8`, u32 | confirmed, 3 sources | **no** |
| `gender` | `"M"` | **unknown** | none | **no** |
| `dateOfBirth` | `"1990-01-01"` | **unknown** | none | **no** |
| `version` | int, `2` | `+0` | confirmed | **no** — and out of scope, see §9 |

The real file, byte for byte, pulled 2026-09-04
(`Spin/Tests/pulled/20260903-rideA-real-max-184/watch_settings.json`, and the same
shape in `SettingsKit/Tests/SettingsSplice_test.cpp`):

```json
{"units":"metric","watchFaceId":0,"phone":{"notifications":false},"heartRateZones":[30,60,70,80,90,100],"dailyGoals":{"activityMinutes":30,"steps":5000,"floors":5},"height":190,"weight":90,"gender":"M","dateOfBirth":"1990-01-01","version":2}
```

### 4.1 The right-hand column is the whole argument for doing this

`SDK::Message::RequestSystemSettings` (`Libs/Header/SDK/Messages/CommandMessages.hpp`
in the SDK) reports `languageId`, `imperialUnits`, `timeFormat`, `heartRateCount`,
`heartRateTh[8]`, `activityMin`, `steps`, `floors`, `heightCm`, `weightKg`.

Consequences worth being explicit about:

- **Seven of the twelve fields can be witnessed.** UnitToggle's pattern — write the
  raw value, ask the kernel, revert if the kernel does not report what was just
  written — generalises to every field in that column. One message answers seven
  of them in a single round trip.
- **A general editor is therefore *safer per field* than NotifyToggle already is**,
  not riskier. NotifyToggle writes a byte it cannot witness at all. Say so in your
  proposal if you agree, and refute it if you do not.
- **Displaying those seven needs no raw read at all.** The supported message is the
  display path; the raw pointer is needed only for the write. That is already
  UnitToggle's shape.
- **The unverified offsets just got cheap.** `height` at `+0x24` is "likely by
  elimination" and can now be settled in minutes: read the candidate offset,
  compare against `heightCm`. That tool did not exist when FINDINGS was written.
  The same applies to `heartRateZones` against `heartRateTh[]`.
- `timeFormat` and `languageId` are reported but are **not in `settings.json`** —
  they belong to `local_settings.json` (`alertMute`, `gpsPwrMode`, `sound.*`,
  `vibro.*`, `clock.*`), which FINDINGS established is a **sibling class with its
  own vtable**, needing a second offset table. Out of scope; see §9.

---

## 5. Settled by measurement — do not re-derive these

Each cost a session. They are inputs.

### 5.1 The heart-rate ladder is derived from one number

From `Spin/README.md` and `Spin/Docs/RECOVERY-FIELD-RESULTS.md`, measured against
a watch:

- The watch reports its ladder as **50/60/70/80/90/100% of maximum heart rate**, so
  the last of the six values **is the maximum**, not a floor.
- The observed real ladder was `[92,110,129,147,166,184]`, which is exactly those
  six percentages of 184.
- `heartRateCount` counts **zones, not thresholds** — the SDK header says so itself
  (`4 thresholds = 5 zones`). It came back as **7 for six values**, so
  `heartRateTh[count - 1]` reads a slot the firmware never filled. It reads 0,
  which is a maximum heart rate of zero. This cost Spin an entire field session:
  every recovery window was discarded `no_max_hr` on a watch that had a maximum set
  the whole time. `Spin`'s `ZoneLadder::fromWatch` owns the split now and is tested
  against the ladder the watch actually sent — **read it before writing any
  heart-rate code.**
- With every floor left at 0, spreading them evenly from half the maximum up to it
  reproduces the watch's own five floors **exactly**, asserted in
  `Spin/Tests/ZoneLadder_test.cpp`.

**`[30,60,70,80,90,100]` in the pulled file does not fit that rule** — 30 is not
50% of 100. Its README says it is "the synthetic maximum the later desk sessions
were run at, left in place afterwards", so it is probably an artefact of a hand
edit rather than something the phone app wrote. Establishing which matters to §6.3
and is an open question, not a settled one.

### 5.2 The cross-check spans `+8..+0xF`

`LiveSettings::readChecked` reads **eight** bytes at `watchFaceIdOffset` (`+8`) as a
little-endian `uint64_t` and refuses anything over 1000. So it is asserting that
`+0xC..+0xF` are effectively zero as well as that `watchFaceId` is small. Two things
follow: it does not overlap `heartRateZones` at `+0x10`, and **an app that can write
`watchFaceId` can invalidate the check guarding every other raw read it makes.**

### 5.3 The `heartRateZones` contradiction may already be resolved

FINDINGS flags `+0x10` **CONTRADICTORY** — the constructor appears to write a
4-byte pointer-shaped literal there, which does not fit a 6-byte array. But
`2026-09-06-units-offset.md`, written later, independently reads the *parser* at
`0x080abd1a` and finds a six-iteration loop storing one byte at a time from
`structBase+0x10`. That is one more source than FINDINGS had, from the function that
actually parses the file. Resolve it or refute it explicitly; do not inherit either
verdict silently.

### 5.4 The firmware heals a stranded settings file

Measured 2026-09-07: with `2:/settings.json` renamed away by hand and the watch
power-cycled, kernel 1.4.0 rewrote it — and its own `.bak` — at boot, with the
wearer's real fields intact, before any app ran. `SettingsKit/README.md` says what
survives of the recovery argument and what does not. An interrupted commit is not
the data-loss event the kit was designed around, but the litter and the stale-`.bak`
window are real.

### 5.5 Exceptions, `std::string` and `new` cost about 10 KB

Measured: nothrow `new` and `std::string` each drag libstdc++'s exception runtime
into an `-fno-exceptions` app. NotifyToggle's `.uapp` went **56,108 → 46,072** bytes
when they came out. So: no `std::string`, no allocation, no `snprintf` on the write
path. Formatting a number means a manual decimal conversion into a `char[12]`.

### 5.6 The panel and the glass

- Sharp LS012B7DD06, **240×240, round**, `ABGR2222`: one byte a pixel and exactly
  four levels a channel — 0, 85, 170, 255. Only the inscribed disc is glass.
- `TextKit`, measured on this glass: **22–26 px em is confirmed readable**; 11–12 px
  is detection only. Bright text on a dark ground is the only kind proven to render.
- Buttons: `SW1` = UP (L1), `SW2` = SELECT (R1), `SW3` = DOWN (L2), `SW4` = BACK (R2).
  Four, and BACK is spoken for.
- The GUI ticks at about **10 fps**.

---

## 6. The questions that are actually open

### 6.1 What is the value type?

A tagged union? A `void*` plus a type tag? Separate parallel tables per type, so
each stays monomorphic and the compiler keeps checking? Something else? The JSON
types in play are boolean, enum-of-string-tokens, unsigned integer, integer array
and date-string; the live types are u8-boolean, u8-enum, u32, f32 and u8[6].

Constraints that bear on it: this must stay free of SDK types and header-only
enough that `Tests/` can drive it on the host without a kernel — that is what
makes the splice testable, and it is where all the tests are. No allocation.

### 6.2 Which fields are editable at all?

Propose a tier structure and defend it. A starting position to argue with:

- **Witnessed and editable** — `units`, the three `dailyGoals`, `height`, `weight`,
  `heartRateZones`. An independent supported read confirms every write and a failed
  witness reverts it.
- **Unwitnessed** — `phone.notifications` (NotifyToggle already accepts this),
  `watchFaceId` (and §5.2 argues it should stay read-only whatever else is decided).
- **No offset, no witness** — `gender`, `dateOfBirth`. See §6.8.

An unwitnessed field's state cannot be *claimed*, which UnitToggle's `known` flag
already models and its `unit_toggle_status` enum already draws five different
screens for. Does that enum generalise, or does a per-field editor need a different
vocabulary?

### 6.3 Heart-rate zones — the hard one

Six values that must be valid together, in a file that is rewritten whole. Three
candidate interfaces; cost all three.

**(a) Six independent steppers with auto-advance.** Editing zone *i* to *v* forces
every zone *j > i* up to at least *v + (j − i)*, applied as the wearer scrolls
rather than refused at confirm time. Questions it raises, all of which need
answering before it can be built:

- Is it symmetric? Does lowering Z6 pull Z1..Z5 down, or refuse?
- **Z6 is the maximum heart rate** (§5.1). So raising Z1 far enough silently raises
  the wearer's maximum HR — a number every activity app in this repository reads,
  and the one whose misreading cost Spin a field session. Is a side effect that
  changes it acceptable at all?
- Minimum spacing of 1 bpm is arbitrary. The measured real ladder has gaps of
  18/19/18/19/18. What spacing is meaningful, and what is the evidence for it?
- The values are u8. Six ascending values with the top pinned at 255 caps Z1 at 250.
  Where does the clamp show up on screen?

**(b) One control: maximum heart rate.** Derive all six by the watch's own
50/60/70/80/90/100% rule, which `Spin/Tests/ZoneLadder_test.cpp` proves reproduces
the watch's floors exactly. One number, one stepper, no ordering constraint to
enforce, no accidental change to the maximum, and it writes what the phone app
would have written. It cannot express a ladder the watch itself cannot express —
which may be the entire point, or may be the thing that makes it useless.

**(c) (b) as the primary control, with a per-zone override behind it.** Costs both
implementations and both explanations.

The choice turns on a fact not yet established: **does the phone app write
`50/60/70/80/90/100%` of one number, or six independently settable values?**
`[30,60,70,80,90,100]` (§5.1) is the only evidence against, and it is probably a
hand-edit artefact. Say how you would settle it — flipping the maximum in the phone
app and pulling the file twice would do it — and design for what the evidence
supports rather than for the more flexible option by default.

Whichever wins: the splice replaces a whole JSON array whose length changes
(`[30,60,70,80,90,100]` → `[92,110,129,147,166,184]` grows 5 bytes), the raw write
is six bytes rather than one, and the witness compares against `heartRateTh[0..5]`
with §5.1's off-by-one firmly in view.

### 6.4 Numbers in the file

- **Token extent.** `setUnits` finds the old token by matching a known literal.
  A number has to be scanned to its terminator and *validated* — anything that is
  not `-?[0-9]+(\.[0-9]+)?` must be `FieldNotFound`, exactly as `setUnits` refuses
  `"furlongs"`. This matters more than it looks: the commit's readback compares the
  file against the buffer just written, so a wrong edit is **confirmed rather than
  caught**. Two entry points already shipped with a bug of that shape.
- **`weight` is an integer in the file and a float in the struct.** Does the app
  write `90`, or `90.5`, or `90.000000`? What does the firmware's parser accept —
  and what does it store if it accepts something it cannot represent? Does the
  wearer get fractional kilograms at all?
- **Formatting** without `snprintf` or `std::string` (§5.5).
- **What does the parser do with an out-of-range number?** Unknown. The units doc
  established that a token matching neither spelling stores *nothing* and parsing
  carries on; whether a number behaves the same way is unmeasured, and it decides
  whether a bad write is inert or destructive.

### 6.5 When does a commit happen?

A stepper from 90 to 75 kg is 15 presses. A commit per press is 15 whole-file
rewrites, 15 rename pairs, and 15 windows in which power loss strands the file. So
the pending value has to live somewhere and be committed once, on confirm.

Where does it live — the Rust state, the C++ shell, or both? What does the screen
show while a change is pending but not committed, given that UnitToggle already has
five distinct things to say about a value's status? What happens on `APP_STOP` or a
suspend with a change pending — commit, discard, or refuse to exit? And what
happens when the phone app changes the same field while an editor is open on it
(UnitToggle re-reads about once a second precisely because that happens)?

### 6.6 Navigation, on a round disc with four buttons

Four buttons, one of them BACK. A twelve-item list on a 240 px disc loses width on
exactly the rows a name-and-value pair needs, and the legibility floor is 22–26 px
(§5.6). Candidates:

- **One field per screen**, UP/DOWN paging between fields, SELECT entering the
  editor, BACK exiting. No list widget, no scrolling, and UnitToggle's layout
  carries over nearly unchanged.
- **A scrolling list** plus a per-field editor screen. Two navigation modes and a
  widget nobody in this repository has built yet — check whether that is true before
  believing it.
- **Grouping** (Body / Goals / Heart rate / Phone) — a third level of navigation on
  four buttons.

Measure this rather than asserting it: the Rust crate has a host simulator
(`src/bin/sim.rs`) and a frame dumper (`examples/dump.rs`, `Tools/frames_to_png.py`)
that render the real thing at the real size with the real four grey levels. Produce
images.

### 6.7 What replaces `raw != 0 && raw != 1`?

The most important question in this task. Per-field plausibility bounds, and they
must be as tight as the 0/1 check was, or the widening is a straight loss of safety.
Say where each bound comes from. `height` 50–250 cm and `weight` 20–300 kg are
guesses this document is making up; find better ones, and note that the witness
gives you a second, independent line of defence that NotifyToggle never had.

Two more pieces of that answer: does the `watchFaceId` cross-check stay as it is
(§5.2), and does a *write* need a bound the *read* does not — refusing to write a
value the wearer cannot have meant, separately from refusing to trust a value the
struct should not be holding?

### 6.8 `gender` and `dateOfBirth`

No offset, and no supported message reports them, so a raw write cannot be witnessed
at all. A file-only edit is possible — both are string splices, and `gender` is a
one-character token — but then the live struct disagrees with the file until the next
reboot, and if anything makes the kernel write its live copy back out, the edit is
silently reverted. FINDINGS found **no caller of `save()` anywhere in the 4 MB
image**, which means nobody knows when or whether that happens.

Read-only, file-only with the risk stated, or out of scope? Decide, and say what
would change the answer. Note that `dateOfBirth` mostly exists to drive age-based
heart-rate defaults, which §6.3(b) may make directly editable anyway.

### 6.9 Does this replace `NotifyToggle` and `UnitToggle`?

Both are released, versioned, and carry assigned app ids. A general editor makes
both redundant in function. Options: retire them, keep them as single-purpose
shortcuts, or make them thin wrappers over the general app's field table. Each has
a different cost in `SettingsKit`'s API, in the app store, and for a wearer who has
one of them installed. There is also a question of whether `SettingsKit` remains a
shared directory at all if one app is its only consumer.

### 6.10 Configuration

UnitToggle gates the entire write path behind one `saveToSettings` bool, off by
default, "so an install that nobody configures never writes anything"
(`AppConfigFields.hpp`). Does a twelve-field app keep one gate, one per field, or
one per tier? `app-manifest.json` declares config fields too, and nothing checks
that the manifest and `AppConfigFields.hpp` agree.

---

## 7. Traps

- **The readback confirms the buffer, not the field.** An edit to the wrong key is
  reported as success and reverts at the next reboot. Two shipped entry points had
  exactly that fault. Every new key path needs a depth, and a test that a same-named
  key deeper in the file is left alone.
- **Designated initialisers, not positional.** `SettingsAddresses.cpp` says why:
  inserting a member silently shifts every value past it into the wrong member. A
  twelve-row field table makes this worse, not better.
- **`isWellFormed` is a `constexpr` gate, not a comment.** Whatever the field
  descriptor becomes, it needs the equivalent — `static_assert`ed at every
  declaration site — because several members will still be interchangeable
  `const char *`s and offsets.
- **One `AddressSet` row per ABI, and an ABI spans firmware versions.** A row the
  ABI selects is a candidate, never a verdict; `validatePrimitives` is what promotes
  it. Adding twelve fields does not change that, but it does mean twelve offsets
  ride on one gate.
- **`validatePrimitives` writes scratch files**, which is why UnitToggle defers it
  to the first press rather than running it at launch: opening an app should not cost
  the wearer's flash a write they never asked for. A twelve-field app has more
  reasons to run it early and the same reason not to.
- **The ABI fingerprint.** `unit_toggle_gui.h` computes FNV-1a over the C struct
  layout in both C++ and Rust and refuses to start on a mismatch. A general app's
  state struct is much larger and will change often during development; keep the
  check and expect it to fire.
- **Do not bump `appVersion` by hand.** CI does it from the commit type.
- **Conventional commits, one logical change per commit.** `git add <App>` has
  swept an unrelated change into a commit whose message described only the intended
  fix.

---

## 8. What to produce

1. **A design record**, in this repository's register — the reasoning owned in
   prose, in a file, not scattered through source comments. `Spin/README.md` is the
   bar. It must answer every question in §6 with a decision, a cost, and what would
   falsify it, and must state plainly which fields it is proposing to make editable
   and which it is refusing.
2. **A recommendation with a build order** — what to build first, what can be
   proved without a watch, and what is blocked on hardware or on another
   reverse-engineering pass.
3. **The parts that can be settled now, settled.** Two can:
   - **The splice and its tests.** Everything about `SettingsSplice` runs on the
     host. If the proposal is a generalised value splice, write it and test it
     against the real files already in `SettingsSplice_test.cpp` — including the
     metric/imperial pair pulled either side of a real flip. All existing cases must
     still pass.
   - **The screens.** The Rust simulator and frame dumper render the real 240×240
     disc with the real four grey levels. A screenshot of a stepper at 22 px em is
     evidence; a description of one is not.
4. **A list of the measurements you could not take**, each with the one experiment
   that would take it. Be specific enough that someone with the watch in front of
   them can run it.

A proposal that recommends the flexible option everywhere is probably wrong. Most
of the interesting content here is in what should *not* be editable and why.

---

## 9. Out of scope

- **`version`.** It is the file's schema version, not a setting; nothing on the
  wrist should touch it.
- **`local_settings.json`** — the sibling class, its own vtable, its own offset
  table (§4.1). Note in passing what it would cost; do not build it.
- **New firmware ABIs.** One row exists, for kernel 1.4.0, derived from one
  CRC-verified image on one physical unit. Everything here is gated on it. Do not
  add speculative rows.
- **Finding `save()`'s caller.** FINDINGS' open question. This app writes the file
  itself and does not need it — but if you have a cheap way to settle whether
  anything ever writes the live copy back out, that answer changes §6.8.
- **A new address-derivation pass**, unless §5.3 or `height` at `+0x24` genuinely
  needs one. Both should be settlable against the supported message instead, which
  is much cheaper; try that first and say so.

---

## 10. Where the evidence is

```sh
# The splice tests, host, no kernel and no watch
cd SettingsKit && cmake -B build -S Tests && cmake --build build && ctest --test-dir build

# The screens, at real size with the real four grey levels
cd UnitToggle/Software/Apps/CustomGUI/rust && cargo run --bin sim --features sim

# Real settings files pulled off a watch
Spin/Tests/pulled/*/watch_settings.json
SettingsKit/Tests/SettingsSplice_test.cpp        # kRealFile, kRealMetric, kRealImperial

# The heart-rate ladder, already settled
Spin/README.md            # the zone section
Spin/Tests/ZoneLadder_test.cpp
Spin/Docs/RECOVERY-FIELD-RESULTS.md
```

There is no `cmake` and no ARM toolchain on the author's Mac; the `.uapp` build
happens in Docker against the image `.github/workflows/app-build.yml` pins.
**Re-read that pin rather than reusing a cached image id**, and run by image id.
`clippy` and `rustfmt` are not in the image and run on the host.

---

## 11. Definition of done

- Every question in §6 has a decision, a cost, and a stated falsifier.
- Every field in §4 is explicitly editable or explicitly not, with a reason.
- The heart-rate interface is chosen against §5.1's evidence, not against which
  option is more flexible.
- Whatever replaces `raw != 0 && raw != 1` is at least as tight per field, and the
  document says why.
- The generalised splice passes every existing case in `SettingsSplice_test.cpp`,
  and any case retired is named and justified.
- The screens exist as images rendered by the real renderer.
- No comment in the result would survive a change made in another file
  (`CLAUDE.md`, `/cleanup-comments`).
