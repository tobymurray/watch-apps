# Prompt: Implement an honest sleep-tracking utility for the UNA Watch

You are an expert embedded C++ engineer who is also literate in sleep science and
actigraphy validation. Your task is to design and build **SleepLab**, a background
`Utility` app for the UNA Watch that records a night of wrist data and turns it into
a defensible sleep report. It lives in this repository (`watch-apps`), out of tree,
built against an SDK checkout found through `$UNA_SDK`.

Two things make this app harder than it looks, and they set the shape of the work:

1. **Nothing on this platform has ever run all night.** Every app in both repos is
   either foreground-interactive or a background task measured in minutes. Whether a
   service keeps its sensor subscriptions, its file handles and its battery for eight
   hours is *unmeasured*. Measure it before designing on top of it.
2. **The sensor set cannot support consumer-style sleep staging today, and the honest
   version of this app says so** — while being built so that the one capability that
   would change that (overnight HRV, which UNA is actively working on) drops in
   without a rewrite. See §3.

Work incrementally, tier by tier, with host-testable algorithms and an explicit
feasibility ledger. Do not fake precision the sensors cannot support.

---

## 0. Context: read these first

### In `una-sdk`, on the `research` branch

`git show research:RESEARCH-INDEX.md` — **read this first.** It is the fork's
consolidated record of every investigation, and three of its entries are directly
load-bearing for this app:

- **`Docs/Investigations/2026-06-15-heart-beat-vs-ppg/`** — the authoritative answer,
  from a UNA maintainer, on what the optical sensor can produce. Read all of it
  before designing anything HR-shaped; §2 and §3 below are downstream of it. It also
  carries `BeatProbe.hpp` plus `USAGE.md` and an integration patch: a runnable
  90-second diagnostic that re-asks the question empirically. **Firmware moved to
  1.4 on 2026-08-17, so re-running that probe is now a live Tier 0 task, not a
  someday one.**
- **`Docs/companion-data-channel-analysis.md`** — third-party watch apps have no
  supported way to receive companion data. There is no phone-side settings screen for
  this app, and there will not be one. Settings come from the watch UI or from a JSON
  file dropped in the app folder — the precedent is `Barcode` in this repository,
  which does exactly that for the same reason.
- **`Docs/Investigations/2026-07-29-hardware-config-recovery/`** — the hardware ledger,
  and the **verification convention this app must adopt**: every claim tagged
  CONFIRMED / LIKELY / UNVERIFIED / REFUTED with the corroborating method that earned
  the tag. Its `prototype/una_ble_client.py` is a working, validated phone-free BLE
  client that pulls `.fit` files off the watch — see Tier 4 on retrieval.

### In `una-sdk`, on `main` / `apps-v1.4.0`

- `Docs/platform-overview.md`, `Docs/sdk-overview.md`, `Docs/architecture-deep-dive.md`
  — the dual-process app model (headless **Service** ELF + TouchGFX **GUI** ELF packed
  into one `.uapp`). The power-state material in the deep dive is block diagrams, not
  a contract; treat it as a hypothesis to test, not documentation.
- `Docs/SensorsLayer.md` and `Libs/Header/SDK/SensorLayer/SensorTypes.hpp` — the type
  table. The header is authoritative; the doc's table is already behind it.
- `Examples/Apps/Alarm/` — **your closest template**: `Utility`, `APP_AUTOSTART On`,
  reads wall-clock time, sleeps its loop until its next scheduled work, and raises an
  alert. `Software/Libs/Sources/Service.cpp:12` is how an app gets local time at all.
- `Examples/Apps/Timer/` + `Docs/Examples/Timer-Architecture.md` — the deadline-bounded
  sleep idiom (size the message wait to the next thing that must happen), which is
  exactly what an epoch loop wants. `Examples/Apps/Stopwatch/` is the minimal
  well-documented shell.
- `Examples/Apps/GlanceHR/` + `Docs/Examples/GlanceHR-Architecture.md` — service-only
  app driving a glance.
- `Docs/BLE-File-Transfer-Service.md` and `Docs/BLE-Services-Overview.md` — published
  upstream in 1.4 (PR #273): the wire protocol for reading files off the watch over
  BLE.
- `Docs/Tutorials/Sensors/`, `Docs/Tutorials/Files/`, `Docs/unit-testing.md`,
  `Tests/Host/support/KernelTestDoubles.hpp`, `Tests/Host/support/FakeFileSystem.hpp`,
  `Docs/Simulator.md`, `Docs/app-config-json.md`.

### In this repository

- `MapManager/README.md` — **read all of it.** The only written account of what a
  long-lived autostart service actually does on this device: USB connection terminates
  every running app and autostart relaunches on unplug; `getTimeMs()` is device uptime
  and survives an app restart; an idle service must sleep to its next due work rather
  than poll; and the loop's message wait, not the storage, sets I/O throughput. Its
  closing section on the three-`Utility`-app quick-menu cap is **1.3-only history** —
  1.4 replaced that menu with a scrolling list (see Reachability in §1).
- `FwDump/README.md` — resumable long-running background work, chunk-and-manifest file
  discipline, and confirmation that a service keeps working with the display blanked.
- `Squash/README.md` and `Squash/Docs/IMPLEMENTATION-PROMPT.md` — the recorder-first
  discipline (collect labelled data before believing any classifier), and the prompt
  style this document follows.
- `Barcode/README.md` — the JSON-file-in-the-app-folder settings pattern.

---

## 1. Platform constraints (hard requirements)

**SDK and firmware version — this changed on 2026-08-17.** `apps-v1.4.0` is released,
and the release assets include **`UnaWatch-Kernel_1.4.0.ota`**, so the 1.4 firmware
line has shipped. Target `apps-v1.4.0` (or `main`, which is currently the same
commit), **not** the `apps-v1.3.0` tag the older apps in this repository are pinned to.

**First action, before writing any code: confirm the watch is actually running 1.4
firmware** (BLE DIS Firmware Revision String, or the watch's own Settings screen). A
`.uapp` built against 1.4 carries `KERNEL_INTERFACE_VERSION` 3 and the kernel refuses
to run it on a 1.3 device. If the watch is still on 1.3, either apply the OTA or build
against `apps-v1.3.0` and accept these consequences, all verified against the tags:

| | on `apps-v1.4.0` (target this) | on `apps-v1.3.0` (fallback) |
| --- | --- | --- |
| Message sending | `SDK::send_msg` (PRs #219/#238/#260) | absent — needs `MapManager`/`Chrono`'s `SendMsg.hpp` backport, because 1.3's `allocateMessage` can't forward constructor arguments |
| Home-screen widget | `SDK/HomeWidget/HomeWidget.hpp` present (#231, ABI bump #236) | absent — Tier 4's widget is unbuildable |
| `-fcyclomatic-complexity` | opt-in (#232), nothing to work around | added unconditionally; a pristine checkout builds no app without the `check_cxx_compiler_flag` probe `MapManager`'s CMakeLists carries |
| SDK checkout location | build is location-independent (#234) | fragile |

Everything else this app needs is present in both: every sensor type in §2,
`SDK/Fit/FitWriter.hpp`, `SDK/JSON/JsonStreamWriter.hpp`, `SDK/Metrics/`,
`SDK/Utils/ClockTime.hpp`, `SDK/Glance/`, `SDK/Tools/CircularBuffer.hpp`.

**Runtime.** Cortex-M33 (STM32U5A5), hard-float, `-Os -fPIC -fno-exceptions
-fno-rtti`, C++17. **No exceptions, no RTTI.** Service 500 KB / 10 KB stack, GUI
600 KB / 10 KB stack — fixed-size buffers, no heap churn in the sample path. **Apps
do not create threads:** two single-threaded blocking message loops. Timers are
polled. IPC blocks are pooled and the **largest is 256 bytes** — `#pragma pack(push, 4)`
plus `static_assert(sizeof(Msg) <= 256)` on every custom message.

**Display.** 240×240, 8 bpp ABGR2222 (fixed `SDK::GUI::Color` palette — a subtle
multi-tone design will band), TouchGFX MVP, 10 fps. Buttons `L1 L2 R1 R2` =
simulator keys `1 2 3 4`.

**Two clocks, and you need both.**

- `kernel.sys.getTimeMs()` is **device uptime**: 32-bit, wraps at ~49.7 days, survives
  an app restart, resets only on device reboot. Use unsigned subtraction everywhere.
  This is your monotonic sample clock.
- Wall-clock local time comes from `time(nullptr)` + `localtime_r` (see Alarm). This
  is what "bedtime", "23:00" and "wake by 06:30" mean. It can jump — timezone change,
  host sync, DST — so never derive a duration from two wall-clock readings.
- **Stamp every epoch with both**, and record the (uptime, wall-clock) pair at every
  file flush. That pairing is the only way to stitch a night across a restart, and the
  only way to notice the clock moved under you.

**The USB rule, which is a product constraint and not a footnote.** Plugging in
terminates every running app; autostart relaunches on unplug. **A watch charging
overnight records nothing.** Subscribe to `BATTERY_CHARGING`, mark the night
interrupted, and say so on screen — never present a partial night as a whole one. The
README must tell the user to charge before bed, not during. It is also the reason
Tier 4's BLE retrieval matters: pulling files over BLE leaves the service running,
where plugging in to fetch them kills it.

**Reachability — fixed in 1.4, and worth knowing it once was not.** On the 1.3
firmware the on-watch quick-access menu showed only three `Utility` apps, filled
alphabetically, so a new utility could be installed, autostarting and working while
being impossible to open (`MapManager/README.md` records the discovery). **1.4 replaced
that menu with a scrolling list**, so every installed utility is reachable and the app's
name no longer decides whether its own screen exists. Two consequences remain: the app
must not *rely* on being opened — the nightly report is written as a file whatever
happens, and Tier 4's glance and home widget surface it without a launch — and if you
ever build the 1.3 fallback from §1's table, the cap comes back with it.

**Storage.** `SDK::Interface::IFileSystem` gives open/read/write/seek/flush/rename plus
directory enumeration. The app's own folder is its sandbox; `../SharedData/` is the
cross-app location (see MapManager). The user volume is large — MapManager CRC-verified
160.5 MiB of map packs on it — but measure free space rather than assuming, and cap
every writer.

---

## 2. Sensor contract, and what is genuinely unknown

Subscribe via `SDK::Sensor::Connection(Type, periodMs, latencyMs)`; data arrives
batched as `EVENT_SENSOR_LAYER_DATA`, parsed by the per-type parsers in
`Libs/Header/SDK/SensorLayer/DataParsers/`.

| Sensor | Type | Why a sleep app wants it |
| --- | --- | --- |
| `ACCELEROMETER` | 0x10 | The actual measurement. Float g, 3-axis. Your activity counts come from here. |
| `TOUCH_DETECT` | 0x140 | Worn / not worn. **Mandatory** — see §3.4. |
| `MOTION_DETECT` | 0xB0 | `NO_MOTION` / `MOTION` / `SIG_MOTION` events — a cheap kernel-side movement gate. |
| `ACTIVITY_RECOGNITION` | 0xC0 | `STILL` / `WALKING` / `RUNNING` + confidence — cheap corroboration for out-of-bed. |
| `HEART_RATE` | 0x41 | BPM + trust. Relative to personal baseline only (§3.3). |
| `HEART_RATE_METRICS_DAILY` | 0x42 | AHR/RHR the kernel already computes. |
| `HEART_RATE_EX` | 0x43 | Arbitrated + source (optical/external) + per-source BPM and trust — use it so HR provenance is recorded, not assumed. |
| `STEP_COUNTER` | 0x51 | Monotonic since boot; step deltas across a night are a strong out-of-bed signal. |
| `BATTERY_LEVEL` / `BATTERY_CHARGING` / `BATTERY_METRICS` | 0x120–0x122 | Overnight drain measurement (Tier 0) and the charging interruption above. |
| `AMBIENT_TEMPERATURE` | 0x70 | Ambient only. **Not skin temperature** — never label it as body temp. |
| `SPO2` | 0xF1 | Type and parser exist. Whether firmware produces anything is unverified. |

**Do not use:** GPS (indoors, pointless, expensive) or `FUSION_RAW`/gyro at 100 Hz for
a whole night — gyro buys nothing for actigraphy and costs everything (see Tier 1's
arithmetic).

**Delivery-rate hazard.** The requested period is not honoured naively. The
sample-rate adapter thins per-listener delivery on a boundary at *half* the expected
period, an exact ratio falls on the thinner side, and the thinning is quantised into
bands rather than proportional to rate — pinned by a test on `una-sdk`'s
`feat/sample-rate-adapter-rule`. Anyone assuming "roughly one sample per requested
period" is wrong by up to a factor of two. **Never infer elapsed time from a sample
count.** Derive every epoch boundary from timestamps and record the delivered rate you
actually observed.

### HRV: not today, but this app is the case UNA had in mind

From the maintainer answer recorded in the `research` branch investigation (PR #167),
which you must read in full rather than trusting this summary:

- `HEART_BEAT` (0x40) **emits no events at all** — HR detection is a frequency-domain
  algorithm, not per-beat detection. RR intervals cannot be read off beat timestamps.
- The PPG waveform is **20 Hz, single channel**, described by UNA as "at the low end
  for HRV extraction". That is a property of today's HR-measuring mode, not a hardware
  ceiling: a temporary higher-rate (higher-power) mode is "probably" the route, and
  on-chip HRV calculation is being explored. "Not today, but we are working on it."
- Optical HRV **will only ever work at rest** — mid-exercise HRV has to be an
  electrical measurement. That is physics. But the corollary is the important one
  here: *"Overnight or resting morning HRV is perfect for training
  readiness/recovery."* Overnight is the case optical HRV is good for.
- `RR_INTERVAL` (the SDK-side contract, open PR #220) has **no firmware producer**, and
  the kernel parses then discards the RR values a chest strap already sends. So even a
  Polar H10 worn overnight yields no RR to an app today.

**Consequences, and they are design requirements, not commentary.** Ship nothing that
depends on HRV. *And* build so HRV lands cheaply: the epoch record carries reserved,
documented HRV fields written as absent; the scorer takes an optional HRV channel it
currently never receives; the summary JSON declares which channels contributed. Then
re-running `BeatProbe` after a firmware bump is a measurement, not a redesign.

**Unverified — Tier 0 work, not design input:**

- Does a Service keep receiving sensor batches through a whole night of screen-off
  low-power operation, or does delivery degrade or stop?
- What does continuous optical HR cost in mA, and does a long requested period
  duty-cycle the LED or leave it running?
- Does `SPO2` emit anything at all? Does `HEART_BEAT` still emit nothing on 1.4?
- Does `TOUCH_DETECT` reliably report "worn" for a loosely-strapped sleeping wrist,
  and how often does it flicker?
- Does anything else on the device — another autostart app, the kernel's own daily
  metrics — contend for the HR sensor, and what does arbitration do?
- Free space and sustained append throughput on the user volume.

---

## 3. Sleep-science constraints — the honesty contract

This section is not advisory. It is the specification.

1. **Actigraphy is the modality you have.** Validated wrist algorithms (Cole–Kripke,
   Sadeh, Oakley-style) score **30- or 60-second epochs of activity counts** into
   sleep/wake. Against polysomnography they achieve high *sensitivity* to sleep
   (~85–95 % of true sleep epochs scored correctly) and poor *specificity* to wake
   (frequently 40–60 %): they systematically mistake lying still for sleeping. The
   report must own that asymmetry — "time in bed still" is supportable, "time asleep"
   is an estimate with a known bias direction.
2. **Do not ship a stage hypnogram as fact.** Light/deep/REM discrimination rests on
   HRV plus multi-channel PPG, and you have neither (§2). You may compute a
   *movement-and-HR-based restfulness band*, and you may draw it, but it must be
   labelled as a relative index derived from movement and heart rate, its method must
   be stated on screen and in the summary file, and it must never be called REM or
   deep sleep or given minutes-in-stage figures. When overnight HRV arrives, revisit
   this clause with the literature in hand — and revisit it in the ledger, in writing,
   rather than quietly relabelling a band as a stage.
3. **Heart rate is legitimate only relative to the wearer's own baseline.** Nocturnal
   HR minimum, time-to-minimum, and the morning rise are real, useful and personal.
   Absolute thresholds copied from a paper are not. Build the baseline from this
   user's own recorded nights, report deltas, and refuse to report any delta until
   enough nights exist (state the number; start at 5).
4. **Off-wrist is the failure mode that discredits everything.** A watch on the
   nightstand is perfectly still and reports a flawless night. Gate every sleep claim
   on worn-detection *plus* a plausibility check — a real wrist produces micro-movement
   and a valid HR reading, a table produces neither. A night failing the gate is
   reported as *not worn*, never as sleep.
5. **No skin temperature, no SpO2 until proven, no respiratory rate.** Respiration from
   a wrist accelerometer at these rates is not defensible; do not ship it.
6. **Every threshold is a named constant with a comment stating what data justified
   it** — or a `TODO` naming the recording still needed. This matters more here than in
   `Squash`, because sleep has no visible ground truth to catch you out.
7. **State the disagreement rate you cannot measure.** You have no PSG. Say so, in the
   README and the ledger, and describe what your numbers are: derived from movement and
   heart rate, validated against a self-reported diary and internal consistency, not
   against a sleep laboratory.

---

## 4. Product specification, in tiers

App identity: `APP_TYPE "Utility"`, `APP_AUTOSTART On`, `DEV_ID "UNA"`,
`APP_NAME "SleepLab"` (rename freely — but move this document with it),
`APP_USER_NAME "Sleep Lab"`. Derive `APP_ID` the
way the other apps here do: the first 8 bytes of
`sha256("https://github.com/tobymurray/watch-apps#sleeplab")`. Layout is the standard
out-of-tree app root — `Software/Libs/{Header,Sources}`,
`Software/Apps/SleepLab-CMake/CMakeLists.txt`, `Software/Apps/TouchGFX-GUI/` with
`simulator/gcc/Makefile`, `Resources/icon_{60x60,30x30}.png` (flat teal / flat gray
silhouette, no gradients — the framebuffer is 2 bits per channel).

**Each tier must be working, tested and honest before the next one starts.**

### Tier 0 — Feasibility probes, and a ledger that records their answers

Before any product code, answer every unverified question in §2 empirically, on
hardware, and write each answer into `Docs/FEASIBILITY-LEDGER.md` with its date,
method and a CONFIRMED / LIKELY / UNVERIFIED / REFUTED tag — the convention the
hardware-recovery investigation established. Build the smallest possible probe service
(fork `Timer`'s or `Alarm`'s shell) that subscribes to the candidate sensors, appends
one line per minute carrying uptime, wall clock, per-sensor delivered sample counts,
battery level and charging state, and otherwise sleeps.

Run it for a full night, unplugged, worn. Then a second night on a table. Deliverables:

- Measured overnight battery cost, with and without continuous HR, in percent and mA
  (`BATTERY_METRICS` reports current directly). If HR-all-night costs more than roughly
  a third of the battery, HR sampling must become periodic — design for that answer
  before you know it.
- Delivered vs requested sample rate per sensor, over hours rather than minutes.
- Whether delivery survives the whole night uninterrupted, and if not, where it stopped
  and what the log looked like there.
- Whether `SPO2` produced a single sample.
- **`BeatProbe.hpp` re-run on 1.4 firmware** (patch and usage guide are in the research
  branch): does `HEART_BEAT` still emit nothing? This is a 90-second wear test and it
  is the single highest-value probe here, because a yes reopens §3.2.
- `TOUCH_DETECT` on a sleeping wrist vs a table: flicker rate, false-worn rate.
- Free space and sustained append throughput on the user volume.

This tier is not overhead. Every later tier depends on these numbers, and a sleep app
that discovers in month two that its sensors stop at 02:00 has wasted month one.

### Tier 1 — The recorder (ship this first, and ship it solid)

An autostart service that records epochs all night and never loses a night to a
restart.

- **Epoch pipeline:** accelerometer samples → per-epoch activity counts. Implement a
  documented count derivation (band-limited magnitude deviation from the gravity
  vector, aggregated per epoch, in the manner of the published count-generation
  methods), epoch length a named constant starting at 30 s, derivation testable in
  isolation. Record per epoch: max/mean count, `MOTION`/`SIG_MOTION` event counts, step
  delta, HR mean/min/valid-sample-count with its source, worn fraction, battery, both
  clocks, and the reserved HRV fields from §2.
- **Storage arithmetic, up front.** A 30 s epoch is 960 epochs a night; at ~48 bytes a
  row that is ~46 KB per night, ~17 MB per decade — free. A raw accelerometer CSV at
  25 Hz costs ~1.1 KiB/s (scaling `Squash`'s measured ~4.3 KiB/s at 100 Hz), i.e.
  **~31 MB for eight hours**. So: epochs always, raw never by default. Offer a raw
  research-recording mode as a setting, capped by both bytes and duration the way
  Squash's recorder is, for developing sample-level work.
- **Restart survival is a first-class requirement.** Follow `FwDump`: append, flush and
  close on a bounded cadence; keep a small state/manifest file rewritten after each
  flush; on start, detect an in-progress night and resume it rather than opening a
  second one. The (uptime, wall-clock) pair at each flush is what lets a resumed file
  be stitched honestly, including across a device reboot.
- **Settings** come from the watch UI plus a JSON file in the app folder, following
  `Barcode`'s precedent — bedtime window, wake deadline, smart-alarm window, HR duty
  cycle, raw-recording toggle. There is no companion channel and there will not be one.
- **Idle behaviour:** compute the wait to the next epoch boundary and pass it to
  `getMessage`, the way `Timer` and `Alarm` do. This service runs for the device's whole
  life; a polling loop is a permanent battery tax. Conversely, remember MapManager's
  lesson — when you do bulk I/O, do a *budget* of work per wake, not one chunk.
- **Session boundaries:** a night opens on a configured bedtime window plus sustained
  stillness while worn, and closes on sustained activity or steps after a minimum
  duration, or on leaving the window. Windows cross midnight — write those tests first.
- Per-night outputs in the app folder: the epoch log (CSV, documented header) and a
  summary JSON via `SDK::JSON::JsonStreamWriter`, both interpretable by someone holding
  only the README.

### Tier 2 — The sleep/wake engine

Pure C++17, zero SDK includes, clock and samples injected, fixed-size buffers, fully
exercisable in host tests from CSV fixtures. Suggested split: `EpochCounter`,
`SleepWakeScorer`, `NightSegmenter`, `WornGate`, `BaselineStore`.

- Epoch scoring: implement a **named published algorithm** — state which, and cite its
  epoch length and coefficients beside them in the header — with the rescoring rules
  that belong to it, rather than an invented heuristic.
- Derive and report, using the standard actigraphy definitions and names: sleep onset
  and latency from the bedtime marker, final wake, total time in bed, estimated total
  sleep time, WASO, awakening count and lengths, sleep efficiency, movement index.
- `WornGate` and the plausibility check from §3.4, with the not-worn verdict a
  first-class output that *suppresses* the sleep numbers rather than annotating them.
- Restfulness band from movement plus HR-relative-to-baseline per §3.2, labelled, with
  the method string emitted into the summary JSON so every file is self-describing.
- The optional HRV channel from §2, plumbed and never fed.

### Tier 3 — Reporting on the watch

- Morning summary: asleep-at / woke-at, estimated sleep time, efficiency, awakenings,
  restfulness. If the night was interrupted — charging, not worn, clock jump, resumed
  after a restart — that is the first line, not a badge.
- A per-epoch strip across the night, drawn in the 4-level palette, legible at 240×240.
- History from the per-night JSONs, with the personal baseline and the delta rules from
  §3.3, including refusing to show a delta until enough nights exist.
- Buttons follow the house pattern (`R2` = back). The screen is a report; keep
  interaction to navigation.

### Tier 4 — Now shippable on 1.4, each still behind its own gate

- **Smart alarm:** `SDK::Message::RequestVibroPlay` / `RequestBuzzerPlay`
  (`SDK/Messages/CommandMessages.hpp`) at the first wake-ish scored epoch inside a
  user-set window before a hard deadline, falling back to the deadline. Two facts you
  get for free: Alarm's service already does this from a service with no GUI attached,
  and as of the 1.4 kernel **mute does not silence app-requested alerts** — muting
  covers alerts the watch raises at the user, not feedback an app produces in a session
  it owns (PR #267, una-kernel#260). Confirm both on hardware anyway, once: an alarm
  that fails silently is worse than no alarm.
- **Glance and home widget:** `SDK::Glance` for last night's headline, and
  `SDK/HomeWidget/HomeWidget.hpp` (new in 1.4) for a morning line on the home screen.
  Genuinely optional now that 1.4's scrolling menu makes the app's own screen reachable
  — but a sleep report is glanced at once, half awake, which is exactly what these two
  surfaces are for.
- **Morning retrieval over BLE, not USB.** 1.4 publishes the File Transfer Service
  protocol (`Docs/BLE-File-Transfer-Service.md`, PR #273) and the research branch
  carries a validated phone-free client (`prototype/una_ble_client.py`) that already
  pulls files off this watch with matching CRC. Pulling the night's CSV and JSON that
  way leaves the service running, where plugging in to fetch them kills every app on
  the device. Note the protocol requires a bonded, encrypted link, and that windowed
  reads need version ≥ 5. Extending that client to fetch `Apps/SleepLab/` is a small,
  high-value piece of host-side work — put it in this repo, not in the SDK.
- **FIT export:** `SDK/Fit/FitProfile.hpp` on 1.4 still has no monitoring or sleep
  messages, and its `File` enum holds only `Activity = 4`. Sleep export needs the
  monitoring file type and sleep message definitions from the **official FIT profile** —
  look them up and verify them; do not invent message or field numbers. Until then the
  CSV and JSON *are* the export, and the mobile companion has no sleep concept to
  receive them anyway (`config.json` type is `utility`) — do not imply otherwise in the
  UI.

---

## 5. Architecture requirements

- Service owns sensors, the epoch pipeline, the engine, files and settings. GUI is a
  thin MVP over custom messages in `Commands.hpp`, each packed and size-asserted, sent
  with `SDK::send_msg` (the per-app `Sender` is retired upstream — do not reintroduce
  it).
- A night's epoch strip does **not** fit one 256-byte message. Decide deliberately how
  the GUI gets it — a downsampled burst of indexed rows, as MapManager does for its
  roster, or the GUI reading the day's file itself — and write the reason down.
- Publish nothing to the GUI while no GUI is attached, and never self-terminate when the
  GUI closes: the point of autostart is that recording continues unobserved.
- Everything in Tiers 1–2 must be reachable from host tests with no simulator and no
  hardware, using `KernelTestDoubles.hpp` / `FakeFileSystem.hpp`. The stock fake's
  `dir()` does not enumerate — a real enumerating fake exists only on `una-sdk`'s
  `poc/athensrun` branch — so either keep history listing off the tested path or bring
  your own double, and say which in the tests' README.
- Tests live in `SleepLab/Tests/` with their own `CMakeLists.txt`, following
  `MapManager/Tests` and `Squash/Tests`.

---

## 6. Validation, with no ground truth

You cannot validate against polysomnography, so define what evidence you accept and
gather it deliberately:

- **Synthetic fixtures with known answers:** generated epoch streams with a known
  onset, known awakenings of known length, a known final wake. These pin the
  arithmetic exactly and are what the host tests assert on.
- **A self-reported diary:** lights-out and wake times recorded by hand for at least
  ten nights. Report the mean signed error and spread on onset and final wake in the
  ledger. This is the honest headline accuracy number and it belongs in the README.
- **Adversarial nights, each run at least once, each becoming a regression fixture:**
  worn but deliberately lying awake and still for 30 minutes; not worn, on a table;
  worn while charging; a device reboot mid-night; a timezone or clock change mid-night;
  a night ending in a deliberate early get-up.
- **Internal consistency:** night-to-night baseline stability, and agreement between
  the step-delta out-of-bed signal and the scorer's final wake.

---

## 7. Repository conventions and delivery

- Work the tiers in order. After each: host tests green
  (`cmake -B build . && cmake --build build && (cd build && ctest --output-on-failure)`),
  a simulator run, a hardware night, and the ledger updated with what that night
  measured.
- Build the way the rest of the repo does, and check it with Kira:
  `kira build-app --app SleepLab --sdk /path/to/una-sdk --version 0.1.0 --out SleepLab.uapp`.
  Deploy by copying the `.uapp` into `Apps/SleepLab/` on the USB-MSC volume; the kernel
  rebuilds `app_list.json` itself. Note `Utilities/Scripts/Update-Watch-Apps.ps1`
  (new in 1.4, PR #215) updates apps over USB while preserving their data — which for
  this app means not destroying accumulated nights.
- Write `SleepLab/README.md` in this repository's house voice: what it is, why it
  exists, **what it does not do**, the honest accuracy statement from §6, the sensor and
  power findings, the file formats as a normative spec, buttons, building, the
  simulator, tests, known rough edges, and provenance (which app's shell it forked from
  and what did not carry over). Add its row to the root `README.md` table.
- Keep `Docs/FEASIBILITY-LEDGER.md` current, with the CONFIRMED / LIKELY / UNVERIFIED /
  REFUTED tagging: every metric the app reports, what it actually measures, its
  validation status (synthetic-only / diary-validated / speculative), and its known
  failure modes. This is what keeps the app honest as it grows.
- Conventional-commit titles scoped like the rest of the repo (`feat(sleeplab): …`,
  `fix(sleeplab): …`), one concern per commit, commits authored as
  `toby.murray@protonmail.com`, and **no mention of Claude or AI assistance** in
  commits, PR bodies, code comments or docs.
- SDK-side gaps — the FIT monitoring messages, an enumerating filesystem fake, a sensor
  contract — are separate single-concern branches against `una-sdk`, following the
  upstreaming rules in `Squash/Docs/IMPLEMENTATION-PROMPT.md` §5. The app itself stays
  entirely inside `SleepLab/` with zero SDK modifications, so it rebases trivially.
- **Never post comments on `UNAWatch/una-sdk` PRs or issues.** Read and push branches
  only; upstream communication is the repository owner's.

---

## 8. Non-negotiables

- No sleep stages presented as fact; no minutes-in-REM; no clinical language.
- No sleep numbers at all for a night that failed the worn gate.
- An interrupted night is reported as interrupted, prominently.
- Never derive a duration from wall-clock arithmetic, or elapsed time from a sample
  count.
- No absolute physiological thresholds imported from a paper; baselines are personal
  and earned from recorded nights.
- No busy loops, no periodic GUI IPC, no allocation in the sample path after init, and
  an idle service that sleeps to its next due work.
- Every threshold carries the data that justified it, or a TODO naming the recording
  needed to justify it.
