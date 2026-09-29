# SquashSensor hardware — KiCad project

KiCad 10.0.6, schematic format `20250114`. Five hierarchical sheets, no symbols placed yet.

**Every part choice here has a number behind it in [`../Docs/BOM-REVIEW.md`](../Docs/BOM-REVIEW.md),
and the sheet notes cite the finding rather than restating it.** If a decision on a sheet
looks arbitrary, the finding is where the arithmetic lives.

```sh
# both of these parse the whole hierarchy and are worth running after any hand edit
kicad-cli sch erc --output erc.rpt SquashSensor.kicad_sch
kicad-cli sch export pdf -o SquashSensor.pdf SquashSensor.kicad_sch
```

## Start with Power

`ADVERSARIAL-REVIEW.md` §8 says to start there because it is "the subsystem the original BOM
got wrong". That has inverted: after B1, B4.5, B8.1 and B8.2 it is now the **best**-specified
part of the design — cell, charger, PowerPath, LDO, protection, ship mode and battery sense
all have named parts and stated reasons.

## Symbols

**Only one part in this BOM exists in the stock libraries.** Everything else has to be drawn.

| Available | Library |
|---|---|
| `MDBT50Q-1MV2` (nRF52840 module) | `RF_Module` |
| `Thermistor_NTC`, `Crystal_GND24`, passives | `Device` |
| **`ICM-45686`** | **`SquashSensor`** (this project) |

`SquashSensor.kicad_sym` holds the parts with no stock symbol. **Every pinout in it is
transcribed from a named primary document, and the document is in the symbol's Description
field.** The ICM-45686 came from **AN-000484 Rev 1.1, Figure 2** — the LGA-14 pinout diagram,
pins 1–14. Check any symbol against its source before you wire it; a transcription error
here is silent, permanent and looks fine locally.

**Still to be drawn, with the document each pinout must come from.** These were deliberately
*not* generated, because I have no verified numbered pin table for any of them. **Drop the
datasheet in and the symbol is a few minutes' work** — transcribing a pin table is the part
worth automating, and checking it against the page is the part worth doing by eye.

| Part | Package | Pinout source |
|---|---|---|
| BQ25180 | DSBGA-8 | TI **SLUSE99C** |
| TPS7A0230 | SOT-23-5 | TI **SBVS277C** |
| BQ29700 | DSE-6 1.5 × 1.5 mm | TI **SLUSBU9I** |
| ADXL375 | 14-LGA 3.0 × 5.0 × 0.8 mm | ADI **Rev. B** |
| Dual N-FET | — | **Part not yet chosen** — B8.2, pick backwards from the trip current |
| SPI NOR, 1 Gbit | — | **Part not yet chosen** — must be 2.7–3.6 V, not 1.8 V (B4.5) |
| TVS array | — | **Part not yet chosen** — B8.7 |

Three are reflow-only (ICM-45686 and ADXL375 are both LGA, BQ29700 is WSON). **The charger
was a fourth until B8.1a swapped the 20-ball BQ25155 for the 8-ball BQ25180** — every ball on
an outer edge, so it escapes like a QFN rather than needing via-in-pad. That is a
**reproducibility** win as much as an assembly one; §4.5 requires an assembly-house BOM and
placement files either way.

## Before this schematic is finished

Five things the review flagged that the BOM does not yet contain:

1. **ESD protection on every contact.** Absent entirely from the original BOM — B8.7 — on
   the one user-touchable surface in the design.
2. **The protection FETs.** B8.2: OCD is 0.100 V and SCD 0.5 V *across the FETs*, so at
   50 mΩ the trip lands at 2 A / 10 A — 13–29 C on this cell. Roughly 1 Ω of FET would be
   needed to trip near 1 C and no such part exists, so **record that this is a
   short-circuit protector rather than an overcurrent one** and let the PTC own the middle.
3. **The 32.768 kHz crystal routed to the ICM-45686's `CLKIN`**, not only to the MCU's
   LFCLK. B5 — this is the entire reason the crystal is in the BOM, and nobody has drawn it.
4. **The NTC and its bias** for the BQ25180's `TS/MR` pin. This is F11, the one safety finding.
   The same pin takes a momentary switch — see below — so size the network such that the
   hottest legitimate cell still reads above the button-press threshold.
5. **The pin and peripheral budget redone on the '840** — *redone*, not ported across from
   the '832, per §7 item 12.

## Two things B4.5 changed that need reconciling on the schematic

Both are consequences of the cell going back to 4.2 V, and neither is written down yet.

**The LED.** B8.4 argued red-only because a green or blue Vf of 2.8–3.2 V dies at the bottom
of an *unregulated* LFP discharge. The rail is now a regulated 3.0 V, so that specific
argument no longer applies — **but the conclusion survives for a different reason**: at 3.0 V
there is no headroom for a current-limiting resistor above a 2.8–3.2 V Vf. Red still wins;
the reason in the README should be updated.

**Two off switches, and B8.1a prices the difference.** B8.3 keeps a mechanical slide switch,
because *"a switch you can see beats firmware that says so"* and a FET cannot answer "is it
off" to somebody looking at it. B4.5 then adds ship mode as "a true hard off".

The BQ25180 makes the choice numeric: **15 nA if something else holds it off, 3.2 µA if it
must stay wakeable by a button** — 0.07 mAh against 14 mAh over six months, or **14% of a
100 mAh cell**. So a slide switch that physically breaks the load path is both the visible
off B8.3 demands *and* the cheaper one. A switch that merely drives `TS/MR` costs 200× the
standby to stay wakeable. **Draw the load-path break unless there is a reason not to.**

## What is not blocking this

Four measurements are outstanding (§5) and **none of them changes this schematic**:
full-power shots versus ±200 g and F2-at-the-butt are answered by F15's mechanical wedge,
because no part in either class goes further; the M7 noise question is layout and decoupling;
the WOM-versus-8 KB FIFO question is firmware.

**One does, as a tail risk.** If the drop-shot trigger validation (B7.2) shows a jerk
threshold cannot catch soft shots, the high-g channel has to record continuously — and
180 minutes of that needs >256 MB, which is SPI NAND and a different part family. **Run that
validation before committing the flash footprint.**

## And one thing that should precede a board order, not a schematic

`ADVERSARIAL-REVIEW.md` §8 item B: run `SquashLab`'s `DRIVE DROP` protocol, train on wrist
epochs, report held-out accuracy. *"It should precede any board order."* It never has.

It costs one court session and no hardware. B9 sharpened it: a $9.42, 100 Hz, 6.1 g tennis
dampener reaches 96.75% on six stroke types, which raises the prior that the wrist separates
a drive from a drop perfectly well — in which case the corpus is the whole remaining case
for this device.

Drawing a schematic is free. Fab and paid reflow are not.
