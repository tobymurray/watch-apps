# SquashSensor hardware — what was checked, against what

A wrong pin in a symbol or a footprint survives ERC, looks fine locally, and is found when a
board does not work. This file records, for every part, the document each pinout and land
pattern was taken from and the checks that were run against it. **If a check is not listed
here, it was not done.**

## Symbols

`SquashSensor.kicad_sym`. Each symbol's Description names its source document, figure or
table, and page.

**Three checks, run on every symbol below:**

1. **Render against the page.** `kicad-cli sym export svg`, rasterised and compared pin by pin
   with the datasheet's own pinout figure.
2. **Mechanical diff against the datasheet's text.** Each pin table was re-parsed from the
   `pdftotext` output of its source and diffed against the library: **121 of 121 pins agree.**
   The pin tables in the generator were typed by hand; the parse re-derives them from the
   document, so a typo in one does not reproduce in the other. Two entries are read off the
   page rather than parsed — W25Q01JV pad 7, whose name wraps across three table lines, and
   its exposed pad 9.
3. **Independent transcription, where one exists.** KiCad's stock `RF_Module:MDBT50Q-1MV2`
   (61 pins), `Power_Protection:TPD4E05U06DQA` (10) and `Transistor_FET:Q_Dual_NMOS_S1G1D2S2G2D1`
   (pins 1–6), and this project's first ICM-45686 symbol, drawn from AN-000484 Rev 1.1
   Figure 2 (14). **No disagreement on any pin number.**

| Symbol | Part | Source | Pinout from | Pins |
|---|---|---|---|---|
| `ICM-45686` | TDK ICM-45686 | DS-000577 Rev 1.0, 07/25/2024 | Table 10 pp. 27–30, Figure 4 p. 31 | 14 |
| `MDBT50Q-1MV2` | Raytac MDBT50Q-1MV2 | MDBT50Q-1MV2 & -P1MV2 spec, Version L | §2.5 Pin Assignment pp. 14–17 | 61 |
| `BQ25180YBG` | TI BQ25180YBGR | SLUSE99C, Jan 2023 | Figure 6-1 and Table 6-1, p. 4 | 8 |
| `TPS7A0230PDBV` | TI TPS7A0230PDBVR | SBVS277C, Sept 2022 | Figure 5-2 and Table 5-1, p. 3 | 5 |
| `BQ29700DSE` | TI BQ29700DSER | SLUSBU9I, Aug 2024 | Figure 5-1 and Table 5-1, p. 3 | 6 |
| `W25Q01JVZE` | Winbond W25Q01JVZEIQ | W25Q01JV Rev B1, 2019-11-13 | Figure 1a and §3.2, p. 7; exposed pad §8.3, p. 87 | 9 |
| `PMDPB30XN` | Nexperia PMDPB30XN | Product data sheet, 6 July 2012 | Table 2 Pinning, p. 2 | 8 |
| `TPD4E05U06DQA` | TI TPD4E05U06DQAR | SLVSBO7O, Aug 2024 | Figure 4-3 and Table 4-2, p. 4 | 10 |

**Four things the check turned up that are not pin errors, and matter anyway:**

- **21 of the module's GPIOs are "standard drive, low frequency I/O only"** — ≤10 kHz, and
  Raytac's §2.6 says in as many words that SPI, I²C, UART and PWM are not low-frequency I/O.
  They are marked `*` on the symbol so the pin budget cannot put a bus on one by accident.
- **KiCad's stock `TPD4E05U06DQA` cites the wrong document** — its Datasheet field is the
  TPD2EUSB30A's. Its pins are right.
- **The stock dual-NMOS symbol has no pins for the PMDPB30XN's two exposed drain pads.** The
  project symbol stacks them, hidden, on each FET's drain, so both pads get a net.
- **The W25Q01JVZEIQ ships with Quad Enable set**, so pads 3 and 7 are `IO2`/`IO3`, not
  `/WP`/`/HOLD`, and must not float. Its exposed pad "is not connected to any internal
  electrical signals" (§8.3). W25N01GV (SPI NAND) Rev O Figure 1a assigns pads 1–8 the same
  way.

**Still to be drawn, waiting on a document:**

| Part | Why it is waiting |
|---|---|
| ADI ADXL375 | analog.com refuses scripted downloads |
| ST LIS2MDL (footprint only, do not populate) | st.com refuses scripted downloads; KiCad's stock symbol exists and is unverified |
| 32.768 kHz source | a decision — see below |
| Slide switch, LED | C&K and Lite-On datasheets not yet in hand |
| SWD pads (Tag-Connect TC2030-NL) | Tag-Connect's pinout document not yet in hand |

Two-pin passives use KiCad's `Device` symbols (`R`, `C`, `LED`, `Thermistor_NTC`, `Polyfuse`,
`Battery_Cell`). Their pin numbers only mean something against a footprint, so they are
checked in the footprint pass.

## A finding from the pin tables: a crystal cannot drive `CLKIN`

DS-000577 Table 4 (p. 21) specifies pin 9 — `FSYNC`, which `CLKIN` shares — as a digital
input: **V<sub>IH</sub> ≥ 0.7 × VDDIO, 2.1 V on this rail, and up to 10 pF of input
capacitance.**

On the other side of the crystal, the nRF52840's oscillator runs at 0.23 µA with a
**12.5 pF maximum load** (PS v1.11 §5.4.4.3, p. 171). **The capacitance is the firmer
argument**: hung on `XL1` or `XL2`, an input specified only as "up to 10 pF" is an unknown
share of a 12.5 pF budget, and the crystal's frequency moves by an unknown amount with it. The PS gives no amplitude for its own oscillator; the low-swing external
clock it accepts instead is specified at 200–1000 mV peak-to-peak (p. 172), and a 2.1 V
logic threshold is well above that scale — *an inference, not a datasheet figure.*

What the PS does offer is a **rail-to-rail external clock on `XL1`**, `XL2` grounded (Table 8,
p. 160; §5.4.2.4, p. 161). So one CMOS 32.768 kHz oscillator can clock both parts; a crystal
can clock only the MCU.
