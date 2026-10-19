# Build a battery utility that tells the truth about the battery

Build a new app for the UNA watch, in this repository, that does three things:

1. **Publishes the battery's state to `../SharedData/`** so any other app can read it:
   the gauge's own readings untouched, and a corrected state of charge beside them.
2. **Learns the battery from what it observes**, the way phone and fuel-gauge battery
   health does: how much charge a full battery really holds, how that shrinks with wear,
   and what that does to how long a charge lasts.
3. **Shows it on a glance card**: corrected percentage, time left, and health.

The watch's own percentage is wrong in a way that is already measured (section 2). This
app exists because of that, but must not assume that measurement holds on every watch or
every firmware. Everything it believes, it learns on the wrist, and everything it
publishes says how sure it is.

---

## 0. The honesty contract, which outranks everything here

From `CLAUDE.md`:

> **Prefer measurement over assertion.** If you are choosing a number, measure it and
> record what you measured.

A battery utility is a measurement instrument. Every constant in it (a smoothing rate, a
plausibility bound, the drain at which a reading is treated as rested, the number of
charges before health is shown) is either measured from recordings, with the numbers
written down where `CLAUDE.md` says they belong, or named as an assertion together with
what would settle it.

**A wrong battery number is worse than none.** Someone who trusts "3 days left" and goes
on a trip has been harmed by it. When the model does not know, it publishes `null`, and
the card says so.

**Do not trust this document either.** The observations in section 2 come from one
watch over six days, read off by the author of this prompt. Re-derive them from the
recordings before building on them.

---

## 1. Read these first

- `CLAUDE.md`: the comment rules, the carve-out for measurements, commit rules. Follow
  them exactly. Never bump `appVersion` by hand. Never add a `Co-Authored-By` trailer.
- `Docs/SHARED-HEALTH.md`: the SharedData convention (`now.json` written via
  tmp/bak/rename, `sessions.jsonl` append-only with complete lines, `schema`/`source`,
  `null` for unknown, unit suffixes in field names). Battery data follows the same shape.
  It is not health data, so decide whether it lives under `../SharedData/device/battery/`
  and extend that document with a domain row and any rule it needs. Do not invent a
  second convention.
- `Slumber/`: the model for an always-running Service, a header-only testable core, host
  tests under Docker, a shared-file writer (`Recorder/SleepNow.hpp`, `NightLog.hpp`) and
  its test through the SDK's `JsonStreamReader` (`Tests/SharedSleep_test.cpp`). Slumber
  already subscribes to `BATTERY_LEVEL`, `BATTERY_METRICS` and `BATTERY_CHARGING` every
  30 s and logs them to its day CSVs (`batt_pct_x10`, `batt_mv`, `batt_ma_x10`,
  `batt_avg_ma_x10`, `batt_mah`, `charging`).
- `SunGlance/`: the model for a glance card.
- The SDK's battery parsers: `SDK/SensorLayer/DataParsers/SensorDataParserBatteryLevel.hpp`
  (`getCharge`), `…BatteryMetrics.hpp` (`getVoltage`, `getCurrent`,
  `getAverageCurrent`, `getCapacity` = remaining mAh, `getDesignCapacity`) and
  `…BatteryCharging.hpp`. Each returns `-1` when invalid.
- The user's memory notes on the build: run Docker by the image ID pinned in
  `.github/workflows/app-build.yml`. `std::string` and nothrow `new` drag the exception
  runtime into the app. Rust `core::fmt` integer formatting reads garbage on the device,
  and the Service does not link Rust, so the Service-side core is a C++ header with no
  SDK types. The panel shows 4 levels per channel. The watch has buttons only, no touch.

---

## 2. What is already measured, on one watch

From Slumber's day files, 10 to 15 October 2026, firmware 1.5.0, read at 30 s:

| Observation | Evidence |
| --- | --- |
| Remaining charge after a full charge reads about **204 mAh at 4.38 V** | first rows after each charge |
| The built-in percentage **holds at 100% until remaining falls to about 148 mAh**, nearly two days of use | `batt_pct_x10` = 1000 from 204 down to 149 mAh |
| Below that, **percentage ≈ remaining / 148 mAh** | 139 mAh → 94.9%, 119 → 80%, 103 → 69.9% |
| Whole-watch drain is **1.0–1.2 mA by day and 1.5–1.8 mA overnight**, every night, with one day at 2.2–2.5 mA | remaining mAh in 6-hour blocks |
| **Remaining mAh jumps up at restarts** when the watch has been on USB (+28, +13, +10, +8 mAh) | `charging` was 1 in one row out of ~14,400 |
| At about 1 mA, the voltage tracks remaining charge smoothly: 4.38 V full, 3.86 V at 103 mAh | `batt_mv` against `batt_mah` |

What this suggests, none of it proven:

- The percentage is computed against a capacity of about 148 mAh while the counter
  counts from about 204. **`getDesignCapacity()` was never logged.** If it reads about
  148, that is the cause. Check this first.
- Charging mostly happens while apps are not running (USB mass storage), so **the app
  will rarely see a charge happen**. It will see a restart with more charge than it left
  with, and has to infer the charge from that.
- At about 1 mA, roughly C/200, the terminal voltage may sit close to the open-circuit
  voltage nearly all the time. If so, the voltage is a second, independent estimate of
  state of charge, available at almost every reading. That is the anchor fuel gauges
  normally wait for a long rest to get. Measure how close it is before relying on it.
- Nobody knows whether 204 mAh is what the cell really delivers. **Only a full discharge
  to shutdown settles it.** Plan for one and say what it showed.

---

## 3. Ground it in how battery health is actually done

Survey the established approaches and say which parts you adopt, which you reject, and
why, citing sources. At a minimum:

- **Fuel-gauge ICs.** TI Impedance Track learns the cell's chemical capacity from
  open-circuit voltage at rest and reports full charge capacity, remaining capacity and
  state of health = full charge capacity / design capacity. Maxim/ADI ModelGauge m5
  blends coulomb counting with a voltage model, learns full capacity over cycles, and
  counts cycles. Read the application notes on capacity learning and on
  coulomb-counter drift.
- **Apple.** *Maximum Capacity*, capacity measured against when new, with a "service"
  flag when it is significantly degraded. A cycle is 100% of cumulative discharge, not one
  charge. Its stated retention targets: 80% at 500 cycles for older iPhones, 1,000 for
  iPhone 15 and later, and Apple Watch. Optimized charging learns a routine. The
  displayed percentage never runs backwards while discharging.
- **Android.** Android 14 exposes cycle count and state of health through
  `BatteryManager`. Settings shows battery health on recent Pixels. Adaptive Battery
  predicts time left from the user's own usage.
- **Wearables.** How Garmin, Fitbit and others estimate days remaining from usage, and
  how they present uncertainty.
- **Cell ageing.** Calendar ageing against cycle ageing, temperature and time at high
  state of charge. Know which of these can be inferred from what this watch exposes.

The survey earns its place only where it changes a decision. A paragraph of background
that no design choice depends on is padding. Cut it.

---

## 4. What the model has to learn

Keep the model in a header-only C++ core free of SDK types, tested on the host. The
Service feeds it readings and persists its state. Design it; this section names the
quantities, not the method.

1. **Learned full capacity.** What a full battery actually holds, learned from each
   observed or inferred full charge and cross-checked by integrating current over
   discharge against the counter's fall. That cross-check is what tells you whether the
   counter's scale itself is right. Gate every update on a plausibility check, weight it
   by how much discharge it spans, and count how many updates back the estimate.
2. **Corrected state of charge.** Remaining over learned full capacity, as a percentage.
   Wherever voltage proves to be a usable anchor (section 2), blend it in and record how
   much it moved the estimate. **Never let the displayed percentage rise while
   discharging, or jump.** Converge corrections over time, and record the rate you chose
   and why.
3. **Health.** Learned full capacity against a reference. Pick the reference, and justify
   it: the design capacity, the first capacity learned, or the best seen. Apple measures
   against new; this app joins a battery already in service, and the published fields
   must say which. Also track equivalent full cycles, as cumulative discharge over full
   capacity, and the capacity's trend over time.
4. **Drain profile.** The observed drain differs by time of day (section 2). Learn a
   profile, by hour of day or hour of week, sized by the data you actually have. Decay it
   so a change of routine is picked up. Measure how fast it should decay.
5. **Time left.** Remaining charge played forward through the drain profile, with a
   range, not a single number. Back-test it on the recordings: at each hour, what did it
   predict, and when did the battery actually reach that charge? Publish the error.
6. **What wear costs.** How long a full charge lasts at the user's own drain, today
   against when the reference was taken. "A full charge now lasts 6.8 days; it lasted
   7.6" is the answer someone with an ageing battery actually wants.
7. **Restarts and gaps.** A restart that finds more charge than it left is an
   unobserved charge. A gap with less charge is unlogged drain. Neither may corrupt the
   learned capacity or the drain profile. Decide what each counts as, and test it.

State survives restarts in a small versioned file in the app's own folder. A file from
an older format is migrated or discarded, never misread.

---

## 5. What goes in SharedData

Propose the files and their schemas in the `Docs/SHARED-HEALTH.md` style. As a starting
point, to be argued with:

- **`now.json`**, rewritten every few minutes:
  - `raw`: the gauge exactly as read, with `pct`, `remaining_mah`, `voltage_mv`,
    `current_ma`, `avg_current_ma`, `design_mah` and `charging`.
  - `corrected`: `pct`, `full_mah`, and how many observations back them.
  - `health`: `capacity_pct`, `reference`, `reference_mah`, `cycles`.
  - `time_left`: the expected hours and their range, the drain rate used, and how the
    estimate back-tested.

  Every derived field is `null` until known.
- **A history file** of what a reader needs over months: one line per charge or per day,
  with the learned capacity, cycles and drain, so another app can plot ageing without
  replaying raw readings.

Write through the SharedData helpers Slumber already uses: tmp, bak, then rename for
state, complete lines for history. Remember the platform rules: ten open files
watch-wide, `rename()` never replaces a file, and `open()` returns the same `false` for
absent and busy. Read the published files back in a host test through `JsonStreamReader`,
as `Slumber/Tests/SharedSleep_test.cpp` does. Size every buffer from a measured
worst-case record, with a test that the worst case fits.

---

## 6. The app's own cost

A battery utility that costs battery is a joke someone will tell. Measure what the app
draws: sampling period, wake-ups, writes per hour. Then measure the whole watch's drain
with the app running and with it not running, over long enough to see past the
day-to-day spread in section 2. Choose the sampling period from that, not from habit.
Find out, by experiment, whether a second app subscribing to the same battery sensors as
Slumber changes anything.

---

## 7. The card

One glance card, buttons only. It shows the corrected percentage, time left with its
range, and health once enough charges back it. Until then it says it is learning, rather
than showing a number it cannot defend. Respect the panel's 4 levels per channel and
the legibility floors already measured in this repository.

---

## 8. Tests and data

- **Synthetic traces in the repository**, each a test:
  - the 148/204 clamp;
  - a USB top-up seen only as a restart;
  - an unlogged gap;
  - a battery ageing 1% a month;
  - a sudden change of routine;
  - a gauge whose counter is itself mis-scaled.
- **Replay of real recordings**, through an environment variable pointing at a folder of
  day CSVs, as Slumber's `SLUMBER_DAYS_DIR` does.
- **This repository is public.** Real recordings, and anything personal they reveal
  (sleep times, where the wearer was), never get committed. Commit synthetic fixtures and
  the numbers measured from real data, not the data.

---

## 9. Order of work

1. **Probe.** Log `getDesignCapacity()` and every battery field, plus what happens to the
   readings across a charge and a restart. Confirm or overturn section 2. Stop and report
   before building on it.
2. **Survey** (section 3), with the decisions it drives.
3. **Core and tests**, back-tested on the existing recordings.
4. **SharedData writer**, with the convention document extended.
5. **Service.** Install it and run it alongside Slumber for at least one full charge
   cycle.
6. **Card.**
7. **A full discharge to shutdown**, to give the learned capacity a ground truth.

Record every measurement in the app's `README.md`, its design record, in the register
of `Spin/README.md`. Conventional commits, one logical change each, staged deliberately.
