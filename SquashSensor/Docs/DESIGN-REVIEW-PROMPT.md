# Take SquashSensor apart before anyone spends money on it

[`SquashSensor/README.md`](../README.md) proposes a racket-handle-embedded IMU
cartridge: dual IMU, nRF52832 module, SPI NOR fallback, contact-pad charging in
the butt cap, and a five-phase plan that ends at a board. It was produced in a
single session, and one of its own premises — that the device wraps around the
outside of the grip — survived an entire design pass before being found wrong
and corrected in place.

**Your job is not to improve the document.** It is to work out whether the thing
it describes should be built at all, and if so whether *this* is it. Five
passes, in this order, because each one can kill the ones after it:

1. Adversarially review the design and the execution plan.
2. Establish whether the design permits the open outcome §1 requires — it was
   not written with that requirement and may not survive it.
3. Ground every decision in what other sports have already shipped and what
   happened to those products and their users' data.
4. Establish whether these are the sensors a state-of-the-art feedback product
   needs, **within this form factor** — a handle cartridge, accepting that
   sensors elsewhere (wrist, body, court, camera) would offer more.
5. First-pass part-compatibility review of the BOM and the shopping list.

The verdict "build it as described" is available and may be right. So is "the
cheapest experiment in this document kills it, run that first."

---

## 0. The honesty contract, which outranks everything below

`CLAUDE.md`'s standing rule:

> **Prefer measurement over assertion.** If you are choosing a number, measure
> it and record what you measured.

A finding of the form "the RF risk seems high" is worth nothing. One of the form
"fc = c/2a puts 2.4 GHz 2.3 GHz below cutoff, and here are three published
measurements of carbon-composite shielding effectiveness at 2.4 GHz" is worth
something. Where you cannot measure, say you are asserting, and say what would
settle it.

**Do not trust the document's framing, and be harsher for two reasons.** It was
written with a model, in one session, about hardware the author wants to exist —
the author is a squash player and the intended user, which is a conflict of
interest pointing at "build it." And a model reviewing it is reviewing its own
family's output; the failure mode is agreeing with the voice.

**Some of this requires looking outside the repository.** Tasks 2, 3 and 5
cannot be done from the source tree. Use the web. Cite what you find with dates,
and keep a hard line between a vendor's spec-sheet claim, an independent
measurement, and a forum post.

---

## 1. The two requirements the document does not contain

These are constraints on the answer, not preferences to be traded away. The
design under review was written before they were stated, so parts of it are
likely to fail them. Say which.

**Openness of the data.** The wearer owns the recording. Anyone must be able to
take every raw sample off the device themselves, in a documented format, with no
account, no cloud, no pairing secret held only by one app, and ideally no
special software at all. A co-developed app interprets that data; it is one
consumer of a published format, never the gate in front of it. This repository
already made exactly this call once — `Squash/README.md`: "It writes no activity
file and exports nothing… the app is the instrument that collects the data a
squash metric would be built from" — and records raw sensor LSB unscaled "so the
recording keeps the saturation rather than hiding it behind a conversion."
Hold the racket device to the same standard, and say where the design as written
would fall short of it.

**Open hardware.** The intended end state is a design someone else can read,
build, modify and repair: schematics, editable layout, BOM, mechanical CAD,
firmware and calibration procedure published, under a licence that says so.
The repository is MIT today. This is a real engineering constraint, not a
publishing decision made at the end — it can disqualify a part whose register
map is under NDA, a stack that depends on a proprietary binary blob, and an
assembly process nobody else can reproduce.

Neither requirement appears anywhere in the design under review. Treat that
absence as unpriced, not as settled.

---

## 2. The claims everything else rests on

The document's conclusions are not independent. These carry the rest:

| # | Claim | Stated basis |
|---|---|---|
| C1 | The bore is ~25×32 mm interior, hollow, zero-taper for ≥100 mm | Assumed. No racket was measured. |
| C2 | 2.4 GHz will not propagate out of the bore | `fc = c/2a ≈ 4.69 GHz`, ideal-conductor waveguide |
| C3 | ~15–25 g added is plausibly acceptable | Volumetric estimate, explicitly unmeasured |
| C4 | Nordic beats ESP32 on standby current, and standby dominates | Datasheet sleep currents, no duty-cycle model |
| C5 | Racket-frame kinematics can quantify shot disguise | The wrist clips and is the wrong frame |
| C6 | ±32 g / ±4000 dps clears the zero-clipping requirement | Wrist railed at ±8 g / ±2000 dps |

**Check C1 first.** It is the one measurement that costs a tape measure and a
dead racket, and the conclusions it supports are not the ones you would guess.
Note which way each dependent conclusion moves if the bore is smaller than
assumed: `fc` scales as `1/a`, so C2 gets *stronger*; battery volume, "generous
interior volume, zero grip-feel impact", the mass estimate, and the claim that
a bring-up board can be relaxed to a comfortable size all get weaker or
collapse. Say which of the document's conclusions survive C1 being wrong.

---

## 3. Settled, or deliberately out — do not re-derive these

- **The wrist saturates during real play.** 1,800 epochs of
  `Squash/Tests/pulled/20260913-v0.6.0-70min-match`: 13.8% of epochs with a
  railed accelerometer axis, 3.9% with a railed gyroscope axis, one epoch at 22%
  of its samples. Re-derive with `cargo run --features std --bin phase-a`, do
  not re-argue.
- **100 Hz on the wrist cannot resolve the ball-on-strings transient.**
  `Squash/README.md` states it as arithmetic, not as a measurement.
- **The exterior grip wrap is dead.** The document says so and says why. It is
  not, however, the only alternative to an internal cartridge — see §6.
- **`EffortKit`'s shot detector is uncalibrated by design.** `src/shot.rs`
  records the threshold sweep over the 183-second labelled warm-up and states
  that only a counted drill settles it.
- **`SquashLab` already owns drill protocols and the shot-kind wire format.**
  `SquashLab/protocols.json`: `16 + side*8 + type`, 32+ unassigned. Anything
  this device records should be readable by what exists, not a second format.
- Out of scope entirely: ball tracking, court position, anything needing a
  camera or a second person's hardware — except where §7 needs them to say
  honestly what the handle can never give.

---

## 4. Task one — adversarially review the design and the plan

Attack the design, not the prose. Candidate lines of attack, none of which are
findings until you have done the work:

**The scientific premise (C5).** The stated primary goal is quantifying how
similar a drive's and a drop's approach are before they diverge. Ask what the
computed metric actually is, on what data, aligned how — two swings have to be
time-aligned before "before they diverge" means anything, and impact time is the
only obvious anchor, which is the end of the swing, not the start. Then ask what
validates it: disguise is a perceptual property of what an opponent can predict,
and no racket signal is ground truth for that. If there is a protocol that
produces a human-prediction label, name it; if there is not, the ML target is
unfalsifiable and that is a design finding, not a research detail.

**Drilled shots may not contain the phenomenon.** A player hitting 45 drops in a
row is not disguising anything. `SquashLab`'s protocols produce clean labels for
shot *type*; the disguise question needs shots chosen under uncertainty. Say
whether the corpus this device would collect contains what it exists to measure.

**The plan has no kill-shot experiment and its cheapest test is missing.**
Phase 0 de-risks RF. But the document's own unresolved list says the mass and
balance question needs a physical mockup, and that mockup is 20 g of putty in a
butt cap and one session on court — cheaper than the RF test, decisive, and
nowhere in the five phases. The sacrificial racket Phase 0 already buys is the
same racket that answers C1 and C3. Say what the correct ordering is if the
cheapest experiment is also the most likely to end the project.

**No phase tests the hypothesis.** Phases 0–4 end at a board. Nothing in the
plan records data and checks whether racket-frame kinematics separate a drive
from a drop better than the wrist already does. Some of that is answerable today
on `Squash/Tests/pulled` and `SquashLab` drills. Say how much headroom the
racket sensor has to buy, and whether that is known before or after Phase 4.

**BLE-as-fallback may be inverted.** Work out the session payload and the
achievable BLE throughput on this module, and say whether flash is the fallback
or the primary path. Then ask what the charging contacts could be doing: the
cartridge already terminates in a pad array on the butt cap that docks in a
cradle. Two more pads is SWD or USB. If bulk transfer happens over the cradle,
RF stops being the largest open risk in the document, the platform argument in
C4 changes shape, and — see §5 — the openness requirement gets much easier to
satisfy.

**C4's arithmetic is not shown.** 9 µA of standby delta over a week is ~1.5 mAh.
Put that against the cell this device actually needs and the duty cycle it
actually has — days in a bag, wake-on-motion in a bag that gets jostled, active
advertising between sessions — and say whether standby dominates anything. If it
does not, the platform choice needs a different argument or a different answer.

**The self-consistency checks.** The requirement line says 800 Hz–1 kHz+. The RF
section says one worst-case session is 110–140 MB/hour at the combined dual-IMU
rate. Six axes plus three at 2 bytes each is 18 B/ms, or ~65 MB/hour at 1 kHz —
so the stated figure implies ~1.7–2.2 kHz, or per-sample overhead the document
never mentions. Find which, and audit the rest of the numbers the same way.

**C6 may not clear what it claims.** The wrist railed at ±2000 dps. Bound the
racket's angular rate from squash head speeds and swing geometry, and from what
the existing wrist recordings imply, and say whether ±4000 dps rails too. "No
clipping" is the requirement the whole device exists to satisfy; if the new part
also clips, say at what rate and whether that is acceptable.

**Reinsertion, and whether it happens at all.** Orientation repeatability is
solved with a keying rib and a depth stop *because* the cartridge is expected to
come out. If charging no longer requires removal, ask how often it is really
reinserted, and whether per-unit calibration plus a mechanical key is over- or
under-engineered for the true frequency.

Also review: the certification claim (whose obligation, for how many units, sold
or not — and see §5, which changes it), the thermal path for a linear charger
inside a sealed tube, whether squash's governing rules permit a modified racket
in sanctioned play, and whether a cracked frame's carbon layup is representative
of an intact one.

---

## 5. Task two — does this design permit the open outcome?

§1 is the requirement. This task prices it against the design as written.

### 5.1 Getting the raw data off

- **Define "all the raw data".** Per-sensor samples in raw LSB, each sensor's own
  clock, the session's configuration, the per-unit calibration coefficients, and
  the data-quality channels the design already specifies (die temperature,
  battery voltage). A recording that has been scaled, fused or filtered on the
  device has thrown away what the wearer owns; say what the device is allowed to
  do to a sample before writing it.
- **Say how a user gets it with no special software.** A custom BLE GATT service
  needs a tool somebody wrote. USB mass storage needs none, on every OS, for
  ever. That makes USB support a requirement question, not a throughput
  convenience — and the chosen nRF52832 has no USB peripheral while the nRF52840
  does. Price that difference, including against the charging cradle that already
  exists in the mechanical design.
- **Name the format and where its specification lives.** `RecorderKit` and
  `Squash` already write three CSV files on one shared clock, byte-compatible
  with the SDK simulator's playback parser. Say whether the racket device should
  join that format, extend it, or define a documented binary one — and what the
  session payload size does to that choice.
- **Time sync is part of the data contract.** A racket sample and a wrist sample
  are useless together unless they can be placed on one timeline. Say what the
  mechanism is, and that it is recorded in the file rather than assumed.
- **No hostages.** No account, no cloud, no server-side calibration table, no
  pairing key that only one app holds, nothing that stops working when a
  repository goes quiet. Check the design for anything that would.

### 5.2 Open hardware, concretely

- **Recommend a licence set** — hardware, firmware, documentation — with the
  reasoning, not a list of options. The repository is MIT today; say whether that
  is the right answer for the hardware too or whether a hardware-specific licence
  earns its complexity here.
- **Say what must be published to meet the OSHWA definition**, and check the
  design can produce each: schematic, editable layout (KiCad is already the
  plan), BOM with orderable part numbers, mechanical CAD for the liner and cap in
  an editable format rather than only STL, firmware source, and the per-unit
  calibration procedure with whatever jig it needs.
- **Find the parts that cannot be open.** A part whose full datasheet or register
  map is behind an NDA makes the firmware underivable by anyone else, however
  good the silicon. The design already flags "verify per-range noise density
  against the full datasheet table" for the primary IMU — establish whether that
  table is public. Do this for every part, and treat a failure as disqualifying
  rather than inconvenient.
- **The proprietary-stack question.** Nordic's SoftDevice is a binary blob.
  Zephyr ships an open-source Bluetooth controller for nRF5x and ESP-IDF ships
  NimBLE under a permissive licence — so an open path exists on either platform,
  but it is a different stack from the one the design implies, with different
  timing behaviour. That interacts directly with the document's own concern about
  radio ISRs preempting application code, and with C4. Resolve it.
- **Reproducibility by a stranger.** The design contains an LGA that cannot be
  hand-soldered, a liner that needs a bore nobody else has measured, and a
  per-unit axis calibration. Each is a barrier to "clone the repository and build
  one." Say what the minimum viable reproduction path is — assembly-house BOM and
  placement files, a liner parameterised by measured bore dimensions, a printable
  calibration jig — and which parts of the current design make it harder than it
  needs to be.
- **Availability is part of openness.** A design that depends on a part nobody
  can buy in two years is not reproducible. Check lifecycle status, second
  sources, and whether the parts exist in the catalogues of the assembly services
  a third party would realistically use.
- **Certification, redistributed.** If others build from the files, who is the
  manufacturer? A pre-certified module's modular grant attaches to a product as
  marketed; a design published as files, a kit, and a sold unit are three
  different regulatory objects. One paragraph, correct, and say whether it is a
  reason to keep the module even at a power cost.
- **Serviceability is already half-solved.** No potting, a removable cell, charge
  without disassembly. Say what else the design would have to keep to stay
  repairable, and what it currently gets wrong.

### 5.3 The app, and what it is not

The interpreting app is a consumer of the format, co-developed with it. State the
contract: the format is specified independently, a third-party reader is a
first-class citizen, and nothing the app computes is unavailable to someone with
the files. Say where that app should live, under what licence, and what — if
anything — it is allowed to know that the format does not carry.

---

## 6. Task three — ground it in what other sports already shipped

This is a solved product category in four other sports, mostly by companies that
no longer sell the product. Find out what they built, what it weighed, where
they put it, what happened to it, and **what happened to their users' data when
it ended** — that last one is evidence for §1, and it is the strongest available
argument for the whole open goal.

Start here, and do not stop here:

- **Tennis** — Babolat Play (sensors integrated into the handle at manufacture;
  ITF Player Analysis Technology approval), Sony Smart Tennis Sensor, Zepp
  Tennis / Tennis 2, Head Tennis Sensor, QLIPP.
- **Golf** — Arccos (one sensor per club, in the grip cap, through a hollow
  graphite shaft — the closest structural analogue to this design's RF problem),
  Blast Motion, Garmin Approach CT10, Shot Scope.
- **Baseball / cricket** — Blast Motion, Zepp, Diamond Kinetics SwingTracker,
  StanceBeam Striker, Str8bat — all knob- or handle-end mounted.
- **Badminton and table tennis** — the closest sports by stroke mechanics and by
  the wrist-snap question this project cares about.
- **Squash itself** — find out what has actually been tried, including
  camera-based and court-instrumented systems, and why the category is thin.
- **Anything open** — implement sensors, or sports sensors generally, published
  as open hardware. If the answer is "almost nothing", that is itself a finding
  about what this project would be.

For each, get the numbers that matter here: **added mass, mount location, sample
rate, sensor ranges, whether data leaves live or after the session, whether raw
data was ever available to the owner, and battery/charging model.** Then answer
the questions that only this comparison can:

- **Every shipped implement sensor found the butt end from the outside.** This
  design goes inside the tube and inherits an RF problem, a thermal problem, a
  servicing problem, and a per-racket liner. What did the outside cost them that
  justified going in — and is the external butt-cap puck, which the document
  never considers as distinct from the rejected grip wrap, the actual answer?
- **Mass.** Tennis and baseball sensors cluster near 6–11 g on 280–340 g and
  850–900 g implements. This proposes 15–25 g on a 110–145 g frame. Get the real
  numbers and state the ratio; if this is several times worse than anything a
  competitive player has accepted in any sport, that is the finding.
- **What sample rate and range did shipped products actually use**, and what did
  the academic racket-sport IMU literature find sufficient for shot
  classification? If 100–200 Hz classifies shots at 90%, this design is
  over-specified for its secondary goal and the 1 kHz+ requirement exists only
  for the disguise question — which makes that question load-bearing for the
  whole spec.
- **Why did they stop selling them?** Discontinuation is evidence. Separate "the
  sensing did not work" from "the feedback was not worth the friction" from "the
  business failed", because only the first is a hardware finding and the second
  is the one this project is most exposed to.
- **What did owners lose?** Retired apps, dead cloud services, stranded
  hardware, exported data or none. Say plainly what an open design would have
  changed in each case, and what that implies for §5's format decision.

---

## 7. Task four — are these the sensors, for the form factor?

Take "state of the art feedback" to mean: the best that a handle-resident device
could give a player, in 2026, about their squash. Not what this BOM gives.

Work the question in both directions.

**What the design has, and whether it is enough.** Two accelerometers and one
gyroscope, all at one point on the handle axis.

- **No magnetometer.** Heading drifts without one, so racket *face angle in the
  world frame* is unrecoverable over a rally — arguably the single most coachable
  variable in any racket sport. Establish whether a magnetometer is usable a few
  mm from carbon, a cell and a switching load, whether the court environment
  permits it, and what is lost if it is genuinely impossible. This repository has
  a `feat/magprobe` branch; find out what it measured.
- **One IMU cannot locate the pivot; two can.** The document declares "cannot
  separate wrist from forearm or shoulder contribution" an explicit limitation
  requiring a body-worn reference. Two accelerometers at a known separation along
  the handle see different centripetal and tangential terms and constrain the
  instantaneous axis of rotation. Establish whether the achievable separation
  inside a handle gives enough signal above noise to matter — this is a real
  calculation, not a hand-wave — because if it does, the device can answer its
  own primary question without a second body-worn unit.
- **No microphone.** Ball-on-strings is a loud, sharp, unambiguous event, and the
  IMU shot detector this repository already wrote is uncalibrated and most likely
  to miss exactly the soft drop the project cares about. A MEMS mic gives an
  impact timestamp that does not depend on a gyro threshold, plausibly separates
  clean from frame hits, and may carry wall and floor bounces for rally
  structure. Cost it in power, volume and firmware, and say whether it belongs.
  Note that a microphone in a device worn into a club is a privacy question as
  well as an engineering one, and an open design has to answer it in the open.
- **No impact-location sensing.** Babolat Play claimed it with piezo elements in
  the handle. Find out how well it worked before recommending it.
- **No grip sensing.** Grip pressure and grip-change timing are coaching
  variables, and a grip change before a soft shot is a disguise tell. A
  capacitive or force sense under the grip also gives free auto start/stop that
  wake-on-motion cannot — a racket in a bag moves.
- Cheap and probably right: die temperature and battery voltage are already
  specified as data-quality flags. Say whether anything else belongs in that
  class.

**What the handle can never give, stated plainly.** Ball speed, length, accuracy,
T-recovery, opponent position, whether the shot was the right one. The honest
version of this section names the ceiling, so nobody later expects a handle
sensor to deliver a coaching product.

**The system, not the device.** The watch already records HR, wrist IMU and
labels through `RecorderKit`, and `Squash` and `SquashLab` are the other half of
this. §5.1 asks for the time-sync mechanism; this section asks what fusing the
two buys that neither gives alone, and whether that changes the sensor list.

Finish with a ranked list: sensors that must be in rev 1, sensors that should be
footprinted-but-unpopulated, and sensors that are a different product.

---

## 8. Task five — first-pass part compatibility

A schematic-level review of the BOM table and the Phase 0 shopping list. Every
finding names a datasheet page or an orderable part number. Carry §5.2's tests —
NDA'd documentation, lifecycle, assembly-house availability — through this list
rather than treating them separately.

Non-exhaustive; find what is not listed here:

- **Rails.** The BOM lists a charger, a protection IC, an MCU module, two sensors
  and a flash — and no regulator. Check every part's absolute-maximum supply
  against a Li-Po at 4.2 V, and say what the power tree actually has to be. If a
  regulator is needed, it changes quiescent current, which is the argument in C4.
- **Interfaces.** Maximum SPI clock per part, logic levels, whether the module's
  pin count and available SPIM instances actually support two sensor buses plus
  flash plus the wake interrupts plus the ADC divider — and, if §5.1 lands on
  USB or SWD over the cradle, those pins too.
- **Flash.** Size a session properly, then check what that capacity costs in SPI
  NOR, what it costs in SPI NAND, and what NAND costs in firmware (bad blocks,
  ECC, wear) that the plan does not budget. Check erase and program time against
  the session write rate, not just capacity. If the user is to mount this as a
  filesystem, say what that adds.
- **Charging.** A linear charger inside a sealed carbon tube dissipates into a
  cavity with no airflow. Compute it. Then check whether the chosen charger has
  thermal regulation and a battery-temperature input, and say what charging a
  sealed Li-Po in a struck sports object without cell temperature monitoring
  means — for the wearer, and for a design other people will build.
- **The protection IC and its companion FET.** Verify the pairing against the
  datasheet's companion table and check the fixed overcurrent thresholds against
  a cell this small.
- **The module.** Confirm whether it ships with the 32.768 kHz crystal the
  firmware requirements demand, its exact dimensions and antenna keepout against
  a board that has to fit the bore measured in C1, and its modular-grant
  conditions.
- **The eval boards.** Confirm every Phase 0 part number is orderable as written,
  and — this is the specific trap — whether the sensor eval boards are standalone
  or satellite boards requiring a vendor controller board and its software. A
  shopping list that is $100 and one board short per sensor is a plan that stalls
  on arrival.
- **Assembly reality.** Which packages are hand-solderable, which force paid
  reflow, whether the bring-up board can be populated incrementally, and whether
  a third party could have one made without bespoke tooling.

---

## 9. Traps

- **Do not redesign it.** Every section above asks for a judgement and its
  evidence. A better BOM is not the deliverable.
- **A datasheet number is not a measurement.** Sleep currents, shielding
  effectiveness and BLE throughput are all quoted under conditions this device
  does not have.
- **The waveguide number is a bound, not a prediction.** It is also robust: it
  gets stronger, not weaker, as the bore shrinks. Do not treat "real carbon is
  leaky" as a refutation without a number from real material.
- **Discontinuation is evidence about a market, not proof about physics.** Keep
  the two apart in Task 3.
- **Do not let the wrist's limitations argue for this specific device.** That the
  wrist clips is measured and true. That a racket cartridge is the answer is a
  separate claim with separate evidence.
- **Open is a property of the design, not of the repository it lands in.**
  Publishing files for a board nobody can source, assemble or reflash is not open
  hardware; say so when you see it.
- **"Verify from the datasheet" is not a finding.** The document already has a
  list of those. Findings are things you actually checked.
- **Weight is not the only felt change.** Balance point, swingweight and torsional
  feel are different quantities and a player notices different ones.

---

## 10. What to produce

1. **A verdict, in one paragraph**, on whether this should be built as described,
   built differently, or not built yet — and the single cheapest experiment that
   most changes the answer.
2. **The C1–C6 table, re-scored**, each claim marked verified, refuted, or still
   open, with what you checked.
3. **Findings from Task 1**, each with the evidence, and each marked as either
   fatal, expensive-if-late, or cosmetic.
4. **The openness verdict from Task 2**: what the raw-data path is, what the
   format is and where it is specified, the licence recommendation, and the list
   of things in the current design that fail §1 — separated into "fix in rev 1"
   and "changes the architecture".
5. **A comparison table from Task 3** — product, sport, mount, mass, rate,
   ranges, transfer model, raw-data availability, status — with sources and
   dates, and one paragraph on what this project should copy and what it should
   deliberately not.
6. **The ranked sensor list from Task 4**, plus the plain statement of what a
   handle device can never measure.
7. **A part-compatibility findings list from Task 5**, each naming a datasheet
   page or a part number, ordered by what blocks a schematic.
8. **A revised phase plan**, if the review changes it, ordered by cost-to-kill
   rather than by subsystem.
9. **What you could not settle**, and the measurement, purchase or session that
   would settle each.

Do not design a board. This is a review pass.

---

## 11. Out of scope

- Court-side or camera systems, except as §7's honest ceiling.
- Anything about the `Squash` or `SquashLab` watch apps except the parts this
  device must interoperate with — the marker format, the clock, `EffortKit`.
- The FIT file. Field numbers are frozen and a wrong one is silent.
- Firmware architecture beyond the hardware requirements it imposes and the
  stack-licensing question in §5.2.
- Business model and pricing. Whether anyone else would *build* one is in scope,
  under §5.2; whether anyone would buy one is not.

---

## 12. Where the evidence is

```sh
# The design under review, and the measurements it cites
SquashSensor/README.md
Squash/README.md                  # saturation, Nyquist, wrist-frame limits,
                                  # and the "the recording is the product" call

# Re-derive the saturation numbers rather than quoting them
cargo run --features std --bin phase-a -- Squash/Tests/pulled/*/imu_*.csv

# What already exists in software, and what it says is uncalibrated
EffortKit/src/shot.rs             # the threshold sweep and why it is not a default
EffortKit/src/epoch.rs            # the features the watch and the analysis share
SquashLab/protocols.json          # drill protocols and the shot-kind wire format
RecorderKit/README.md             # the recording format both apps write

# The labelled recordings that exist today
ls Squash/Tests/pulled/

# The repository's own prior art for a review of this shape
Docs/HR-READING-CONVERGENCE-PROMPT.md   Docs/HR-READING-CONVERGENCE.md
SleepLab/Docs/ADVERSARIAL-REVIEW.md

# And for an open-publication decision made with numbers
InscribedDisc/Docs/2026-09-10-publish-or-not.md
LICENSE                           # MIT, and what it already covers

# A magnetometer probe already exists on a branch
git log --oneline origin/feat/magprobe
```
