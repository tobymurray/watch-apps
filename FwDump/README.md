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

## Why this is possible

A `.uapp` is an ordinary binary the kernel loads into RAM and runs, and on this
hardware it runs unrestricted. Three registers say so, and all three were
confirmed 0 across several independent sessions on this unit:

| Register | Value | Meaning |
| --- | --- | --- |
| `CONTROL.nPRIV` | 0 | the app thread runs **privileged** |
| `MPU_CTRL.ENABLE` | 0 | the MPU is switched **off**, all 8 regions unprogrammed |
| `FLASH_OPTR.TZEN` | 0 | no TrustZone secure/non-secure split (corroborated by a live `SAU_CTRL` of 0) |

So a running app can read kernel flash, the ARM System Control Space and
peripheral registers without faulting. The MCU is an **STM32U5A5** (Cortex-M33
r0p4, `DBGMCU_IDCODE 0x30036481`) with 4 MB of internal flash at `0x08000000`.

The app re-reads those three registers at every start, logs them decoded, and
writes them to `dump_context.txt` (`DeviceContext.hpp`). That costs three loads
and is what makes a successful dump mean something: if a future firmware turned
the MPU on, the reads this app makes would fault, and you would want to know that
rather than wonder. It is a sanity check and not a guard — it cannot prevent a
fault, only put the evidence on the record. On a simulator build it honestly
reports `measured=N` rather than presenting its zero-initialised fields as
findings, since zero is the permissive value for every one of them.

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
4. **Now plug in USB** and copy `Apps/FwDump/` to the host.
5. **Verify on the host** with `reassemble_dump.py` — see
   [verifying a dump](#verifying-a-dump).

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

Five states, each visibly distinct, none of which should be mistakable for a
crash — because the one confusion that matters here is "finished" versus
"stalled at 31/32", where one means plug in and the other does not.

| State | What it says |
| --- | --- |
| **Idle** | `READY` (or `RESUME` with `12 of 32 already done`), the region size, and *"Unplug USB, press play. Do not reconnect until DONE."* |
| **Checking** | `CHECKING`, with `N of 32 found`, so the resume scan cannot look like a stall |
| **Dumping** | `07/32`, a progress bar, `1.2 / 4.0 MB 320 KB/s`, `ETA 2m10s` |
| **Done** | `DONE 32/32`, the `whole_image_crc32`, and *"Plug in USB and copy Apps/FwDump/"* |
| **Error** | what failed, which chunk, and how many chunks were kept — pressing play retries from them |

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

Three kinds of file end up in `Apps/FwDump/`:

| File | What it is |
| --- | --- |
| `dump_000000.bin` … `dump_3E0000.bin` | The region, in 32 chunks of 128 KB. |
| `dump_manifest.txt` | Per-chunk CRC-32s, the whole-image CRC-32, and spot reads. Makes the chunks verifiable. |
| `dump_context.txt` | Everything the flash image **cannot say about itself**. |

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
- **The kernel's own version string**, asked for over the message queue
  (`REQUEST_SYSTEM_INFO`) rather than read from a register — the one statement of
  what firmware this is that does not need `strings` run over the image
  afterwards. Bounded by a 250 ms timeout, so a kernel that does not answer
  records `firmware=unavailable` instead of stalling startup.
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
The line that would matter most is `CTX isolation … mpu_enable=`: if a future
firmware turns the MPU on, this app stops working and the reason will be right
there.

## Verifying a dump

The host side already exists and is **not** vendored here. It lives on the
`una-sdk` repo, `research` branch:

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

Also: no network, no BLE, no external side effects. `IsolationCheck` reads six
registers and writes none — note that the prior investigation's sweeps *did*
write `MPU_RNR` to walk the MPU region table, and this app deliberately does not.

One honest limit: **a read that faults is not recoverable in-app.** Internal
flash is memory-mapped, so a "read" here is a pointer dereference — there is no
read call that can fail or come up short, which is why there is no
faulted-read error state. An address that does not decode raises a BusFault
which, with no handler of ours, escalates to a HardFault and takes the app down.
The symptom is the app dying mid-dump, and the manifest's last complete line is
what tells you where. For the default flash region this is moot; it is the reason
to be careful with a non-default region.

## Dumping something else (optional)

Drop a `fwdump.json` into the app's folder over USB to point it at a different
window. It is optional in the strongest sense: every failure — absent, oversized,
unparseable, unknown schema, bad field, incoherent geometry — falls back to the
built-in flash region rather than refusing to run, and the screen reports which.

```json
{
  "schema": 1,
  "base": "20000000",
  "size": "00040000",
  "chunk": "00010000",
  "subwrite": "00001000"
}
```

Addresses are bare hex strings without `0x`, matching the manifest's own
notation — JSON has no hex literal, and writing `0x08000000` as a decimal number
is how you end up dumping the wrong region. Omitted fields keep their defaults.
Geometry must tile exactly: `size` a whole number of chunks, `chunk` a whole
number of sub-writes, `subwrite` no larger than 16 KB.

**A configured region is read exactly as flash is — read-only.** What changes is
the chance that an address does not decode, which faults rather than returning an
error. The default is a region already known to be readable.

### Where a non-default region writes

The default flash region writes flat into `Apps/FwDump/`, with exactly the names
the host reassembler expects. **Any other region gets its own
`region_<base>/` subdirectory** holding the same names —
`Apps/FwDump/region_20000000/dump_000000.bin` and so on.

That is not tidiness. Chunk filenames are derived from the offset *within* the
region and the manifest name is fixed, so without the split an SRAM dump at
`0x20000000` would write the very same `dump_000000.bin` as a flash dump and
destroy it — and resume would then re-verify the survivors against the wrong
memory. Keeping the names identical inside the subdirectory means
`reassemble_dump.py` needs no changes: it takes a directory, so point it at the
subdirectory instead.

So SRAM, the ST ROM bootloader at `0x0BF90000` and the system-information area
are all dumpable today with no code change — only a config file.

## Tests

```sh
export UNA_SDK=/path/to/una-sdk-apps-v1.3.0
cd FwDump/Tests
cmake -B build -G "Unix Makefiles" . && cmake --build build
./build/fwdump-dumper-tests
```

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
  falls back to the flash default without half-applying.

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

**`$UNA_SDK` must point at an `apps-v1.3.0` checkout, not at mainline.** The
kernel interface version is baked into the app: `apps-v1.3.0` is
`KERNEL_INTERFACE_VERSION 2`, mainline is `3`, and the watch runs the 1.3 line. An
app built against `3` exits instantly to an `App PID` error screen on a v2
kernel, and nothing catches the mistake at build time. This is the same pinning
[Chrono](../Chrono/README.md#why-13-matters) and
[Map Manager](../MapManager/README.md#why-its-pinned-to-sdk-13) describe.

`AppID` is `5D041A7EB1D16CAA` =
`sha256("https://github.com/tobymurray/watch-apps#firmwaredump")[0:8]`, following
the repo convention. Note the anchor is `firmwaredump`, the app's purpose, not
`fwdump`, its folder — recompute it before changing either.

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
phone-sync first — see the warnings above.

## Credit

The technique, the register sweep, the manifest format and the chunked-dump
routine this app is a port of are all from the
`2026-07-29-hardware-config-recovery` investigation on `una-sdk@research`.
`reassemble_dump.py` is that investigation's work and is deliberately referenced
rather than copied, so there is one copy of it to keep correct.
