# Take the bill of materials apart, line by line

[`SquashSensor/README.md`](../README.md) now names a part, or a part class, for
every function the device needs. Most of those were chosen once and have not been
challenged since. **Your job is to challenge every one of them**, in the same
form each time:

> Theoretically, what else could do this job? What are the tradeoffs, quantified?
> Does the current choice hold?

The verdict "it holds" is available for every line and will be right for most of
them. A review that overturns nothing is a useful result; a review that overturns
something for a reason that does not survive arithmetic is worse than useless.

---

## 0. The standard to aim for

The design originally specified "a small pouch cell". Reviewing that line produced
this, and it is the shape every finding in this document should have:

1. **A requirement nobody had written down.** The cell spends summers in a car
   boot, inside a sealed carbon tube, in an object that gets tapped on the floor.
   None of that was in the spec.
2. **A choice that was never actually justified.** "Pouch cell" named a form
   factor, not a chemistry. Lithium-cobalt was inherited, not decided.
3. **The hazard bounded before the analysis.** 0.37–0.56 Wh — about an AirPod
   cell, against 15 Wh for a phone. A failure vents and may flame briefly; it is
   not a house fire. Without that number the analysis would have been fear-driven.
4. **A quantified tradeoff.** LiFePO4 costs +0.9 g at 100 mAh and moves thermal
   runaway onset from ~150–200 °C to ~270 °C, with no cathode oxygen to feed it.
5. **A second-order consequence nobody was looking for, larger than the first.**
   A full LFP cell is 3.6 V, which is exactly the recommended maximum for all
   three active parts — so the chemistry change retires the over-voltage finding
   that was the worst thing in the original BOM.
6. **The obvious alternative checked and killed with arithmetic.** A replaceable
   primary cell is Garmin's answer and looks strictly better until you notice a
   CT10 detects shots rather than logging, and that a CR2032 rated near 0.2 mA
   derates to ~68 mAh at this device's 6–11 mA.

**Item 5 is the one to hunt for.** A part change that only trades cost against
performance is a purchasing decision. A part change that dissolves a requirement
somewhere else in the system is a design finding, and this BOM is tightly enough
coupled that several more should exist.

---

## 1. The honesty contract

`CLAUDE.md`'s standing rule governs: **prefer measurement over assertion.** "An
LDO would be better here" is worth nothing. "This LDO's 25 nA quiescent against
the chosen part's 3 µA is 0.5 mAh per week, which is 4% of a session" is worth
something.

- **A datasheet number is not a measurement**, and say which you are quoting.
- **Distributor stock is not a lifecycle check.** A part in stock today can be
  marked not-recommended-for-new-designs on the manufacturer's own page.
- **Where you cannot measure, say you are asserting, and say what would settle it.**
- A model reviewing this is reviewing a document a model helped write. The failure
  mode is agreeing with the voice. Be harsher than feels natural.

---

## 2. Settled, and why — overturn any of it, but bring numbers

These are conclusions with derivations behind them in
[`ADVERSARIAL-REVIEW.md`](ADVERSARIAL-REVIEW.md). They constrain the search space.
They are not sacred; they are *expensive to overturn*, which is different.

| Settled | Where | What would overturn it |
|---|---|---|
| Accelerometer needs ≥±32 g, gyro ≥±4000 dps | F2 — peak centripetal is 50.4 g at a 30.8 m/s head speed | a better head-speed figure, or a racket-mounted measurement |
| The IMU mounts on its body diagonal, buying √3 | F15 | nothing; the geometry is exact |
| Two carriers, one module — butt puck and insertable plug | §5 | a mass or volume number that rules one out |
| Nordic over Espressif, on **active** current and footprint | README, *Open decisions* | a measured ESP32 active figure at a sensible clock |
| LiFePO4 over lithium-cobalt | README, *LiFePO4, not LiPo* | a sourcing failure at 100–150 mAh |
| A regulator is mandatory | F3 | an LFP-only design where every rail tolerates 3.6 V |
| The recording is raw LSB, unscaled, with calibration in the file | §4.1 | nothing — it is the openness requirement |
| Open hardware: CERN-OHL-P 2.0 / MIT / CC BY 4.0 | §4.4 | a part whose documentation is under NDA |

The last one is a live constraint on every part you consider: **a part whose full
datasheet or register map is behind an NDA is disqualified**, however good the
silicon, because it makes the firmware underivable by anyone else.

---

## 3. The requirement that is new, and under-specified

**The cell must survive the racket being tossed and bouncing across the floor, and
still be replaceable.** Neither half is currently specified and they pull against
each other.

Work out, and say:

- **What shock the cell actually sees.** A racket dropped or thrown onto a court
  floor — derive the deceleration from a plausible drop height and contact time,
  and compare it against what a pouch cell, its tabs, and its solder joints
  tolerate. The ADXL375 survives 10,000 g; nothing says the cell does.
- **What retention method meets it.** Candidates, none of which is a finding until
  costed: soldered tabs plus structural adhesive; a spring-contact holder; a
  coin-format cell in a retainer; foam preload in a pocket; potting the cell only.
  Each has a different answer to "replaceable by whom".
- **What "replaceable" means.** By the owner with a screwdriver, by anyone with a
  soldering iron, or by the person who built it? The open-hardware goal says the
  design should be repairable; it does not say by whom, and the answer changes the
  mechanical design. Pick one and justify it.
- **Whether the two carriers need the same answer.** The puck unclips and opens;
  the plug is inside a handle behind a butt cap. They may not.
- **Whether a soft cell belongs in a struck object at all**, or whether a hard-cased
  format — coin, or a small cylindrical — trades energy density for a retention
  problem that solves itself. Quantify the mass and volume cost of that trade the
  way the LFP trade was quantified.

---

## 4. The list

Every line. For each: what it is for, what else could do it, the tradeoff in
numbers, and whether the choice holds.

**Power**

- **The cell.** Chemistry is settled; format, capacity, retention (§3) and sourcing
  are not. Is 100–150 mAh right, given ~11 mA active and a device that docks after
  every session? What does halving it buy in mass and cost against how many
  sessions of reserve?
- **The charger.** An LFP charger at 3.6 V constant-voltage. Which one, and does it
  have a battery-temperature input? Could the MCU do CC/CV itself through a load
  switch and delete the IC — and what does that cost in firmware you now have to be
  right about?
- **The protection IC and its FETs.** `BQ29700`'s thresholds are set by the
  companion FETs' RDS(on), so the FET choice *is* the trip current. Is a separate
  protection IC needed at all if the cell ships with an integrated PCM, and is
  trusting a cell vendor's PCM acceptable in a design others will build?
- **The regulator.** With a 3.6 V chemistry, is one still needed, or does the
  argument reduce to brownout behaviour and headroom? LDO or buck? What is the
  quiescent penalty of each against an 11 mA active load, and does it matter at
  all given §2's finding that standby decides nothing here?

**Sensing**

- **The 6-axis IMU.** `ICM-45686` is ±32 g / ±4000 dps, and F15's √3 means the
  effective threshold for a diagonal mount is lower than the raw requirement
  suggests. Does that open up parts that were previously disqualified? Check at
  minimum the rest of the ICM-456xy and 426xy families, Bosch's BMI3xx, and ST's
  ISM330/LSM6 lines, on range, noise density, FIFO depth, timestamping, supply and
  package. Is there anything with a deeper FIFO or a better AUX arrangement?
- **The high-g accelerometer.** `ADXL375` is the weakest-documented choice in the
  BOM: a 2014-era part, scale factor specified only to ±10% and only below 800 Hz,
  a **32-sample FIFO with no timestamp** which F13 names as the hard real-time
  deadline in the whole design. Alternatives worth checking include ADI's other
  ±200 g parts and ST's H3LIS-series — **specifically, does anything in this class
  have a deep FIFO, or hardware timestamps, or both?** If it does, that is an F13
  finding, not a component swap.
- **The magnetometer footprint.** Unpopulated by decision. Does the part chosen for
  the footprint constrain anything, and is footprinting the right call versus not
  routing it at all?

**Compute and storage**

- **The module.** `nRF52832` or `nRF52840` is an open decision in the README and
  turns on USB. Also ask: is a pre-certified module right at all, against a bare
  SoC that is smaller and cheaper but forfeits the modular grant? What does
  `nRF5340`'s second core buy, and is that a real problem here or an imagined one?
- **The 32.768 kHz crystal.** Specified for usage-log timestamps across weeks. If
  the device docks after every session, the cradle can reset the clock — so **is
  the crystal needed at all**, and what does the internal RC actually cost over one
  session rather than over weeks? This is a board part, a cost, and a placement.
- **Flash.** SPI NOR is chosen. What about SPI NAND, an MCU with more internal
  flash, eMMC, or **a microSD card** — which is user-replaceable, removes the bulk
  transfer problem entirely because you pull the card, and changes the openness
  argument completely. Against that: a connector in a struck object, vibration,
  contact fretting, and card firmware nobody controls. This is the single largest
  second-order question in the BOM; cost it properly.

**Interface and the rest**

- **The off switch.** Slide, magnetic reed, hall plus latch, or something else. It
  must break the load path, survive a butt tapped on the floor, and be visibly
  positioned. Which of those does each candidate fail?
- **The LED.** One part, trivial — but check the current, the optics through
  whatever covers it, and whether one is enough for the states listed.
- **Charging and data contacts.** Recessed gold-flashed pads, spring pins on the
  cradle. What else: magnetic connectors, a USB-C receptacle in the cap, an
  edge connector. Each changes the ingress, the sweat-corrosion and the
  short-in-a-gym-bag story.
- **Passives and the things nobody lists.** Decoupling, the ADC divider, the PTC,
  ESD protection on anything user-touchable, the antenna keepout, and the
  adhesives and tapes that hold the mechanical BOM together — which are consumable
  parts with shelf lives and temperature ratings, in a design that gets hot in a
  car.

**Mechanical**

- The printed sled, the replacement butt cap, the puck housing and its clip, the
  wedge that sets the diagonal mount angle, the conformal coating and the foam.
  Materials, process, temperature rating in a car boot, and which of them a
  stranger can reproduce without bespoke tooling.

---

## 5. Out of scope

- Re-deriving anything in §2 without numbers.
- The sample-rate and module decisions as *decisions* — they are the README's to
  make. Their consequences for other parts are in scope.
- Firmware architecture, except where a part choice moves work into it.
- Price, except where it changes what a third party can reproduce. This is not a
  cost-reduction exercise.
- Anything about the `Squash` or `SquashLab` watch apps.

---

## 6. Traps

- **A cheaper part is not a finding.** A cheaper part that is also smaller, or that
  deletes another part, is.
- **Do not design the board.** Name the finding and its numbers; stop.
- **Second-order effects are the point, and they cut both ways.** A part that
  improves one line and quietly adds a requirement somewhere else has not improved
  anything until that requirement is costed too.
- **Newer is not better.** A 2014 part with a public datasheet, ten years of
  errata and three distributors may beat a 2025 part with none of those.
- **Watch for the part that is right for the wrong reason.** The platform was
  chosen on standby current, which decides nothing here, and happened to be the
  right answer on active current. Check whether any other line has that shape.
- **"Verify from the datasheet" is not a finding.** Findings are things you checked.

---

## 7. What to produce

1. **A table of every line item**: function, current choice, the alternatives
   considered, and a verdict of *holds*, *change*, or *open*.
2. **The findings**, each with its numbers, ordered by how much they change —
   a part swap that dissolves a requirement elsewhere first, a straight
   substitution last.
3. **The battery retention answer** from §3, specific enough to draw.
4. **A list of anything in §2 you think is wrong**, with the arithmetic.
5. **What you could not settle**, and the datasheet, measurement or purchase that
   would settle each.

---

## 8. Where the evidence is

```sh
SquashSensor/README.md                  # the BOM under review, and the decisions
SquashSensor/Docs/ADVERSARIAL-REVIEW.md # where every number in it was derived
Squash/README.md                        # the recording philosophy the format follows
RecorderKit/README.md                   # the format a racket recording converts to
EffortKit/src/shot.rs                   # what is uncalibrated, and why it matters
```

Datasheets are public for every part currently in the BOM; that was checked and is
itself a constraint on replacements. The parts most worth re-reading first are the
ADXL375 (Rev. B) for the FIFO and scale-factor limits, and AN-000478 for the
ICM-45686's FIFO packet table and whether its AUX I²C master can carry an external
sensor into that FIFO.
