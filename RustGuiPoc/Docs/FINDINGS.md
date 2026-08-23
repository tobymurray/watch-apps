# Findings

What five hardware runs established, kept because the instrumentation that
produced it has been removed from the app. The raw logs are in
[`../Captures/`](../Captures); every number below is traceable to one of them.

Measured on a UNA Watch (STM32U595) against `apps-v1.4.0`.

## The platform

| | |
|---|---|
| Panel | 240×240, 8bpp ABGR2222 — **four levels per channel**, 64 colours |
| Framebuffer | one, 57,600 bytes, app-owned; **whole frames only** (`RequestDisplayUpdate`'s `x/y/width/height` are marked reserved) |
| GUI RAM | 600 KiB, and **code executes from RAM** — framework size is charged against the same budget as the framebuffer |
| Rendering | software only; the port's `STM32DMA` is a stub |
| Input | four buttons, no touch |

A GUI process **cannot reach a sensor**. `SDK::Kernel` is `{sys, log, mem, comm, fs}`;
`SDK::Sensor::Connection` works only from the Service half. Anything sensor-driven
must travel Service → custom message → GUI, which is why the app has two halves.

## The render loop

- **The GUI ticks at 10 fps, not 30.** Frame gaps have a median of 100 ms across
  every run. The SDK's TouchGFX documentation says "typically 30–60 FPS"; that is
  not what the kernel delivers here.
- **Pushing a framebuffer costs 21–24 ms** and always succeeds. `sendMessage` for
  `RequestDisplayUpdate` does not block, so a 1000 ms timeout on it is never
  reached in practice.
- Because the push is cheap and the tick is slow, a frame has ~78 ms of slack.
  Per-pixel work at this resolution fits comfortably.

## The sensor pipeline

This took four runs and a four-cell parameter sweep. The conclusions:

**1. The requested period is not what you get.** A 100 ms request is snapped to
80 ms (12.5 Hz); a 20 ms request is honoured exactly (50 Hz). The ODR ladder has
both steps and a 10 Hz ask lands on the one above. The driver does not report back
what it actually gave — the only way to know is to log its timestamps.

**2. Samples arrive in ~1 s bursts, and nothing in the app can change that.**
Delivery is not paced at the sample rate. A second's worth of samples arrives
within the same millisecond, once a second:

| period / latency | delivered rate | samples per burst | burst interval |
|---|---|---|---|
| 100 ms / 0 | 12.5 Hz | 12–13 | 1004 ms |
| 100 ms / 20 ms | 12.5 Hz | 12–13 | 1004 ms |
| 100 ms / 2000 ms | 12.5 Hz | 20–30 | 2006 ms |
| 20 ms / 0 | 50.0 Hz | 48–50 | 997 ms |

Quintupling the sample rate leaves the interval at ~1 s and merely quintuples the
burst. So the aggregation is a **timer above the driver**, not a FIFO filling to a
high-water mark — a depth-triggered flush would make the interval track the burst
size, and it does not. The perfect 13,12,13,12 alternation at 12.5 Hz is what a
1.000 s timer does against an 80.35 ms period.

**3. `latency` reaches the hardware but does not control the aggregation.** At
2000 ms the driver batches ten samples into one `DataBatch`, so the parameter is
plainly not ignored. But 0 and 20 ms are indistinguishable from each other, and
neither removes the 1 s cadence. Two different mechanisms, layered.

**4. Delivery is lossless.** Sequence numbers were contiguous across every run,
including 4,316 samples in one. The bursting is a latency characteristic, not a
loss one.

**5. Therefore a staleness gate must clear the transport's own cadence, not the
sample period.** Worst observed gaps: **1,272 ms** at the default config,
**2,038 ms** with a 2 s driver latency. A gate below that reports complete data as
missing. This app uses 2,500 ms.

**6. Read the whole `DataBatch`.** Keeping only the newest entry silently
discarded nine samples in ten as soon as a latency was configured. `size()` was
reported honestly the whole time, which is the only reason it was catchable.

## The panel

**Dark thin glyphs on a light fill drop out.** Bright-on-dark renders crisply; an
early black-text-on-white-box readout showed as a blank white band. This is the
glass, not the framebuffer, so a simulator will happily show text the watch will
not. Every label in this app is bright-on-black as a rule, not a preference.

**Four levels per channel means visible banding on any gradient**, and spatial
dithering removes it. That is the `DITHER` screen's whole argument.

## TouchGFX, accurately

Worth stating precisely, because the loose version is wrong and gets corrected
immediately:

**It can** texture-map at 8bpp (`LCD8bpp_ABGR2222` ships bilinear,
nearest-neighbour and A4 variants), draw anti-aliased vector shapes
(`CanvasWidgetRenderer` has an ABGR2222 painter), and draw gradients (there is a
`LinearGradient` painter). "TouchGFX can't do transforms at 8bpp" is false.

**It cannot dither.** Zero occurrences of the word across the entire framework. It
will draw a gradient on this panel with its own painter and band it, with nothing
in the box to fix that.

**And it only pushes when something changed** — `writeDisplayFrameBuffer` is gated
on `sFlushBufferReq`, so a static screen sends nothing. An app that pushes every
tick meets any slow path far more often than the SDK's own port does.

The real argument is not capability but cost: anything procedural means a custom
widget in C++, and at that point the Designer — which is what justifies the
framework's footprint — is no longer involved. The Designer is also **Windows
only**, with no macOS support and none planned.

### Footprint

Same 600 KiB window, both measured from linker maps:

| | this app | TouchGFX `Running` |
|---|---|---|
| `.text` | 29,044 | 392,264 |
| `.bss` | 71,564 | 71,556 |
| total | **111,008 — 18.1%** | 492,224 — 80.1% |

`.bss` is nearly all framebuffer on both sides, so the comparison is ~29 KB of
code against ~392 KB. Since code runs from RAM here, that is ~360 KB of budget
returned.

## Toolchain

- **Target `apps-v1.4.0`.** `system.cpp` refuses to launch when the running
  kernel's version is *lower* than the app's, so the check is one-directional: an
  app built against an older SDK still runs on newer firmware. Building against an
  SDK newer than the watch is the mistake, and it fails silently at install time.
- **Kira's toolchain image needs `pyelftools` and `Pillow`** for the SDK's packing
  scripts. Adding only the first moves the traceback one import down.
- On an arm64 Mac the image needs `--platform linux/amd64`; the pinned base is
  amd64-only.
- Ninja is not in that image. Use `-G "Unix Makefiles"`.
- **The C++ half type-checks on a host** with
  `clang++ -fsyntax-only -I"$UNA_SDK/Libs/Header"`, no ARM toolchain required. This
  catches renames across the C ABI, which nothing else does.
- **Filesystem paths are relative to the app's own directory.** `"probe_log.csv"`,
  not `"Apps/YourApp/probe_log.csv"` — the prefix gets applied twice. Escape with
  `../SharedData/…`.

## Things that cost time

**Let cargo decide when to rebuild.** A CMake rule with `OUTPUT` and no `DEPENDS`
rebuilds the Rust archive only when it is missing, so editing Rust and rebuilding
produced a `.uapp` containing the previous renderer — and, because the ABI struct
had grown, one that read every field past the third at the wrong offset. Restating
cargo's inputs in a `DEPENDS` list is the wrong fix; anything the list misses
fails the same way.

**The hand-maintained C ABI is the weakest part of the design.** Every field must
be declared in `poc_gui.h` and `lib.rs` and can silently disagree. `poc_gui_state_size()`
catches size drift at startup but **would not catch a reordering** — swap two
`u32` fields and the size is identical while every read is wrong. Per-field
`offset_of!` / `offsetof` assertions would make both a compile error on both
sides; generating one header from the other would make the class impossible.

**Three separate instrument defects masqueraded as hardware findings.** A stale
archive faked a sensor fault; a log buffer that dropped instead of flushing
reported a truncated run as a complete one; and a column recording *which*
configuration rather than *which visit* turned a revisited cell into an apparent
150-second sensor outage. Each was caught by disagreement with a second signal —
the frame counter, a row count, a wall-clock sum — never by the number looking
wrong on its own. An instrument that can be misread by someone who does not
already know the answer is not finished.
