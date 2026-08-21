# UNA Watch sensor profile — not yet measured

**There is no profile.** SensorLab has been built, tested and packed, and it has
never been run on the watch. This file is a placeholder that says so, because the
alternative — a plausible-looking document assembled from inherited rows and
inference — is exactly what this app exists to prevent.

A profile is generated, not written. When a hardware run exists:

```sh
python3 Tools/pull_profile.py <BLE-address> --out Profiles/1.4.0-2026-08-21
python3 Tools/profile_report.py Profiles/1.4.0-2026-08-21/profile-1.4.0.json \
    -o SENSOR-PROFILE.md
```

and this file is replaced by that rendering.

---

## What is known about this device's sensors today, and where it came from

None of it from SensorLab. All of it from
[SleepLab](../SleepLab/Docs/FEASIBILITY-LEDGER.md), which measured it as a side
effect of building something else, and it is reproduced in
[`Docs/LEDGER.md`](Docs/LEDGER.md) §2 with attribution. In one table, because a
reader arriving here deserves the current state rather than a redirect:

| Sensor | What is known | Tag |
| --- | --- | --- |
| `ACCELEROMETER` (0x10) | Delivers ~48 Hz against a requested 25 Hz — nearly double, in the opposite direction to the simulator's documented thinning. Neither the requested period nor the requested latency does anything: 5000 ms of latency produced 195 ms batches, 308 a minute at 9.6 samples each. | **REFUTED** ×2 |
| `TOUCH_DETECT` (0x140) | An **event** sensor, not a streaming one. One sample in 507 minutes on a sleeping wrist, and zero worn/not-worn transitions. | **REFUTED**, then **CONFIRMED** |
| `SPO2` (0xF1) | Resolves no driver at all. There is no firmware producer to ask. | **REFUTED** |
| `HEART_BEAT` (0x40) | Resolves no driver at all. UNA state that HR detection is frequency-domain rather than per-beat (PR #167), so there are no beat timestamps to read. **Has an expiry date.** | **CONFIRMED** |
| `HEART_RATE` (0x41) | Delivered exactly its requested 1 Hz — so the period *is* honoured here and not on the accelerometer. | **CONFIRMED** |
| `HEART_RATE_EX` (0x43) | 30 169 arbitrated readings in one night, every one attributed to the wrist optical source, none to a strap, none unattributed. | **CONFIRMED** |
| `BATTERY_LEVEL` (0x120) | Reads 100.0 % at both ends of an 8.45 h night in which the fuel gauge lost 10 mAh. **Not a slow gauge — it did not move at all.** | **REFUTED** |
| `PPG` (0xF0) | Never observed to resolve a driver. Given the two above, there is a real chance it has no app-facing driver either — which would close the on-device HRV route. | **UNVERIFIED** |
| The other 29 types | Nothing. No app has ever subscribed to them. | **UNVERIFIED** |

**Twenty-nine of thirty-seven sensor types on this device have never been
subscribed by anything.** That is the gap this app was built to close, and the
reason a placeholder is more useful here than a document.

---

## What the silicon can do, which is knowable without the device

Sourced, cited, and in [`Docs/EXPECTED.md`](Docs/EXPECTED.md). One figure is worth
repeating here because it is the largest single thing this exercise has produced
so far:

> The **BMI270** supports accelerometer output data rates from **0.78 Hz to
> 1.6 kHz** — *BMI270 Datasheet*, BST-BMI270-DS000-08 rev 1.6, Key features p. 2,
> and `ACC_CONF.acc_odr` §5.2.41 pp. 101–102.
>
> The SDK delivers **~48 Hz**.
>
> **~33× of headroom, and it is not in the silicon.**

`ACC_CONF`'s reset ODR is 100 Hz and its neighbouring setting is 50 Hz, which makes
"the kernel writes `acc_odr = 0x07`" a testable hypothesis rather than a shrug.
`ACC_RANGE`'s reset value is ±8 g at 4096 LSB/g, so layer 5's recovered
quantisation step should be **244 µg** if the kernel leaves the register alone —
which is a register read done with arithmetic.

Five datasheets are unsourced: BMM350, MS5837, PAH8316LS, MAX17262, AG3335M.
`Docs/EXPECTED.md` says what each would settle rather than guessing at figures.

---

## What is already known to be wrong with the platform

Eleven entries in [`Docs/FINDINGS.md`](Docs/FINDINGS.md), each with a minimal
reproduction. Nine were found by reading the SDK; **two were measured**, and those
two are the ones a reader should look at first:

- `SDK::JsonStreamWriter::add(int32_t)` writes a negative value as its unsigned
  reinterpretation on any 64-bit build. `add("x", -5)` produced
  `{"x":4294967291}`. ARM is unaffected, which is exactly why it survives.
- `add(int64_t)` casts to `double` and formats with `%g`, so
  `add("t", 1755553500)` produced `{"t":1.75555e+09}` — a UNIX timestamp with its
  seconds gone.

And the one most worth fixing, found by reading:
`GpsLocation::isDataValid()` dereferences field 1 *before* checking the field
count, and `DataView`'s bounds assert is compiled out at `-Os`. A short
`GPS_LOCATION` frame is an out-of-bounds read in any shipped build. It is the only
parser of the 29 that does this, and reordering the `&&` fixes it.

Nothing in that file has been posted anywhere; upstream communication is the
repository owner's.

---

## Completeness

Of the ~1 974 claims in the catalogue, **zero have been answered by SensorLab.**

That number is the point of the app. The denominator was fixed before any
measurement was taken — every claim exists from the first run, tagged UNVERIFIED,
naming the probe that would settle it — so the fraction is honest from minute one.
A profiler that only counted what it had attempted would always look finished.
