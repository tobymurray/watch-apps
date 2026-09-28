# FW Dump — an on-device flash reader

A `Utility` app that reads the watch's own internal flash straight out of memory
and writes it, in verifiable chunks, into its own folder — where you read it back
over USB. It is the productised form of a one-off `peekDumpFlash()` that was
bolted onto the SDK's `HelloWorld` tutorial service to recover this watch's
hardware configuration; the difference is that this one yields instead of
blocking, resumes instead of restarting, and shows you what it is doing instead
of only logging it.

Default region is the whole 4 MB of internal flash at `0x08000000`, in 32 chunk
files of 128 KB, with a manifest carrying a CRC-32 per chunk and one for the
whole image.

**It is read-only.** See [the read-only guarantee](#the-read-only-guarantee).

This is a tool for reading your own hardware. It needs no SWD probe and no
decryption key: the release firmware is AES-encrypted in transit but sits in
plaintext in flash at rest, and a UNA app runs with no memory isolation, so an
app can simply read it.

It is built to be installed from a store by someone who has never seen this
repository. The folder it writes explains itself (a `README.txt` beside the
dump), a copy can be checked with stock Python, and a firmware that isolates
apps gets a refusal instead of a crash. Which firmware versions it has been
shown to work on is in [Firmware support](#firmware-support).

## Why this is possible

A `.uapp` is an ordinary binary the kernel loads into RAM and runs, and on this
hardware it runs unrestricted. Three registers say so. All three read 0 across
several independent sessions of the 2026-07-29 investigation, on firmware
1.3.0:

| Register | Value | Meaning |
| --- | --- | --- |
| `CONTROL.nPRIV` | 0 | the app thread runs **privileged** |
| `MPU_CTRL.ENABLE` | 0 | the MPU is switched **off**, all 8 regions unprogrammed |
| `FLASH_OPTR.TZEN` | 0 | no TrustZone secure/non-secure split (corroborated by a live `SAU_CTRL` of 0) |

So a running app can read kernel flash, the ARM System Control Space and
peripheral registers without faulting. The MCU is an **STM32U5A5** (Cortex-M33
r0p4, `DBGMCU_IDCODE 0x30036481`) with 4 MB of internal flash at `0x08000000`.

### The gate: refuse instead of crashing

A read here is a pointer dereference, and one the hardware refuses raises a
fault this app has no handler for. So if a future firmware turned the MPU on,
1.0.0 would have died on its first flash read, and the only evidence would have
been a manifest that stops early. To a store user, that looks like a broken app.

The app now answers the question before the first read of memory. The three
registers are read in an order where each read happens only if the one before
it says the read cannot fault (`ReadGate.hpp`):

1. **`CONTROL`**, by `MRS`, which cannot fault. If `nPRIV` is 1 the thread is
   unprivileged, and any access to the System Control Space would fault, so
   nothing further is read.
2. **The System Control Space**, which the MPU never governs. If
   `MPU_CTRL.ENABLE` is 1, which regions the MPU allows is in its region table,
   and reading that table needs a write to `MPU_RNR`. So the app stops here, and
   the UID, the option bytes and the register sweep go unread.
3. **The `FLASH` peripheral**, safe once the MPU is known to be off. If
   `FLASH_OPTR.TZEN` is 1, secure flash would fault a non-secure read.

If any of the three is set, the screen says **BLOCKED** and names the register.
`dump_context.txt` records the verdict (`CTX gate verdict=refused
blocked_by=MPU_CTRL.ENABLE`) and says which fields it read and which it skipped.
No dump is offered.

The rule depends only on those three bits and never on a firmware version, so a
firmware that changes `SPSEL`, `PRIVDEFENA` or an option bit this app does not
depend on is not refused over it. It prevents only the one fault that matters,
isolation, not every fault. [The read-only guarantee](#the-read-only-guarantee)
covers the rest. On a simulator build the file says `measured=N`. It does not
present zero-initialised fields as findings, because zero is the permissive
value for every one of them.

## Firmware support

"Supported" here means **run on a watch with that firmware**, not inferred from
a header.

| Firmware | Shown on hardware | `nPRIV` / `MPU_CTRL.ENABLE` / `TZEN` | Firmware as the bundle reports it | Dump time | Whole-image CRC-32 |
| --- | --- | --- | --- | --- | --- |
| 1.3.0 | **Yes**, by FwDump's first build (`cbcb869`, built against `apps-v1.3.0`, 2026-08-17): 32/32 chunks clean, device and host agree. It is the 1.3.0 half of the two dumps `555bca3` compares | 0 / 0 / 0. `CONTROL=0x00000006`, `MPU_CTRL=0x00000000`, from the 2026-07-29 investigation's sweep #3 on this unit | Not recorded: that build did not ask | Not recorded | `658D9BB2` |
| 1.4.0 | **Yes**, by FwDump on 2026-08-18: all 32 chunks match the manifest. The image is kept on `una-sdk@research` under `firmware-dumps/1.4.0/` | Not kept. Its `dump_context.txt` was not preserved. The whole of flash read without a fault, which is consistent with all three being 0 but does not show it | `REQUEST_SYSTEM_INFO` is answered `FAIL` by kernel 1.4.0 (see [NotifyToggle](../NotifyToggle/README.md)), so it said `unavailable`. The image scan this build adds reads `1.4.0` from that same image | Not recorded | `14009D03` |
| 1.5.0 | **Yes**, by this build (1.1.0, AppID `78C1174ADA9C5EBD`) on 2026-09-28: gate `allowed`, 32/32 chunks clean by both the `README.txt` check and `reassemble_dump.py`, all three spot lines match | 0 / 0 / 0. `CONTROL=0x00000006`, `MPU_CTRL=0x00000000`, `FLASH_OPTR=0x1FEFF8AA` | Kernel still gives no answer to `REQUEST_SYSTEM_INFO` (`unavailable`). `CTX kernel interface=3`. The image scan reads `1.5.0` at `0817AB87`, and so does a host scan of the reassembled image | Under 10 s, play to DONE, timed by eye by the wearer: over 400 KB/s | `CC925D97` |

Two things this table does **not** show:

- **This build has run on 1.5.0 only.** Its 1.3.0 and 1.4.0 rows are from
  1.0.0, whose dump engine (`FlashDumper`, `DumpManifest`, `Crc32`) this build
  reuses unchanged, built against the same `apps-v1.3.0`. The gate, the image
  scan, the settings envelope and `README.txt` have run on a watch only on
  1.5.0.
- **The image scan has held across two versions, not every version.** In 1.4.0
  and 1.5.0 the kernel string sits between the same neighbours (`%06lu` before,
  `virtual bool Driver::Uart::transmit…` after), and the bootloader's `0.1.4`
  is at `08019194` in both. The scan took 333 ms of startup on 1.5.0
  (`scan_ms`).

**What would add or complete a row:** open the app once on that firmware (with
USB out), let a dump finish, and keep `dump_context.txt`, the DONE screen's CRC
and how long the dump took. The gate line, the `CTX isolation` lines and the
`CTX image … firmware=` line fill in every column but the time. The manifest
format is fixed, so the time has to be noted by hand, or read off a UART
capture.

`minKernelVersion` in `app-manifest.json` is `1.3.0`: the oldest firmware
this app has run on, not the interface-2 floor of `1.0.0`. It is a floor with no
ceiling, so it cannot say which later versions are unproven. This table has to
say that.

## The two-phase workflow

**The dump cannot run while USB is connected.** Plugging in puts the watch into
charge/mass-storage mode and the kernel stops every running app. So:

1. **Unplug USB.** Run on battery, or on the dev tool, which supplies power
   without acting as a USB host.
2. **Launch FW Dump and press the play button (R1).** It runs unattended.
   The screen shows `chunk 07/32`, a progress bar, MB done and an ETA computed
   from the rate actually observed. Leaving the screen does not stop the dump;
   the service keeps going with the display blanked.
3. **Wait for `DONE`.** The screen shows `DONE 32/32` and the
   `whole_image_crc32`. That value is what you eyeball against the host's.
4. **Turn phone sync off, then plug in USB** and copy `Apps/FwDump/` to the
   host. See the [USB-MSC warnings](#usb-msc-warnings-both-learned-the-hard-way).
5. **Verify on the host** with the check `README.txt` gives, or with
   `reassemble_dump.py`. See [verifying a dump](#verifying-a-dump).

Do not connect USB before it says `DONE`. If you do, the dump stops where it is;
nothing is corrupted and nothing is lost, but you will need to relaunch and let
it finish. Which is fine, because:

### It resumes

A dump that has to fit into "however long the watch is off the cable" has to
survive being interrupted, so every chunk file is flushed and closed before the
next is opened, and the manifest is rewritten after every chunk. Relaunching
lands on `RESUME`, showing `12 of 32 already done`, and pressing play finishes
the rest rather than redoing the lot.

Resume does not simply trust a file that is the right size. Every chunk is
always re-read from memory and hashed — it has to be, because the whole-image
CRC is a single running value chained across all 32 chunks in order — and a
chunk whose file is already on disk is read back and hashed alongside it. The
write is skipped only when the two agree, which is the only thing that makes
skipping it honest. A chunk that disagrees, or that turns out to be unreadable,
is rewritten from memory.

One consequence worth knowing: a resumed run produces a *complete* manifest, not
one patched together from a previous run's lines. Nothing carries between runs
except the chunk files themselves.

## The screen

Six states, each visibly distinct, none of which should be mistakable for a
crash — because the one confusion that matters here is "finished" versus
"stalled at 31/32", where one means plug in and the other does not.

| State | What it says |
| --- | --- |
| **Idle** | `READY` (or `RESUME` with `12 of 32 already done`), the region size, a config status if `fwdump.json` was applied or ignored, and *"USB out, then play / no USB until DONE"* |
| **Checking** | `CHECKING`, with `N of 32 found`, so the resume scan cannot look like a stall |
| **Dumping** | `07/32`, a progress bar, `1.2 / 4.0 MB 320 KB/s`, `ETA 2m10s` |
| **Done** | `DONE 32/32`, the `whole_image_crc32`, and *"phone sync off, copy / Apps/FwDump/"* |
| **Error** | what failed, which chunk, and how many chunks were kept — pressing play retries from them |
| **Blocked** | `BLOCKED`, which isolation register refused the read, and *"nothing read; see dump_context.txt"*. No play button |

Every line was measured in Poppins Medium 16 against its box rather than
eyeballed. 1.0.0's *"Unplug USB, then play"* was 176 px in a 172 px box and
rendered clipped. Simulator screenshots of READY, DONE, BLOCKED, a refused SRAM
config and an applied flash window show every line whole. The watch clips the
square simulator frame to a circle, and the boxes are sized to that circle's
chord at each row.

Buttons: **R1** starts, **R2** leaves. A second press while a dump runs is a
no-op, not a restart.

There is deliberately no "paused" state. While the kernel has the app stopped —
which is exactly what USB does — nothing runs and nothing is drawn, so a paused
app cannot tell you it is paused. What the app can do is notice, afterwards, that
it lost wall-clock time mid-dump, and the `Dumping` screen then says *"Paused
earlier - USB in? Keep it out."* That is retrospective by nature and is a
heuristic on a time gap, not a reading of USB state: the SDK exposes no USB
connection status to an app.

The same milestones go to `LOG_INFO` — state transitions, per-chunk completion,
the final CRC — so a dev-tool UART capture (921600 8N1) corroborates the display
and gives a record when the screen is off.

## What the bundle contains, and why `dump_context.txt` exists

Four kinds of file end up in `Apps/FwDump/`:

| File | What it is |
| --- | --- |
| `dump_000000.bin` … `dump_3E0000.bin` | The region, in 32 chunks of 128 KB. |
| `dump_manifest.txt` | Per-chunk CRC-32s, the whole-image CRC-32, and spot reads. Makes the chunks verifiable. |
| `dump_context.txt` | Everything the flash image **cannot say about itself**. |
| `README.txt` | For whoever opens the folder: what each file is, which line identifies the watch, the USB warnings, and a stock-Python check. Rewritten on every launch. |

`dump_context.txt` contains the watch's **96-bit UID**, the one thing in the
bundle that identifies the unit, on the line starting `CTX uid=`. `README.txt`
tells the user to delete that line before sharing the file. The image itself is
the vendor's firmware. The README says so and gives no legal advice about it.

That last one earns its place. A flash image is remarkably self-describing —
`strings` on it recovers the kernel and bootloader version numbers, the build
paths they were compiled from, and the class name of every chip driver the
firmware contains. What it cannot contain is anything that lives in a **register**
rather than in `0x08000000`–`0x08400000`:

- **Which unit it came off** — the 96-bit UID. Two watches on identical firmware
  produce identical images.
- **Which die** — `DBGMCU_IDCODE`, so DEV_ID and silicon revision.
- **Whether the read was legitimate** — `CONTROL.nPRIV`, `MPU_CTRL.ENABLE`,
  `FLASH_OPTR.TZEN`. An image taken while isolation was active would be suspect,
  and there is no way to tell afterwards.
- **Where the kernel actually starts** — `SCB VTOR`, measured rather than
  inferred from the image's structure.
- **The option bytes** — RDP level, TrustZone, dual-bank, boot addresses. Option
  bytes are a *separate flash area* and are not inside the dumped region.
- **Which firmware it is**, three ways, because no single one works everywhere:
  - `CTX kernel firmware=`: the kernel's own answer to `REQUEST_SYSTEM_INFO`,
    bounded by a 250 ms timeout. Kernel 1.4.0 answers `FAIL`, so on 1.4 this
    says `unavailable`.
  - `CTX kernel interface=`: `gIKernel->version`, the interface version the
    running kernel reports. Every firmware answers it, but it only tells ABIs
    apart, not versions.
  - `CTX image … firmware=`: every whole `N.N.N` string between NUL bytes in
    flash, each with its address, and the one above the kernel's vector table
    (`VTOR`) named as the firmware. In the 1.4.0 image there are exactly two:
    `0.1.4` at `08019194` in the bootloader and `1.4.0` at `08168255` in the
    kernel. The scanner reports `1.4.0` when run over that real image. On a
    1.5.0 watch it reported `1.5.0` at `0817AB87`, with the same two
    neighbours, in `scan_ms=333`. A version whose layout differs would show
    it rather than be summarised wrongly, because every match is listed.
    (`FirmwareStrings.hpp`)
- **A raw sweep** of `SCB`, `NVIC_ISER`, `NVIC_IPR`, `RCC`, `GPIOA`–`GPIOH`,
  `I2C1`–`I2C6`, `SPI1`/`SPI3`, `USART3` and `LPUART1` — which clocks and
  peripherals are enabled, the pin-mux, which interrupts are on, and the bus
  speeds (`TIMINGR`) and baud rates (`BRR`). Every base was read successfully on
  this unit by the prior investigation.

It is written **at app start, before any dump**, and rewritten on every launch.
So merely opening the app captures the hardware context, which makes it cheap to
record the state of a firmware version you are about to replace. It is also why
it is a file and not only a log line: the log needs a UART capture to be seen at
all, and the first real dump taken with this app lost its register context
exactly that way.

The sweep is written in the same address-labelled form the prior investigation
used (`SWP RCC 46020C00: xxxxxxxx …`), so two versions' files diff line-for-line
with `diff`, and decode with that investigation's existing Python.

Everything in there is a read. Note that it deliberately does **not** walk the
MPU region table: that needs a write to `MPU_RNR` to select each region, which
the prior investigation's sweeps did and this app will not. `MPU_TYPE` and
`MPU_CTRL` answer the question that matters — is it on — without writing.

### Comparing two firmware versions

The register state of a firmware version is unrecoverable the moment it is
replaced, so capture it **before** updating. Diffing two flash images tells you
the code changed; it does not tell you the new firmware enabled a peripheral,
remapped a pin, turned on an interrupt, or switched the MPU on.

To capture a version, in full:

1. Launch the app once with USB out — that alone writes `dump_context.txt`.
2. Press play and let the dump finish, for the image itself.
3. Copy `Apps/FwDump/` off, plus `Apps/app_list.json` for the app inventory.

Then, after updating, do the same and `diff` the two `dump_context.txt` files.
The line that would matter most is `CTX gate verdict=`: if a future firmware
turns the MPU on, it says `refused blocked_by=MPU_CTRL.ENABLE`, and the app
reads nothing more.

The `CTX` lines changed shape after 1.0.0. Isolation is now two lines, not one,
and the UID line carries `flash_kb`. So a 1.0.0 file and a later one differ on
those lines even when the registers do not. The `SWP` sweep lines are
unchanged.

## Verifying a dump

There are two ways, and they agree.

**With stock Python, from the `README.txt` in the folder.** This is the one for
someone who installed the app from a store. It needs Python 3 and nothing else.
It checks every chunk's length and CRC-32 against the manifest, chains them
into the whole-image CRC, and compares that with the manifest's:

```
ok 32/32 whole_image_crc32 FA842BEC matches
```

That CRC is the number the DONE screen shows. The command is tested as written:
a host test (`check_readme_verifier.py`) takes it out of the `README.txt` the
synthetic exporter writes and runs it. It must print `ok 32/32 … matches` on the
intact bundle and `ok 31/32 … DIFFERS` with one byte flipped.

**With `reassemble_dump.py`**, which also rebuilds the single image and checks
the spot bytes. It is **not** vendored here, so there stays one copy of it to
keep correct. It lives on the `una-sdk` repo, `research` branch:

```
Docs/Investigations/2026-07-29-hardware-config-recovery/
├── REPRODUCTION-GUIDE.md                     the original technique
├── README.md                                 the verification ledger
├── reassemble_dump.py                         the host reassembler + verifier
└── service-cpp-instrumentation-sweep7.cpp     the routine this app is a port of
```

`reassemble_dump.py` is the authority on the manifest format at the host end,
and this app's `DumpManifest` is written to satisfy it byte for byte. To verify:

```sh
# after ejecting and remounting -- see the warnings below
python3 reassemble_dump.py /path/to/copied/Apps/FwDump -o flash_dump.bin
```

Success is all three of: every chunk verified clean, `whole-image CRC32
device == host`, and the spot bytes matching.

```
base=0x08000000 size=0x400000 chunk=0x20000 nchunks=32
manifest describes 32/32 chunks
whole-image CRC32: device=BCD2F8E0 host=BCD2F8E0 [MATCH]
spot addr=0x08000000 manifest=00001F20... reassembled=00001F20... [MATCH]

All 32 chunks verified clean.
```

A *different* whole-image CRC from a previous run is not necessarily wrong — the
firmware version differs. A device-versus-host **mismatch** is.

An interrupted dump is still useful: the reassembler reports
`** INCOMPLETE DUMP: only 20/32 chunks reached the manifest`, zero-fills the
rest, and verifies what it does have.

### USB-MSC warnings, both learned the hard way

- **Turn off BLE phone-sync before any USB-MSC session.** Concurrent watch BLE
  sync and host USB writes to the same exFAT partition corrupt files:
  byte-identical from the page cache, divergent after remount, then
  `Input/output error`.
- **Eject and remount before checksumming.** `sha256sum` straight after `cp`
  reads the page cache and will happily "verify" a copy that never hit the
  medium.

## The manifest format

Fixed by the host reassembler, not chosen here. `DumpManifest.hpp` is the
normative comment; the shape is:

```
DUMP base=08000000 size=00400000 chunk=00020000 subwrite=00001000 nchunks=32
DUMP chunk=0/32 off=00000000 size=00020000 crc32=DB0F4C08 bw=131072 ok=Y
...
DUMP whole_image_crc32=FA842BEC
DUMP spot addr=08000000 bytes=EF3FCE48585066C346F2C6BC53F7091F
```

- `off` is the chunk's offset **from `base`**, not an absolute address. The
  reassembler derives the chunk filename from it as `dump_%06X.bin`, so the two
  must agree.
- `subwrite` is the block size the app reads, hashes and writes in. Recorded for
  the record; the host's arithmetic does not depend on it.
- The CRCs are **CRC-32/ISO-HDLC** — zlib's `crc32`, polynomial `0xEDB88320`,
  init and xorout `0xFFFFFFFF`. There is no zlib on the device, so `Crc32.cpp`
  implements it, and getting that byte-compatible is the single fiddliest
  correctness point in the app: an incompatible CRC does not produce a wrong
  number, it condemns every honest dump the app will ever take. Hence the host
  tests below.
- The manifest is held in RAM and the whole file rewritten after every chunk, so
  what is on disk is always a whole number of lines. A half-written line is worse
  than a missing one, because a regex can match the wrong half.

Nothing about the contract needed adjusting — the manifest this app writes is
parsed by the unmodified `reassemble_dump.py`, verified end to end (see below).

## The read-only guarantee

The app issues memory **reads** and filesystem **writes into its own sandbox**,
and nothing else. It contains no code path that writes to any address in flash,
the option-byte space, or any peripheral register.

That matters because the one irreversible risk on this chip is writing option
bytes and raising `RDP`. This unit reads `RDP = 0xAA` — level 0, and in fact
byte-for-byte the ST factory production value, so its security configuration is
untouched rather than deliberately opened. That means the chip stays fully
recoverable over SWD. **This app does not rely on that**, because it never writes
anything.

Also: no network, no BLE, no external side effects. The isolation gate reads
registers and writes none. The prior investigation's sweeps *did* write
`MPU_RNR` to walk the MPU region table, and this app deliberately does not.

One honest limit: **a read that faults is not recoverable in-app.** Internal
flash is memory-mapped, so a "read" here is a pointer dereference — there is no
read call that can fail or come up short, which is why there is no
faulted-read error state. An address that does not decode raises a BusFault
which, with no handler of ours, escalates to a HardFault and takes the app down.
Two things keep the app away from that: [the gate](#the-gate-refuse-instead-of-crashing)
refuses when isolation is on, and a configured region must lie inside internal
flash, the one range already read whole without a fault.

## Dumping part of flash (optional)

`app-manifest.json` declares four optional settings: `base`, `size`, `chunk` and
`subwrite`. Kira and the phone offer them as a form and write the answers to
`Apps/FwDump/fwdump.json` in the envelope the SDK's `Docs/app-config-fields.md`
specifies:

```json
{
  "schema": 1,
  "values": { "base": "08060000", "size": "00100000" }
}
```

`fwdump.json` is read by the app's own `DumpConfig`, not by `SDK::AppConfig`.
AppConfig first appears in `sdk-v1.4.0`, and using it would cost firmware 1.3.
A file with no `values` object is read with the same keys at the top level,
which is the form 1.0.0 documented, so a hand-written 1.0.0 file still works.

It is optional in the strongest sense. Every failure falls back to the built-in
flash region rather than refusing to run, and the Idle screen says which: a
missing, oversized or unparseable file, an unknown schema, a bad field, geometry
that does not tile, or a region outside flash. A file that asks for exactly the
default reports as the default, not as `config applied`. A companion writes the
envelope even when every answer was left alone.

Addresses are bare hex strings without `0x`, matching the manifest's own
notation — JSON has no hex literal, and writing `0x08000000` as a decimal number
is how you end up dumping the wrong region. The form's `pattern` rejects a
malformed value before it reaches the watch, and the app rejects it again if it
does. Geometry must tile exactly: `size` a whole number of chunks, `chunk` a
whole number of sub-writes, `subwrite` no larger than 16 KB.

**A configured region must lie inside internal flash**, `08000000` to
`083FFFFF` (`DumpRegion::knownReadable`). Outside it, the region falls back to the
default with the status `config not in flash`. The rule follows what has actually
been read without a fault. That is the whole of flash, on two firmware versions,
and nothing else on record. SRAM was dumped once at `0x20000000` (the commit that
added region directories says so), but its size was never recorded, so it is
not evidence for any range. A range joins the list when a clean manifest of it
from a watch does.

### Where a non-default region writes

The default flash region writes flat into `Apps/FwDump/`, with exactly the names
the host reassembler expects. **Any other region gets its own
`region_<base>/` subdirectory** holding the same names —
`Apps/FwDump/region_08060000/dump_000000.bin` and so on.

That is not tidiness. Chunk filenames are derived from the offset *within* the
region and the manifest name is fixed, so without the split a dump of the kernel
at `0x08060000` would write the very same `dump_000000.bin` as a whole-flash
dump and destroy it — and resume would then re-verify the survivors against the
wrong memory. Keeping the names identical inside the subdirectory means
`reassemble_dump.py` and the `README.txt` check need no changes: run either in
the subdirectory instead.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0
cd FwDump/Tests
cmake -B build -G "Unix Makefiles" . && cmake --build build
ctest --test-dir build --output-on-failure
```

CI runs the same thing on every change under `FwDump/`, against `apps-v1.3.0`
(`.github/workflows/fwdump.yml`).

`FlashDumper` reads its region through a window pointer rather than a hardcoded
address, which is what makes any of this testable: in the tests a `std::vector`
stands in for flash, and every other line is the code that runs on the watch.
Covered:

- **`Crc32_test.cpp`** — the CRC against hardcoded `zlib.crc32` output, including
  the published `0xCBF43926` check value, an erased-flash `0xFF` chunk (about half
  the real region), and that hashing in blocks of 1/3/64/512 bytes gives the same
  answer as one call. The expectations are literal zlib values, not recomputed
  with the same table, so a wrong polynomial cannot pass.
- **`DumpManifest_test.cpp`** — the exact bytes of each line against the host
  regexes, and that an overflowing manifest reports it rather than silently
  losing its tail.
- **`FlashDumper_test.cpp`** — a full pass; the manifest gaining a line per chunk
  with the header on disk first; resume skipping absent, mismatching and
  wrong-sized chunks correctly; the whole-image CRC being independent of which
  chunks were rewritten; a short write reported rather than claimed as success;
  every handle closed and every chunk flushed.
- **`DumpConfig_test.cpp`** — every way a config file can be wrong, and that each
  falls back to the flash default without half-applying. Also the companion's
  `values` envelope, a region outside flash, and a file that restates the
  default.
- **`ReadGate_test.cpp`** — each isolation bit refusing on its own, the first one
  in read order being the one named, 1.3.0's measured registers being allowed,
  and bits that do not decide whether a read faults being ignored.
- **`FirmwareStrings_test.cpp`** — the two version strings at the addresses and
  with the neighbouring bytes the 1.4.0 image has, the kernel's being the one
  above `VTOR`, no answer when that is ambiguous or matches were dropped, and
  near-misses (`1.4`, `v1.4.0`, `1.4.0.2`) ignored.
- **`check_readme_verifier.py`** (a CTest, not a gtest) — the `README.txt`
  check, run exactly as written, accepting an intact synthetic bundle and
  rejecting one with a flipped byte.

Unlike MapManager's suite, none of this needs the SDK's `InMemoryDirectory`: the
resume scan probes the 32 chunk filenames it already knows rather than
enumerating a directory. That is also more robust on hardware, and it is a
deliberate difference from the "scan the folder" approach the brief suggested.

`Service`'s own run loop is not host-tested — it blocks on the kernel message
queue and never returns.

### Verifying the contract against the real script

The tests above check the manifest against *this repo's reading* of the host
regexes. `fwdump-export-synthetic` checks it against the script:

```sh
./build/fwdump-export-synthetic /tmp/synth
python3 /path/to/reassemble_dump.py /tmp/synth -o /tmp/synth.bin
```

It runs the real `FlashDumper` over a synthetic 4 MB region shaped like this
watch's flash — pseudo-random for the first 2.04 MB, `0xFF` beyond, which is what
the verified prior dump found — at the real geometry, and writes real files. The
unmodified `reassemble_dump.py` then verifies all 32 chunks, matches the
whole-image CRC and matches all three spot lines.

## Building

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0    # not mainline; see below
cd FwDump/Software/Apps/FwDump-CMake
cmake -B build -G "Unix Makefiles" -DBUILD_VERSION=1.0.0 . && cmake --build build
```

**`$UNA_SDK` must point at an `apps-v1.3.0` checkout**, which is upstream
commit `7a556a3`. The kernel interface version is baked into the app:
`apps-v1.3.0` is `KERNEL_INTERFACE_VERSION 2`, while `apps-v1.4.0`, `apps-v1.5.0`
and mainline are `3`. The launch check refuses only a kernel older than the app,
so an interface-2 build is the one that can start on firmware 1.3 as well as
the later lines. An app built against `3` exits instantly to an `App PID` error
screen on a 1.3 kernel, and nothing catches the mistake at build time.

CI builds it that way. `fwdump.yml` passes `sdk_ref: 7a556a3…` to
`app-build.yml`, which builds and tests against that checkout. It still runs
`validate_app_config.py` and `min_kernel_version.py` from its pinned `SDK_REF`,
because `apps-v1.3.0` has neither. The floor check reads the build SDK's headers,
so `minKernelVersion: 1.3.0` passes against interface 2. It would fail against
`SDK_REF`'s interface 3 (`'1.3.0' is below the ABI 3 floor '1.4.0'`).

The cost of 1.3 is `SDK::AppConfig`, which this app cannot use; see
[Dumping part of flash](#dumping-part-of-flash-optional). Nothing else this app
needs is missing from 1.3.

| Build, against `apps-v1.3.0` | `.uapp` bytes | GUI `.text` | Service `.text` |
| --- | --- | --- | --- |
| 1.0.0 source (`555bca3`) | 147,348 | 120,380 | 16,744 |
| This branch | 101,016 | 69,628 | 21,264 |

The GUI shrank because it no longer carries the stopwatch's SemiBold 20/40/60
fonts, which were 50,359 bytes of the ELF by `nm` to draw nothing. The Service
grew by the gate, the image scan, `README.txt` and the envelope reader. Both
from real builds in the pinned toolchain image (`cca44e2ca090`).

`AppID` is `78C1174ADA9C5EBD`, the id this app is registered under. It replaced
`5D041A7EB1D16CAA`, a hash of a URL that nothing had registered, and which 1.0.0
shipped with. The AppID is the whole of an app's identity on the watch, the
phone and in Kira, so 1.0.0 and everything after it are **two different apps**
that happen to share the `Apps/FwDump/` folder. Delete the old `.uapp` from that
folder when installing the new one: two `.uapp`s in one folder is a coin toss
over which one the kernel loads (see [Installing](../Docs/INSTALLING.md)). The
build prints the AppID it packed (`INFO:root:ID : 78C1174ADA9C5EBD` in the
`app_merging.py` output), so check it there rather than assume it.

`apps-v1.3.0` passes `-fcyclomatic-complexity`, which only ST's CubeIDE GCC
accepts; mainline `arm-none-eabi-gcc` rejects it outright. The `CMakeLists.txt`
probes for it and drops it when unsupported, the same guard Map Manager carries.

### Simulator

```sh
cd FwDump/Software/Apps/TouchGFX-GUI
make -f simulator/gcc/Makefile
./build/bin/simulator.out
```

The simulator has no STM32 flash to read, so on that build the service fills a
4 MB buffer with a deterministic pattern and dumps *that* — it says so, loudly,
in the log. A `DONE` and a matching CRC there prove the chunk/manifest/CRC/resume
machinery works and prove nothing whatever about the watch's flash. Its mock
filesystem also accepts paths the real device rejects, so the sandbox-path
behaviour is not tested there either.

One trap worth recording, because it costs a segfault with no output to explain
it: **the service must not log anything before the TouchGFX HAL exists.**
`simulator/main.cpp` constructs `Service` before it calls `setupSimulator`, and
the SDK's mock logger routes `LOG_INFO` through `touchgfx_printf`, which
dereferences the HAL singleton. So `Service::configure()` and all of `DumpConfig`
are deliberately logger-free, and everything is logged from `run()` instead.

## Deploying

```sh
udisksctl mount -b /dev/sda1
MP=$(findmnt -n -o TARGET /dev/sda1)
mkdir -p "$MP/Apps/FwDump"
rm -f "$MP/Apps/FwDump/"*.uapp
cp build/FW_Dump_*.uapp "$MP/Apps/FwDump/"
sync
udisksctl unmount -b /dev/sda1
```

Then unplug, power-cycle, and launch. The watch regenerates `Apps/app_list.json`
from the `.uapp` headers on boot, so there is no manifest to edit -- but note that
it regenerates from whatever `.uapp` files it finds, so if a rebuild changes
`APP_USER_NAME` the filename changes with it (`make_file_safe_name`) and the old
file must be **deleted**, not just overwritten. Two `.uapp`s in one folder sharing
an `APP_ID`, or a stale `app_list.json` entry naming a file that is gone, both
need a power-cycle to sort out. Turn off BLE
phone-sync first — see the warnings above. A watch that still has 1.0.0 has a
`.uapp` under the old AppID in this folder, and the `rm` above is what keeps it
from winning. Then check the app actually registered, as
[Installing](../Docs/INSTALLING.md) says, rather than trusting that the file is
in place.

## What was deliberately not done

- **No `SDK::AppConfig`.** It would have given nothing the envelope reader does
  not, and cost firmware 1.3: it first appears in `sdk-v1.4.0`.
- **No SRAM, ROM or peripheral windows.** The 1.0.0 README said they were
  dumpable with a config file. None has a recorded clean read of a known range,
  so none is allowed.
- **No dump timing in the bundle.** It would be the one column the firmware
  table cannot fill from the files, but the manifest's format is fixed by
  `reassemble_dump.py`. A new file just for a number nobody parses was not
  worth it. The Dumping screen shows the rate, and a UART capture has the rest.
- **No vendored `reassemble_dump.py`.** The `README.txt` check covers what a
  store user needs. The script stays on `una-sdk@research`, as one copy.
- **The simulator's Checking and mid-dump screens were not captured.** The
  synthetic dump finishes in under two seconds, before a screenshot lands. The
  code for both is unchanged from 1.0.0.

## Credit

The technique, the register sweep, the manifest format and the chunked-dump
routine this app is a port of are all from the
`2026-07-29-hardware-config-recovery` investigation on `una-sdk@research`.
`reassemble_dump.py` is that investigation's work and is deliberately referenced
rather than copied, so there is one copy of it to keep correct.
