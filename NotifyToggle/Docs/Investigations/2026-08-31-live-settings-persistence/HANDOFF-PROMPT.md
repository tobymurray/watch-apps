# Handoff prompt: find the real settings-persist path so NotifyToggle can be a full, durable configuration interface for the UNA Watch's `settings.json`

Paste everything below into a fresh session (working directory: `/home/toby/git/cpp/watch-apps`,
with `/home/toby/git/cpp/una-sdk` also checked out alongside it — both are needed).

---

## 0. Objective

`NotifyToggle` (`watch-apps/NotifyToggle`) is a watch app that started as a single
phone-notifications on/off switch. Tonight's session proved that the app-facing SDK
filesystem sandbox can **never** reach `settings.json` (see "Ruled out" below), so
the only viable route is direct memory access to the kernel's live, in-RAM settings
struct — which apps can do freely on this hardware (no MPU, no privilege
separation; see "Confirmed" below). A **live, in-memory, immediate-effect** write
of `phone.notifications` is already implemented, tested, and working on real
hardware (`Software/Libs/Sources/LiveSettings.cpp`). It does not yet survive a
reboot: nothing has written the new value back to flash.

**The goal now is bigger than one boolean.** The owner's intent is a general,
reusable configuration interface for `settings.json`-backed fields (and its
sibling `local_settings.json`, backed by the same class, evidence below) that this
app can fully wield going forward, independent of whether/when UNA's own SDK adds
native support for it. So the deliverable of this session is not "one more
address" — it's understanding the **general save/persist mechanism** for this
settings class well enough to read and durably write arbitrary fields on it, the
same way `LiveSettings.cpp` already does for one field in RAM.

Concretely: locate (or safely reconstruct) the real "write current settings to
`2:/settings.json`" code path, get one field (`phone.notifications`, already fully
characterized) surviving an actual power cycle, and then generalize the resulting
mechanism into a small reusable module other fields can use.

---

## 1. Guardrails — read first

- **Own hardware, explicit informed consent already given.** The owner bought this
  watch specifically for this kind of access, and the vendor has told them they
  will help restore firmware if something gets bricked. That changes the
  acceptable risk envelope for *device* risk — it does **not** relax care around
  **data** risk (see next point), and it does not mean skipping verification
  steps to move faster.
- **Protect the user's personal data more carefully than the device's
  bootability.** `settings.json` holds real personal values with no independent
  backup except the device's own `.bak` and whatever this investigation captures
  in `NotifyToggle/DeviceBackups/` (height, weight, gender, date of birth, HR
  zones, daily goals). A bricked device is vendor-recoverable; a silently
  corrupted or reset settings file may not be. Back up `settings.json` (already an
  established habit this session — see `NotifyToggle/DeviceBackups/`) before every
  single live write attempt, and verify a read-back after every write, exactly as
  `LiveSettings.cpp` and `SettingsFile.cpp` already do.
- **Never call `0x080abbb4` / thumb-address `0x080abbb5`.** This was initially
  mistaken for a save function; it is actually a reset-to-factory-defaults routine
  (it `memset`s 48 bytes of the struct to zero and writes hardcoded constants).
  Calling it would zero or overwrite real fields (heart-rate zones, daily goals,
  etc.), not persist anything.
- **Prefer calling into the kernel's own tested file-write path over hand-rolling
  flash access.** This MCU's flash needs proper unlock/erase/program sequences;
  getting that wrong from scratch risks corrupting far more than one file (a whole
  erase block, potentially the running kernel). If the real "commit to flash"
  function can be found and its calling convention understood, call *that* —
  don't reimplement flash writes from first principles unless every other avenue
  is exhausted, and if it comes to that, treat it as its own escalation requiring
  the owner's explicit sign-off, the same way tonight's memory-write step needed
  explicit confirmation before proceeding (see "A process note" below).
- **Every new field offset needs independent cross-validation before it's
  trusted**, the same way `phone.notifications` (offset +5) was cross-checked
  against `watchFaceId` (offset +8) and against `settings.json`'s actual current
  value, live, on the device — not accepted on a single disassembly pass alone.
  Section 4 below lists which offsets already have that level of confidence and
  which don't yet.
- **Do not extract or redistribute the OTA decryption key/algorithm** if you cross
  it, and **keep the flash dump / large decompiled excerpts out of both git
  repos** (`watch-apps` and `una-sdk`) — this mirrors the existing convention on
  `una-sdk`'s `research` branch investigations. Findings (addresses, offsets,
  call-graphs, code) belong in this investigation's own doc; the 4 MB binary does
  not.
- **A process note on live vs. static work**: tonight, the moment code was written
  that reads/writes a hardcoded physical RAM address on real hardware, Claude
  Code's own safety classifier blocked the compile command outright, and required
  the owner to explicitly confirm before it would proceed. Expect the same the
  first time this session compiles or deploys anything touching these addresses
  — don't try to route around it; surface it and get the same kind of explicit,
  specific go-ahead, especially before anything that **writes** (reads have not
  triggered this).

---

## 2. Orientation

The UNA Watch (this exact unit) runs kernel 1.4.0 on an STM32U5A5 (Cortex-M33).
Apps are ordinary position-independent `.uapp` binaries the kernel loads into RAM;
critically, **the kernel does not sandbox an app's memory access** — no MPU, CPU
runs privileged, no TrustZone split. A running app can read and write arbitrary
addresses, including kernel RAM and (in principle) flash, with a plain volatile
pointer. This isn't new tonight — `una-sdk`'s own `research`/
`docs/hardware-config-recovery` branches document and rely on exactly this,
including a full previously-taken, CRC-verified 4 MB dump of this same watch's
kernel flash at both 1.3.0 and 1.4.0 (see Section 6).

Tonight's investigation started from "why can't `NotifyToggle` read
`settings.json` over the normal SDK filesystem API" and ended by hand-disassembling
the real path from JSON file to live struct — read this section and Section 4
before doing anything else; they're the ground truth this session builds on.

---

## 3. Ruled out (don't redo this work)

- **The app-facing filesystem sandbox cannot reach `settings.json`, full stop.**
  `FileSystemGuard::getFullPath` (the function every app's `Kernel::fs` call goes
  through, disassembled at flash `0x0809c15c`–`~0x0809c31a` in the 1.4.0 dump)
  implements **exactly one** hardcoded relative-path escape: a literal string
  match on `"../SharedData"` (optionally with `/<name>` appended), rewritten to
  `"<drive>:/SharedData[/<name>]"` on the same FatFs volume the app lives on. Any
  other path starting with `..` does not match, and the function logs
  `"Rejected/invalid path! [%s]"` and fails outright — it does not fall through to
  naive concatenation, and there is no general parent-directory mechanism. This
  was confirmed twice: once by disassembly (no other `%.*s/<name>`-shaped literal
  exists anywhere in the string table besides `SharedData`), and once empirically
  on the real device (every spelling of a two-`..` path — `../../settings.json`,
  `/../settings.json`, `./../settings.json`, `Apps/../../settings.json`,
  `../Apps/../settings.json` — failed identically; only single-`..` paths like
  `../SharedData/` ever resolved).
- `settings.json` genuinely lives at `2:/settings.json` (confirmed both by the
  literal string in the kernel and by an independent BLE File Transfer Service
  filesystem walk on `una-sdk`'s `research` branch, commit `80d77cb3`,
  `Docs/Investigations/2026-07-29-hardware-config-recovery/BLE-COMPANION-protocol-spec.md`
  §2.2.2 — the real on-watch tree has `settings.json` and `Apps/<name>/` as
  siblings directly under the root of drive `2:`). It's on the **same** drive an
  app's own sandbox lives on — this was never a "different volume" problem, it's
  that the app-facing wrapper simply never exposes a path to it.
- `NotifyToggle`'s `SettingsFile.cpp` / `SettingsPatch.cpp` (the file-based
  approach) are kept in the tree as a record of this — they are fully tested and
  correct for what they do, but conclusively cannot reach the target file on this
  firmware. Don't extend them for this purpose; `Gui.cpp` no longer calls them for
  the live toggle logic.

---

## 4. Confirmed (multi-method, safe to build on)

All of this was derived from the 1.4.0 flash dump (`una-sdk/firmware-dumps/1.4.0/`,
whole-image CRC32 `0x14009D03`, confirmed to be **this exact unit** — same UID as
the 1.3.0 dump per that folder's own README) and, where marked **(live)**,
independently confirmed on the running watch itself tonight.

- Apps run **privileged** (`CONTROL=0x6`), **MPU disabled**
  (`MPU_CTRL.ENABLE=0`), **no TrustZone** (`TZEN=0`), **RDP level 0** (`0xAA`,
  open) — from this unit's own `dump_context.txt` (1.4.0) and the 1.3.0-era
  investigation's register sweep, and unchanged across the 1.3.0→1.4.0 update.
- **Settings data struct base = `0x20010cb0`.** Derived from the JSON loader's
  call site:
  ```
  806b596: ldr r4, [pc, #504] @ (0x806b790)   ; literal: 0x20010ca0 (see "lock object" below)
  806b5ba: ldr r4, [pc, #472] @ (0x806b794)   ; literal: 0x20010ca8 ("object base")
  806b618: add.w r3, r4, #8                    ; 0x20010ca8 + 8 = 0x20010cb0
  806b61c: bl    0x80abc78                     ; the settings JSON loader; r3 is its 4th arg
  ```
  Inside the loader (`mov r4, r3` in its prologue at `0x80abc7c`), field
  destinations are offsets from this `0x20010cb0` base.
- **`phone.notifications` = one byte at `0x20010cb5`** (`0x20010cb0 + 5`).
  Confirmed on both the load side and the save side of the code (below), and
  **(live)**: read matched `settings.json`'s stored value both before and after a
  write; write-then-immediate-readback matched; the value stayed stable across
  many ~1s polls with no drift or crash. This is what
  `LiveSettings.cpp`'s `kPhoneNotificationsAddr` uses today.
- **`watchFaceId` = 8 bytes (uint64) at `0x20010cb8`** (`0x20010cb0 + 8`). Used as
  a cross-check field in `LiveSettings.cpp` (must be `<= 1000` or the read/write is
  refused). **(live)**: read `0`, matching `settings.json`'s `"watchFaceId":0`,
  both before and after the notifications write.
- **The JSON→struct loader** is at flash `0x080abc78` (thumb address
  `0x080abc79`). Field-by-field, e.g. for `phone.notifications` specifically:
  ```
  80abd0c: ldr r1, [pc,#168] @ (0x80abdb8)   ; r1 = &"phone.notifications"
  80abd0e: adds r2, r4, #5                    ; dest = (loader's r4) + 5
  80abd12: bl    0x80ca6a4                     ; getBool-into-r2
  ```
- **The struct→JSON serializer ("save")** is at flash `0x080aba60` (thumb
  `0x080aba61`). It independently reads the **same offsets** relative to its own
  2nd argument (its `r1`, moved to `r4` in its prologue): `r4+4` (a bool used to
  select between the literal strings `"metric"`/`"imperial"` — i.e.
  `unitsImperial`), `r4+5` (`phone.notifications`, read as
  `ldrb r2, [r4, #5]` immediately before the `"notifications"` key string is
  loaded — this is the second, independent confirmation of that offset),
  `r4+8` (`watchFaceId`, read as 8 bytes via `ldrd`). This cross-validates the
  struct layout from the opposite direction of the loader.
- **`save()` and `load()` sit in adjacent C++ vtable slots.** The vtable was found
  by searching the flash image for the raw 4-byte value of `save()`'s thumb
  address (`0x080aba61`) and finding exactly one hit, at flash `0x081757bc`,
  inside what is structurally a vtable (a run of function-pointer-shaped words
  immediately followed by plain data — a two-entry `{length, pointer}` lookup
  table for the literal strings `"metric"` (len 6) and `"imperial"` (len 8) at
  `0x081757c0`–`0x081757cf`, matching the `unitsImperial`→string conversion seen
  in `save()`). Slot layout observed (offsets from `0x0817579c`, **not confirmed
  to be the true vtable start** — there may be more slots before this, e.g. a
  destructor pair or RTTI-related entries per the Itanium ABI vtable prefix):
  `+0x00: 0x080ab839`, `+0x04: 0x080ab6e1`, `+0x08: 0x080ab851`, `+0x0c: 0`,
  `+0x10: 0`, `+0x14: 0x080aba2d`, `+0x18: 0x080aba49`,
  `+0x1c: 0x080abc79` (**load**), `+0x20: 0x080aba61` (**save**).
- **A mutex-like lock guards this struct**: lock object at `0x20010ca0` (the
  literal pool value at `0x0806b790`, loaded right before an atomic-flag check
  `lda r3,[r4]; lsls r3,r3,#31; bmi` and a lock/unlock call pair at `0x0811ea74`
  (acquire) / `0x0811eaac` (release)), used in the code path guarding a
  reset-to-defaults call. `LiveSettings.cpp`'s writes do **not** currently take
  this lock (see its own header comment) — closing that race, if it's worth
  closing, is in scope for this session.

---

## 5. Likely / unverified — re-check before relying on these

These came from a **single** disassembly pass (the `save()` function body) and
were **not** independently cross-checked the way `phone.notifications` and
`watchFaceId` were (against a second code path *and* a live value). Treat them as
leads, not facts:

- `heartRateZones`: 6 × `uint8_t` at `0x20010cb0 + 15` through `+20` (inclusive),
  read via a loop in `save()` (`add.w r6, r4, #15` / `add.w r7, r4, #21`).
  `settings.json` on this unit currently reads
  `"heartRateZones":[92,110,129,147,166,184]` — an easy, low-risk live
  cross-check: read those 6 bytes and confirm they match before trusting the
  offset.
- `dailyGoals.activityMinutes` / `.steps` / `.floors`: read as 32-bit values at
  `r4+24`, `r4+28`, `r4+32` in one pass through `save()`. `settings.json` reads
  `{"activityMinutes":30,"steps":5000,"floors":5}` — again a cheap live
  cross-check once the reading code exists.
- A `weight` field, read as a **float** (`vldr s0, [r4, #40]` or thereabouts, seen
  in an earlier, less careful pass over the same function — re-derive this
  cleanly rather than trusting the exact offset transcribed here).
  `settings.json` reads `"weight":90`.
- The exact vtable slot semantics for `+0x00`/`+0x04`/`+0x08`/`+0x14`/`+0x18`
  (constructor? destructor? a "hasChanged" hook? load-from-defaults?) — unknown,
  not needed for the phone.notifications goal but relevant to fully
  understanding the class if the generalized config interface is meant to cover
  more than a couple of fields.
- Whether `"Settings.Local"` (`local_settings.json`, fields `alertMute`,
  `gpsPwrMode`, `sound.*`, `vibro.*`, `clock.*` — string cluster at flash
  `0x165d72`–`0x165e21`) is a **second instance of the same class** (sharing this
  exact vtable) or a structurally similar sibling class with its own vtable and
  save/load pair. If it's the former, the general mechanism this session finds
  covers both files for free; worth checking once the "this" object question
  (Section 8, Phase A) is resolved.

---

## 6. Resources

**Flash dumps** (already on disk, don't re-dump unless you have a specific reason
— e.g. suspecting this analysis session's own tooling corrupted state, or wanting
a live register sweep alongside a fresh dump):
```
una-sdk/firmware-dumps/1.4.0/flash_08000000_4MB.bin   # THIS unit, kernel 1.4.0 — use this one
una-sdk/firmware-dumps/1.4.0/README.md                 # provenance, verification, register sweep
una-sdk/firmware-dumps/1.4.0/dump_context.txt          # CONTROL/MPU_CTRL/etc. register capture
una-sdk/firmware-dumps/1.3.0/flash_08000000_4MB.bin    # same unit, older kernel — useful for diffing
```
Re-verify before trusting, same as every prior session:
```sh
python3 -c "import zlib; print(hex(zlib.crc32(open('una-sdk/firmware-dumps/1.4.0/flash_08000000_4MB.bin','rb').read())))"
# expect 0x14009d03
cd una-sdk/firmware-dumps/1.4.0 && sha256sum -c sha256sums.txt
```

**Prior investigation docs** (read before re-deriving anything they already
cover):
- `una-sdk` branch `research`, `Docs/Investigations/2026-07-29-hardware-config-recovery/README.md`
  — the hardware-recovery investigation this all builds on (register sweep,
  hardware inventory, the peek/dump technique).
- Same folder, `BLE-COMPANION-protocol-spec.md` §2.2.2 — the BLE-FTS filesystem
  walk that independently confirmed `settings.json`'s real location.
- `una-sdk/firmware-dumps/1.4.0/README.md` and `1.3.0/README.md` — dump
  provenance and the 1.3.0→1.4.0 diff (confirms the security posture didn't
  change across the update; useful context if this session also touches 1.3.0).

**This session's own working app** (`watch-apps/NotifyToggle`):
- `Software/Libs/Sources/LiveSettings.cpp` / `.hpp` — the working, live-tested
  RAM read/write for `phone.notifications`. Extend this, or generalize it, rather
  than starting a parallel mechanism.
- `Software/Libs/Sources/DebugLog.cpp` / `.hpp` — **the only diagnostic channel
  available.** There is no debug UART wired up in this environment (a UNA Dev
  Tool adapter would give one — see the hardware-recovery investigation's
  README for the 921600-baud technique — but tonight's whole session worked
  without one). Every diagnostic finding tonight came from writing lines to a
  file in the app's own sandbox directory (`gui-debug.log` / `service-debug.log`)
  and reading it back over USB mass storage after each test cycle. Keep using
  this pattern; it's slow (one physical reconnect per experiment) but has been
  completely reliable all night.
- `Software/Apps/CustomGUI/Gui.cpp` — wires `LiveSettings` into the UI; R1 toggles.
- `DeviceBackups/` — every `settings.json` snapshot and crash/success log taken
  tonight, timestamped. Keep this habit: back up before every write attempt.

**Tooling**: `arm-none-eabi-objdump` is installed and is what produced every
finding above (`objdump -D -b binary -m arm -Mforce-thumb --adjust-vma=0x08000000
--start-address=0x... --stop-address=0x... flash_08000000_4MB.bin`). It's a flat
linear disassembly with no function/xref analysis — every cross-reference in this
document was found by hand, searching the binary for the little-endian 4-byte
encoding of a target address (see the `python3` / `struct.pack("<I", addr)`
pattern used throughout tonight's session — ask the transcript or re-derive, it's
a five-line script). **Neither radare2 nor Ghidra is installed.** Getting one of
them installed (`sudo pacman -S radare2` on this Arch host) would make Section 8,
Phase A (finding the vtable's live object / the real call site) meaningfully
faster — **ask the owner to install one if the manual xref-by-value technique
becomes the bottleneck.**

---

## 7. What you'll need to ask for

- **Explicit confirmation before compiling or deploying anything that performs a
  live memory *write*** (reads have not triggered Claude Code's safety
  classifier tonight; writes to this specific class of hardcoded-address code
  did, once, and needed the owner's explicit go-ahead — expect to ask again for
  new write code, don't assume the earlier confirmation blanket-covers new
  addresses/functions).
- **A physical reconnect cycle per experiment**, same as all night: deploy →
  owner power-cycles and exercises the app → owner reconnects → you read the log.
  There is no faster loop available without the UNA Dev Tool adapter (a debug
  UART) — ask the owner whether they have one or want to acquire one if this
  session's pace becomes limiting.
- **radare2 or Ghidra**, if Phase A's vtable/call-site hunt turns out to need
  proper xref analysis rather than the manual technique (likely, per the
  seam-hunt investigation's own experience with this exact class of problem).
- If the trail leads toward needing to call an unfamiliar internal function with
  more than one or two arguments (as `save()` itself did — it forwards two of its
  own arguments into a JSON-writer's init call whose purpose wasn't fully
  resolved tonight): **stop and lay out the specific risk before writing the call
  site**, the same way this session did before the RAM-poke step. Don't treat
  "we found an address" as equivalent to "safe to call."

---

## 8. The plan

### Phase A — Find the live "this" object and the real call site

`save()`/`load()` are virtual — nothing in the disassembled kernel range
(`0x08060000`–`0x08210000`, essentially the whole real firmware image) calls
`0x080aba60` directly via `bl`; it's only reachable through the vtable at
`~0x0817579c`. Find:
1. Where a live object gets **this vtable's address** stored into its first word
   (a constructor, or a static initializer) — search for the vtable base address
   as a raw little-endian value the same way `save()`'s own address was found, or
   (much faster with r2/Ghidra) look for xrefs to `0x0817579c`.
2. The **live RAM address of that object** — likely a static global, following
   the same pattern as `0x20010ca8`/`0x20010cb0` (a literal-pool-embedded fixed
   address, not heap-allocated).
3. **The real call site**: code that loads `*(objectAddr)` (the vtable pointer),
   indexes to offset `+0x20` (the save slot) or `+0x1c` (load), and does an
   indirect call (`blx`). This is what actually invokes save/load in normal
   operation — e.g. triggered by a BLE write from the phone's settings screen, a
   specific kernel message handler, or similar.
**Falsification:** a real call site loads the vtable pointer from the *same*
object address found in step 2, and the offset it indexes matches `+0x20`
exactly (not a neighboring slot) when it's clearly calling save (compare against
`+0x1c` for load — both should be reachable, from different call sites).

### Phase B — Read off the real calling convention

Once a genuine call site is found, read what it actually passes as `save()`'s
`r0`/`r2`/`r3` (the `r1` argument is already known — the settings data pointer,
`0x20010cb0`). In the traced code, `r0` was discarded inside `save()` itself and
`r2`/`r3` were stored verbatim into a stack-local JSON-writer object without being
immediately used further — their real purpose (buffer bounds? a
context/callback pointer? something that matters once the writer object is later
consumed) is still unknown. A real call site should make this obvious.
**Falsification:** the values passed at the real call site should be small,
plausible constants (not heap/stack addresses that would only make sense in that
caller's own stack frame) if they're meant to be reusable from a different
caller — if they're clearly caller-frame-relative, this function isn't safely
callable standalone and the plan needs to shift to Phase C.

### Phase C — Find (or reconstruct) the actual flash write

`save()` itself only builds an in-memory key/value list and returns whether
construction hit an internal error — it does **not** write `2:/settings.json`.
Trace forward from a real call site (Phase A) past its `save()` call: the code
that runs *after* checking `save()`'s success should serialize the writer
object's key/value list into a real JSON string and call into the **unwrapped**
`FS::FileSystem` class (not `FileSystemGuard` — that's the app-sandboxed one,
already proven to reject this path) to open, write, and close
`2:/settings.json`. This is the function this whole session is really after.
**Falsification:** the candidate function should visibly reference the string
`"2:/settings.json"` (flash `0x0815954f`) or open a path built from a stored
drive-prefix + that literal, and should call something that ultimately reaches
FatFs write primitives (`f_write`-shaped calls), not just log or return.

### Phase D — Prove it end-to-end, live, on real hardware

With the real call chain (or, if Phase C leads somewhere Section 1's guardrails
say to stop, an alternative that reuses the same underlying `FS::FileSystem`
methods `FileSystemGuard::objectInfo` etc. delegate to once its sandbox check is
bypassed at the same layer) understood:
1. Back up `settings.json` fresh (`DeviceBackups/`).
2. Wire a call into `LiveSettings.cpp` (or a new module) that: reads the current
   live value (as today), flips it, invokes the real persist path, and reads
   `settings.json` back over USB after a **reboot** — not just an in-app
   readback — to confirm the change actually survived.
3. Log every step through `DebugLog` exactly as tonight's session did — this is
   the only visibility available.
**Falsification:** `settings.json`'s on-flash bytes must change to reflect the
new value (confirmed by re-mounting and reading the file), and must still read
correctly after a full power cycle. An in-RAM-only change confirmed by app
readback is not sufficient — that's what already exists tonight and isn't the
open question.

### Phase E — Generalize into the actual deliverable

Once one field's full read → live-write → persist → reboot-survive cycle is
proven, generalize `LiveSettings.cpp` (or replace it) into a small reusable API:
read/write a byte, a bool, a small int, at a given validated offset from the
settings struct base, plus the persist call — so that adding support for another
field (say, `units`, or something in `local_settings.json` if Section 5's
"same class" question resolves favorably) is a matter of deriving and
cross-validating one more offset, not repeating this whole investigation.

---

## 9. Deliverables

1. **Updated confidence ledger** in this folder (extend this doc or add a
   `FINDINGS.md`) — every claim in Sections 4–5 either promoted to confirmed
   (with the live cross-check that did it) or corrected, plus new entries for
   whatever Phases A–C turn up, tagged CONFIRMED / LIKELY / UNVERIFIED /
   REFUTED with method, matching the style already used in `una-sdk`'s
   `research`-branch investigations.
2. **The real call chain**, documented the same precise way Section 4 documents
   the loader/save functions — addresses, register roles, what falsifies each
   claim.
3. **A generalized `LiveSettings`-style module** implementing Phase E, with the
   same read-verify-write-verify discipline as tonight's version, plus a
   real reboot-survival test result (not just an in-app one).
4. **An honest statement of what's still not possible**, if anything remains
   out of reach after this session — e.g. if Phase C's real function turns out
   to require state only its normal caller can safely provide, say so plainly
   rather than shipping a call that merely "seems to work" once.
