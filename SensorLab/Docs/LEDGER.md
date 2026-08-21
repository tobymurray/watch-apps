# SensorLab ledger

Every claim this app rests on, and what earned it. The convention is the one the
[hardware-config-recovery
investigation](https://github.com/tobymurray/una-sdk/tree/research/Docs/Investigations/2026-07-29-hardware-config-recovery)
established, because it is the only one that survives an app growing:

| Tag | Means |
| --- | --- |
| **CONFIRMED** | Measured directly, on this hardware or in a test that exercises the real code, and the method is recorded here. |
| **LIKELY** | Corroborated indirectly — inferred from another measurement, or from documentation that is itself unverified. Good enough to design against, not good enough to state as fact. |
| **UNVERIFIED** | Assumed. Nobody has checked. The row says what would check it. |
| **REFUTED** | Checked and found false. Kept, because a claim that was once believed and is now known wrong is worth more on the record than deleted. |
| **INAPPLICABLE** | The claim cannot exist. Distinct from UNVERIFIED, and the report does not count it as missing. |

**This ledger is not the profile.** The profile — `profile-<firmware>.json` and
the `SENSOR-PROFILE.md` rendered from it — holds ~1 974 claims about the *sensors*,
each with its own verdict, and it is generated. This file holds the claims about
the *platform and this app* that the profile's existence depends on: build shape,
storage behaviour, IPC limits, clocks, and what the instrument itself has been
shown to do. Roughly forty rows, maintained by hand.

Rows are dated. A row with no date has not been checked since it was written.

---

## 0. The state of the instrument, in one paragraph

**SensorLab has never been run on the watch.** Tier 0 (the evidence core) and
Tier 1 (existence, structure, liveness) are built, tested and packed; all three
builds are green; the host suite drives whole runs through the real `Service` and
passes. Every sensor row in §2 below is therefore inherited from SleepLab or
UNVERIFIED. **No sensor claim in this repository has ever come from a SensorLab
run, and this section is the first thing to change when one does.**

---

## 1. Platform and build

| # | Claim | Tag | Method / what would change it | Date |
| --- | --- | --- | --- | --- |
| P1 | The watch runs the 1.4 firmware line, so a `.uapp` carrying `KERNEL_INTERFACE_VERSION` 3 will run on it. | **LIKELY**, and **now mechanically answerable** | Inherited from SleepLab's ledger, where it is LIKELY because it was *reported by the device's owner* rather than read from the device. `SDK::Message::RequestSystemInfo` (`Messages/CommandMessages.hpp:238`) returns `firmwareVersion[16]` and `hardwareVersion[16]`; **nothing in the SDK uses it** and the only app in either repository that does is `FwDump`. SensorLab reads it at start and writes it into every manifest with a `firmware_read_from_kernel` flag saying whether it came from the kernel or from `settings.json`. **One hardware run promotes this to CONFIRMED with a quoted string.** | 2026-08-21 |
| P2 | `apps-v1.4.0` and `main` are the same commit (`edf2feeb`). | **CONFIRMED** | `git rev-parse main apps-v1.4.0` in the SDK checkout. Re-checked for this app. | 2026-08-21 |
| P3 | A `Utility` app **cannot** be service-only: the merger requires a GUI ELF for every type except `Glance`. | **CONFIRMED** | SleepLab's ledger, from `Utilities/Scripts/app_merging/app_merging.py:205` and a build that failed with `Missing .gui file`. Inherited, not re-tested. This is why SensorLab has a screen — which turned out to be the second most valuable thing in it. | 2026-08-18 |
| P4 | Every app is marked glance-capable regardless of type. | **CONFIRMED** | SleepLab's ledger, from `cmake/una-app.cmake:380`. Inherited. Means a Tier 4 glance needs no type change and would not cost autostart — which SensorLab does not want anyway. | 2026-08-18 |
| P6 | `open(write, override=false)` positions the handle at **offset 0**, not end-of-file. Appending requires an explicit `seek(size())`. | **CONFIRMED** | SleepLab's ledger, found by a failing host test. Re-pinned here: `Tests/Pipeline_test.cpp`'s `TheRunLogIsAppendedRatherThanOverwrittenFromByteZero` asserts the header is still first and the run-open row still present after many appends. Without the seek in `RunLog::append`, the file keeps only its newest row. | 2026-08-21 |
| P8 | Plugging in USB terminates every running app; autostart relaunches on unplug. | **CONFIRMED** | SleepLab's ledger, from `MapManager` recovering the kernel's own log. Inherited. Consequence here: **a profiling run cannot be watched over USB**, so a run that ends at a plug-in event is marked `truncated_by_usb` rather than completed, and `BATTERY_CHARGING` is subscribed so "it was on the cable" is in the data rather than in somebody's memory. | 2026-08-13 |
| P9 | `getTimeMs()` is device uptime: 32-bit, wraps at ~49.7 days, survives an app restart, resets only on device reboot. | **CONFIRMED** | SleepLab's ledger, from `MapManager` reading its own log across 25 launches in 10 boots. The wrap is arithmetic: `2^32 ms`. | 2026-08-13 |
| P9a | SensorLab's own arithmetic survives the wrap. | **CONFIRMED** | 2026-08-21, by construction and by test. Every duration in `StreamStats` and `Service` goes through one signed-difference helper. `Tests/Stats_test.cpp`'s `EveryDurationSurvivesTheUptimeWrap` feeds 20 000 samples starting 256 ms before the wrap and asserts the span, longest gap, rate and cadence match an identical run away from it; `Tests/Pipeline_test.cpp`'s `AWholeRunAcrossTheUptimeWrapMatchesItsTwinAwayFromIt` does the same for a whole run through the real `Service`, including the roster the screen would have drawn. A single magnitude compare anywhere would show up as a 49-day interval. | 2026-08-21 |
| P10 | The largest IPC pool block is 256 bytes. | **LIKELY** | Stated in the SDK docs and relied on by every app in this repo. Not measured. Every SensorLab message carries `static_assert(sizeof(T) <= 256)` so being wrong is a build failure rather than a runtime one. | 2026-08-21 |
| P12 | `SDK::MessageBase` deletes copy-construction and copy-assignment, so a payload anything wants to keep must be a separate plain struct. | **CONFIRMED**, again | 2026-08-21, by a build failure: `std::vector<SensorLabStatus>` in the test harness gave `use of deleted function 'SDK::MessageBase::MessageBase(const SDK::MessageBase&)'`. SleepLab hit the assignment half; this is the construction half. `SensorLabStatusData` is now separate from `SensorLabStatus`, which the GUI needed anyway. | 2026-08-21 |
| P14 | The three builds — ARM `.uapp`, host tests, TouchGFX simulator — do not agree on which sources exist or which libc functions do. | **CONFIRMED** | SleepLab's ledger, twice. Inherited and respected: the ARM build globs `Sources/*.cpp` while `simulator/gcc/Makefile` enumerates them, so every new source has to be added to the Makefile in the same commit. **SensorLab adds a third disagreement, and it is measured — see P15.** | 2026-08-18 |
| P15 | The SDK's JSON writer is safe to pass numbers to. | **REFUTED**, twice over, and this one is new | 2026-08-21, measured on the host build. `add(int32_t)` formats an `int32_t` with `%ld`, so on any LP64 target a **negative value comes out as its unsigned reinterpretation**: `add("x", -5)` produced `{"x":4294967291}`. And `add(int64_t)`/`add(uint64_t)` cast to `double` and format with `%g`, so `add("t", 1755553500)` produced `{"t":1.75555e+09}` — a UNIX timestamp with its seconds gone. Both reproductions were run, not reasoned. ARM is unaffected by the first (`long` is four bytes there), **which is exactly why it survives**: only the two builds that do not ship can see it. Consequence: **the only SDK number path this app trusts is `add(uint32_t)`**; everything fractional, negative or wider than 32 bits is written as a string. See `Docs/FINDINGS.md` §9 and §10. | 2026-08-21 |
| P16 | The whole app builds and packs into a `.uapp` against `apps-v1.4.0`, service and GUI. | **CONFIRMED** | 2026-08-21, `Tools/docker-build.sh app`, in the container Kira publishes binaries from. **235 412 bytes, ID `0D110CA4A2ADD105`, CRC `0x143DD113`**, version 0.1.0. | 2026-08-21 |
| P17 | The service fits its 500 KB budget with the whole claim catalogue resident. | **CONFIRMED** for the static size; the stack is untested | 2026-08-21. `sizeof(Service)` is **139 448 bytes (136 KB)**, measured with `sizeof` in the host container: ~67 KB of claim table (1 974 claims x 32 B), ~48 KB of histograms (37 streams x two 128-bin plus one 64-bin), ~10 KB of per-field statistics, and the rest fixed. It is a static member of the SDK's service entry point, so a build that no longer fits fails to *link* rather than failing at 03:00. **The 10 KB stack is not measured** — nothing here recurses and the sample path allocates nothing, but that is an argument rather than a measurement. | 2026-08-21 |
| P18 | The GUI's app-specific message queue can absorb a roster burst. | **REFUTED**, and fixed | 2026-08-21, in the simulator: `TouchGFXCommandProcessor::waitForFrameTick: Queue for custom messages is full`. The queue is a `FixedQueue<MessageBase*, 10>` and it discards the **oldest** when full, so an over-eager publisher silently loses the *first* roster burst — a partial data set that looks like a complete one. One publish here is four messages, and the GUI's `onStart` and `onResume` both asked for one inside a frame. Fixed by rate-limiting publishes to one per 250 ms in the service, with the request carried forward rather than dropped; the warning is gone from a simulator run. | 2026-08-21 |
| T5 | The simulator does not deliver `COMMAND_APP_NOTIF_GUI_RUN` to the service. | **CONFIRMED** | SleepLab's ledger. Inherited and designed around: SensorLab treats the GUI's first `SENSORLAB_REQUEST` as the evidence that a GUI is attached, which only a GUI can send and which is better evidence than the notification anyway. | 2026-08-18 |
| T6 | The simulator's shutdown path calls a pure virtual method, so a run's exit status is meaningless. | **CONFIRMED** | SleepLab's ledger; fixed on `una-sdk`'s `fix/simulator-shutdown-pure-virtual` (PR #214). Re-observed here: every SensorLab simulator run ends `pure virtual method called / terminate called without an active exception` after `Mock.System::exit`. Harmless — it happens after the app has stopped — but it is why `docker-build.sh sim-run` does not check one. | 2026-08-21 |
| T8 | The simulator resolves sensor drivers for a service. | **REFUTED** | 2026-08-21. A SensorLab simulator run reports `existence sweep: 0 of 37 types resolved a driver`. Linking the *entire* simulator sensor layer changed nothing — `KernelMessageDispatcher.cpp`, `SensorManager.cpp`, `InstanceSensorLayer.cpp`, all eleven simulated drivers and the timers, exactly the list `Docs/Tutorials/Sensors` uses. It built, it linked, and every request still went unanswered. Reverted, because dead code that implies a capability is worse than an honest absence. Same class as T5. Does not block anything: a simulator run still exercises the settings reader, the manifest, the sweep's code path, the claim store, the run log, the profile writer, the roster burst and the screen, and it wrote a complete `profile-unknown.json` with 40 log rows. | 2026-08-21 |
| P19 | The app keeps its inputs, not only its conclusions. | **CONFIRMED** for the writer and the decoder; the *cost* is inferred | 2026-08-21. `raw/<run>-<seq>.bin` holds every batch verbatim -- the `EventData` header plus the frame bytes, nothing interpreted -- and `runs/<id>.csv`'s `B` rows hold the full histogram behind every quantile. `RawLog_test.cpp` asserts byte-identical round trips, and `sensorlab-raw-roundtrip` has the real decoder cross-check real chunks against the manifest the same run wrote. **What is inferred is the throughput**: a 3-field sample is 24 B, so ~48 Hz is 4.15 MB/h for one sensor and a dozen types is 6-10 MB/h, which at an 8 KB buffer is a write cycle every ~3 s against the one-a-minute row S9 confirmed. The first soak measures it. | 2026-08-21 |
| P20 | A statistic that says it keeps its inputs, keeps them. | **REFUTED**, and fixed | 2026-08-21, found in this app rather than in the SDK. `Histogram::bin()` carried a doc comment saying its counts went to the profile "-- an analysis without its inputs cannot be corrected" and **nothing called it**, for two commits. The comment was a claim the code did not honour, which is precisely the failure this app exists to catch, and it was caught by grepping for callers rather than by reading the comment. Now written as `B` rows, and the round trip checks a bimodal fixture (1224 samples at 21 ms, 152 at 27 ms) whose second mode no single p50 could show. | 2026-08-21 |
| P21 | A measured zero and an unmeasured value are distinguishable in the run log. | **REFUTED**, and fixed | 2026-08-21. `writePair` wrote the `(0, 127)` sentinel -- "never established" -- for anything that was not `DecimalKind::Finite`, and `DecimalKind::Zero` is not `Finite`. So a histogram's `origin` of 0.0 read back as unmeasured, and a genuine `min` of 0.0 would have too. Zero now encodes as `(0, 0)`, which reads back as `0 x 10^0`. **Found by reading a `B` row out of the round-trip fixture**, not by reasoning about the code -- which is the argument for the round trip existing. | 2026-08-21 |
| T9 | The whole pipeline — handshake to sample to interval row to claim to profile — runs correctly offline. | **CONFIRMED** | 2026-08-21. `Tests/RunHarness.hpp` scripts the kernel message queue and drives whole runs through the real `Service`; 41 scenarios in `Pipeline_test.cpp` cover a type with no producer, an event sensor that speaks once, a frame wider than its parser, a frame narrower than its parser, a handle above 255, a kernel that will not name its firmware, a stuck field, a NaN stream, a microsecond field over 999, a run truncated by the cable, a run left open by a crash, a reboot, a volume that fills mid-run, four settings failure modes and a run across the uptime wrap. **A run costs ~70 ms.** This is what replaces the desk-answerable half of the work; it says nothing about a sensor. | 2026-08-21 |

---

## 2. Sensors — inherited, and what this app will settle

**Every row in this section came from SleepLab, and is reproduced with
attribution because those rows were paid for and should not be re-earned.** The
"SensorLab claim" column names the claim id that will confirm, refute or extend
each one on the first hardware run — which is the point of writing them down here
rather than only linking to them.

Source: [`SleepLab/Docs/FEASIBILITY-LEDGER.md`](../../SleepLab/Docs/FEASIBILITY-LEDGER.md) §2.
Dates and tags are SleepLab's.

| # | Claim (SleepLab's wording, abridged) | SleepLab's tag | SensorLab claim that revisits it |
| --- | --- | --- | --- |
| S1 | A Service keeps receiving sensor batches through a whole night of screen-off operation. No gap anywhere across 507 contiguous minutes. | **CONFIRMED** 2026-08-19 | `0x10.liveness.longest_gap_ms` over a soak. SensorLab measures it per type rather than for the accelerometer alone. |
| S3 | The delivered accelerometer rate is close to the requested one. **2875 samples in a minute against a requested 40 ms period — ~48 Hz delivered where 25 Hz was asked for.** | **REFUTED** 2026-08-18 | `0x10.timing.delivered_hz` with its spread, and `0x10.control.period_honoured`. **And now with a datasheet behind it**: the BMI270 does 0.78 Hz…1.6 kHz (`Docs/EXPECTED.md`), so ~48 Hz is ~33x of headroom, and `ACC_CONF`'s reset ODR of 100 Hz with a neighbouring 50 Hz setting makes "the kernel writes `acc_odr = 0x07`" a testable hypothesis rather than a shrug. |
| S3a | The requested accelerometer period does anything at all. | **UNVERIFIED** | `0x10.control.period_honoured` and `0x10.control.period_floor_ms`, from the layer-6 sweep. **Generalised to every streaming type**, which is the half nobody has looked at. |
| S4 | `SPO2` (0xF1) produces at least one sample. It does not even resolve a driver. | **REFUTED** 2026-08-18 | `0xf1.existence.default_resolves`, re-checked every run — and `0xf1.physical.driver_exists` is INAPPLICABLE with that reason until it flips, at which point the whole protocol turns back on by itself. |
| S5 | `HEART_BEAT` (0x40) still emits nothing on 1.4 firmware — it does not resolve a driver at all. | **CONFIRMED** 2026-08-18, with an expiry date | `0x40.existence.default_resolves`. The expiry date is why it is re-checked on every run rather than believed. |
| S6 | The PPG waveform is 20 Hz, single channel. | **UNVERIFIED**, and possibly unreachable | `0xf0.existence.default_resolves` first — if `PPG` resolves nothing, the on-device HRV route is closed and the rate question is moot. `0xf0.frame.field_count` if it does, since no parser ships and a measured layout would be the only description that exists. |
| S7 | `TOUCH_DETECT` holds "worn" without flickering: **one touch sample in 507 rows, zero transitions.** | **CONFIRMED**, emphatically, 2026-08-19 | `0x140.liveness.classification` and `0x140.physical.transitions_per_h_*`. Worth re-confirming on new firmware precisely because so much rests on it. |
| S8 | Nothing else on the device contends for the HR sensor. 30 169 arbitrated readings, all optical, none external, none unattributed. | **CONFIRMED** for the no-other-app case | `0x41.control.second_connection` measures the half that governs whether two utilities can be published together, which is untested. Also settles `Docs/FINDINGS.md` §8: those 30 169 readings are what prove `HeartRateEx::getSource()`'s float-encoded enum works. |
| S9 | Sustained one-row-a-minute open-seek-write-flush-close survived 8.45 h with no failure. | **CONFIRMED** 2026-08-19 | SensorLab writes far more — a dozen `S` rows and up to 76 `V` rows a minute against the probe's one — so this is confirmed at the probe's rate and **inferred at SensorLab's**. Every writer is capped by bytes and by duration for that reason, and the run manifest records its own throughput. |
| S11 | The simulator's sample-rate thinning rule applies on hardware and not only in the simulator. | **UNVERIFIED** | Superseded in practice by T8: the simulator resolves no drivers for a service, so its thinning gate is unreachable from here. `0x10.timing.dt_ms`'s histogram on hardware is the check, and row S3 already points the other way. |
| S12 | `TOUCH_DETECT` publishes on a clock, so an epoch with no samples means "not worn". It is an **event** sensor. | **REFUTED**, and it was a bug | `0x140.liveness.classification`, **measured from the delivered dt distribution rather than assumed**. This is the row that most shaped SensorLab: classification is a measurement, and for an event sensor the dt claims become INAPPLICABLE rather than staying UNVERIFIED for ever. |
| S17 | The requested accelerometer batch latency does anything. **5000 ms requested, 195 ms delivered, 308 batches a minute at 9.6 samples each.** | **REFUTED** 2026-08-19 | `0x10.control.latency_honoured`, and `0x10.timing.batch_jitter_ms` with its spread — kept as a separate quantity from sample dt, because conflating them is how the requested latency read as honoured. |
| S18 | `batt_pct_x10` can measure overnight battery use. The gauge read **100.0 % at both ends of an 8.45 h night** in which capacity fell 10 mAh. | **REFUTED** 2026-08-19 | `0x120.value.f0_ever_changed` and `0x120.value.f0_stuck_max_run` — the failure mode mechanised. `0x120.consistency.vs_metrics_capacity_pct` establishes the relationship, and `0x122.consistency.current_sign_convention` settles the sign the ledger flags as an unverified firmware contract. |

Rows S2, S10, S13, S14, S15 and S16 are about SleepLab's own scoring and power
budget rather than about the sensor layer, and SensorLab neither inherits nor
revisits them. They stay in SleepLab's ledger.

---

## 3. This app's own thresholds and what would justify them

Every threshold in SensorLab is a named constant with a comment stating what
justified it, or a TODO naming the run that would. The ones that are currently
*reasoned rather than measured* are listed here so they are findable in one place
rather than only by grepping for TODO.

| Constant | Value | Status | What would settle it |
| --- | --- | --- | --- |
| `kDtMinimumN` (`Catalogue.cpp`) | 10 000 samples | **reasoned** | The prompt's figure, and the arithmetic behind it holds: at the measured ~48 Hz it is ~3.5 minutes, long enough that a p95 is a property of the sensor rather than of the minute. What would improve it is the observed run-to-run spread of a p95 at several run lengths. |
| `kSkewMinimumN` | 500 000 samples | **reasoned** | ~3 hours at 48 Hz, chosen as the shortest run in which a few tens of ppm is distinguishable from millisecond quantisation. A layer-4 soak that reports skew at 1 h, 3 h and 8 h would replace it with a measurement. |
| `kLsbMinimumN` | 5 000 samples | **guess** | A layer-5 soak of `ACCELEROMETER_RAW`, whose LSB is knowable independently from the BMI270's configured range (`Docs/EXPECTED.md`). Plot the recovered step against n and take the knee. |
| `kStreamingMedianMaxMs` (`StreamStats.hpp`) | 1000 ms | **reasoned** | The only threshold in the cadence classifier. Every app in this repo requests 1000 ms for its event sensors and `HEART_RATE` delivered exactly 1 Hz, so the boundary sits between them. The layer-3 sweep across all 37 types is what would replace it with the observed distribution. |
| `kPeriodicSpreadFactor` | 3.0 | **reasoned** | An event sensor's dt spans orders of magnitude and a stream's does not, so three separates them with room. The measured distribution of p95/p50 across all types would replace it. |
| `kCadenceMinSamples` | 60 samples | **reasoned** | One minute of the slowest plausible stream. Below it the classifier says `Unknown`, which is what `TOUCH_DETECT`'s one-sample-in-507-minutes correctly reports. |
| `DEFAULT_TOLERANCE` (`profile_diff.py`) | 2 % | **reasoned** | Used only where a claim carries no spread. Above mantissa and bin quantisation, well below the smallest change anyone would call a regression (S3 is 92 %, the `RUNNING_CADENCE` shrink is 50 %). Replace with the measured run-to-run spread per claim once several profiles of one firmware exist. |
| `kAssumedFields` (`Catalogue.hpp`) | 8 slots | **reasoned** | The widest shipped frame is `HEART_RATE_EX` at 7, so 8 records a discovered frame one field wider than anything currently shipped. A wider frame records its first 8 fields and raises the note on `frame.field_count`; it does not overflow. |
| `kPublishMinGapMs` (`Service.cpp`) | 250 ms | **measured against a bound** | The bound is real (P18: a ten-deep queue, four messages a publish). 250 ms is chosen well inside it rather than derived; anything under ~60 ms would be at risk again. |
| `kRawBufferBytes` (`RawLog.hpp`) | 8 KB | **reasoned** | At ~10 MB/h this is one write cycle every three seconds, against the one-a-minute row S9 confirmed. Larger reduces the write rate and loses more to a cable event; smaller pushes the rate somewhere nothing has measured. The first soak's manifest carries bytes and duration, which settles it. |
| `rawMaxMb` default (`Settings.hpp`) | 256 MB | **reasoned** | A twelve-hour soak is 70-120 MB by the arithmetic above, so this holds one with margin. `MapManager` CRC-verified 160.5 MiB on this volume, which is the right order to fit and the wrong order to be casual about. |
| `rawChunkKb` default | 512 KB | **reasoned** | Small enough that a chunk lost to the cable is ~3 minutes of one stream; large enough that rotation is not the dominant cost. Nothing has measured the trade. |

---

## Open threads

- **The first hardware run is the whole of the next step.** Copy
  `Sensor_Lab_0.1.0.uapp` into `Apps/SensorLab/` on the USB-MSC volume, unplug,
  open the app, and read the roster. That alone produces the first publishable
  artifact — an existence and structure table for all 37 types on this firmware —
  and it promotes P1 to CONFIRMED with a quoted firmware string. It takes minutes.
- **Then a soak.** `R1` starts one; twelve hours is the default cap. That is what
  fills layers 3, 4 and 5, and it is the run that must not be interrupted by the
  cable.
- **Layers 6, 7 and 8 are not built.** Their claims are in the catalogue,
  UNVERIFIED, each naming its method — which is what keeps the completeness
  fraction honest rather than letting a Tier 1 profile read as finished. Layer 8
  is the highest value per unit of effort and needs no user at all beyond one run
  across local midnight; do it before layer 7, because half of what it finds
  changes what layer 7's protocols should measure.
- **Four datasheets are unsourced.** BMM350, MS5837, PAH8316LS, MAX17262 and the
  AG3335M. `Docs/EXPECTED.md` says what each would settle; the MS5837 and the
  MAX17262 are the two whose absence currently costs the most.
- **The 10 KB service stack is an argument, not a measurement.** Nothing
  recurses and the sample path allocates nothing, which is a good argument. A
  hardware run that does not crash is weak evidence; a stack high-water mark
  would be real. `FwDump`'s direct-memory precedent is how it would be read.
- **Tier 5 is not built and may never be.** Reads only, never writes, gated
  behind a setting that is off by default. The flag is parsed and recorded in the
  manifest so a profile can say the tier was off; the LIKELY inference from
  layer 5's LSB estimate is what the profile carries meanwhile, and
  `Docs/EXPECTED.md` shows that inference is precise enough to name the register
  value it implies. **That is probably good enough for ever.**
