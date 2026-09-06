# Where `units` lives in the live settings struct — kernel 1.4.0

Derived 2026-09-06 from `una-sdk-research`'s `firmware-dumps/1.4.0/flash_08000000_4MB.bin`,
the CRC-verified image `SettingsAddresses.cpp`'s signatures were taken from.

**Result: `settingsStructBase + 4`, one byte, `0` = metric, `1` = imperial.**

## Why the constructor alone could not answer it

`NotifyToggle`'s existing offsets were read out of the settings constructor at
`0x080abbb4`, which stores each documented default into the object. That works
for a field whose default is non-zero. It cannot work for `units`, because the
constructor's third instruction is

```
movs r2, #0x30      ; 48 bytes
movs r1, #0
str  r3, [r0], #8   ; vtable at objectBase; r0 now objectBase+8 == structBase
bl   #0x802af3c     ; memset(structBase, 0, 0x30)
```

and the default units are metric, which is the zero it just wrote. There is no
store to find.

That same instruction is worth keeping in view for another reason: it is what
puts the struct eight bytes past the object, so a `[r4, #N]` in the constructor
(where `r4` is the object) is `structBase + N - 8`. Three fields confirm the
relationship holds — `version` = 2 at `structBase+0`, `phone.notifications` = 1
at `structBase+5` (the `strb r6, [r4, #0xd]`), and the daily goals 30 and 5000
at `structBase+0x18`/`+0x1c`.

## The parser does answer it

The string `"units"` at `0x08165e38` is referenced from two literal pools,
`0x080abb78` and `0x080abda8` — both adjacent to the constructor. The second
belongs to the settings *parser*, which begins reading fields at `0x080abcd6`.
In that function `r4` is the struct base directly, not the object; the same
three fields confirm it, and the six-iteration loop at `0x080abd1a` storing one
byte at a time from `structBase+0x10` is `heartRateZones`.

The units key is read into a stack string and compared against two literals:

```
0x080abcd6  movs r3, #0
0x080abcd8  ldr  r1, [pc, #0xcc]     ; -> 0x081757c0
0x080abcda  add  r2, sp, #0x14
0x080abcde  strd r3, r3, [sp, #0x14]
0x080abce2  bl   #0x80ca86e          ; read "units" into sp+0x14

0x080abce6  ldr  r1, [pc, #0xc4]     ; -> 0x081757c0
0x080abcea  bl   #0x80abc68          ; compare
0x080abcee  ldr  r5, [pc, #0xc0]     ; r5 = 0x081757c8
0x080abcf2  bne  #0x80abd78          ; matched the first literal
0x080abcf4  mov  r1, r5
0x080abcf8  bl   #0x80abc68          ; compare
0x080abcfe  bne  #0x80abd8a          ; matched the second literal
```

Those two pointers are into a table of `{u32 length, const char *text}` pairs:

| Address | Length | Text |
|---|---|---|
| `0x081757c0` | 6 | `0x08165ef6` → `metric` |
| `0x081757c8` | 8 | `0x08165efd` → `imperial` |

Both match branches converge two instructions later:

```
0x080abd78  sub.w r1, r5, #8         ; the metric entry
0x080abd7c  ldm   r1, {r0, r1}
0x080abd7e  ldm.w r5, {r2, r3}       ; the imperial entry
0x080abd82  bl    #0x807d44c         ; string-view equality -> 0 or 1
0x080abd86  strb  r0, [r4, #4]       ; <- the store
0x080abd88  b     #0x80abd00

0x080abd8a  mov   r1, r5             ; imperial: compare the entry with itself
0x080abd8c  b     #0x80abd7c
```

The metric branch compares two different entries and stores the 0 that falls
out; the imperial branch compares `r5` with itself and stores 1. So the byte at
`structBase+4` is `unitsImperial`, and `0` means metric.

Two things fall out for free:

- **The lengths in that table are 6 and 8**, which is independently the same
  two-byte delta measured in the file itself the same day (245 → 247 bytes).
  Two unrelated routes to the same number.
- **A token matching neither spelling stores nothing.** Control falls from
  `0x080abcfe` to `0x080abd00` and carries on to `watchFaceId`, leaving the byte
  at whatever it already held — the constructor's zero on a fresh parse. This is
  why `SettingsSplice::setUnits` refuses a third spelling rather than coercing
  it: the firmware would not have honoured it either.

## What would falsify this

A firmware whose parser stores the units result at a different offset, or reads
a different set of spellings, or an observed live struct where `structBase+4`
disagrees with `RequestSystemSettings::imperialUnits`.

That last one is the check worth having, and it is cheap: unlike
`phone.notifications`, this field *is* reported through a supported message, so
an app can compare its raw read against the kernel's own answer and flip the
byte to see the message follow. `UnitToggle` does exactly that at launch and
refuses when they disagree — the offset above has been derived statically but
never yet confirmed against a running watch.
