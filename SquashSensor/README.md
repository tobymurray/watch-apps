# SquashSensor — a racket-embedded IMU for pre-divergence shot-disguise kinematics

A hardware complement to `Squash` and `SquashLab`, not a replacement for either.
Those record and label from the wrist; this exists because the wrist is the
wrong place to answer one specific question: **how similar is a drive's and a
drop shot's approach before they diverge** — the kinematic signature of shot
disguise. That signal lives closer to the hand and racket than to the wrist,
and it clips on the wrist sensor before it can be measured anyway.

## Why not the wrist

`Squash/README.md` already measured the limitation this project exists to fix.
Over 1,800 epochs of real match play on the BMI270 (±8g accel, ±2000dps gyro,
100Hz), **13.8% of epochs had an accelerometer axis railed and 3.9% a
gyroscope axis**, one epoch at 22% of samples railed. And structurally: "the
watch is on the wrist, not the racquet — head speed is a proxy at best."
Disguise detection needs exactly the kinematics that recording can't give:
un-clipped, racket-frame, high enough rate to catch a wrist-snap burst.

## Goal

- **Primary**: raw racket-frame swing kinematics for ML, specifically
  quantifying how similar a drive's and drop shot's approach are before they
  diverge.
- **Secondary**: shot-type classification, usage-ratio logging over a season.
- **Explicit limitation, not a defect**: a single IMU fixed to the racket
  measures the racket's net motion only. It cannot separate "the wrist did
  something different" from "the forearm or shoulder did" — both produce the
  same racket-frame signal. Isolating wrist contribution specifically would
  need a body-worn reference IMU, out of scope here.

## Mechanical architecture

**This is an internal cartridge that slides into the hollow carbon handle
through the open butt end. It is not, and was never meant to be, a wrap
mounted around the grip exterior** — an early pass at this design assumed the
latter and got the mechanical, RF, and grip-feel analysis wrong as a result.
If a rebuild of this section starts from "wraps around the outside," that
assumption is wrong; every finding below depends on the cartridge sliding
inside the bore.

- **Envelope**: ~25×32mm interior cross-section, carbon fiber, hollow the
  full handle length, accessible only from the butt. Assumed zero-taper for
  at least 100mm — some racquets taper before that, deliberately not chased;
  those are out-of-compatibility exceptions, not a design driver.
- **Fit**: a custom 3D-printed liner per racquet takes up bore tolerance
  (foam/TPU standoffs), not a bespoke rigid shell. The rigid cartridge needs
  defined attachment points (bosses, ribs) for the liner to key into.
- **Orientation repeatability**: a keying rib (blocks rotational ambiguity)
  and a positive depth stop (fixes axial position) on the cartridge. Needed
  because reinsertion is part of normal operation once charging doesn't
  require full cartridge removal (below) — without both, the accelerometer's
  effective lever-arm from the swing pivot drifts between sessions, which
  quietly corrupts the cross-session comparability this whole device exists
  to produce.
- **Per-unit axis calibration is still required on top of the mechanical
  key** — 3D-printed and hand-assembly tolerance is not precise enough on its
  own for an ML application sensitive to subtle wrist-angle differences.
  Calibrate each build against a known reference plane before use.
- **Charging**: flat, low-profile contact **pads** on the outward face of the
  (rarely-removed) butt cap — not spring pins on the racket. Spring pins live
  on the charging cradle instead, where they don't take impact. The butt of a
  racket gets tapped on the floor and takes hits on dives; a spring pin
  proud of that surface is a bent contact waiting to happen. Recess the pads
  in a non-conductive channel and gold-flash them — exposed always-live
  contacts a few mm apart are a short hazard in a gym bag (keys, coins), and
  sweat runoff collects at the butt end.
- **Inductive charging: considered, rejected.** A receive coil sized usefully
  for this bore sits within a few mm of the conductive tube wall around its
  *entire* circumference, not just along the tube's length — unlike the
  antenna, there's no "move it to the mouth" escape from this, because the
  opening is the same conductive-walled bore. That risks eddy-current heating
  in the load-bearing composite itself, repeated every charge cycle, and
  commercial Qi transmitters will likely false-trigger foreign-object
  detection on the tube. The contact-pad design above already solves the
  actual goal (no cartridge removal to charge) with well-understood physics;
  inductive would trade that for an unverified thermal risk in the racket's
  own structure. Revisit only if a real thermal measurement on an actual
  tube section says otherwise.
- **No potting; conformal coating + foam cushioning instead.** Because
  charging doesn't require removing the cartridge, it stays genuinely
  serviceable (battery replacement over the product's life is realistic)
  rather than sealed shut — a better outcome for "sealed cell inside a struck
  object" than potting would have forced.
- **Mass estimate: ~15–25g added**, dominated by the battery (~6–8g) and the
  printed liner (~6–10g) — a rough volumetric estimate, not measured. Against
  a ~110–140g competitive frame that's roughly 10–20% of total mass. Placement
  near the butt/pivot keeps the swingweight (moment of inertia) impact small,
  but it does shift the balance point toward the handle (more head-light) —
  a real, felt effect that needs a physical mockup to confirm is acceptable,
  not just a paper calculation.
- **Butt-end placement is a scientific fit, not just where the void happens
  to be**: the signal that predicts disguise (subtle hand/wrist cues before
  the head diverges) originates closer to the hand than to the racket head, so
  the mechanically-forced location happens to be well-suited to the actual
  research question.

## RF — the largest open technical risk

Modeling the bore as a rectangular waveguide (broad dimension a = 32mm):
cutoff **fc = c/(2a) ≈ 4.69GHz**. Operating at 2.4GHz is below that cutoff,
giving on the order of **70+ dB of theoretical attenuation over the 100mm
length** if the tube were an ideal conductor. Real carbon composite is
lossier and leakier than ideal, so the true number is lower — but the
direction is unambiguous: an antenna buried deep in this tube will not have a
usable link, independent of module or antenna tuning.

- **Mitigation**: orient the BLE module so its own vendor-tested antenna edge
  faces the cap/mouth, and place that end of the board closest to the
  opening. Do **not** relocate the antenna onto a separate flex/coax
  sub-board — a pre-certified module's modular grant is conditioned on the
  antenna configuration it was tested with, and a custom remote antenna
  generally voids that, pushing you back into full intentional-radiator
  certification regardless of module choice. Confirm the specific module
  vendor's grant conditions before assuming this is settled.
- **This must be bench-validated before any board commitment** — see Phase 0
  below. The ideal-waveguide number is a bound, not a prediction of the real
  material's behavior.
- **Flash fallback is mandatory, not optional**, given the residual link
  uncertainty — but it must be sized for at least one full worst-case
  session (~110–140MB/hour at the combined dual-IMU rate), not treated as a
  small dropout ring buffer. A generic small SPI NOR flash chip is
  undersized for this; pick capacity deliberately.
- **Full-board EMI context**: the whole assembly sits a few mm from a lossy
  conductive tube on all sides, not just near the antenna — treat it as an
  unintended shielded cavity generally (grounding, decoupling), not only an
  antenna-keepout problem.

## Electronics

| Function | Part | Why |
|---|---|---|
| Primary IMU | ICM-45686 (TDK InvenSense) | ±2–32g accel, ±4000°/s gyro, ODR to 6.4kHz, 2.5×3.0×0.76mm LGA. Run accel at ±32g — no ±24g step exists on this part, and the zero-clipping requirement argues for exceeding rather than settling for ±16g. Verify per-range noise density against the full datasheet table before finalizing. |
| High-g accel | ADXL375 (Analog Devices) | ±200g, dedicated bus/CS so impact transients don't cost swing-phase resolution. Exact current draw/package height unverified this session — confirm from the datasheet directly. |
| MCU/Radio | nRF52832, as a pre-certified module (e.g. Raytac MDBT42Q) for rev 1 | See platform-choice rationale below. Three SPIM/DMA instances available — put each IMU on its own bus. |
| Flash | SPI NOR, sized for one full session | Mandatory BLE-dropout fallback, not a nice-to-have — see RF section. |
| Charger | MCP73831 (SOT-23-5) | Small linear Li-Po charger, programmable current via Rprog — set only once a final cell capacity is known. |
| Protection | BQ29700 (WSON-6) + companion dual N-FET + PTC fuse | Overcharge/overdischarge/overcurrent/short-circuit — MCP73831 alone doesn't provide this. Source the cell with integrated PCM as an additional independent layer if available. |
| Battery | Small pouch cell, sized against final cartridge length split | Interior volume (25×32×100mm) makes this easy compared to an exterior-wrap design; get a real capacity quote once electronics length is fixed. |

### Platform: Nordic over ESP32

Chosen despite the author having far more ESP32 experience, on the strength
of the standby power gap specifically: nRF52832/840 achieve sub-1µA System
OFF sleep current versus ~7–10µA for classic ESP32 and ~5µA datasheet (often
higher in practice) for ESP32-C3 — a 5–10x gap that dominates a device
spending days in standby between short active sessions. Active BLE
advertising current favors Nordic by a wider margin still (Nordic: tens of
µA typical; ESP32-C3: ~0.8mA average). ESP32's dual-core architecture would
solve the BLE-stack timing-jitter risk noted below, but only the
power-hungry dual-core variants have two cores — the power-competitive
single-core variants (C3/C6) share nRF52832's single-core contention risk
without Nordic's power advantage. If jitter isolation proves necessary after
bench testing, Nordic's own dual-core nRF5340 is the better answer, not a
family switch. Real cost accepted: nRF Connect SDK (Zephyr-based) is a
steeper on-ramp than ESP-IDF/Arduino.

## Firmware-adjacent hardware requirements

- **Timestamps must derive from each IMU's own ODR/FIFO clock**, not from
  MCU interrupts serviced under the BLE SoftDevice — a single-core
  nRF52832's radio ISR can preempt application code by tens to hundreds of
  µs around connection events, and this is a timing-sensitive measurement by
  definition (the whole point is *when* drive and drop diverge).
- **Wake-on-motion**: the IMU's motion interrupt must be wired to a
  wake-capable GPIO. Season-long usage-ratio logging can't depend on the
  player remembering to start/stop recording manually.
- **External 32.768kHz crystal for the RTC** — needed for usage-log
  timestamps to stay meaningful across weeks between phone syncs; the
  internal RC oscillator drifts too much for that timescale.
- **Battery voltage sense (ADC divider) and the IMU's built-in die
  temperature sensor** — both cheap to wire in now, both needed later for
  data-quality flags (brown-out artifacts, gyro bias temperature drift) that
  can't be reconstructed after the fact if not logged at capture time.
- **Firmware low-battery threshold above the protection IC's hard cutoff**
  (e.g. graceful session-end/flash-flush around 3.4V) — without this, a
  session that runs the battery down risks a corrupted trailing record right
  as the hard cutoff hits.

## Requirement compliance

| Requirement | Status |
|---|---|
| Accel ≥16g, ideally 24g | Exceeded (±32g) — no ±24g step exists on the chosen part |
| Gyro ≥2000dps, ideally 4000dps | Met |
| Separate high-g impact channel | Met |
| 800Hz–1kHz+ sustained | Met with hardware margin; timing architecture (above) still needs firmware validation |
| LiPo charge + full protection | Met, pending final cell/PCM sourcing |
| Physical envelope | Met — generous interior volume, zero grip-feel impact |
| BLE data path | **At risk** — antenna-at-mouth mitigation proposed, unverified until bench-measured; flash fallback is the actual safety net |
| Mounting survives impact/vibration, connector fretting | Met — no in-racket connectors, conformal coating + foam cushioning |
| Orientation/placement consistency | Addressed by keying rib + depth stop + per-unit calibration — none of the three is optional |

## Cost and weight (estimates, not quotes)

- **Added mass**: ~15–25g, dominated by battery and liner.
- **BOM, low quantity**: ~$25–40/unit in components.
- **All-in, prototype run (5–10 units)**: ~$70–120/unit, dominated by
  assembly setup fees (the ICM-45686's LGA package cannot be hand-soldered —
  budget for paid reflow assembly, not an iron).
- **All-in, 50–100 unit run**: ~$35–55/unit as setup costs amortize.

## Open, unresolved — named so they can be checked, not assumed away

- **Real RF attenuation through the actual carbon layup** — the waveguide
  number is a bound; only a bench measurement on real material resolves it.
- **Whether the chosen module vendor's modular grant permits the final
  antenna configuration** — check before assuming certification is solved by
  module choice alone.
- **ADXL375 exact current draw and package height** — unverified this
  session, confirm from the datasheet.
- **Achievable battery capacity in the final cartridge geometry** — get a
  real quote once electronics length is fixed.
- **ICM-45686 FIFO/timestamp register-level behavior** — needed to confirm
  the jitter-free timestamping architecture above is actually achievable as
  described.
- **Whether the ~15–25g mass addition and head-light balance shift are
  acceptable to a competitive player** — no amount of analysis substitutes
  for a physical mockup here.

## Execution plan

### Phase 0 — RF proxy test and confident-subsystem bring-up (now, in parallel)

De-risk the one large unknown (RF) and the well-understood subsystems
(sensors, charging) at the same time, using off-the-shelf hardware — no
custom PCB yet.

**RF proxy test:**
1. Sacrificial racket — a cracked/damaged frame from a local squash club or
   marketplace listing; condition of the frame/strings doesn't matter, only
   the handle's carbon construction does.
2. 3D-print a crude positioning jig (not the final liner) that places a
   module eval board's antenna at several depths: flush at the mouth, ~20mm,
   ~50mm, fully buried near 90–100mm.
3. Measure RSSI/packet loss at a fixed distance via a BLE scanner app, with
   cap on/off and hand gripped/ungripped, to isolate whether the tube, the
   cap, or the hand dominates the loss.
4. Treat exact dB numbers as approximate (the eval board's antenna is tuned
   for the vendor's reference ground plane, not the final board) — the
   directional trend (depth matters this much, hand vs. tube contribution)
   is what this test is for.

**Shopping list (Digi-Key Canada — confirmed in stock at time of writing):**

| Item | Part | Qty | Purpose |
|---|---|---|---|
| BLE module eval board | Raytac MDBT42Q-DB-32 | 1 | RF proxy test — exact module + antenna under consideration |
| BLE dev kit | Nordic NRF52-DK | 1 | Second RF reference point; onboard J-Link also programs the Raytac board via debug-out (check exact pinout in the DK user guide first) |
| Primary IMU eval board | TDK EV-ICM-45686 | 1 | Sensor bring-up — no custom breakout needed for this stage |
| High-g accel eval board | Analog Devices EVAL-ADXL375Z | 1 | Sensor bring-up |
| Charger IC | MCP73831T-2ACI/OT | 5–10 | Cheap, hand-solderable (real leads) — expect losses during first SMD attempts |
| Protection IC | BQ29700DSER | 5–10 | Cheap but WSON (reflow-only) — buy ahead of the batched breakout order |
| Companion dual N-FET | *confirm exact part against BQ29700 datasheet's companion table before ordering* | 5–10 | Switches charge/discharge path on a protection fault |
| PTC fuse, Rprog resistor, passives | *pick after sizing final charge current* | 5–10 each | Don't lock in values before a final cell capacity is known |
| Bench-test battery | any small 3.7V LiPo, JST-PH | 1 | Not the final cell — any source is fine for this stage |

Software: free — nRF Connect for Mobile for RSSI/packet-loss logging.

### Phase 1 — KiCad schematic capture, confident subsystems only

IMU and charge/protection reference schematics — mostly transcribing each
datasheet's application circuit. Pull verified footprints (SnapEDA has a
listing for the ICM-45686) and cross-check against the datasheet's own land
pattern before trusting a third-party symbol, especially for the fine-pitch
LGA/WSON parts. Nothing RF-dependent gets designed in this phase.

### Phase 2 — Resolve RF placement and certification path

Complete the proxy test; confirm module orientation and whether the chosen
vendor's modular grant permits the final antenna configuration as tested.
Nothing in Phase 3 or 4 should start before this phase has an answer.

### Phase 3 — Loose-form-factor bring-up board

One integrated board, same schematic as the final design, laid out at a
comfortable size rather than the tight 25×32mm cartridge cross-section —
catches footprint/schematic mistakes cheaply before committing to the final
tight layout.

### Phase 4 — Final cartridge-form-factor board, liner, and cap

Tight layout respecting the antenna-at-mouth orientation from Phase 2;
per-racquet 3D-printed liner with keying rib and depth stop; removable cap
with charging contact pads (not spring pins) on its outward face.
