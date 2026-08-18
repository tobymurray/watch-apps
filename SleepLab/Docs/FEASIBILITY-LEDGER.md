# SleepLab feasibility ledger

Every claim this app rests on, and what earned it. The convention is the one
the [hardware-config-recovery
investigation](https://github.com/tobymurray/una-sdk/tree/research/Docs/Investigations/2026-07-29-hardware-config-recovery)
established, because it is the only one that survives an app growing:

| Tag | Means |
| --- | --- |
| **CONFIRMED** | Measured directly, on this hardware or in a test that exercises the real code, and the method is recorded here. |
| **LIKELY** | Corroborated indirectly — inferred from another measurement, or from documentation that is itself unverified. Good enough to design against, not good enough to state as fact. |
| **UNVERIFIED** | Assumed. Nobody has checked. The row says what would check it. |
| **REFUTED** | Checked and found false. Kept, because a claim that was once believed and is now known wrong is worth more on the record than deleted. |

**Nothing here is validated against polysomnography.** There is no PSG, there
will not be one, and no amount of internal consistency substitutes for it. What
the sleep-science rows below are validated against is stated in each row, and it
is never a sleep laboratory. See [§ Validation status of every reported
metric](#validation-status-of-every-reported-metric).

Rows are dated. A row with no date has not been checked since it was written.

---

## 1. Platform and build

| # | Claim | Tag | Method / what would change it | Date |
| --- | --- | --- | --- | --- |
| P1 | The watch runs the 1.4 firmware line, so a `.uapp` carrying `KERNEL_INTERFACE_VERSION` 3 will run on it. | **LIKELY** | Reported by the device's owner, who checked the watch. Not read here from the BLE DIS Firmware Revision String (0x2A26) or a Settings screenshot, which is what would make it CONFIRMED. Everything in this repo's SleepLab tree is built against `apps-v1.4.0`; if this is wrong, the app exits instantly to an `App PID` error screen and nothing catches it at build time. | 2026-08-18 |
| P2 | `apps-v1.4.0` and `main` are the same commit (`edf2feeb`). | **CONFIRMED** | `git rev-parse main apps-v1.4.0` in the SDK checkout. | 2026-08-18 |
| P3 | A `Utility` app **cannot** be service-only: the merger requires a GUI ELF for every type except `Glance`. | **CONFIRMED** | Built the probe with no `TOUCHGFX_PATH` and got `Missing .gui file in .../Tmp (required for type Utility)`. The rule is at `Utilities/Scripts/app_merging/app_merging.py:205`, `gui_optional = (args.type == "Glance")`. This is why the Tier 0 probe has a screen. | 2026-08-18 |
| P4 | Every app is marked glance-capable regardless of its type, so a `Utility` app can carry a glance without becoming a `Glance` app. | **CONFIRMED** | `cmake/una-app.cmake:380` passes `-glance_capable` unconditionally. The probe's own build logs `Flags: 0x00000029` — type 1 (`Utility`) \| autostart `0x8` \| glance-capable `0x20` — and `Glance-capable: yes`. Settles the "should this be a Glance instead?" question: Tier 4's glance needs no type change, and changing type would cost autostart. | 2026-08-18 |
| P5 | A `Glance` app's service is driven by the glance carousel — `EVENT_GLANCE_START` connects, `EVENT_GLANCE_STOP` returns from `run()` — so it cannot record overnight. | **LIKELY** | Read from `Examples/Apps/GlanceHR/Software/Libs/Source/Service.cpp:60` and `Docs/Examples/GlanceHR-Architecture.md:151`; none of the four Glance examples sets `APP_AUTOSTART`. Not tested by building a Glance app with autostart and seeing what the kernel does, which is what would settle whether the lifecycle is the kernel's or GlanceHR's own choice. Nothing depends on the answer: `Utility` is right regardless (P4). | 2026-08-18 |
| P6 | `open(write, override=false)` positions the handle at **offset 0**, not at end-of-file. Appending requires an explicit `seek(size())`. | **CONFIRMED** | Found by a failing host test: two `Log::begin()` calls produced two lines instead of three, because the second launch overwrote the first from byte 0. Confirmed against `Tests/Host/support/KernelTestDoubles.cpp`, whose `InMemoryFile::open()` ends `mPos = 0` in both modes; FatFs `f_open` behaves the same way, which is why the fix is a seek rather than a different open flag. `ProbeLog.cpp`'s `append()` now seeks; `ProbeLog_test.cpp` pins it. | 2026-08-18 |
| P7 | The Tier 0 probe builds and packs into a `.uapp` against `apps-v1.4.0`. | **CONFIRMED** | `SleepLab/Tools/docker-build.sh probe`, in the container Kira publishes binaries from. 181 192 bytes, `CRC 0x24307220`, `ID 999A630E7F105A07`. | 2026-08-18 |
| P8 | Plugging in USB terminates every running app; autostart relaunches on unplug. So a watch charging overnight records nothing. | **CONFIRMED** | Not by this app — by `MapManager`, which recovered the kernel's own log out of `Crash/dump_*.bin`: `UsbDevice::onUsbDetected: USB cable plugged` → `App.Manager::stopAll: Terminate all active applications`. See `MapManager/README.md`. Independently re-checkable from any SleepLab probe log: a run boundary in the middle of a night is this. | 2026-08-13 |
| P9 | `getTimeMs()` is device uptime: it survives an app restart and resets only on device reboot. It is 32-bit and wraps at ~49.7 days. | **CONFIRMED** | `MapManager/README.md`, from reading its own log across 25 app launches in 10 device boots. The wrap is arithmetic, not measurement: `2^32 ms`. Every duration in SleepLab uses unsigned or signed differences accordingly. | 2026-08-13 |
| P10 | The largest IPC pool block is 256 bytes. | **LIKELY** | Stated in the SDK docs and relied on by every app in this repo (`Alarm/Commands.hpp` sizes `AlarmList` to 134 bytes against it explicitly). Not measured here. Every SleepLab message carries a `static_assert(sizeof(T) <= 256)` so that being wrong is a build failure rather than a runtime one. | 2026-08-18 |

---

## 2. Sensors — what the Tier 0 probe is for

**Every row in this section is UNVERIFIED until a probe night has run.** That is
the honest state of this app today and it is the reason the probe exists before
any product code. Each row names the column of `probe_log.csv` that settles it;
`Tools/probe_report.py` prints them grouped the same way.

| # | Claim | Tag | What would settle it |
| --- | --- | --- | --- |
| S1 | A Service keeps receiving sensor batches through a whole night of screen-off low-power operation. | **UNVERIFIED** | One unplugged, worn night. The report's `continuity` section: gaps in `uptime_ms` longer than 90 s. A gap is the failure — a stalled service writes no rows at all rather than rows of zeroes. |
| S2 | Continuous optical HR costs less than about a third of the battery overnight. | **UNVERIFIED** | Two nights, `probe.json` `"hr": "continuous"` then `"hr": "off"`, the report's `power` section. `batt_pct_x10` gives percent; `batt_avg_ma_x10` gives mA directly. If the answer is "more than a third", HR sampling becomes periodic and `"hr": "duty"` is already there to measure the alternative. |
| S3 | The delivered accelerometer rate is close to the requested one. | **UNVERIFIED** | The report's `delivered rates`. Expected to be **false**: the per-listener gate thins on a boundary at *half* the expected period, an exact ratio falls on the thinner side, and the thinning is quantised into bands rather than being proportional to rate. Nothing in SleepLab infers elapsed time from a sample count. |
| S4 | `SPO2` (0xF1) produces at least one sample. | **UNVERIFIED** | One night with `"spo2": "on"`. Non-zero `spo2_n` anywhere in the file. Nothing is built on SpO2 until this is CONFIRMED — §3 of the implementation brief forbids it. |
| S5 | `HEART_BEAT` (0x40) still emits nothing on 1.4 firmware. | **UNVERIFIED** | Non-zero `beat_n` anywhere in a probe night. UNA answered "no events at all — it's more of a frequency domain algorithm" against the 1.3 line (PR #167, recorded in `una-sdk@research`). Firmware moved on 2026-08-17 and UNA describe a higher-rate PPG mode and on-chip HRV as things they are working on, so the answer has an expiry date. **A non-zero count here reopens overnight HRV, and overnight HRV reopens the staging clause in §3.** The probe subscribes to `HEART_BEAT` in every mode, including `"hr": "off"`, because a stream that emits nothing costs nothing to listen to. |
| S6 | The PPG waveform is 20 Hz, single channel. | **LIKELY** | UNA maintainer, PR #167, 2026-07-01. Re-measurable here as `ppg_n / ppg_ts_span_ms` with `"ppg": "on"`. Above 20 Hz would mean the higher-frequency mode landed. |
| S7 | `TOUCH_DETECT` holds "worn" for a loosely-strapped sleeping wrist without flickering. | **UNVERIFIED** | The report's `worn detection`: transitions per hour, and how many rows contain one. This is the single most load-bearing sensor claim in the app — §3.4 makes every sleep number conditional on the worn gate, so a sensor that flickers thirty times an hour would suppress every night. A worn *fraction* cannot answer it; that is why `touch_edges` is its own column. |
| S8 | Nothing else on the device contends for the HR sensor. | **UNVERIFIED** | `hrex_opt` / `hrex_ext` / `hrex_unk` across a night with no other app installed, then again with one. `HEART_RATE_EX` is subscribed precisely so HR provenance is recorded rather than assumed. |
| S9 | The user volume has room for a decade of nights, and sustained append throughput is not a constraint. | **UNVERIFIED** | `bytesWritten` against elapsed time, and the file size after a night. Arithmetic says ~46 KB/night at 30 s epochs and ~17 MB/decade, against a volume `MapManager` CRC-verified 160.5 MiB of map packs on — but measured, not assumed. |
| S10 | An idle service that sleeps to its next deadline does not wake often enough to matter. | **UNVERIFIED** | The report's `loop` section: `wakes` per row. The probe sleeps to its next row boundary and should wake roughly once per delivered batch. The report flags above 2000 wakes/row as a spinning loop. |
| S11 | The sample-rate thinning rule (S3) applies on hardware and not only in the simulator. | **UNVERIFIED** | The rule is CONFIRMED *for the simulator* — pinned by `Tests/Host/simulator/SampleRateAdapter_test.cpp` on `una-sdk`'s `feat/sample-rate-adapter-rule`, which asserts the half-period boundary, the exact-ratio edge and the quantised bands. Whether the kernel's own gate behaves identically is not known. `acc_n` against `acc_ts_span_ms` on hardware is the check. |

---

## 3. Sleep science — what the numbers mean

These rows are not about the hardware. They are about whether a number this app
prints means what its name says, and they are the ones that go stale quietly.

| # | Claim | Tag | Basis, and its limits |
| --- | --- | --- | --- |
| A1 | Wrist actigraphy scores sleep/wake from activity counts with high sensitivity to sleep (~85–95 %) and poor specificity to wake (~40–60 %). | **LIKELY** | The published validation literature for Cole–Kripke, Sadeh and Oakley-style scorers against PSG. Not measured here and not measurable here. The consequence is a design requirement, not a caveat: the report says *time in bed still* where it can and marks *estimated sleep time* as an estimate with a known direction of bias — it over-reports sleep, because lying still awake scores as sleep. |
| A2 | Light/deep/REM discrimination is not supportable with this sensor set. | **CONFIRMED** | Follows from S5 and S6: staging rests on HRV plus multi-channel PPG, `HEART_BEAT` emits nothing so there are no RR intervals, and `RR_INTERVAL` (SDK PR #220) has no firmware producer — the kernel parses and discards the RR values a chest strap already sends, so even a Polar H10 worn overnight yields none. **No stage hypnogram, no minutes-in-REM, ever, while this row stands.** If S5 flips, this row is revisited *here, in writing*, with the literature in hand — not by quietly relabelling a band as a stage. |
| A3 | A movement-and-HR "restfulness" band is honest as a relative index. | **UNVERIFIED** | It is arithmetic over two channels this app does record, so it is well-defined; whether it corresponds to anything is unknown. It ships labelled as what it is, with its method string written into every summary JSON so no file is ever ambiguous about how its band was computed. It is never given minutes-in-stage figures. |
| A4 | Heart rate is meaningful only relative to the wearer's own baseline. | **LIKELY** | Nocturnal HR minimum, time-to-minimum and the morning rise are real and personal; absolute thresholds copied from a paper are not transferable. No absolute physiological threshold appears anywhere in this app. Deltas are refused until enough of this user's own nights exist — five, chosen as the smallest number that is not one bad night, and itself UNVERIFIED. |
| A5 | Off-wrist is the failure mode that discredits everything. | **CONFIRMED** | Arithmetic, not measurement: a watch on a nightstand is perfectly still and would score a flawless night. Every sleep claim is gated on worn-detection *plus* a plausibility check (micro-movement and a valid HR reading), and a night failing the gate is reported as *not worn* with its sleep numbers **suppressed**, not annotated. |
| A6 | Respiratory rate from a wrist accelerometer at these rates is not defensible. | **CONFIRMED** | Not shipped. Recorded here so the absence is a decision rather than an oversight. |
| A7 | Skin temperature is not available. `AMBIENT_TEMPERATURE` (0x70) is ambient. | **CONFIRMED** | The SDK's own type name and its doc comment. Never labelled as body temperature anywhere in this app. |

---

## Validation status of every reported metric

The table SleepLab's README points at. Filled in as each tier lands; a metric
with no row here is a metric the app must not print.

| Metric | What it actually measures | Validation | Known failure modes |
| --- | --- | --- | --- |
| *(Tier 1 and 2 metrics land here as they are implemented)* | | | |

Legend for **Validation**: *synthetic-only* — pinned by generated fixtures with
known answers, which prove the arithmetic and nothing about a person.
*diary-validated* — compared against at least ten nights of hand-recorded
lights-out and wake times, with the mean signed error and spread stated.
*speculative* — computed and displayed, with no evidence it corresponds to
anything.

---

## Open threads

- **P1 is the one row blocking a hardware night.** Read the DIS Firmware
  Revision String (UUID 0x2A26) or the watch's Settings screen and promote it to
  CONFIRMED, or apply `UnaWatch-Kernel_1.4.0.ota` first.
- **Every row in §2 is UNVERIFIED.** Two probe nights — one worn and unplugged
  with `"hr": "continuous"`, one on a table — settle most of them. See
  `../Probe/README.md`.
- **A3 and A4's thresholds are unjustified numbers with TODOs against them** in
  the code. Each carries a comment naming the recording that would justify it.
