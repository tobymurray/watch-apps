# Prompt: Implement a state-of-the-art Squash activity app for the UNA Watch

You are an expert embedded C++ engineer and sports-science-literate signal-processing engineer. Your task is to design and implement a **Squash** activity app for the UNA Watch SDK (this repo), taking wrist-worn racquet-sport analytics meaningfully beyond what commercial watches ship today. Work incrementally, phase by phase, with host-testable algorithms and honest feasibility calls — do not fake precision the sensor physics cannot support.

---

## 1. Context: the platform you are building on

Read these before writing code:

- `Docs/platform-overview.md`, `Docs/sdk-overview.md`, `Docs/architecture-deep-dive.md` — dual-process app model (headless **Service** ELF + TouchGFX **GUI** ELF packed into one `.uapp`).
- `Docs/Examples/Stopwatch-Architecture.md` — the best-documented minimal app; copy its architecture idioms.
- `Examples/Apps/Workout/` — **your primary template.** An indoor, HR-centric Activity app already doing: `FUSION_RAW` IMU at 100 Hz, external BLE HR strap acquisition, FIT recording with crash recovery (`ActivityWriter`), HR zones/calories, settings persistence, laps, activity summary. Steal its structure wholesale.
- `Docs/SensorsLayer.md` — full sensor-type table, units, delivery model.
- `Docs/Tutorials/Sensors/` — sensor plumbing reference for every type.
- `Docs/ExternalSensors.md` — BLE HR strap (Polar H10) acquisition API.
- `Docs/unit-testing.md`, `Tests/Host/` — GoogleTest host-test harness.

### Hardware and runtime constraints (hard requirements)

- MCU: STM32U575/585, Cortex-M33, hard-float FPU. Compile flags: `-Os -fPIC -fno-exceptions -fno-rtti`, C++17. **No exceptions, no RTTI, no dynamic_cast.**
- RAM budgets (linker defaults): Service 500 KB / 10 KB stack, GUI 600 KB / 10 KB stack. Prefer fixed-size buffers (`SDK::Tools::CircularBuffer`, `FixedQueue`); avoid heap churn in the sample path.
- **Apps do not create threads.** You get exactly two single-threaded blocking message loops (Service and GUI). All Service work happens inside `Service::run()`'s `getMessage` loop. Timers are polled software timers (`Libs/Header/SDK/Timer/Timer.hpp`); monotonic time is `kernel.sys->getTimeMs()` (32-bit, wraps ~49.7 days — use unsigned subtraction everywhere).
- IPC messages come from fixed kernel pools; **largest block is 256 bytes**. Every custom message: `#pragma pack(push, 4)` and `static_assert(sizeof(Msg) <= 256)`. Use `SDK::make_msg<T>(kernel)` (`Libs/Header/SDK/Messages/MessageGuard.hpp`).
- Display: 240×240, 8 bpp ABGR2222 (only the fixed `SDK::GUI::Color` palette), TouchGFX MVP, **10 fps** tick rate (`SDK::GUI::Config::kFrameRate`). Buttons: `SDK::GUI::Button::{L1,L2,R1,R2}` = simulator keys `1 2 3 4`.
- Power: the kernel deep-sleeps when the message path is quiet. Batch and extrapolate; never poll the GUI with periodic IPC when arithmetic on a timestamp will do (Stopwatch pattern).

### Sensor contract

- IMU: subscribe `SDK::Sensor::Type::FUSION_RAW` (0x131) via `SDK::Sensor::Connection(Type::FUSION_RAW, 1000.0f/100.0f, /*latencyMs*/100)` → 100 Hz samples delivered in ~10-sample batches as `EVENT_SENSOR_LAYER_DATA`. Parse with `SDK::SensorDataParser::FusionRaw` (`Libs/Header/SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp`): 6× int16 = accel X/Y/Z + gyro X/Y/Z. **Units: ±8 g → 4096 LSB/g; ±2000 dps → 16.4 LSB/dps.** The part is a Bosch BMI270.
- Heart rate: `HEART_RATE_EX` (0x43) at 1000 ms period — gives `getSource() ∈ {OPTICAL, EXTERNAL}`, per-source BPM and trust. Request an external strap with `SDK::Message::Accessory::RequestPrepare{ kinds = SDK::Accessory::Kind::HRM }` on GUI start (see `Workout Service.cpp:403`); show state via the `SensorStatusRow` widget. The kernel auto-releases on app stop.
- Also subscribe: `BATTERY_LEVEL` (event), `TOUCH_DETECT` (worn/unworn), `WRIST_MOTION` (event, backlight).
- **No GPS** — squash is indoors; do not include any GPS/track/map code.
- RR intervals (`RR_INTERVAL` 0x44) exist only on branches `feat/rr-interval-contract` + `feat/rr-interval-tooling` and have **no firmware producer yet** (watch PPG is 20 Hz single-channel; `HEART_BEAT` emits nothing). Treat HRV as a stretch feature behind a compile-time flag; design the recovery metrics so they degrade gracefully to 1 Hz BPM.

### Critical sensor-physics facts your algorithms must respect

1. **Both IMU ranges saturate during real squash strokes.** Elite-adjacent wrist angular velocity exceeds 2000 dps and impact shock exceeds 8 g. Clipping is not noise — treat it as signal: use *time-spent-saturated* (clip duration per axis) as an intensity feature, and never build peak-based metrics that silently rail at the range limit. All "speed"/"power" outputs are **relative indices for this user**, never absolute m/s.
2. The watch is on the **wrist, not the racquet**. Racquet-head speed is only estimable as a proxy (wrist angular velocity × nominal lever arm + clip-duration correction). Present it as "Swing Speed Index" (unitless or normalized to the session/user baseline), and say so in the UI copy.
3. Handedness and watch-wrist matter. Add a one-time setting: dominant hand + which wrist wears the watch (racquet wrist vs. off wrist). If the watch is on the **off wrist**, per-shot features are unavailable — detect this case and fall back to movement/HR analytics only, telling the user why.
4. 100 Hz Nyquist = 50 Hz. Impact transients alias; rely on band-limited features (gyro envelope, jerk magnitude over 20–50 ms windows), not single-sample peaks.

---

## 2. What already exists for you on branches — use it

**`simulator-imu-source`** (local + origin; rebased onto `upstream/main` — resolve its HEAD from the branch, not a pinned SHA) is the enabling dependency. Merge or rebase it first (it also carries a real sensor-manager deadlock fix). It provides:

- `SDK::Simulator` `ImuFusion` driver: a simulated `FUSION_RAW` source, `skMinPeriodMs = 1.0f`, registered via `ConfigurationSimulator.hpp` macros `IMU_FUSION_SIM_ENABLE`, `IMU_FUSION_SIM_SWING_KEY '6'`, `IMU_FUSION_SIM_CSV_PATH`.
- `ImuFusionSource` (`Libs/Header/SDK/Simulator/Components/Sensors/IMU/ImuFusionSource.hpp`): dependency-free, host-tested generator with two modes — **synthetic racquet swings** (alternating `SwingType::FOREHAND/BACKHAND`, gyro sign flips, gravity-at-rest on +Z, deterministic) triggered by key `6`, and **CSV playback** (`t_ms,ax,ay,az,gx,gy,gz`, looped, sample-and-hold).

This means your development loop is: record/synthesize IMU CSVs → replay through the simulator → also feed the same CSVs into GoogleTest host tests as fixtures. Build everything downstream on that loop.

Note: `simulator-imu-source` has **not been PR'd upstream yet**. Per §5, it is the first thing to upstream — it fits the profile of what UNAWatch merges from outside contributors almost perfectly.

---

## 3. Product specification

App identity: `APP_NAME "Squash"`, `APP_TYPE "Activity"`, `DEV_ID "UNA"`, new unique 16-hex `APP_ID`. Directory: `Examples/Apps/Squash/` with the canonical layout (copy Workout's tree: `Software/Libs/{Header,Sources}`, `Software/Apps/Squash-CMake/CMakeLists.txt`, `Software/Apps/TouchGFX-GUI/` with `simulator/{main.cpp,ConfigurationSimulator.hpp,gcc/Makefile,msvs/Application.vcxproj}` and `config/gcc/app.mk`). Adding `simulator/gcc/Makefile` auto-enrols the app in the Linux-simulator CI matrix.

Implement in tiers. **Each tier must be fully working, tested, and honest before starting the next.**

### Tier 1 — Core session (table stakes, must be rock solid)

- Start/pause/resume/discard/save session; elapsed time; live HR + zone (reuse `HeartRateZone` tpkg widget); calories via the Workout MET model; battery/worn status.
- BLE strap acquisition (Polar H10) with status row; HR provenance recorded.
- FIT recording via a copy of Workout's `ActivityWriter` (30 s flush, `recoverInterrupted()` crash recovery). **Extend `SDK::Fit::Sport`** — `FitProfile.hpp:46` has no racquet sport; add the FIT-profile-correct value for squash (verify against the official FIT SDK profile: racquet sports family; do not invent a number) and follow the extension precedent on `upstream/feat/multi-activity-variants`.
- Raw-IMU **research recording mode** (settings toggle): stream `FUSION_RAW` to a CSV in the app sandbox (`t_ms,ax,ay,az,gx,gy,gz`, batched writes, ~2.9 MB per 30 min at 100 Hz — check flash headroom and cap duration). This is how real labeled squash data gets collected for every later tier; it must ship first.

### Tier 2 — Shot detection and classification

All algorithms live in an **SDK-free, clock-injected engine** (see §4). Detection pipeline on the 100 Hz stream:

- **Swing detection**: gyro-magnitude envelope over a sliding window; candidate when envelope crosses an adaptive threshold (rolling median + k·MAD, so it self-calibrates to the player); confirm with an accel jerk burst (ball impact) within the swing window; refractory period ~350 ms. Report per-shot: timestamp, duration, peak gyro (with clip-duration), impact sharpness.
- **Forehand vs. backhand**: dominant rotation sign about the forearm axis during the acceleration phase, gated by handedness setting. Target ≥90 % on the synthetic generator (which encodes exactly this signature) and validate on real CSVs.
- **Racquet-prep time**: backswing start = gyro sign reversal preceding the forward-swing envelope rise; prep = time from reversal to impact, plus integrated backswing rotation amplitude (deg, from gyro integration over that short window — drift is negligible at <1 s).
- **Swing Speed Index**: peak forward-swing angular velocity, corrected by gyro clip duration, normalized to the user's rolling session baseline. Explicitly relative.
- **Swing smoothness**: normalized jerk cost over the forward swing (a validated motor-control metric) — lower = cleaner technique.
- Live screen: shot count, FH/BH split, last-shot speed index, live speed-index sparkline if cheap enough at 10 fps.

### Tier 3 — Rally structure and movement

- **Rally segmentation**: two-state (rally/rest) HMM-lite or hysteresis machine over 1 s epochs of accel-variance + shot events + step activity. Rally = sustained movement containing ≥1 shot; rest = quiet ≥ ~4 s. Emit rally records: duration, shots, mean/max HR, movement intensity.
- Derived: rally count, **longest rally** (shots and seconds), work:rest ratio, shots per rally distribution.
- **Serve detection** (heuristic, feeds assisted scoring): first shot of a rally following a rest period, preceded by ≥2 s of low movement while standing (accel variance floor), often with a distinctive slow deliberate backswing. Flag it as `serve: likely` — do not overclaim.
- **Lunge detection and count**: high linear-accel deceleration spike with forward-lean signature, *not* coincident with a swing, followed by push-off; count per rally and per session. Validate thresholds on real recordings before trusting.
- **Court coverage / movement load**: no GPS indoors and wrist-PDR is not credible on court — do **not** print meters. Ship an honest **Movement Load** index (integrated accel variance during rallies, normalized) plus step count and lunge count. If you later want distance, say it requires validation data and keep it behind a flag.
- **Inter-rally HR recovery**: HR drop over the first 30 s of each rest ≥30 s (HRR30 proxy from 1 Hz BPM); trend it across the session. With the RR branches merged and an H10 present, add RMSSD over rest windows behind `SQUASH_HRV_ENABLE`.

### Tier 4 — Scoring, assessment, fatigue

- **Assisted score keeping** (be honest: wrist IMU cannot know who won a point): rally-end auto-detected → the watch buzzes and shows a one-tap attribution screen for ~6 s (`R1` = my point, `L1` = opponent, timeout = unattributed rally). Serve detection pre-fills server continuity per squash PAR-11 rules; user can correct. Score, games, and match state persist across pause/crash.
- **Technique decay under fatigue**: per-shot feature vectors (prep time, speed index, smoothness, FH/BH balance) bucketed into session thirds; report z-shifts, e.g. "In the final third your prep time dropped 18 % and smoothness fell 1.2 SD — technique degrading with fatigue." Only report when shot count per bucket ≥ 20.
- **"What kind of squash player are you?"**: transparent rule-based archetype from session aggregates — e.g. Retriever (high movement load, long rallies, modest speed index), Shooter (short rallies, high speed index, high FH share), Grinder (high work:rest, flat HR recovery), All-Court. Show the top-2 with the driving stats; never a black box.
- **Session summary + history**: post-session summary screens (shots, FH/BH, longest rally, speed-index percentile vs. personal history, HRR trend, archetype); persist per-session aggregates as JSON (`SDK::JSON` streams, model on Workout's `ActivitySummarySerializer`); write per-shot and per-rally data as FIT developer fields so it exports.

---

## 4. Architecture requirements

- **`SquashEngine`** (suggested split: `SwingDetector`, `StrokeClassifier`, `RallyTracker`, `ScoreKeeper`, `FatigueModel`) in `Software/Libs/Header|Sources/` — pure C++17, **zero SDK includes**, clock and samples injected (`void onImuSample(uint64_t tUs, const Sample&)`, `void onHr(uint32_t tMs, float bpm)`), fixed-size internal buffers, callbacks/polled getters out. Model on `WristTiltDetector` and `ImuFusionSource`. Everything in §3 Tiers 2–4 must be exercisable in host tests with CSV fixtures — no simulator, no hardware.
- Service owns sensors, engine, `ActivityWriter`, settings; GUI is a thin MVP over custom messages defined in `Commands.hpp` (pack/size asserts; use `SDK::send_msg` per PR #219 if merged, else Workout's `Sender` pattern). Follow Workout's message naming and Stopwatch's extrapolate-don't-poll timing pattern.
- GUI screens (TouchGFX Designer project, reuse tpkg widgets `Title`, `Buttons`, `HeartRateZone`, `PauseIndicator`, `InfoCarousel`): pre-start (sensor status + settings), live carousel (time/HR ▸ shots ▸ rally ▸ score), point-attribution overlay, pause, summary carousel, discard/save.
- Host tests in `Tests/Host/apps/Squash/` registered in `Tests/Host/CMakeLists.txt`; fixtures under the test tree: synthetic-generator sweeps (vary `SwingParams`), edge cases (clipped swings, off-wrist, timestamp wrap at 0xFFFFFFFF ms, batch gaps), and at least one real recorded session CSV once available. Every classifier threshold must be a named constant with a comment stating what data justified it — or a TODO naming the recording needed.

## 5. Upstreaming strategy — grounded in UNAWatch/una-sdk's actual PR history

This project is developed on a fork (`tobymurray/una-sdk`) with the intent of upstreaming as much as possible to `UNAWatch/una-sdk`. The upstream repo's ~200-PR history (read it: `gh pr list -R UNAWatch/una-sdk --state all`) shows exactly what merges and what doesn't. Structure ALL work to respect these findings:

### What upstream demonstrably merges from outside contributors (fast, usually within days)
- Small, surgical, verifiable fixes: simulator/Linux build enablement (#122, #125, #127, #177, #180, #202–#205, #209), docs corrections (#104, #176, #213-class), tooling fixes (#186, #189), non-gating CI (#212).
- Small SDK-side contracts and utilities: sensor-type enum additions (#166 SpO2 — merged same day), shared utility refactors (#208 ClockTime single-sourcing).
- Host tests are valued (#139 added SDK host tests as a feature) and Windows/MSVC parity is actively maintained (#207 fixed missing MSVC sim sources) — every simulator change must update **both** `simulator/gcc/Makefile` and `simulator/msvs/Application.vcxproj`.

### What demonstrably does NOT merge
- **Monolithic external PRs.** #66 (Linux simulator, 7 fixes in one PR) was closed; a maintainer rebased it themselves as #123. Lesson: slice into single-concern PRs or maintainers will take the work over on their own schedule.
- **Features overlapping the internal roadmap.** #194 (12/24-hour time format) was closed with "we've been working on [this] internally, and your work here helped inform the path" — the team then shipped their own version (#206, #210). Lesson: for anything plausibly on their roadmap, expect absorption; design so that absorption is a *success outcome* (the reusable SDK pieces merge even if the feature is reimplemented).
- **Anything requiring closed-firmware answers.** The kernel is closed source. #167 (BeatProbe) confirmed `HEART_BEAT` emits nothing and PPG is 20 Hz single-channel; #220 (RR_INTERVAL contract) sits open explicitly awaiting firmware answers. Lesson: an SDK-side contract with no firmware producer can be *proposed* but will stall; never make shipped features depend on one.
- **New example apps from outside contributors — zero precedent.** Every merged app (Treadmill #149, Stopwatch #218, external-HRM integration #169/#170, intervals #155, native FIT encoder #171) came from the core team (rryles, sdvsaienko, OleksandrDroid).

### Consequences for this project — a layered PR stack

**Layer A — upstream first, before app work (high merge probability, each its own PR):**
1. `simulator-imu-source`: the `ImuFusionSource`/`ImuFusion` simulated `FUSION_RAW` driver — simulator infrastructure + 340 lines of host tests, exactly the merged pattern. Split the sensor-manager deadlock fix (`fix/simulator-sensor-deadlock`) into its own PR, as with #214's shutdown fix.
2. `FitProfile.hpp` racquet-sport extension (`Sport`/`SubSport` values from the official FIT profile) — same shape as merged #166; note upstream itself extends this enum on `feat/multi-activity-variants` (#221), so match that precedent.
3. Any incidental simulator/build/docs fixes discovered along the way — always separate PRs, never bundled into feature work.

**Layer B — the Squash app itself (no merge precedent; build for graceful non-merge):**
- Keep the app 100 % self-contained under `Examples/Apps/Squash/` with zero SDK modifications beyond Layer A, so carrying it on the fork is a trivial rebase and offering it upstream is a clean single-directory PR.
- Watch #221 (multi-activity variants, "one binary, many launcher activities"): if a racquet/court-sports family fits that architecture, aligning Squash with it materially raises the odds upstream wants it. Likewise adopt #219's `SDK::send_msg` idiom instead of a hand-rolled `Sender` if/once it merges — new-app boilerplate reduction is explicitly what that PR is for.
- Put reusable algorithm pieces where they could migrate to `Libs/Header/SDK/` later (the engine is SDK-free by design — that is also what makes it upstreamable piecemeal, cf. `WristTiltDetector`, `CadenceStrideModel` which live SDK-side).

**Layer C — not upstreamable app-side; flag-gated or out of scope:**
- HRV/RMSSD (needs `RR_INTERVAL` producer — kernel work, tracked by open #220), raw PPG access, generic BLE/GATT passthrough, strap battery level. Behind compile-time flags with graceful degradation, never on the critical path.

### Process norms (observed, mandatory)
- Conventional-commit titles scoped like upstream's (`feat(squash): …`, `fix(simulator): …`); one concern per PR; PR bodies state problem → cause → fix → how it was verified, like the merged examples.
- CodeRabbit auto-reviews every upstream PR — pre-empt it: no dead code, initialized members, guarded narrowing conversions; address its findings in follow-up commits (`fix(...): address CodeRabbit — …` is the established idiom in upstream history).
- Base branches on `upstream/main`, not the fork's integration branches. Commits authored as `toby.murray@protonmail.com`; never mention Claude or AI assistance in commits, PR bodies, or code comments. **Never post comments on UNAWatch PRs/issues** (including `@coderabbitai` triggers) — push branches and read only; the user handles all upstream communication.

## 6. Delivery process

Work in this order, verifying each step before the next:

1. Execute Layer A of §5: rebase `simulator-imu-source` and the deadlock fix onto `upstream/main` as separate upstream-ready PR branches, plus the FIT racquet-sport enum PR; merge them into your working branch.
2. Tier 1 app skeleton → builds `.uapp` via CMake **and** runs in the Linux simulator (`UNA_SDK=<repo> make -f simulator/gcc/Makefile`, boot marker `"GUI is now running"`, headless: `SDL_VIDEODRIVER=dummy`).
3. `SquashEngine` swing detection with host tests against the synthetic generator, then wire into the sim (key `6` produces a counted, classified shot on screen).
4. Tiers 2→4 in order; after each tier: host tests green (`ctest --test-dir build-host --output-on-failure`), simulator smoke run, FIT output inspected.
5. Keep a `Docs/Examples/Squash-Architecture.md` in the style of the existing architecture docs, including a **feasibility ledger**: for every metric, what it truly measures, its validation status (synthetic-only / real-data-validated / speculative), and known failure modes.

Non-negotiables: never present unvalidated absolutes (m/s, meters, kcal beyond the MET model) as fact; degrade gracefully when sensors are absent (no strap, off-wrist, unworn); no busy loops or periodic GUI IPC; all sample-path code allocation-free after init.
