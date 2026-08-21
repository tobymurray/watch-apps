# What the silicon can do, and what the SDK exposes

The `expected` column of a conformance row needs a named source, and a datasheet
figure compiled into an app is a figure nobody can check. So the app carries only
one kind of expectation — a field count read out of an SDK header — and every
physical figure lives here, next to its citation, where `profile_report.py` and a
human can both reach it.

**Nothing in this file is restated from memory.** Each figure was extracted from
the datasheet named at the head of its section, and each row cites the section or
table and the page it appears on. Where a datasheet could not be obtained, the
section says so and the rows are absent rather than guessed. A profile that says
"not sourced" is honest; one with a plausible wrong number in that cell is worse
than useless, because it will be believed.

The parts come from
[`Docs/Investigations/2026-07-29-hardware-config-recovery/`](https://github.com/tobymurray/una-sdk/tree/research/Docs/Investigations/2026-07-29-hardware-config-recovery)
on `una-sdk@research`. That investigation's confidence tags are quoted as its
own, not adopted here.

| Function | Part | That investigation's tag | Datasheet sourced here |
| --- | --- | --- | --- |
| IMU (accel + gyro) | **BMI270** (Bosch) | **CONFIRMED** twice — `CHIP_ID` reg 0x00 = 0x24 at I2C4/0x68, and driver strings in the dumped kernel | **yes** — see below |
| Magnetometer | **BMM350** (Bosch) | **LIKELY**, string-only; a device answers at I2C4/0x14 but a `CHIP_ID` read did not match | no |
| Barometer / temperature | **MS5837** (TE) | part **CONFIRMED** from driver strings; **bus and address UNKNOWN** — its fixed address never ACKed on any of the six I²C buses | no |
| PPG / optical HR | **PAH8316LS** (PixArt) | part **CONFIRMED** from driver strings; address never meaningfully tested | no |
| Fuel gauge | **MAX17262** (Maxim ModelGauge m5) | **CONFIRMED**, I2C1/0x36 | no |
| GNSS | **Airoha AG3335M** | part **CONFIRMED** from driver strings; UART pairing not verified | no |
| BLE radio | **BlueNRG-2** (ST) | **CONFIRMED** | no |
| Haptic | **DRV2625** (TI) | **CONFIRMED**, I2C1/0x5A | not a sensor |
| Display | **LS012B7DD06A** (Sharp/JDI memory LCD) | **CONFIRMED** | not a sensor; recorded because the same investigation found **no touch controller**, which is why this app is button-driven |

---

## BMI270 — accelerometer and gyroscope

**Source.** *BMI270 Datasheet*, Bosch Sensortec, document revision 1.6, release
date March 2026, document number **BST-BMI270-DS000-08**, 150 pages. Fetched
2026-08-21 from
`https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf`.

### The single most useful number in this document

| | |
| --- | --- |
| Accelerometer ODR the part supports | **0.78 Hz … 1.6 kHz** (Key features, p. 2; `ACC_CONF.acc_odr` values `odr_0p78`…`odr_1k6`, §5.2.41, pp. 101–102) |
| Accelerometer rate the SDK delivered | **~48 Hz**, measured (SleepLab ledger row S3) |
| Headroom | **~33×** |

`ACC_CONF.acc_odr` values `0x0d` (3.2 kHz), `0x0e` (6.4 kHz) and `0x0f`
(12.8 kHz) are marked **Reserved** for the accelerometer (§5.2.41, p. 102), so
1.6 kHz is the ceiling the part documents rather than the largest number in the
table. The gyroscope's table goes further: `odr_3k2` = 3200 Hz is *not* reserved
there (§5.2.43, p. 103), which matches the Key features line quoting
"25 Hz … 6.4 kHz (gyroscope)" (p. 2) — the two disagree about 6.4 kHz and the
register table is the narrower claim.

### Two testable predictions, and this is what makes them worth writing down

**Prediction 1 — the configured range, from layer 5's LSB estimate.** The part is
16-bit with four selectable ranges (Table 2, p. 14):

| `ACC_RANGE.acc_range` | Range | Sensitivity | Implied LSB in g |
| --- | --- | --- | --- |
| `0x00` `range_2g` | ±2 g | 16384 LSB/g | 61.04 µg |
| `0x01` `range_4g` | ±4 g | 8192 LSB/g | 122.07 µg |
| `0x02` `range_8g` | ±8 g | 4096 LSB/g | **244.14 µg** |
| `0x03` `range_16g` | ±16 g | 2048 LSB/g | 488.28 µg |

Ranges and register values: §5.2.42, p. 102. Sensitivities and the 16-bit
resolution: Table 2, "Output Signal Accelerometer", p. 14.

`ACC_RANGE`'s **reset value is `0x02`** (§5.2.42, p. 102), which is ±8 g. So if
the kernel leaves the register alone, `0x10.value.f0_lsb` should converge on
**244 µg**. A measured 61 µg would mean the kernel selects ±2 g; 488 µg would
mean ±16 g. **This is a register read done with arithmetic**, and it is why
Tier 5 is optional rather than necessary — layer 5 answers the same question with
a LIKELY tag and no risk of writing to a sensor under the kernel's driver.

**Prediction 2 — where ~48 Hz comes from.** `ACC_CONF`'s reset value is `0xA8`,
whose low nibble is `0x8` = `odr_100` = **100 Hz** (§5.2.41, pp. 100–101). The
neighbouring value `0x07` = `odr_50` = 50 Hz. A delivered ~48 Hz is far closer to
50 than to 100, so the likeliest reading is that the kernel writes
`acc_odr = 0x07` and the shortfall from 50 is loss in the delivery path rather
than in the sensor. That is a claim `0x10.timing.delivered_hz` can be compared
against directly, and `0x10.control.period_honoured` can then say whether the
requested period moves it at all.

Both predictions are **LIKELY at best** until measured. They are recorded here so
that a layer-5 run has something to be wrong against, which is the only way a
number becomes evidence.

### Gyroscope

| `GYR_RANGE.gyr_range` | Range | Sensitivity |
| --- | --- | --- |
| — | ±125 dps | 262.144 LSB/dps |
| — | ±250 dps | 131.072 LSB/dps |
| — | ±500 dps | 65.536 LSB/dps |
| — | ±1000 dps | 32.768 LSB/dps |
| — | ±2000 dps | 16.384 LSB/dps |

Sensitivities and the 16-bit resolution: "Output Signal Gyroscope", p. 15.
Ranges: Key features, p. 2. `GYR_CONF` reset value `0xA9` → `gyr_odr = 0x9` =
`odr_200` = **200 Hz** (§5.2.43, p. 103).

### Figures layer 7's guided protocols are checked against

| Claim | Datasheet figure | Where |
| --- | --- | --- |
| `0x10.physical.bias_x_g` and its siblings | Zero-g offset **±20 mg** typ., TA=25 °C, nominal VDD, soldered, over lifetime | Table 2, p. 14 |
| `0x10.physical.scale_err_*_pct` | Sensitivity error **±0.4 %** (`SA_err_8g`) | Table 2, p. 14 |
| `0x10.physical.cross_axis_pct` | Cross-axis sensitivity **0.2 %** relative contribution | Output Signal Gyroscope table, p. 15 — **note: this is the gyroscope's figure.** The accelerometer's cross-axis line is at p. 14 and the extracted text did not carry its value cleanly; **not sourced**, and the row's `expected` stays absent until it is. |
| `0x20.physical.zero_rate_*_dps` | Zero-rate offset **±0.5 dps** typ. (`Ω, oL`) | p. 15 |
| `0x20.physical.bias_drift_dps_per_min` | Zero-rate offset change over temperature **±0.015 dps/K** (`TCOG`) | p. 15 — a temperature coefficient, **not** a drift per minute. The two are not the same quantity and this row's `expected` is therefore **absent**; what the datasheet bounds is how the bias moves when the part warms, which a still-bias protocol will partly see and cannot separate. |
| Gyro noise | **< 7 mdps/√Hz** typ. in performance mode | Key features, p. 2 |
| Whole-part current | **685 µA** typ. at full ODR, aliasing-free | Key features, p. 2 |
| `CHIP_ID` | register `0x00`, value **`0x24`** | §5.2.1, p. 75 |

The `CHIP_ID` row is the one figure in this file the hardware investigation has
already checked, and it matched — which is why that investigation's IMU
identification is CONFIRMED twice rather than once.

---

## Not sourced yet

Each of these is a datasheet nobody has fetched. Until one is, every conformance
row that would cite it carries `expected: null` and
`conformance: NO_CLAIM` — which is correct, and is not the same as the row being
unmeasured.

| Part | What its datasheet would settle | Why it matters |
| --- | --- | --- |
| **BMM350** | Field range in µT, resolution, ODR range, `CHIP_ID` register and value | `MAGNETIC_FIELD` ships no parser, so layer 2 has to discover the frame; a field range would say whether a discovered value is plausibly µT, gauss or raw counts. And a `CHIP_ID` would close the hardware investigation's open question — a device answers at I2C4/0x14 but the ID read did not match, so the part may not be a BMM350 at all. |
| **MS5837** | Pressure and temperature ranges, resolution, conversion times, **its fixed I²C address** | The standing puzzle: the investigation exhausted all six I²C buses looking for that address and never found it. If `PRESSURE`, `ALTIMETER` or `AMBIENT_TEMPERATURE` produce plausible values, that is evidence the part is there on a bus the sweep missed; if they produce nothing, it corroborates the negative. Either way the correlation belongs in both documents. |
| **PAH8316LS** | PPG sample rate modes, channel count, whether a higher-rate mode exists | **The ceiling on everything HR- and HRV-shaped.** UNA describe the waveform as 20 Hz single channel and call that "the low end for HRV extraction" (PR #167, 2026-07-01). This datasheet is what would say whether a higher-rate mode is a firmware choice or a hardware limit — which decides whether asking for one is a feature request or a wish. |
| **MAX17262** | ModelGauge m5 register map, `RepSOC` resolution and update cadence, `AvgCurrent` **sign convention** | Ledger row S18: the percent gauge read 100.0 % at both ends of an 8.45 h night in which capacity fell 10 mAh. A register map would say whether the SDK is reading `RepSOC` at all, and whether it is reporting it at the resolution the part offers. The sign convention is flagged in SleepLab's ledger as an unverified firmware contract and this is where it would be settled. |
| **Airoha AG3335M** | Constellations, update rate, cold/warm/hot TTFF specifications, position accuracy | Layer 7's GPS protocols measure CEP50/CEP95 and time to first fix. A specification gives those measurements something to be compared against instead of only to each other. |

To add one: fetch the datasheet, extract with `pdftotext -layout`, and add a
section above with the document number, revision, page numbers and the URL and
date it came from. Then add the corresponding `expected` rows — and if a figure
turns out not to be in the document, say that here rather than leaving the
question open.

---

## A note on what "conformance to spec" means here

This platform has four specs and they disagree. The report ranks them explicitly,
and the ranking matters because a divergence between two of them is a different
kind of finding from a divergence between one of them and the device:

1. **The headers** — authoritative for the app-facing contract: type values,
   field counts, field order, units in comments, enum ranges. A device that
   disagrees with a header is a *behavioural* finding.
2. **`Docs/SensorsLayer.md`** — behind the headers, so mostly a source of
   *documentation* findings. Six types missing, an `ACTIVITY` row that
   contradicts itself, an `AMBIENT_TEMPERATURE` unit carrying a literal question
   mark. `Docs/FINDINGS.md` has the table.
3. **Maintainer answers** recorded on `una-sdk@research` — authoritative about
   intent and about firmware, with dates, and with expiry dates.
4. **The silicon datasheets**, via the hardware inventory — this file. Where
   "limitation" becomes quantitative: the gap between what the part can do and
   what the SDK exposes is the headroom, and headroom is what a feature request
   is made of.

The BMI270 section above is the whole argument for ranking them this way. The
header says nothing about a sample rate; `SensorsLayer.md` says nothing about a
sample rate; the maintainers have not been asked; and the datasheet says the part
does 1.6 kHz while the measurement says the app gets 48 Hz. Only the fourth spec
turns "the accelerometer is slower than I expected" into "there is 33× of
headroom here and it is not in the silicon".
