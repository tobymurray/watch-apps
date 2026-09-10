# The whole live settings struct — kernel 1.4.0

Derived 2026-09-07 from `una-sdk-research`'s
`firmware-dumps/1.4.0/flash_08000000_4MB.bin`, CRC32 `0x14009D03` re-verified
this session — the same image `SettingsAddresses.cpp`'s signatures were taken
from.

[`2026-09-06-units-offset.md`](2026-09-06-units-offset.md) derived one field
from the settings **parser** rather than the constructor, and said the method
was reusable. This is that method run over the whole function, which turns out
to name every key it stores. Nothing here needed a watch.

**Result: all ten fields the kernel parses, with no unknowns left.**

| Offset | Bytes | Field | JSON key the parser reads | Reader |
|---|---|---|---|---|
| `+0x00` | 4 | `version` | `version` — read, then **discarded** | `0x080ca754` |
| `+0x04` | 1 | `unitsImperial` | `units` | string compare |
| `+0x05` | 1 | `phoneNotifications` | `phone.notifications` | `0x080ca6a4` |
| `+0x06` | 2 | — | never written | — |
| `+0x08` | **8** | `watchFaceId` | `watchFaceId` | `0x080ca7d0` |
| `+0x10` | 6 | `heartRateZones[6]` | `heartRateZones[%u]`, six times | `0x080ca7a0` |
| `+0x16` | 2 | — | never written | — |
| `+0x18` | 4 | `dailyGoals.activityMinutes` | `dailyGoals.activityMinutes` | `0x080ca754` |
| `+0x1c` | 4 | `dailyGoals.steps` | `dailyGoals.steps` | `0x080ca754` |
| `+0x20` | 4 | `dailyGoals.floors` | `dailyGoals.floors` | `0x080ca754` |
| `+0x24` | 4 | `height` | `height` | `0x080ca754` |
| `+0x28` | 4 | `weight`, `f32` | `weight` | `0x080ca81e` |
| `+0x2c` | 4 | — | never written | — |

The struct is `0x30` bytes: the constructor's `memset(structBase, 0, 0x30)` at
`0x080abbc4` is its size, and `str.w r3, [r0], #8` two instructions earlier is
what puts it eight bytes past the object at `0x20010ca8`.

## How the keys were read off

The parser is `0x080abc78` — the function the vtable's `load()` slot points at,
called from `0x0806b61c` and `0x0806bcd0`, the two sites
`Docs/Investigations/.../FINDINGS.md` already found. Its fourth argument is the
struct base, so `[r4, #N]` in it is `structBase + N` directly, not the
constructor's `structBase + N - 8`. `0x0806bcc4` sets that argument up as
`0x20010cb0`, which is a third confirmation of the base:

```
0x0806bcc4  ldr   r3, [pc, #364]     ; 0x20010cb0
0x0806bcc8  ldr   r2, [sp, #0x54]
0x0806bccc  sub.w r0, r3, #8         ; this = the object
0x0806bcd0  bl    #0x80abc78         ; parse(this, json, len, structBase)
```

Each field is one `ldr r1, <key>` / `add r2, r4, #N` / `bl <reader>` triple, so
the key and the offset are in the same three instructions and cannot be paired
wrongly. The keys are dotted paths — `phone.notifications`, `dailyGoals.steps` —
and the array is a **formatted** key, `heartRateZones[%u]`, built into a 32-byte
stack buffer by `0x0802a83c` on each of six iterations:

```
0x080abd16  movs  r5, #0
0x080abd18  ldr   r7, [pc, #160]     ; -> "heartRateZones[%u]"
0x080abd1a  mov   r3, r5             ; the index
...
0x080abd26  add.w r2, r4, #16        ; structBase + 0x10
0x080abd2a  add   r2, r5             ;            + i
0x080abd32  bl    #0x80ca7a0         ; the one-byte reader
0x080abd36  cmp   r5, #6
0x080abd38  bne.n #0x80abd1a
```

### `height` at `+0x24` is settled

FINDINGS had it as **UNVERIFIED** — "likely `height` by elimination and struct
layout". The parser names it:

```
0x080abd5e  add.w r2, r4, #36        ; structBase + 0x24
0x080abd62  ldr   r1, [pc, #104]     ; -> 0x081642b0 = "height"
0x080abd66  bl    #0x80ca754         ; the 32-bit reader
```

`save()` at `0x080aba60` writes the same offset under the same string
(`0x080abb9c` in its own literal pool), which is the second source. No watch
needed, and the supported-message check §4.1 of the prompt proposed is now a
third rather than the first.

### The `heartRateZones` contradiction is resolved, not merely outvoted

FINDINGS flagged `+0x10` **CONTRADICTORY**: the constructor writes "a single
4-byte pointer-shaped literal" there, which does not fit a six-byte array. It is
not a pointer. It is the first four bytes of the default ladder, stored as one
word because they happen to be four contiguous bytes:

```
0x080abbcc  ldr   r3, [pc, #132]     ; the literal at 0x080abc54 = 0x9885725F
0x080abbd0  str   r3, [r4, #24]      ; objectBase+24 = structBase+0x10
0x080abbd2  movw  r3, #0xBEAB
0x080abbd6  strh  r3, [r4, #28]      ; objectBase+28 = structBase+0x14
```

A word then a halfword is six bytes at `+0x10..+0x15`, and little-endian they
are **95, 114, 133, 152, 171, 190** — which is 50/60/70/80/90/100% of 190
exactly. So the four sources now agree: the constructor's default, the parser's
six-iteration loop, `save()`'s six-iteration loop (`r6 = r4+15` pre-indexed to
`r4+16`, stopping at `r4+21`), and the ladder a watch reported through
`RequestSystemSettings` on 2026-09-03 (`[92,110,129,147,166,184]`, which is the
same six percentages of 184).

**The rule is the firmware's own, twice over.** `Spin` measured it from a watch;
the constructor states it. 190 is also 220 − 30, and the file's own default date
of birth is `1990-01-01` — an age-30 estimate — which is why the default ladder
is the one it is.

### `watchFaceId` is 64-bit

`0x080ca7d0` calls `strtoull` and stores eight bytes:

```
0x080ca80c  bl    #0x80333b0         ; strtoull(token, NULL, 10)
0x080ca810  strd  r0, r1, [r7]
```

`save()` reads it back the same width (`ldrd r2, r3, [r4, #8]`). So `+0x08..+0x0F`
is one field, and `LiveSettings::readChecked` reading eight bytes there and
bounding them at 1000 is a plain range check on one value — not, as it looks, a
check that `+0x0C..+0x0F` are separately zero. Nothing else lives in those bytes
to be constrained.

### What the readers do with a value the app would not have written

This is the question that decides whether a bad write is inert or destructive,
and the readers answer it. None of them validates a range; the app's bounds are
the only bounds.

- **`0x080ca754`, the 32-bit reader** — locates the key, then `strtol(token,
  NULL, 10)` and a 4-byte store. **No type check at all.** So a key present with
  a non-numeric value stores `strtol`'s failure return, `0`; an out-of-range one
  stores `LONG_MAX`; a negative one stores a negative. A missing key leaves the
  field at whatever the constructor put there.

  This is the opposite of `units`, where a token matching neither spelling
  stores *nothing*. A malformed number is not inert — it zeroes the field.
- **`0x080ca7a0`, the one-byte reader** — the 32-bit reader into a stack `int32`,
  then `strb` of the low byte, only if the key was found. So `300` in the file
  lands as `44` in the struct, silently. `EditableFields.hpp`'s `isWellFormed`
  refuses a range that could do that.
- **`0x080ca81e`, the float reader** — `strtod` then `__aeabi_d2f` and a 4-byte
  store. So the parser accepts a fractional `weight` and can represent it; the
  file on this watch held the integer `90`.
- **`0x080ca6a4`, the boolean reader** — the only one that is both type- and
  range-checked. Token type 3 stores 1, type 4 stores 0, type 2 (a number)
  stores 0 or 1 and **refuses anything above 1**, leaving the byte alone.

### `gender` and `dateOfBirth` are not in this firmware at all

Neither string appears anywhere in the 4 MB image — not as a key the parser
reads, not in `save()`'s literal pool, not in any other function. The parser
returns two instructions after `weight`. So nothing in kernel 1.4.0 reads or
writes either field; they are the phone app's, preserved in the file only
because nothing on the watch rewrites it.

Falsified by either string appearing in another firmware image.

### `save()` did not write the file on this watch, and can be shown not to have

`save()` emits `version` **first** and emits no `gender` and no `dateOfBirth`.
The real file emits `version` **last** and carries both. So the file on this
watch was not produced by `save()` — which is a fact about the file rather than
another search for a caller, and it agrees with FINDINGS finding no caller at
all. An independent scan of every `bl`/`blx` in the image for
`0x080aba60` this session also found zero.

The one other reference to the `"2:/settings.json"` string outside the settings
object is `0x08072fa0`, which builds a string view of that path and *compares*
it with its argument — a path test, not a writer, with one caller at
`0x080732be`.

**What follows for an app:** nothing on this firmware writes the live copy back
out, so a file-only edit would not be silently reverted by `save()`. But if
`save()` ever did run it would delete `gender` and `dateOfBirth` outright,
whatever any app had done, so this is not a licence to edit them — it is a
reason not to depend on the live copy and the file agreeing.

## What would falsify all of this

A different firmware image: every address here is a literal in one 4 MB build,
and the offsets are that build's `WatchSettings` layout. The cheap live check is
the one `UnitToggle` already runs for `units` — read the raw field and compare it
against `RequestSystemSettings`, which reports six of these ten. A disagreement
on any of them falsifies that row.
