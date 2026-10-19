# Decide the first buildable SquashSensor from the facts alone

You are choosing the hardware for the first prototype of an open-source motion logger
that clips onto the butt of a squash racket. The current proposal grew one finding at a
time, and some of its parts answer concerns that later measurements removed. **Decide
again, from what is known now.**

Everything you need is in this document, with a source for every number. **Do not read
anything else in `SquashSensor/Docs/` or the reasoning in `SquashSensor/README.md` until
you have written your decisions.** Those files record how the design got here, not what
constrains it. If you can browse, open the linked documents to check any number you lean
on, and price parts on live distributor pages.

---

## 1. The decision rule

Apply it in this order, and show it being applied:

1. **Meet every hard requirement in §3.** An option that misses one is out, however
   cheap. If you think a requirement is wrong, say so and show the arithmetic. Don't
   quietly relax it.
2. **Among what is left, choose the lowest complexity.** Count:
   - distinct parts;
   - sensors and the buses they need;
   - boards and reflow passes;
   - parts that are hard to hand-assemble;
   - firmware mechanisms the hardware depends on (clock sequencing, trigger logic,
     bad-block management, rate fitting);
   - measurements that must happen before layout can start.
3. **Break ties on cost.** Use the landed cost of 2 and of 3 home-assembled boards: parts,
   PCB and stencil, excluding tools.
4. **Add complexity or cost only for a named, quantified gain in outcome**, meaning a
   margin in g, dps, ms, µA, hours, dB or dollars, with its source. "More robust" and
   "best practice" are not numbers.

**Outcome means:**
- impacts and swings recorded without unrecoverable clipping or aliasing;
- every sample placed on one timeline;
- the board survives the shocks and heat in §3, and the cell is handled safely;
- a stranger can build it from public documents;
- it fits the puck.

The current proposal (§5) is a baseline, not a default. It has to win under this rule
like any other option.

---

## 2. Already decided — do not reopen

- **Carrier A.** A puck clips onto a mount taped under the grip wrap at the butt, outside
  the racket. An in-handle version exists on paper and is out of scope.
- **nRF52840 in a pre-certified module, using the module's own antenna at a board edge.**
  USB mass storage offloads 128 MB in about 2.7 minutes, against about 34 over BLE, and
  needs no maintained software. A module is used for its radio certification.
  - Propose a different module only if it is a certified nRF52840 module with a public
    datasheet, and is cheaper or smaller. If you do, give its FCC and ISED IDs.
- **Home assembly:**
  - stencil, SAC305 T4 paste, and a hotplate with a 30 × 30 mm heating area;
  - every soldered part on one side;
  - no via-in-pad;
  - nothing smaller than 0402.
- **A 4-layer board with a solid ground plane on layer 2.**
- **Open hardware, CERN-OHL-P 2.0.** Every part has a public datasheet and an orderable
  part number; nothing is under NDA.
- **The builder is in Canada.** DigiKey.ca and Mouser.ca sell no lithium cells into
  Canada. The cell below is orderable from PiShop.ca, Abra and Newark.
- **Budget.** About C$200 per board all-in was rejected as too much. Aim well below it.

---

## 3. Hard requirements, and the facts behind them

**What the device records** (the project's data contract):
- raw per-sensor LSB, unscaled, so saturation stays visible;
- per-unit calibration, the mount rotation matrix and the carrier ID, stored in the file;
- a timestamp for every FIFO batch, roughly every 5 ms, from a crystal-derived timer on
  the MCU. Sample times are never reconstructed from a nominal rate;
- the trigger threshold, the trigger instants and the gaps, also in the file.

Offload is over USB mass storage from a cradle, through spring-pin contacts on the puck
(VBUS, D+, D−, GND). BLE carries only status and a sync timestamp.

**Session length:** 90 minutes covers about 75% of sessions, 120 about 95%, and **180
about all of them**.

**Measured on a racket.** The rig was an Adafruit Feather nRF52840 Express, an ADXL375
breakout on SPI and TDK's EV_ICM-45686. The builder still has it, so it can run more
tests.

| # | Measured | Value |
|---|---|---|
| M1 | Batch timestamp noise, FIFO-watermark interrupt plus a 1 MHz timer off the crystal | **1.9 µs RMS** |
| M2 | Worst gap between FIFO reads while writing flash | 5.9 ms |
| M3 | Samples lost | none |
| M4 | ADXL375 real output rate at 3200 Hz nominal | **3096–3112 Hz, about 3% slow** |
| M5 | Drift within one session | **about 500 ppm, +1 Hz/min, thermal** |
| M6 | Error from assuming one fitted rate | **up to 4.9 ms per minute** |
| M7 | ADXL375 noise density on breadboard wiring | 7–10 mg/√Hz (datasheet: 5) |
| M9 | Largest peak captured unclipped at ±200 g | 130 g, a finger flick |
| M11 | **Ball contact duration**, 5 hits | **1.6, 3.5, 3.5, 5.2, 5.8 ms** |
| M12 | Light string hits | 21–37 g vector; highest single axis 53 g |
| M13 | Suspected frame hit | **74.5 g**, 1.6 ms contact |
| M14 | Frame ringing | **145 Hz** on 3 of 5 hits |
| M15 | Swing peaks at the throat, low-passed below 20 Hz | soft 4.0–6.2 g, medium 11.5–13.6 g, hard 23.0–27.6 g |
| M17 | Swing against impact, by jerk | 0.06 against 6.8 g/sample, about 100× apart |
| M18 | Impact axis | mainly Z; Z is perpendicular to the strings |

**Derived from those measurements and published figures:**
- **Peak centripetal acceleration at the sensor is 50.4 g** at a published 30.8 m/s head
  speed (a = ω·v).
- **Gyro rates at impact are 2,900–3,300 dps, with a tail past 4,000.** Every candidate
  gyro stops at ±4,000 dps, so that tail clips on all of them.
- **The high-g channel needs about 3.2 kHz.** A half-sine contact sampled N times is
  under-read by up to cos(π/2N): 57% for a 3.5 ms contact at 400 Hz, 1% at 3200 Hz.
- **The 6-axis channel needs at least about 800 Hz**, because of the 145 Hz frame mode.
- **Full-power shots have never been recorded.** Whether they exceed 200 g is unknown.
- **The reason this device exists:** a wrist IMU clipped 12.2% of epochs on the
  accelerometer and 3.4% on the gyro, across 7,084 epochs.
- **Clipping is per axis.** Putting the dominant load direction on a sensor's body
  diagonal buys √3 of range. Rotating the footprint gives only one degree of freedom, in
  the board plane; the full diagonal needs the board tilted.

**Mechanical.** These are estimates: **the butt cap has never been measured.**
- Plan area about 30 × 25 mm, which leaves 28 × 23 mm inside 1 mm walls.
- Thickness about 4.8 mm, if module, board and cell sit side by side in plan.
- Stacked instead: 0.8 mm PCB + 2.2 mm to the top of the module + 4.0 ± 0.3 mm cell =
  **7.0 mm nominal, 7.6 mm maximum**.
- Added mass: target 6–10 g, which is 4.1–6.9% of a 145 g frame. Shipped sports-implement
  sensors sit at 1.5–3.0%.
- Shock: about 200 g when the butt is tapped on the floor, 1,000–2,000 g if the racket is
  dropped, about 5,000 g if thrown.

**A placement check has been run.** KiCad's stock footprints were placed on a 26 × 25 mm
board with the cell stacked over everything except the antenna strip:
- every part fitted on one side, with courtyards covering 74% of the area outside the
  strip;
- the 25 mm dimension overruns the 23 mm inside the estimated walls;
- a top-view LED under the cell cannot be seen;
- a Tag-Connect TC2030-NL has three alignment holes through the board, and no position
  cleared the top-side parts.

**Heat.** A parked car reaches 65–70 °C. The cell may be discharged only between −20 and
60 °C, and charged only between 0 and 45 °C. The off switch is the mitigation.

**Power, estimated for the current proposal:**
- about 11 mA while recording, so about 13 hours from 150 mAh;
- a standby budget of about 95 µA while on and idle;
- a mechanical switch that breaks the load path, for the true off state.

---

## 4. Reference designs to lean on

- **Raytac's own reference schematics** are in the MDBT50Q spec, §8, pp. 35–46. Use §8.3
  for a supply below 3.6 V and §8.4 for USB power. Raytac also publishes the schematic of
  its MDBT50Q-DB-40 demo board:
  https://www.raytac.com/upload/catalog_b/a3d12bc457a69c92377e403666508bd0.jpg
- **Nordic's nRF52840 reference circuitry** is in the Product Specification, §7.3, p. 937.
  - Altium reference layouts: https://www.nordicsemi.com/Products/nRF52840/Compatible-downloads
  - DK (PCA10056) hardware files: https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK/Download
    — their license limits use to Nordic ICs.
- **Adafruit Feather nRF52840 Express** (the rig's MCU board):
  https://github.com/adafruit/Adafruit-nRF52-Bluefruit-Feather-PCB
  - MCP73831 charger, RT9080 or AP2112 3.3 V regulator, GD25Q16 QSPI flash, and a UF2 USB
    bootloader shipped from the factory.
  - Its README says it was not designed for low power.
  - The README says CC BY-SA, but the repository has no license file.
- **TI BQ25180EVM user's guide**, SLUUCH5B: https://www.ti.com/lit/pdf/sluuch5
- **Sensor evaluation boards:**
  - EV_ICM-45686: AN-000484, schematic Fig. 4.
  - EVAL-ADXL375Z: UG-598.
  - Adafruit ADXL375 breakout 5374: https://github.com/adafruit/Adafruit_ADXL375_PCB
  - ST STEVAL-MKI251A for the LSM6DSV320X: schematic in DB5503,
    https://www.st.com/resource/en/data_brief/steval-mki251a.pdf

---

## 5. The parts

**The current proposal, as the baseline:**
- **Radio and sensors:**
  - MDBT50Q-1MV2 module;
  - ICM-45686 and ADXL375, each on its own SPI bus;
  - LIS2MDL pads left unfitted.
- **Clock and storage:**
  - an OV-7604-C7 oscillator driving both the nRF's XL1 and the ICM's CLKIN;
  - W25Q01JV flash.
- **Power and protection:**
  - BQ25180 charger feeding a TPS7A0230 regulator;
  - BQ29700, PMDPB30XN FETs and an 0805L020 PTC;
  - an NTC on the charger's TS pin;
  - TPD4E05U06 on the contacts;
  - a PCM12 slide switch breaking the load path.
- **Interface:** one red LTST-C191KRKT LED; a Tag-Connect TC2030-NL for SWD.
- **Mechanics:** the cell stacked over the board; the IMU tilted on a moulded wedge.

**Prices** are in USD from DigiKey.ca unless marked, read from live pages on 28–29
September 2026. "LCSC" means JLCPCB's parts library on 28 September. Stock moves; re-check
anything you depend on. For CAD, use 1.4168 CAD/USD and 13% HST.

### MCU and radio: Raytac MDBT50Q-1MV2 (nRF52840)

- **Spec:** Version L, 2023-05-24 — https://www.raytac.com/download/index.php?index_id=43
- **Size:** 15.5 × 10.5 × 2.05 mm.
- **Antenna keep-out:** a 3.8 mm strip across the full board width, with no copper on
  any layer (§2.3).
- **On the module:** the 32 MHz crystal and a DC/DC inductor. **The 32.768 kHz crystal is
  not on it** (§8, p. 35).
- **Certification:** FCC ID SH6MDBT50Q; IC 8017A-MDBT50Q. The certificate's model list
  omits "V2", so check it.
- **Slow pins:** 21 GPIOs near the radio are limited to 10 kHz and must not carry SPI,
  I²C, UART or PWM (§2.6).
- **Price:** C$18.65 at Mouser.ca as Adafruit 4078. It is on order, with 772 due on 20
  October 2026.
- **nRF52840 Product Specification:** v1.11 (4413_417, 2024-10-01).
  - HTML, latest version: https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html
  - A PDF mirror: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6469/NRF52840-DK.pdf
  - QSPI supports 24- and 32-bit addressing (p. 595).

### Motion sensing

| Part | Ranges | High-g rate into FIFO | FIFO | Current | Clock | Package | Price, stock |
|---|---|---|---|---|---|---|---|
| **TDK ICM-45686** (incumbent, 6-axis) | gyro ±4000 dps, 3.8 mdps/√Hz; accel ±32 g, 110 µg/√Hz | — (no high-g) | 2 KB, or 8 KB with APEX off | 420 µA, 6-axis LN at 1.6 kHz | **CLKIN 20–40 kHz**, see below; internal ±1.25% initial, ±1% over temperature | LGA-14, 2.5 × 3 × 0.81 | $5.26 |
| **ADI ADXL375** (incumbent, high-g) | ±200 g (±180 min), 49 mg/LSB, 5 mg/√Hz | 3200 Hz, needs SPI ≥ 2 MHz | 32 samples = 10 ms | 145 µA at 3200 Hz | none | LGA-14, 3 × 5 × 1 | $14.63 |
| **ST LSM6DSV320X** (one part for both) | gyro ±4000 dps, 3.8 mdps/√Hz; low-g ±16 g, 60 µg/√Hz; **high-g ±32–320 g**, 10.4 mg/LSB at ±320 g, 1 mg/√Hz | **480–7680 Hz**; HAODR adds 3200 | 1.5 KB, or 4.5 KB compressed (high-g not compressed) | 0.8 mA, all three | none; HAODR ±1% with gyro on; `INTERNAL_FREQ_FINE` reads the rate error in 0.13% steps | LGA-14, 2.5 × 3 × 0.83 | $8.78, **0 in stock, no date** |
| **ST LSM6DSK320X** (sibling, "enhanced vibration immunity") | same ranges as LSM6DSV320X, per ST's product page | not checked | up to 4.5 KB | 0.67 mA 6-axis, 0.80 mA with high-g | not checked | LGA-14, 2.5 × 3 × 0.83 | $9.17, **468 in stock** |
| **ST ISM6HG256X** (industrial) | gyro ±4000 dps; high-g ±32–256 g; −40 to +105 °C; rated for 100,000 shocks at 300 g | 480–7680 Hz | 1.5 KB, or 4.5 KB compressed | 0.8 mA | none | LGA-14, 2.5 × 3 | $9.56, 0 in stock, 4,948 due 30 Nov 2026 |

Sources:
- ICM-45686:
  - datasheet DS-000577 Rev 1.0, https://d17t6iyxenbwp1.cloudfront.net/s3fs-public/2026-04/ds-000577-icm-45686-datasheet.pdf
  - user guide AN-000478 Rev 1.0, https://d17t6iyxenbwp1.cloudfront.net/s3fs-public/2026-02/AN-000478_ICM-45605-ICM-45686-User-Guide%20%282%29_0.pdf
- ADXL375: Rev. B, https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL375.PDF (status "recommended for new designs").
- LSM6DSV320X:
  - datasheet DS14623 Rev 3, https://www.st.com/resource/en/datasheet/lsm6dsv320x.pdf
  - application note AN6119, https://www.st.com/resource/en/application_note/an6119-lsm6dsv320x-6axis-imu-with-ai-highg-accelerometer-and-embedded-sensor-fusion-for-highend-applications-car-crash-detection-stmicroelectronics.pdf
- LSM6DSK320X: https://www.st.com/en/mems-and-sensors/lsm6dsk320x.html. **Its datasheet has not been opened.** Check its high-g FIFO behaviour and pinout before choosing it.
- ISM6HG256X: DS15034 Rev 2, https://www.st.com/resource/en/datasheet/ism6hg256x.pdf
- Shock ratings: 20,000 g for the ICM-45686, LSM6DSV320X and ISM6HG256X; 10,000 g for
  the ADXL375. Not checked for the LSM6DSK320X.

**Clock facts that decide between these:**
- **The ICM-45686's nominal CLKIN is 32.000 kHz, not 32.768** (AN-000478 §7.1). Its
  output rates scale by f_CLKIN ÷ 32 kHz, so a 32.768 kHz clock runs every rate 2.4% fast.
  - CLKIN works through an internal interpolator that re-times the samples.
  - The FIFO timestamp stays on the internal clock.
  - CLKIN shares pin 9 with INT2 and FSYNC.
  - The full-scale clamp comes after the interpolator (DS-000577, signal path).
- **The ADXL375 has no clock or sync input at all.**
- **ST's external-sync ("ODR-triggered") mode does not work with the high-g channel**
  (AN6119, p. 25).

**Data rates** (arithmetic):
- 6-axis at 800 Hz is about 46 MB/h.
- A high-g channel is about 69 MB/h if it runs continuously at 3200 Hz, or about 2 MB/h
  if it records only on a trigger.
- The trigger is on jerk, or on the 6-axis channel saturating.

**Optional magnetometer pads:** ST LIS2MDL, DS12144 Rev 6,
https://www.st.com/resource/en/datasheet/lis2mdl.pdf — LGA-12, 2 × 2 × 0.7 mm; not priced.

### Timebase

- **The nRF's 32.768 kHz crystal oscillator** (PS §5.4.4.3, p. 171):
  - load ≤ 12.5 pF, shunt capacitance ≤ 2 pF, ESR ≤ 100 kΩ;
  - **0.23 µA**;
  - BLE needs ±500 ppm.
  - Raytac is stricter: load ≤ 9 pF, ESR < 80 kΩ (Footprint Design Guide 230606, a RAR at
    https://www.raytac.com/document/act.php?act=1&index_id=30).
  - Candidate crystals, all 3.2 × 1.5 mm:
    - Abracon ABS07, 9 pF option: https://abracon.com/Resonators/ABS07.pdf — the only one
      with a guaranteed C0 maximum;
    - Epson FC-135R: https://download.epsondevice.com/td/pdf/app/FC-135R_en.pdf
    - Micro Crystal CM7V-T1A: https://www.microcrystal.com/fileadmin/Media/Products/32kHz/Datasheet/CM7V-T1A.pdf
  - Nordic's reference BOM uses a 9 pF crystal with 12 pF caps (PS Table 71, p. 941).
- **The nRF's internal RC oscillator:** 0.7 µA, ±500 ppm after calibration (recalibrated
  at least every 8 s), and ±5% uncalibrated (PS §5.4.4.4).
- **An external clock on XL1** is supported: rail-to-rail, with XL2 grounded (PS Table 8,
  p. 160; §5.4.2.4).
- **Micro Crystal OV-7604-C7**, a 32.768 kHz oscillator. Datasheet v4.5:
  https://www.microcrystal.com/fileadmin/Media/Products/kHzOSC/Datasheet/OV-7604-C7.pdf
  - 300 nA with its output off; driving 10 pF adds about 0.98 µA;
  - ±20 ppm;
  - SON-4, 3.2 × 1.5 × 1.0 mm;
  - its supply must rise no faster than 0.5 V/ms;
  - start-up 250–500 ms;
  - $2.11 at LCSC.
- **The nRF can generate the ICM's clock itself.** 16 MHz ÷ 500 = 32.000 kHz, from PWM or
  a timer, while the 32 MHz crystal runs. It needs one full-speed pin, not one of the 21
  limited to 10 kHz.
  - Microsecond timestamps need the 32 MHz crystal running anyway. The nRF's internal
    fast oscillator is ±1.5% typical and ±8% worst case.
  - The crystal draws 70–143 µA, depending on the part.
  - A timer on the crystal draws 646 µA at 1 MHz and 823 µA at 16 MHz (PS §5.2.1.9).

### Storage (all three share the WSON-8, 8 × 6 mm footprint)

| Part | Capacity | Idle current | Notes | Price |
|---|---|---|---|---|
| Winbond W25Q01JVZEIQ, NOR (incumbent) | 1 Gbit = 128 MB | power-down 1 µA typ | Two stacked dies. Busy status is per die. Ships with Quad Enable set, so pads 3 and 7 are IO2/IO3 and must not float. | $26.07 |
| Winbond W25Q512JVEIQ, NOR | 512 Mbit = 64 MB | power-down 1 µA typ | One die. | $15.31 |
| Winbond W25N01GV, SLC NAND | 1 Gbit | standby 10 µA typ; **no power-down mode** | Needs bad-block management; page program 250 µs. | not priced |

Datasheets:
- W25Q01JV Rev F: https://www.winbond.com/resource-files/W25Q01JV%20SPI%20RevF%2005252026%20Plus.pdf
- W25Q512JV Rev I: https://www.winbond.com/resource-files/W25Q512JV%20SPI%20RevI%20%2005052026%20Plus.pdf
- W25N01GV Rev R: https://www.winbond.com/resource-files/W25N01GV%20Rev%20R%20070323.pdf

### Power path, protection and cell

- **Cell: PKCELL LP402025**, 150 mAh (Adafruit 1317). Spec QA.S.0228:
  https://cdn-shop.adafruit.com/product-files/1317/C1515_-_Li-Polymer_402025_150mAh_3.7V_with_PCM.pdf
  - 25 × 20 × 4.0 mm, each ±0.3, including its protection board. The UN38.3 report
    measures 26.0 × 20.0 × 4.0 and weighs it at 4.5 g.
  - Maximum charge 150 mA (1C); maximum continuous discharge 150 mA.
  - Protection built in: ABLIC S-8261AAJMD, overcharge 4.325 V, overdischarge 2.500 V,
    3.5 µA (S-8261 datasheet:
    https://www.ablic.com/en/doc/datasheet/battery_protection/S8261_E.pdf). The FET, and
    so the cell's trip current in amps, is unknown.
  - **No thermistor built in.** Leads with a JST-PH plug.
- **Charger: TI BQ25180**, SLUSE99C: https://www.ti.com/lit/ds/symlink/bq25180.pdf
  - input 3–5.5 V, overvoltage trip 5.5–5.9 V, 25 V tolerant;
  - charge current 5–1000 mA; charge voltage 3.5–4.65 V;
  - battery drain 3 µA, or 4 µA with the push-button enabled;
  - ship mode 3.2 µA (wakes on the button); shutdown 15 nA (wakes on the adapter only);
  - one pin shared between the NTC and the push-button, with a 38 µA bias;
  - **DSBGA-8, 1.6 × 1.1 mm, 0.4 mm ball pitch.** TI's stencil is 0.25 mm square
    apertures in a 0.1 mm stencil (pp. 52–54). Moisture level 1, SnAgCu balls.
  - $3.01.
- **Regulator: TI TPS7A0230PDBVR**, 3.0 V, SOT-23-5. SBVS277C:
  https://www.ti.com/lit/ds/symlink/tps7a02.pdf
  - 25 nA quiescent;
  - ≤ 310 mV dropout at 200 mA;
  - $0.46 at LCSC.
- **Protector: TI BQ29700**, SLUSBU9I: https://www.ti.com/lit/ds/symlink/bq2970.pdf
  - overcharge 4.275 V, overdischarge 2.800 V;
  - overcurrent ±0.100 V and short circuit 0.5 V, both measured across the FETs;
  - 4 µA;
  - WSON-6, 1.5 × 1.5 mm;
  - $0.66 at LCSC.
- **FETs: Nexperia PMDPB30XN**, dual N-channel: https://assets.nexperia.com/documents/data-sheet/PMDPB30XN.pdf
  - 40 mΩ typical, 53 mΩ maximum at 2.5 V gate drive; DFN2020-6.
  - **Its two drains are separate pads**, so a common-drain pair needs them tied in
    copper.
  - Two in series put the protector's 100 mV trip at about **0.94–1.56 A**, 6–10× the
    cell's rating (arithmetic).
  - $0.30 at LCSC.
- **PTC fuse: Littelfuse 0805L020.** Hold 0.2 A, trip 0.5 A, **9 V maximum**; $0.27 at
  LCSC.
- **NTC: Murata NCP15XH103F03RC.** 10 kΩ, B25/50 3380 K, 0402; $0.02 at LCSC.
- **ESD: TI TPD4E05U06**, SLVSBO7O: https://www.ti.com/lit/ds/symlink/tpd4e05u06.pdf
  - 0.5 pF;
  - **5.5 V working voltage, so it suits D+ and D−, not VBUS**;
  - USON-10, 2.5 × 1.0 mm;
  - $0.08 at LCSC.

### Interface and mechanics

- **Slide switch: C&K PCM12SMTR.** C&K's PDF via Octopart:
  https://datasheet.octopart.com/PCM12SMTR-C&K-Components-datasheet-137916553.pdf
  - body 6.7 × 2.6 × 1.4 mm; actuator protrudes 1.5 mm;
  - 0.3 A at 6 VDC;
  - $1.78 at LCSC.
- **LED, top-view: Lite-On LTST-C191KRKT**, 0603 red:
  https://optoelectronics.liteon.com/upload/download/DS22-2000-223/LTST-C191KRKT.PDF
  - VF 2.0 V typical at 20 mA; $0.02 at LCSC.
- **LED, side-view: Lite-On LTST-S270KRKT**:
  https://optoelectronics.liteon.com/upload/download/DS22-2000-210/LTST-S270KRKT.pdf
  - 1.65 × 1.15 × 0.60 mm; emits parallel to the board.
- **SWD connector: Tag-Connect TC2030-NL**, footprint Rev B:
  https://www.tag-connect.com/wp-content/uploads/bsk-pdf-manager/2019/12/TC2030-IDC-NL-Datasheet-Rev-B.pdf
  - 6 pads, 0.787 mm, on a 1.27 mm grid;
  - 3 unplated holes, 0.991 mm, at (−2.54, 0) and (+2.54, ±1.016) mm.

### Boards and stencil per build

- **About C$50 landed:** OSH Park 4-layer, 1.6 mm only, three copies; plus an OSH Stencils
  0.10 mm stainless stencil.
- **About C$127 landed:** JLCPCB 4-layer 0.8 mm, five copies, plus stencil and UPS.

---

## 6. The decisions to make

They interact. Decide them as one design, and say where one choice forces another.

1. **Motion sensing.** ICM-45686 plus ADXL375, or one ST part carrying both. Weigh:
   - parts and buses;
   - range and resolution against §3;
   - FIFO depth against the 5.9 ms worst-case read gap;
   - clocking;
   - availability today;
   - whether the rig can test the choice first.
2. **Timebase.** Crystal only, shared oscillator, nRF-generated CLKIN, or the internal RC
   oscillator. Batch timestamps are required whatever you choose (§3).
3. **Storage.** Which part, given the answer to 1, the trigger scheme and 180 minutes.
4. **Battery protection.** The on-board protector, FETs and PTC, or the cell's own
   protection plus less. Use the thresholds in §5.
5. **Power path and off.** The charger, regulator and slide switch, or something simpler
   that still has a thermal interlock and a true off.
6. **Programming access.** Tag-Connect, bare pads with a pogo jig, or SWD once followed by
   a USB bootloader through the cradle contacts.
7. **Indicator.** An LED the user can see with a cell stacked over the board, or no LED.
8. **Envelope and mount.** Board outline against the puck, and whether the cell stacks
   over the parts or sits beside them. Also the sensor's mount angle, flat or on a wedge,
   and whether decision 1 changes the need for a wedge.
9. **Magnetometer pads.** Keep them unfitted, or drop them.
10. **Board and stencil.** Thickness, fab house and stencil thickness.

---

## 7. What to hand back

1. **One decision per item**, in a table: the choice, the runner-up, the deciding
   numbers with their sources, and what measurement would reverse it.
2. **The resulting BOM**: function, part number, package, quantity, price with its source
   and date (or "not priced"), and one line of why.
3. **Pin and bus plan**: every signal on the module. Name the full-speed pins used, and
   confirm nothing fast sits on the 21 pins limited to 10 kHz.
4. **Cost of 2 and of 3 boards**, landed in CAD, excluding tools.
5. **Complexity against the current proposal**: the §1 counts, side by side.
6. **Measurements needed before layout**, ordered by what each one unblocks. Include
   which the existing rig can do.
7. **Risks**, each with the number that bounds it.

**Honesty rules:**
- Every number comes from a named document (with table or page), a measurement from §3,
  or your own arithmetic, labelled as such.
- A datasheet figure is not a measurement.
- Prices come only from live distributor pages, with the date. Otherwise write "not
  priced".
- Where a choice rests on something unmeasured, say what would settle it and what you
  would do meanwhile.
