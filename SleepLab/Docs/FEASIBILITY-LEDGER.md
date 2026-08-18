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
| P11 | The whole app builds and packs into a `.uapp` against `apps-v1.4.0`, service and GUI. | **CONFIRMED** | `SleepLab/Tools/docker-build.sh app`, in the container Kira publishes binaries from. 230 740 bytes, `ID E4782CD726259DC6`. | 2026-08-18 |
| P12 | `SDK::MessageBase` deletes copy-assignment, so a message payload the GUI needs to keep must be a separate plain struct. | **CONFIRMED** | Build failure: `use of deleted function 'SDK::MessageBase& operator=(const SDK::MessageBase&)'`. Correct behaviour — a message is a pooled block with an identity, and copying one would produce a second object claiming the same slot. `SleepReportData` is now separate from `SleepReport`. | 2026-08-18 |
| P13 | `SleepReport` at 100 strip buckets is 216 bytes and fits the pool block; at 200 it did not. | **CONFIRMED** | Measured with `sizeof` in the host container. `MessageBase` is 40 bytes, not the 32 the older examples' comments imply. The bucket count is set by the panel (200 px usable / 2 px per bucket) rather than by the message size, and fitting is the happy consequence. | 2026-08-18 |

---

## 2. Sensors — what the Tier 0 probe is for

**A two-minute hardware run on 2026-08-18 settled four of these**, from the
probe's own screen rather than from a log. The rest still need a night. Each row
names the column of `probe_log.csv` that settles it; `Tools/probe_report.py`
prints them grouped the same way.

The screen read:

```
run 0h02m hr:cont
ATMRHXbpoSLCE
rows 3 ok
last acc 2875 hr 60
beat 0 spo2 0
batt 100.0%
```

Lower case in the block means the driver did not resolve, so `b`, `p` and `o`
are `HEART_BEAT`, `PPG` and `SPO2`. The absent `worn` line means TOUCH_DETECT
delivered nothing that minute -- which turned out to be the most consequential
thing on the screen. See S12.

| # | Claim | Tag | What would settle it |
| --- | --- | --- | --- |
| S1 | A Service keeps receiving sensor batches through a whole night of screen-off low-power operation. | **UNVERIFIED** | One unplugged, worn night. The report's `continuity` section: gaps in `uptime_ms` longer than 90 s. A gap is the failure — a stalled service writes no rows at all rather than rows of zeroes. |
| S2 | Continuous optical HR costs less than about a third of the battery overnight. | **UNVERIFIED** | Two nights, `probe.json` `"hr": "continuous"` then `"hr": "off"`, the report's `power` section. `batt_pct_x10` gives percent; `batt_avg_ma_x10` gives mA directly. If the answer is "more than a third", HR sampling becomes periodic and `"hr": "duty"` is already there to measure the alternative. |
| S3 | The delivered accelerometer rate is close to the requested one. | **REFUTED** | 2026-08-18, on hardware: **2875 accelerometer samples in a minute against a requested 40 ms period**. That is ~48 Hz delivered where 25 Hz was asked for -- nearly *double*, and in the opposite direction to the thinning the simulator's gate does. Heart rate in the same minute delivered exactly 60 samples against a requested 1 s period, so the period is honoured there and not here; the likeliest reading is that the accelerometer runs at a native rate it will not go below. **Nothing downstream is wrong because of it**: EpochCounter is rate-independent by construction and a host test pins that across a 4x span, which is precisely the design decision this measurement justifies. What it does cost is roughly double the IPC and sample-path power that was budgeted for. |
| S3a | The requested accelerometer period does anything at all. | **UNVERIFIED** | Follows from S3. Ask for 80 ms and 20 ms in two short runs and compare `acc_n`: if all three land near 2875, the period is ignored and `kAccelPeriodMs` is decoration. That matters for power, since it decides whether the sample rate is a lever or a given. |
| S4 | `SPO2` (0xF1) produces at least one sample. | **REFUTED** | 2026-08-18, on hardware. It does not even resolve a driver: `spo2` defaults to on, so `connect()` was called and returned false, and the screen showed lower-case `o`. There is no firmware producer to ask. Nothing is built on SpO2 and now nothing can be. |
| S5 | `HEART_BEAT` (0x40) still emits nothing on 1.4 firmware. | **CONFIRMED**, and more strongly than expected | 2026-08-18, on hardware. It does not resolve a driver at all — lower-case `b` on the screen — so the question is not "does it emit events" but "there is nothing to subscribe to". UNA answered "no events at all" against the 1.3 line (PR #167); 1.4 has not changed it. **The staging clause in §3 stands**, and the HRV fields stay reserved and absent. Re-check after any firmware bump: UNA describe a higher-rate PPG mode and on-chip HRV as things they are working on, so this row has an expiry date even though it is CONFIRMED today. |
| S6 | The PPG waveform is 20 Hz, single channel. | **UNVERIFIED**, and possibly unreachable | UNA maintainer, PR #167, 2026-07-01. On the 2026-08-18 run `ppg` was off in config, so the lower-case `p` proves nothing -- but given `HEART_BEAT` and `SPO2` both failed to resolve, there is a real chance `PPG` has no app-facing driver either. Settle it with `"ppg": "on"` for one short run and read the block: an upper-case `P` means a driver exists, lower-case means the raw waveform is not available to apps at all, which would close the on-device HRV route for good. |
| S12 | `TOUCH_DETECT` publishes on a clock, so an epoch with no samples means "not worn". | **REFUTED**, and it was a bug | 2026-08-18, on hardware: it resolved (upper-case `T`) and delivered **zero samples in a minute**. It is an event sensor — it publishes on a change of state. SleepLab read a sample-less epoch as 0 % worn, which put every epoch below the scorer's worn floor, which made every epoch Unscorable, which would have reported **every night as NOT WORN** with its numbers suppressed. Fixed: worn state is sticky across epochs now, and a sensor that never speaks at all yields `Uncertain` with its own reason rather than `NotWorn` — telling somebody their watch was not worn would send them to put on a watch they are already wearing. Found in two minutes of hardware, by the app that exists to find it. |
| S7 | `TOUCH_DETECT` holds "worn" for a loosely-strapped sleeping wrist without flickering. | **UNVERIFIED** | The report's `worn detection`: transitions per hour, and how many rows contain one. This is the single most load-bearing sensor claim in the app — §3.4 makes every sleep number conditional on the worn gate, so a sensor that flickers thirty times an hour would suppress every night. A worn *fraction* cannot answer it; that is why `touch_edges` is its own column. |
| S8 | Nothing else on the device contends for the HR sensor. | **UNVERIFIED** | `hrex_opt` / `hrex_ext` / `hrex_unk` across a night with no other app installed, then again with one. `HEART_RATE_EX` is subscribed precisely so HR provenance is recorded rather than assumed. |
| S9 | The user volume has room for a decade of nights, and sustained append throughput is not a constraint. | **LIKELY** | 2026-08-18: three rows written and none failed (`rows 3 ok`), so the open-seek-write-flush-close cycle works against the real filesystem -- which is the half that could have been silently broken. Throughput and free space still need a night: `bytesWritten` against elapsed time, and the file size afterwards. Arithmetic says ~46 KB/night and ~17 MB/decade, against a volume `MapManager` CRC-verified 160.5 MiB of map packs on. |
| S10 | An idle service that sleeps to its next deadline does not wake often enough to matter. | **UNVERIFIED** | The report's `loop` section: `wakes` per row. The probe sleeps to its next row boundary and should wake roughly once per delivered batch. The report flags above 2000 wakes/row as a spinning loop. |
| S11 | The sample-rate thinning rule (S3) applies on hardware and not only in the simulator. | **UNVERIFIED** | The rule is CONFIRMED *for the simulator* — pinned by `Tests/Host/simulator/SampleRateAdapter_test.cpp` on `una-sdk`'s `feat/sample-rate-adapter-rule`, which asserts the half-period boundary, the exact-ratio edge and the quantised bands. Whether the kernel's own gate behaves identically is not known. `acc_n` against `acc_ts_span_ms` on hardware is the check. |

---

## 2a. Tier 4 surfaces

| # | Claim | Tag | Method / what would change it | Date |
| --- | --- | --- | --- | --- |
| T1 | As of the 1.4 kernel, mute does not silence app-requested alerts — muting covers alerts the watch raises at the user, not feedback an app produces in a session it owns. | **UNVERIFIED** | Stated upstream (PR #267, una-kernel#260) and not checked on this unit. **An alarm that fails silently is worse than no alarm**, which is why the alarm is off by default and the README says to test it on a weekend. To confirm: mute the watch, set `alarm_at` two minutes ahead inside the bedtime window, and wait. |
| T2 | The glance and the home widget work from a `Utility` app without changing its type. | **LIKELY** | The flag is CONFIRMED (row P4) and the code builds; neither surface has been seen on hardware. `EVENT_GLANCE_START` / `TICK` / `STOP` are handled in the service exactly as `GlanceHR` handles them, and the widget follows `Timer`'s claim/release pattern. |
| T3 | `SDK/Fit/FitProfile.hpp` on 1.4 has no monitoring or sleep messages; its `File` enum holds only `Activity = 4`. | **CONFIRMED** | Read directly: `enum class File : uint8_t { Activity = 4 };` at `FitProfile.hpp:45`, and `grep -i "monitor\|sleep"` over the header returns nothing. So there is no FIT export, and the CSV and JSON are the export. | 2026-08-18 |
| T5 | The simulator does **not** deliver `COMMAND_APP_NOTIF_GUI_RUN` to the service. | **CONFIRMED** | Found by running it: the service never set `mGuiStarted`, never published, and the screen sat on "waiting for service..." for the whole run. Fixed by treating a `SLEEP_REQUEST` as the evidence that a GUI is attached, which only a GUI can send and which is better evidence than the notification anyway. Whether hardware delivers the notification is untested; nothing now depends on it. | 2026-08-18 |
| T6 | The simulator's shutdown path calls a pure virtual method. | **CONFIRMED**, and **not this app's** | Every run ends `pure virtual method called / terminate called without an active exception` after `Mock.System::exit`. It is the SDK's own teardown order, fixed on `una-sdk`'s `fix/simulator-shutdown-pure-virtual` (PR #214). Harmless here -- it happens after the app has stopped -- but it means a simulator run's exit status says nothing. | 2026-08-18 |
| T7 | The report and history reach the screen: the service reads the index, chunks a burst, and the GUI reassembles it. | **CONFIRMED** | `Tools/docker-build.sh sim-run` against `Tests/fixtures/index.csv`: `publishing 6 nights of history`, no crash across a 12 s run with the report and history screens both drawn. Exercises the index tail-read, the burst contract and the strip widget's construction -- and, the simulator having no sensors and no battery, nothing at all about an overnight recording. | 2026-08-18 |
| T4 | The BLE File Transfer Service can pull files out of `Apps/SleepLab/` while the service keeps running. | **UNVERIFIED** | `prototype/una_ble_client.py` is validated for `.fit` files under `Apps/GpsLab/` with matching CRC-16, and nothing in the protocol is path-specific — but `Tools/pull_nights.py` has never been run against a watch. Needs Linux with BlueZ, `dbus_fast`, and a bonded device. |

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
| A8 | The Cole-Kripke weights, P, and the Webster rescoring thresholds implemented here are the published ones. | **UNVERIFIED** | Transcribed from the literature; nobody here has checked them against Cole *et al.* 1992, *Sleep* 15(5):461-469 or Webster *et al.* 1982, *Sleep* 5(4):389-399. They are gathered in one constant block in `SleepWakeScorer.hpp` precisely so that checking them is a one-block fix. The *shape* is unmistakable and is pinned by tests (the current epoch dominates; the window runs four back and two forward); the exact values are not. |
| A9 | `kCountScale`, which bridges this device's count units to the units Cole-Kripke was fitted against, is correct. | **UNVERIFIED** | **It is a guess**, and it is the single number standing between "cites a real paper" and "is validated". It cannot be derived, only measured. See the validation table below and the TODO on the constant itself. |
| A7 | Skin temperature is not available. `AMBIENT_TEMPERATURE` (0x70) is ambient. | **CONFIRMED** | The SDK's own type name and its doc comment. Never labelled as body temperature anywhere in this app. |

---

## Validation status of every reported metric

The table SleepLab's README points at. Filled in as each tier lands; a metric
with no row here is a metric the app must not print.

| Metric | What it actually measures | Validation | Known failure modes |
| --- | --- | --- | --- |
| **activity count** | Integrated band-limited (0.25–3 Hz) acceleration per axis over the epoch, combined as the vector magnitude of the three integrals, in units of g·s × 1000. | *synthetic-only.* Host tests pin stillness near zero, linear scaling with amplitude, the band's rejection of 0.05 Hz drift (20×) and 12 Hz shock (15×), identical response on every axis, and **independence from the delivered sample rate across a 4× span**. | A delivery gap contributes nothing rather than a fabricated rectangle, but an epoch built from a handful of samples still integrates to near-zero — which reads as perfect stillness. Guarded twice: a `kDataGap` flag at the recording epoch and `Unscorable` at the scoring epoch. |
| **sleep / wake per epoch** | Cole-Kripke over a ±window of counts, with Webster rescoring. | *synthetic-only, and the correspondence to sleep is unestablished* — see A9. | Systematically over-calls sleep: lying still awake scores as sleep. |
| **estimated total sleep time** | Epochs scored Sleep between onset and final wake. | *synthetic-only.* Exact against nights of known shape. | **Biased high**, direction known. Reported alongside `stillInBedMin` for exactly this reason. |
| **time in bed still** | Epochs between onset and final wake with counts below the movement floor. | *synthetic-only* for the arithmetic; the floor itself is a guess with a TODO. | Measures stillness, which is what was observed. Does not distinguish a still sleeper from a still reader. |
| **sleep onset / latency** | First epoch of the first run of 10 consecutive Sleep epochs; session start to there. | *synthetic-only.* | Unscorable epochs break the run, so a delivery outage delays onset rather than declaring it. |
| **WASO, awakenings** | Wake epochs, and runs of them, between onset and final wake. | *synthetic-only.* Exact, including that wake before onset and after final wake is neither. | An outage inside the night is neither wake nor an awakening — it breaks a run without ending it as one. |
| **sleep efficiency** | totalSleepMin / timeInBedMin, as a percentage. Against time in **bed**, the conventional denominator. | *synthetic-only.* | Inherits the sleep-time bias exactly. The onset-to-final-wake variant is also called sleep efficiency and gives a flattering number; this is not that one. |
| **movement index** | Percentage of epochs between onset and final wake above the movement floor. | *synthetic-only.* | Independent of the scorer, so it stays meaningful where the scorer's calibration does not. |
| **worn verdict** | Worn fraction plus a micro-movement-or-pulse plausibility check. | *synthetic-only.* Host tests pin the nightstand case, mid-night removal, brief dropout tolerance, and the HR-off degradation to `Uncertain`. | **Every threshold is a guess** pending the worn-vs-table probe nights (S7). Too loose and a nightstand passes; too tight and real nights are suppressed. |
| **restfulness band** | Four-level ordinal index over movement and heart rate relative to the night's own HR minimum. | *speculative.* Well-defined arithmetic; no evidence it corresponds to anything. | **Not a sleep stage.** Comparable in shape between nights, not in level, because the HR reference is per-night. |
| **nocturnal HR minimum / mean** | Lowest and mean epoch-mean heart rate. | *sensor measurement,* reported as such. | Optical HR degrades with a loose band. Provenance is recorded per epoch via `HEART_RATE_EX`, not assumed. |
| **HR and efficiency deltas** | Tonight against the median of the wearer's own last 28 recorded nights. | *synthetic-only* for the arithmetic. | Refuses to report below 5 nights. Median, not mean, so one night on a plane does not move it. Nights that failed the worn gate never enter it. |

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
- **A3, A4 and A9's thresholds are unjustified numbers with TODOs against them**
  in the code. Each carries a comment naming the recording that would justify
  it. The largest is `kCountScale`: sweep it against ten diary-validated nights,
  report the mean signed error and spread on onset and final wake at each value,
  and put the minimising value in the code and the residual error in the README.
- **The diary has not been started.** §6 of the brief asks for at least ten
  nights of hand-recorded lights-out and wake times. That is the only thing that
  can turn any row in the validation table from *synthetic-only* into
  *diary-validated*, and it can be started the same night as the first probe run.
- **The adversarial nights have not been run.** Each is meant to become a
  regression fixture: worn but lying awake and still for 30 minutes; not worn on
  a table; worn while charging; a device reboot mid-night; a clock change
  mid-night; a deliberate early get-up. The synthetic fixtures already cover the
  *shapes* — what the real nights would add is whether the thresholds put real
  data on the right side of them.
