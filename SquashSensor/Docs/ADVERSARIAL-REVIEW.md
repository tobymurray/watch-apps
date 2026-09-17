# Adversarial review — SquashSensor, 2026-09-17

A review of `SquashSensor/README.md` as it stands on `origin/squash-sensor-design`
(0638eb6). The question is not whether the document is well written. It is whether
the device should be built, and whether this is it.

Everything below was done at a desk in one session: the repository's own
recordings, arithmetic that the design asserts but never shows, and published
numbers for parts and for products that already shipped this idea in four other
sports. **No racket was measured and no bench test was run.** Where that matters
the finding says so and names what would settle it.

Three constraints were stated after the design was written and are treated here as
requirements, not preferences: **the wearer can take every raw sample off the
device themselves in a documented format with no account and no cloud**; **the
design is publishable as open hardware someone else can build**; and **it is a
training tool, not competition equipment**, so conformity to the World Squash
racket specification is not a design driver. None appears in the document under
review, and the third removes a constraint rather than adding one — it reopens
mounting positions that the racket specification would otherwise close (§5).

---

## 0. The verdict

> **Second revision: the disguise motivation has been abandoned by the author.**
> That resolves F1 — not by making the metric measurable but by removing it — and
> with it the last fatal finding. **The verdict is now: build it.** Three things
> follow, and the third is a new gate rather than a green light.
>
> *What it takes with it.* The 800 Hz–1 kHz+ requirement was justified by the
> disguise question alone: §5 found shot classification at 97–98% in the literature
> at ordinary rates, and a sampling-rate study putting "sufficient" at 100–400 Hz.
> The requirement is therefore no longer forced. It is also no longer expensive —
> F4 established that the 3,200 Hz high-g channel, not the 6-axis rate, is what
> broke the flash budget. At 1 kHz for the IMU and 800 Hz for the ADXL375 an hour is
> 60 MB, which a 512 Mbit NOR holds. **So keep the rate if you want it; the decision
> is now free rather than inherited, and it should be made deliberately.** For
> reference, at 400 Hz an hour is 17 MB on a 256 Mbit part, active draw is ~6.4 mA,
> a 60 mAh cell gives nine sessions, and 38 kbit/s is streamable over BLE rather
> than merely offloadable — which would retire most of F5 as well.
>
> *What survives as the reason to build it.* Three things, all structural and none
> touched by dropping disguise: the wrist clips, measured here at **12.2% of epochs
> accel-railed and 3.4% gyro-railed over 7,084 epochs**, with reconstructed
> single-axis peaks reaching 4,408 dps and 18 g past the rails; racket-frame
> kinematics are not wrist-frame kinematics; and the rate ceiling is higher.
>
> *The new gate.* **Phase −1 item B is now decisive rather than informative.** While
> disguise was the goal, that test asked how much headroom the racket had to buy.
> Now it asks whether the racket has anything to buy at all: if wrist epochs already
> separate a drive from a drop at 90%+, the remaining case rests on clipping and on
> the corpus, not on classification. It costs one court session and no hardware, and
> it should happen before a board is ordered.
>
> *And the honest cost.* Disguise was the novel question; shot classification is
> well-trodden. What is left that nobody else has is **an open, raw, racket-frame
> squash corpus with a published format** — §5 found almost nothing open anywhere in
> sports sensing, and `Squash/README.md` already records that no public wrist corpus
> exists either. That is a real contribution and it is the one the openness
> requirements in the preamble were pointing at the whole time. It is a different
> claim from the one the document opens with, and the document should say so.

> **First revision, after the review was worked through.** The verdict below was written
> against the design as submitted. Four of its findings have since been defused —
> F15 (a diagonal mount buys √3 and mostly answers F2), F14 and §5 (a carrier
> architecture puts the mass at 4–7% of frame rather than 10–17%), §5 again (the
> prior art largely settles the mass question at that size), and F3 (a missing
> regulator is a schematic fix, not a project killer). **RF dissolves entirely for a
> butt puck.** What has *not* moved is F1. So the honest verdict is now: **build it,
> build it as a recorder rather than as a disguise-measurement device, and build the
> puck first.** The original text stands below because the reasoning that got there
> is the useful part.
>
> On Racketware as evidence: its existence proves the mounting, the form factor and
> the RF are solvable in squash, which is exactly the class of risk this review spent
> most of its length on, and that is worth a great deal. It proves nothing about F1 —
> it does not attempt a disguise metric — and nothing about the raw-recording data
> model, since like everything in §5 it almost certainly extracts events. It de-risks
> the engineering and leaves the science where it was.
>
> On the sled being strictly better than the puck: it is better on the axes named —
> balance (a 7 g sled at 45 mm shifts 15.0 mm against a puck's 19.9 mm), separation
> (~90 mm against ~20 mm, 4.6 g against 1.0 g of α signal), net mass (it removes the
> cap it replaces) and tidiness. It is worse on RF (its antenna is in the bore, −2.6 dB
> at 20 mm depth, so Phase 2 is required for it and not for the puck), on C1 (it needs
> the bore measured), on reproducibility (per-racket-model rather than universal), on
> charging (the puck unclips with its cell; the sled brings the cap-contact apparatus
> back) and on reversibility. **Four to five — better where it counts for the physics,
> worse where it counts for shipping.** Which is the argument for building both off one
> module, and the puck first.

**Not yet, and probably not this.** The mechanical reasoning is sound and the
correction from "wrap around the grip" to "cartridge inside the bore" was right.
But three of the six claims the rest depends on fail to arithmetic that costs
nothing: the ±32 g primary accelerometer **rails on centripetal acceleration
during an ordinary squash forehand, before any impact** — and not clipping is the
entire reason this device exists; the power tree has **no regulator**, putting a
4.2 V cell across parts rated 3.6 V, two of which have an absolute maximum of
3.9 V; and
the standby-current argument that chose the platform is **beaten 8:1 by the IMU's
own wake-on-motion current**, so it cannot have decided anything. Against that,
the device proposes 10–23% of the frame's mass where every implement sensor that
ever shipped carried 1.5–3%, and moves the balance point 30–47 mm where a player
feels 5 mm.

**The cheapest experiment that most changes the answer is not the RF test.** It is
20 g of Blu-Tack pushed into the butt of your own racket and one session on court.
It costs nothing, takes an hour, needs no purchase, and is the single most likely
thing in this document to end the project. The RF test — which the plan puts
first, buys hardware for, and calls the largest risk — is third in line, and the
link-budget arithmetic in F7 says it is a smaller risk than billed.

**And the mounting position should be reopened, because it is where most of this
goes away.** Every aftermarket implement sensor that ever shipped found the butt end
from the outside, at a third of the mass; the one product that went inside the
handle could only do it by rebuilding the frame at the factory. The squash-specific
prior art, Racketware, uses a third position the document dismisses in one line — **a
mount taped under the grip, with the sensor clipping into it.** That position adds no
length, needs no liner, does not care what the bore measures, puts the antenna
outside the carbon, makes the clip its own orientation key, and lets the cell leave
with the sensor. It deletes the document's largest stated risk, its largest mass
item, its thermal cavity and its dependency on C1 — for grip feel, which nobody has
measured, and a two-accelerometer baseline worth less than §6 first suggests. With
this device out of scope for sanctioned play, the 686 mm racket limit that would
have ruled out a protruding puck does not bind either, so all three positions are
available and **the internal cartridge has to earn its place against them** (§5).
The strongest form is not to choose: one module, two carriers — **a puck that
clips to the butt end**, which anyone can reproduce and which has no waveguide
problem at all, and **an insertable plug on a sled with a replacement butt cap**,
which is net-light because it removes the cap it replaces, reaches a real
two-accelerometer baseline, and needs its radio in the cap rather than down the
bore. Both fit 1.24 cm³ of electronics. That takes the mounting decision off the
critical path and is the best implementation shape in this document.

---

## 1. What was measured, and how

**The wrist saturation figures reproduce exactly, two independent ways.** The
repository's own path — `cargo run --features std --bin phase-a -- <csv> --epochs
ep.csv`, then counting non-zero `accel_sat`/`gyro_sat` columns — gives 13.8% of
epochs with a railed accelerometer axis, 3.9% with a railed gyroscope axis, worst
epoch 22% of its samples, over 1,800 epochs of `20260913-v0.6.0-70min-match`. An
independently written Python counter over the same CSV gives the same three
numbers. Pooled over all six labelled sessions (7,084 epochs, 704,838 samples):
12.2% accel, 3.4% gyro.

One note for anyone repeating this: **`phase-a` alone does not print these
figures.** Its A2 section needs labelled epochs and the 70-minute match carries
two markers and no labels, so the run reports "No labelled epochs" and says
nothing about saturation. The numbers come out of `--epochs`, not the report.

**Two things measured here that the repository had not recorded before.**

*How far past the rail the wrist actually goes.* A clipped run hides its peak. Near
a maximum a gyro excursion is locally quadratic in time, so for a run of L samples
with the rail crossed at each end and the two bracketing unclipped samples pinning
the curve, the peak is `P = RAIL + q·L²/(2L+1)` where `q = RAIL − mean(pre, post)`.
Exact for L = 0 and degrading as L grows, because 10 ms spacing stops resolving the
curve — so it is reported per run length and the long runs are the untrustworthy
ones.

| clipped run | ms | n | median reconstructed peak | max |
|---|---|---|---|---|
| 1 | 10 | 144 | 2,147 dps | 2,597 dps |
| 2 | 20 | 112 | 2,216 dps | 2,969 dps |
| 3 | 30 | 123 | 2,423 dps | 3,908 dps |
| 4 | 40 | 76 | 2,908 dps | 3,930 dps |
| 5 | 50 | 16 | 3,484 dps | 4,408 dps |
| 6 | 60 | 3 | 3,782 dps | 3,983 dps |

Over all 474 runs: median 2,293 dps (1.15× the rail), p90 3,306, p99 3,983, max
4,408. The same method on the accelerometer, restricted to the runs short enough
to trust (L ≤ 3, 1,948 of 2,626 runs), gives a median single-axis peak of ~11 g
and a maximum of 18 g against an 8 g rail.

*What the gyro reads when it is not clipped.* Vector magnitude on the 70-minute
match: p99 = 1,368 dps, p99.9 = 2,421 dps, max 3,181 dps — and the maximum is a
lower bound, because reaching it required axes at the rail.

Everything else below is published numbers plus arithmetic. Sources and dates are
in §10.

---

## 2. C1–C6, re-scored

| # | Claim | Verdict | What was checked |
|---|---|---|---|
| **C1** | Bore ~25×32 mm, hollow, zero-taper ≥100 mm | **Still open, and optimistic** | No racket measured, by the document or here. Published construction says the handle *is* hollow and *is* used for counterweighting, and the butt cap comes off with two staples — so the premise is not absurd. But handles are commonly moulded with PU foam or a pallet over the graphite, foam that "can sometimes break apart", and the grip cross-section is an octagon, not a rectangle. Working inward from a ~110 mm grip circumference: 33 mm across flats outside, less ~1.8 mm of grip wrap, ~3 mm of pallet and ~1.5 mm of composite wall per side leaves **~18–24 mm across flats of usable bore**, not 25×32. That is an estimate with a stated method, not a measurement. |
| **C2** | 2.4 GHz will not propagate out of the bore | **Verified as a bound, and it is robust** | `fc = c/2a` at a = 32 mm is 4.69 GHz; below-cutoff attenuation `α = (2π/λc)√(1−(f/fc)²)` gives **728 dB/m, 72.8 dB over 100 mm** — the document's "70+ dB" is right. It gets stronger as the bore shrinks, exactly as predicted: 997 dB/m at 25 mm, 1,290 dB/m at 20 mm. Independent CFRP shielding measurements point the same way (37 dB at 2 GHz for single-layer C/C, 68 dB at 2.7 GHz doubled). **But see finding F7: the bound is about *depth*, and at the mouth it buys nothing, which is what makes the mitigation work.** |
| **C3** | ~15–25 g added is plausibly acceptable | **Refuted as stated, still testable** | 15–25 g on a 110–145 g frame is **10.3–22.7%**. Every implement sensor that shipped carried 1.5–3.0% (§5). The one exception, Babolat Play at 10.0%, was integrated at the factory with the frame relaid to compensate — which an aftermarket cartridge cannot do. Worse, mass is the wrong variable: 20 g at 50 mm from the butt moves the **balance point 31–39 mm toward the handle**, against a just-noticeable difference of about 5 mm on the first swing. Racket-customisation practice corroborates the arithmetic independently: 2.8 g at the butt cap is quoted as one balance point (3.175 mm) on a ~300 g tennis racket, which scales to roughly 45 mm for 20 g on a half-as-heavy squash frame. F14 breaks the mass down and finds three quarters of it in the liner and an over-sized cell, neither of which is the price of sensing. The document's other claim here is correct and worth keeping — **swingweight barely moves** (0.4% about the conventional 100 mm axis), because the mass sits essentially on that axis. |
| **C4** | Nordic beats ESP32 on standby, and standby dominates | **Refuted** | The 9 µA delta over a week is 1.51 mAh. One 1-hour session at ~20 mA (MCU 4 mA + ICM 0.42 + ADXL 0.145 + near-continuous NOR page programming ~15 mA) is **19.6 mAh**. Weekly play makes the platform delta **7.7% of the budget**, and it would need **91 days of standby** to pay for one session. Then: the device cannot be in System OFF, because wake-on-motion is a requirement — the ICM-45686's own low-power accelerometer costs tens of µA, call it 70, which is **8× the platform delta**, and BLE advertising between sessions is ~25 µA, another 3×. The standby term that decides anything is the sensor's, not the MCU's. *The conclusion may still be right; the argument is not the reason.* |
| **C5** | Racket-frame kinematics can quantify shot disguise | **Refuted as a measurable target** | See F1. There is no alignment anchor before impact, no human-prediction label, and the corpus the device would collect does not contain the phenomenon. |
| **C6** | ±32 g / ±4000 dps clears the zero-clipping requirement | **Refuted as mounted; recoverable by F15** | See F2, then F15 — a body-diagonal mount buys √3 and changes both answers. ±32 g is exceeded for every cartridge speed between 6.1 and 24.7 m/s. ±4000 dps sits inside the reconstructed tail of the wrist's own distribution before the wrist joint's contribution is added. |

**Which conclusions survive C1 being wrong.** `fc ∝ 1/a`, so C2 strengthens —
that one is safe. The casualties are all on the other side: "generous interior
volume, zero grip-feel impact" in the requirement table; the battery capacity that
sets session length and the charge current that sets the thermal load; the mass
estimate, which is dominated by a liner whose volume is unknown; and Phase 3's
premise that the bring-up board can be relaxed "to a comfortable size" and still
be the same schematic — at ~20 mm across flats the MDBT42Q's **16 mm width** is
most of the usable span before a liner, and the final layout stops being a
relaxation of anything.

---

## 3. Findings

Marked **fatal** (kills the design as written), **expensive-if-late** (fixable, but
not after a board), or **cosmetic**.

### F1 — The primary goal is not a measurable quantity. **Fatal to the stated goal — since resolved by abandoning that goal.**

> **Resolved by scope, not by solution.** The author has abandoned the disguise
> motivation. Nothing below is withdrawn — the metric is still not measurable as
> stated, and would be again if anyone revived it — but it no longer blocks
> anything. See the second revision in §0 for what the device is for instead, and
> what the 800 Hz–1 kHz+ requirement loses when its only justification goes.


The stated primary goal is "quantifying how similar a drive's and a drop shot's
approach are before they diverge". Three things are missing and none is a research
detail.

*There is no alignment anchor.* "Before they diverge" requires two swings on a
common time axis. The only unambiguous event in a racket-frame signal is impact —
which is the *end* of the approach. Aligning on impact and looking backwards makes
"divergence" a function of how long each swing took, so a slower drop appears to
diverge earlier than a faster one purely from duration. Aligning on swing onset
requires a threshold crossing, and `EffortKit`'s own module documentation records
that the threshold is uncalibrated and that the drop is "the soft shot the gyro
threshold is most likely to miss". Both anchors are worse for exactly the shot the
project cares about.

*There is no ground truth.* Disguise is a property of what an opponent can
predict. No racket signal is evidence about that. A metric computed from racket
kinematics and called "disguise" is unfalsifiable: it cannot be wrong, because
nothing says what the right answer was. **The protocol that would fix this exists
and is cheap** — occlude video of the swing at N frames before impact, show it to
squash players, record their predicted shot, and use prediction accuracy as the
label. That is a camera and some volunteers, and it is prior to any hardware.

*The corpus would not contain the phenomenon.* `SquashLab/protocols.json` asks for
45 forehand drops in a row. A player hitting 45 drops in a row is not disguising
anything — they are grooving a stroke, and the thing that makes a shot disguised
is that the opponent does not know which one is coming. The labelled corpus this
device is designed to collect is a corpus of *undisguised* shots. Shots chosen
under uncertainty come from match play, which is where the labels are worst.

*The secondary goal is unharmed and is not in doubt.* Shot-type classification and
season-long usage logging are well-evidenced in the literature at 100–200 Hz
(§5), which makes the 800 Hz–1 kHz+ requirement load-bearing for the primary goal
only — and the primary goal is the one that does not survive this finding.

### F2 — ±32 g rails on an ordinary forehand, before impact. **Fatal to C6.**

For planar rigid-body motion, the centripetal acceleration at a point C on the
racket has a convenient closed form: `a_C = ω·v_C` — angular rate times the point's
own speed. Published squash forehand drive kinematics give a mean racket head
velocity of **30.8 m/s** (shoulder internal rotation 46.1%, wrist hand flexion
18.2%, forearm pronation 12.0%). With the cartridge centre ~50 mm from the butt
and the head centre ~530 mm, `d = 0.48 m`, so `ω = (v_head − v_C)/d`:

| cartridge speed | ω | ω (dps) | centripetal | +1 g gravity |
|---|---|---|---|---|
| 3 m/s | 57.9 rad/s | 3,318 | 17.7 g | 18.7 g |
| 5 m/s | 53.8 | 3,080 | 27.4 g | 28.4 g |
| 6 m/s | 51.7 | 2,960 | 31.6 g | 32.6 g |
| 8 m/s | 47.5 | 2,722 | **38.7 g** | 39.7 g |
| 10 m/s | 43.3 | 2,483 | **44.2 g** | 45.2 g |
| 12 m/s | 39.2 | 2,244 | **47.9 g** | 48.9 g |

Solving `a_C = 32 g` gives roots at 6.10 and 24.70 m/s: **the ±32 g rail is
exceeded for every cartridge speed between them**, and a squash hand at impact is
comfortably inside that band. ±16 g is exceeded above 2.68 m/s, ±24 g above 4.26,
±40 g above 8.41. **Nothing below ±64 g avoids clipping at this head speed.** Add the
tangential term (α·r ≈ 8 g at 500 rad/s²) and it is worse.

Note what the table does *not* say: ω and `a_C` trade against each other, so the
device cannot escape by assuming a whippier or a stiffer swing. A faster hand
lowers the gyro reading and raises the accelerometer's; a slower hand does the
reverse.

The gyro is the better-behaved of the two but not safe. The racket's angular
velocity is the hand's; the watch reads the distal forearm's, which includes
pronation but not wrist flexion or deviation — and hand flexion at the wrist
contributes 18.2% of head speed, roughly 640 dps about the flexion axis. The
wrist's own reconstructed single-axis peak already reaches **4,408 dps** (§1).
Racket = wrist + wrist joint puts ±4000 dps **inside the distribution, not above
it**. Expect railing on the hardest shots, at a rate the drill in F9 would
measure.

**F15 is the cheapest answer to this**, and it was not available to the document
because the document never considers mount orientation: rotating the part so the
handle axis lies on the sensor's body diagonal buys √3 = 1.73×, which takes ±32 g to
an effective 55.4 g and clears the 50.4 g worst case — though only up to a 32.3 m/s
head speed, so it is worth one full-scale step and not a substitute for an adequate
range. For the gyro it is decisive: ±4000 dps becomes 6,928 dps.

The consequence is not that the device is impossible. It is that **the requirement
line "zero clipping" is not met by this BOM as mounted**, the fallback during clipping is the
ADXL375 at 49 mg/LSB — 50× coarser than the ICM's ±32 g range (0.98 mg/LSB) —
and a design whose entire justification is the wrist's clipping now clips too, at a place it has not
budgeted for.

### F3 — The BOM has no regulator, and every part is over-volted. **Fatal to the schematic.**

| Part | Supply | Absolute max |
|---|---|---|
| nRF52832 | 1.7–3.6 V | **3.9 V** |
| ICM-45686 | 1.71–3.6 V | — |
| ADXL375 | 2.0–3.6 V (VDD I/O 1.7 V–VS) | **3.9 V**, both rails |
| Li-Po, fully charged | — | **4.2 V** |

A fully charged cell is 0.6 V above every part's recommended maximum and 0.3 V
above the *absolute* maximum of two of them — the ADXL375 datasheet (Rev. B,
Table 2) rates VS and VDD I/O at −0.3 V to +3.9 V, the same ceiling as the
nRF52832. The BOM lists a charger, a protection IC,
a module, two sensors and a flash, and no regulator; the design never mentions a
power tree. This is the finding that blocks a schematic before anything else.

It also feeds back into C4: adding a regulator adds quiescent current, which is
the axis the platform was chosen on. Nano-quiescent LDOs exist and would not undo
the argument — but the argument was already beaten 8:1 by the sensor (C4), so the
regulator does not change the conclusion, only removes the last reason to believe
the original one.

### F4 — Flash sized for the stated payload is not a "generic small SPI NOR" problem, it is a different memory technology. **Expensive-if-late.**

First, the payload. The document says 800 Hz–1 kHz+ in one place and 110–140 MB/h
in another. Six plus three axes at 2 bytes is 18 B/ms — **65 MB/h**, not 110–140.
The stated range is reconstructible, but only under a configuration written down
nowhere:

| configuration | rate | MB/h |
|---|---|---|
| literal reading: 9 axes @ 1 kHz, 2 B each — *not an available configuration* | 18.0 kB/s | 65 |
| ICM 12 B @ 1 kHz + ADXL375 at its 3,200 Hz max | 31.2 kB/s | **112** |
| ICM 16 B FIFO @ 1 kHz + ADXL @ 3.2 kHz | 35.2 kB/s | **127** |
| ICM 20 B FIFO @ 1 kHz + ADXL @ 3.2 kHz | 39.2 kB/s | **141** |

So 110–140 MB/h means the **ADXL375 pinned at its maximum ODR**, plus FIFO packet
overhead.

The 16- and 20-byte rows are not guesses: they are the TDK family convention, read
off a sibling part's datasheet. The ICM-42670-P (DS-000451 Rev 1.2, Figure 9 and
the `FIFO_COUNT_FORMAT` bit) defines exactly four packet shapes — 8 bytes for
header + one sensor + temperature, **16 bytes** for header + accel + gyro +
temperature + a 2-byte timestamp, and **20 bytes** for the same plus a 3-byte
20-bit extension. The ICM-45686 is a different family (456xy against 426xy) and its
own table is in AN-000478, so this is corroboration and not proof — but the
architecture matches, and the 45686's documented 19-bit gyro and 18-bit accel
output is the same extension idea. That configuration is not the one the
requirement line implies, and the document should say which it means, because the answer changes the flash part, the battery, the
transfer time and the cost.

**The literal reading is not even a configuration the part can be put in.** The
ADXL375's ODR ladder is binary — 400, 800, 1600, 3200 Hz (Table 6) — with **no
1 kHz step**. Running both sensors on one rate means picking 800 or 1600 Hz for the
high-g channel, or running them at different rates and resampling, which makes the
alignment in F13 a thing the firmware has to construct rather than inherit.

**And 3,200 Hz is the worst place on the part's curve to be.** The ADXL375 datasheet
(Rev. B) specifies sensitivity and scale factor — 20.5 LSB/g, 49 mg/LSB — for
**ODR ≤ 800 Hz only**, and at 1600 Hz and 3200 Hz "the LSB of the output data-word
is always 0", so the part drops a bit while still costing two bytes per axis on the
wire. The 3,200 Hz channel therefore buys **49–62% of the whole data budget,
depending on the ICM's packet format, and the 1–2 Gbit flash part, for data whose
scale factor the datasheet does not specify.**

In fairness the dropped bit is not the real cost: at 5 mg/√Hz and a bandwidth of
ODR/2, the noise floor at 3,200 Hz is ~200 mg RMS against a 98 mg step, so the part
is noise-limited either way. The cost is the budget and the unspecified scale
factor.

Then the consequences, which the document's "pick capacity deliberately" does not
reach:

- **Capacity.** One hour at 141 MB/h needs 128 MB; at 112 MB/h, 128 MB still. A
  W25Q256 (32 MB) holds 14 minutes. That is a 1–2 Gbit (128–256 MB) part.
- **Erase, which is the real blocker.** NOR sector erase is 45 ms typ / 400 ms max
  per 4 KB. Pre-erasing 128 MB is 32,768 sectors — **25 minutes typical, 218
  minutes worst case**. There is no point in a squash session where that fits.
- **Write margin.** 256-byte page program is 0.4 ms typ / 3 ms max: 640 kB/s
  typical, **85 kB/s worst case**. The required 39.2 kB/s is 46% of the worst-case
  rate, with erase still to be paid for.
- **SPI NAND is the technology that fits** — 1–2 Gbit at a fraction of the price,
  128 KB block erase in ~2–3 ms — and it brings bad-block management, ECC and wear
  levelling, none of which the plan budgets and all of which are firmware the
  design does not mention.

The alternative worth costing before buying NAND: **the 3,200 Hz ADXL375 is what
breaks the budget**. At 800 Hz — the top of its specified range — the high-g
channel costs 4.8 kB/s instead of 19.2, and the hour is 60 MB with 12-byte ICM
packets or 89 MB with 20-byte ones. A **512 Mbit (64 MB)** NOR covers the first
outright and most of the second — two to four times smaller than the 1–2 Gbit part
the 3,200 Hz configuration forces, cheaper still per bit, and with no NAND firmware
to write. Whether the high-g channel needs 3.2 kHz is a question about the
impact transient, and it is answerable from one recording.

### F5 — BLE is not the primary path, and the cradle already exists. **Expensive-if-late, and it changes the architecture.**

Nordic's own figure for nRF52832 application throughput with 2M PHY and DLE is
~1,400 kbit/s, device-to-device, ideal.

| path | 112 MB | 141 MB |
|---|---|---|
| BLE, Nordic ideal 1,400 kbit/s | 10.7 min | 13.4 min |
| BLE, realistic phone link 500 kbit/s | 29.9 min | 37.6 min |
| BLE, conservative iOS 200 kbit/s | 74.7 min | 94.0 min |
| USB full-speed mass storage, ~800 kB/s | **2.3 min** | **2.9 min** |

Live streaming needs 250–314 kbit/s sustained — 18–22% of the ideal ceiling,
through a carbon tube, for an hour. **Flash is the primary path and always was.**
Calling it "the fallback" inverts the architecture, and it is the reason RF is
billed as the largest risk when it is not.

And the cradle is already in the mechanical design. The cartridge terminates in a
recessed, gold-flashed pad array on the butt cap that docks into spring pins.
**Two more pads is SWD; four more is USB.** Put bulk transfer on the cradle and:
the RF requirement collapses to "advertise a summary and a timestamp", which the
antenna-at-the-mouth mitigation covers with margin (F7); the platform argument
(C4) changes shape entirely; and — see §4 — the openness requirement becomes easy
instead of hard, because USB mass storage needs no software anybody has to write.

### F6 — The plan's cheapest test is missing, and it is the one most likely to end the project. **Fatal to the plan's ordering.**

The document's own unresolved list says the mass and balance question "needs a
physical mockup" and that "no amount of analysis substitutes" for it. That mockup
is 20 g of putty in a butt cap and one session. It appears in none of Phases 0–4.

Phase 0 instead spends about CAD $250–350 on eval boards to de-risk RF. The
sacrificial racket Phase 0 buys is the same racket that answers C1 — and a
sacrificial racket answers C1 for free while it is waiting for the modules to
arrive. **The correct ordering is cost-to-kill, not subsystem**: §8.

### F7 — RF is a smaller risk than billed, and the number that matters is depth, not attenuation. **Cosmetic as a risk; the mitigation is right.**

The waveguide bound is correct and robust. But the design's own mitigation —
antenna at the mouth — is not a hope, it is arithmetic. nRF52832 at 0 dBm TX and
−93 dBm sensitivity, free-space loss 49.7 dB at 3 m, leaves 43.3 dB of budget.
Spend ~26 dB on hand and body blocking, tube detuning of the module's antenna, and
phone antenna and polarisation loss, and what remains buys burial depth:

| antenna depth in a 32 mm bore | below-cutoff loss | margin at 3 m |
|---|---|---|
| 0 mm (flush at the mouth) | 0 dB | **+17.3 dB** |
| 20 mm | 14.6 dB | +2.7 dB |
| 50 mm | 36.4 dB | **−19.1 dB** |
| 100 mm | 72.8 dB | −55.5 dB |

So the real specification is not "measure the attenuation", it is **"the antenna
must be within roughly 20 mm of the mouth, and the cap must be non-conductive"** —
and the bench test's job is to find where the cliff is, not whether there is one.
That is a narrower, cheaper test than Phase 0 describes, and the result is a
mechanical constraint on the cap, not a go/no-go on the project.

Two things the RF section gets right and should keep: the warning about relocating
the antenna voiding a modular grant, and treating the whole assembly as sitting in
an unintended shielded cavity rather than only as an antenna-keepout problem.

### F8 — Reinsertion may be a solved problem the design is still paying for. **Cosmetic, but it buys simplification.**

Keying rib, positive depth stop and per-unit axis calibration are each justified by
reinsertion being "part of normal operation". But the design's own charging
decision removes the reason to remove the cartridge. If the cartridge comes out
only for battery replacement — once a year or less — then the mechanical key is
over-engineered and the **per-unit calibration is under-specified**, because it now
has to survive a year rather than a session, and nothing says how it is stored, in
what frame, or how a reader knows which calibration applies to which recording.
That last point is a data-format question (§4.1), not a mechanical one.

### F9 — No phase tests the hypothesis, and part of it is answerable today. **Expensive-if-late.**

Phases 0–4 end at a board. Nothing in the plan records data and asks whether
racket-frame kinematics separate a drive from a drop better than the wrist already
does. Some of that is answerable now, on hardware that exists:

1. Run `SquashLab`'s `DRIVE DROP` protocol on the watch. It already labels forehand
   and backhand drives and drops with the frozen `16 + side*8 + type` kinds.
2. Train a classifier on wrist epochs and report held-out drive-vs-drop accuracy.
3. That number is the headroom the racket sensor has to buy. If the wrist already
   separates drive from drop at 90%+, the racket's case rests entirely on the
   disguise question — which F1 says is not currently measurable.

This costs one court session and no hardware, and it is currently scheduled to
happen after Phase 4.

### F10 — The certification and rules questions, answered. **Cosmetic.**

*Rules.* A racket-embedded sensor is **permitted in sanctioned squash**: the World
Squash rules state that "player analysis technology may be incorporated into the
playing equipment as long as that equipment conforms to the specifications in
Appendix 5", where player analysis technology covers recording, storing,
transmission, analysis and communication to the player. The racket must still meet
the racket specification. Maximum weight **255 g**, so a 145 g frame plus 25 g is
comfortably inside. Maximum overall length is **686 mm** against production rackets
built at 685 mm — about 1 mm of headroom, so **anything protruding from the butt is
out of specification**, where the ITF leaves a tennis racket 51 mm to play with.

**This is recorded rather than applied.** The device is a training tool, not
competition equipment, so none of it is a design driver — it only says what would
have to change if the thing were ever to be used in sanctioned play, and the answer
is the mounting position and nothing else. Coaching is permitted only in the
intervals between games, so a device that records and says nothing during play is
the compliant shape; a live-feedback display would not be.

*Certification.* A pre-certified module's modular grant attaches to a product as
marketed, and "published as files", "sold as a kit" and "sold as a unit" are three
different regulatory objects. Files are not a product and carry no obligation.
Selling assembled units makes the seller the responsible party, and the modular
grant is what makes that survivable without full intentional-radiator testing.
**This is the strongest reason to keep the pre-certified module even at a power
cost** — and it is the reason the design's own warning about relocating the antenna
matters more than it looks.

*A cracked frame's layup.* A frame cracked at the head is representative at the
handle; a frame cracked at the throat or handle is not. Since the test only needs
the handle intact, "condition doesn't matter" is nearly right and should say
"handle intact" instead.

### F11 — The thermal path is fine; the cell temperature monitoring is not. **Expensive-if-late, and a safety finding.**

At 100 mA charge from 5 V into a 3.7 V cell the MCP73831 dissipates 130 mW. In
SOT-23-5 at θJA ≈ 235 °C/W the junction rises ~30 °C; the cavity bulk rise is
0.7–3.8 °C depending on charge current and whether the grip covers the tube.
**The cavity is not the problem.** The problem is what is not there: the MCP73831
has thermal regulation based on **its own die temperature** and no THERM/NTC pin,
so nothing in this design ever measures the cell. Charging a sealed Li-Po with no
cell temperature sensing, inside an object that gets struck and tapped on the
floor, is the one finding here that is about somebody getting hurt — and in an open
design, about somebody *else* getting hurt, building from published files. It wants
either a charger with a battery temperature input or an NTC and an MCU-gated charge
enable, and the decision recorded.

### F12 — The inductive-charging rejection is right, and for a better reason than given. **Cosmetic.**

Eddy-current heating and foreign-object detection are both real. The simpler
argument is that inductive charging solves a problem contact pads already solve, at
the cost of a coil that consumes the scarcest dimension in the design (C1) — and
the design's own note that "there is no move-it-to-the-mouth escape" is the correct
observation. Keep the rejection; it is one of the better-reasoned passages.

### F13 — The firmware's own timestamping requirement is unsatisfiable for the high-g channel. **Expensive-if-late.**

The design states it as a hardware requirement: "**Timestamps must derive from each
IMU's own ODR/FIFO clock**, not from MCU interrupts serviced under the BLE
SoftDevice." It then names the ICM-45686's FIFO/timestamp register behaviour as an
open item to confirm. The part nobody checked is the other sensor.

**The ADXL375 has a 32-level FIFO and no timestamp in it.** Its FIFO stores x, y and
z for the latest 32 samples and nothing else; there is no timestamp field and no
sample counter. The asymmetry with the primary IMU is stark: every TDK FIFO packet
that carries both sensors carries a **2-byte timestamp with them** (ICM-42670-P
DS-000451, Figure 9 — the 16- and 20-byte packets both have one). The ICM
timestamps itself in hardware, exactly as the requirement asks. The ADXL375
cannot. So an ADXL375 sample can be placed on a timeline only by counting
back from the moment the host read it — which is MCU-interrupt-derived timing, the
exact thing the requirement forbids.

The buffer depth makes it tighter than it looks. 32 samples at 3,200 Hz is **10 ms**
of slack before samples are silently discarded, against the ICM-45686's 8 KB FIFO,
which at 20 B and 1 kHz holds 400 ms. **The high-g channel, not the primary IMU, is
the hard real-time deadline in this design**, and it is the one the firmware section
never mentions. Ten milliseconds is still ~100× the "tens to hundreds of µs" of
radio-ISR preemption the document worries about, so it is survivable — but it is the
budget that actually has to be met, and it shrinks as ODR rises.

Two consequences. The alignment between the two sensors becomes a firmware
construction rather than a hardware fact, so **it has to be recorded in the file and
its uncertainty stated** (§4.1 #3 asks for this between the racket and the watch; it
turns out to be needed *within* the cartridge too). And it is one more reason to run
the ADXL375 at 800 Hz rather than 3,200 (F4): four times the deadline, a specified
scale factor, and a quarter of the data.

**There may be a hardware answer already in the BOM, and it is worth one document to
find out.** AN-000484 describes the ICM-45686 as a "dual interface (UI + AUX)" part
whose AUX "supports SPI slave mode for connection to OIS controllers or **I2C master
mode for connection to external sensors**" — this is the difference between it and
the ICM-45605, and the AUX pins are brought out on the EVB. The ADXL375 speaks I²C.
If the ICM's I²C master places external-sensor data into its **own 8 KB FIFO**, as
the InvenSense pattern has historically done, then hanging the high-g channel off
the AUX port puts both sensors in one timestamped stream on one clock — F13 stops
being a firmware problem and an MCU SPIM instance comes back (§7 item 12). Two
things to check before believing it, both in AN-000478: whether external-sensor data
reaches the FIFO at all, and at what master polling rate. The bandwidth is
plausible but not generous — 6 bytes at 800 Hz is 4.8 kB/s plus addressing, against
the ADXL375's 400 kHz I²C ceiling.

### F14 — Three quarters of the added mass is two items, and neither of them senses anything. **Fatal to C3 as designed; fixable without touching the sensors.**

The design states its own breakdown: 15–25 g "dominated by the battery (~6–8 g) and
the printed liner (~6–10 g)". That is **12–18 g of a 15–25 g total — roughly three
quarters of the mass in two parts that carry no signal.** Everything that does the
actual job (PCB, module, both IMUs, flash, charger, protection, cap, coating) is the
remainder. So the question "why is this two to three times what shipped in every
other sport" has a specific answer, and it is not the sensing.

**The liner, 6–10 g, is the price of going inside.** It is 100 mm of plastic filling
the annulus between a rigid cartridge and an uncontrolled bore. For a 20 mm bore
around a 14 mm cartridge over 100 mm that annulus is 20 cm³ — 25 g solid, and the
design's 6–10 g is a sparse-infill print of it. It scales with **length**, it exists
only because the bore is uncontrolled and varies per racket, and an external puck
does not have one at all. This is the single largest line item and it buys nothing
except the decision to be inside the handle.

**The cell, 6–8 g, is sized for a charging model the design has already replaced.**
6–8 g is a 300–400 mAh pouch. Against ~19.6 mA of active draw that is **15–20
sessions between charges** — which would be the right cell for a device you charge
occasionally. But the mechanical design already terminates in contact pads that dock
in a cradle, so it charges after every session, and 15–20 sessions of reserve is
reserve nobody uses. This is the same pattern as F8: the cradle removed a
requirement, and the design kept paying for it.

It compounds with F4. Of that 19.6 mA, **15 mA — 77% — is NOR page programming**,
which exists because the payload is 39.2 kB/s. Drop the ADXL375 to 800 Hz as F4
recommends and the payload falls to 16.8 kB/s, active draw to ~11 mA, and one hour
costs 11 mAh. A **100–150 mAh cell (2.1–3.2 g)** then gives 9–14 sessions — still far
more reserve than a cradle-charged device needs.

Budgeting the two architectures with the same sensors and the same raw recording:

| | internal cartridge, as designed | external puck, F4 payload |
|---|---|---|
| cell | 300–400 mAh, **6–8 g** | 100–150 mAh, **2.1–3.2 g** |
| printed liner | **6–10 g** | **none** |
| PCB, module, two IMUs, flash, charger, protection | 2–4 g | 2–3 g |
| shell, cap, pads, coating, foam / housing and mount | 1–3 g | 2–4 g |
| **total** | **15–25 g** | **6–10 g** |

That lands on the shipped comparators exactly — Zepp 6.25 g, Arccos 7.34 g, Sony
8.0 g, Garmin CT10 9.0 g — and on the figure reported for Racketware, the one
squash-specific product, which appears to be under 10 g (no published figure found;
the number is consistent with the class).

**Read the right-hand column as a bound, not a proposal.** §5 finds that a
protruding external puck is almost certainly outside the 686 mm racket
specification, so that architecture may not be available in squash at all. Its
value here is the arithmetic showing where the mass went — and the liner line is
zero for the butt puck too (§5), and for the insertable plug it is a sled of
1–4 g rather than a 100 mm liner of 6–10 g. **The half that is
recoverable without leaving the handle is the cell**: ~4 g, for nothing except
sizing it against the charging model the design actually has.

**So the mass is not the price of the sensing, and it is not the price of recording
raw.** It is ~8 g for being inside the handle and ~4 g for a cell sized against a
charging model the design no longer has. Neither requires giving up a single
sensor, a single hertz, or an unclipped sample. One honest caveat in the other
direction: an external puck has far less volume — Zepp's was 25.4 × 12.3 mm, about
6 cm³ — so a 100–150 mAh cell plus a 10 × 16 mm module plus flash fits, but not
comfortably, and that is a packaging question a mockup would answer alongside F6.

---
### F15 — Mounting the IMU off-axis buys a factor of √3, and it is the cheapest fix for F2. **Not a defect; a correction to how F2 should be answered.**

Clipping is **per-axis**, so a vector of magnitude M clips when its largest
*component* exceeds the full-scale range R. The set of representable vectors is a
cube of side 2R, not a sphere, and how much magnitude fits depends on direction:

| dominant direction, in sensor axes | largest component | clips at |
|---|---|---|
| along an axis `[1,0,0]` | 1.000 | **1.00 R** |
| face diagonal `[1,1,0]` | 0.707 | 1.41 R |
| body diagonal `[1,1,1]` | 0.577 | **1.73 R** |

**The worst possible place to put a known dominant direction is along a sensor
axis, which is where a naively aligned mount puts it.** Rotating the part so that
direction lies on the cube's body diagonal buys **√3 = 1.73×** of range for free.

This matters because F2's dominant term has a fixed direction in the racket frame.
Centripetal acceleration points from the sensor toward the instantaneous centre of
rotation, which sits near the hand — so it lies along the handle's long axis, every
shot, every time. That is exactly the case the trick is for.

**What it buys against F2.** The maximum centripetal acceleration at a 30.8 m/s head
speed is `(v_head/2)²/d = 494 m/s² = 50.4 g`, reached at a cartridge speed of
15.4 m/s:

| part | axis-aligned | on the body diagonal | against a 50.4 g peak |
|---|---|---|---|
| ±16 g | 16 g | 27.7 g | still clips |
| ±24 g | 24 g | 41.6 g | still clips |
| **±32 g** | 32 g | **55.4 g** | **never clips** |
| ±40 g | 40 g | 69.3 g | never clips |

So a diagonal mount takes the ICM-45686's ±32 g from "clips across the whole
plausible range of cartridge speeds" to "clears the worst case with 10% to spare".
**It does not make F2 go away.** 30.8 m/s is a published *mean*; the head speed at
which a diagonal ±32 g starts clipping again is **32.3 m/s**, which a hard hitter
will pass. The honest reading is that the trick is worth about one full-scale step,
and it should be spent on top of an adequate range rather than instead of one.

**For the gyro it is decisive.** ±4000 dps on the body diagonal is **6,928 dps**
against F2's racket estimate of 2,900–3,300 dps at impact with a tail past 4,000.
That resolves C6's gyro half outright.

**It costs no resolution and no noise.** Magnitude error is rotation-invariant —
`dM = (a·da)/|a|` gives variance σ² whatever the direction — and a 200,000-sample
Monte Carlo confirms it: magnitude error 1.0007σ both axis-aligned and diagonal, for
a per-axis noise of σ. There is no dynamic-range penalty for spreading a signal
across three axes.

**The headroom is moved, not created**, and that is the thing to check rather than
assume. Once the handle axis sits on `[1,1,1]`, directions perpendicular to it clip
at 1.22 R to 1.41 R — so nothing drops to 1.00 R, and every direction of interest is
better off than one of them being axis-aligned. That is only true because the
dominant direction is known and fixed; for an unknown direction distribution the
worst case is 1.00 R whatever you do.

Four practical consequences:

- **A rotated footprint is not enough.** Rotating the part on the PCB gives one
  degree of freedom, about the board normal. A true body diagonal needs the board
  itself tilted — a moulded wedge in the enclosure seat. Cheap in a butt-cap
  carrier, expensive under a grip where thickness is the scarce dimension (§5), so
  **the two carriers may want different angles**, which is fine because calibration
  is already per-enclosure.
- **It makes the rotation part of the data contract.** Raw LSB stops corresponding
  to anything anatomically meaningful, so the mount rotation has to be published
  with the recording. This does not weaken §4.1 — record raw, publish the transform —
  but it moves the rotation matrix from nice-to-have to mandatory.
- **Existing analysis code is unaffected.** `EffortKit`'s `gyro_magnitude` is the
  vector norm, which is rotation-invariant, so nothing downstream needs to change.
- **A railed axis becomes more informative, not less.** Axis-aligned, when the one
  loaded axis rails you know only "≥ rail". Diagonal, two unrailed axes still carry
  direction, and with the dominant direction known the magnitude can be partly
  reconstructed — which suits this repository's existing position that saturation is
  signal rather than noise.

**Pick the angle by measurement, not by symmetry.** `[1,1,1]` is optimal for one
dominant direction; the real distribution has several (drive, drop, volley, serve,
forehand and backhand). Record a session, take the distribution of vector directions
in the racket frame, and choose the rotation that maximises the *minimum* margin
across it. That is a small optimisation over data this project can already collect,
and it is the house rule applied to a number that would otherwise be a guess.

---

## 4. The openness verdict

### 4.1 Where the design as written fails the requirement

**Fix in rev 1** (does not change the architecture):

1. **Nothing says what the device may do to a sample before writing it.** Define it:
   raw per-sensor LSB, unscaled, each sensor's own FIFO clock, no on-device fusion,
   filtering or scaling. `Squash/README.md` already made this call — "so the
   recording keeps the saturation rather than hiding it behind a conversion" — and
   given F2, the racket device will clip too, so the same reasoning applies with
   more force.
2. **The per-unit calibration coefficients are not in the data path.** They are
   mentioned as a manufacturing step and nowhere as something that travels with a
   recording. A calibration held only by the builder is exactly the hostage the openness
   requirement forbids: the file must carry its own coefficients, its configuration, and the
   data-quality channels the design already specifies (die temperature, battery
   voltage). **The ADXL375 datasheet makes this a correctness requirement, not a
   nicety**: its scale factor is specified only to ±10% (44–54 mg/LSB), its 0 g
   offset is ±400 mg typical and ±6,000 mg worst case, and its on-chip offset
   registers trim in **1.56 g steps** and are cleared on every power cycle. Nothing
   useful can be corrected on the part, so the coefficients have to live in the
   file or they do not exist.
3. **No time-sync mechanism.** A racket sample and a wrist sample are useless
   together unless they can be placed on one timeline, and nothing in the document
   proposes a mechanism. It must be recorded in the file, not assumed.
4. **The 32.768 kHz crystal is not on the module.** Raytac supplies the footprint
   and a recommended spec for an *external* 32.768 kHz crystal; it is a board part,
   not a module feature. The firmware requirement that depends on it is therefore a
   schematic item nobody has drawn.

**Changes the architecture:**

5. **The nRF52832 has no USB peripheral. The nRF52840 does.** This is the single
   decision with the largest effect on the openness requirement. A custom BLE GATT service needs a tool
   somebody wrote, hosted somewhere, maintained by someone — which is precisely the
   failure mode that killed five products in §5. **USB mass storage needs nothing,
   on every operating system, for ever.** Zephyr ships the USB MSC sample and can
   expose an external flash as a disk over littlefs or FAT, on nRF52840, today. The
   cradle already has the pads (F5). The cost is a larger, more expensive module and
   a part change; the benefit is that the openness requirement stops depending on
   anybody's continued goodwill.
6. **The SoftDevice is a binary blob.** An open path exists on both platforms —
   Zephyr's open-source Bluetooth controller for nRF5x, or NimBLE under ESP-IDF —
   so this is resolvable, but it is a different stack with different timing
   behaviour, which interacts with the document's own concern about radio ISRs
   preempting application code. **Resolution: use Zephyr with the open controller,
   and let F5 remove the pressure** — if bulk transfer is over the cradle, BLE
   carries a status advert and a timestamp, radio activity during recording drops
   to near zero, and the ISR-jitter concern that motivated the dual-core discussion
   largely evaporates.

### 4.2 The raw-data path, concretely

Flash → USB mass storage over the cradle → files on a disk. BLE carries session
metadata, battery state and a sync timestamp, not bulk. No account, no cloud, no
server-side calibration table, no pairing key held by one app.

### 4.3 The format, and where it is specified

`RecorderKit` already writes three CSV files on one shared clock, byte-compatible
with the SDK simulator's `Sensor::ImuFusionSource` playback parser, and its marker
`kind` column is already a frozen wire format with a documented extension rule —
apps with their own vocabulary take values above the defined ones. `SquashLab`
already uses that rule for `16 + side*8 + type`.

**Recommendation: define a documented binary format on the device, and specify a
converter to RecorderKit CSV as part of the same repository.** Not CSV on the
device: at 39 kB/s a text encoder is CPU and flash the cartridge does not have, and
the sample file's byte format is load-bearing for a 100 Hz six-axis parser that
this device does not produce. Not a new incompatible thing either: the converter is
what makes a racket recording readable by everything that already exists, and the
marker vocabulary must be RecorderKit's, extended, never redefined.

The format specification lives in `SquashSensor/Docs/FORMAT.md` and is versioned
independently of the firmware. A third-party reader is a first-class citizen. The
test that it works is the one this repository already uses everywhere else: a host
test that reads a recorded fixture and asserts on it.

### 4.4 Licences

**Recommend: CERN-OHL-P 2.0 for the hardware, MIT for firmware and tooling, CC BY
4.0 for documentation.**

The repository is MIT and should stay MIT for code — the apps derive from the UNA
SDK's MIT-licensed examples and a licence change would be gratuitous. But MIT is a
*software* licence: it grants copyright permissions over source code and says
nothing about the things that make hardware reproducible — layout files, the
patent question, or the obligation to pass the design files along. A
hardware-specific licence earns its complexity here for exactly one reason: it
names the design files as the thing being licensed. CERN-OHL-P (permissive) rather
than -S (strongly reciprocal) keeps the spirit of the repository's MIT choice: a
club that wants to build ten and not publish their changes should be able to.

This is not a novel arrangement — ProtoCentral's Move Ultralight, an open-source
sports wearable announced April 2026, uses exactly CERN-OHL-P 2.0 for hardware and
a permissive licence for firmware.

### 4.5 What must be published, and what currently blocks it

| OSHWA requirement | Can this design produce it? |
|---|---|
| Schematic | Yes — KiCad is already the plan |
| Editable layout | Yes |
| BOM with orderable part numbers | Yes, once §7's findings are fixed |
| Mechanical CAD, editable | **At risk** — the liner is per-racket and parameterised by a bore nobody has measured. It must ship as a parametric model with measured dimensions as inputs, not as an STL of one racket. |
| Firmware source | Yes, on the open-controller path (4.1 #6) |
| Calibration procedure and jig | **Missing** — named as a requirement, never specified |

**No part in the BOM has an NDA problem.** The one the design flagged — "verify
per-range noise density against the full datasheet table" for the ICM-45686 — is
public: TDK publishes DS-000577 and, more importantly, **AN-000478, the user guide
carrying the full register map**, as a free PDF. The ADXL375, MCP73831, BQ2970,
nRF52832 and W25Q datasheets are all public. This is a real result and it removes
a risk the design named.

**Reproducibility by a stranger** has three barriers, in descending order:
the per-racket liner (fix: parameterise it); the LGA parts, which force paid reflow
(fix: publish an assembly-house BOM and placement files, and note that the ADXL375
is *also* a 14-lead LGA — the document's claim that only the ICM-45686 cannot be
hand-soldered is wrong); and per-unit axis calibration (fix: a printable jig, and
the coefficients in the file per 4.1 #2).

**Availability.** The parts are current and stocked. The one to watch is the
ADXL375: its datasheet is at Rev. B and dated April 2014, and the ordering guide
lists three live part numbers with no not-recommended-for-new-designs marking — so
it is available, but it is a decade-old part and the only one in the BOM with no
obvious second source at ±200 g in that footprint.

### 4.6 The app

The app is a consumer of the format, co-developed with it, and knows nothing the
format does not carry. It lives in this repository, under MIT. The contract is one
sentence: **anything the app computes, it computes from files anyone else can also
read.**

---

## 5. What shipped in other sports, and what happened to it

| Product | Sport | Mount | Mass | Rate | Transfer | Raw data to owner | Status |
|---|---|---|---|---|---|---|---|
| Babolat Play | Tennis | **Integrated in handle at manufacture**, piezo + IMU | 29.9 g incl. cap (10.0% of 300 g) | — | App, cloud | No | **Announced end 1 Mar 2021; app off 31 Dec 2021** |
| Babolat POP | Tennis | Wristband | — | — | App, cloud | No | **Cloud off 23 Mar 2021** |
| Sony SSE-TN1 | Tennis | Butt cap, external | 8 g (2.7%) | — | App, cloud | No | **Service ended 30 Sep 2021**; cannot log in, sync or record |
| HEAD Tennis Sensor | Tennis | Butt cap, external | ~6 g (2.1%) — *inferred: it is the Zepp unit rebadged* | — | App, cloud | No | **Discontinued 13 Sep 2021** |
| Zepp Tennis 2 | Tennis | Butt cap, external; flex/pro/insert mounts | 6.25 g (2.1%), 25.4 × 12.3 mm | >1000 Hz, **dual accel + dual 3-axis gyro** | 3,500 swings in flash, then app | No | **Discontinued 2020** |
| Arccos Caddie | Golf | Grip cap, external, on a hollow graphite shaft | 7.34 g twist-in / 4.40 g grip-embedded (1.5–2.4%) | Shot events | App, cloud | No native export; third-party API scrapers exist | **Alive** — Gen 4, Jan 2025. **Subscription required**: no membership, no new rounds |
| Garmin Approach CT10 | Golf | Screws into the grip end | **9 g (3.0%)** | Shot events | Watch/app | No | **Alive**. **CR2032, user-replaceable, ~4 years, no subscription** |
| Diamond Kinetics SwingTracker | Baseball | Bat knob, external | 14.2 g (1.6% of 880 g) | **1600 Hz** | App | No | — |
| Racketware | **Squash** | **Mount taped under the grip; sensor clips into the mount** (reported by this repository's author from a unit seen, not a published figure) | <10 g reported | — | iOS/Android app | No | PSA official partner; gen 2 c. Dec 2021 |
| ProtoCentral Move Ultralight | General | Wearable | — | — | — | Yes | **Open: CERN-OHL-P 2.0 + Apache 2.0**, Apr 2026 |

**Compare balance shift, not mass ratio — squash is lighter, so it is worse per
gram.** Mass as a fraction of the implement is the natural comparison and it
flatters this project, because a squash racket is half the mass of a tennis racket
and so moves twice as far per gram added at the butt:

| | mass | frame | % | balance shift | × JND |
|---|---|---|---|---|---|
| Zepp Tennis 2 | 6.25 g | 300 g | 2.1% | −6.1 mm | 1.2 |
| Arccos Caddie | 7.34 g | 300 g | 2.4% | −7.2 mm | 1.4 |
| Sony SSE-TN1 | 8.0 g | 300 g | 2.7% | −7.8 mm | 1.6 |
| Garmin CT10 | 9.0 g | 300 g | 3.0% | −8.7 mm | 1.7 |
| Diamond Kinetics | 14.2 g | 880 g | 1.6% | −6.5 mm | 1.3 |
| Babolat Play *(net to the player — the frame was relaid)* | — | 300 g | **0%** | **0 mm** | **0** |
| **butt-cap carrier (F14, 5 g net)** | 5 g | 145 g | 3.4% | **−11.7 mm** | **2.3** |
| **internal cartridge, as designed** | 15–25 g | 145 g | 10–17% | **−33 to −52 mm** | **6.6–10.3** |

**5 g on a 145 g squash racket shifts the balance further than 8 g on a 300 g tennis
racket.** So the shipped band is 1.2–1.7× the just-noticeable difference; the
butt-cap carrier is 2.3×, which is **1.4× beyond demonstrated practice**; the
internal cartridge is 6.6–10.3×, which is **4–6× beyond it.**

Those are two different kinds of question, and it is worth being explicit about which
the prior art answers. **For the carrier it largely does**: one to two JNDs past a
band that five products shipped into and two still sell into is a modest
extrapolation, and "noticeable but not disabling" is a fair reading of the record.
**For the cartridge it does not**: nothing has ever shipped at 4–6× the shipped
band, and the one product that approached this design's mass ratio removed frame
material so that the player felt nothing at all.

Two things that do not transfer even for the carrier, both unmeasurable from the
record. Squash strokes involve more wrist and more touch than a tennis groundstroke
or a golf swing, and the drop — the shot this project exists to study — is the most
touch-dependent of them. And these products sold mostly to recreational players;
whether a competitive one accepts 2.3× JND is not something the sales record
answers. **Note also that "you get used to it" is an inference, not a finding**: what
the record shows is that products shipped and sold, which is weak evidence of
acceptability and says nothing about adaptation time.

**The mass finding, restated.** Everything external clusters at 1.5–3.0%. The
only product above that is the only one integrated at the factory, where Babolat
could relay the frame to compensate. This design proposes **10.3–22.7%**, which is
three to fifteen times what any competitive player has accepted in any sport, and
it cannot compensate because the frame already exists.

**Every *aftermarket* implement sensor found the butt end from the outside.** The
only one that went inside the handle was built into the frame at the factory, by
the frame's manufacturer, and even it put the electronics in a moulded handle cavity
designed around them rather than a bore that already existed. Arccos is
the closest structural analogue — a sensor in the grip cap at the mouth of a hollow
graphite shaft — and it sits *outside* the tube, which is why it has no waveguide
problem, no sealed-cavity thermal problem, no per-club liner and a serviceable
battery. Garmin went further and used a coin cell, which removes charging entirely.
Going inside buys: protection from impact, invisibility, and a longer lever for a
two-accelerometer baseline (§6). It costs: RF, thermal, servicing, a per-racket
liner, and the mass and balance findings above. The external butt-cap puck is not
the rejected grip wrap; it is a design this document never considers.

**In squash it would not be legal, though that does not bind here.** Overall racket
length is capped at
**686 mm** by the World Squash racket specification, and production squash rackets
are built at **685 mm** — about 1 mm of headroom. Every sport where the external
butt-cap puck shipped has room to spare: the ITF caps a tennis racket at 29 in
(736.6 mm) against a 27 in (685.8 mm) standard frame, **51 mm of headroom**, and
golf and baseball have more. Zepp's puck was 12.3 mm thick. On a squash racket that
is ~697 mm and outside the specification, and the Player Analysis Technology clause
does not help — it permits the technology in the equipment, it does not exempt the
equipment from the racket specification (F10).

**But this device is a training tool, so that is a fact about the sport and not a
constraint on the design.** It is worth knowing — it is probably part of why the
squash category is thin where tennis had five products, since a vendor selling to
competitive players has to clear it and this project does not. With legality set
aside, the protruding butt-cap puck is back on the table alongside a flush
replacement cap (which adds no length but offers only ~3 cm³) and the third option
below.

### How the one shipping squash product mounts, and what that position gets

Racketware uses **a mount whose tape tucks under the grip wrap, with the sensor
clipping onto that mount at the butt.** (Reported by this repository's author from a
unit seen; no published description found.) The anchor is under the wrap; the sensor
body is a puck at the butt end. The design document rules the family out in its
first line of mechanical architecture — "It is not, and was never meant to be, a
wrap mounted around the grip exterior" — which is a fair description of a wrap and
not of this, and the reason given is grip feel, which nobody has measured.

It is worth setting out what that position gets, because it is most of the list:

| | internal cartridge, 100 mm | **butt puck (Racketware-style)** |
|---|---|---|
| Overall length | adds none | **adds none** — and not binding here in any case |
| RF | below-cutoff waveguide; the document's "largest open technical risk" | **entirely outside the tube — the problem does not exist** |
| Per-racket liner | 6–10 g, the largest mass item (F14) | **none; tape adapts to any handle** |
| Bore dimensions (C1) | every downstream number depends on them | **irrelevant** |
| Orientation repeatability | keying rib + depth stop into a bore of unknown shape | **the clip is the key, and the handle's octagonal bevels are the datum** |
| Thermal (F11) | sealed cavity, no airflow | outside the cavity |
| Charging and service | contact pads, cradle, sealed cell in a struck object | **sensor unclips; the cell leaves with it** |
| Two-accelerometer baseline (§6) | 50–80 mm available | ~20 mm — thin but not zero |
| Grip feel | none | the anchor tape under the wrap — the cost, and it is unmeasured |
| Volume for a cell and flash | generous | the binding constraint |

**This eliminates the document's own largest stated risk by mounting choice.** RF,
the thing Phase 0 buys hardware to de-risk and Phase 2 gates the whole plan on,
simply stops being a question when the antenna is outside the carbon. So do C1, the
liner, the bore tolerance, the thermal cavity and the sealed-cell safety finding.

Two things it genuinely costs. The **two-accelerometer baseline** (§6) needs 50–80 mm
of separation along the handle axis to give α at 2.5–4 g; a clip-in puck gives
millimetres, so that capability is gone. And **grip feel**, which is the stated
reason the document rejected the family — and which is testable the same afternoon
as F6, by taping something the right size under a grip and playing. It is the same
shape of experiment, on the same racket, in the same session.

One caveat that matters more than it looks: **Racketware's form factor works for
Racketware's data model.** Nothing suggests it records continuous raw IMU; like every
product in this table it almost certainly extracts events. A 5–9 cm³ envelope is
generous for that and tight for an hour of 1 kHz raw plus the flash to hold it — so
adopting the mount does not by itself dissolve the volume problem. It does after
F4's payload cut, which is one more reason that cut is load-bearing.

### What the internal cartridge has left

With competition legality out of scope, the case for going inside the handle rests
on exactly two things, and the rest of this review has already weakened both.

**Volume.** ~20–30 cm³ against ~5–9 cm³ outside. Decisive if the device needs a
300–400 mAh cell and a 1–2 Gbit flash — but F4 says run the ADXL375 at 800 Hz and a
512 Mbit part covers the hour, and F14 says the cell is sized for a charging model
the design replaced, so 100–150 mAh does it. After both, the payload fits the
smaller envelope and the volume advantage largely dissolves.

**A two-accelerometer baseline.** 50–80 mm of separation inside the handle against
millimetres outside, worth 2.5–4 g of angular-acceleration signal at ~25× the noise
floor (§6). This one is real and irreducible. But §6 also establishes what it does
*not* buy: two accelerometers give α without differentiating the gyro, and they do
**not** locate the swing pivot, so the body-worn reference is still needed either
way. A useful advantage, not a transformative one.

**Against those two it spends:** the largest open technical risk in the document
(RF), the largest single mass item (the 6–10 g liner, F14), every number that
depends on a bore nobody has measured (C1), a sealed thermal cavity and the
cell-temperature safety finding (F11), a per-racket liner that is the main barrier
to anyone else reproducing the design (§4.5), and the contact-pad-and-cradle
charging apparatus that exists only because the cartridge cannot come out easily.

That is a poor trade, and it was a defensible one only while the racket
specification appeared to close the alternatives. **It no longer does.**

### One board, two enclosures — and the arithmetic says it works

The strongest version of this is not to choose a mounting position at all: design
**one electronics module that fits two carriers** — an easy one (a mount under the
grip wrap, or a puck over the butt) and a harder, better one (a replacement for the
racket's own butt cap). It is worth checking whether the smaller enclosure makes the
board unbuildable, because that is the objection. It does not.

| | mm³ |
|---|---|
| MDBT42Q module | 352 |
| ICM-45686 | 6 |
| ADXL375 | 15 |
| 512 Mbit NOR (F4's part), USON-8 | 7 |
| charger, protection, FET, passives | 48 |
| **silicon subtotal** | **428 mm³ = 0.43 cm³** |
| 100 mAh cell (F14's size) | +0.81 cm³ |
| **total** | **1.24 cm³** |

Against a cap-only butt replacement at ~3 cm³ that is **41% full** — tight but
buildable; against a butt puck at ~6 cm³, 21%. The module dominates at 82%
of the silicon volume, and the binding dimension is **thickness, not volume**: a
butt cap is 3–5 mm and the module (2.2 mm) plus PCB (0.8) plus cell (~4) will not
stack inside that. In plan they need not — 30 × 25 mm is 750 mm² and the module
plus cell is about 400 — so they sit side by side and the assembly is ~4.8 mm,
which works if it may extend a few millimetres into the mouth of the bore. That
extension is free volume and it is also exactly where F7 wants the antenna.

**The butt-cap enclosure is close to net-zero added mass, which nothing else in this
document achieves.** It replaces a part that is already there: a 30 × 25 mm cap
4 mm thick in moulded plastic is ~3 g, so an 8 g assembly is **4.5–5.5 g net, or
3.1–3.8% of a 145 g frame** — inside the band every shipped product occupies (Zepp
2.1%, Arccos 2.4%, Sony 2.7%, Garmin 3.0%) and against the internal cartridge's
10.3–22.7%. Balance follows: 5 g net at the butt shifts it **12 mm**, 2.3× the
just-noticeable difference, where the 20 g cartridge at 50 mm shifted it 39 mm and
7.8×. That is the difference between a question and a problem. (The cap's real mass
is an estimate; the Phase 0 racket settles it with a scale.)

**RF is where the two genuinely differ, and the plug's constraint is tighter than
"at the mouth".** The puck sits entirely outside the tube, so there is no waveguide
term at all — the document's largest stated risk simply does not exist for it. The
plug's antenna *is* in the bore, and for a 25 mm bore at 997 dB/m the margin at 3 m
runs **+17.3 dB flush, +7.3 dB at 10 mm, and −2.6 dB at 20 mm**. So the plug works
only if **the radio lives in the replacement butt cap and the sled carries the cell
and the outboard accelerometer inward** — never the module. That is a constraint the
design can meet, but it is a constraint, and it means Phase 2 still has to happen
for the plug and not for the puck.

Three things this costs, none of them hidden:

- **The two-accelerometer baseline belongs to the plug, and scales with sled
  length.** A butt puck is compact, so its separation is its own depth — about
  20 mm, giving an α term of 1.02 g at α = 500 rad/s², or 10× the ADXL375's 100 mg
  RMS noise at 400 Hz bandwidth. Measurable, but thin. A sled reaches further:

  | | separation | α term at 300 / 500 / 800 rad/s² | SNR at 500 |
  |---|---|---|---|
  | puck at the butt | ~20 mm | 0.61 / 1.02 / 1.63 g | 10× |
  | sled, 40 mm | ~30 mm | 0.92 / 1.53 / 2.45 g | 15× |
  | sled, 70 mm | ~60 mm | 1.84 / 3.06 / 4.89 g | 31× |
  | sled, 100 mm | ~90 mm | 2.75 / 4.59 / 7.34 g | 46× |

  **Sled length is therefore a real trade, and it is a cheap one**: 40 mm to 100 mm
  costs 1.8–2.4 g of printed sled and buys 3× the α signal and the cell volume with
  it. Two things come with the separation, neither fatal. The outboard sensor sits
  further from the instantaneous centre, so `a = ω²r` is 1.5–1.8× the module's — 40 g
  at the module is 61–72 g out there, which rails a ±32 g part even diagonal-mounted
  (F15), so **the ±200 g ADXL375 is the right part for that end** and it is already
  in the BOM on its own bus. And differencing two sensors multiplies any gain
  mismatch by the *common-mode* load of ~40 g, so a 1% mismatch is 0.40 g against a
  3 g signal — **per-unit calibration of both parts becomes a correctness
  requirement**, not a refinement, and the separation has to be recorded in the file
  alongside F15's rotation matrix.
- **Calibration becomes per-enclosure, not just per-unit.** The same module in the
  two carriers sits at a different position and orientation relative to the racket,
  so the coefficients differ and **the recording has to say which carrier it was
  in**. That is a data-format item and it sharpens §4.1 #2.
- **"Replace the butt" is ambiguous and the answer changes the volume by 2×.**
  Cap-only is ~3 cm³ with no bore dependency; cap-plus-a-short-plug is ~6 cm³ and
  reintroduces a mild one — far milder than a 100 mm cartridge, but C1 comes back
  in a small way. Decide which, and say so.

The sequencing benefit is the real prize. **It takes the mounting decision off the
critical path.** The schematic, the firmware, the format and the openness work all
proceed against one module while the carriers stay open, and the easy enclosure is
the one a stranger reproduces (§4.5) while the nice one is the author's own racket.
That is the minimum-viable-reproduction path this review asked for and did not get.

**Why they stopped, and the distinction that matters.** Discontinuation is evidence
about a market, not proof about physics — but the pattern here is specific enough
to be evidence about *this* project. Five products died within 18 months of each
other, 2020–2021, and the shared cause was not that the sensing failed. It was that
**their value depended entirely on a company maintaining a server**. When the
commercial case went, the hardware was intact and useless: no firmware update, no
local mode, no third-party app restores any of them, and users who had not exported
before the shutdown lost everything. That is the failure mode the openness requirement exists to prevent,
and it is the strongest available argument for the whole open goal — an argument
this design, as written, would not have survived either.

The second lesson is the one the project is most exposed to. Separate "the sensing
did not work" from "the feedback was not worth the friction" from "the business
failed". Only the first is a hardware finding; the evidence says these mostly died
of the second and third. A device whose feedback loop is "collect a season of raw
IMU and train a model" has *more* friction than the ones that died, not less — and
its user is one person who is also the author.

**Sample rate.** Published racket-sport classification accuracies are high at
ordinary rates: ~97% for six stroke actions and ~98% for tennis shot detection,
with 96% on stroke type, from wrist and forearm IMUs; a sampling-rate study puts
sufficient rates at 100 Hz for walking, 200 Hz for running and 400 Hz for
high-speed cyclic movement. **So the secondary goal is over-specified at 1 kHz by
roughly 5×**, and the 800 Hz–1 kHz+ requirement exists for the disguise question
alone — which F1 says is not currently measurable. That makes F1 load-bearing for
the entire specification, not just for one goal.

*Source caution:* the consolidated shutdown dates come from a vendor blog
(Auratide) that sells a competing sensor and has an interest in rivals being dead.
Sony's own regional store pages independently show the product discontinued, and
the Babolat and Sony dates corroborate across sources. Treat the specific dates as
good and the framing as interested.

---

## 6. Are these the sensors, for this form factor?

### What two accelerometers actually buy — the calculation

The hypothesis is that two accelerometers separated along the handle constrain the
instantaneous axis of rotation and let the device answer its own question without a
body-worn reference. **It does not, and the algebra says why.** With separation
`s` along the handle axis x̂:

```
Δa = ω(ω·s) − s ω²x̂ + α × s
Δa_x = −s(ω_y² + ω_z²)        determined by the gyro — no new information
Δa_y = s(ω_x ω_y + α_z)
Δa_z = s(ω_x ω_z − α_y)
```

Two accelerometers give **angular acceleration α directly, without differentiating
the gyro** — and that is genuinely useful, because differentiating a noisy 1 kHz
gyro is exactly where a "when did they diverge" feature would lose its signal. It
is measurable: at s = 50 mm and α = 500 rad/s², the term is 2.55 g against a
gyro-determined cross term of 7.7 g. At s = 80 mm, 4.08 g. Against the ADXL375's
own noise floor it is comfortable but not lavish — 5 mg/√Hz over a bandwidth of
ODR/2 is ~100 mg RMS at 800 Hz, so the α term sits about 25× above the noise, and
about 12× above it if the part is run at 3,200 Hz. That is another reason F4's
800 Hz recommendation is the right one: it is also the setting that makes this
measurement work.

What it does **not** give is the swing pivot. The instantaneous centre of rotation
is defined by the *velocity* field, and accelerometers see the acceleration field;
recovering velocity means integrating, which drifts. Two accelerometers do not fix
that, three would not either. **The design's stated limitation stands and should
not be quietly dropped** — separating wrist from forearm from shoulder needs a
body-worn reference, and the watch already is one.

One free consequence worth noting: the design already has two accelerometers
(ICM-45686 and ADXL375) and gains this for the price of **separating them along the
handle instead of placing them next to each other**. Given F2, separating them also
means the two see different centripetal loads, and the one nearer the butt clips
later — which is the right way round, because the far one is the ±200 g part.

**Separation is the thing the insertable plug buys that a butt puck cannot.** A
compact puck gives ~20 mm and an α term of 1.02 g at α = 500 rad/s²; a 70 mm sled
gives ~60 mm and 3.06 g; a 100 mm sled, ~90 mm and 4.59 g. The signal is linear in
separation, so this is the one capability that argues for going into the bore at
all. §5 works the trade through.

### Ranked

**Must be in rev 1:**

1. **A 6-axis IMU with ≥±40 g accel and ≥±4000 dps gyro.** F2 makes the range a
   correctness requirement, not a preference. The ICM-45686's ±32 g is the finding;
   the part is otherwise excellent and its documentation is fully public.
2. **A second accelerometer, separated along the handle axis by as much of the
   cartridge length as fits.** Already in the BOM; costs a placement decision.
3. **Die temperature and battery voltage.** Already specified, correctly, and both
   are unreconstructible after the fact.
4. **Cell temperature (NTC).** Not a data-quality channel — a safety one. See F11.

**Footprint but do not populate:**

5. **A MEMS microphone.** This is the strongest addition on the list and the
   evidence is direct: published work fuses wearable audio and IMU for shot
   detection in racquet sports, reporting ~95.6% fused accuracy in table tennis,
   and the argument is exactly the one this repository already has — `shot.rs`
   is uncalibrated, and `SquashLab`'s own protocol comment says the drop "may not
   be DETECTED at all: it is the soft shot the gyro threshold is most likely to
   miss". A microphone gives an impact timestamp that does not depend on a gyro
   threshold, which is also the alignment anchor F1 says the primary metric lacks.
   It plausibly separates clean from frame hits and may carry wall and floor
   bounces for rally structure.
   **Footprint rather than populate, for two reasons.** Power and volume are real
   but small; the blocker is that a microphone inside a sealed carbon tube, under a
   grip, hears mostly structure-borne noise — the acoustic path is the thing to
   measure before committing. And: **a microphone in a device carried into a club
   is a privacy question**, and an open design has to answer it in the open. The
   answer that works is that the firmware never stores audio, only a derived
   impact timestamp and an energy value, and that this is verifiable because the
   firmware source is published. Write that down before adding the part.
6. **A magnetometer.** Without one, racket *face angle in the world frame* is
   unrecoverable over a rally, and face angle is arguably the most coachable
   variable in any racket sport. The honest state of the evidence:
   - A magnetometer a few mm from carbon, a cell and a switching load is a hard-iron
     and soft-iron problem, and hard-iron offset from co-rotating material is
     *calibratable*; the part that is not calibratable is time-varying field from
     the switching load, which is why it would need to be characterised on a real
     board, not argued about.
   - This repository's `feat/magprobe` branch is the relevant prior art and it
     **has not been run**. Its README says so outright: "It has never been run on
     the watch. Every verdict below is a thing it is built to answer, not a thing
     it has answered." The watch's own magnetometer is tagged LIKELY, string-only,
     and something answers at I2C4/0x14 whose `CHIP_ID` did not match, so the part
     may not even be a BMM350. The machinery MagProbe builds — unit bands,
     rotation-invariance, dip angle, hard-iron sweep — is exactly the right test
     battery for this question, and running it is the cheapest way to learn
     anything about magnetometers in this project.
   - What is lost if it is genuinely impossible: absolute face angle. Relative
     face-angle *change* within one swing survives on the gyro alone, and that may
     be enough for the drive-vs-drop question, which is a comparison not an
     absolute.
7. **Grip sensing (capacitive or force, under the grip).** Two arguments, one
   strong. The weak one is that grip pressure and grip-change timing are coaching
   variables and a grip change before a soft shot is a disguise tell — plausible,
   unevidenced. The strong one is **free auto start/stop**: wake-on-motion cannot
   distinguish a racket in a bag on a bus from a racket being played with, and
   season-long usage logging is a stated secondary goal that wake-on-motion alone
   will pollute. A capacitive sense under the grip answers "is a hand on this"
   with no ambiguity and near-zero current.

**A different product:**

8. **Impact-location sensing.** Babolat Play claimed it with piezo elements in the
   handle and showed a string-bed heat map. The published state of the art is
   candid that "robustly recovering impact location from such signals in tennis,
   especially under realistic noise conditions, remains an open problem", and it is
   not a disguise variable. Leave it out.

### What a handle device can never measure

Ball speed. Shot length and accuracy. Where the ball landed, and whether it was
tight. Court position and T-recovery. The opponent — where they were, what they
were committed to, whether the shot worked. Whether it was the *right* shot.

All of those need a camera, a court instrument, or a second person's hardware, and
they are the variables a coaching product is actually made of. **A handle sensor
measures how you swung, never what happened.** Anyone who later expects this device
to produce coaching advice will be disappointed for reasons no firmware can fix.

### The system

The watch already records HR, wrist IMU and labels through `RecorderKit`, and
`Squash` and `SquashLab` are the other half of this. The fusion buys two things
neither gives alone: **the wrist is the body-worn reference** the design says it
needs to separate wrist from forearm from shoulder (the calculation above), and the watch's HR
and labels put the racket's kinematics in a physiological and semantic context that
the racket cannot see. That raises the priority of the time-sync mechanism (4.1
#3) from a nicety to the thing that makes the pair worth more than the parts.

---

## 7. Part compatibility, ordered by what blocks a schematic

1. **No regulator; every rail is over-voltage.** nRF52832 recommended 1.7–3.6 V,
   **absolute maximum 3.9 V**; ICM-45686 1.71–3.6 V; ADXL375 2.0–3.6 V; Li-Po
   4.2 V. Blocks the schematic. See F3.
2. **The module does not carry the 32.768 kHz crystal.** Raytac supplies a
   footprint and a recommended spec for an external one. The firmware requirement
   that names it therefore needs a board part that is not in the BOM.
3. **Flash is undersized by 4–8× and NOR is the wrong technology.** See F4. A
   W25Q256 holds 14 minutes of the stated payload; a 128 MB NOR takes 25 minutes
   typical / 218 minutes worst case to pre-erase.
4. **BQ29700's thresholds are set by the companion FETs' RDS(on), not by the IC.**
   The detection is a fixed voltage across the external FETs — the datasheet's own
   worked example is 14.3 mΩ × 4.5 A = 65 mV. So "confirm exact part against the
   companion table" is the wrong framing: **the FET choice *is* the overcurrent
   threshold**, and with low-RDS(on) FETs on a cell of a few hundred mAh, the trip
   current can land at tens of C — protection that never operates. Pick the FETs
   from the desired trip current backwards.
5. **MCP73831 has thermal regulation on its own die and no battery temperature
   input.** Junction rise ~30 °C at 100 mA; cavity bulk rise 0.7–3.8 °C. The IC is
   protected; the cell is not. See F11.
6. **ADXL375 is also an LGA.** 3.00 × 5.00 × 0.80 mm, 14-terminal. The cost claim that "the
   ICM-45686's LGA package cannot be hand-soldered" is true and incomplete — there
   are two such parts, and the BQ29700 is WSON, which the shopping list already
   notes. Three reflow-only parts, not one.
7. **ADXL375 numbers, verified against the datasheet** (Rev. B, April 2014 — the
   document lists these as unresolved). 145 µA in measurement mode at any ODR
   ≥ 100 Hz, so it does **not** scale with rate; 0.1 µA standby. VS 2.0–3.6 V,
   VDD I/O 1.7 V to VS, both absolute-max 3.9 V. 20.5 LSB/g, 49 mg/LSB,
   **specified only for ODR ≤ 800 Hz**, and only to ±10% (44–54 mg/LSB). Noise
   5 mg/√Hz, bandwidth = ODR/2. Maximum ODR 3200 Hz. **Maximum SPI clock 5 MHz**,
   CPOL = CPHA = 1, and the datasheet recommends ≥2 MHz for the 1600/3200 Hz rates
   — so this part cannot share a bus with the flash at speed, which is what forces
   the three-SPIM allocation rather than merely suiting it. ODR ladder 400/800/
   1600/3200 Hz, **no 1 kHz step**. Current is not monotonic in rate: Table 6 gives
   140 µA at 800 Hz, **90 µA at 1600 Hz** and 145 µA at 3200 Hz, so the choice
   between 800 and 1600 is not a power decision — it is a specification one, and
   800 Hz is the last rate the datasheet specifies a scale factor for. Survives 10,000 g
   powered and unpowered, which is the one thing about it that is comfortably
   over-specified for a racket. The 3200 Hz figure is what reconciles the
   110–140 MB/h claim (F4) — and F4 and F13 are why it should not be used.
8. **ICM-45686 numbers:** 1.71–3.6 V, LGA-14, 2.5 × 3.0 × **0.81** mm — the document
   says 0.76, and 0.81 is what TDK's own EVB guide prints under the pinout figure — 8 KB FIFO, 0.42 mA low-noise / 0.22 mA low-power 6-axis. Full
   register map public in AN-000478 — the NDA risk the design flagged does not
   exist.
9. **MDBT42Q is 10 × 16 × 2.2 mm.** Against the ~18–24 mm usable bore estimated in
   C1, **16 mm is most of the span before a liner**. This is the specific place
   where C1 being wrong stops being an inconvenience.
10. **Eval boards — the shopping-list trap, checked.**
    - `EV_ICM-45686`: **settled, from AN-000484, the board's own user guide**
      (Rev 1.1). AN-000483, the EV_ICM-45605 guide, describes the same board with
      the other die fitted — TDK states "the same PCB fab may be used for
      TDK-InvenSense other motion sensors" — and the two documents' connector,
      jumper and host-interface sections are identical word for word. The Host
      Interface Options section is explicit: "sensor data can be read using the jump wires or by soldering the
      required pins from CN1 to the external host CPU", and the board "can be
      directly plugged in via CN1 to a TDK InvenSense SmartMotion Host Interface
      board DK-UNIVERSAL-I, **ordered separately**". CN1 is a plain 10×2 0.1"
      right-angle header, not a proprietary satellite connector. **So the shopping
      list is not a trap here either**: the controller board is needed only for
      TDK's own MotionLink software, and the plan already buys an nRF52-DK.
      Two things to know before wiring it, both of which would cost an evening:
      **CN1 pin 19 expects 5 V**, and **JP1/JP2 default to 1.8 V** for VDD and
      VDDIO (selectable 1.8/3.0 V and 1.2/1.8/3.0 V respectively). Straight out of
      the box the logic is 1.8 V, so a 3.3 V host drives it out of spec — move JP2
      to 3.0 V and set the DK's own VDD to match.
    - `EVAL-ADXL375Z`: **settled from the datasheet's own ordering guide.** ADI
      sells three: `EVAL-ADXL375Z` ("Evaluation Board"), `EVAL-ADXL375Z-M`
      ("Inertial Sensor Evaluation System, Includes ADXL375 Satellite") and
      `EVAL-ADXL375Z-S` ("ADXL375 Satellite, Standalone"). The shopping list names
      the plain board, which carries no processor — an external host and
      user-supplied firmware are required — and that is the **right** choice here,
      because the plan already buys an nRF52-DK to drive it. **The shopping list is
      not a trap for this part.** It would be if anyone expected to plug it in and
      see numbers; `-M` is the one with the controller and software.
11. **MCP73831 absolute maximum VDD is 7 V** — it is the one part in the BOM that
    tolerates the raw cell, which is consistent with it being upstream of the
    regulator that does not yet exist.
12. **SPIM instances.** The nRF52832 has three, and the design allocates one per
    IMU plus one for flash — exactly three, with none left. Add the wake interrupts
    and the ADC divider and it still closes, but if F5's USB or SWD-over-cradle
    lands, or a microphone (PDM) is added, the pin and peripheral budget needs
    redoing on the nRF52840 rather than patched on the '832.

---

## 8. A revised plan, ordered by cost to kill

**Phase −1 — the free experiments, this week.** No purchase.

- **A. Putty in your own racket's butt — but how much, and how hard you test it,
  now depends on the architecture.** §5 compares balance shift rather than mass
  ratio and finds that the prior art **already largely answers the carrier case**:
  5 g net is 2.3× the just-noticeable difference against a shipped band of 1.2–1.7×,
  a modest extrapolation from five products that shipped and two that still sell.
  The internal cartridge is 6.6–10.3×, which nothing has ever shipped at. So:
  - **Butt puck or insertable plug (~6–9 g):** a confirmation, not a study. One
    short session, mostly subjective, checking that nothing is obviously wrong.
    Running the full protocol below on this is over-engineering.
  - **Internal cartridge (15–25 g):** the full protocol below. This is the case the
    prior art does not reach and the one most likely to end the project.

  This means **the architecture decision now gates the mass test rather than the
  other way round.** Do G0 first (§5, one module and two carriers); if it lands on a
  carrier, item A shrinks to a confirmation and stops being the project's biggest
  open risk.
- **A2. A dummy puck on the butt, same session.** Tape a block the size and mass of
  a clip-on sensor (~8 g) to the butt cap and play. Together with A this brackets
  the decision: A is the cartridge's 20 g, A2 is what either carrier actually
  weighs, and §5 says the prior art already covers the second.
- **B. Drive-vs-drop separability on the wrist. This is now the gate.** Run
  `SquashLab`'s `DRIVE DROP` protocol, train on wrist epochs, report held-out
  accuracy. While disguise was the goal this measured how much headroom the racket
  had to buy; with disguise abandoned it measures whether there is anything to buy.
  If the wrist already separates drive from drop at 90%+, the racket's case rests on
  the measured clipping and on the corpus rather than on classification — which is
  still a case, but a different and smaller one. One court session, no hardware, and
  it should precede any board order.
- **C. Write down the disguise metric and its validation protocol.** If the
  occlusion study in F1 cannot be specified, the primary goal is not a goal and the
  device is a shot-type classifier — which the wrist may already do (B).
- **D. Run MagProbe on the watch.** It exists, it has never been run, and it is the
  cheapest evidence available about magnetometers in this project.

#### A protocol for item A, for the case the prior art does not cover

This is sized for the internal cartridge at 15–25 g. For a ~5 g carrier, §5 says the
shipped record covers most of the question and a short subjective session is
proportionate; run this only if that session raises something.

**Do not test whether you can feel it.** You can: 39 mm is 7.8× the just-noticeable
difference, and a detection test will return "yes, it feels different" and decide
nothing. The questions that decide something are *does it make me play worse* and
*does that go away*.

1. **Write down the threshold first.** "I would accept this if my length accuracy
   drops by less than X." Without that number the result is unfalsifiable, and with
   it the test can be sized. This is the same discipline F1 asks of the disguise
   metric.

2. **Know what one session can see.** Detecting a drop in a hit rate, α = 0.05,
   power 0.80, from a 70% baseline:

   | drop to detect | shots per condition | total |
   |---|---|---|
   | 5 points | 1,374 | 2,748 |
   | 10 points | 354 | 708 |
   | 15 points | 160 | 320 |
   | 20 points | 91 | 182 |

   A solid solo session is 200–400 shots in total. **One session powers a 15–20
   point effect and nothing subtler**; a 5-point effect needs about ten sessions.
   That is fine, because a 5-point effect would not change the decision and a
   20-point one would — but it has to be said before the test, not after a null
   result.

3. **Count outcomes, not kinematics.** Straight-drive length into a target (behind
   the short line, within a metre of the side wall) is countable, and drop accuracy
   into the front quarter is countable. Also record the longest run of consecutive
   good drives: it is more sensitive per shot (p = 0.70 → 0.65 moves the expected
   longest run in 200 shots from 11.5 to 9.9) but it has a long tail, so use it as
   corroboration and not as the decision.

4. **Alternate, do not block.** ABBA or randomised blocks of 20–30 shots, same
   drill throughout. All-unweighted-then-all-weighted confounds the condition with
   warm-up and fatigue, which over a session are larger than the effect being
   measured.

5. **The watch is a secondary instrument here, and it is easy to misuse.** A heavier,
   more head-light racket changes wrist kinematics *mechanically*, so a change in
   the IMU signal is expected and is not evidence of degradation — reading it that
   way turns the objective measure back into a detection test. What the watch is
   genuinely good for is two things: a **compliance check** (did the blocks actually
   contain similar shot counts and intensity — `EffortKit`'s shot detector and epoch
   features already compute this), and **compensation** — if outcome holds constant
   but `gyro_max` or the saturation fraction moves, you kept the accuracy by working
   differently, which is a real finding and a different one from "no effect".

6. **Rate each block before you look at the count.** One number, fixed scale, written
   down immediately. Collected that way it is data; collected afterwards it is a
   rationalisation of the score.

7. **Test adaptation, because it is probably the deciding variable.** Repeat the
   session a week later having played with the weight fitted in between. A device
   you adapt to in a week is acceptable; one you do not is not, and a single session
   cannot tell those apart.

8. **Run it sequentially, not factorially.** Start at 20 g, the internal cartridge's
   figure. If that passes, the whole design space is open and you are done. If it
   fails, retest at **5 g** — F14's net figure for the butt-cap carrier — because
   that decides whether the carrier choice rescues the project or the mass does not
   work at any size.

**One honest limitation.** There is no control that varies balance alone. Holding
mass and balance constant while moving only the balance point is impossible:
+20 g arranged to preserve a 370 mm balance point costs **+17 to +31 kg·cm² of
swingweight** on a ~130 kg·cm² base, which is a bigger change than the one under
test. So A-versus-B measures the package — mass, balance and swingweight together —
and that is the right thing to measure, because that is what you would actually be
playing with.

**Phase 0 — one dead racket.** ~CAD $20–40 on a marketplace listing, handle intact.

- **E. Measure the bore.** C1. Cut the handle open. Record interior cross-section
  at 0, 20, 50 and 100 mm from the butt, wall thickness, and whether there is foam
  or a pallet. Every dimension below depends on this and it costs a hacksaw.
- **F. Weigh it, find its balance point, then repeat A with the real mass.**

**Phase 1 — decide the architecture.** No purchase, one afternoon.

- **G0. Do not pick a mounting position — design one module for two carriers.**
  §5 costs it: 1.24 cm³ of module plus cell fits both a butt puck and an insertable
  plug, and both land at 4–7% of frame mass against the cartridge's 10–17%. The
  puck is the reproducible one and has no RF question; the plug is net-lighter
  (it removes the cap it replaces), carries the two-accelerometer baseline, and
  needs the radio in the cap with only the cell and the outboard sensor down the
  sled. **Phase 2 is needed for the plug and not for the puck**, which is the
  cleanest reason to build the puck first. What it costs is a recording that has to
  name which carrier it came from, and its separation.
- **G. Cradle-carries-bulk, or not.** F5. If yes: nRF52840, USB, mass storage, and
  the openness requirement is satisfied by construction. If no: a GATT service, a
  tool somebody has to maintain, and the openness requirement is at permanent
  risk. This decision gates the module, the pin budget, the flash part and the
  licence discussion.
- **H. Settle the recording configuration.** The ADXL375's ODR sets the payload,
  which sets the flash technology, which sets the cost (F4). Pick it deliberately
  and write the number down.

**Phase 2 — RF, narrowed.** The eval boards, now with a specific question.

- **I. Find the depth cliff**, not the attenuation. F7 predicts the link survives at
  the mouth with ~17 dB to spare at 3 m and dies between 20 and 50 mm. Measure where
  it actually falls over, with cap on and off and hand on and off. The output is a
  mechanical constraint on the cap, not a go/no-go.

**Phase 3 — schematic.** Only after F3 (regulator), F4 (flash), item 4 (FET-set
protection thresholds) and F11 (cell temperature) have answers.

**Phase 4 — loose-form bring-up board.** As the document has it. Good phase, keep it.

**Phase 5 — cartridge board, liner, cap.** As the document has it, plus the liner
as a parametric model with the Phase 0 measurements as inputs.

The one structural change, now qualified: **the external butt-cap puck should be
costed against the internal cartridge at Phase 1** — but the first thing to cost is
whether it is legal at all, because §5 finds a protruding one outside the 686 mm
limit and a flush replacement cap short of the volume. Measure the butt cap while
the Phase 0 racket is already apart. §5 says every
shipped product chose it and the reasons the document rejects grip wraps do not
apply to it.

---

## 9. What could not be settled here

| Open | What would settle it |
|---|---|
| **C1 — the actual bore.** Everything numeric downstream is an estimate from grip circumference. | A hacksaw and a dead racket. Phase 0 item E. |
| **Whether the mass and balance are acceptable.** The arithmetic says 6–9× the JND; whether *you* mind is not a calculable quantity. | Phase −1 item A. |
| **Real CFRP attenuation at this geometry.** Published SE figures are for flat laminates in a waveguide fixture, not for a small tapered tube with a hand on it. | Phase 2 item I. |
| **ICM-45686 FIFO packet sizes and timestamp resolution**, and **whether its AUX I²C master can put external-sensor data into that FIFO** (F13). The 16/20-byte assumption is corroborated by the ICM-42670-P's own table (F4) but not confirmed for this part. These are now the only open questions a document can answer. | Read AN-000478 — §FIFO and the AUX/I²C-master section. |
| **Whether the racket gyro actually rails at ±4000 dps.** F2 gives two independent estimates that bracket it; neither is a racket-mounted measurement. | Any ±4000 dps IMU taped to a racket for one session. This is cheap enough to be Phase −1 if a spare IMU exists. |
| **Whether a microphone hears the ball through a sealed carbon tube.** The acoustic path, not the microphone, is the unknown. | A microphone taped inside a handle and one knock-up. |
| **The achievable per-unit calibration accuracy**, and therefore whether the mechanical key is worth its complexity (F8). | Calibrate one unit twice with a reinsertion between, and report the difference. |
| **How long the sled should be.** §5 shows separation buys α signal linearly and costs ~0.03–0.04 g/mm, but the right point on that curve depends on whether the α measurement earns its place at all, which F1 has not settled. | Decide after F1; the trade is computed and the answer is cheap either way. |
| **Racketware's mass, data model and whether it exposes raw samples.** The mount is now known; the rest is not, and it is the only squash-specific prior art. | Buy one, or ask them. |

---

## 10. Sources

Repository: `SquashSensor/README.md` @ 0638eb6; `Squash/README.md`;
`RecorderKit/README.md`; `EffortKit/src/shot.rs`, `src/epoch.rs`, `src/bin/phase_a.rs`;
`SquashLab/protocols.json`; `MagProbe/README.md` @ ccbd6f5;
`Squash/Tests/pulled/20260913-v0.6.0-70min-match` and `20260916-v0.6.103-2s-and-3s`.

External, all accessed 2026-09-17. Vendor spec-sheet claims, independent
measurements and forum posts are distinguished in the text.

- Squash forehand kinematics, racket head velocity 30.8 m/s and segment
  contributions — *J. Sports Sci.* 2020, doi:10.1080/02640414.2020.1747828 (paywalled;
  figures via abstract and secondary reporting).
- World Squash racket specification (maximum length 686 mm, maximum weight 255 g)
  and the Player Analysis Technology clause — worldsquash.sport. Production squash
  rackets at 685 mm — retail listings. Compared against the ITF Rules of Tennis
  maximum overall length of 29 in (73.66 cm) versus a 27 in (68.58 cm) standard
  frame — itftennis.com. The two headroom figures, ~1 mm and ~51 mm, are what §5
  turns on.
- nRF52832 Product Specification, absolute maximum VDD 3.9 V; Nordic DevZone,
  ~1,400 kbit/s application throughput at 2M PHY with DLE.
- **ADXL375 data sheet, Analog Devices, Rev. B (April 2014), read in full.** Table 1
  (sensitivity 20.5 LSB/g and 49 mg/LSB, both for ODR ≤ 800 Hz only; noise
  5 mg/√Hz; 145 µA at ODR ≥ 100 Hz; VS 2.0–3.6 V; ODR ≤ 3200 Hz; bandwidth = ODR/2),
  Table 2 (VS and VDD I/O absolute maximum 3.9 V; 10,000 g powered and unpowered),
  the FIFO Buffer section (32 levels, no timestamp), the SPI section (5 MHz maximum,
  ≥2 MHz recommended for 1600/3200 Hz), Data Formatting at Output Data Rates of
  3200 Hz and 1600 Hz (LSB always 0), Offset Calibration (1.56 g/LSB, volatile), the
  outline drawing (3.00 × 5.00 × 0.80 mm) and the Ordering Guide (`EVAL-ADXL375Z`,
  `-M`, `-S`).
- ICM-45686 — TDK DS-000577 and AN-000478 user guide (public, neither read here);
  distributor data for 1.71–3.6 V, LGA-14 2.5×3.0×0.81 mm, 8 KB FIFO, 0.42/0.22 mA.
- **ICM-42670-P data sheet, TDK InvenSense, DS-000451 Rev 1.2 (April 2026)** — read
  for the family FIFO packet convention only (Figure 9; `FIFO_COUNT_FORMAT`). It is
  a *different* part: ±16 g and ±2000 dps, which F2 and C6 disqualify twice over.
- **AN-000484, TDK-InvenSense EV_ICM-45686 EVB User Guide, Rev 1.1**, with
  **AN-000483** (the EV_ICM-45605 guide, 25 Sep 2024) read alongside it as
  corroboration — the two are identical through the connector, jumper and
  host-interface sections. Read for: Usage (dual UI + AUX interface, 8 KB FIFO,
  20,000 g shock), Board Overview (2.5 × 3.0 × 0.81 mm LGA), Connector and Jumpers
  (CN1 pinout with 5 V on pin 19; JP1/JP2 defaulting to 1.8 V) and Host Interface
  Options (standalone via jump wires or soldered CN1; DK-UNIVERSAL-I ordered
  separately). Neither document covers FIFO packet formats.
- Raytac MDBT42Q datasheet — 10 × 16 × 2.2 mm, external 32.768 kHz crystal footprint.
- MCP73831/2 datasheet (DS20001984H) — 7 V absolute maximum, die-temperature thermal
  regulation, no THERM pin. bq2970 datasheet — FET-referenced overcurrent thresholds.
- W25Q256JV datasheet — 256 B pages, 3 ms max page program, 133 MHz; W25Q family to
  2 Gbit.
- CFRP shielding effectiveness at 2–2.7 GHz — *Materials Today Communications* 2022;
  *Materials* (PMC7070600) 73.8 dB at 2.3 GHz for a filled layered composite.
- Audio + IMU shot detection in racquet sports — arXiv:1805.05456.
- IMU sampling-rate sufficiency — PMC11991382.
- Product data: tennis-technology.com (Babolat Play sensor 29.9 g); Sony AU/NZ store
  and UK support (SSE-TN1 8 g, service ended 30 Sep 2021); gadgetsandwearables
  (Zepp Tennis 2, 6.25 g, 25.4 × 12.3 mm, >1000 Hz, dual accel + dual gyro);
  arccosgolf.com (7.34 g / 4.40 g, Gen 4 Jan 2025, subscription); Garmin / retail
  (Approach CT10, 9 g, CR2032, ~4 years, no subscription); Diamond Kinetics
  (SwingTracker ~14.2 g, 1600 Hz); thesquashsite.com (Racketware).
- Shutdown chronology — auratidecollective.com, a vendor selling a competing sensor;
  dates corroborated for Sony and Babolat elsewhere. Treat the framing as interested.
- ProtoCentral Move Ultralight, CERN-OHL-P 2.0 + Apache 2.0 — hackster.io, Apr 2026.
- OSHWA certification requirements — certification.oshwa.org.
