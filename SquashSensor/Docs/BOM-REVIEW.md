# BOM review — SquashSensor, 2026-09-17

A line-by-line review of the bill of materials in [`SquashSensor/README.md`](../README.md),
against the derivations in [`ADVERSARIAL-REVIEW.md`](ADVERSARIAL-REVIEW.md). The question
for each line is the same: what else could do this job, what is the tradeoff in numbers,
and does the current choice hold.

**What was done.** Datasheets were downloaded and read for the ICM-45686 (TDK DS-000577
Rev 1.0) and its user guide (AN-000478 Rev 1.0), the LSM6DSV320X (ST DS14623 Rev 1) and its
application note (AN6119 Rev 1), the ADXL372 (ADI Rev. C), BQ2970/BQ29700 (TI SLUSBU9I),
MCP73123/223 (Microchip DS22191E), Varta CoinPower CP 1254 A4 and CP 1654, and TDK
AN-000265. Distributor catalogues were checked for lifecycle and stock where a sourcing
claim is made. **No board was built, no part was bought, and nothing here was measured on
hardware.** Where a number is a datasheet figure it says so; where it is arithmetic over
datasheet figures it says so; where it is an assertion it says so and names what would
settle it.

**Two of the design's own open questions are settled here from primary sources**, and the
answers are in B2. Reading DS-000577 and AN-000478 also corrected two numbers that this
review had wrong on a first pass, and one that the README and the adversarial review share.

---

## 0. The verdict

**Most of the BOM holds. One line does not, and it blocks a build.**

The LiFePO4 decision was right on the physics and is unbuildable on the sourcing, and
because it was made late it left three numbers elsewhere in the document still describing a
lithium-cobalt cell. That is the largest finding and it is not a part swap — it is a
decision that has to be re-made against a constraint nobody applied to it.

The second is that the ICM-45686 already does the thing the plan is budgeting a
document-read to find out about, and it does it with a cap nobody anticipated: **the AUX
I²C master carries two external sensors into the timestamped FIFO, at a maximum of
400 Hz.** That resolves F13 without a part change and halves the high-g rate, and those two
facts have to be weighed against each other rather than celebrated separately.

Seven findings follow, ordered by how much they change. Three "settled" items in §2 of the
prompt are challenged in §4, one of them successfully.

---

## 1. The line items

| Line | Function | Current choice | Alternatives considered | Verdict |
|---|---|---|---|---|
| **Cell — chemistry** | energy | LiFePO4 100–150 mAh | LiPo pouch; Li-ion hard coin (Varta CoinPower); primary CR2032/2450 | **change** — B1 |
| **Cell — format** | retention, thickness | pouch (implied) | hard coin; small cylindrical | **open** — carrier-dependent, §3 |
| **Cell — capacity** | session reserve | 100–150 mAh | 70 mAh | **open** — §3.5 |
| **Charger** | CC/CV | "an LFP charger, 3.6 V, + cell temperature" | MCP73123; CN3058E; BQ25155; MCU-driven | **change** — B1.3, B8.1 |
| **Protection IC** | cell fault cutoff | BQ29700 + dual N-FET | BQ29701/2/3; cell-vendor PCM | **change** — B1.2 |
| **Protection FETs** | trip current | "pick backwards from trip current" | — | **holds**, with a number it lacked — B8.2 |
| **PTC** | redundant overcurrent | unspecified | delete | **holds** |
| **Regulator** | rail | "required, nano-quiescent LDO" | none; buck | **change** — B4 |
| **6-axis IMU** | primary kinematics | ICM-45686 | LSM6DSV320X; ICM-45605; ICM-42688-P; BMI3xx; ISM330/LSM6DSV16X | **open** — B3 |
| **High-g accel** | unclipped peak, α baseline | ADXL375 | ICM AUX path; LSM6DSV320X high-g; ADXL372; H3LIS331DL | **open** — B2, B3, B6 |
| **Magnetometer footprint** | future channel | footprint, unpopulated | populate on ES1 | **holds**, and the argument against it loses a leg — B2.3 |
| **MCU / radio module** | compute, BLE, cert | pre-certified module, '832 vs '840 open | bare SoC; nRF5340 | **holds** — B8.5 |
| **32.768 kHz crystal** | timekeeping | "a board part" | delete, resync in cradle | **holds — for a different reason** — B5 |
| **Flash** | one hour of samples | SPI NOR 512 Mbit | SPI NAND; microSD; eMMC; MCU-internal | **holds** — B7 |
| **Off switch** | break load path | slide, recessed | reed; hall + latch; charger ship-mode | **holds** — B8.3 |
| **LED** | recording / stopped | one, low-brightness | two; RGB | **holds, with a colour constraint** — B8.4 |
| **Charge/data contacts** | dock | recessed gold pads + cradle pogo pins | magnetic; USB-C in cap; edge | **holds** — B8.6 |
| **ADC divider** | battery sense | resistor divider | nRF52 SAADC VDD channel; charger ADC | **change** — B4.3 |
| **ESD protection** | user-touchable pads | not listed | TVS array | **change** — B8.7, a gap not a swap |
| **Printed sled / puck** | carrier | "printed" | material unspecified | **change** — B8.8 |
| **Conformal coating, foam, tapes** | seal, preload | listed, unspecified | — | **open** — §3, B8.7 |

---

## 2. The findings

### B1 — LiFePO4 is right on the physics and has no orderable part, and the change to it orphaned three numbers that are still in the document. **Largest.**

This is the finding the prompt's §2 invited: *"LiFePO4 over lithium-cobalt — overturned by a
sourcing failure at 100–150 mAh."* It is a sourcing failure.

**B1.1 — the sourcing.** The smallest LiFePO4 cell DigiKey stocks is ZEUS
`PCIFR18650-1500`: **1500 mAh in an 18650, 18.2 × 65.5 mm**. That is ten times the capacity
this design needs, and it is the *floor* of the distributor catalogue, not the middle of
it. Below it there is nothing: LiFePO4 at 100–150 mAh exists only through Alibaba-tier
suppliers, without a datasheet, a UN 38.3 report, a UL listing, or a second source.

That collides with two things the design has already committed to: §4.5's **"BOM with
orderable part numbers"**, and §4.5's reproducibility list. It is worse than an ordinary
sourcing inconvenience because of *what the LFP choice was for*. The argument in *LiFePO4,
not LiPo* is a safety argument made on behalf of strangers building from published files.
Telling a stranger to buy an undocumented cell from an unnamed supplier, in order to obtain
a safety margin, gives back more than it buys — the documentation is most of what makes a
small lithium cell safe to design around.

**B1.2 — the protection IC no longer protects.** Verified from the TI datasheet (SLUSBU9I,
Device Comparison Table): **BQ29700 overcharge detection is 4.275 V**, undervoltage
2.800 V. A LiFePO4 cell charges to 3.6 V and is out of specification above roughly 3.65 V.
**An overcharge threshold 0.675 V above the charge voltage cannot trip before the cell it
protects is already damaged.** The rest of the family is worse in the same direction —
BQ29701 at 4.280 V, BQ29702 at 4.350, BQ29703 at 4.425 — so there is no pin-compatible
escape. The undervoltage threshold at 2.800 V is, by accident, roughly right for LFP (the
knee is below 3.0 V), which is the only reason this reads as an oversight rather than an
error.

**B1.3 — the obvious LFP charger has the exact defect the README warns about, and a second
one.** The README says to "check it for a battery-temperature input", naming the MCP73831's
lack of a THERM pin as the failure. The direct LiFePO4 sibling, **MCP73123**, has the same
failure: its pin list is VDD ×2, VBAT ×2, NC ×2, STAT, VSS ×2, PROG, EP — **no thermistor
input** — and thermal regulation "based on the die temperature", in the same words. The
warning, followed literally, lands on a part that fails it.

It also fails on a number nobody looked for: **MCP73123's fast-charge current is
programmable from 130 mA to 1100 mA.** On a 100–150 mAh cell, 130 mA is **0.87–1.3 C**, and
it is the *minimum*. For comparison, Varta's CP 1654 specifies standard charge at 0.5 C and
rapid charge at 1 C. **The MCP73123 cannot be programmed to a standard charge rate for a
cell this size.**

**B1.4 — the firmware low-battery threshold is on the wrong chemistry.** The README asks for
"a firmware low-battery threshold above the protection IC's cutoff, **around 3.4 V**". A
LiFePO4 cell's discharge plateau is **3.25–3.30 V**, with the knee below 3.0 V and nothing
useful left by 2.5 V. **3.4 V is above the entire plateau** — on LFP that threshold fires at
approximately full charge. It is a lithium-cobalt number (where 3.4 V is a sensible 10–20%
warning) that survived the chemistry change. The LFP equivalent is about **3.05 V under
load**, and the README's own observation that the flat curve makes state-of-charge hard to
read is the reason it needs stating carefully rather than inheriting.

**B1.5 — what to do instead, costed.** Three candidates, and the interesting one is third.

| | LiPo pouch | LiFePO4 pouch | **Li-ion hard coin** (Varta CoinPower) |
|---|---|---|---|
| verified example at ~100 mAh | — | — | **CP 1654: 100 mAh, 16.1 × 5.4 mm, 3.2 g** |
| specific charge | 48 mAh/g (README) | 33.3 mAh/g (README) | **31.3 mAh/g** (datasheet) |
| full-charge voltage | 4.2 V | 3.6 V | 4.20 ± 0.05 V |
| runaway onset | 150–200 °C | ~270 °C | 150–200 °C |
| case | soft, tabbed | soft, tabbed | **hard steel can** |
| orderable, with datasheet | partly | **no, at this size** | **yes** (Avnet Abacus; UL MH13654) |
| thickness | ~4 mm | ~4 mm | **5.4 mm** |
| discharge / pulse | — | — | 2 C / 3 C @ 2 s — 18× the 11 mA load |
| documented abuse testing | — | — | **overcharge at 12 V/3 C/12 h passed** |

**The coin cell is not a mass win, and an earlier draft of this review said it was.** That
came from extrapolating the 70 mAh CP 1254 A4 (1.8 g, 38.9 mAh/g) across the family. The
actual 100 mAh part is **3.2 g against LFP's 3.0 g — 6% worse**, because the steel can is a
fixed overhead that the smaller cell amortises better. The coin's case rests on the hard
case, the distributor availability, the datasheet and the published abuse tests, **not** on
mass. Its costs are 5.4 mm of thickness, a 4.2 V rail, and Li-Co chemistry.

**A coin cell fits carrier A and does not fit carrier B.** 5.4 mm against the ~4 mm the
README budgets for the cell, in an assembly the README computes at ~4.8 mm total inside a
3–5 mm butt cap. That is the first place the two carriers demand different answers, and it
is developed in §3.

The Varta datasheet also settles a question the prompt raises — *is a separate protection IC
needed at all if the cell ships with an integrated PCM?* Varta's own answer, in bold in its
datasheet: **"Cell must not be used without external safety electronics (PCM — Protection
Circuit Module)!"** The cell does not ship with one. The protection IC is required by the
cell vendor, not by taste.

---

### B2 — The ICM-45686's AUX path carries **two** external sensors into its timestamped FIFO, at **400 Hz maximum** — and the 8 KB FIFO is conditional on giving up wake-on-motion. **Second, and it settles two of the design's own open questions.**

The README lists as *Not settled*: "Whether the ICM's AUX I²C master can carry the high-g
channel into its FIFO." The plan budgets a document-read for it. F13 says that if it can,
"the high-g channel inherits hardware timestamps and the problem goes away". **It can.**
DS-000577 §6.1 and AN-000478 §2.1 settle it, and they add two constraints nobody
anticipated.

**B2.1 — it works, and the frame layout is explicit.** DS-000577 §6.1 gives a **32-byte
frame**: 2-byte header, accel (6), gyro (6), ES0 (6 or 9), ES1 (6), temperature (1), and
**timestamp (2)**. The datasheet states that "the 32 bytes format is always selected when at
least one internal sensor and one external sensor are enabled", and the `TMST_FIELD_EN`
header bit is set when "either Accel or Gyro are enabled, and either ES0 or ES1 are
enabled". So hanging the ADXL375 off the AUX I²C master puts it in the same
hardware-timestamped stream as the gyro. **F13 is resolvable with no part change.**

**B2.2 — the README undercounts the slots.** *Six axes, not nine* closes with: "the ICM's
AUX I²C master … can carry one part. The high-g channel and a magnetometer compete for it,
and the high-g channel wins." AN-000478 §2.1: **"Through the I2CM, the host can access up to
two external sensors."** The frame carries ES0 *and* ES1. **There is no competition**, and
that argument should be withdrawn.

**B2.3 — so the magnetometer footprint gets cheaper, and it changes nothing else.** The
6-versus-9 decision keeps all its real arguments — gravity pins two of three orientation
degrees of freedom, the drift it corrects is on the wrong timescale, and a 100 Hz
magnetometer cannot resolve a 3,000 dps swing. It loses only the slot-contention argument,
which turns out never to have been true. Footprint-and-do-not-populate remains right, and
`MagProbe` remains the thing that settles it.

**B2.4 — and here is the cap: `EXT_ODR` maxes at 400 Hz.** AN-000478's
`DMP_EXT_SEN_ODR_CFG` register gives the external-sensor ODR ladder that kicks off the I²C
master: 3.125 / 6.25 / 12.5 / 25 / 50 / 100 / 200 / **400 Hz**. The README specifies the
ADXL375 at 800 Hz, and for a good reason — 800 Hz is the last rate its scale factor is
specified at. **The AUX path halves it.**

That is not fatal, and what it costs depends on which of the high-g channel's two jobs you
are asking about:

- **For the α baseline (§6), 400 Hz is better, not worse.** §6 sizes the two-accelerometer
  angular-acceleration measurement against the ADXL375's noise at ODR/2 bandwidth: ~100 mg
  RMS at 800 Hz. At 400 Hz the bandwidth halves and the noise falls to **~71 mg RMS**, so
  the SNR on a butt puck's 1.02 g α term rises from 10× to about 14×. α is a
  swing-timescale quantity and 400 Hz resolves it comfortably.
- **For the impact transient, 400 Hz is probably not enough.** A 200 Hz bandwidth against a
  ball-on-strings contact of roughly 1–5 ms is undersampling the thing the high-g channel
  exists to catch. The README already flags the high-g rate as answerable from one
  recording; this makes that recording decisive rather than informative.

**So the honest statement is that the ICM's AUX path serves one of the high-g channel's two
jobs and not the other**, and the design currently asks one part to do both.

**B2.5 — and it costs a doubling of the flash budget.** The frame goes from 16 bytes
(accel + gyro + temp + timestamp) to 32, whether or not the external sensor updated that
cycle. At 1 kHz that is **115 MB/h against 57.6 MB/h** — a 92% increase to carry a channel
sampling at 400 Hz. The 512 Mbit (64 MB) part holds 33 minutes of it. `ODR_DECIMATE_CONFIG`
can decimate the accel and gyro into the FIFO, which trades the 6-axis rate for the frame
overhead; that is a decision, not a fix.

**B2.6 — the 8 KB FIFO is not 8 KB by default, and the README, the adversarial review and
this review's first draft all assumed it was.** DS-000577: "Up to 8Kbytes FIFO buffer …
**default FIFO size is 2Kbytes, user can extend it up to 8kByte by disabling APEX
functions**", and the FIFO-depth register marks 2 KB as "(recommended setting)" and 8 KB as
"(valid when all APEX features are disabled)". AN-000478 §4.1 adds that the 8 KB SRAM is
shared between the eDMP's ROM features, the stack and the FIFO, with 1280 bytes gone to ROM
features.

**APEX includes WOM — wake-on-motion — which the README lists as a hardware requirement.**
F13's figure of "400 ms" of ICM FIFO slack assumed 8 KB unconditionally. Corrected:

| configuration | frame | FIFO | slack at 1 kHz |
|---|---|---|---|
| accel + gyro, default | 16 B | 2 KB | **128 ms** |
| accel + gyro, high-resolution | 20 B | 2 KB | 102 ms |
| **accel + gyro + external sensor** | 32 B | 2 KB | **64 ms** |
| accel + gyro, APEX disabled | 16 B | 8 KB | 512 ms |
| accel + gyro + external sensor, APEX disabled | 32 B | 8 KB | **256 ms** |

There is a clean way out and it should be written down as a requirement rather than
discovered at the bench: **wake-on-motion is needed between sessions and not during one.**
Arm WOM when idle; on session start, disable APEX, extend the FIFO to 8 KB, and record;
re-arm WOM at session end. That gives 256 ms of slack during recording with the external
sensor batched — still 6× the ADXL375's standalone 40 ms. It makes the FIFO depth
mode-dependent, which is a firmware-adjacent hardware requirement the document does not
currently have.

*One thing this review could not settle: whether routing external-sensor data into the FIFO
itself counts as an "APEX feature" for the 8 KB rule. AN-000478 says the eDMP reformats the
data before moving it to the FIFO, and the 8 KB rule is phrased in terms of APEX features
rather than the eDMP. If it does count, the bottom two rows of that table are unavailable
and the slack with an external sensor is 64 ms.*

---

### B3 — One part replaces two, and after B2 its case is narrower than it first looked. **Third.**

**ST LSM6DSV320X** (DS14623 Rev 3, October 2025) is a 6-axis IMU with a second, independent
high-g accelerometer on the same die and in the same FIFO.

| | ICM-45686 + ADXL375 | **LSM6DSV320X** |
|---|---|---|
| package | LGA-14 2.5 × 3.0 × 0.81 **plus** 14-LGA 3.00 × 5.00 × 0.80 | **LGA-14 2.5 × 3.0 × 0.83** |
| gyro | ±4000 dps, **3.8 mdps/√Hz**, ±0.2% tolerance | ±4000 dps, **3.8 mdps/√Hz**, ±0.3% tolerance |
| main accel | ±32 g, 0.98 mg/LSB, **110 µg/√Hz** | low-g ±16 g, 0.488 mg/LSB, **60 µg/√Hz** |
| high-g accel | ±200 g, **49 mg/LSB, scale factor ±10%**, 5 mg/√Hz | ±32–320 g, **1.95 mg/LSB at ±64 g**, **1 mg/√Hz** |
| **high-g rate into the FIFO** | **400 Hz max (B2.4)** | **480 / 960 / 1920 / 3840 / 7680 Hz**, and see HAODR below |
| buses | two | one |
| external clock input | **CLKIN, 20–40 kHz** | **none** |
| FIFO | 2 KB default, 8 KB with APEX off | 1.5 KB |
| current | 0.42 + 0.145 = 0.565 mA | ~0.895 mA |

**The high-g channel really does stream**, and this was checked rather than assumed: the
FIFO tag table (DS14623 Table 232) carries **two** high-g entries — `0x18` "High-g
accelerometer peak value" and **`0x1D` "High-g accelerometer"** — and `XL_HG_BATCH_EN` in
`COUNTER_BDR_REG1` enables batching. A part that only reported peaks would be disqualified
outright by §4.1's raw-samples requirement. (I read the tag table once, stopped at 0x1C, and
briefly had this backwards.)

**What B2 took away from this finding.** An earlier draft of this review credited the ST
part with dissolving F13, retiring the AUX open question, and freeing the magnetometer from
slot contention. **All three were already true of the ICM-45686** and the ST part gets no
credit for them. What survives:

1. **It is the only way to run the high-g channel above 400 Hz in a hardware-timestamped
   stream.** That is now the whole of the structural case, and it is a real one: it is
   exactly the job B2.4 says the AUX path cannot do.
2. **The high-g channel is 25× finer and 5× quieter** — 1.95 mg/LSB at ±64 g against
   49 mg/LSB, and 1 mg/√Hz against 5 — with a **±0.3% sensitivity tolerance against the
   ADXL375's ±10% scale factor**, which is the single worst-specified number in the current
   BOM and the reason §4.1 makes per-unit calibration mandatory.
3. **F2 is cleared axis-aligned.** ±64 g covers the 50.4 g peak without the wedge, so F15's
   moulded tilt stops being load-bearing for the accelerometer and buys gyro headroom only.
4. **One part, one bus, one placement, one calibration**, and an SPIM instance freed against
   §7 item 12's zero-spare allocation.
5. **HAODR mode offers ODR sets that include round numbers**, which the nominal ladder does
   not. `HAODR_SEL_[1:0]` selects four families (DS14623 Table 22): the default 960 Hz
   family, a **1000 Hz** family, an **800 Hz** family and an 833 Hz family. §6.1.4 states
   HAODR "is applied to the UI low-g, **high-g accelerometers**, UI gyroscope, EIS
   gyroscope, and temperature", so the high-g ladder should rescale with it — which in the
   800 Hz family would give 400/800/1600/3200 Hz, **the ADXL375's ladder exactly**. It
   costs about +20 µA at 960 Hz combo. *Flagged as an inference: Table 22 is printed for
   `ODR_XL`/`ODR_G`, and no equivalent HAODR table is printed for `ODR_XL_HG`. See §5.*

**What it costs.**

1. **No external clock input.** This is the serious one and it is B5.
2. **The FIFO word carries 17% overhead.** ST batches 1 tag byte + 6 data bytes. Three
   channels plus timestamp at 960 Hz is 28 B/ms — **101 MB/h**, against the 512 Mbit part's
   64 MB. Two ways out, both decisions: timestamp decimation ÷32 gives 76 MB/h; **dropping
   the low-g channel gives 51 MB/h and fits**. The second is genuinely available — AN6119
   §3.1 says "it is possible to enable the high-g accelerometer in standalone mode", and
   the low-g accelerometer may sit at ODR = 0, so this costs neither data nor the low-g
   channel's 200 µA. The constraint that does bind is that the low-g accelerometer must be
   *configured* in high-performance or high-accuracy ODR mode whenever the high-g channel
   is on; its low-power, normal and ODR-triggered modes are incompatible (DS14623
   Table 21). *For comparison, B2.5 puts the AUX path at 115 MB/h, so on flash budget the
   two options are close and both are worse than the current split-bus 60 MB/h.*
3. **The obvious third way out does not work, and this was checked.** ST's FIFO compression
   looks like it should absorb the overhead — "up to 4.5 KB" against 1.5 KB. AN6119 §9.10
   says otherwise on two counts. It applies **only to the low-g accelerometer and
   gyroscope, never to the high-g channel**. And it is slope-dependent lossless delta
   coding: 3× only when consecutive samples differ by **under 16 LSB**, 2× between 16 and
   128, **none above 128**. For the gyro at ±4000 dps (140 mdps/LSB), 128 LSB is 17.9 dps
   between samples; at 960 Hz and F15's 500 rad/s² working figure, consecutive samples
   differ by 28,650/960 = **29.8 dps = 213 LSB**. **The compression is off for the whole
   swing** — it is tuned for slow wearable signals and returns 1× exactly when the data rate
   is being set. It is also unsupported above 1920 Hz BDR and costs 2/BDR of latency. So
   1.5 KB, not 4.5 KB, sizes both the flash budget and the FIFO slack: **57 ms** at 960 Hz
   with three channels and a timestamp, against B2.6's 256 ms for a properly configured
   ICM-45686.
4. **The high-g channel is 9× noisier than the ICM's primary accelerometer** — 1000 µg/√Hz
   against 110 µg/√Hz at ±32 g. Irrelevant against a 50 g signal (0.04%), and still 4.5×
   better than the ADXL375 it replaces, but it is not a free upgrade on every axis.
5. **High-g zero-g offset is ±1.5 g**, against the ADXL375's ±400 mg typical — calibratable,
   and 3.75× larger.
6. **Maturity: the silicon looks settled, the software has not been.** This was chased
   properly and the answer is two-sided. **DS14623 is now at Rev 3 (October 2025)** and its
   revision history shows **no specification corrections**: Rev 2 (May 2025) updated the
   operating-mode descriptions and added a high-g filter block diagram, Rev 3 changed a
   footnote about gyroscope self-test. Every number quoted above was re-checked against
   Rev 3 and is unchanged from Rev 1. **No silicon errata document exists**, and unlike B6
   there is no anomaly list to read.

   Against that: ST's own component driver has had **twelve releases in fifteen months**,
   four of them major-version bumps, and the corrections land in exactly the areas this
   design depends on — V1.1.0 corrected "ODR values, register addresses, FIFO APIs";
   V2.0.0 fixed "FIFO batch counter functions and ODR enumerations"; V5.0.0 (January 2026)
   made "gyroscope data rate fixes … sensorhub validation enhancements"; and **V5.4.0, in
   July 2026 — two months ago — still "resolved improper behavior in accelerometer/
   gyroscope setup APIs".** That is not silicon errata and should not be reported as such,
   but it is the ecosystem this design would be building its timing on, and it was moving
   this year. One further flag: the gyro's ±0.3% sensitivity tolerance carries the
   footnote "**preliminary sensitivity tolerance … on first eng. samples**".

**Verdict: open.** The decision has a clean shape now: **400 Hz high-g with a disciplined
clock (ICM-45686 + ADXL375 on AUX), or 960 Hz+ high-g with a characterised one
(LSM6DSV320X).** Which is right turns on whether the impact transient needs more than
200 Hz of bandwidth — which is one recording, and which the README already has on its list.

**The rest of the 6-axis field, checked and not pursued.** No other current part reaches
±32 g with ±4000 dps: ICM-42688-P and ICM-42670-P are ±16 g / ±2000 dps, Bosch's BMI3xx is
±16 g / ±2000 dps, ST's LSM6DSV16X and ISM330 lines top out at ±16 g. The README's claim
that the range requirement narrows the field to one part is correct, and the LSM6DSV320X
widens it by adding a second accelerometer rather than by having a wider first one.

---

### B4 — With a 3.6 V chemistry the regulator can go, and the ADC divider goes with it. **Fourth. A §2 overturn, and conditional.**

The prompt lists "a regulator is mandatory (F3)" as settled, overturnable by "an LFP-only
design where every rail tolerates 3.6 V". Every rail does.

| Part | Recommended supply | LFP cell range, 3.6 V → 2.5 V |
|---|---|---|
| nRF52832 | 1.7–3.6 V | inside |
| ICM-45686 | 1.71–3.6 V | inside |
| LSM6DSV320X | 1.71–3.6 V | inside |
| ADXL375 | 2.0–3.6 V | inside |

**B4.1 — and the LDO is not neutral, it costs capacity.** On a flat-curve chemistry a
regulator's dropout eats the plateau, not the tail. A **3.3 V** output with 200 mV dropout
needs ≥3.5 V in, which on LFP exists only while charging — **unusable**. A **3.0 V** output
needs ≥3.2 V in, below the 3.25 V plateau bottom but above the knee — workable, and it
truncates. So the regulator's output voltage becomes a capacity decision in a way it never
was on a 4.2 V cell. Deleting it removes the decision.

**B4.2 — the counter is the charger's tolerance.** MCP73123 regulates to 3.6 V **+0.5%** =
3.618 V, 18 mV above the recommended maximum of every part in the table. That is inside
every absolute maximum (3.9 V for the nRF52832 and ADXL375) with 0.28 V to spare, but it
means "a full LFP cell is exactly the recommended maximum" holds only to the charger's
tolerance, not with margin. BQ25155's I²C-programmable `VBATREG` starts at exactly 3.6 V, so
it does not help either.

**B4.3 — deleting the regulator deletes the ADC divider too, and the arithmetic is exact.**
The nRF52832's SAADC can select **VDD as its own input** (`PSELP = VDD`); at 1/6 gain
against the internal 0.6 V reference the full-scale input is **0–3.6 V**, precisely the LFP
range, with no divider, no GPIO and no leakage. A 1 MΩ/1 MΩ divider on a 3.3 V rail leaks
**1.65 µA continuously** — roughly twice the LED's average current in the README's own
5 ms/5 s pattern, and about 180× the 9 nA platform standby delta the Nordic-versus-Espressif
argument was originally made on. Deleting a part and the largest remaining standby term in
one stroke is the shape of finding this BOM should be looking for.

**B4.4 — but the condition binds two other lines.**

- **The ADXL372 cannot be on this rail at all.** Recommended maximum 3.5 V, **absolute
  maximum 3.6 V** — no headroom above a full LFP cell, and *less* than the 2014 part it
  would replace (3.9 V). See B6.
- **A Li-ion coin (B1.5) puts 4.2 V on the rail and the regulator returns**, with the
  finding it was deleted by.
- **The LED colour is constrained.** Green and blue LEDs have Vf 2.8–3.2 V; at the 3.0 V end
  of an unregulated LFP discharge they will not light usefully. **Red, at Vf 1.8–2.0 V, is
  the only colour that works across the whole cell range.** See B8.4.

**Verdict: change, conditionally.** The regulator can go *if and only if* the design stays
on a ≤3.6 V chemistry and the high-g part is not the ADXL372. **That makes the regulator,
the cell chemistry and the high-g accelerometer one decision, not three** — the most useful
structural observation in this review, and why B1, B3, B4 and B6 have to be settled
together.

---

### B5 — The crystal is load-bearing, and not for the reason the README gives. It belongs to the IMU. **Fifth. The "right for the wrong reason" line.**

The README justifies the 32.768 kHz crystal as a board part for "usage-log timestamps across
weeks", and the prompt asks the obvious question: if the device docks after every session,
the cradle can reset the clock — so is it needed at all?

**It is needed, and the argument in the document is not the reason.** Sample timestamps come
from the sensor's own FIFO clock, not the MCU's RTC. That clock is an internal oscillator,
and its accuracy is the accuracy of every timestamp in the file. From DS-000577's
*Internal Clock Source* specifications:

| | value |
|---|---|
| Clock frequency initial tolerance, 25 °C | **±1.25%** |
| Frequency variation over temperature, −40 to +85 °C, **gyro active** | **±1%** |
| Frequency variation over temperature, gyro inactive | ±3% |

Worst case with the gyro running is about **±2.25%, which is 81 seconds per hour**; even the
room-temperature initial tolerance alone is ±1.25%, or 45 s/hour. The README's data contract
says "a racket sample and a wrist sample are useless together unless they can be placed on
one timeline". **A ±2% sample clock makes that unmeetable no matter how good the RTC is or
how often the cradle resyncs it**, because the error is inside the sample stream rather than
between the device and the world.

DS-000577 §4.14: "The CLKIN pin on ICM-45686 provides the ability to input an external
clock … External clock input supports highly accurate clock input from **20 kHz to 40 kHz**."
32.768 kHz is in range, and `RTC_MODE` in `RTC_CONFIG` enables it.

Three consequences:

1. **Keep the crystal, and route it to the IMU's CLKIN**, not only to the MCU's LFCLK. That
   is a schematic and layout requirement that appears nowhere in the document.
2. **1024 Hz is the sample rate that divides 32.768 kHz exactly** (÷32); 1000 Hz does not.
   A small, free argument for 1024 Hz in the open sample-rate decision.
3. **This is the strongest argument against B3.** The LSM6DSV320X has **no external clock
   input**. What it has is `INTERNAL_FREQ_FINE` (4Fh), an 8-bit two's-complement readout of
   "difference in percentage of the effective ODR (and timestamp rate) with respect to the
   typical", step 0.13%, with published formulas for the actual ODR and timestamp
   resolution. The host can *characterise* the oscillator but not *discipline* it, and the
   residual after correction is half a step: **±650 ppm, about 2.3 s/hour.**

   The honest reading is that this is recoverable but not free. ±650 ppm is a **stable scale
   factor, not jitter** — the same number all session — so a two-point sync (dock at the
   start, dock at the end, fit the drift) removes most of it, and the cradle exists. What it
   costs is that **`INTERNAL_FREQ_FINE` becomes a mandatory field in the recording header**,
   alongside the rotation matrix and the carrier identity, and the two-point sync becomes
   part of the session protocol rather than a nicety.

   ST's HAODR mode (B3, point 5) is the obvious rejoinder and it does not close the gap on
   paper: DS14623 §6.1.4 and AN6119 §3.2 both say only that it "**typically** reduces the
   part-to-part ODR variation", and **neither document gives a number**. It reduces
   part-to-part spread, which `INTERNAL_FREQ_FINE` already lets you measure per unit; what
   it does not claim to fix is drift over the −40 to +85 °C range, which is where the
   ICM-45686 spends ±1% even with the gyro running. So HAODR is worth enabling and it is
   not a substitute for a disciplined clock.

*Correction to an earlier draft: this finding previously quoted "±10,000 ppm improving to
±50 ppm" from a secondary source. DS-000577 gives ±1.25% initial and ±1%/±3% over
temperature, and specifies no post-CLKIN ODR tolerance at all — the inference that it
becomes the input clock's accuracy is standard but is not stated in the datasheet.*

---

### B6 — There is a deep-FIFO high-g part, and its own errata is what disqualifies it. **Sixth.**

The prompt asks directly: *does anything in this class have a deep FIFO, or hardware
timestamps, or both?* In the ±200 g class the answer is **ADXL372** (ADI Rev. C, 9/2022): a
**512-sample FIFO against the ADXL375's 32**, at **22 µA against 145 µA**, with a 4-pole
antialiasing filter, a bandwidth selectable independently of ODR, and an `EXT_SYNC` input
that lets an external trigger set the sample instants.

Then the datasheet's own **Silicon Anomaly** section, `er002 — FIFO Error`:

> **Issue:** In all FIFO modes, data misalignment occurs. Data may be stored in the FIFO as
> a y, z, x, … sequence or a z, x, y, … sequence.
> **Workaround:** Leverage the external trigger synchronization function to **disable the
> sensor ADC before accessing the FIFO.**

For the impact-capture application ADI designed it for that costs nothing — you read the
FIFO after the event. **For a device that records continuously for an hour it means the
high-g channel stops sampling at every FIFO read.** 512 samples is 170 three-axis sets,
213 ms at 800 Hz; a 1,024-byte SPI read at 5 MHz plus overhead is roughly 2 ms. That is a
**deterministic ~0.9% duty-cycle hole — a 2 ms gap every 213 ms**. Against a ball-on-strings
transient of 1–5 ms, roughly **0.9% of impacts lose their transient**, about 5 shots in a
500-shot session.

Bounded rather than catastrophic, and a decision rather than a fact. But it converts the
headline advantage into a trade, and the rest does not rescue it:

| | ADXL375 (Rev. B, 2014) | ADXL372 (Rev. C, 2022) |
|---|---|---|
| FIFO | 32 samples, no timestamp | **512 samples**, no timestamp, **er002** |
| scale factor | 49 mg/LSB, ±10% | 100 mg/LSB, ±10% — 2× coarser, same tolerance |
| RMS noise | ~100 mg at 800 Hz | **350 mg** — 3.5× worse |
| 0 g offset | ±400 mg typ | ±1 g typ, **−7 to +7 g** over the supply range |
| current | 145 µA | **22 µA** |
| supply / abs max | 2.0–3.6 V / **3.9 V** | 1.6–3.5 V / **3.6 V** |
| external sync | none | **EXT_SYNC, EXT_CLK** |

**The noise decides it.** §6 sizes the α baseline against the ADXL375's ~100 mg RMS: a butt
puck's ~20 mm separation gives 1.02 g at 500 rad/s², a 10× SNR, "measurable, but thin". At
350 mg that becomes **2.9×**, on the carrier the plan says to build first.

**Verdict: the line changes, but not to this part** — to the ICM AUX path (B2) or the ST
high-g channel (B3). The ADXL372 is a genuine near-miss and the reason it misses is worth
recording so it is not rediscovered. ST's **H3LIS331DL** was checked and is not a candidate:
1 kHz maximum ODR, no deep FIFO, so it solves neither problem better than what is there.

---

### B7 — microSD fails on retention arithmetic before it reaches the firmware argument. **Seventh.**

The prompt calls this "the single largest second-order question in the BOM". It resolves
cleanly, in the direction the README already chose.

**The attraction is real.** A microSD card is user-replaceable, removes bulk transfer
entirely, and changes the openness argument completely — a FAT-formatted card is readable
everywhere with no driver and no tool. It would also weaken the nRF52840-over-nRF52832
argument, since USB mass storage is what the '840 is being bought for.

**It fails on the shock numbers.** From §3.1: a butt tapped on the floor is ~200 g, a
dropped racket 1,000–2,000 g, a thrown one ~5,000 g. A microSD card masses about 0.4 g.

| event | card inertial load | vs retention |
|---|---|---|
| butt tap, ~200 g | 0.78 N | survives |
| dropped racket, 1,000 g | **3.92 N** | **at the limit** |
| dropped racket, 2,000 g | **7.85 N** | **past it** |

Published connector figures: consumer push-push microSD holders give **20–50 gf
(0.20–0.49 N) per contact**, described as insufficient against sustained vibration;
industrial locking types such as the DM3NW reach **4 N** card-lock retention; automotive
practice targets 80–150 gf per contact. **The best available retention is roughly equal to
the inertial load of an ordinary racket drop, and the consumer part is well under it.**

Ejection is not even the main failure. The documented mode is **micro-displacement breaking
contact intermittently, causing I/O errors and CRC failures** — in a continuous recorder,
a write failure mid-session, which is exactly what the LED exists to announce and what
`Squash` already lost 40 minutes of a match to.

Add a card slot as an ingress path into a sealed cavity in a sweaty bag; card firmware whose
wear levelling and garbage collection produce write latencies nobody controls, against a
fixed sample rate; and contact fretting in a saline environment.

**Verdict: SPI NOR holds.** SPI NAND remains the fallback if the payload grows — and B2.5
and B3 both grow it, so F4's analysis is now more likely to be needed than it was. eMMC is
a BGA that buys nothing here. The openness argument microSD would have won is better won by
USB mass storage over the cradle, which the design already reaches for.

---

### B8 — The smaller lines.

**B8.1 — the charger, resolved.** The right class is a programmable-voltage charger with a
real thermistor input, not a fixed-chemistry linear part. **BQ25155** (or BQ25150) gives I²C
`VBATREG` from 3.6 V to 4.6 V in 10 mV steps at 0.5% accuracy, a **TS pin with a 16-bit
ADC**, an `ADCIN` channel, 10 nA ship mode, and a 2.5 × 2.5 mm package. One part answers
F11's safety finding, covers both candidate chemistries in B1.5, and its ADC is a second
route to the battery-voltage channel. Costs, named: **WCSP at 0.4 mm pitch is a harder
assembly than DFN-10** and makes a fourth reflow-only part, pushing against §4.5's
reproducibility list; and I²C configuration is firmware you must be right about, on the part
that charges a lithium cell. *On the prompt's question of whether the MCU could do CC/CV
itself through a load switch and delete the IC: no. It moves the last analogue safety
interlock into firmware in a design others will build from, and the IC it deletes is 2.5 mm
square.*

**B8.2 — the protection FETs get the number the README asks for.** From the BQ2970
datasheet the thresholds are fixed voltages across the external FETs: **OCD = 0.100 V,
SCD = 0.5 V**. With a typical dual N-FET at 25 mΩ per device, 50 mΩ in series gives **2 A
overcurrent and 10 A short-circuit** — 13–29 C on a 70–150 mAh cell. Tripping near 1 C would
need ~1 Ω of FET, which does not exist in this class. **So the honest description is that
this circuit is a short-circuit protector, not an overcurrent protector**, and the design
should say so and let the PTC own the intermediate band. The README's instruction to pick
the FETs backwards from the desired trip current is right in method and reaches the
conclusion that the desired trip current is unattainable — worth writing down, because it
otherwise gets rediscovered at the bench.

**B8.3 — the off switch holds, and the shock number is what it holds on.**

| candidate | breaks load path | survives a tapped butt | visibly positioned |
|---|---|---|---|
| **slide switch, recessed** | **yes** | yes, if the load is on the housing not the actuator | **yes — the position *is* the indicator** |
| magnetic reed | yes | yes (no contact wear) | **no**, and a stray magnet closes it |
| hall + latching FET | no — it is a FET | yes | **no** |
| charger ship mode (10 nA) | no — it is a FET | yes | **no** |

Reed and hall both fail the requirement the README puts most weight on: the switch is the
armed/disarmed indicator *because it is mechanical and needs no power*. The 10 nA ship mode
in B8.1 is electrically equivalent to off and is not the same claim — a FET cannot answer
"is it off" to somebody looking at it, which is the whole argument in the microphone case.
**Keep the slide switch.** The design requirement it generates: the actuator must not be the
load path for a floor tap — the housing takes the impact, the switch is recessed behind it.

**B8.4 — the LED holds, with one constraint the README does not state.** The power
arithmetic checks out: 2 mA at 10 ms/1 s is 20 µA, and 0.5 mA dimmed at 5 ms/5 s is 0.5 µA,
so "the light can be almost free" is correct and the duty cycle really is set by not
distracting anybody. One LED carries the three states listed, because "stopped" is
distinguished by pattern *and* brightness rather than colour. **The constraint is B4.4: on
an unregulated LFP rail only a red LED (Vf 1.8–2.0 V) works down to the 3.0 V end.** A green
or blue one at Vf 2.8–3.2 V dims and stops exactly when the battery is low — the moment the
light is most needed, and a failure that looks like the device having died.

**B8.5 — the module holds.** A bare SoC is smaller and cheaper and forfeits the modular
grant, which F10 establishes is the reason the module is there. The nRF5340's second core
addresses an ISR-jitter concern that §4.1 #6 already notes "largely evaporates" once bulk
transfer moves to the cradle — an imagined problem here, at the cost of a larger module.
**The '832-versus-'840 decision is the README's and stands as open**; B3 would relieve the
pin-and-peripheral pressure on the '832 by one SPIM, which weakens one supporting argument
for the '840 without touching the USB one, and the USB one is the argument that matters.

**B8.6 — the contacts hold, and one alternative is disqualified by a dimension.** Recessed
gold-flashed pads with the spring pins on the cradle is right, for a reason worth recording:
**the wear part is on the replaceable side**. A USB-C receptacle in the cap needs an opening
of roughly **9 × 3.3 mm** in a butt-cap face of 25–30 mm — about a third of the width, and a
hole straight into the cell cavity of a sealed object that gets struck. Magnetic connectors
put a magnet a few millimetres from the magnetometer footprint the design is deliberately
keeping open. On the gym-bag short: **MCP73123 lists integrated reverse-discharge
protection**, so the charge pads cannot back-drive the cell — which is what makes exposed
pads acceptable, and is a property to require of whatever charger is chosen rather than a
property of pads in general.

**B8.7 — the things nobody lists, which is a gap rather than a swap.** ESD protection on the
charge and data contacts is absent from the BOM and they are the one user-touchable surface;
a TVS array on four lines is one part. Decoupling and the antenna keepout are covered in
spirit by the instruction to treat the assembly as a shielded cavity. The PTC holds and is
cheap redundancy given B8.2. The **adhesives and tapes are a real gap**: consumable parts
with shelf lives and temperature ratings, load-bearing for §3, and none specified.

**B8.8 — the printed parts have a material requirement the car boot sets, and it costs
reproducibility.** Measured car-cabin air on a summer day reaches **65–70 °C** at 35 °C
ambient.

| material | Tg | HDT |
|---|---|---|
| **PLA** | 55–65 °C | **50–60 °C** |
| PETG | 75–85 °C | 60–70 °C |
| ABS | 105 °C | 95–105 °C |
| ASA | 100 °C | ~95 °C |

**PLA is at or below its glass transition in a hot car**, and a PLA sled or puck under §3's
preload will creep and lose it. PETG is marginal at 65 °C *under load*, which is the
condition that matters. **ASA is the right answer** — ABS-equivalent temperature performance
plus UV stability. The second-order cost: **ASA needs an enclosed, heated-chamber printer,
and PLA is what most people reproducing this design will own.** The car-boot requirement
quietly raises the bar on who can reproduce the mechanical parts, and the design should say
so rather than let somebody print it in PLA and find out in July.

---

## 3. Battery retention — the answer, specific enough to draw

### 3.1 What shock the cell actually sees

Deceleration is `a = v²/2d`, and `d` — the stopping distance — is the term the design
controls. With `v = √(2gh)`:

| event | impact speed | stopping distance | deceleration |
|---|---|---|---|
| butt tapped on the floor (habitual, ~0.1 m) | 1.4 m/s | 0.5 mm (rigid) | **~200 g** |
| racket dropped, 1.0 m, rigid cap on sprung wood | 4.43 m/s | 0.5 mm | **~2,000 g** |
| same, with 2 mm of elastomer crushing 1 mm | 4.43 m/s | 1.0 mm | **~1,000 g** |
| racket thrown | ~7 m/s | 0.5 mm | **~5,000 g** |

**The first design lever is not the retention, it is `d`.** Acceleration goes as 1/`d`, so
doubling the crush distance halves the shock everything downstream must survive. Compliance
between housing and cell is worth more than a stronger bond.

The ADXL375's 10,000 g survival covers all of this, as §7 item 7 notes. **It is the only
part in the BOM with a shock rating**, and that is the finding: nothing says what the cell,
the module, the LGA solder joints or the crystal tolerate.

### 3.2 What each retention method carries

Inertial load on a 3 g cell is `3 g × n` — 5.9 N at 200 g, 29 N at 1,000 g, 59 N at
2,000 g, 147 N at 5,000 g.

| method | capacity | fails at | replaceable by |
|---|---|---|---|
| **spring-contact holder** (coin retainer) | 1–3 N preload | **~100 g** | the owner |
| microSD-class latch (for comparison, B7) | 4 N | ~1,000 g on a 0.4 g card | the owner |
| **soldered tabs alone** | ~100 N (joint-limited) | ~3,400 g, **and fatigues** | a soldering iron |
| **structural adhesive pad**, 15 × 15 mm at 0.5 MPa | ~112 N | ~3,800 g | destructive |
| **foam preload in a pocket** | reduces the load at source via `d` | — | anyone |

**The spring holder is disqualified by arithmetic, not by taste.** A 1–3 N preload against a
3 g cell fails at about 100 g — below a deliberate butt tap. That kills the coin-cell
retainer, and it is the same arithmetic that kills microSD in B7 and would have killed a
user-replaceable CR2032 if the duty cycle had not already done so.

### 3.3 The answer

**Carrier A (butt puck) — a hard-cased cell, tab-welded, foam-preloaded, mechanically
decoupled from its solder joints.**

- **A hard-cased Li-ion coin (B1.5), not a pouch.** The puck sits outside the tube and is
  not thickness-constrained, so it can take the 5.4 mm. The steel can removes the pouch's
  tab and seal fragility, and it is the only format at this size with a real datasheet, a
  distributor and published abuse testing.
- **The cell sits in a pocket in the housing, preloaded by 2 mm of closed-cell foam
  compressed ~30%**, so it never moves relative to the housing and the foam is the crush
  distance that sets `d`. This is the load path.
- **Its tabs are formed into a short S before the PCB pad**, so the joint is loaded in shear
  rather than peel, and the tabs carry current only — never the inertial load.
- **No adhesive on the cell.** Adhesive makes replacement destructive, and the foam and
  pocket already carry it.
- **Replaceable by anyone with a soldering iron.** Two screws open the puck, the cell lifts
  out, two tabs are unsoldered.

**Carrier B (insertable plug) — a pouch, because the coin does not fit, and the sled is the
pocket.**

- A 5.4 mm coin does not fit a 3–5 mm cap in an assembly the README computes at ~4.8 mm.
  **The plug keeps a pouch cell**, and therefore keeps the pouch's retention problem.
- **The sled is the pocket**: the cell sits in a printed channel with foam on both faces,
  and the sled's walls — not the tabs, not an adhesive — take the load. The sled is already
  a per-racket parametric part, so the pocket costs nothing extra to parameterise.
- **The tabs run the length of the sled to the module in the cap** and want a service loop,
  because the sled and cap move relative to each other on every insertion.
- **Replaceable by removing the butt cap and withdrawing the sled**, then the same soldering
  job. The cap is already removable and the sled already withdrawable.

**The two carriers do not get the same answer, and what separates them is 5.4 mm against
4.8 mm.** That is the mass-and-volume number the prompt's §3 asks for.

### 3.4 What "replaceable" means, and why

**Pick: replaceable by anyone with a soldering iron, not by the owner with a screwdriver.**

The owner-replaceable option requires a spring holder, and §3.2 disqualifies spring holders
at ~100 g against a device that sees 200 g on an ordinary tap. There is no version of
owner-replaceable that survives the mechanical environment, so the choice is not between two
good answers — it is between a soldering iron and a sealed unit.

It is also the right reading of the open-hardware goal. The design already requires paid
reflow for three LGA and WSON parts (four with B8.1), so **the person who can build this
device can already solder**, and a repair standard stricter than the build standard would be
incoherent. The design owes that person two things it does not currently promise: **the
cell's pocket must be reachable without destroying anything** (hence screws, not glue, and
no adhesive on the cell), and **the cell must be a part they can buy** — which is B1, and is
why B1 is the largest finding here.

### 3.5 Is 100–150 mAh right?

At ~11 mA, **70 mAh is 6.4 h — six sessions**, against the README's target of nine to
fourteen. Halving the cell buys about **1.2 g**, which by the README's own 2.34 mm/g scaling
is **2.8 mm of balance shift, roughly half a just-noticeable difference** — the difference
between about 2.3× and 1.8× JND. For a device that docks after every session, six sessions
of reserve is already more than the charging model needs, and F14's argument that the cell
was sized for a model the design replaced applies again one size down.

**Verdict: open, and it is a playing judgement rather than a calculation.** It should be
decided inside Phase −1 item A, which already varies mass on court, rather than at a desk.

---

## 4. What in §2 I think is wrong

**Overturned: "LiFePO4 over lithium-cobalt."** See B1. The physics argument is sound and the
sourcing argument was never made. The prompt named the overturning condition exactly and it
is met.

**Overturned, conditionally: "a regulator is mandatory."** See B4. Every rail spans the whole
LFP range, the LDO costs plateau capacity rather than being neutral, and deleting it deletes
the ADC divider. The condition — a ≤3.6 V chemistry and not the ADXL372 — is the coupling in
B4.4, and if B1 lands on a 4.2 V coin cell the regulator returns.

**Not overturned, but weakened: "the IMU mounts on its body diagonal, buying √3."** The
geometry in F15 is exact and nothing here touches it. What changes is what it is *for*. It
was the cheapest answer to F2 because ±32 g axis-aligned rails across the plausible range; a
±64 g channel (B3) clears F2's 50.4 g peak axis-aligned. The wedge then buys gyro headroom
only, and a moulded tilt in an enclosure seat is not free. **It should stay** — the gyro
benefit is real and the instruction to pick the angle from a recorded distribution rather
than from symmetry is the right method — but it stops being load-bearing, and a finding that
stops being load-bearing should be re-costed rather than inherited.

**Two corrections to the documents themselves, neither in §2.**

- **"The ICM's AUX I²C master … can carry one part"** — it carries two, ES0 and ES1
  (AN-000478 §2.1, and DS-000577's 32-byte frame). The slot-contention argument in *Six
  axes, not nine* should be withdrawn. See B2.2.
- **"8 KB FIFO"** — the default is 2 KB, and 8 KB requires all APEX features disabled,
  including the wake-on-motion the README lists as a hardware requirement. F13's "400 ms"
  of ICM slack is 128 ms as configured, or 64 ms with an external sensor batched. See B2.6.

**Everything else in §2 stands.** The ±32 g / ±4000 dps requirement (F2), the two-carrier
architecture, Nordic on active current, raw-LSB recording and the licence choice were all
checked against the parts considered here and none moved. On the openness constraint
specifically: **every part named in this review has a public datasheet with no NDA** — TDK's
DS-000577, AN-000478 and AN-000265, ST's DS14623 and AN6119, ADI's ADXL372 Rev. C, TI's
SLUSBU9I and BQ25155, Microchip's DS22191E and Varta's cell datasheets were all read for
this review, which is the test.

---

## 5. What could not be settled

| Open | What would settle it |
|---|---|
| **Whether external-sensor-to-FIFO counts as an "APEX feature"** for the 8 KB FIFO rule. If it does, B2.6's 256 ms becomes 64 ms and the AUX path gets materially less attractive. AN-000478 describes the eDMP reformatting the data; the 8 KB rule is phrased in terms of APEX features. | One bring-up: set `FIFO_ES0_EN`, request the 8 KB depth, and read back whether it took. |
| **Whether 400 Hz is enough for the impact transient.** B2.4 turns the AUX-versus-ST decision on this, and the README already calls it answerable from one recording. | One recording of ball-on-strings at 3,200 Hz, then decimate and see what is lost. This is now the cheapest decisive experiment in the BOM. |
| **Gyro g-sensitivity at 50 g.** TDK's AN-000265 names sensitivity to linear acceleration as a deterministic IMU error. **Neither DS-000577 nor DS14623 specifies it** — both give sensitivity tolerance and cross-axis sensitivity and stop. This device runs its gyro at a *sustained* 50 g, which is where the term stops being a footnote. | A rate table with a centrifuge arm, which nobody here has — or a direct question to TDK and ST. Worth asking, because it is a systematic error proportional to the dominant load. |
| **Whether HAODR rescales the high-g ODR ladder.** B3 point 5 infers that it does, from §6.1.4's statement that HAODR applies to the high-g accelerometer. If it does, the 800 Hz family gives the ADXL375's ladder exactly and part of B2.4's rate argument changes shape. DS14623 prints the HAODR table for `ODR_XL`/`ODR_G` only. | One bring-up: set HAODR_SEL = 10, enable the high-g channel, and time the data-ready interrupt. Or ask ST. |
| **How much HAODR actually buys on ODR variation.** Both DS14623 and AN6119 say "typically reduces" and give no figure, so B5's ±650 ppm stands as the only quantified number for the ST part. | An ST characterisation report, or measuring a handful of parts against a reference clock. |
| **LiFePO4 at 100–150 mAh from a supplier with a datasheet.** B1's search covered DigiKey and Mouser; it did not cover Asian distributors or a direct approach to a cell maker. | A quotation request naming UN 38.3 and IEC 62133 documentation as a requirement. If nobody will supply that paperwork at this size, B1 is settled for good. |
| **Varta CoinPower availability in ones and twos.** CP 1654 is Avnet-distributed rather than DigiKey-stocked, so a hobbyist's ability to buy one is not established — which matters only because reproducibility is a requirement. | A distributor check at quantity 1. |
| **The actual shock the cell sees.** §3.1 derives 200–5,000 g from plausible drop heights and *assumed* stopping distances, and the answer is most sensitive to the assumed term. | An accelerometer in the puck and twenty drops onto a court floor. The high-g channel is the instrument, so this is free once one board exists. |
| **Per-unit calibration at 50 g.** TDK's AN-000265 is explicit that full calibration needs a **2-DOF rate table**, and temperature calibration needs that table **inside a temperature chamber** for a ~7-hour soak sequence per unit. Neither is available to somebody reproducing this design. The affordable procedure is **6-side flip** (no machine — and the puck's own six faces are the fixture), which yields offset and sensitivity **at 1 g**. | Nothing cheap. The honest response is to publish the 6-side-flip jig, record the temperature the calibration was done at, and **state in the file that the scale factor is a 1 g figure extrapolated to a 50 g operating range** — rather than let a reader assume it was calibrated where it is used. Note this matters far more for the high-g channel (ADXL375 ±10% scale factor) than for the primary IMU (ICM-45686 ±0.2%). |

---

## 6. Sources

Read in full or in the named sections, all 2026-09-17.

- **TDK ICM-45686, DS-000577 Rev 1.0** — §3 Electrical Characteristics (gyro 3.8 mdps/√Hz
  and ±0.2% tolerance; accel 110 µg/√Hz at ±32 g and ±0.2%; **internal clock initial
  tolerance ±1.25%, variation over temperature ±1% gyro-active / ±3% gyro-inactive**),
  §4.14 Clocking (**CLKIN, 20–40 kHz**), §6.1 Packet Structure (8/16/20-byte internal
  frames; **16/20/32-byte external-sensor frames**, the 32-byte layout with ES0, ES1,
  temperature and timestamp), §6.2 FIFO header (`TMST_FIELD_EN`), the FIFO-depth register
  (**2 KB default/recommended; 8 KB "valid when all APEX features are disabled"**), and
  `RTC_CONFIG`.
- **TDK AN-000478 Rev 1.0** — §2.1 I2CM structure (**"up to two external sensors"**;
  eDMP reformatting into FIFO), `DMP_EXT_SEN_ODR_CFG` (**`EXT_ODR` ladder, 3.125 Hz to
  400 Hz maximum**), §3.6 Setting up FIFO for external sensor (`FIFO_ES0_EN`, `FIFO_ES1_EN`,
  `FIFO_ES0_6B_9B`), §4.1 APEX overview (**8 KB SRAM shared; 1280 B to ROM features, 6192 B
  left for stack and FIFO**).
- **ST LSM6DSV320X, DS14623 Rev 1 (March 2025) and Rev 3 (October 2025)**, read against each
  other — Table 3 (sensitivities, noise densities, ODR ladders, offset accuracies, and
  footnote 2's "preliminary … first eng. samples" on gyro sensitivity tolerance), Table 4
  (supply, current, temperature), §6.1.2 and Table 21 (**high-g requires the low-g
  accelerometer in high-performance or HAODR mode**), §6.1.4 and Table 22 (**HAODR ODR
  families, including the 800 Hz and 1000 Hz sets**), Tables 149/150 (high-g ODR and full
  scale), §6.10/§6.5 (FIFO contents and size), Tables 43/44 (`XL_HG_BATCH_EN`), Table 232
  (FIFO tags including **0x1D** and 0x0E–0x11), §9.42 (timestamp resolution 21.7 µs), §9.54
  (`INTERNAL_FREQ_FINE`), and **Table 549, the document revision history**.
- **ST AN6119 Rev 1** — §3.1/§3.2 operating modes (**high-g standalone is possible; low-g may
  be at ODR = 0**), §9.10 FIFO compression (**low-g and gyro only**; slope thresholds at 16
  and 128 LSB; unsupported above 1920 Hz BDR; 2/BDR latency) and §9.10.1 time correlation.
- **ST `lsm6dsv320x-pid` driver release notes** (GitHub, V1.0.0 April 2025 through V5.4.0
  July 2026) — read for maturity evidence, not for specifications. See B3 point 6.
- **ADI ADXL372 Rev. C (9/2022)** — Table 1 (scale factor and ±10%, 0 g offset, RMS noise,
  supply, current, ODR), Table 4 (absolute maximum 3.6 V, 10,000 g), *Using an External
  Clock* and the `TIMING` register, FIFO section, and **Silicon Anomaly `er001`/`er002`**.
- **TI BQ2970/BQ29700, SLUSBU9I** — Device Comparison Table (OVP 4.275 V, UVP 2.800 V,
  OCD 0.100 V, SCD 0.5 V).
- **TI BQ25155** — I²C `VBATREG` 3.6–4.6 V in 10 mV steps at 0.5%, 16-bit ADC with TS and
  ADCIN, 10 nA ship mode, 2.5 × 2.5 mm.
- **Microchip MCP73123/223, DS22191E** — features (3.6 V ±0.5%, 130–1100 mA, integrated
  reverse-discharge protection), Table 3-1 pin function table (**no thermistor pin**), §5.9
  thermal regulation on die temperature.
- **Varta CoinPower CP 1654 (type 63165)** — 100 mAh, 16.1 × 5.4 mm, **3.2 g**, charge
  4.20 ± 0.05 V at 0.5 C standard / 1 C rapid, 2 C continuous and 3 C pulse discharge,
  discharge −20 to +60 °C, <0.4 Ω, >500 cycles at 0.2 C, UL MH13654, overcharge tests
  passed. **CP 1254 A4** — 70 mAh, 12.1 × 5.4 mm, 1.8 g, and the PCM requirement.
- **TDK AN-000265 Rev 1.1** — deterministic error list including g-sensitivity, the 2-DOF
  equipment requirement, the soak and ramp temperature procedures, and the 6-side-flip and
  sphere-fit fallbacks.

Secondary, and flagged as such in the text: distributor catalogue searches for LiFePO4 stock
(DigiKey/ZEUS, Mouser); microSD connector retention figures (connector-vendor and
industrial-integrator publications, not a single datasheet); car-cabin temperature
measurements; 3D-printing filament Tg and HDT ranges; LiFePO4 discharge-curve shape.
