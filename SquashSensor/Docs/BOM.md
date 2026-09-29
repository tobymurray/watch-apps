# SquashSensor BOM — current state

**This document has no history in it.** It states what the design is now and the constraints
that bind it, so the whole thing can be attacked as a system rather than argued with one
finding at a time. [`BOM-REVIEW.md`](BOM-REVIEW.md) holds the derivations; nothing here
depends on having read it.

Carrier A — a puck that clips to the racket butt — is what is being built.

---

## 1. What the device has to do

Record raw, un-clipped, racket-frame swing kinematics to a documented format, on a squash
racket, for a session, and hand every sample to the wearer with no account and no cloud.

**It computes nothing.** Samples are written as raw per-sensor LSB, unscaled, on each
sensor's own clock, so saturation stays visible instead of being hidden behind a conversion.
Per-unit calibration coefficients, the mount rotation matrix and the carrier identity travel
in the file.

The design is published as open hardware anyone can rebuild: CERN-OHL-P 2.0 for the
hardware, MIT for firmware, CC BY 4.0 for documentation.

---

## 2. The constraints

Each is a fact about the world or a measurement, not a preference.

### Mechanical

| | |
|---|---|
| Plan area available | ~30 × 25 mm |
| Assembly thickness | ~4.8 mm — **the binding dimension**; module 2.2, PCB 0.8, cell ~4 will not stack |
| Added mass target | 6–10 g, 4.1–6.9% of a 145 g frame |
| Balance shift | 5 g net at the butt = **−11.7 mm ≈ 2.3× the just-noticeable difference**. Shipped implement sensors in other sports sit at 1.2–1.7× |
| Swingweight | negligible at the butt — the mass sits on the swing axis |
| Shock | butt tapped on the floor **~200 g**; racket dropped **1,000–2,000 g**; thrown **~5,000 g** |

**The butt cap has never been measured.** The plan area and thickness are estimates.

### Signal

| | |
|---|---|
| Peak centripetal acceleration at the sensor | **50.4 g** at a published 30.8 m/s head speed |
| Gyro at impact | 2,900–3,300 dps, tail past 4,000 |
| **Ball contact duration** | **1.6–5.8 ms**, measured |
| Peak error from undersampling | a 3.5 ms contact read at 400 Hz under-reads the peak by **57%**; at 3200 Hz, **1%** |
| Racket frame first bending mode | **145 Hz**, measured — sets a floor of ~800 Hz on the 6-axis channel |
| Wrist clipping this device exists to avoid | 12.2% of epochs accel-railed, 3.4% gyro-railed, over 7,084 epochs |

### Clocks

| | |
|---|---|
| ADXL375 real ODR | **3% slow**, wanders ~500 ppm within a session, trends with temperature |
| Cost of assuming one fitted rate | **up to 4.9 ms per minute** — so timestamps are per batch, never reconstructed from a nominal rate |
| ICM-45686 internal clock | ±1.25% initial, ±1% over temperature — **disciplined by a shared 32.768 kHz oscillator on its `CLKIN`**, not by a crystal (see Timebase) |
| Achieved host-bus timestamp noise | 1.9 µs RMS |

### Storage

| | |
|---|---|
| Session coverage | 90 min covers ~75% of sessions, 120 min ~95%, **180 min ~100%** |
| Payload | ICM at 800 Hz = 46 MB/h; ADXL375 triggered ≈ 2 MB/h |
| Result | **~48 MB/h → 160 min on 1 Gbit** |

### Environment and supply

- **A car interior reaches 65–70 °C.** Every lithium chemistry's discharge ceiling is 60 °C,
  so chemistry does not solve this — the off switch does.
- **DigiKey.ca and Mouser.ca sell no lithium cell of any kind into Canada.**
- **No LiFePO4 cell qualifies at 70–150 mAh** — not in Canada, and not anywhere with a
  datasheet. The chemistry is lithium-cobalt by elimination, not by preference.
- Every part must have a public datasheet and an orderable part number. Nothing under NDA.

### RF and certification

- The antenna sits outside the carbon tube; carrier A has no waveguide problem.
- **Use the module's own vendor-tested antenna at a board edge.** Relocating it voids the
  pre-certified modular grant, which is the reason a module is used at all.
- Treat the assembly as sitting in an unintended shielded cavity for grounding and
  decoupling.

---

## 3. The BOM

### Power

| Function | Part | Why |
|---|---|---|
| Cell | **PKCELL LP402025** (Adafruit 1317) — 150 mAh Li-ion pouch, 4.0 ± 0.3 mm, 4.65 g | The only cell with a maker's datasheet and a published UN 38.3 report that a Canadian can order in ones |
| Charger | **BQ25180**, DSBGA-8 | I²C `VBATREG`, `SYS` power path, `TS/MR` thermistor input, ship mode, 5–1000 mA charge. Eight balls, none buried, so it escapes like a QFN |
| Protection | **BQ29700** + dual N-FET + PTC | 4.275 V OVP is correct for this chemistry. **The FETs set the trip**: 0.100 V OCD across 50 mΩ is 2 A — this is a short-circuit protector, not an overcurrent one |
| Regulator | **TPS7A0230PDBVR**, fixed 3.0 V, SOT-23-5, fed from `SYS` | 25 nA quiescent. 3.0 V clears every part's minimum and keeps ~340 mV of dropout margin at the low-battery cutoff |
| Cell temperature | NTC on `TS/MR` | The one safety interlock. Sized so the hottest legitimate cell still reads above the button-press threshold |
| Battery sense | 2 × 1 MΩ divider into the nRF52840 SAADC | 2.1 µA continuous, 2.2% of standby |

### Compute and radio

| Function | Part | Why |
|---|---|---|
| MCU / radio | **Raytac MDBT50Q-1MV2** (nRF52840) | USB: 128 MB offloads in **2.7 min against 34 min over BLE**. Mass storage needs no software anybody maintains. 163 mm² against the '832 module's 160 |
| Timebase | **32.768 kHz active oscillator (XO), CMOS output** → MCU `XL1` *and* the ICM's `CLKIN` | Without it the sample clock is ±2%, which no RTC resync can fix. **A crystal cannot do this job**: `CLKIN` is a CMOS input needing 0.7 × VDDIO = 2.10 V and presenting <10 pF (DS-000577), against an LFXO node budgeted for 12.5 pF total whose swing Nordic does not specify. One XO replaces the crystal and its two load caps |

### Sensing

| Function | Part | Rate | Why |
|---|---|---|---|
| 6-axis IMU | **ICM-45686** | 800 Hz | Only current part at ±32 g with ±4000 dps. 800 Hz is set by the 145 Hz frame mode |
| High-g | **ADXL375**, own SPI, host bus | 3200 Hz | 1.6–5.8 ms contacts need it. **Not on the ICM's AUX** — that caps external sensors at 400 Hz |
| Magnetometer | footprint, **do not populate** | — | A channel not recorded cannot be added later; the pads cost nothing |
| Mount | moulded wedge, IMU on its body diagonal | — | Clipping is per-axis, so the dominant direction on the cube diagonal buys √3 |

### Storage and interface

| Function | Part | Why |
|---|---|---|
| Flash | **1 Gbit SPI NOR, 2.7–3.6 V** | 160 min. Needs 4-byte addressing; these are stacked dies |
| Contacts | recessed gold-flashed pads; **spring pins on the cradle** | The wear part belongs on the replaceable side |
| ESD | TVS array on every contact | The only user-touchable surface |
| Indicator | **one red LED** | 3.0 V leaves no headroom over a green or blue Vf. 5 ms every 5 s dimmed ≈ 0.5 µA |
| Switch | **mechanical slide, breaking the load path** | 15 nA against 3.2 µA for a wakeable ship mode — and a switch you can see beats firmware that says so |
| Enclosure | printed **ASA** | PLA is at its glass transition in a hot car; ASA needs an enclosed printer, which raises the bar on who can reproduce it |
| Cell retention | pocket + foam preload, tabs in shear, no adhesive | A spring holder fails at ~100 g, below a deliberate butt tap |

---

## 4. Recording architecture

- **ICM-45686 continuous at 800 Hz.** Nothing about the swing is discarded.
- **ADXL375 triggered**, on jerk **or** ICM saturation. Swing and impact separate by ~100× on
  jerk (0.06 against 6.8 g/sample); the saturation trigger exists so a clipped peak is never
  missed while the high-g channel is idle.
- **Timestamps per batch**, roughly every 5 ms. Never reconstruct a session from one rate.
- **The file records the trigger threshold, the trigger instants and the gaps.** Otherwise a
  reader cannot tell a discarded quiet period from a dropout.
- Offload over USB mass storage from the cradle. BLE carries status and a sync timestamp.

Active draw ~11 mA; a 150 mAh cell gives ~13 hours against a device that docks after every
session.

---

## 5. Not settled

| | What would settle it |
|---|---|
| **Whether full-power shots exceed the ADXL375's ±200 g** | Full-power drives on the existing rig. Light hits reached 21–37 g clean, 74.5 g on a suspected frame hit |
| **Whether a jerk trigger catches a drop shot** | A continuous recording containing drops, with the trigger replayed offline. If it misses them, the high-g channel goes continuous and 180 min needs SPI NAND |
| **F2 checked at the butt** | The 50.4 g prediction has never been compared against a measurement at the position the sensor occupies |
| **Whether the BQ25180's 37 µA `TS` bias is charge-only** | If not, it is 39% of standby and the NTC must be switched |
| **Whether the ADXL375's noise is the part or the rig** | Measured 7–10 mg/√Hz against a datasheet 5, on breadboard wiring |
| **Whether 128 MB is exposed as a filesystem or as raw blocks** | A filesystem written at 48 kB/s can be corrupted by power loss mid-write |
| **The butt cap's real dimensions** | A caliper and a racket |

**And one gate that precedes a board order.** If a wrist IMU already separates a drive from
a drop at 90%+, this device's case rests on the measured clipping and on the corpus rather
than on classification. That has never been tested, and it costs one court session.
