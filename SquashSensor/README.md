# SquashSensor — a racket-frame IMU recorder, and the corpus it exists to produce

A hardware complement to [`Squash`](../Squash) and `SquashLab`, not a replacement
for either. Those record and label from the wrist; this records from the racket,
because the wrist clips during real play and because head speed on a wrist sensor
is a proxy at best.

**It is an instrument, not a metric.** It writes raw samples in a documented
format and computes nothing. That is the same call `Squash/README.md` makes for
the watch — "the app is the instrument that collects the data a squash metric
would be built from" — and it is made here for the same reason: there is no public
corpus of racket-frame IMU data from a squash court, and nothing downstream can be
tuned without one.

It is a **training tool, not competition equipment**. Conformity to the World
Squash racket specification is not a design driver; see [Rules](#rules) for what
would have to change if it ever were.

Every number below is derived in [`Docs/ADVERSARIAL-REVIEW.md`](Docs/ADVERSARIAL-REVIEW.md),
which owns the reasoning. This file owns the decisions.

## Why not the wrist

Measured over 7,084 epochs of labelled play on the BMI270 (±8 g, ±2000 dps,
100 Hz), pooled across six sessions: **12.2% of epochs had an accelerometer axis
railed and 3.4% a gyroscope axis**; on the 70-minute match alone, 13.8% and 3.9%,
with one epoch at 22% of its samples. Re-derive with
`cargo run --features std --bin phase-a -- <csv> --epochs ep.csv` and count the
non-zero `accel_sat` and `gyro_sat` columns.

The rails hide more than they show. Reconstructing each clipped run as a locally
quadratic excursion puts the true single-axis wrist peak at a median of 2,293 dps
and a maximum of **4,408 dps** against a 2,000 dps rail, and the accelerometer at
~11 g median and 18 g maximum against an 8 g rail.

Two structural limits sit behind the clipping and are not fixed by a better wrist
sensor: the watch measures the forearm, not the racket, so it misses the wrist
joint's own contribution entirely; and 100 Hz is a 50 Hz Nyquist, below the
transient of ball on strings.

## What it is for

- **Primary**: an open corpus of raw, un-clipped, racket-frame swing kinematics,
  in a published format, that anyone can read without an account or an app.
- **Secondary**: shot-type classification and season-long usage logging.
- **Explicit limits, not defects.** A single rigid-mounted IMU measures the
  racket's net motion. It cannot separate a wrist contribution from a forearm or
  shoulder one — both produce the same racket-frame signal — and no arrangement of
  accelerometers on the racket recovers the swing pivot, because that is defined
  by the velocity field and accelerometers see the acceleration field. The
  body-worn reference for that is the watch, which is why the two clocks have to
  meet (see [The data contract](#the-data-contract)).
- **The ceiling, stated plainly.** Ball speed, shot length, accuracy, court
  position, T-recovery, the opponent, and whether it was the right shot are all
  outside what a racket-mounted sensor can measure. It records how you swung,
  never what happened.

## Mechanical — one module, two carriers

The electronics are one board that fits two enclosures. Neither is a wrap around
the grip exterior, and neither is a full-length cartridge.

**Carrier A — a puck at the butt.** A mount anchored by tape tucked under the grip
wrap, with the sensor clipping onto it at the butt end. This is the shape the one
shipping squash product uses. It has no waveguide problem, needs no bore
measurement, adapts to any handle, unclips with its cell for charging, and is the
one a stranger can reproduce. Its two-accelerometer separation is its own depth,
about 20 mm.

**Carrier B — an insertable plug.** Remove the butt cap, insert the module on a
printed sled, fit a replacement butt cap. Net-lighter than the puck because it
removes the cap it replaces, flush, and it reaches a real accelerometer baseline.
It needs the bore measured, and **the radio must live in the replacement cap** —
the sled carries the cell and the outboard accelerometer inward, never the module.

|  | A — butt puck | B — insertable plug |
|---|---|---|
| Mass | 6–10 g added (4.1–6.9% of a 145 g frame) | **5.8–8.6 g net** (4.0–5.9%), cap removed |
| Balance shift | −19.9 mm | **−15.0 mm** |
| RF | **none — outside the tube** | antenna in the bore; cap-mouth only |
| Bore dependency | **none** | sled is per racket model |
| Accelerometer separation | ~20 mm → 1.02 g of α at 500 rad/s² | **~90 mm → 4.59 g** |
| Charging | **unclips with the cell** | contacts through the cap |
| Reversible | **yes** | modifies the racket |
| Reproducible by a stranger | **yes** | needs a measured bore |

**Build A first.** It is the one that needs no bore measurement and no RF bench
test, and it is the reproduction path the open-hardware requirement depends on.

Sled length in carrier B is a trade and a cheap one: 40 mm to 100 mm costs
1.8–2.4 g of printed sled and buys 3× the α signal, plus the cell volume with it.

For scale, the electronics are **1.24 cm³** — 0.43 cm³ of silicon (the module is
82% of it) plus 0.81 cm³ for a 100 mAh cell. A butt cap envelope is ~3 cm³. The
binding dimension is thickness, not volume: module (2.2 mm), PCB (0.8) and cell
(~4) do not stack inside a 3–5 mm cap, so they sit side by side in plan and the
assembly runs ~4.8 mm, which works where it may extend a few millimetres into the
mouth of the bore.

## Mount orientation — the part that is easy to get wrong

**Mount the IMU on its body diagonal, not square to the handle.** Clipping is
per-axis, so the representable set is a cube of side 2R: a vector along a sensor
axis clips at 1.00 R, along a face diagonal at 1.41 R, along the body diagonal at
**1.73 R**. The dominant load has a fixed direction in the racket frame —
centripetal acceleration points from the sensor toward the instantaneous centre,
near the hand, on every shot — so putting that direction on the diagonal buys √3
of range for free.

It costs nothing: magnitude error is rotation-invariant, `dM = (a·da)/|a|`, so
there is no noise or dynamic-range penalty, and `EffortKit`'s `gyro_magnitude` is
the vector norm and does not change. It also makes a railed axis *more*
informative, because two unrailed axes still carry direction.

What it needs: the board tilted on a moulded wedge (a rotated footprint gives only
one degree of freedom), and **the rotation matrix published with the recording**.

`[1,1,1]` is optimal for one dominant direction. The real distribution has several,
so pick the angle by recording a session, taking the distribution of vector
directions in the racket frame, and maximising the minimum margin across it.

## Ranges, and what clips

Peak centripetal acceleration at the sensor is `a = ω·v`, which for a published
squash forehand head speed of 30.8 m/s maxes at **50.4 g**. Axis-aligned, ±32 g is
exceeded for every sensor speed between 6.1 and 24.7 m/s — the whole plausible
range. On the body diagonal it becomes 55.4 g effective and clears the worst case,
though it clips again above a 32.3 m/s head speed.

| Channel | Part | Range | Diagonal-effective |
|---|---|---|---|
| 6-axis | ICM-45686 | ±32 g, ±4000 dps | 55.4 g, 6,928 dps |
| High-g | ADXL375 | ±200 g | — |

The gyro is comfortable: 6,928 dps against a racket estimate of 2,900–3,300 dps at
impact with a tail past 4,000. The accelerometer is adequate with the diagonal
mount and marginal without it. **Zero clipping is not claimed** — the recording is
written in raw LSB so a railed axis stays visible, as `Squash` already does.

## RF

For carrier A there is nothing to do: the antenna is outside the tube.

For carrier B, a 25 mm bore is 997 dB/m below cutoff at 2.4 GHz, and the link
budget at 3 m leaves **+17.3 dB flush at the mouth, +7.3 dB at 10 mm, and −2.6 dB
at 20 mm**. So the radio sits in the replacement cap and the specification is a
depth limit, not an attenuation measurement. Use the module's own vendor-tested
antenna edge outward; do not relocate the antenna onto a flex or coax sub-board,
which voids a pre-certified module's modular grant.

Treat the whole assembly as sitting in an unintended shielded cavity for grounding
and decoupling, not only as an antenna-keepout problem.

## Electronics

| Function | Part | Notes |
|---|---|---|
| Regulator | **required** | Every part below is 3.6 V maximum; the nRF52832 and ADXL375 have a 3.9 V *absolute* maximum and a full cell is 4.2 V. Pick a nano-quiescent LDO. |
| 6-axis IMU | ICM-45686 | 1.71–3.6 V, LGA-14 2.5 × 3.0 × 0.81 mm, 8 KB FIFO, 0.42 mA low-noise. Full register map public in AN-000478. |
| High-g accel | ADXL375 | 2.0–3.6 V, 3.00 × 5.00 × 0.80 mm 14-LGA, 145 µA at any ODR ≥ 100 Hz, survives 10,000 g. **Run it at 800 Hz**: sensitivity is specified only for ODR ≤ 800 Hz, and at 1600/3200 Hz the output LSB is always zero. Its ODR ladder is 400/800/1600/3200 — there is no 1 kHz step. SPI 5 MHz maximum. |
| MCU / radio | pre-certified module — see [Open decisions](#open-decisions) | The modular grant is the reason to keep a module even at a power cost: files, a kit and a sold unit are three different regulatory objects, and only the last needs it. |
| 32.768 kHz crystal | **a board part** | Raytac supplies a footprint and a recommended spec, not a fitted crystal. |
| Flash | SPI NOR, sized from the rate decision | 512 Mbit covers an hour at 1 kHz + 800 Hz. Avoid NAND and the bad-block, ECC and wear-levelling firmware it brings. |
| Charger | MCP73831 + **cell temperature** | 7 V absolute maximum, so it sits upstream of the regulator. Its thermal regulation watches its own die and it has no THERM pin, so the cell is unmonitored unless an NTC and an MCU-gated charge enable are added. In a sealed object that gets struck, add them. |
| Protection | BQ29700 + companion dual N-FET + PTC | **The FETs' RDS(on) *is* the overcurrent threshold** — detection is a fixed voltage across them. Pick the FETs backwards from the desired trip current, not from a compatibility table. |
| Battery | 100–150 mAh pouch, 2.1–3.2 g | ~11 mA active gives 9–14 sessions between charges, which is ample for a device that docks after every session. |

Three parts are reflow-only: the ICM-45686 and ADXL375 are both LGA and the
BQ29700 is WSON. Budget paid assembly and publish placement files.

### Six axes, not nine

This is a decision, not an omission, and it is not really a choice between two
parts: no 9-axis device offers this range. The 9-axis parts are ±16 g and
±2000 dps, which is the range that clips on the *wrist* — adopting one would
undo the reason the device exists. Nine axes therefore means a discrete
magnetometer beside the ICM-45686, and that is the thing being declined.

**Gravity already pins two of the three orientation degrees of freedom.** A
6-axis IMU gives absolute roll and pitch; only heading about the vertical is
relative. A magnetometer adds absolute heading, and absolute heading is the least
useful of the three here, because it is only meaningful together with court
position and facing, which a handle sensor cannot measure.

**The drift it would correct is on the wrong timescale.** With a residual gyro
bias of 0.05–0.5 °/s after calibration, heading error is 0.0–0.1° over a 0.3 s
swing, 1.5–15° over a rally, and 135–1350° over a session. The racket face angle
worth coaching on lives inside the swing, where the gyro alone is sub-degree. The
magnetometer fixes the session-length error, which nothing downstream can use.

**And it could not resolve the swing anyway.** MEMS magnetometers run at 100 Hz,
400 Hz at best. At 3,000 dps the racket turns 30° between 100 Hz samples.

Against that: a few mm from a cell and a switching load, inside or on conductive
carbon, in a steel-framed court; no in-house experience — `MagProbe` is this
repository's instrument for the question and has never been run; and the shipped
precedent is 6-axis, including the one product that carried two gyroscopes.

There is one real argument the other way, and it is the corpus argument: a channel
that is not recorded cannot be added later. So **footprint it and do not populate
it.** The pads and the I²C routing cost nothing in layout, and running `MagProbe`
settles whether a magnetometer is usable at all before a season of recordings
depends on one.

Note that the ICM's AUX I²C master — the tidy way to land an external sensor in
the timestamped FIFO — can carry one part. The high-g channel and a magnetometer
compete for it, and the high-g channel wins: it solves a problem this design
actually has.

## Open decisions

Two, both cheap, both to be made before a board is ordered because both change the
BOM.

**1. Sample rate.** 1 kHz was originally justified by a shot-disguise metric that
is not part of this design; published racket-sport classification reaches 97–98% at
ordinary rates. The rate is therefore no longer forced — but nor is it expensive,
because the ADXL375's 3,200 Hz mode, not the 6-axis rate, was what broke the flash
budget.

| 6-axis rate | per hour | NOR for one hour | BLE sustained |
|---|---|---|---|
| 400 Hz | 17 MB | 256 Mbit | 38 kbit/s |
| 800 Hz | 35 MB | 512 Mbit | 77 kbit/s |
| 1 kHz | 43 MB | 512 Mbit | 96 kbit/s |

Add 17 MB/hour for the ADXL375 at 800 Hz. Record more than you think you need —
undersampling is unrecoverable and the recording is the product — but decide it
rather than inherit it.

**2a. Nordic, and why — because the obvious argument is the wrong one.** The
tempting case is standby current: Nordic reaches sub-1 µA System OFF against
~5 µA for an ESP32-C3. That gap is **9 µA, which is 1.51 mAh over a week against
11 mAh for a single hour of recording** — 14% of a weekly budget with one session,
and it would need 51 days of standby to equal one hour of play. It is not even the
dominant standby term: the IMU's own wake-on-motion current is ~70 µA and BLE
advertising ~25 µA. Standby decides nothing here.

**Active current decides it, and by a lot**, because this device logs continuously
for an hour rather than waking briefly:

| MCU | MCU current | system total | hours on a 100 mAh cell |
|---|---|---|---|
| nRF52832 @ 64 MHz from flash, DC/DC | 3.7–4 mA | ~11 mA | 9.1 |
| ESP32-C3 @ 160 MHz | ~22 mA | ~29 mA | 3.5 |
| ESP32-S3 @ 160 MHz | ~35 mA | ~42 mA | 2.4 |

Matching a Nordic hour on a C3 needs 264 mAh instead of 100, which is **+3.5 g of
cell on a 6–10 g product**. That is the finding the platform choice actually rests
on, and it is a hundred times the effect the standby argument describes. *Sourcing
note: the nRF52832 figure is `IDDFLASHCACHEDCDC` from its product specification;
the Espressif figures are typical rather than pinned to a datasheet line here, and
a C3 clocked down would do better. The direction is not in doubt, the ratio is
worth an afternoon on an eval board.*

**Footprint agrees.** MDBT42Q is 160 mm² in plan against ESP32-C3-MINI-1's 219 and
ESP32-S3-MINI-1's 316, in a butt cap with ~750 mm² of plan area of which the cell
wants ~240.

**Openness does not favour Espressif either**, which is the counter-intuitive part.
Nordic's SoftDevice is a binary blob, but Zephyr ships an open-source Bluetooth
controller for nRF5x that avoids it; Espressif's PHY layer is a blob regardless of
using NimBLE. Nordic with Zephyr is the cleaner of the two.

**The real cost is the on-ramp**, and it is not nothing: nRF Connect SDK is a
steeper climb than ESP-IDF, and this project's author has far more ESP32
experience. That is a genuine engineering cost, paid once.

**2b. nRF52832 or nRF52840.** The '840 has a USB peripheral and the '832 does not,
and that is the whole of it. **USB mass storage needs no software anybody has to
write, on every operating system, for ever**; a custom BLE GATT service needs a
tool somebody maintains, which is exactly what stranded the users of every product
in the review's comparison table. The carriers already expose a face for contacts.
The cost is a larger, more expensive module. Lower sample rates weaken the
throughput argument for USB but not the openness one, which is the argument that
matters.

## Firmware-adjacent hardware requirements

- **The ICM-45686 timestamps itself; the ADXL375 does not.** TDK FIFO packets carry
  a 2-byte timestamp with the sensor data. The ADXL375's FIFO is 32 levels of x, y
  and z with no timestamp and no sample counter, and 32 samples at 800 Hz is 40 ms
  of slack. **The high-g channel is the hard real-time deadline in this design**,
  and its alignment is a firmware construction that must be recorded in the file
  with its uncertainty rather than assumed. Check whether the ICM's AUX I²C master
  can place external-sensor data into its own FIFO — if it can, the high-g channel
  inherits hardware timestamps and the problem goes away.
- **Wake-on-motion** wired to a wake-capable GPIO. Season-long usage logging cannot
  depend on remembering to start a recording. Note that the IMU's own wake-on-motion
  current, tens of µA, dominates the standby budget — the MCU's System OFF current
  does not decide anything.
- **Battery voltage sense and IMU die temperature**, both logged as data-quality
  channels. Neither is reconstructible after the fact.
- **A firmware low-battery threshold above the protection IC's cutoff**, around
  3.4 V, so a session ends and flushes rather than being truncated by a hard cutoff.

## The data contract

The device writes raw per-sensor LSB, unscaled, on each sensor's own clock. It does
not fuse, filter or scale a sample before writing it — the recording keeps the
saturation rather than hiding it behind a conversion.

A recording carries, in its own header, everything needed to interpret it without
asking anyone:

- per-unit calibration coefficients for both accelerometers, which are mandatory
  rather than optional: the ADXL375's scale factor is specified only to ±10%
  (44–54 mg/LSB), its 0 g offset to ±400 mg typical, and its on-chip offset
  registers trim in 1.56 g steps and clear on every power cycle;
- **the mount rotation matrix**, because the diagonal mount means raw LSB no longer
  corresponds to anything anatomical;
- **which carrier it was in, and the accelerometer separation**, both of which are
  per-installation;
- the session configuration, die temperature and battery voltage.

The on-device format is documented binary — at these rates a text encoder is CPU
and flash the cartridge does not have — and ships with a converter to
[`RecorderKit`](../RecorderKit)'s three-file CSV, so a racket recording is readable
by everything that already exists. Marker vocabulary is RecorderKit's, extended
above the values it defines, never redefined. The specification lives in
`Docs/FORMAT.md` and is versioned independently of the firmware.

**Time sync with the watch is part of the contract**, not an afterthought: a racket
sample and a wrist sample are useless together unless they can be placed on one
timeline, and the mechanism is recorded in the file rather than assumed.

**No hostages.** No account, no cloud, no server-side calibration table, no pairing
key held by one app, nothing that stops working when a repository goes quiet.

## Open hardware

Published: schematic, editable KiCad layout, BOM with orderable part numbers,
mechanical CAD for both carriers in an editable format, firmware source, assembly
placement files, and the per-unit calibration procedure with its jig. The sled is
parameterised by measured bore dimensions rather than shipped as one racket's STL.

**Licences**: CERN-OHL-P 2.0 for the hardware, MIT for firmware and tooling, CC BY
4.0 for documentation. MIT stays for code because the repository is MIT and the
apps derive from the UNA SDK's MIT examples; the hardware wants a licence that
names design files as the thing being licensed, and the permissive CERN variant
keeps the repository's existing spirit.

**No part in the BOM has an NDA problem** — the ICM-45686's full register map is
public in AN-000478, and every other datasheet is a free download.

The companion app is a consumer of the format, co-developed with it, under MIT.
Anything it computes, it computes from files anyone else can also read.

## Mass and cost

| | |
|---|---|
| Added mass | 6–10 g (carrier A), 5.8–8.6 g net (carrier B) |
| As a fraction of a 145 g frame | 4.0–6.9% |
| Balance shift | −15 to −20 mm |
| Swingweight | negligible — under 1 kg·cm² on a ~130 base |
| BOM, low quantity | ~$25–40/unit |
| Prototype run, 5–10 units | ~$70–120/unit, dominated by assembly setup |

For context, shipped implement sensors in tennis, golf and baseball carry
1.5–3.0% of implement mass and shift balance 6–9 mm. This sits just above that
band, and a squash frame is half a tennis racket's mass so it moves further per
gram — balance shift, not mass ratio, is the number that transfers between sports.

## Rules

Not a design driver, recorded so it is not rediscovered. World Squash permits
player analysis technology in playing equipment, and coaching only between games —
so a device that records and displays nothing during play is the compliant shape.
The racket must still meet the racket specification: **255 g maximum weight**,
which is not binding, and **686 mm maximum length** against production frames built
at 685 mm, which means carrier A would not be legal in sanctioned play and carrier
B would.

## Plan, ordered by cost to kill

**Now, free.**

- Run `SquashLab`'s `DRIVE DROP` protocol, train a classifier on wrist epochs, and
  report held-out drive-versus-drop accuracy. **This is the gate.** If the wrist
  already separates them at 90%+, this device's case rests on the measured clipping
  and on the corpus rather than on classification — still a case, but a smaller one.
- Tape ~8 g to your butt cap and play a session. The prior art largely covers this
  mass, so it is a confirmation rather than a study; if something is obviously
  wrong, `Docs/ADVERSARIAL-REVIEW.md` §8 has a powered protocol.
- Make the two open decisions above.
- Read AN-000478 for the FIFO packet table, the timestamp resolution, and whether
  the AUX I²C master reaches the FIFO.

**Then, at a desk.** KiCad, starting with the power tree — it is the subsystem the
original BOM got wrong and the one no open decision touches. IMU and charge
circuits are mostly datasheet transcription; cross-check third-party footprints
against each datasheet's own land pattern.

**Then, one board.** Carrier A at a comfortable size before the tight one. No RF
bench test is needed for it.

**Only for carrier B.** A sacrificial racket, handle intact: measure the bore at 0,
20, 50 and 100 mm from the butt, the wall thickness, whether there is foam or a
pallet, and weigh the butt cap. Then the depth-cliff RF test, which is looking for
where the link falls over rather than for an attenuation figure.

## Not settled

- Whether racket-frame kinematics beat the wrist at anything. The gate above.
- The bore, for carrier B. Nothing about it has been measured.
- Whether the ICM's AUX I²C master can carry the high-g channel into its FIFO.
- Whether a microphone hears ball-on-strings through a carrier — a MEMS mic would
  give an impact timestamp that does not depend on a gyro threshold, which is the
  soft-drop detection problem `EffortKit`'s `shot` module records as unsolved. The
  acoustic path is the unknown, not the microphone, and a mic in a device carried
  into a club is a privacy question an open design has to answer in the open.
- Whether a magnetometer is usable near a cell and a switching load; racket face
  angle in the world frame is unrecoverable without one. `MagProbe` is the
  instrument for that question and has never been run.
- Per-unit calibration repeatability across a reinsertion, and therefore how much
  mechanical keying is worth.
- What Racketware's data model is, and whether it exposes raw samples.
