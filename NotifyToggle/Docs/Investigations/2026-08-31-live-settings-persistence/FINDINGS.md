> **Canonical copy has moved.** Per project convention, all discoveries from this investigation
> are now tracked in `una-sdk`'s `research` branch at
> `Docs/Investigations/2026-08-31-live-settings-persistence/README.md` (worktree:
> `una-sdk/.claude/worktrees/live-settings-persistence/`), alongside the corrected `HANDOFF-PROMPT.md`.
> That copy is kept current, including a major update this session (the phone app's real
> persistence mechanism, decompiled and confirmed) not reflected below. This file is left as a
> point-in-time snapshot; don't extend it further.

# Findings ledger — live settings persistence

Extends `HANDOFF-PROMPT.md`. Method for every entry below: static disassembly of
`una-sdk/firmware-dumps/1.4.0/flash_08000000_4MB.bin` (CRC32 `0x14009d03`,
re-verified this session) via Ghidra headless + `arm-none-eabi-objdump`. No live
device access in this pass — nothing below has been touched on real hardware yet.

Tags: CONFIRMED / LIKELY / UNVERIFIED / REFUTED / CONTRADICTORY, per the
handoff's own convention.

## Corrections to the handoff doc

- **REFUTED: vtable base `0x0817579c`.** The true vtable pointer stored into
  live Settings objects is **`0x081757b0`**, not `0x0817579c`. The region
  `0x08175760`–`0x081757e0` is actually four contiguous 6-word Itanium-ABI
  vtables (2-word null prefix + 4 method slots, 0x18 bytes apart) belonging to
  four different classes: `0x08175768`, `0x08175780`, `0x08175798`,
  `0x081757b0`. The Settings class is the 4th block. The handoff's `save()`
  address-search hit was real, but it landed on a slot whose *containing*
  vtable it mis-identified.
  - CONFIRMED via the vptr-store instruction itself:
    `0x080abc46: ldr r3,[pc]@(0x80abc64→0x081757b0); 0x080abc48: str r3,[r4,#0]`,
    inside the object's constructor, r4 = `this`.
- **Corrected slot layout** (offsets from `0x081757b0`, 2-word null prefix
  already excluded): `+0x00: 0x080aba2d`, `+0x04: 0x080aba49`,
  `+0x08: load() = 0x080abc79`, `+0x0c: save() = 0x080aba61`. Save is 3 slots
  in from the real base, not the handoff's `+0x20`.
- **CONFIRMED: the constructor is `0x080abbb4`** (thumb `0x080abbb5`) — **this
  is the same address the handoff bans calling** (Section 1: "reset-to-factory
  defaults"). Static analysis confirms *why* the handoff's ban is correct: this
  is the object's real C++ constructor / default-initializer — it writes the
  vtable pointer, then zeroes and default-fills the struct. "Construct" and
  "reset to factory defaults" are the same code path for this class. **The ban
  stands; nothing here was called live, only read.**
  - Real callers found (magic-static lazy-init guards): `0x0806b5aa`,
    `0x0806e972`, `0x0807f9b4`. Caller #1 confirms objectBase = `0x20010ca8`,
    structBase = objectBase+8 = `0x20010cb0` — matches Section 4 exactly.
- **REFUTED: the lock at `0x0811ea74`/`0x0811eaac` is Settings-specific.** It's
  a compiler-emitted guard-variable helper shared by ~149 unrelated
  function-local statics across the kernel, not a dedicated Settings mutex.
  Doesn't change `LiveSettings.cpp`'s current behavior, just corrects the
  characterization in Section 4.
- **RESOLVED (REFUTED): Section 5's "is `local_settings.json` the same class"
  question.** Found a structurally identical but distinct constructor at
  `0x080ab998` writing a *different* vtable pointer (`0x08175798`) into a
  differently-shaped struct — a sibling class with its own vtable/save/load
  pair, not a second instance of the Settings class. (Link to
  `local_settings.json` specifically not yet confirmed — didn't cross-reference
  the `"Settings.Local"` string cluster.) **Practical effect: whatever general
  mechanism this investigation builds will need a second offset table and a
  second vtable for `local_settings.json` — it isn't free.**

## Section 5 items promoted/corrected

- `dailyGoals.activityMinutes/steps/floors`: **CONFIRMED** at `+0x18/+0x1c/+0x20`
  (corrected from handoff's `+24/+28/+32`) — cross-checked against 3
  independent sources: `save()`'s decompile, the constructor's own default
  values (30/5000/10), and (partially) live `settings.json`.
- `weight`: **CONFIRMED** as a float at `+0x28` (handoff had guessed `~+40`) —
  `save()` calls a distinct float-taking writer on this offset.
- **New, not in the handoff:** an undocumented 4-byte int at `+0x24`, between
  `floors` and `weight`, written by the same accessor used for the 3
  `dailyGoals` ints. **UNVERIFIED** — likely `height` by elimination and struct
  layout, not cross-checked against a live value yet.
- `heartRateZones`: offsets **corrected** from the handoff's `+15..+20` to
  `+16..+21` per `save()`'s actual loop bounds — but flagged
  **CONTRADICTORY**: the constructor writes what looks like a single 4-byte
  pointer-shaped literal at this same `+0x10`, which doesn't fit a 6-byte array
  start there. Unresolved — do not trust this offset until that's reconciled.
- `phone.notifications` (+5) and `watchFaceId` (+8): reconfirmed via a third
  independent source (constructor's default-init values). No change.

## Critical new finding: `save()` has no known caller anywhere in this firmware

Exhaustive checks across the full 4 MB image:
- Direct `bl 0x80aba60`: **zero hits.**
- Indirect vtable dispatch through slot `+0x0c` near any `blx` (checked both
  tight and 10-instruction windows): **zero hits** across ~3700 `blx` sites.

For contrast, **`load()` does have two real, confirmed callers** —
`0x0806b61c` (already known from the handoff) and a newly found
`0x0806bcd0` — so the class/offset math for `load()` is now doubly
cross-validated end-to-end. `save()` is not.

This breaks the handoff's Phase C premise ("trace forward from a real
`save()` call site") — there is no such call site to trace from, at least not
one reachable by the vtable-dispatch or direct-call patterns checked here.
Two live-caller register patterns for `load()` (`0x0806b61c` vs `0x0806bcd0`)
also don't fully agree with each other, which is a loose end worth revisiting.

Two explanations, unresolved:
1. `save()` is genuinely dead/unreachable code in this firmware build (maybe a
   leftover from an earlier persistence design, superseded by something else).
2. It's invoked through a mechanism this pass's search patterns don't cover —
   e.g. a vtable pointer or method index computed at runtime rather than
   embedded as a flash literal, or reached from a call site whose immediate
   offset isn't a literal `#0xc` (harder-to-grep patterns).

**Next step (not yet run):** search for xrefs to the `"2:/settings.json"`
string literal (`0x0815954f`) directly — this doesn't depend on `save()`'s
vtable slot at all, and was already one of Phase C's own falsification
criteria in the handoff. If a function references that string and reaches
FatFs write primitives, that's the real persist path regardless of whether
it's the same `save()` this session has been chasing.

## Bottom line

The struct-offset picture for `phone.notifications` is unchanged and stays
CONFIRMED (now via a third independent source). But the mechanism this session
set out to find — "call the kernel's real save-to-flash function" — currently
has no identified entry point. Phase C needs a different approach before any
live write/persist code gets written.
