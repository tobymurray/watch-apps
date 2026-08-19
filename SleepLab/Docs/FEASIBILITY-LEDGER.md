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

**A note on what "a test that exercises the real code" now means.** Until
2026-08-18 nothing exercised the recorder's own path: the engine had tests over
synthetic scoring inputs, the store over synthetic epochs, and the simulator had a
screen and no sensors. `Tests/NightHarness.hpp` drives whole nights through the
real `Service` by scripting the kernel message queue, so a night costs 110 ms. Rows
promoted on that basis say so and name the scenario. It is still generated data:
it proves the code computes what it claims to, and nothing whatever about a person
or about this hardware.

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
| P9a | SleepLab's four uptime comparisons — the epoch grid, the HR duty cycle, the resume classification and the sample path's dt — survive the wrap. | **CONFIRMED** | 2026-08-18. Previously a reading of the code; now three offline-harness nights *constructed across* the wrap (`Pipeline_test.cpp`, `UptimeWrap.*`): an ordinary night, one on a duty cycle, and one interrupted on the near side and resumed on the far side. All three match their away-from-the-wrap twins on time in bed, total sleep and the reported wake minute, with no epoch that absorbed 49 days and a duty cycle still turning HR on in the second half of the night. This is the row's *consequence* earned by measurement rather than by inspection. | 2026-08-18 |
| P10 | The largest IPC pool block is 256 bytes. | **LIKELY** | Stated in the SDK docs and relied on by every app in this repo (`Alarm/Commands.hpp` sizes `AlarmList` to 134 bytes against it explicitly). Not measured here. Every SleepLab message carries a `static_assert(sizeof(T) <= 256)` so that being wrong is a build failure rather than a runtime one. | 2026-08-18 |
| P11 | The whole app builds and packs into a `.uapp` against `apps-v1.4.0`, service and GUI. | **CONFIRMED** | `SleepLab/Tools/docker-build.sh app`, in the container Kira publishes binaries from. 235 204 bytes, `ID E4782CD726259DC6`. | 2026-08-18 |
| P14 | The three builds — ARM `.uapp`, host tests, TouchGFX simulator — do not agree on which sources exist or which libc functions do. | **CONFIRMED**, twice, and each time only one build could catch it | 2026-08-18. The ARM build globs `Sources/*.cpp`; the simulator's Makefile enumerates them, so a new source packs into a `.uapp` and fails to link the simulator. And `timegm` links under the host tests and the simulator (both glibc) and does not exist in the watch's newlib, so a date routine compiled everywhere except the build that ships. **All three have to be run before a branch is believed**, which is what `docker-build.sh app`, `tests` and `sim-run` are for. | 2026-08-18 |
| P12 | `SDK::MessageBase` deletes copy-assignment, so a message payload the GUI needs to keep must be a separate plain struct. | **CONFIRMED** | Build failure: `use of deleted function 'SDK::MessageBase& operator=(const SDK::MessageBase&)'`. Correct behaviour — a message is a pooled block with an identity, and copying one would produce a second object claiming the same slot. `SleepReportData` is now separate from `SleepReport`. | 2026-08-18 |
| P13 | `SleepReport` at 100 strip buckets is 216 bytes and fits the pool block; at 200 it did not. | **CONFIRMED** | Measured with `sizeof` in the host container. `MessageBase` is 40 bytes, not the 32 the older examples' comments imply. The bucket count is set by the panel (200 px usable / 2 px per bucket) rather than by the message size, and fitting is the happy consequence. | 2026-08-18 |

---

## 2. Sensors — what the Tier 0 probe is for

**A two-minute hardware run on 2026-08-18 settled four of these**, from the
probe's own screen rather than from a log. **A full night on 2026-08-19 settled
six more**, and its numbers are quoted in the rows below.

The night: probe 0.1.1, run 13 of `probe_log.csv`, started 2026-08-19 04:15:50Z.
**8 h 26 m, 507 rows, unbroken** — worn, unplugged, `hr=continuous`, defaults
otherwise. Diary: lights out 00:33 EDT, woke 08:00 EDT, with a Garmin Fenix 6 Pro
worn alongside reporting 00:37 and 08:02. The only `charging` row is row 507 of
507 at 08:42, which is the cable going in to pull the log, so the night itself
never saw it.

**What the night could not do is settle anything about sleep.** The probe records
no activity counts (S13), so the best-referenced night this project will get — a
diary *and* an independent commercial device — cannot be scored. That is the cost
of the split the `POST-MORTEM.md` merge recommendation is about.

Each row names the column of `probe_log.csv` that settles it;
`Tools/probe_report.py` prints them grouped the same way.

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
| S1 | A Service keeps receiving sensor batches through a whole night of screen-off low-power operation. | **CONFIRMED** | 2026-08-19, the 8 h 26 m night. **No gap anywhere**: row span min 55.9 s, median 60.0 s, max 60.0 s, none over 66 s, across 507 contiguous rows with no run boundary inside them. Accelerometer **0/507 empty rows**, heart rate 0/507, `HEART_RATE_EX` 0/507. This is the row every other row was built on, and the one whose failure would have made the rest wasted. It did not fail. |
| S2 | Continuous optical HR costs less than about a third of the battery overnight. | **CONFIRMED** as worded; the *split* still wants the second night | 2026-08-19, `hr=continuous` for 8.45 h. `batt_mah` **216 -> 206, so 10 mAh**, at a mean current of 1.33 mA (magnitude only — the sign is per an unverified firmware contract). Ten of 216 mAh remaining is **~4.6 % of the pack for the whole app including heart rate**, so HR's own share is necessarily below that and the claim as worded is comfortably true. What the `"hr": "off"` night still buys is the breakdown: how much of the 4.6 % is HR rather than the accelerometer and the loop. `batt_pct_x10` cannot answer any of this — see S18. |
| S3 | The delivered accelerometer rate is close to the requested one. | **REFUTED** | 2026-08-18, on hardware: **2875 accelerometer samples in a minute against a requested 40 ms period**. That is ~48 Hz delivered where 25 Hz was asked for -- nearly *double*, and in the opposite direction to the thinning the simulator's gate does. Heart rate in the same minute delivered exactly 60 samples against a requested 1 s period, so the period is honoured there and not here; the likeliest reading is that the accelerometer runs at a native rate it will not go below. **Nothing downstream is wrong because of it**: EpochCounter is rate-independent by construction and a host test pins that across a 4x span, which is precisely the design decision this measurement justifies. What it does cost is roughly double the IPC and sample-path power that was budgeted for. |
| S3a | The requested accelerometer period does anything at all. | **UNVERIFIED** | Follows from S3. Ask for 80 ms and 20 ms in two short runs and compare `acc_n`: if all three land near 2875, the period is ignored and `kAccelPeriodMs` is decoration. That matters for power, since it decides whether the sample rate is a lever or a given. |
| S4 | `SPO2` (0xF1) produces at least one sample. | **REFUTED** | 2026-08-18, on hardware. It does not even resolve a driver: `spo2` defaults to on, so `connect()` was called and returned false, and the screen showed lower-case `o`. There is no firmware producer to ask. Nothing is built on SpO2 and now nothing can be. |
| S5 | `HEART_BEAT` (0x40) still emits nothing on 1.4 firmware. | **CONFIRMED**, and more strongly than expected | 2026-08-18, on hardware. It does not resolve a driver at all — lower-case `b` on the screen — so the question is not "does it emit events" but "there is nothing to subscribe to". UNA answered "no events at all" against the 1.3 line (PR #167); 1.4 has not changed it. **The staging clause in §3 stands**, and the HRV fields stay reserved and absent. Re-check after any firmware bump: UNA describe a higher-rate PPG mode and on-chip HRV as things they are working on, so this row has an expiry date even though it is CONFIRMED today. |
| S6 | The PPG waveform is 20 Hz, single channel. | **UNVERIFIED**, and possibly unreachable | UNA maintainer, PR #167, 2026-07-01. On the 2026-08-18 run `ppg` was off in config, so the lower-case `p` proves nothing -- but given `HEART_BEAT` and `SPO2` both failed to resolve, there is a real chance `PPG` has no app-facing driver either. Settle it with `"ppg": "on"` for one short run and read the block: an upper-case `P` means a driver exists, lower-case means the raw waveform is not available to apps at all, which would close the on-device HRV route for good. |
| S12 | `TOUCH_DETECT` publishes on a clock, so an epoch with no samples means "not worn". | **REFUTED**, and it was a bug | 2026-08-18, on hardware: it resolved (upper-case `T`) and delivered **zero samples in a minute**. It is an event sensor — it publishes on a change of state. SleepLab read a sample-less epoch as 0 % worn, which put every epoch below the scorer's worn floor, which made every epoch Unscorable, which would have reported **every night as NOT WORN** with its numbers suppressed. Fixed: worn state is sticky across epochs now, and a sensor that never speaks at all yields `Uncertain` with its own reason rather than `NotWorn` — telling somebody their watch was not worn would send them to put on a watch they are already wearing. Found in two minutes of hardware, by the app that exists to find it. |
| S13 | A Tier 0 probe night can set `WornGate::kMicroMovementFloor`, as its TODO claimed. | **REFUTED** | 2026-08-18, by reading `ProbeLog.hpp` against that TODO. The probe records `acc_n`, `acc_ts_span_ms`, `acc_max_gap_ms` and `acc_batches` — delivery statistics — and **no activity counts at all**, so there is no count distribution in a probe night to put a floor between. `kMinPlausiblePct` inherited it by reference. **Demonstrated in practice 2026-08-19**: a clean 8 h 26 m night with a hand-kept diary and a Garmin Fenix 6 Pro worn alongside — the best-referenced night this project is likely to get — and it cannot be scored, because the columns a scorer needs were never written. Corrected: both now name SleepLab, whose epoch row carries `count` and `peak`. **Scope, stated carefully because the first version of this row overstated it:** `ROLLOUT.md` phases 3 and 4 *already* assigned the movement thresholds to SleepLab nights fed through `night_report.py thresholds`, so the rollout plan was right and one comment was wrong. `kMovementFloor`, `stillnessCountMax` and `activityCountMin` said only "a diary-validated recording" and never named the probe; they now name SleepLab and the phase, so the confusion cannot spread again. And `kMinWornPct` legitimately *does* point at the probe: `touch_n`, `touch_worn_n` and `touch_edges` are exactly a worn fraction and a flicker rate. | 2026-08-18 |
| S16 | The probe's nights can be dropped in favour of SleepLab's. | **REFUTED** | 2026-08-18. Checked after S13 was written too broadly. The probe uniquely records `batt_mv`, `batt_ma_x10`, `batt_avg_ma_x10` and `batt_mah` — SleepLab records battery *percent* only — so **S2 (what continuous HR costs) is answerable coarsely from SleepLab and precisely only from the probe**. The probe also records `wakes` and `msgs` per row, which is the whole of S10, and SleepLab records nothing about loop wakes. **Corrected 2026-08-19**: this row first said S2 was "answerable coarsely from SleepLab and precisely only from the probe". It is not answerable from SleepLab at all — the percent gauge did not move across the whole night (S18), and the columns that did move are on a channel SleepLab does not subscribe. So `ROLLOUT.md` phase 1 stands as written: run the probe's nights first. What changes is only which app records the *count* distributions (S13). Merging the probe's delivery columns into SleepLab behind a setting would collapse the two, and `POST-MORTEM.md` § "Should the probe and SleepLab stay separate?" has that recommendation with its arithmetic — but it is not built, so until it is, the probe's nights are not optional. | 2026-08-18 |
| S7 | `TOUCH_DETECT` holds "worn" for a loosely-strapped sleeping wrist without flickering. | **CONFIRMED**, emphatically | 2026-08-19. **One touch sample in 507 rows** — 506/507 rows empty — reporting worn, and **zero worn/not-worn transitions, 0.0 per hour, 0/507 rows containing one**. The feared failure was thirty flickers an hour suppressing every night; the measured rate is none. It also confirms S12's mechanism across a night rather than a minute: an event sensor with nothing to report says nothing for eight and a half hours. **And it makes the sticky-worn fix load-bearing rather than a refinement** — without it, 506 of 507 epochs read 0 % worn, every epoch goes Unscorable, and the night reports NOT WORN with its numbers suppressed. Exactly as this row predicted. |
| S8 | Nothing else on the device contends for the HR sensor. | **CONFIRMED** for the no-other-app case | 2026-08-19, with no other app installed: `hrex_opt` **30 169**, `hrex_ext` **0**, `hrex_unk` **0**. Every one of 30 169 arbitrated readings came from the wrist optical sensor, none from a strap, none unattributed — so nothing contended and provenance is unambiguous. The "then again with one installed" half is untested, and it is the half that governs publishing SleepLab alongside Sleep Probe, which is why the catalogue still carries only one of them. |
| S9 | The user volume has room for a decade of nights, and sustained append throughput is not a constraint. | **CONFIRMED** | 2026-08-19: **507 rows over 8.45 h with no write failure**, ~74 KiB for the night. The open-seek-write-flush-close cycle sustained one row a minute all night against the real filesystem. SleepLab writes at twice the cadence and twice per epoch — a row plus the state file — so ~1 900 cycles a night against the probe's 507; the claim is confirmed at the probe's rate and inferred at SleepLab's. Free space was never in doubt: `MapManager` CRC-verified 160.5 MiB of map packs on this volume. |
| S10 | An idle service that sleeps to its next deadline does not wake often enough to matter. | **CONFIRMED**, though the number is higher than expected and S17 is why | 2026-08-19: **220 887 wakes, 220 349 messages, 436 wakes/row** — under the report's 2000/row spinning-loop threshold, and, the part that actually settles it, **435 of every 436 wakes carried a message**. The loop is not waking spuriously, it is being fed: 308 accelerometer batches a minute (S17) plus 60 HR plus 60 `HEART_RATE_EX` plus battery accounts for essentially all of it. And the whole night cost 10 mAh (S2), so the chattiness is real and does not matter. |
| S14 | The activity count is independent of the delivered sample rate, so one night's counts are comparable with another's. | **REFUTED as stated; true only in the upper half of the range** | 2026-08-18, measured in a host test (`EpochCounter.TheCountIsOnlyRateIndependentInTheUpperHalfOfTheRange`). Counts per 60 s scoring epoch against a fixed 0.5 Hz sinusoid: **291 at 96 Hz, 286 at 48, 277 at 25, 259 at 12.5, 221 at 6, 212 at 4.8, 149 at 2**, and 0 below ~1.9 Hz, where every dt exceeds `kMaxGapMs`. So halving the delivered rate costs ~9 % of every count in the night and a tenth of it costs 26 %, **in the direction that reads as a quieter night, which reads as more sleep**. The cause is the filters rather than the integration: a one-pole high-pass re-coefficiented as `a = tau/(tau+dt)` stops approximating its continuous form once dt approaches tau, and at dt = 0.5 s the 0.25 Hz corner attenuates its own passband threefold. The dt-weighted integral does prevent the *proportional* failure the header warns about. Consequence: **two nights are comparable only if their delivered rates are close**, so `provenance.acc_hz_x10`, `acc_samples_min` and `acc_samples_median` are now in every summary. A proper fix is a bilinear-transform biquad, which the header rejected on cost. | 2026-08-18 |
| S15 | The thin-epoch guards would notice delivery degrading rather than stopping. | **REFUTED** | 2026-08-18, by an offline-harness night at a tenth of the delivered rate: it passed both guards silently. `kMinSamplesPerRecordingEpoch` is 60 and `SleepWakeScorer::kMinSamplesPerEpoch` is 120, both set against a **nominal 25 Hz** while the hardware delivers ~48 (S3) — so they fire only below about 4 % of the delivered rate, while the counts are already 26 % low at 10 % of it (S14). Both constants' TODOs already say "set from the delivered-rate column of the first two probe nights", and that rate is now CONFIRMED, so the measurement exists. **Deliberately not changed here**: tightening to a fraction of 48 Hz would blank every night if the hardware ever honoured the 25 Hz it was asked for, which is exactly the "too tight and real nights are suppressed" failure. The mitigation taken instead is recording the rate rather than gating on it. | 2026-08-18 |
| S17 | The requested accelerometer batch latency does anything. | **REFUTED** | 2026-08-19. `accel_latency_ms` was 5000, which at the delivered 49.44 Hz should give ~12 batches a minute of ~247 samples each. Measured: **156 377 batches over 507 rows = 308 a minute, 9.6 samples each, ~195 ms apart.** So the latency is ignored exactly as the period is (S3), and the sample path costs **~26x the IPC the comment on `kAccelLatencyMs` budgeted for** — "without batching, 25 Hz is 25 IPC wakes a second" understates it; batching is happening but at 195 ms, not 5 s. This is the whole of S10's 436 wakes/row. It cost 10 mAh over the night (S2), so it is a finding about the budget being wrong rather than about the battery. Together with S3 and S3a: **the accelerometer honours neither the period nor the latency it is asked for.** |
| S18 | `batt_pct_x10` can measure overnight battery use. | **REFUTED** | 2026-08-19. The gauge read **100.0 % at the start of the night and 100.0 % at the end**, across 8.45 h in which `batt_mah` fell 216 -> 206. It is not a slow gauge, it is non-functional for this purpose — it did not move at all. `batt_mah` and `batt_avg_ma_x10` are the columns that work, and they are on the `BATTERY_METRICS` channel (the probe's `E`), which **SleepLab does not subscribe**. So S2 is not answerable from a SleepLab night at any precision, coarse or otherwise, which is a correction to what S16 first claimed. |
| S11 | The sample-rate thinning rule (S3) applies on hardware and not only in the simulator. | **UNVERIFIED** | The rule is CONFIRMED *for the simulator* — pinned by `Tests/Host/simulator/SampleRateAdapter_test.cpp` on `una-sdk`'s `feat/sample-rate-adapter-rule`, which asserts the half-period boundary, the exact-ratio edge and the quantised bands. Whether the kernel's own gate behaves identically is not known. `acc_n` against `acc_ts_span_ms` on hardware is the check. |

---

## 2a. Tier 4 surfaces

| # | Claim | Tag | Method / what would change it | Date |
| --- | --- | --- | --- | --- |
| T1 | As of the 1.4 kernel, mute does not silence app-requested alerts — muting covers alerts the watch raises at the user, not feedback an app produces in a session it owns. | **UNVERIFIED**, and it is a five-minute experiment rather than a night | Stated upstream (PR #267, una-kernel#260) and not checked on this unit. **An alarm that fails silently is worse than no alarm**, which is why the alarm is off by default and the README says to test it on a weekend. To confirm: mute the watch, set `alarm_at` two minutes ahead inside the bedtime window, and wait. `playAlarm()` now writes an `alarm` line to `Debug/sleeplab.log` with the wall clock and which path fired, so the app's half of the question is answerable from the volume — what remains unreachable from any file is whether the kernel *delivered* it, because the app's record ends at `send()`. |
| T2 | The glance and the home widget work from a `Utility` app without changing its type. | **REFUTED as written, and it was two bugs** | 2026-08-18, by the offline harness. "The code builds" was the whole of the evidence and it was not enough. **The glance was never sent anything at all** — not stale content, none: setting a control's text invalidates it, the carousel's tick is the only thing that sends the form, the tick sends only when the form reports itself invalid, and `glanceRefresh()` marked the form *valid* at the end of building it. All five of the SDK's own Glance examples put that call after the send. **The home widget was never claimed**: `pumpWidget()` was called from the GUI handlers and from `openNight()` and not from `closeNight()`, so the only way to get a widget was to open the app and close it again — the one thing the widget exists to avoid. Both fixed and both pinned by harness scenarios that deliver ticks. Still not seen on hardware: the claim that the *kernel* honours a `Utility` app's glance and widget is untouched by this and remains untested. |
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
| A11 | The suppression is complete: a night that failed the gate yields no sleep figure on any surface. | **REFUTED, then fixed** | 2026-08-18, by an offline-harness night on a nightstand that TOUCH_DETECT reported as worn. The numbers were suppressed and **the epoch strip was drawn in full** — 100 buckets of per-epoch sleep/wake verdict and restfulness level, across the lower third of the report screen, under a caption telling the reader it came from their movement and heart rate. `RestfulnessBand::compute` runs before the gate is consulted, so the arrays were filled and the strip was built from them. A picture of the claim is the claim. The same test found that a night *in progress* drew the previous night's strip, or on a fresh install a strip built from zeroed memory — `Verdict::Sleep` and `Restfulness::Unknown` are both zero, which the widget draws as a solid block of the most settled tone, so a first-ever night reported itself live as unbroken deepest sleep for every minute so far. Both now gated on the one condition that means "there are verdicts for the night on this screen". | 2026-08-18 |
| A12 | Heart rate is reported for a night that failed the gate, and that is correct. | **CONFIRMED**, and checked for leaks | 2026-08-18. `hrMinX10` and `hrMeanX10` are filled whatever the gate says, deliberately: they are measurements of the sensor rather than claims about sleep, and a failed night is exactly when somebody needs to know whether the optical path produced anything. Traced downstream for the leak this could cause: the index row carries the real HR for a not-worn night, `loadBaseline` excludes every row whose worn verdict is not `Worn` (pinned by two host tests), the glance shows `--` for a night without sleep, the home widget is not claimed at all, and the history list prints "not worn" with no columns. So the HR figure does not become a baseline sample or a delta. | 2026-08-18 |
| A6 | Respiratory rate from a wrist accelerometer at these rates is not defensible. | **CONFIRMED** | Not shipped. Recorded here so the absence is a decision rather than an oversight. |
| A8 | The Cole-Kripke weights, P, and the Webster rescoring thresholds implemented here are the published ones. | **LIKELY**, and the check found one error | 2026-08-18. Checked against two independent reference implementations — [pyActigraphy](https://ghammad.github.io/pyActigraphy/_autosummary/pyActigraphy.sleep.ScoringMixin.CK.html) and [`actigraph.sleepr`](https://github.com/dipetkov/actigraph.sleepr/blob/master/R/apply_cole_kripke.R), which both cite p. 466 of Cole *et al.* 1992 — and **not** against the papers themselves, which is why this is LIKELY rather than CONFIRMED. Cole-Kripke came out clean: the seven weights, P = 0.001, the window (four back, two forward), and *sleep below the threshold*, all agree. **Webster's rule 4 was wrong**: it is "a sleep bout of ≤ 6 minutes surrounded by at least **15** minutes of wake" and it was transcribed as 10. Ten is a looser precondition, so the rule fired on patterns the published one leaves alone and short sleep bouts became wake — *against* actigraphy's own bias, which is why it would have read as conservatism rather than as a bug. Rules 1, 2, 3 and 5 were correct. The whole block is now pinned by value **and** by behaviour in `PublishedConstants.*`, and the threshold's *direction* has a test of its own, because one widely-read reference states it the other way round and inverting it inverts every verdict in every night. |
| A10 | The device-to-paper count bridge should saturate, as the reference implementations' does. | **UNVERIFIED**, and deliberately not implemented | 2026-08-18. `actigraph.sleepr` maps ActiGraph counts with `min(axis1 / 100, 300)` — a scale *and a ceiling*. `kCountScale` is linear with no ceiling, so one violent minute can dominate the Cole-Kripke windows of the four epochs after it and the two before it, manufacturing wake at every turn-over. Not added: a ceiling is only meaningful in the units `kCountScale` bridges to, and that constant is itself a guess (A9) — a ceiling derived from a guess is a second guess wearing the first one's authority. It goes in with the A9 calibration, off the same ten diary nights. | 2026-08-18 |
| A9 | `kCountScale`, which bridges this device's count units to the units Cole-Kripke was fitted against, is correct. | **UNVERIFIED** | **It is a guess**, and it is the single number standing between "cites a real paper" and "is validated". It cannot be derived, only measured. See the validation table below and the TODO on the constant itself. |
| A7 | Skin temperature is not available. `AMBIENT_TEMPERATURE` (0x70) is ambient. | **CONFIRMED** | The SDK's own type name and its doc comment. Never labelled as body temperature anywhere in this app. |

---

## Validation status of every reported metric

The table SleepLab's README points at. Filled in as each tier lands; a metric
with no row here is a metric the app must not print.

| Metric | What it actually measures | Validation | Known failure modes |
| --- | --- | --- | --- |
| **activity count** | Integrated band-limited (0.25–3 Hz) acceleration per axis over the epoch, combined as the vector magnitude of the three integrals, in units of g·s × 1000. | *synthetic-only.* Host tests pin stillness near zero, linear scaling with amplitude, the band's rejection of 0.05 Hz drift (20×) and 12 Hz shock (15×), identical response on every axis, and rate-insensitivity **across a 4× span in the upper half of the delivered range only** — the earlier wording claimed independence and it is not independent; see S14. The physical scale is now measured rather than implied: at ~48 Hz a 60 s epoch counts about 30 per 0.001 g of 1 Hz sinusoidal amplitude, so **every threshold in this app lives between 0.3 mg and 9 mg of mean band-limited wrist acceleration**. | A delivery gap contributes nothing rather than a fabricated rectangle, but an epoch built from a handful of samples still integrates to near-zero — which reads as perfect stillness. Guarded twice, at the recording epoch (`kDataGap`) and the scoring epoch (`Unscorable`), and both guards are an order of magnitude looser than they should be (S15). A single non-finite sample used to make every *subsequent* epoch exactly zero for the rest of the night; they are now dropped. |
| **sleep / wake per epoch** | Cole-Kripke over a ±window of counts, with Webster rescoring. | *synthetic-only, and the correspondence to sleep is unestablished* — see A9. | Systematically over-calls sleep: lying still awake scores as sleep. |
| **estimated total sleep time** | Epochs scored Sleep between onset and final wake. | *synthetic-only.* Exact against nights of known shape. | **Biased high**, direction known. Reported alongside `stillInBedMin` for exactly this reason. |
| **time in bed still** | Epochs between onset and final wake with counts below the movement floor. | *synthetic-only* for the arithmetic; the floor itself is a guess with a TODO. | Measures stillness, which is what was observed. Does not distinguish a still sleeper from a still reader. |
| **sleep onset / latency** | First epoch of the first run of 10 consecutive Sleep epochs; session start to there. | *synthetic-only,* and now end to end: the offline harness drives whole nights of known shape through the real recorder, which is what found the quarter-hour offset below. | Unscorable epochs break the run, so a delivery outage delays onset rather than declaring it. **Withheld entirely for a resumed night**, because latency is measured from a session start whose epochs were never scored. Until 2026-08-18 every reported onset and final wake was ~15 minutes early: the summary's epoch indices were relative to the scoring array and the clock arithmetic was relative to the backdated session start. Times of day now come from the clock each epoch recorded for itself. |
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
- **Probe night 1 of 3 is done and it went well** (2026-08-19): S1, S2, S7, S8,
  S9 and S10 all confirmed, S17 and S18 newly refuted. Nights 2 (`"hr": "off"`)
  and 3 (nightstand) remain — but see the thread below, because building the merge
  first may be worth more than running them next.
- **The probe's nights stand; the count distributions are SleepLab's.** S13 was
  first written as though the whole probe-first plan were misconceived, and it is
  not — S16 records why. The probe is the only instrument here for battery current
  and loop wakes, so `ROLLOUT.md` phase 1 is unchanged. What moves is only the worn
  and table *count* distributions, which phases 3 and 4 already took from SleepLab
  nights. `POST-MORTEM.md` has the merge recommendation that would collapse the two,
  with its arithmetic; it is not built.
- **Every remaining row in §2 is UNVERIFIED**, and the ones that are not about
  counts can still share a night. See `POST-MORTEM.md`.
- **The merge has overtaken G4 as the highest-value unbuilt thing.** S1 was the
  question that justified probe-first, and it is now CONFIRMED — so the reason to
  keep the two apps apart has weakened while the cost of keeping them apart has
  become concrete: on 2026-08-19 a diary-and-Fenix-referenced night produced no
  scoreable data (S13). Moving the probe's delivery columns into SleepLab behind a
  setting makes every subsequent night simultaneously a probe night and a
  calibration night. `POST-MORTEM.md` has the arithmetic; S18 adds
  `BATTERY_METRICS` to the list of channels that have to come with it.
- **G4 in `POST-MORTEM.md` remains the highest-value unbuilt *tool*.** Every summary now
  carries the constants that scored its night, so an old night is re-scorable; no
  tool re-scores one. A `night_report.py rescore` subcommand turns ten recorded
  nights into an unlimited number of calibration experiments, which is the
  difference between one attempt at A9 and as many as you like.
- **A3, A4 and A9's thresholds are unjustified numbers with TODOs against them**
  in the code. Each carries a comment naming the recording that would justify
  it. The largest is `kCountScale`: sweep it against ten diary-validated nights,
  report the mean signed error and spread on onset and final wake at each value,
  and put the minimising value in the code and the residual error in the README.
- **The diary has not been started.** §6 of the brief asks for at least ten
  nights of hand-recorded lights-out and wake times. That is the only thing that
  can turn any row in the validation table from *synthetic-only* into
  *diary-validated*, and it can be started the same night as the first probe run.
- **The adversarial nights have not been run**, but their *shapes* now all exist
  as offline fixtures rather than as intentions. `Tests/Pipeline_test.cpp` drives
  the real recorder through a sleeper who never settles, one already settled at
  launch, a forty-minute delivery outage, delivery thinned tenfold, a twenty-hour
  session, a sensor clock reset backwards, a wall clock jumped an hour, an hour on
  the charger, a worn sensor silent all night, one flickering sixty times, a volume
  that fills at 03:00, a summary that cannot be written, a five-minute loop stall,
  and three nights across the uptime wrap. A night takes 110 ms.

  What a real night still adds is the only thing it ever could: **whether the
  thresholds put real data on the right side of them.** Every fixture proves the
  arithmetic and nothing about a person.
