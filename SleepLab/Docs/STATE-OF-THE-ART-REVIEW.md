# What a better instrument would do with this night

A catalogue of experiments, answering
[`STATE-OF-THE-ART-PROMPT.md`](STATE-OF-THE-ART-PROMPT.md). It takes correctness
as given and asks whether this app computes the right things.

**Nothing here proposes a night for anything that could be answered without
one.** Five of the questions the prompt asks turned out to be answerable from
files already in this repository, and one of them — what 24-hour recording costs
— was answered in about four minutes. Those answers are in §1 because they
change the shape of everything after them.

Every citation is in §16 with a DOI or a stable URL, and the ones I could not
verify are marked there as unverified rather than left to look solid.

---

## 0. The short version

> **Revised 2026-08-20**, after commit `4a4866e` added two real SleepLab
> recordings — a worn night and a table hour. They changed the answer more than
> anything else in this document, and §1.7–§1.11 are what they said. The
> original findings, from the probe night, stand and are unchanged; they are
> renumbered below the new ones because the new ones outrank them.

### What the two recordings settled

**At the constants it ships with, this app cannot score a single minute of sleep
on this hardware.** Not "uncalibrated" — inoperative. Cole-Kripke's sleep region
ends at 273 counts per 60 s scoring epoch (the figure is in
`SleepWakeScorer.hpp`'s own comment). A watch lying still on a table measures
**357 at its quietest and 374 at its median**. The scorer's entire sleep region
is below the sensor's own noise floor, so **0 of 624 scoring epochs** of a full
night's sleep score as sleep, and neither do 0 of 53 table epochs. Driving the
real algorithm over the real night confirms it. §1.8.

**`kCountScale` is wrong by a factor of 11–18, not by a factor of two.** The
night becomes plausible in shape — 7–8½ h of sleep in a 10.4 h record — only at
**0.0003–0.0005** against the shipped 0.0055. My own card C-4 proposed sweeping
from 0.0005 upward; **the answer is at or below the bottom of that sweep**, and
the card is corrected. §1.8.

**No night has ever opened, and now everyone knows why.** `stillnessCountMax` is
60 and `activityCountMin` is 250, both far below a floor of 357, so every epoch
reads as "moving" and the segmenter can never open a session. A full night's
sleep produced a `watching.csv` and nothing else — which is exactly the failure
`POST-MORTEM.md` G2 predicted in writing, caught by the very file G2 added.
§1.7.

**The nightstand defence is now, measurably, heart rate and nothing else.** On
the table hour: `worn_pct` median **100 %** (TOUCH_DETECT confidently reports a
table as worn), every epoch below any workable stillness threshold, and
`kMicroMovementFloor` at 8 against a floor of 374 — a test that cannot fail.
Only one thing separated the two recordings cleanly: **1249/1249 worn rows carry
a heart-rate sample; 0/106 table rows do.** The README says the two halves of
the plausibility check are each defeatable alone. One of them is already
defeated. Raising `kMicroMovementFloor` from 8 to ~400 restores it. §1.9.

**The accelerometer is a Bosch BMI270 and its measured noise is 67× its
datasheet.** 160 µg/√Hz over a 3 Hz band is 0.28 mg RMS; the stationary watch
measures 18.6. Two consequences. The floor is not a component choice to regret —
it is a configuration the kernel holds and an app cannot reach, so it is a
firmware request with register names behind it. And **the old `kCountScale` was
about right for a sensor 67× quieter**: at datasheet noise the count floor would
be ~6 rather than 374, and the boundary at 0.0055 sat at 273 with the night's
real movement at ~322. It was not a careless guess. §1.12.

**And the tooling cannot read the recordings.** `night_report.py thresholds`
fails twice: its `skip` set discards a file named `watching.csv` even when named
explicitly on the command line, which its own docstring says is the supported
way to read a noise floor; and the recordings are schema 2 while the script
tracks schema 3. The two irreplaceable recordings that exist to set the movement
constants cannot be fed to the tool that exists to set them from. §1.11.

### What the probe night had already found

**Five things this review found before proposing a single night, and which the
new recordings do not disturb.**

1. **The 2026-08-19 night is not unscoreable.** Ledger row S13 and
   `POST-MORTEM.md` both say the best-referenced night this project will get was
   half-wasted because the probe records no activity counts. It records no
   activity counts. It records `motion_mot` — MOTION events per minute — and a
   ten-minute-quiet rule over that column puts sleep onset at **00:36–00:38**
   against a diary's 00:33 and a Fenix's 00:37, and final wake at **08:00**
   against a diary's 08:00 and a Fenix's 08:02. The same rule fires **zero
   times** across eight hours of daytime wear the same day. That is one night,
   one wearer, and a threshold chosen after seeing the answer — but it is a
   channel SleepLab already records (`motion`, `sig_motion`, schema 3) and
   nobody is using. §1.1, card **C-1**.

2. **24-hour recording costs about 13 % of the pack a day**, and this is
   arithmetic over `probe_log.csv`, not a proposal for a run. The same file
   holds a 4.00 h afternoon (1.25 mA), a 3.98 h evening (1.26 mA) and the 8.45 h
   night (1.18 mA). Round the clock at ~1.22 mA is **~29 mAh in 24 h against a
   216 mAh pack**, so ~6–7 days a charge. The charge schedule is not the
   obstacle the nap-detection question assumed it would be. §1.2, card **C-2**.

3. **Cole-Kripke's coefficients were fitted on single-axis counts and this app
   feeds them the vector magnitude.** The reference implementation the ledger
   checked against is explicit: it uses ActiGraph *axis 1*, transformed
   `min(axis1/100, 300)`. `kCountScale` is therefore bridging an
   axis-combination change as well as a unit scale, and the two are not the same
   kind of error — the second one depends on posture. `count_x/y/z` have been on
   disk since schema 3, so this is free to test. §1.3, card **A-1**.

4. **Cole et al. published three parameter sets and this app implements one.**
   The "30-second" set is not a 30 s scorer: it scores each minute from the
   **maximum** of that minute's two 30 s sub-epochs, weights
   (50, 30, 14, 28, 121, 8, 50) at P = 0.0001. SleepLab records 30 s epochs. So a
   second published variant is computable from every night already recorded, and
   two published variants disagreeing on the same file is a calibration signal
   nobody is collecting. §1.4, card **A-2**.

5. **20 Hz is exactly the published floor for RMSSD, and only with
   interpolation.** Béres & Hejjel report RMSSD within 5 % relative error at
   50 Hz undecimated, or at 50 ms (20 Hz) *with* interpolation — in healthy young
   volunteers, on clean signal, awake. That makes the PPG recovery route neither
   obviously dead nor honest yet, and it makes the free public-dataset
   experiment (**D-2**) worth running before the three-minute hardware one
   (**E-1**) is allowed to raise anyone's hopes. Cards **D-2** and **E-1**, and §11.4.

**And one thing this review did not find:** any way for this project to confirm
the restfulness band. Not a hard way — no way. There is no PSG, and a Fenix is a
proprietary algorithm, not ground truth. The band can be *refuted* by an
experiment in this catalogue and can never be confirmed by one, so by the
prompt's own rule the recommendation is **deletion of the heart-rate half of it**
and demotion of the rest to what is measured. §9, card **H-BAND-1**; ledger rows A18 and A19.

**The catalogue costs 11 nights of sleep, 10 of them with a diary — and 8 of
those 10 were already committed by `ROLLOUT.md` phase 5.** The marginal cost of
everything in this document is **one night** (`hr=off`), one bedtime window with
the watch on a table (which costs data, not sleep), one ordinary day of wear, one
more table hour, and about nine minutes of standing still with the watch on.

**But the order has changed, and this is the practical headline.** Do not spend a
diary night until the four movement constants have been moved off the floor. A
night recorded at `stillnessCountMax = 60` opens no session, scores no epoch and
displays nothing — it is not *wasted*, because `watching.csv` keeps every count
and the diary comparison can be done entirely in `Tools/`, but the app will show
its wearer nothing for ten consecutive mornings and every number will have to be
recovered offline. The two recordings now on disk are enough to set all four
constants today. §9 is re-ordered accordingly.

---

## 1. What the files already on disk say

Four re-analyses, run while writing this, over
`SleepLab/Output/probe/probe_log_2026-08-19.csv`. They cost nothing and they are
reproducible from that file with the standard library.

### 1.1 The night that was supposed to be unscoreable

The file holds three long runs, not one:

| Run | Local (EDT) | Rows | What it is |
| --- | --- | --- | --- |
| 2 | 14:46 – 18:45 | 240 | afternoon, worn, 1 372 steps |
| 3 | 18:48 – 22:46 | 239 | evening, worn, 1 700 steps |
| 13 | 00:16 – 08:42 | 507 | **the night**, diary + Fenix alongside |

`motion_mot` (MOTION events classified as motion, per minute) separates them
without any threshold-fitting at all:

| | median/min | minutes with zero | total |
| --- | --- | --- | --- |
| afternoon | 2 | 28 / 240 | 438 |
| evening | 2 | 41 / 239 | 543 |
| **night** | **0** | **399 / 507** | **174** |

A deliberately crude detector — *the first run of ten consecutive minutes at or
below the threshold* for onset, *the end of the last such run* for final wake —
gives:

| Threshold | Onset | Final wake |
| --- | --- | --- |
| `motion_mot` ≤ 0 | 00:38 | 07:42 |
| `motion_mot` ≤ 1 | 00:36 | **08:00** |
| Diary | 00:33 | 08:00 |
| Fenix 6 Pro | 00:37 | 08:02 |

And the same rule on the two daytime blocks fires **0 times in 479 minutes**,
while at night it finds 10 quiet runs totalling 238 minutes.

Three honest qualifications, because this is the finding most likely to be
over-read:

- **The threshold was chosen after seeing the answer.** That is the failure the
  prompt names by name. Card **C-1** is the pre-registered version, and it is
  pre-registered against the *nine remaining* diary nights, not against this one.
- **It is fragile where it matters least and least where it matters most.**
  Moving the threshold by one event moves final wake by 18 minutes and onset by
  2. A metric whose most-quoted number moves 18 minutes on a one-unit threshold
  change is not ready to be printed.
- **The daytime blocks were unusually active** — 3 072 steps across eight hours.
  The adversary the prompt names, a still evening in front of a screen, is not in this
  sample. Zero false positives across an active afternoon is a much weaker
  result than it looks.

What it does establish, at no cost, is that **S13 as written is too strong**.
The night could not be scored by Cole-Kripke. It carries a movement channel that
reproduced a diary to within five minutes at both ends, and SleepLab records that
same channel in every epoch row.

### 1.2 What 24-hour recording costs

| Block | Hours | `batt_mah` drop | Mean current |
| --- | --- | --- | --- |
| afternoon | 4.00 | 5 | **1.25 mA** |
| evening | 3.98 | 5 | **1.26 mA** |
| night | 8.45 | 10 | **1.18 mA** |

Day and night cost the same. At 1.22 mA the whole app with continuous optical HR
costs **~29.3 mAh per 24 h**, which against the 216 mAh the gauge reported at
100 % is **~13.6 % of the pack a day** — call it **6–7 days a charge**.

`batt_mah` is integer, so a 4 h block is quantised to ±0.25 mA; the three blocks
agree to within that, which is why this is worth reporting as a measurement
rather than an extrapolation. It is the probe's channel set, not SleepLab's, and
SleepLab subscribes `BATTERY_METRICS` only with `diagnostics` on — so the
confirming run (**C-2b**) has to have it on.

**The nap-detection conclusion this forces:** the battery is not what stops 24-hour
recording. What stops it is that every charge kills the app (P8), so a
round-the-clock protocol has to nominate a fixed daytime charging slot and treat
it as a known hole, and the 90-minute gap between run 3 and run 13 in this very
file is what that hole looks like.

### 1.3 The axis mismatch

`actigraph.sleepr`'s source, which the ledger's A8 check used and which cites
p. 466 of Cole *et al.* 1992:

```r
actigraph_adjustment <- function(data) {
  data %>% mutate(count = pmin(.data$axis1 / 100, 300))
}
# The optimal parameters for the mean activity per minute.
# pg. 466, Sleep, Vol. 15, No. 5, 1992.
```

and its documentation: *"The Cole-Kripke algorithm uses the y-axis (axis 1)
counts."*

SleepLab feeds `count`, the vector magnitude of three per-axis integrals. The
README's defence of that choice is sound on its own terms — filtering `|a|` is
blind to rotation, and vector-magnitude counts are a real ActiGraph quantity —
but **the coefficients were not fitted on vector-magnitude counts**. Two true
sentences sitting next to each other imply a third that is false.

The consequence is specific and it is not "the scale is wrong":

- VM ≥ any single axis, always, so the bias has a known direction.
- The VM-to-axis-1 ratio is between 1 and √3 depending on the *direction* of
  movement relative to the device, so it varies with sleeping posture and turns
  over when the wearer does.
- `kCountScale` is a scalar. It can absorb the mean of that ratio. It cannot
  absorb its variance, and the variance is what a scorer with a threshold sees.

`count_x`, `count_y` and `count_z` have been in the epoch CSV since schema 3.
So this costs a rescore, not a night — and it turns A9's one-dimensional sweep
into a two-dimensional one over (axis combination × scale), which is exactly
what the rescore tool in §15 is for.

Also confirmed and worth writing down: the reference transform **saturates** at
300, which is A10, currently UNVERIFIED and deliberately not implemented on the
grounds that a ceiling derived from a guess is a second guess. That reasoning
stands, and the rescore tool makes the ceiling a swept parameter rather than a
guess.

### 1.4 The variant this app already has the data for

Three parameter sets, all cited to p. 466:

| Variant | P | Weights (−4 … +2) | What it is fed |
| --- | --- | --- | --- |
| 1-minute (implemented) | 0.001 | 106, 54, 58, 76, 230, 74, 67 | mean activity per minute |
| 30-second | 0.0001 | 50, 30, 14, 28, 121, 8, 50 | **max** 30 s sub-epoch per minute |
| 10-second | 0.00001 | 550, 378, 413, 699, 1736, 287, 309 | **max** 10 s sub-epoch per minute |

The second one is not a 30 s scorer. It still produces one verdict a minute; it
just summarises the minute by its worst half rather than its total. SleepLab's
`Epoch` is 30 s, so `max(count[2i], count[2i+1])` is available for every night
ever recorded, and the app already carries `peak` per epoch for exactly the
reason this variant exists — *"separates 'moved once, hard' from 'fidgeted
throughout', which sum to the same count"*.

This answers the prompt's question about scoring at 30 s, and the answer is the opposite of what it
expected: **there is no published route to scoring at 30 s**, but there is a
published second opinion computable at no cost, and the disagreement between the
two is a calibration diagnostic that needs no diary at all. When one variant
calls a minute wake and the other calls it sleep, the minute is near the
boundary; the fraction of such minutes is a direct measure of how badly
`kCountScale` is placed.

### 1.5 Heart rate, from the file rather than from a paper

| | Value |
| --- | --- |
| Daytime reference (runs 2 + 3, 479 min): p5 / median | 54.5 / 67.2 bpm |
| Night (507 min): p5 / median / minimum | 50.4 / 56.2 / **48.4** bpm |
| Dip, nadir against daytime p5 | **11.2 %** |
| Dip, night median against day median | **16.4 %** |
| Smoothed (15 min) nadir | 49.2 bpm at **00:47**, 31 min into the record |
| First-hour mean → last-hour mean | 54.1 → 65.0 bpm |
| Morning rise, last 90 min | **+15.8 bpm/hour** |
| `hr_trust_x10` median: day / night / final hour | 12–15 / **29** / 13 |

Three things follow, and one of them is uncomfortable.

**A daytime reference already exists on disk.** The prompt lists "the overnight dip as a
percentage of a daytime reference" under *costs recording*. It is not: eight
hours of same-day daytime heart rate is in the same file. Ledger row **S20**.

**The nadir landed 14 minutes after sleep onset and heart rate rose
monotonically for the rest of the night.** `index.csv` already stores
`hr_min_at_pct`; for this night it would read **6 %**. Whether that is this
person's physiology, an artefact of the nadir coinciding with settling down, or
an optical sensor still stabilising, one night cannot say — but it is enough to
say that **the timing-of-nadir metric should not be shipped on the strength of
the literature's expectation that it falls mid-night.** Card **C-3**.

**`hr_trust_x10` is not just a quality flag.** It is 29 through the still night
and 12–15 through both active daytime blocks, and it collapses to 13 in the hour
the wearer got up. It moves with activity, systematically, in a file where
nothing else about it is documented. The direction of the scale is unverified —
higher may mean better or worse — and settling that is a five-minute desk read of
the SDK header (card **A-6**), after which it is a free second input to the worn
gate, whose entire second half is *"was there a pulse"*.

### 1.6 Fragmentation, computed

From the binary motion channel (active = `motion_mot` > 0), the Lim *et al.*
state-transition statistics:

```
kRA = P(rest -> active) = 63/399 = 0.158     mean rest bout   6.3 min
kAR = P(active -> rest) = 63/107 = 0.589     mean active bout 1.7 min
```

Computable, stable, and **not comparable with any published value**, because
published kRA is defined over activity counts above a threshold and this is over
an event channel with an unknown relationship to that threshold. That is the
whole finding: the arithmetic is free and the interpretation is not. §11.2, card C-5.

---

### 1.7 The two recordings, and why no night opened

Commit `4a4866e` added the two things this whole document said were missing: a
worn SleepLab night and a stationary reference, both `watching.csv`, both
schema 2, both from build 0.2.0.

| | `2026-08-19-worn` | `2026-08-20-table` |
| --- | --- | --- |
| Local (EDT) | 23:16 → 09:40 | 10:06 → 10:59 |
| Rows (30 s) | 1249 | 107, first discarded as the set-down transient |
| Delivered rate | 50.00 Hz | 50.00 Hz |
| A night opened | **no** | no |

The delivered rates are identical to two decimal places, which matters: S14 says
two recordings are comparable only if their rates are close. This is the first
time that could be checked rather than assumed, and it passes.

**Counts per 60 s scoring epoch** — the unit every threshold in the app is
expressed in:

| | min | p1 | p5 | p25 | p50 | p75 | p95 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Table (stationary) | **357** | 358 | 359 | 368 | **373** | 381 | 389 |
| Worn night | **370** | 394 | 414 | 469 | **696** | 1103 | 31517 |

The worn night's **minimum** is below the table's **median**. The quietest minute
of a sleeping human and a watch on a table are the same measurement.

Against that, the shipped constants:

| Constant | Ships at | The floor is |
| --- | --- | --- |
| `SegmenterConfig::stillnessCountMax` | 60 | 357 |
| `SegmenterConfig::activityCountMin` | 250 | 357 |
| `WornGate::kMicroMovementFloor` | 8 | 357 |
| `NightAnalyser::kMovementFloor` | 40 | 357 |

**All four are below the noise floor.** So every epoch of every night reads as
"moving": the segmenter never sees stillness and never opens a session, and if
one ever opened, `activityCountMin` would close it on the next epoch. The worn
night is a full night's sleep that produced 1249 idle rows and no night at all.

That is precisely the failure `POST-MORTEM.md` G2 wrote down in advance —
*"`stillnessCountMax` is a guess at about 2 mg … and if the noise is above it
then no night ever opens, which from the outside is indistinguishable from a
wearer who did not go to bed"* — and it was caught by the file G2 added for the
purpose, on the first night it existed. **The design worked; the constant did
not.** This should be recorded as a success of `watching.csv` and not only as a
failure of the constant, because the counterfactual is a night that looks
identical to a wearer who stayed up.

### 1.8 The scorer cannot reach sleep

`SleepWakeScorer.hpp` states its own operating point: *"the sleep/wake boundary
D = 1 falls at about **273 counts per scoring epoch** … roughly 0.01 g of
sustained 1 Hz movement."* That number was written down and never compared with
a measurement, because there was no measurement. There is now, and it is 357 at
the very quietest.

Driving the real algorithm — Cole-Kripke's seven weights, P = 0.001, then all
five Webster rules — over the 624 scoring epochs of the worn night:

| `kCountScale` | floor subtracted | onset | final wake | TST | WASO | awakenings | sleep as % of record |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **0.0055 (shipped)** | 0 | — | — | **none — no epoch scores as sleep** | | | **0** |
| 0.0030 | 0 | 01:48 | 06:10 | 0 h 52 | 211 | 1 | 8 |
| 0.0020 | 0 | 00:07 | 06:45 | 2 h 35 | 244 | 8 | 25 |
| 0.0010 | 0 | 00:07 | 08:48 | 4 h 51 | 231 | 26 | 47 |
| 0.0007 | 0 | 00:07 | 08:57 | 5 h 50 | 181 | 31 | 56 |
| **0.0005** | 0 | 23:49 | 08:57 | **7 h 18** | 111 | 33 | 70 |
| **0.0003** | 0 | 23:42 | 08:58 | **8 h 31** | 46 | 20 | 82 |
| 0.0055 (shipped) | 360 | 00:08 | 06:43 | 1 h 44 | 292 | 4 | 17 |
| 0.0007 | 360 | 00:07 | 08:57 | 6 h 56 | 115 | 34 | 67 |
| 0.0003 | 360 | 23:42 | 08:59 | 8 h 41 | 37 | 20 | 83 |

Four things follow.

**The shipped constant does not merely mis-scale the night; it removes sleep from
the codomain.** Zero epochs. The app would have reported a night of unbroken
wakefulness, or — because the segmenter never opened — nothing at all.

**The scale is wrong by 11–18×, and that dwarfs the axis mismatch of §1.3.** The
vector-magnitude-to-single-axis ratio is between 1 and √3. An order of magnitude
sits on top of it. §1.3 stands and stays worth fixing, but it is a second-order
correction and this document previously implied otherwise.

**Subtracting the floor is a real but secondary lever.** It rescues the shipped
scale from "nothing" to 1 h 44 — still wrong. The floor is additive and
Cole-Kripke is affine in the counts, so an offset genuinely belongs there; it is
just not the main error. It is a *new constant* the app does not have, which
makes A9 at least two-dimensional before §1.3's axis choice is added at all.

**Two independent channels converge on the same night.** At 0.0003–0.0005 the
corrected scorer puts onset at 23:42–23:49 and final wake at 08:57–08:58. The
MOTION-event rule of §1.1, whose threshold was fixed on a *different* night
recorded by a *different* app, puts them at **23:38 and 08:59**. Neither channel
knows about the other and there is no diary for this night; agreeing to within
11 minutes at onset and 2 at final wake is the strongest evidence in this
document that both are measuring the same thing. It is not proof — the scale was
chosen partly by looking for a plausible night — but it brackets `kCountScale`
today, from files on disk, before any diary night is spent.

### 1.9 The worn gate now rests entirely on heart rate

The two recordings are a worn/not-worn pair, which is exactly what the
nightstand gate was designed against. Each of its inputs, measured:

| Channel | Worn night | Table hour | Separates? |
| --- | --- | --- | --- |
| `worn_pct` (TOUCH_DETECT) | median 100 | **median 100** | **no** |
| `touch_n` delivered | 2 rows in 1249 | 0 rows in 106 | no — an event sensor, as S7 says |
| count vs `kMicroMovementFloor` = 8 | all above | **all above** | **no — the test cannot fail** |
| count vs a floor at 400 | 98.2 % above | 100 % below | yes, in a 6.5 %-wide window |
| **any heart-rate sample present** | **1249 / 1249** | **0 / 106** | **yes, completely** |

A capacitive sensor calls a table worn, with total confidence, all hour. The
micro-movement half of the plausibility check is set 45× below the noise floor
and therefore passes everything, including furniture. What remains is the pulse,
and the pulse is unambiguous.

Two consequences, and the second is the one to act on.

**The README's "either alone is defeatable" is now "one of the two is already
defeated."** That is not a criticism of the design — the design anticipated
exactly this and is why there are two halves — but the ledger should say which
half is currently load-bearing, because a reader would reasonably assume both
are.

**`kMicroMovementFloor` should move from 8 to about 400** and the second half is
restored. The window is table p95 389 to worn p5 414: at 400, no table epoch
passes and 1.8 % of worn epochs fail, which is what `kMinPlausiblePct` = 70 is
for. It is a 6.5 %-wide window on one table hour and one night, so it is thin —
but it is a measured value replacing a number that provably cannot discriminate.

And it matters more once the segmenter is fixed, not less: at a workable
`stillnessCountMax`, **100 % of table epochs pass the stillness test**, so a
watch on a nightstand *will* open a session. Everything then depends on the
gate — which today means everything depends on the optical heart rate alone. A
night with `hr: off`, or with a failed optical path, would report a table as a
flawless night.

### 1.10 What the floor is, and what it is not

I replicated `EpochCounter`'s filter chain and integration exactly in Python
(two cascaded one-pole high-passes at 0.25 Hz, two low-passes at 3 Hz, both
re-coefficiented per sample from the sample's own dt, rectified, dt-weighted, the
three axes combined as a vector magnitude, ×1000). It reproduces the ledger's own
calibration — ~30 counts per 60 s epoch per 1 mg of 1 Hz sinusoid — which is what
makes the rest of this trustworthy.

**The floor is not a filter artefact.** My first hypothesis was gravity leaking
through a time-varying high-pass: the coefficients change with every sample's dt,
the input carries a full 1 g, and the integration rectifies before summing so a
DC residue cannot cancel. It is a good hypothesis and it is **wrong**. A
perfectly constant 1 g, at 20 ms, at alternating 20/40 ms, at uniform 18–22 ms
jitter, and with quantisation added, all produce a count of **0.00 per 30 s**.
Recorded as a refutation because it is the obvious first suspect and someone else
will suspect it too.

**So the floor is real acceleration in the 0.25–3 Hz band**, and it is large.
Calibrated against the same replication:

| Table floor, 187 counts / 30 s, is equivalent to |
| --- |
| ~**18.6 mg RMS** of broadband in-band noise |
| ~**12.4 mg** amplitude of a 1 Hz sinusoid |

The ledger states that *"every threshold in this app lives between 0.3 mg and
9 mg of mean band-limited wrist acceleration."* The floor is 12 mg. **Every
threshold in the app is below the instrument's own noise floor**, which is the
one-sentence version of §1.7.

18.6 mg RMS is one to two orders of magnitude above what a modern MEMS part
should produce in a 2.75 Hz bandwidth, which raises a question this data cannot
answer: **is it the sensor, or was the table vibrating?** An empty room in a
building is not an inertial reference. Two things would separate them, and
neither costs a night:

- **Isotropy.** Sensor noise is roughly isotropic; building vibration is not, and
  is usually dominated by one axis. `count_x/y/z` are exactly this test — and
  **both recordings are schema 2 and do not have those columns.** The one
  measurement that would settle it is missing from the only files that could
  carry it, and a schema-3 build fixes that for the cost of one more table hour.
  Card **E-4**.
- **A second surface.** The same hour on a different surface, ideally on
  something soft on the floor. Same card.

**And there is no configuration lever.** `Service.cpp` subscribes the
accelerometer as `(Type::ACCELEROMETER, kAccelPeriodMs, kAccelLatencyMs)` — type,
period, latency. The subscription API exposes no range and no ODR, and S3 and
S17 have already established that the kernel ignores both of the parameters it
does accept. So if the floor is the sensor, it is a given and not a setting.
Card **A-7** is the grep that confirms it.

### 1.11 The tooling cannot read the recordings

`night_report.py thresholds` is the instrument `ROLLOUT.md` phase 4 exists to
run, and the two recordings are precisely its documented input. It fails twice.

**First, on the filename.** `load_nights()` carries

```python
    skip = {"index.csv", "watching.csv"}
    files = [f for f in files if os.path.basename(f) not in skip]
```

applied to *every* path, including ones named explicitly on the command line —
while the comment three lines above it says *"naming it explicitly works and is
how you read a noise floor off it"* and gives the exact invocation. It does not
work; the file is discarded and the script dies with `no epoch rows found`, which
names neither the file nor the reason.

**Second, on the schema.** Renaming past the first blocker, the script refuses
correctly and loudly: the recordings are schema 2 and `EPOCH_COLS` tracks
schema 3, which inserts `count_x/y/z` after `samples`. That refusal is the app's
own rule working exactly as intended — but the consequence is that two
irreplaceable recordings need either a schema-2 reader or a re-recording.

Both are correctness findings and belong to
[`EVALUATION-PROMPT.md`](EVALUATION-PROMPT.md). They are here because between
them they block the only path from these recordings to the constants, and because
the second one is P15 arriving in practice: the header literal says schema 3, the
constant says 2, and the files say 2.

---

### 1.12 The parts, and what they say the constraints actually are

`github.com/UNAWatch/una-hardware` publishes the BOMs. Reading them moves four
ledger rows and corrects a mechanism this app reasons about in three documents.

| Board | Part | What it is |
| --- | --- | --- |
| `UNAcore` | **STM32U5A5QJI6Q** | Cortex-M33, 160 MHz, 4 MB flash, 2.5 MB RAM |
| `UNAlink_AG3335M` | **Bosch BMI270** | the accelerometer — "IMU 3D accelerometer, 3D gyroscope", plus a 3-axis magnetometer nothing uses |
| `UNAbody_PAH8316` | **PixArt PAH8316LS-IN** | "PPG AFE for HRD and SpO2, 6 LED drivers, 4 photodiode inputs, **IR touch detection**, DC lead on/off detection", with 4× SFH 2703 photodiodes and 2× SFH 7016 (green + red + IR) |
| `UNAaltimeter_MS5837` | MS583702BA01-50 | pressure and temperature, unused |

**The noise floor is 67× the accelerometer's datasheet.** BMI270's noise density
is **160 µg/√Hz**. Over the app's 0.25–3 Hz band that is

```
expected   160 µg/√Hz × √3 Hz  =   0.28 mg RMS
measured                           18.6 mg RMS      (§1.10)
                                   ~67×, 36 dB
```

Quantisation is not it either: 16-bit at 0.06 mg/LSB contributes ~0.02 mg
in-band. So the floor is not the part performing to specification.

**And this rehabilitates the original guess.** Had the sensor delivered its
datasheet noise the count floor would be ~6 per scoring epoch rather than 374,
and Cole-Kripke's boundary at the *old* `kCountScale = 0.0055` sat at 273 counts
with the worn night's real movement (median 696 − 374 ≈ 322) landing just above
it. **`kCountScale` was about right for a sensor 67× quieter.** It was not a bad
guess; it was a guess made against a datasheet the hardware is not meeting in
this band. That is a more interesting failure than "the constant was wrong", and
it is the strongest possible argument for the stationary reference hour: no
amount of reasoning from datasheets would have found it.

**What is producing it, and what is not.** §1.10's measurements narrow it
without closing it — stationary (−2.7 % across the hour, so not a thermal
transient) and crest factor 2.90 (band-limited random noise, so not an HVAC tone
and not footfall). It behaves like sensor noise while being 67× the sensor's
spec. Four candidates remain, in my order of belief:

1. **Low-power mode.** BMI270 duty-cycles and averages bursts in low-power mode;
   the 160 µg/√Hz figure is normal/performance mode. A 24/7 watch will be in
   low-power mode, and duty-cycling folds wideband noise down into the low band.
2. **`acc_filter_perf` set to power-optimised** rather than performance.
3. **Minimal averaging** (`acc_bwp` = avg1). Bosch's own note: more averaged
   samples, lower noise.
4. **The `ACCELEROMETER` (0x10) pipeline** adding noise the sensor does not.

**The levers exist and none is app-reachable.** BMI270 has programmable range
(±2 g…±16 g), programmable filter bandwidth (5.5–684 Hz), low-power averaging
(avg1…avg128) and a performance/power filter mode. `SensorConnection` takes
`(type, period, latency)` and nothing else, and S3/S17 show the kernel ignores
both parameters it does accept. So **this is a firmware request to UNA with a
part number and register names behind it**, not an app change — and at 1.2 mA for
the whole app (§1.2) there is budget to trade for it.

**`ACCELEROMETER_RAW` (0x11) is the experiment that separates them**, and it is
free. The SDK documents it as int16 raw alongside the float channel: the LSB
reveals the configured range, and comparing its floor against 0x10's says whether
the noise is in the silicon or added downstream. Card **E-5**.

#### Three things the BOMs contradict

**SpO2 is a hardware capability.** The PAH8316 is specified "for HRD and SpO2"
and the board carries red and IR emitters. S4 reads *"there is no firmware
producer to ask"* and this review wrote SpO2 into the terminal nodes as closed.
The silicon is there; the gap is firmware. The row and the terminal node both
need their reopening condition rewritten — from "a sensor that does not exist" to
"a driver UNA have not written".

**The PPG is not single-channel.** Four photodiodes, six LED drivers, three
wavelengths. `README.md`, `RestfulnessBand.hpp` and ledger A2 all justify "no
staging, ever" partly on *"the PPG waveform is single-channel"*. That is a
statement about what the firmware exposes, not about the hardware. A2's
conclusion may well survive — staging also needs beat-to-beat intervals, and S5
stands — but **it must not rest on a hardware claim that is false**, because the
next person to read the BOM will conclude the ledger is careless rather than that
one clause was.

**`TOUCH_DETECT` is IR, not capacitive.** It is the PAH8316's own IR touch
detection. `README.md` reasons about "a capacitive sensor" reporting a
face-down watch as worn. The conclusion held — it called the table worn for 106
straight epochs — but the mechanism is optical reflectance, which explains it
*better* and predicts different failure modes: a matte black surface may read
not-worn where a pale table reads worn, and a loose strap in daylight is a
different problem from a loose strap under a duvet. Any future work on the worn
gate should reason about reflectance, not capacitance.

---

## 2. The MCU budget, established from the code

The prompt asks for this to be established rather than assumed, because half the
proposals turn on whether the arithmetic runs on the watch or in `Tools/`.

From `Probe/.../flags.make` (the same toolchain flags the app build uses) and
`Output/SleepLabService.elf.elf.map`:

| | |
| --- | --- |
| Core | **Cortex-M33**, `-mfpu=fpv5-sp-d16 -mfloat-abi=hard` |
| Float | **single-precision in hardware; `double` is emulated in software** |
| Language | `-std=gnu++17 -fno-exceptions -fno-rtti -Os`, `-nostdlib`, newlib |
| RAM region | `0x7d000` = **512 000 bytes**; `STACK_SIZE` `0x2800` = 10 240 |
| Service ELF | `.text` 76 356 · `.data` 964 · `.bss` 39 756 → **~127 KB with stack** |
| GUI ELF | `.text` 145 372 · `.data` 3 076 · `.bss` 69 392 → **~218 KB** |
| Both | ~345 KB of 512 KB → **~155 KB nominal headroom** |
| A night in RAM | `mScoring` 960 × 16 B = 15.4 KB, `mVerdicts` 960 B, `mBand` 960 B, `mScoringWallEnd` 7.7 KB → **~25 KB** |

What that permits, stated so proposals can be checked against it:

- **`float` is free; `double` is not.** Any candidate whose reference
  implementation is written in `double` needs its numerics re-argued, not just
  re-typed. `-Os` plus soft-float `double` is a 10–50× penalty on the hot path.
- **A whole-night O(n log n) pass over 960 epochs is free.** DFA, a 1024-point
  FFT, a percentile sort, a five-parameter sweep — all trivially affordable once
  a night, at 06:41, on a device that has just spent eight hours idle.
- **A trained model is not landing here.** Sundararajan's random forest is
  hundreds of trees over a 60-feature window. Even quantised, that is the wrong
  shape for 155 KB shared with a TouchGFX tree — and §11.1 argues below that it
  should not be attempted for a stronger reason than size.
- **Anything at 20 Hz is free in CPU.** A PPG peak detector at 20 samples a
  second is nothing. What it needs is a driver, which is card **E-1**.

---

## 3. The card format, and how to read the catalogue

Every card carries the prompt's eight fields. They are grouped by instrument, so
the free ones are visibly first, and **ordered within each group by what a
refutation saves**, not by cost.

**Re-ordered 2026-08-20.** Everything below is now downstream of one thing: the
four movement constants are below the instrument's noise floor (§1.7), so until
they move, no night opens, no epoch scores as sleep, and every night recorded is
a `watching.csv` to be analysed offline. **C-6 and A-7 come first, then E-4, then
the nights.** The three cards below remain load-bearing for everything after
that:

- **E-1** (three minutes) can delete the whole PPG-variability route.
- **D-2** (an afternoon, no watch) can delete it even if E-1 says yes, and it
  is the result that stops a feature from being built and believed.
- **D-3** (a day, no watch, no nights) can set `kCountScale` against *PSG ground
  truth* rather than against a diary, which changes what the ten diary nights are
  for.

---

## 4. Group A — desk. Free, minutes.

### A-1 — Cole-Kripke's coefficients were fitted on single-axis counts, not vector-magnitude counts.

*Hypothesis:* The reference implementations the A8 check used transform a
**single axis** before applying the weights, so `kCountScale` bridges an
axis-combination change as well as a unit change. Creates ledger row **A13**.

*Instrument:* desk. Read `apply_cole_kripke.R` and pyActigraphy's `CK`. Done
while writing this; recorded here so the row can be dated.

*Procedure:* fetch both implementations' sources; find the transform applied to
counts before the weighted window; record whether it names an axis.

*The measurement:* the presence of `axis1` (or equivalent) in the transform, and
whether any reference implementation applies the weights to a vector magnitude.

*Decision rule:* both references use a single axis → **CONFIRMED**. Either uses
a vector magnitude → **REFUTED**, and the mismatch does not exist. One each →
inconclusive, and the primary source (A-3) settles it.

*Result:* CONFIRMED. `actigraph.sleepr` uses `pmin(axis1/100, 300)` and
documents it as the y-axis.

*What confirming unlocks:* the axis-combination sweep in the rescore tool
(**§15**), which makes A9 a two-parameter calibration instead of a one-parameter
one. Card **C-4**.

*What refuting locks:* nothing; it would have simplified A9.

*Preempted by:* nothing.

*Expiry:* A-3. If the paper itself specifies otherwise, this row moves.

---

### A-2 — Cole *et al.* published a second variant this app can compute from nights already on disk.

*Hypothesis:* the paper's 30-second parameter set scores minutes from the
maximum 30 s sub-epoch, so SleepLab's 30 s recording epochs are exactly its
input. Creates ledger row **A14**.

*Instrument:* desk, done. Corroborated in one reference implementation only, so
the row is LIKELY.

*The measurement:* the presence of the second and third parameter sets in a
reference implementation, with their scale factors, and the word "maximum" in
the comment describing what they are fed.

*Decision rule:* a second set present, cited to p. 466, described as a maximum
over sub-epochs → **LIKELY** pending A-3. Absent → **REFUTED** and this card
dies.

*Result:* LIKELY. P = 0.0001, weights (50, 30, 14, 28, 121, 8, 50), commented
*"the optimal parameters for the maximum 30-second nonoverlapping epoch of
activity per minute."*

*What confirming unlocks:* a second published scorer running on the same file at
zero recording cost; the **variant-disagreement fraction** as a
diary-free calibration signal for `kCountScale`; and a real cash-in of the
"record finer than you score" decision, which until now has been a promise about
the future.

*What refuting locks:* the claim that recording at 30 s buys anything a
published algorithm can use today. The 30 s grid would then be justified only by
future scorers.

*Preempted by:* nothing.

*Expiry:* A-3.

---

### A-3 — the A8 residue: read the primary sources.

*Hypothesis:* p. 466 of Cole *et al.* 1992 carries the seven weights, P = 0.001,
the four-back/two-forward window, sleep below threshold, **and** the count
transform A-1 and A-2 depend on; and Webster *et al.* 1982 pp. 389–399 carry the
five rescoring rules as implemented. Moves **A8** from LIKELY to CONFIRMED or
REFUTED-with-the-correction.

*Instrument:* desk — a library errand or an institutional login. Hours, calendar
cost zero.

*Procedure:* obtain both papers. For Cole: transcribe the equation table on
p. 466 in full, including every parameter set and the definition of the activity
variable each is fitted for. For Webster: transcribe the rescoring rules and
their minute thresholds.

*The measurement:* the seven weights and P for the 1-minute set; the definition
of the activity input; the threshold's direction; the five rules' wake/sleep
minute pairs.

*Decision rule:* every value agrees with `PublishedConstants.*` → **CONFIRMED**.
Any disagreement → **REFUTED** with the correction written into the row, as
happened once already with Webster rule 4. Papers unobtainable → inconclusive,
A8 stays LIKELY, and the row says the attempt was made and on what date, so it is
not re-attempted every six months.

*What confirming unlocks:* the only thing standing between this app's scorer and
"transcribed from the primary source". Also promotes A13 and A14 out of their
one-reference dependence.

*What refuting locks:* depends on what is wrong. A wrong weight is a constant
change; a wrong *input definition* would invalidate every night scored so far,
which is why every summary carries its constants.

*Preempted by:* nothing. It preempts nothing either — A-1, A-2 and the whole of
§15 proceed on the reference implementations, as they do today.

*Expiry:* none. A paper does not change.

---

### A-4 — `kEpochCsvSchema` and the CSV header disagree.

*Hypothesis:* `NightStore.hpp` declares `kEpochCsvSchema = 2` while
`NightStore.cpp`'s header literal says "schema 3" and the column list carries
`count_x/y/z`, so the machine-readable schema number in every summary JSON
(`epochs_csv_schema`) is stale by one.

*Instrument:* desk, done. `NightStore.hpp:127`, `NightStore.cpp:35`,
`NightStore.cpp:719`.

*Decision rule:* constant ≠ literal → confirmed. Equal → nothing here.

*Result:* confirmed. This is a correctness finding and therefore
[`EVALUATION-PROMPT.md`](EVALUATION-PROMPT.md)'s territory, recorded here only
because §11.2 and §15 both propose readers that key off that number, and a reader
obeying the app's own rule — *"a reader that does not recognise a schema must
refuse the file"* — will refuse a schema-3 file that says it is schema 2, or
worse, accept it and miss three columns.

*What refuting locks:* n/a.

---

### A-5 — the MCU budget.

Established in §2. Creates no ledger row of its own; it is the constraint every
other card is checked against.

---

### A-6 — which direction is `hr_trust_x10`?

*Hypothesis:* the SDK's `HEART_RATE` trust field has a documented direction and
range, so a threshold on it is writable without a hardware run.

*Instrument:* desk — grep the SDK headers.

*Procedure:* `grep -rn "trust" $UNA_SDK/Libs/Header/` and read the doc comment on
the field, then check whether any SDK example thresholds it.

*The measurement:* the field's documented range and whether higher means more
confidence.

*Decision rule:* a documented range and direction → confirmed, and the worn
gate gains a second input for free. A field with no doc comment → inconclusive,
and the fallback is empirical: §1.5 shows it sits at 29 through a still night and
12–15 through active daytime, which fixes the *direction relative to movement*
without fixing its meaning. **Do not write a threshold on the empirical reading
alone** — an inverted trust scale would make the worn gate trust exactly the
readings it should distrust.

*What confirming unlocks:* G1 in `POST-MORTEM.md` is already closed at the
recording end (`hr_trust_x10` is a column); this is what makes it usable.
A watch on a warm surface producing a present-but-untrusted pulse is precisely the
nightstand case the gate exists for.

*What refuting locks:* if the SDK documents no direction and no example
thresholds it, the column stays diagnostic-only and the worn gate's second half
stays binary. Write that down so it is not re-litigated.

*Preempted by:* nothing.

*Expiry:* any SDK bump.

---

### A-7 — is the accelerometer's noise floor a setting or a given?

*Hypothesis:* the SDK exposes no way for an app to choose the accelerometer's
measurement range, ODR or internal filtering, so the 12 mg in-band floor of
§1.10 cannot be reduced from an app. Creates **S23**.

*Instrument:* desk — grep the SDK.

*Procedure:* `grep -rniE "range|odr|fullscale|full_scale|bandwidth" $UNA_SDK/Libs/Header/SDK/SensorLayer/` and read the `Sensor` constructor's parameter list.

*The measurement:* whether any constructor, setter or config struct takes a range
or an output data rate.

*Decision rule:* no such parameter anywhere → **confirm**; the floor is a
property of the platform and every threshold must be set above it, permanently.
A range or ODR parameter exists → **refute**, and a short hardware run at the
narrowest range is the highest-value experiment in the document, because
halving the range typically halves the noise and this app needs an order of
magnitude of headroom it does not have.

*Result, 2026-08-20:* **confirmed, with a sting.** `SensorConnection` takes
`(type, period, latency)` and the accelerometer parser returns `float g` — no
range, no ODR, no bandwidth, and nothing quantised app-side. But §1.12 identifies
the part as a **Bosch BMI270**, which *does* have programmable range, programmable
filter bandwidth, low-power averaging and a performance/power filter mode. So the
honest wording is not "the floor is a platform given" but **"the levers exist and
are held by the kernel"** — which makes this a firmware request with register
names behind it rather than a closed door. Ledger row S23.

*What confirming unlocks:* nothing new — it closes a hope, which is why it is
cheap and worth doing before anyone spends time on §1.10's isotropy question.

*What refuting locks:* nothing; it opens a lever, and a large one.

*Preempted by:* nothing. Run it before **E-4**.

*Expiry:* any SDK bump.

---

## 5. Group B — host tests and the offline harness. Free, minutes.

### B-1 — the biquad would fix S14, and S14 is worse than a comparability caveat.

*Hypothesis:* replacing `EpochCounter`'s one-pole band-pass with a
bilinear-transform biquad holds the count within ±3 % across 4.8–96 Hz delivered,
where the current filter loses 26 % at a tenth of rate.

*Instrument:* host test — the existing
`EpochCounter.TheCountIsOnlyRateIndependentInTheUpperHalfOfTheRange` fixture, run
against both filters.

*Procedure:* implement the biquad behind a compile-time switch; run the existing
rate-sweep fixture at 96, 48, 25, 12.5, 6, 4.8 and 2 Hz against the 0.5 Hz
sinusoid; record counts per 60 s scoring epoch for each.

*The measurement:* max deviation from the 48 Hz count, as a percentage, across
the sweep.

*Decision rule:* max deviation ≤ 3 % across 4.8–96 Hz → **confirm**, adopt, and
S14 becomes a historical row. Deviation > 10 % → **refute**; the filter is not
the cause and the rate-dependence is in the integration after all. Between 3 %
and 10 % → **inconclusive**: better than today, not good enough to stop recording
`acc_hz_x10`, and the decision is then about MCU cost rather than correctness.

*What confirming unlocks:* nights recorded at different delivered rates become
comparable, which unlocks *every* multi-night metric in §11.2 — SRI, the 28-night
baseline, kRA/kAR trends — none of which currently survives a rate change.
It also removes the strongest argument against the raw-accelerometer route
(§11.1), because a rate-robust count is what a raw route was going to buy.

*What refuting locks:* the belief that this is fixable in the filter. The
mitigation stays what it is — record the rate, refuse to compare across it — and
that becomes permanent rather than provisional.

*A sub-hypothesis, tested and refuted 2026-08-20, recorded so it is not
re-suspected:* that the one-pole high-pass, re-coefficiented per sample, leaks
gravity into the passband and is therefore the source of the 12 mg noise floor of
§1.10. Replicating the exact filter chain in Python and feeding it a perfectly
constant 1 g at 20 ms, at alternating 20/40 ms, at uniform 18–22 ms jitter, and
with quantisation, yields a count of **0.00 per 30 s** in every case. The filter
does not leak. The floor is real acceleration, and B-1 is about rate-sensitivity
only — it does not lower the floor.

*Preempted by:* nothing. **This is the highest-value free card in the document
after the rescore tool and the constants**, because S14 silently biases in the
direction that reads as more sleep.

*Expiry:* none.

---

### B-2 — the motion-event detector survives decimation to a cheap day log.

*Hypothesis:* the §1.1 detector, run over epochs decimated to 5-minute
resolution with heart rate duty-cycled, still separates the night from the two
daytime blocks.

*Instrument:* offline harness plus re-analysis. Free, minutes.

*Procedure:* decimate run 13 and runs 2–3 to 2-, 5- and 10-minute bins by
summing `motion_mot`; re-run the quiet-run detector with the run length scaled
to the bin; record onset, final wake and daytime false-positive count at each
resolution.

*The measurement:* onset and final-wake error against the diary, in minutes, at
each bin size; and the count of ≥1-bin quiet runs in the daytime blocks.

*Decision rule:* onset and final-wake error stay within 15 minutes at the 5-minute
bin **and** daytime false positives stay at zero → **confirm**, and the
"smallest version" is a 5-minute day log. Error exceeds 30 minutes at 5 minutes
→ **refute**; a day log has to run at the night's own resolution and its cost
argument has to be redone. Between → inconclusive, and the 2-minute bin is tried.

*What confirming unlocks:* the reduced-fidelity day log in §11.3 at a fraction of
the write cadence; and with it, the daytime HR reference on a continuing basis
rather than by accident.

*What refuting locks:* the cheap version of 24-hour recording. Round-the-clock
would then cost full-fidelity epochs all day, which is a storage and write-cycle
argument rather than a battery one — ~250 KB a day, ~90 MB a year.

*Preempted by:* **C-1**. If the motion channel does not survive
pre-registration on real nights, there is nothing to decimate.

*Expiry:* none.

---

### B-3 — the van Hees per-axis non-wear criterion against the nightstand fixture.

*Hypothesis:* a per-axis standard-deviation criterion over long windows
separates a watch on a table from a worn sleeping wrist at least as well as the
app's bespoke micro-movement-plus-pulse gate, using `count_x/y/z` which are
already recorded.

*Instrument:* offline harness (the existing nightstand scenario in
`Pipeline_test.cpp`) plus one hour of a watch left on a table, which is not a
night.

*Procedure:* implement the criterion in `Tools/` over `count_x/y/z` per 30 s
epoch aggregated to 60-minute windows on a 15-minute step; run it against the
harness's nightstand night, its worn nights, and an hour of table recording
pulled off the watch.

*The measurement:* the number of windows classified non-wear in each, and
whether any worn window is misclassified.

*Decision rule:* zero worn windows misclassified **and** ≥ 90 % of table windows
caught → **confirm**, and it becomes a second, independent gate. Any worn window
misclassified → **refute** for this device; the criterion's thresholds are
defined in mg of raw acceleration and this app's counts are a band-limited
integral, so the units do not transfer and the row must say so. Between →
inconclusive, and it is a diagnostic column rather than a gate.

*What confirming unlocks:* a published non-wear method alongside the bespoke
one, which matters because the bespoke one's thresholds are all guesses and
`ROLLOUT.md` phase 4 openly contemplates "NO CLEAN VALUE — the distributions
overlap".

*What refuting locks:* the idea that the accelerometry literature's non-wear
methods transfer to a counts-based recorder without recalibration. That is worth
knowing, because it also applies to every other raw-signal method in §11.1.

*Preempted by:* nothing.

*Expiry:* changes to `EpochCounter`'s band.

---

## 6. Group C — re-analysis of files already recorded. Free, hours.

### C-1 — the motion-event channel scores a night to diary tolerance.

*Hypothesis:* a fixed rule over `motion` events — the first run of ten
consecutive minutes with ≤ 1 event for onset, the end of the last such run for
final wake — estimates sleep onset and final wake within 15 minutes of a
hand-kept diary, on nights it has not been fitted to. Creates **A15**.

*Instrument:* re-analysis of nights that will be recorded anyway. **No night is
spent on this card** — it rides on the ten diary nights `ROLLOUT.md` phase 5
already requires.

*Procedure:* fix the rule now, in code, before the nights exist: threshold ≤ 1
event per minute, run length 10 minutes, no smoothing, no per-night tuning. Add
`night_report.py motion` as a subcommand. Run it against each night's epoch CSV
and compare with `diary.csv`.

*The measurement:* mean signed error and interquartile spread, in minutes, on
onset and final wake, against the diary, over ≥ 8 nights; and separately against
Cole-Kripke's own onset and final wake from the same file.

*Decision rule, written now:* median absolute error ≤ 15 min on **both** onset
and final wake → **confirm**, and the channel becomes a `kCountScale`-free
cross-check on the segmenter and on the scorer. ≥ 30 min on either → **refute**;
the 2026-08-19 result was one night and a fitted threshold, and it is closed.
Between 15 and 30 → **inconclusive**, and the follow-up is the threshold sweep
over the same nights, which is free.

*What confirming unlocks:* an independent second opinion on onset and final wake
that does not pass through the guessed constant — which is the single most useful
thing a second channel could be, because every published number this app might
one day print rests on `kCountScale` and nothing else currently checks it. It
also makes the segmenter's open/close decisions auditable against something other
than themselves. And it retroactively makes the 2026-08-19 night — diary, Fenix
and all — a scored night.

*What refuting locks:* the claim that `motion` and `sig_motion` carry sleep
information on this device. They stay diagnostic columns, S13 stands as written,
and the 2026-08-19 night stays unscoreable. Write the row either way, because the
appeal of "we already record this" will otherwise resurface.

*Preempted by:* nothing.

> **Strengthened 2026-08-20.** The rule, with its threshold and run length fixed
> on the probe night, was run unchanged over the new worn SleepLab night — a
> different night, a different app, no diary. It gives onset **23:38** and final
> wake **08:59**. The corrected Cole-Kripke of §1.8, at `kCountScale`
> 0.0003–0.0005, independently gives **23:42–23:49** and **08:57–08:58**. Two
> channels that share no constant agreeing to 11 minutes and 2 minutes is the
> best corroboration available without a diary, and it raises this card from a
> nice-to-have to the second opinion the calibration should be checked against.
> It does not replace the pre-registered run on the diary nights.

*Expiry:* any firmware change to the MOTION classifier, which is an opaque
kernel-side algorithm and could change without notice. **This is the weakest
part of the proposal** and it is the reason the channel should be a cross-check
and never the primary scorer: it has no published definition and no way to detect
that its definition moved.

---

### C-2 — 24-hour recording is affordable.

*Hypothesis:* the whole app with continuous optical HR costs 13–15 % of the pack
per 24 h. Creates **S19**.

*Instrument:* re-analysis, done (§1.2). The confirming run is **C-2b**.

*Decision rule (applied):* three independent blocks within ±0.1 mA of each other
→ confirm. Any block above 2 mA → refute. Spread > 0.5 mA → inconclusive.
*Result:* 1.18, 1.25, 1.26 mA. Confirmed.

*What confirming unlocks:* §11.3 entirely — 24-hour recording, and with it the
daytime HR reference, total 24-h sleep, and the *possibility* of the circadian
metrics in §11.2 (which need multiple 24-h days, not one).

*What refuting locks:* nothing was refuted.

---

### C-2b — the 24-hour confirmation run.

*Instrument:* one ordinary day of wear. **Costs a day, not a night.**

*Procedure:* SleepLab installed, `diagnostics: on`, `hr: continuous`, bedtime
window widened to 24 h (`bedtime` and `wake_by` one minute apart is refused as
zero-width, so use `"00:00"`/`"23:59"`). Charge to full at a fixed time — say
19:00 — and note it in the diary as a known hole. Wear for 24 h from the
following morning. Keep a written log of every deliberate stillness longer than
20 minutes: a film, a meeting, a train.

*The measurement:* `batt_mah` first row to last row of the day's
`watching.csv` and night CSV, over the summed `span_ms`; expressed as a
percentage of the `batt_mah` at 100 %.

*Decision rule:* ≤ 18 % of pack per 24 h → **confirm** S19 and 24-hour recording
is adopted. ≥ 30 % → **refute**; the extrapolation was wrong and the day log has
to be duty-cycled. 18–30 % → **inconclusive**, and the explicit cause to check
first is an unusually active day: compare the day's step total against the
2026-08-18 daytime blocks' 3 072.

*What confirming unlocks:* the day log; the daytime HR reference as a standing
measurement rather than an accident; the false-positive dataset for §11.3.

*What refuting locks:* continuous 24-h HR. `hr: duty` at 60 s in 300 s is
already implemented and would then become the documented default for daytime.

*Preempted by:* **C-2**, which is why this is a confirmation and not a discovery.

*Expiry:* any firmware change to the HR sensor's duty cycle.

---

### C-3 — the nocturnal dip and the nadir's timing.

*Hypothesis:* the timing of the nocturnal HR minimum, expressed as
`hr_min_at_pct`, is stable enough across a wearer's own nights to be worth
reporting. Creates **A22**.

*Instrument:* re-analysis of the diary nights. No extra nights.

*Procedure:* for each night, compute the 15-minute-smoothed HR nadir and its
position as a fraction of the night; and the dip against that wearer's own
daytime p5 from the day log (or, until the day log exists, from runs 2–3 of
`probe_log.csv`).

*The measurement:* the interquartile range of `hr_min_at_pct` across ≥ 8 nights;
and the dip percentage per night.

*Decision rule, written now:* IQR of nadir position ≤ 20 percentage points →
**confirm** it is a stable personal quantity and it may be *described* on the
history screen. IQR ≥ 40 points → **refute**; it is noise and must not be
printed, and `hr_min_at_pct` stays an index column nobody reads. Between →
**inconclusive**, and it is reported in the file and not on the screen.

*What confirming unlocks:* the honest half of §11.4 — a personal, trended,
descriptive cardiac number with no recovery claim attached.

*What refuting locks:* every "when did your heart rate bottom out" feature.
**Two nights now exist and they disagree almost maximally.** The probe night put
the smoothed nadir at **6 %** of the night, 14 minutes after onset, with heart
rate rising monotonically thereafter. The SleepLab worn night puts it at
**05:43 — about 65 %** of the sleep period, with the conventional mid-night
shape. Two nights spanning 59 percentage points is already outside this card's
pre-registered ≥ 40-point refutation band. It is n = 2 across two apps, so it is
not a result — but it is a strong prior that A22 will be refuted, and the honest
consequence is to **not build the feature while waiting to find out.**

*Preempted by:* nothing.

*Expiry:* none.

**And the part that does not have an experiment:** whether the dip *means*
recovery. Searching for validation of "nocturnal HR dip indicates recovery"
returns vendor documentation and device marketing, not peer-reviewed
free-living validation. Firstbeat's own method — which is what Body Battery is —
is documented in white papers rather than in the peer-reviewed literature, and it
rests on beat-to-beat intervals this device does not produce. **No experiment
available to this project can validate a recovery interpretation of the dip.**
The number is describable; the interpretation is not available, and §13's
do-not-build list says so.

---

### C-4 — the rescore sweep, over (axis × scale × ceiling).

*Hypothesis:* there exists a combination of axis input, `kCountScale` and
saturation ceiling that puts this app's onset and final wake within 20 minutes of
a diary, and the surface has a single minimum rather than a plateau.

*Instrument:* re-analysis, once the ten diary nights exist. Free thereafter,
unlimited.

*Procedure:* the rescore tool of §15. Sweep **`kCountScale` over 0.0001 to 0.005**
logarithmically; a **noise-floor offset** over {0, 300, 360, 400}; the input over
{`count` (VM), `count_x`, `count_y`, `count_z`, max-axis}; the ceiling over
{none, 100, 300, 1000} in post-scale units; and the variant over {1-minute,
30-second-max}. Score every night at every point.

> **Corrected 2026-08-20.** This card first proposed sweeping `kCountScale` from
> **0.0005 upward over three decades**. §1.8 measures the plausible range as
> **0.0003–0.0005** — at or *below* the bottom of that sweep, which would have
> returned a maximum at an endpoint and been read as "widen and re-run" at best.
> The offset dimension is also new: it did not exist as a parameter before the
> table hour showed there is a floor to subtract.

*The measurement:* mean signed error and IQR on onset and final wake, per point.

*Decision rule, written before the sweep:* the winner is the point minimising
**the sum of absolute mean signed errors on onset and final wake**, subject to
the IQR at that point being no worse than 1.5× the best IQR anywhere on the
surface. **The failure mode this rule exists to avoid, named as the prompt
asks:** minimising mean signed error alone is minimised perfectly by any
constant that makes the scorer call the whole night sleep — onset goes to the
first epoch, final wake to the last, and on a night where the diary brackets the
recording, both errors go to zero for the wrong reason. The IQR side-condition is
what stops that, and a second guard is required: **reject any point whose median
sleep efficiency across the nights exceeds 97 %.**

If no point satisfies both → **inconclusive**, and the honest report is that the
count-to-paper bridge does not exist at any scalar value, which is itself the
strongest possible argument for a raw-signal scorer (§11.1).

*What confirming unlocks:* A9 and A10 together, the README's accuracy table, and
every metric in the validation table moving from *synthetic-only* to
*diary-validated*.

*What refuting locks:* the scalar bridge. If the surface has no minimum, no
amount of further diary nights fixes it, and the choice is between a raw-signal
scorer (§11.1) and permanently labelling every sleep figure as uncalibrated.

*Preempted by:* **D-3**, which may supply the scale before any diary night is
scored — in which case this card becomes a *check* rather than a *search*, and
the ten diary nights confirm a value rather than finding one.

*Expiry:* any change to `EpochCounter`'s band or integration, including B-1.

---

### C-6 — all four movement constants, from the two recordings that exist.

*Hypothesis:* the worn night and the table hour together fix
`stillnessCountMax`, `activityCountMin`, `kMicroMovementFloor`,
`kMovementFloor` and bracket `kCountScale`, without a further night. Creates
**S22**.

*Instrument:* re-analysis, done in §1.7–§1.9. What remains is a decision and an
edit.

*Procedure:* per 60 s scoring epoch, from the two files. Placed as
`ROLLOUT.md` phase 4 specifies, except that the specification's own
"NO CLEAN VALUE" branch has to be checked first, and is passed only narrowly.

*The measurement and the proposed values:*

| Constant | Ships at | Basis | Proposed |
| --- | --- | --- | --- |
| `WornGate::kMicroMovementFloor` | 8 | between table p95 (389) and worn p5 (414) | **400** |
| `SegmenterConfig::stillnessCountMax` | 60 | the value that opens the night where the MOTION channel and the corrected scorer both put onset (23:38) | **900** |
| `NightAnalyser::kMovementFloor` | 40 | worn p75 | **1100** |
| `SegmenterConfig::activityCountMin` | 250 | worn p99 is 88 028 and is contaminated by getting up; **this one is not settled by these recordings** | needs a few minutes of deliberate walking — see below |
| `SleepWakeScorer::kCountScale` | 0.0055 | §1.8 | **bracketed at 0.0003–0.0005**, not yet chosen |

*Decision rule, applied:* a constant is settled here only if the two
distributions separate at it. `kMicroMovementFloor` separates in a 6.5 %-wide
window — **settled, narrowly**. `stillnessCountMax` at 900 opens the night at the
right minute but **100 % of table epochs also fall below it**, which is expected
(the gate, not the segmenter, is what rejects furniture) and is why §1.9's
`kMicroMovementFloor` change is a precondition rather than an option.
`activityCountMin` is **not settled**: a night spent asleep contains no example
of "active enough to be up" that is not the wearer getting up at the end, and
`ROLLOUT.md` phase 4 already says so and says what to record instead — a few
minutes of walking about, which is not a night.

*What confirming unlocks:* a night that opens. Everything downstream of that.

*What refuting locks:* if a later table hour on a different surface (**E-4**)
moves the floor materially, every value here moves with it and the pair has to be
re-recorded — which is why E-4 comes before the edit is trusted, and why the
values above should be committed **with the two recordings' medians written into
the ledger beside them**, so the next person can see what they were derived from.

*Preempted by:* **A-7**. If the accelerometer's range is a setting, the floor
moves and all five numbers are provisional.

*Expiry:* **B-1**, which changes every count; and any firmware change to the
sensor's own filtering.

---

### C-5 — the fragmentation statistics.

*Hypothesis:* kRA and kAR computed from this app's counts are stable within a
wearer across nights and vary between nights in a way WASO and the awakening
count do not.

*Instrument:* re-analysis. No extra nights.

*Decision rule:* across ≥ 8 nights, kRA's between-night variance exceeds its
within-night bootstrap variance by ≥ 3× **and** its correlation with the
awakening count is |r| < 0.8 → **confirm** it carries information the existing
metrics do not. |r| ≥ 0.8 → **refute**; it is the awakening count wearing a
Greek letter, and it should not be added. Between → inconclusive.

*What confirming unlocks:* a fragmentation measure with a published definition
and a real literature behind it (Lim *et al.* 2011), computed per night from the
`count` column, replacing "awakenings: 3" as the headline restlessness figure.

*What refuting locks:* the addition. `movementIndexPct` already exists, is
scorer-independent, and would be doing the same job.

*The caveat that must go in the row either way:* published kRA values are defined
over a specific device's counts above a specific threshold. This app's kRA will
not be comparable with any published value, ever, and must never be shown next to
one. It is a personal, trended quantity or it is nothing.

*Preempted by:* nothing.

*Expiry:* B-1, which would change every count.

---

## 7. Group D — public datasets. Free, hours to days, no watch.

### D-1 — the scorer bake-off.

*Hypothesis:* at least one of Sadeh (1994), the UCSD algorithm (Kripke *et al.*
2010), van Hees' HDCZA (2018) and the universal-filter approach (Biegański
*et al.* 2026) beats Cole-Kripke-with-Webster on epoch-by-epoch agreement with
PSG, on data where ground truth exists.

*Instrument:* public dataset. The Walch *et al.* 2019 PhysioNet set
(`sleep-accel`, 31 subjects, raw Apple Watch acceleration + PPG-derived heart
rate + PSG labels) is the one that fits, because it is the only open set carrying
*raw wrist acceleration* alongside PSG — which is what lets this app's own
`EpochCounter` be run over it.

*Procedure:* implement each candidate in `Tools/`, and — this is the part that
makes it valid for *this* app rather than for actigraphy in general — feed
Cole-Kripke from counts produced by **SleepLab's real `EpochCounter`** compiled
as a host library and run over the dataset's raw acceleration, not from the
dataset's own count column. Score every subject with every candidate. Compare
against the PSG labels.

*The measurement:* per-subject epoch-by-epoch accuracy, sensitivity to sleep,
**specificity to wake**, and Bland-Altman bias and limits of agreement on total
sleep time and on WASO — the Menghini *et al.* (2021) reporting set, which is the
standard this project should adopt wholesale rather than inventing.

*Decision rule, written before the run:* a candidate replaces Cole-Kripke only
if it improves **specificity to wake by ≥ 10 percentage points** without losing
more than 3 points of sensitivity to sleep, on ≥ 24 of the 31 subjects. Nothing
meets that → Cole-Kripke stands and the row says so with the numbers. A candidate
meets it but cannot run on the MCU (§2) → it becomes a `Tools/` re-analysis
scorer only, and the on-watch scorer stays Cole-Kripke. Improvements below the
bar → inconclusive; not worth invalidating the calibration for.

*What confirming unlocks:* a scorer swap, and with it a rewrite of §11.1's answer.

*What refuting locks:* the recurring suspicion that a 1992 algorithm must be
behind. Patterson *et al.* (2023) already report that simple regression and
heuristic algorithms slightly outperform complex ML on sleep-wake
classification, which is the prior this experiment tests rather than assumes.

*Preempted by:* nothing. But note what a swap would cost, because the prompt
asks: **a scorer change invalidates the `kCountScale` calibration plan
entirely.** Sadeh and the UCSD algorithm have their own count-unit assumptions;
HDCZA and the universal filter do not use counts at all. Any swap must be decided
*before* the ten diary nights are spent calibrating a constant the new scorer does
not have.

*Expiry:* none, but it should be re-run when a new candidate appears.

---

### D-2 — can 20 Hz carry a nocturnal RMSSD?

*Hypothesis:* RMSSD computed from a PPG waveform decimated to 20 Hz, with cubic
spline interpolation of the detected peaks, is within 10 % of the RMSSD computed
from the same signal at full rate, for nocturnal-magnitude variability.

*Instrument:* public dataset. No watch, no night. An afternoon.

*Procedure:* take open PPG or ECG with beat-to-beat ground truth; decimate to
20 Hz; apply the peak detection and cubic-spline interpolation that would
actually be implemented on an M33 in single-precision float; compute RMSSD and
SDNN; compare against the full-rate values. Stratify by the true RMSSD, because
the error is a fixed quantisation and the signal is not: **50 ms quantisation
against a nocturnal RMSSD that may itself be 20–50 ms is the whole problem.**

*The published prior, which is why this is marginal rather than obvious:*
Béres & Hejjel report RMSSD within 5 % relative error at 50 Hz **without**
interpolation, and that with interpolation 50 ms sampling — exactly 20 Hz — is
sufficient. Their subjects were healthy young volunteers with normal variability,
on clean recordings, awake; they note the required rate rises where variability
is reduced. Their earlier work recommends ≥ 5 Hz for pulse rate and ≥ 50 Hz for
HRV. So the literature says 20 Hz is at the boundary and the boundary was
measured under conditions kinder than a sleeping wrist.

*Decision rule, written before the run:* median relative RMSSD error ≤ 10 %
**and** 90th-percentile error ≤ 20 %, in the true-RMSSD band 20–50 ms →
**confirm**; a nocturnal PRV figure would be honest if the driver exists.
Median error ≥ 25 % → **refute**; no PRV, no HRV, no recovery figure from PPG on
this device, ever, at 20 Hz. Between → **inconclusive**, and the inconclusive
branch is *not* "try it on the watch": it is that the figure could only be
reported as a coarse ordinal, and the app has a rule against relabelling a coarse
thing as a fine one.

*What confirming unlocks:* the *right to ask* E-1, and nothing else. Confirming
D-2 does not produce a recovery figure; it produces permission to find out
whether the waveform exists.

*What refuting locks:* **the whole PPG-variability route, and this is the most valuable single
result in the review.** It closes a door left ajar in three documents — the
README, `Epoch.hpp`'s reserved HRV block and `SleepWakeScorer.hpp`'s "when a
producer appears" — at the cost of an afternoon and zero nights. The reserved
columns stay reserved (they cost nothing and they are honest about being absent),
but the sentence "when a producer appears, this is a scorer change" acquires a
second clause: *and even then, not at 20 Hz.*

*Preempted by:* nothing. **It runs before E-1**, deliberately, even though E-1
is cheaper — because a lower-case `p` from E-1 and a refutation from D-2 close the
same door, but only D-2's refutation survives a firmware update.

*Expiry:* a higher-rate PPG mode, which UNA have described as in progress. This
is why the row must record the *rate* it refuted at, not just the conclusion.

---

### D-3 — `kCountScale` from PSG rather than from a diary.

*Hypothesis:* running SleepLab's real `EpochCounter` over open raw wrist
acceleration recorded alongside PSG yields a `kCountScale` that maximises
epoch-by-epoch agreement with PSG, and that value is within a factor of three of
the value the ten diary nights will produce.

*Instrument:* public dataset. Free, days.

*Procedure:* compile `EpochCounter` as a host library (it is already pure C++17
with no SDK header, which is what makes this possible at all — the design
decision paying off in a way its comment did not anticipate). Run it over the
Walch dataset's raw acceleration at its native rate. Sweep `kCountScale`;
at each value, score with Cole-Kripke-plus-Webster and compare against the PSG
labels. Take the value maximising Cohen's kappa.

*The measurement:* kappa against PSG as a function of `kCountScale`; and the
location and sharpness of its maximum.

*Decision rule, written before the run:* a single interior maximum with kappa
≥ 0.5 → **confirm**, and that value becomes `kCountScale`'s **prior**, replacing
a guess with a PSG-anchored estimate. A flat surface, or kappa < 0.3 everywhere
→ **refute**; the count pipeline does not transfer across devices even with a
free scale, and the diary nights are the only route. A maximum at an endpoint of
the sweep → inconclusive; widen and re-run, free.

*What confirming unlocks:* **this is the card that changes what the ten diary
nights are for.** Today they are the only way to set the constant. With a
PSG-anchored prior they become a *check* on a value derived against ground
truth, which is a much stronger position — and it means the first five nights can
say something rather than nothing. It also gives the README's accuracy table a
second row: performance against PSG on somebody else's wrist, clearly labelled as
such, which is more than *synthetic-only*.

*What refuting locks:* the hope of borrowing calibration. It also weakens the
raw-accelerometer argument in §11.1 in an interesting direction: if this app's own
count pipeline does not transfer across devices, then neither does the claim that
raw-signal algorithms sidestep calibration — they would be inheriting the same
problem one layer down.

*The limitation that must be in the row:* the Apple Watch's accelerometer is not
this device's. Noise floor, mounting stiffness and delivered rate all differ, and
S14 says this pipeline's counts are rate-sensitive. So the result is a **prior**,
never a calibration, and the row must say the word.

*Preempted by:* nothing. It preempts the *search* half of C-4.

*Expiry:* B-1, which changes the pipeline the prior was measured through.

---

## 8. Group E — short hardware runs. Minutes on the wrist.

### E-1 — `PPG` resolves an app-facing driver on 1.4 firmware.

*Hypothesis:* `PPG` resolves a driver and delivers samples, so the raw waveform
is reachable from an app. Settles **S6**.

*Instrument:* short hardware run, ~3 minutes, calendar cost zero.

*Procedure:* `"ppg": "on"` in `Probe/probe.json`; launch; read the driver block
on the screen after one `M` row has been written; pull `probe_log.csv` over USB.
`ProbeConfig` already carries `ppgEnabled` and the log already carries `ppg_n`
and `ppg_ts_span_ms`, so nothing needs building.

*The measurement:* the case of `P` in the `ATMRHXbpoSLCE` block; `ppg_n` and
`ppg_ts_span_ms` for the first full row.

*Decision rule:* upper-case `P` **and** `ppg_n` > 0 → **confirm**, and
`ppg_n / (ppg_ts_span_ms/1000)` is the delivered rate, which is a second finding
for free and one S3 and S17 say not to assume. Lower-case `p` → **refute**.
Upper-case `P` with `ppg_n` == 0 → **inconclusive**: a driver that resolves and
delivers nothing is `HEART_BEAT`'s failure wearing a different hat, and the
follow-up is a ten-minute run before anything else is concluded.

*What confirming unlocks:* the right to build what D-2 has already said is or is
not honest. **Nothing about recovery is unlocked by this card alone.** Also:
storage arithmetic becomes real — 20 Hz × 8 h at 4 bytes a sample is ~2.9 MB a
night, which is 2 % of the raw accelerometer's 60 MB and entirely affordable, so
if the waveform exists it can be *recorded* even if it cannot yet be *used*.

*What refuting locks:* every PRV, HRV and cardiac-staging route on this
hardware. S6 flips to REFUTED, §11.4 collapses to its heart-rate-only and timing-only halves, and the reserved
`rmssd_x10`/`sdnn_x10`/`rr_count` columns become a memorial rather than a plan.

*Preempted by:* **D-2**, whose refutation makes this card's *positive* outcome
worthless. Run D-2 first even though this is cheaper.

*Expiry:* any firmware bump. UNA have described a higher-rate PPG mode and
on-chip HRV as things they are working on, so this row has an expiry date on
either outcome.

---

### E-2 — mute does not silence an app-requested alert.

*Hypothesis:* T1 as written. Five-minute experiment, already fully specified in
the ledger and in `POST-MORTEM.md` G3.

*Decision rule:* the watch muted, `alarm_at` two minutes ahead inside the
window: something audible or felt → **confirm**. Nothing, with an `alarm` line in
`Debug/sleeplab.log` → **refute**, and the alarm must be documented as
mute-suppressed. Nothing, with no `alarm` line → **inconclusive**, and it is an
app bug rather than a kernel behaviour.

*Why it is in this catalogue at all:* the smart alarm is the one feature that
sets the wearer's final wake time, and **an alarm night cannot validate
final-wake error**. So this has to be settled before the diary nights start, or
every diary night has to run with the alarm off — which is what §9's night plan
assumes.

---

### E-3 — is the accelerometer period a lever or a given?

*Hypothesis:* S3a. Two 3-minute runs at `accel_period_ms` 80 and 20; compare
`acc_n` against the 2 875 measured at 40.

*Decision rule:* all three within 10 % of 2 875 → **confirm** the period is
ignored, `kAccelPeriodMs` is decoration, and the sample rate is not a power
lever. Any run differing by ≥ 30 % → **refute**; the period works and the rate is
a lever. Between → inconclusive.

*What confirming unlocks:* nothing new, but it closes the last hope of reducing
the sample path's cost, which is 26× the budgeted IPC (S17). Given C-2 says the
whole app costs 13 % of the pack a day, the honest conclusion is that this no
longer matters — which is worth writing down, because it retires an optimisation
several documents still contemplate.

*What refuting locks:* nothing; it opens a lever.

---

### E-4 — a second table hour, on a schema-3 build.

*Hypothesis:* the ~12 mg in-band floor of §1.10 is the accelerometer's own noise
and not the surface the watch was resting on, and it is isotropic across the
three axes. Creates **S24**.

*Instrument:* short hardware run — **one hour on a table, no night**. Repeat it
twice on different surfaces and it is two hours.

*Procedure:* build 0.3.0 or later, which declares and writes schema 3 so
`count_x/y/z` exist (P15, fixed 2026-08-20). Copy
[`settings.table-hours.json`](settings.table-hours.json) to
`Apps/SleepLab/settings.json`. Leave the watch on a hard table in an empty room
for one hour; then repeat on something soft on the floor, noting the wall-clock
time each hour starts. Pull `Nights/watching.csv` afterwards and split it by
those times.

**The one way to waste this test:** `watching.csv` is written for every epoch
*inside* the bedtime window and for none outside it, so a table hour run at 14:00
against the default 21:00–11:00 window records **absolutely nothing**, and the
symptom is an empty file rather than an error. That is what the settings file is
for and why its window is the whole day. Read the driver block on the screen
before starting, as `ROLLOUT.md` phase 1 says: thirty seconds against an hour.

Both hours land in one `watching.csv` — it truncates on *entering* the window,
not per run — which is why the start times have to be written down. A session
will also open on the table now (`stillnessCountMax` = 900 is above the floor and
TOUCH_DETECT calls a table worn), and that is harmless: `watching.csv` records
continuously whatever the session state, a discarded night keeps its CSV, and a
table night fails the worn gate so it never reaches the baseline.

*The measurement:* `count_x`, `count_y`, `count_z` per 60 s epoch on each
surface: their medians, and the ratio of the largest to the smallest.

*Decision rule, written before the run:* the three axis medians within a factor
of 1.5 of each other **and** the two surfaces' total counts within 20 % →
**confirm**: it is sensor noise, it is a platform given, and §1.7's four
constants stand. One axis more than 3× another, **or** the two surfaces
differing by more than 50 % → **refute**: a material part of the floor is
environmental, the constants derived from one hard table are wrong, and the pair
has to be re-recorded on the surface the watch actually spends the night near.
Between → **inconclusive**, and the constants ship with the ledger row saying
they rest on one surface.

*What confirming unlocks:* the four constants of **C-6**, promoted from
provisional to measured; and the per-axis non-wear method of **B-3**, which needs
these columns and cannot run on the schema-2 recordings at all.

*What refuting locks:* the idea that a single table hour is a device
calibration. It would also mean the app's noise floor is partly a property of
where the wearer's nightstand is, which is a genuinely awkward finding and
better known than not.

*Preempted by:* **A-7**. If the range is a setting, run the hour at the narrowest
range instead.

*Expiry:* firmware, and any change to `EpochCounter`.

---

### E-5 — is the noise in the sensor or in the pipeline?

*Hypothesis:* the floor measured on `ACCELEROMETER` (0x10, float g) is present in
`ACCELEROMETER_RAW` (0x11, int16 raw) at the same magnitude, so it originates in
the BMI270 rather than in whatever produces the float stream. Creates **S25**.

*Instrument:* short hardware run, **five minutes on a table**. Needs a probe
build that subscribes 0x11 — the probe already has the shape for it, and this is
the probe's remaining job.

*Procedure:* subscribe both 0x10 and 0x11 for five stationary minutes. Record
per-axis sums for each, and the distinct raw values 0x11 emits.

*The measurement:* three numbers. (1) The smallest non-zero difference between
consecutive distinct int16 codes — the LSB, which gives the **configured
full-scale range** (0.06 mg/LSB is ±2 g, 0.24 mg/LSB is ±8 g). (2) The RMS of
0x11 converted to g. (3) The RMS of 0x10 over the same window.

*Decision rule, written before the run:* the two RMS figures within 20 % of each
other → **confirm**; the noise is the sensor as configured, and the fix is a
kernel configuration change (low-power averaging, `acc_filter_perf`). 0x10 more
than 2× the raw → **refute**; the float pipeline is adding it, which is a
different and much more fixable bug and one the app could in principle route
around by subscribing 0x11 itself. Between → inconclusive, and the follow-up is a
longer window.

*What confirming unlocks:* a precise firmware request — "run the BMI270's
accelerometer in performance filter mode, or raise low-power averaging, and tell
us what it costs in µA" — instead of "the noise seems high". Given the whole app
costs 1.2 mA (§1.2) there is budget to trade.

*What refuting locks:* the assumption that the app is seeing what the sensor
sees. It would also mean **every count ever recorded by this app is of a derived
stream**, which is a provenance statement that belongs in the ledger whatever
else follows.

*Preempted by:* nothing. It is the cheapest remaining question in the document
and it is upstream of any conversation with UNA.

*Expiry:* any firmware change to the sensor layer.

---

## 9. Group F — nights. The plan.

### H-BAND-1 — the restfulness band, which can only be refuted

*Hypothesis:* the four-level restfulness band's ordinal level bears **no**
relationship to any independent device's notion of how deep a sleeper was.
Settles **A3** in one direction only. Creates **A18** and forces **A19**.

*Instrument:* rides on nights 1–3, which are recorded anyway. Its marginal cost
is **wearing the Fenix on three nights that are already happening**, and reading
its stage sequence off the Garmin app in the morning.

*Procedure:* nights 1–3 with the Fenix on the other wrist. Each morning, record
the Fenix's per-30-minute stage sequence. Align to the SleepLab night by wall
clock. Collapse the Fenix's four stages to an ordinal depth (awake < light < REM
< deep — and note that placing REM on a depth axis is already a compromise the
band's own premise would refuse, which is part of why this experiment can only
refute). Compute Cohen's kappa, and Spearman's rho, between the band's level and
that ordinal, over epochs both devices scored as sleep.

*The measurement:* kappa and rho, pooled across the three nights, and per night.

*Decision rule, written before the nights:* pooled |rho| ≤ 0.1 **and** kappa
≤ 0.05 → **refute**, and the band's heart-rate term is deleted per A19. |rho|
≥ 0.3 → **inconclusive** — and this is the point of the card: a positive result
is *not* a confirmation, because two algorithms agreeing is evidence of a shared
input (both see movement) and not of either being right. Between → also
inconclusive.

**So the only outcome that changes anything is the refutation**, which is exactly
why the card is worth its zero marginal nights: it is the one experiment
available to this project whose most likely useful result is deletion.

*What confirming unlocks:* nothing. Stated plainly because the temptation to
read |rho| = 0.4 as vindication is the failure this card exists to forestall.

*What refuting locks:* the four-level band. It becomes a three-tone movement
strip drawn from `count` quantiles within the night, with wake in the accent
colour, and `RestfulnessBand::kMethod` is retired rather than bumped —
`kHrSettledX10` and `kHrRestlessX10` go with it, and the summary no longer needs
`usedHeartRate`.

*Preempted by:* nothing.

*Expiry:* none. A better reference would reopen it, and this project will not
get one.

---

### The mutually exclusive pairs

Named first, because a plan that cannot be executed is worse than no plan.

| These cannot share a night | Why |
| --- | --- |
| `hr: continuous` and `hr: off` | one setting |
| `hr: off` and the worn gate's plausibility check | with HR off the verdict is `Uncertain` and every sleep number is suppressed — so the HR-off night produces **counts and no sleep figures**, deliberately |
| `raw_recording: on` and a ten-night budget | 60 MB a night |
| SleepLab and Sleep Probe installed | S8, and it is not verified for two claimants |
| A night on the charger and a night | P8: plugging in terminates every app |
| **The alarm on and a final-wake measurement** | the alarm *sets* the final wake; the diary then validates the alarm's clock, not the scorer's |
| 24-hour recording and "charge before bed" | the charge has to move to a fixed daytime slot and becomes a known hole in the day |
| A table night and a worn night | the same window |
| **Recording a night and the constants being wrong** | at `stillnessCountMax` = 60 no session ever opens, so a night recorded before phase −1 yields counts and no scored night. Not mutually exclusive in the strict sense — but it makes every night before the fix a `Tools/` exercise rather than a working app |
| A schema-2 build and the per-axis columns | the two existing recordings cannot answer §1.10's isotropy question at all |

### The calendar

> **Re-ordered 2026-08-20, and this is the most consequential change in the
> revision.** Nights 1–12 as first written would each have produced a
> `watching.csv` and nothing else: no session, no score, no screen. They would
> not have been *wasted* — every count is on disk and the diary comparison is a
> `Tools/` exercise — but the wearer would have seen nothing for ten mornings and
> the whole calibration would have had to be reconstructed offline. **Phase −1
> now comes first, it costs no nights, and the data to do it is already on
> disk.**

| # | Night | Settings | Worn | Diary | Fenix | Settles |
| --- | --- | --- | --- | --- | --- | --- |
| **−1** | *(at a keyboard, ~1 h)* | — | — | — | — | **A-7** (grep), **C-6** (set the four constants from the two recordings), the two `night_report.py` blockers of §1.11. **No night proceeds until this is done** |
| **−0.5** | *(two table hours, daytime)* | `diagnostics: on`, schema-3 build | **no** | — | — | **E-4**. Confirms or refutes the floor the four constants were derived from. Costs two hours, not a night |
| 0 | *(evening)* | — | — | — | — | **E-2** (5 min), **E-3** (6 min), **E-1** (3 min) — all before bed, none costs a night |
| 1 | worn | `diagnostics: on`, `hr: continuous`, `alarm: off` | yes | **yes** | **yes** | C-1, C-3, C-4, C-5 (all as data); S1/S7/S8/S9/S10 for SleepLab rather than the probe; the count distribution for `ROLLOUT.md` phase 4 |
| 2 | worn | as 1 | yes | yes | yes | as 1 |
| 3 | worn | as 1 | yes | yes | yes | as 1, plus **H-BAND-1** (three Fenix nights is its minimum) |
| 4 | **table** | as 1 | **no — on a nightstand for the whole window** | note the times | no | the false-worn half of S7; **B-3**; `watching.csv`'s count distribution, which is what `night_report.py thresholds --table` wants. **Costs data, not sleep** |
| 5 | worn | `hr: off` | yes | yes | no | the cheap half of **S2** — what HR alone costs. Produces counts and **no sleep figures** |
| 6–12 | worn | as 1 | yes | yes | no | the remaining seven of the ten diary nights |
| D | **a full day** | window `00:00`–`23:59`, `diagnostics: on` | yes, 24 h | log every stillness > 20 min | no | **C-2b**, **B-2**'s real input, the daytime false-positive dataset, the standing daytime HR reference |

**The total: 11 nights of sleep, 10 of them with a diary, plus one bedtime window
on a table, two daytime table hours, one ordinary day — and about an hour at a
keyboard that has to happen before any of the nights are worth recording.**

**Eight of those ten were already committed** by `ROLLOUT.md` phase 5 before this
review existed. The marginal cost of this entire catalogue is therefore **one
night** (night 5, `hr: off`), **one window** (night 4, no sleep cost), **one day**
(D) and **nine minutes standing up** (night 0).

### What a reduced plan gives up

- **Drop night 5** (`hr: off`): S2's split stays unmeasured. Given C-2 says the
  whole app costs 13 % of the pack a day, the split is now a curiosity rather
  than a decision input. **This is the first night to cut.**
- **Drop nights 11–12** (eight diary nights instead of ten): `night_report.py
  diary` refuses to quote an accuracy figure below ten nights, by design. Cutting
  these means no accuracy figure at all, so **do not cut them** — they are the
  only thing that moves the validation table.
- **Drop day D**: §11.3 collapses. No nap detection (which §13 says not to build
  anyway), no daytime reference on a continuing basis, no circadian metrics ever.
  The 2026-08-19 file's daytime blocks are a one-off substitute for the HR
  reference and nothing else.
- **Drop the Fenix** on nights 1–3: H-BAND-1 becomes unrunnable and the band can
  then be neither confirmed nor refuted, which by §13's rule means delete it.

---

## 10. The unlock/lock map

### The map

| Experiment | Confirms → unlocks | Refutes → locks, permanently |
| --- | --- | --- |
| **C-6** four constants | a night that opens at all — everything downstream | nothing; the constants are already known to be wrong |
| **A-7** range/ODR | the floor is a given; every threshold sits above it for good | opens a lever that could recover the order of magnitude the app is short |
| **E-4** table hours | C-6's values promoted from provisional to measured; B-3 becomes runnable | the idea that one table hour is a device calibration; the pair is re-recorded |
| **D-2** 20 Hz PRV | the right to run E-1 meaningfully | **all PRV/HRV/recovery from PPG at 20 Hz.** Reopens only at a higher PPG rate |
| **E-1** PPG driver | the waveform columns, on-device PRV *if* D-2 confirmed | S6 → REFUTED; every cardiac-variability route. Reopens only on firmware |
| **D-3** PSG-anchored scale | `kCountScale` prior; diary nights become a check | borrowing calibration across devices; and the raw-signal argument weakens with it |
| **C-4** rescore sweep | A9, A10, the accuracy table, the whole validation column | the scalar bridge. Forces either a raw-signal scorer or a permanent "uncalibrated" label |
| **B-1** biquad | cross-night comparability; every multi-night metric | S14 becomes permanent; nights at different rates stay incomparable |
| **C-1** motion channel | a `kCountScale`-free cross-check on onset and wake | S13 stands; `motion`/`sig_motion` stay diagnostic |
| **D-1** bake-off | a scorer swap, and a rewrite of §12's ranking | the "1992 must be obsolete" suspicion. Cole-Kripke stands, with numbers |
| **C-2b** 24 h power | the day log, circadian metrics, the daytime reference | continuous daytime HR; falls back to `hr: duty` |
| **C-3** nadir stability | a described personal cardiac quantity | every "when did your heart rate bottom out" feature |
| **H-BAND-1** the band | *nothing* — see below | the four-level band |
| **A-3** primary sources | A8 → CONFIRMED | depends on what is wrong; the constants block is one edit, the input definition is not |

### The terminal nodes

Features that **no result from any experiment in this catalogue could unlock**,
written down as closed so they stop being somebody's someday.

| Closed | Because | Reopens only if |
| --- | --- | --- |
| **Sleep staging** — light/deep/REM, minutes-in-stage, a hypnogram | A2, from S5 and the absence of an `RR_INTERVAL` producer. **Not** from the PPG being single-channel, which A33 shows is false of the hardware | `HEART_BEAT` or `RR_INTERVAL` gains a firmware producer **and** the staging literature is re-read in writing, per A2's own clause. The row stays terminal on the beat-to-beat argument alone, which is the half that was always load-bearing |
| **SpO2, desaturation index, any apnoea proxy** | S4 REFUTED — no driver, `connect()` refused | a firmware producer for 0xF1 — and **this is a nearer door than it looked** (A32): the PAH8316 AFE is specified for SpO2 and the board carries red and IR emitters, so the silicon is present and the gap is a driver UNA have not written. Not a terminal node; a request |
| **Respiratory rate** | A6 | never from this wrist at these rates; a chest device is a different product |
| **Skin temperature** | A7 — `AMBIENT_TEMPERATURE` is ambient | a body-temperature sensor that does not exist |
| **A Body-Battery-equivalent recovery score** | requires beat-to-beat intervals; Firstbeat's method is built on them; the device produces none | **three** conditions together: E-1 confirms, D-2 confirms, *and* overnight wrist PRV is validated against something — and the third has no route on this project |
| **Any absolute population judgement** — "your resting heart rate is good", a 0–100 readiness number | A4, and it is a design decision rather than a hardware limit | never. This one is closed by the app's premise, not by the sensor set |
| **PSG-validated accuracy claims for this device** | there is no sleep laboratory here and there will not be one | never. D-3 gives PSG performance on *somebody else's* wrist and must be labelled that way |
| **Sleep satisfaction** (the S of RU-SATED) | it is self-report | a text entry field, which needs a keyboard this watch does not have |
| **Cosinor, IS, IV, L5/M10 from night-only data** | they are defined over 24-hour records; computing them from nights alone is invalid, not merely noisy | day D is adopted **and** ≥ 7 consecutive 24-h days are recorded, charge holes and all |
| **A validated nap detector** | §11.3 | an adult-validated diary-free method appears. GGIR's is evaluated in pre-schoolers |

---

## 11. Answering the five questions

### 11.1 Is Cole-Kripke still the right scorer? — Yes, provisionally, and for a better reason than the app currently gives.

The app's defence is that Cole-Kripke has published validation, ships with
Webster because that is how its figures were measured, and has two reference
implementations to check against. That is a real argument and it survives
contact with the objections, but not for the reason it states.

**The strongest support is external and recent.** Patterson *et al.* (2023),
reviewing forty years of actigraphy, report that simple regression-based and
heuristic algorithms slightly *outperform* complex machine-learning and deep
models on sleep-wake classification and outcome estimation. Palotti *et al.*
(2019) benchmarked classical and ML scorers on 1 743 participants and did not
produce a rout of the classical methods. So the prior that a 1992 algorithm must
be behind a 2021 random forest is not supported by the benchmarks.

**The strongest objection is not age — it is `kCountScale`, and §1.3 makes it
worse than the ledger says.** The app is applying coefficients through a guessed
scalar *and* a changed axis combination. A9 is one number; the mismatch it is
absorbing is two.

**Which candidates need raw acceleration, and is that as decisive as it sounds?**

| Candidate | Input | On this MCU? | Verdict |
| --- | --- | --- | --- |
| Cole-Kripke + Webster | counts, 60 s | yes, shipping | the incumbent |
| Cole-Kripke 30 s-max variant | 30 s counts | yes, trivially | **free second opinion; §1.4** |
| Sadeh (1994) | counts, 60 s, over an 11-minute window with a standard deviation and a natural log | yes; the `log` is the only cost | worth benchmarking (D-1); has its own unit assumptions, so it does **not** escape `kCountScale` |
| UCSD / Kripke (2010) | counts | yes | worth benchmarking |
| Oakley / Actiwatch | counts, 15 s epochs | yes, but the app records 30 s | the algorithm is a **technical report**, not a peer-reviewed paper, and I could not obtain it — see §13. Do not implement from a secondary description |
| van Hees z-angle (2015) / HDCZA (2018) | **raw acceleration**; the z-angle in 5 s windows | the angle is cheap; the 5 s windows over a night are cheap; but it needs the raw stream, i.e. 60 MB a night to record or an on-the-fly implementation | **the interesting one** |
| Sundararajan RF (2021) | raw acceleration, ~60 features, hundreds of trees | **no** — §2 | `Tools/` only, and see below |
| Biegański universal filter (2026) | counts from a **universal low-pass filter** across hardware and sampling rates; validated on 1 635 PSG co-registrations from three datasets, beating Cole-Kripke, Sazonov, Scripps, UCSD and Webster at p < 0.001 on the most relevant metrics | the filter is the same shape as `EpochCounter`'s | **the most relevant modern candidate, and it addresses this app's exact problem** |

**On whether a raw-signal algorithm really sidesteps `kCountScale`:** partly, and
less than it sounds. HDCZA works on the *angle* of the arm rather than its
acceleration magnitude, so it genuinely does not need a count-unit bridge — the
angle is in degrees and degrees are degrees. That is a real escape and it is the
strongest argument in this section. But it moves the calibration rather than
removing it: HDCZA's threshold is a 5° change over 5 minutes, and whether this
device's noise floor is above or below 5° is exactly the same *kind* of unknown
as `stillnessCountMax` being at 2 mg. The difference is that the new unknown is
**measurable without a diary** — leave the watch on a table for an hour and read
the angle noise — where `kCountScale` needs ten nights of sleep. That is a
genuine improvement in the *cost of finding out*, and it is why HDCZA earns a card
in the bake-off rather than a dismissal.

**On the trained models:** Sundararajan's random forest cannot be honestly
re-fitted by one person with a diary — n = 1, no ground truth, and the diary knows
lights-out and final wake and nothing in between. It is worth naming as the reason
a claim will never be made here, and nothing more.

**On epoch length:** §1.4 settles it. There is no published route to a 30 s
verdict from Cole-Kripke; the 30 s parameter set produces minute verdicts from a
maximum. The 30 s recording grid remains right for the file and buys a second
published scorer today, which is more than it was buying yesterday.

**Verdict.** Keep Cole-Kripke, add the 30 s-max variant as a second opinion, run
D-1 before spending the diary nights, and treat Biegański's universal filter as
the candidate most likely to displace it — because it is the only one whose
central claim is about the problem this app actually has.

> **Sharpened 2026-08-20.** §1.8 makes the case for keeping Cole-Kripke *stronger*
> and the case for the incumbent implementation *much weaker*, and the two should
> not be confused. The algorithm was never the problem: at the right scale it puts
> onset within 11 minutes of a channel that shares none of its constants (A28).
> What was wrong was a single guessed constant, by a factor of 11–18, in the
> direction that made sleep unreachable. **That is an argument for calibration,
> not for replacement** — and it is also the clearest possible illustration of
> why a scorer driven through an unmeasured bridge is not the paper it cites.
> It raises Biegański's relevance for a different reason than before: a method
> whose central claim is universality across hardware is attractive precisely
> because this device's bridge turned out to be wrong by an order of magnitude
> and nobody could have known without a stationary reference.

### 11.2 What a lab would compute and this app does not

| Metric | Class | Gated by | Notes |
| --- | --- | --- | --- |
| **Cole-Kripke 30 s-max as a second scorer** | **free now** | A-2 | §1.4. Disagreement fraction is a diary-free calibration signal |
| **Single-axis scoring** | **free now** | A-1, C-4 | §1.3 |
| **kRA / kAR fragmentation** (Lim 2011) | **free now** (arithmetic) / free later (nights) | C-5 | not comparable with published values, ever |
| **Nocturnal dip vs a personal daytime reference** | **free now** — the reference is on disk | C-3 | S20. Describable, not interpretable as recovery |
| **Nadir timing** | free now (`hr_min_at_pct` is already an index column) | C-3 | likely to be refuted; §1.5 |
| **Morning HR rise slope** | free now | C-3 | +15.8 bpm/h on the one night; no validation route |
| **van Hees per-axis SD non-wear** | free now | B-3 | an academic answer to the nightstand problem |
| **Sleep Regularity Index** (Phillips 2017) | **free later** — 14+ consecutive nights, no watch code | none | a **calendar** commitment: first value in a fortnight. Needs `index.csv` only |
| **Sleep midpoint, and its variability** | free later — 5+ nights | none | the cheapest regularity measure there is; ships with the SRI |
| **Social jetlag** (Wittmann 2006) | free later — needs work/free day labels | none | requires the wearer to label days, which is a diary column, not a sensor |
| **Bland-Altman + Menghini (2021) reporting** | **free now** as a methodology decision | D-1 | adopt the framework rather than inventing one |
| **DFA / scale-invariance** (Hu 2009) | free later | none | needs many hours and ideally 24-h data; the exponent is a population-level finding and one person's α is not interpretable. **Compute-and-trend only, or not at all** |
| **IS / IV / RA / L5 / M10** (Van Someren 1999) | **costs recording** — 24-h, multiple days | C-2b | invalid from night-only data. This is the 24-hour unlock that matters |
| **Total 24-hour sleep** | costs recording | C-2b | |
| **Cosinor** | costs recording | C-2b | and needs more days than the SRI does |

**On what a Fenix cross-reference can settle, since `ROLLOUT.md` leans on it.**
Less than the plan assumes. Chinoy *et al.* (2021) tested seven consumer devices
against PSG and found their sleep-staging performance materially worse than their
sleep-wake performance; a Fenix is a proprietary algorithm with its own biases and
no ground truth of its own. So:

- It **can** corroborate onset and final wake — two events a diary also knows,
  where two independent errors agreeing is weak but real evidence, and where
  §1.1's agreement to within 5 minutes across three independent methods is the
  best result in this document.
- It **cannot** validate total sleep time, WASO, efficiency or anything
  stage-like. Agreement there would mean the two algorithms share a bias, and
  disagreement would mean nothing at all.
- It therefore **cannot confirm the restfulness band** — only refute it, which
  is exactly H-BAND-1.

That is a finding that changes the plan: the Fenix belongs on nights 1–3 for the
band's refutation and for onset/wake corroboration, and it should be retired
after that rather than worn for ten nights.

### 11.3 Nap detection — do not build it

1. **Is a nap distinguishable from sedentary stillness on a wrist
   accelerometer?** Not reliably, and the open literature says so by omission.
   GGIR — the reference open toolkit for raw wrist accelerometry — carries nap
   detection whose own documentation states it has been evaluated **only in
   pre-schoolers** and explicitly asks for community help evaluating it in
   adults. That is the strongest available answer and it is a negative one. §1.1
   shows zero daytime false positives, but across an afternoon with 3 072 steps
   in it; the adversary is a still evening, and the honest position is that it is
   untested.
2. **What does 24-hour recording cost?** ~13.6 % of pack per day (§1.2). Not the
   obstacle. The charge schedule is.
3. **What does it unlock that naps do not?** This is the answer that matters:
   IS, IV, RA, L5/M10, cosinor, total 24-h sleep and a standing daytime HR
   reference are **all** gated on 24-hour data, and several of them are routinely
   computed from night-only data in the wild and are not valid that way.
   Round-the-clock recording is an enabling change for a class of metrics, not a
   feature for one — which raises its priority enormously and lowers nap
   detection's to zero.
4. **The smallest version:** a day log at 5-minute epochs with `hr: duty` at 60 s
   in 300 s, written to `watching.csv`'s existing format so the existing tooling
   reads it unchanged. Parameters to be confirmed by B-2 before anything is
   built. Storage ~50 KB a day, ~18 MB a year.

**So: build the day log, do not build the nap detector.** Report daytime
stillness as *stillness* if it is reported at all, which is what the app already
does for the night.

### 11.4 Recovery — what can honestly be said

**7.1** E-1, three minutes, and D-2 first.

**7.2** The published bound (Béres & Hejjel) puts 20 Hz *exactly* at the RMSSD
floor, with interpolation, in healthy young volunteers on clean awake signal.
Nocturnal RMSSD is often in the 20–50 ms range and 20 Hz is 50 ms of
quantisation. This is genuinely undecided and D-2 decides it for the cost of an
afternoon.

**7.3** With HR only: nocturnal minimum, mean, the dip against a personal daytime
reference, the nadir's timing, the morning rise. All computable; §1.5 computed
them. **All descriptive, none interpretable as recovery.** The search for
peer-reviewed free-living validation of "a bigger dip means better recovery"
returned vendor documentation and device marketing. That is not a gap in my
searching that a card can close — it is the state of the evidence, and the honest
consequence is that these numbers may be *trended against the wearer's own
nights* and may never carry the word recovery.

**7.4** The two-process model needs only timing, which the app has. And it fails
here for a reason the prompt anticipates: Process S is driven by sleep and wake
*history*, and this app's sleep/wake scoring is biased high by construction, so a
homeostatic trace built on it would under-predict sleep pressure systematically
and invisibly. It would also need day D to know the wake history at all. The
RU-SATED framing (Buysse 2014) is more useful and more honest: **five of its six
dimensions are already computable here** — regularity (SRI, free later),
timing (midpoint), duration, efficiency, alertness-by-proxy (no) — and
satisfaction is self-report and is a terminal node. Presenting the four that are
computable as four dimensions, each against the wearer's own baseline, is a
defensible framing that requires no new sensor and no new claim.

**7.5 The answer as asked.** **No.** This app cannot tell its wearer how
rejuvenating a night was, and if D-2 refutes, it never will.

What it *can* say, that a person would find useful:

```
LAST NIGHT
23:12 - 06:41
est 7h04  still 6h41
eff 89%  awake 3x
HR low 51  (-2 vs you)   dip 14% vs your days
[////▂▁▁▂▃▁▁▂██▁▁▁▂▃▂▁▁]
not sleep stages
```

One line added, and a caption that fits. The added line is the dip against that
wearer's own daytime reference — a real measurement, personal, trended, with no
interpretation attached.

The caption is short because it has to be, not because the band changed:
"movement & heart rate - not sleep stages" renders 340 px at Poppins Medium 16
and the row below the strip on a round panel is 164 px, so what actually reached
the wearer was about "movement & heart" — the inputs named and the disclaimer
dropped (P17). Of the two halves, the one worth twenty characters is the one
nothing else on the screen says. What the band is *made of* is in `kMethod`, in
every summary JSON, in the README and in the ledger.

**How the screen should say it so it does not read as a recovery score:** by
never producing a single number. A recovery score is a scalar; scalars invite
comparison with other people's scalars and with yesterday's. Four independent
lines that each name their own unit are not a score, and the app's existing
discipline — `est` next to `still`, a delta only against "you", nothing at all
below five nights — is already the right instinct. **Do not add a summary
figure**, however carefully it is labelled. That is the one change that would
undo the premise.

### 11.5 Where it is ahead, and where it is behind

**At or ahead of the state of the art.**

| Claim | Verdict |
| --- | --- |
| dt-weighted counts, filters re-coefficiented per sample | **Ahead in intent, short in execution, and it knows it.** This is the same problem Biegański *et al.* (2026) address with a universal low-pass filter, arrived at independently and earlier; ActiGraph's own counts are fixed-rate. But S14 measured that the one-pole implementation does not achieve the goal below half rate, and the header rejects the biquad on cost. **B-1 is what turns this from a good idea into a true claim** |
| per-axis integrals combined as a vector magnitude, rather than filtering `\|a\|` | Correct as engineering and **wrong as an input to Cole-Kripke** (§1.3). The two facts are independent and both need saying |
| suppression rather than annotation for unworn nights | **Ahead of every consumer device.** Chinoy (2021)'s seven devices annotate at best |
| the method string travelling with every figure | **Ahead of research tools too.** GGIR writes a config; per-figure method strings are rare anywhere |
| recording epochs finer than scoring epochs | **Ahead, and now cashed in** — §1.4 |
| the summary carrying the constants that scored its night | Ahead, and **inert until the rescore tool exists**. G4 |
| `-1` means not measured, never zero | Ahead, and unusual |
| **`watching.csv`, and G2's written prediction** | **The strongest single vindication in the project.** `POST-MORTEM.md` wrote down, in advance, that if the sensor's noise sat above `stillnessCountMax` no night would ever open and it would be indistinguishable from a wearer who stayed up — and added the file that would catch it. On the first night that file existed, that is exactly what happened, and it was diagnosable in an afternoon instead of costing a fortnight of blank mornings. No consumer device does this and few research tools do |

**Behind a £30 supermarket band.** Which of these actually matter:

| Behind | Matters? |
| --- | --- |
| **It cannot currently open a night, or score a minute of sleep** | **Yes — this now outranks everything else in the column.** A £30 band's numbers are wrong; this app's are absent. Both are fixable, and this one is fixable today from data already on disk (§1.7, §1.8) — but until it is, the comparison is not close |
| **No retrospective re-analysis** (G4) | **Yes, most of all** *of the remaining items*.| A cheap band silently reprocesses your history when its algorithm updates. This app records everything needed to do it properly and does not do it at all |
| **No 24-hour context** | **Yes.** It is what blocks the one recovery-adjacent number that is defensible (the dip), and every circadian metric |
| **No multi-night trend beyond a 28-night median** | **Yes.** SRI and sleep midpoint are free-later, need no watch code, and are better evidenced than most of what a band prints |
| A history that is a single index file | No. It is a deliberate trade with a documented reason |
| 100 buckets on a 240×240 panel | No. It is the panel |
| No export anyone else can read | **Marginal.** The CSV is a better analysis format than FIT; the loss is ecosystem, not science |
| **One wearer's calibration data forever** | **Yes — and D-3 is the answer.** A band is calibrated on thousands of wrists. This one is calibrated on none. Borrowing a PSG-anchored prior from an open dataset is the only route to that, and it costs no nights |
| No stages, no readiness score, no respiratory rate, no nap timeline | **No — this is the price of the honesty and it is worth paying.** All four are things a cheap band is confidently wrong about |

---

## 12. Ranked candidate additions

Ranked by **evidence per night spent**. Everything above the line costs zero
nights.

> **Re-ranked 2026-08-20.** Three entries were added at the top and nothing else
> moved. They are not additions to the app so much as the corrections without
> which none of the rest can be evaluated.

| # | What | Mechanism | Citation | Inputs | Class | Cost | Gated by |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **0a** | **Move the four movement constants above the noise floor** | §1.7: all four ship below 357 counts/60 s, so no night can open | — | the two recordings | **free now** | four constants | C-6, A-7 |
| **0b** | **Bring `kCountScale` to 0.0003–0.0005** and add a noise-floor offset | §1.8: at 0.0055 no epoch can score as sleep | Cole 1992 | the two recordings | **free now** | one constant + one new one | C-6, then C-4 |
| **0c** | **Raise `kMicroMovementFloor` 8 → ~400** | §1.9: at 8 the test cannot fail, so the nightstand gate is heart rate alone | — | the two recordings | **free now** | one constant | C-6 |
| **0d** | **Ask UNA for the BMI270's filter configuration** | §1.12: measured noise is 67× datasheet; low-power averaging and `acc_filter_perf` are the levers, and the kernel holds them | BMI270 datasheet | — | costs an email | none | E-5 first |
| 1 | **`night_report.py rescore`** | re-score a recorded night at arbitrary constants | — | epoch CSV + summary JSON | free now | ~200 lines | — |
| 2 | **Axis sweep in the rescore tool** | score from `count_x/y/z` as well as VM | Cole 1992 via `actigraph.sleepr` | epoch CSV | free now | in #1 | A-1 |
| 3 | **Cole-Kripke 30 s-max variant** | second published scorer over the same file | Cole 1992 | epoch CSV | free now | ~30 lines | A-2 |
| 4 | **`kCountScale` prior from PSG** | run the real `EpochCounter` over open raw acceleration with PSG labels | Walch 2019 | public dataset | free now | ~a day | D-3 |
| 5 | **Biquad band-pass** | fix the rate-sensitivity S14 measured | Biegański 2026 (same idea) | sample path | free now | ~60 lines + a test | B-1 |
| 6 | **Menghini reporting in `night_report.py diary`** | Bland-Altman, sensitivity/specificity, not just mean signed error | Menghini 2021 | diary + nights | free now | ~80 lines | — |
| 7 | **MOTION cross-check** | `kCountScale`-free onset/wake second opinion | — (no published method; this device's own channel) | epoch CSV | free now | ~40 lines | C-1 |
| 8 | **Nocturnal dip vs personal daytime reference** | night nadir against the wearer's own daytime p5 | descriptive; **no validated recovery interpretation** | epoch CSV + day log | free now (reference on disk) | ~20 lines | C-3 |
| 9 | **van Hees per-axis SD non-wear** | published non-wear alongside the bespoke gate | van Hees non-wear criterion (**unverified**, §13) | `count_x/y/z` | free now | ~40 lines | B-3 |
| 10 | **kRA / kAR** | rest↔activity transition probabilities | Lim 2011 | epoch CSV | free later | ~30 lines | C-5 |
| — | *— everything below needs nights or days —* | | | | | | |
| 11 | **Sleep Regularity Index** | probability the wearer is in the same state 24 h apart | Phillips 2017 | `index.csv` + epoch CSVs | free later, **14 nights of calendar** | ~50 lines | — |
| 12 | **Sleep midpoint + its spread** | the cheapest regularity measure | Wittmann 2006 (for the framing) | `index.csv` | free later, 5 nights | ~15 lines | — |
| 13 | **The day log** | 5-min epochs, HR duty-cycled, all day | — | new recording mode | costs recording | moderate | C-2b, B-2 |
| 14 | **IS / IV / RA / L5 / M10** | non-parametric circadian analysis | Van Someren 1999 | ≥ 7 × 24 h | costs recording | ~80 lines | #13 |
| 15 | **RU-SATED four-dimension framing** | four computable dimensions, each vs the wearer's own baseline | Buysse 2014 | `index.csv` | free later | screen work | #11 |
| 16 | **PRV / nocturnal RMSSD** | 20 Hz PPG + interpolation | Béres & Hejjel 2021 | PPG waveform | **blocked** | — | D-2 **then** E-1 |

---

## 13. Do not build

| Do not build | Why it fails | What would have to come back differently |
| --- | --- | --- |
| Sleep stages, or any relabelling of the band as one | **hardware** — A2, S5 | a firmware producer for `HEART_BEAT` or `RR_INTERVAL`, *and* A2 revisited in writing |
| A nap detector | **evidence** — the reference open implementation is evaluated in pre-schoolers only and asks for help with adults | an adult-validated diary-free method |
| A recovery score, a readiness number, a Body Battery equivalent | **hardware and honesty** — needs beat-to-beat intervals; and a scalar invites exactly the comparison the app exists to refuse | all three of E-1, D-2 and a validation route that does not exist |
| "Your heart rate dipped 14 %, so you recovered well" | **evidence** — the interpretation is vendor literature, not validated free-living science | peer-reviewed free-living validation of the dip as a recovery marker |
| Respiratory rate | **hardware** — A6 | nothing on this wrist |
| SpO2 anything | **hardware** — S4 REFUTED | a firmware producer for 0xF1 |
| Cosinor, IS, IV, L5/M10 from **night-only** data | **honesty** — they are defined on 24-h records and are invalid otherwise, however plausible the output | day D adopted and ≥ 7 consecutive 24-h days |
| A saturation ceiling on `kCountScale` before A9 is measured | **evidence** — A10's own reasoning, which is correct | C-4, which sweeps the ceiling as a parameter |
| An ML scorer re-fitted on this wearer's diary | **evidence** — n = 1, and a diary knows two events a night | nothing available to this project |
| A single summary figure for the night, however labelled | **honesty** — §11.4 | never; this is the premise |
| Implementing the Oakley/Actiwatch algorithm from a secondary description | **evidence** — it is a technical report I could not obtain; every description of it I found was second-hand | obtaining the report |
| Tightening the thin-epoch guards to a fraction of 48 Hz | **evidence** — S15 already decided this correctly | nothing; it is right as it stands |
| Any new metric at all, before the four movement constants move | **evidence** — no night opens, so nothing downstream has an input (§1.7) | C-6, which costs no nights |
| Trusting the worn gate on a night with `hr: off` | **evidence** — A29: the micro-movement half cannot fail at `kMicroMovementFloor` = 8, so the gate is the pulse alone | raising the floor to ~400, which C-6 proposes |

---

## 14. Ledger rows

In `FEASIBILITY-LEDGER.md`'s format, keyed to that document's own sections.
Dated 2026-08-20.

### For the ledger's §2 — sensors

| # | Claim | Tag | What would settle it | Date |
| --- | --- | --- | --- | --- |
| S19 | Continuous recording costs 13–15 % of the pack per 24 h, so round-the-clock recording is affordable and the constraint is the charge schedule rather than the battery. | **LIKELY** | 2026-08-20, by re-analysis of `Output/probe/probe_log_2026-08-19.csv`. Three independent blocks in one file: a 4.00 h afternoon at **1.25 mA**, a 3.98 h evening at **1.26 mA**, and the 8.45 h night at **1.18 mA**, from `batt_mah`. At 1.22 mA that is **~29.3 mAh per 24 h against the 216 mAh** the gauge reported at 100 %, so **~13.6 % of pack a day, ~6–7 days a charge**. LIKELY rather than CONFIRMED because no 24-hour block was recorded contiguously and `batt_mah` is integer (±0.25 mA on a 4 h block). Card C-2b confirms or refutes it, with ≤ 18 % confirming and ≥ 30 % refuting. | 2026-08-20 |
| S20 | A same-day daytime heart-rate reference, sufficient to express the nocturnal dip, already exists on the volume. | **CONFIRMED** | 2026-08-20. Runs 2 and 3 of `probe_log_2026-08-19.csv` carry **479 minutes of daytime heart rate** on the day preceding the night: p5 54.5 bpm, median 67.2. Against the night's nadir of 48.4 that is an **11.2 % dip**; against the night's median, 16.4 %. So `STATE-OF-THE-ART-PROMPT.md` §5's placement of the dip under "costs recording" is wrong for this wearer today, and right for every subsequent night until the day log exists. | 2026-08-20 |
| S22 | **The accelerometer's own in-band noise floor is ~374 activity counts per 60 s scoring epoch, and all four of the app's movement constants ship below it.** | **CONFIRMED** | 2026-08-20, from `Recordings/2026-08-20-table` — 106 rows, a stationary watch in an empty room, at the same 50.00 Hz the worn night delivered. Counts per 60 s epoch: **min 357, median 373, p95 389**, and the spread is 167–202 per 30 s across the whole hour, so it is a stationary process rather than an event. Against it: `stillnessCountMax` 60, `activityCountMin` 250, `kMovementFloor` 40, `kMicroMovementFloor` 8. **Consequence, measured on `Recordings/2026-08-19-worn`: a full night's sleep opened no session at all** — 1249 idle rows, no night, exactly as `POST-MORTEM.md` G2 predicted in writing and caught by the file G2 added. Calibrated against a replication of `EpochCounter`, 374 counts/60 s is ~**18.6 mg RMS** of broadband in-band acceleration, or ~12.4 mg of 1 Hz sinusoid — against the ledger's own statement that every threshold in this app lives between 0.3 mg and 9 mg. | 2026-08-20 |
| S23 | The accelerometer's noise floor cannot be changed **by an app**, but the levers exist and the kernel holds them. | **CONFIRMED**, and the first wording of this row was too fatalistic | 2026-08-20. `Service.cpp:167` constructs the sensor as `(Type::ACCELEROMETER, kAccelPeriodMs, kAccelLatencyMs)`; `SensorConnection`'s constructor takes type, period and latency and nothing else, and the accelerometer parser returns `float g`, so nothing is quantised app-side either. S3 and S17 already show the kernel ignores both parameters it does accept. **But the part is a Bosch BMI270** (S26), which has programmable range ±2 g…±16 g, programmable filter bandwidth 5.5–684 Hz, low-power averaging avg1…avg128 and a performance/power filter mode. So this is not "a platform given" — it is **a firmware request with register names behind it**, and at 1.2 mA for the whole app (S19) there is budget to trade for it. | 2026-08-20 |
| S25 | The floor is in the BMI270 rather than in the stream that reaches the app. | **UNVERIFIED** | 2026-08-20. The SDK documents two accelerometer channels — `ACCELEROMETER` (0x10, float g) and `ACCELEROMETER_RAW` (0x11, int16 raw) — and every count this app has ever recorded came from the first. Whether the second carries the same noise is unknown and separates "the sensor as configured" from "something in the pipeline". The int16 LSB also reveals the configured full-scale range, which nothing else here can. Card **E-5**: five minutes on a table. | 2026-08-20 |
| S26 | The accelerometer is a **Bosch BMI270**, and its measured in-band noise is ~67× its datasheet. | **CONFIRMED** for the part, **CONFIRMED** for the arithmetic | 2026-08-20, from `github.com/UNAWatch/una-hardware`, `una-watch/electronics/UNAlink_AG3335M` BOM: "BMI270 — IMU 3D accelerometer, 3D gyroscope, SPI and I2C", alongside a 3-axis magnetometer nothing uses. Datasheet noise density **160 µg/√Hz**; over the app's 0.25–3 Hz band that is **0.28 mg RMS** against the **18.6 mg RMS** measured on a stationary watch (S22). Quantisation contributes ~0.02 mg at 16-bit / 0.06 mg per LSB, so it is not that. **The consequence for A9 is worth stating on its own**: at datasheet noise the count floor would be ~6 rather than 374, and the *old* `kCountScale = 0.0055` put Cole-Kripke's boundary at 273 with the worn night's real movement at ~322 — so the original guess was roughly right for a sensor 67× quieter. It was not a careless number; it was a number derived from a datasheet the hardware is not meeting. Mechanism unresolved: low-power duty-cycling, `acc_filter_perf`, minimal averaging, or the 0x10 pipeline (S25). | 2026-08-20 |
| S27 | The MCU is an **STM32U5A5QJI6Q**. | **CONFIRMED** | 2026-08-20, `UNAcore` BOM: "32-bit microcontroller, Cortex-M33, 160 MHz, 4MB Flash, 2.5MB RAM, with SMPS, UFBGA132 package". §2 inferred Cortex-M33 with a single-precision FPU from the compiler flags; this is the part. The app's own RAM region is 512 KB of that 2.5 MB. | 2026-08-20 |
| S24 | The floor of S22 is the sensor rather than the surface, and it is isotropic. | **UNVERIFIED** | 2026-08-20. 18.6 mg RMS in a 2.75 Hz band is one to two orders of magnitude above what a modern MEMS part should produce, which makes "the table was vibrating" a live alternative that this data cannot exclude — an empty room in a building is not an inertial reference. The test is per-axis: sensor noise is roughly isotropic, building vibration is not. **`count_x/y/z` are exactly that test and both recordings are schema 2, which does not carry them.** Card **E-4**: one hour on a hard table and one on something soft, on a schema-3 build. Costs two hours and no night. | 2026-08-20 |
| S21 | `hr_trust_x10` varies systematically with activity, so it carries information beyond a quality flag. | **LIKELY**, with the direction of the scale **UNVERIFIED** | 2026-08-20, re-analysis. Median 29 across the still night, 12 and 15 across the two active daytime blocks, and 13 in the hour the wearer got up. The pattern is unambiguous; what higher *means* is not documented anywhere this review found. Card A-6 is a grep of the SDK header. **Do not threshold it on the empirical reading alone** — an inverted scale would make the worn gate trust exactly the readings it should distrust. | 2026-08-20 |

### For the ledger's §3 — sleep science

| # | Claim | Tag | Basis, and its limits |
| --- | --- | --- | --- |
| A32 | **SpO2 is a hardware capability of this watch. The gap is firmware.** | **CONFIRMED** | 2026-08-20, from the `UNAbody_PAH8316` BOM: the AFE is a PixArt **PAH8316LS-IN**, described as "PPG AFE for HRD and **SpO2**, 6 LED drivers, 4 Photodiode inputs, IR touch detection, DC Lead On/Off Detection", on a board carrying 2× SFH 7016 (green, red **and infrared** emitters) and 4× SFH 2703 photodiodes. Red and IR are exactly what pulse oximetry needs. **S4 is not wrong** — `connect()` really is refused and there really is no producer — but its wording, "there is no firmware producer to ask", reads as though the sensor does not exist, and this review repeated that by writing SpO2 into the terminal nodes as permanently closed. It is not permanently closed; it is unwritten. The reopening condition changes from "hardware that does not exist" to "a driver UNA have not shipped", which is a request someone can actually make. |
| A33 | **The PPG is not single-channel.** | **CONFIRMED** for the hardware; the firmware's exposure is untested | 2026-08-20, same BOM: four photodiode inputs, six LED drivers, three wavelengths. `README.md`, `RestfulnessBand.hpp` and ledger **A2** all support "no staging, ever" partly with *"the PPG waveform is single-channel"*, sourced from a maintainer's remark about what the firmware delivers. As a statement about the silicon it is false. **A2's conclusion probably survives** — staging also needs beat-to-beat intervals and S5 stands, unchanged and independently — but the clause has to be rewritten to say what it actually means, which is that the *app-facing* waveform is one channel and that even that is UNVERIFIED (S6). A ledger that supports a permanent refusal with a false hardware claim invites the reader to distrust the rows that are right. |
| A34 | **`TOUCH_DETECT` is infrared, not capacitive.** | **CONFIRMED** | 2026-08-20, same BOM: the PAH8316's feature list includes "IR touch detection", and there is no capacitive sensing part on any of the six boards. `README.md` reasons about "a capacitive sensor [that] can report 'worn' for a watch face-down on a duvet". The *conclusion* held and was measured — 106 straight table epochs reported worn (A29) — but the mechanism is optical reflectance, and it explains the observation better than capacitance does: an IR emitter pointed at a pale table gets its light back. It also predicts different failure modes, which matters for any future work on the worn gate: a matte dark surface may read not-worn where a pale one reads worn, a gap between watch and skin matters more than dielectric contact, and ambient infrared is a confounder that capacitance would not have. | 2026-08-20 |
| A26 | **At the constants it ships with, this app cannot score any epoch as sleep on this hardware.** | **CONFIRMED** | 2026-08-20. `SleepWakeScorer.hpp` states its own operating point — *"the sleep/wake boundary D = 1 falls at about 273 counts per scoring epoch"* — and S22 measures the floor at 357 minimum. So the scorer's entire sleep region lies below the noise floor. Verified by driving the real algorithm (seven weights, P = 0.001, all five Webster rules) over the 624 scoring epochs of `Recordings/2026-08-19-worn`: **0 epochs scored sleep**, and 0 of the table hour's 53 as well. This is not "the correspondence to sleep is unestablished" (A9); it is that sleep is not in the codomain. **The two numbers were both already written down in this repository and had never been multiplied together, because until 2026-08-20 there was no measurement of the floor.** |
| A27 | `kCountScale` is wrong by a factor of 11–18, not by a factor of two. | **LIKELY** | 2026-08-20. Sweeping the real algorithm over the worn night: the night becomes plausible in shape — 7 h 18 to 8 h 31 of sleep in a 10 h 24 record, efficiency 70–82 %, 20–33 awakenings — at **0.0003–0.0005** against the shipped **0.0055**. LIKELY, not CONFIRMED, because "plausible in shape" is not a diary and one night is one night. What raises it above a guess is A28. **This bracket did not exist before the two recordings and it changes what the ten diary nights are for**: from finding a value to confirming one. |
| A28 | Two channels that share no constant agree on where this night's sleep began and ended. | **LIKELY** | 2026-08-20, on `Recordings/2026-08-19-worn`, which has no diary. The MOTION-event rule of A15 — threshold and run length fixed on a different night recorded by a different app, unchanged — gives onset **23:38** and final wake **08:59**. Cole-Kripke at the A27 bracket gives **23:42–23:49** and **08:57–08:58**. Agreement to 11 minutes and 2 minutes between a channel that uses no activity counts and one that uses nothing else. Not proof: the scale was chosen partly by looking for a plausible night, so the two are not fully independent. It is the strongest corroboration available without a diary and it is why C-1 is now a calibration check rather than a curiosity. |
| A29 | **The nightstand gate's micro-movement half is currently a test that cannot fail, so the gate is heart rate alone.** | **CONFIRMED** | 2026-08-20, from the worn/table pair. On the table hour: `worn_pct` median **100** — TOUCH_DETECT calls a table worn, all hour, exactly as the README predicted a capacitive sensor would; every epoch above `kMicroMovementFloor` = 8, which sits 45× below the floor; and 100 % of epochs below any `stillnessCountMax` that would let a real night open, so **a corrected segmenter will open a session on a nightstand**. One channel separated the two recordings completely: **1249/1249 worn rows carry a heart-rate sample and 0/106 table rows do.** The README's *"either alone is defeatable"* is now *"one of the two is already defeated"*, and a night with `hr: off` or a failed optical path would report furniture as a flawless night. **The fix is measured**: `kMicroMovementFloor` from 8 to ~400, between the table's p95 (389) and the worn night's p5 (414). |
| A30 | The separation between a sleeping wrist and a table, on the count channel, is 6.5 % wide. | **LIKELY** | 2026-08-20. Table p95 389, worn p5 414. At a floor of 400: **0 of 53 table epochs pass and 1.8 % of 624 worn epochs fail**, which `kMinPlausiblePct` = 70 absorbs. So `ROLLOUT.md` phase 4's "NO CLEAN VALUE — the distributions overlap" branch is **not** taken, but only just, and on one table hour against one night. The worn night's *minimum* (370) is below the table's p95 (389), so the overlap is real at the extreme tail. LIKELY, and E-4 is what would harden or break it. |
| A31 | The noise floor is not an artefact of the per-sample re-coefficiented filter. | **REFUTED**, and recorded because it is the obvious first suspect | 2026-08-20. Hypothesis: the one-pole high-passes are re-coefficiented from each sample's own dt, the input carries a full 1 g of gravity, and `sumGs` accumulates the *rectified* output so a DC residue cannot cancel — therefore gravity leaks into the passband and manufactures the floor. Tested by replicating the exact filter chain and integration in Python and feeding it a perfectly constant 1 g: at a regular 20 ms, at alternating 20/40 ms, at uniform 18–22 ms jitter, and with quantisation added, the count is **0.00 per 30 s** in every case. The filter is clean. The floor is real acceleration, and S24 is about what is producing it. |
| A13 | Cole-Kripke's published coefficients were fitted on **single-axis** counts under the transform `min(axis1/100, 300)`, and this app feeds them the **vector magnitude** of three per-axis integrals. | **CONFIRMED** | 2026-08-20, by reading `actigraph.sleepr`'s source, which applies `pmin(axis1/100, 300)` and documents *"the Cole-Kripke algorithm uses the y-axis (axis 1) counts"*, citing p. 466. So `kCountScale` bridges an axis-combination change as well as a unit scale. A scalar can absorb the mean of the VM-to-axis ratio; it cannot absorb its variance, and the ratio varies between 1 and √3 with the direction of movement, hence with sleeping posture. `count_x/y/z` are recorded (schema 3), so the fix is a rescore, not a night. **This contradicts nothing in the ledger and qualifies README's "ActiGraph's own vector magnitude counts"**, which is true and, placed next to the Cole-Kripke citation, implies something that is not. |
| A14 | Cole *et al.* published three parameter sets, and the 30-second one scores each **minute** from the maximum of that minute's two 30 s sub-epochs — so it is computable from every night this app has ever recorded. | **LIKELY** | 2026-08-20. One reference implementation, `actigraph.sleepr`, carries all three sets cited to p. 466, with the 30 s one commented *"the optimal parameters for the maximum 30-second nonoverlapping epoch of activity per minute"*: P = 0.0001, weights (50, 30, 14, 28, 121, 8, 50). LIKELY, not CONFIRMED, because it rests on one implementation and not the paper — A-3. Consequence: a second published scorer at zero recording cost, and the **fraction of minutes on which the two variants disagree** is a diary-free measure of how badly `kCountScale` is placed. |
| A15 | The 2026-08-19 night can be scored, coarsely, from `motion_mot`. | **LIKELY**, and the threshold was fitted post hoc | 2026-08-20, re-analysis. A ten-consecutive-minute quiet rule over MOTION events gives onset 00:36–00:38 against a diary's 00:33 and a Fenix's 00:37, and final wake 08:00 (threshold ≤ 1) against a diary's 08:00 and a Fenix's 08:02. Zero false positives across 479 minutes of daytime wear the same day. **Three qualifications belong in this row and not in a footnote:** the threshold was chosen after seeing the answer; moving it by one event moves final wake 18 minutes; and the daytime blocks carried 3 072 steps, so the sedentary adversary is untested. Card C-1 is the pre-registered version, and it rides on nights already committed. |
| A16 | SleepLab records the channel that produced A15. | **CONFIRMED** | 2026-08-20, desk. `Epoch::motionEvents` and `Epoch::sigMotion`, columns `motion` and `sig_motion`, schema 3, written every 30 s regardless of the diagnostics setting. |
| A17 | **S13 is too strong as written.** The 2026-08-19 night is unscoreable *by Cole-Kripke*; it is not unscoreable. | **CONFIRMED** as a correction to S13's wording | 2026-08-20. S13's substance stands — the probe records no activity counts, and the count distribution the movement thresholds need does not exist in a probe night. What does not stand is *"cannot be scored"* and `POST-MORTEM.md`'s "half-wasted": see A15. The best-referenced night this project will get is recoverable to within five minutes at both ends, by a channel SleepLab already writes. |
| A18 | **No experiment available to this project can confirm the restfulness band.** It can only be refuted. | **CONFIRMED** as reasoning | 2026-08-20. There is no PSG and there will not be one. A Garmin Fenix is a proprietary algorithm with its own biases and no ground truth of its own; Chinoy *et al.* (2021) found consumer devices' stage-like output materially worse than their sleep-wake output. So agreement between the band and a Fenix's stages would evidence a shared bias, and disagreement would evidence nothing — an asymmetry that permits refutation and forbids confirmation. By the standard this project applies elsewhere, a metric that can never leave *speculative* should not be shown. See card **H-BAND-1** and the recommendation in A19. |
| A19 | The band's heart-rate term should be **deleted** and the strip demoted to what is measured. | **UNVERIFIED**, and it is a recommendation rather than a finding | 2026-08-20. `kHrSettledX10` = 2 bpm and `kHrRestlessX10` = 8 bpm above the night's own minimum are guesses about a channel whose nadir on the one night available landed 14 minutes after onset with heart rate rising monotonically thereafter (§1.5) — so the reference the band is relative to is itself unstable. A strip drawn from `count` quantiles within the night, with wake in the accent colour and the caption *"not sleep stages"* — which is what it now says, because the longer wording never fitted the glass (P17) — makes exactly the claim the numbers make, needs no unvalidatable constant, and removes the summary's need to write `usedHeartRate` at all. **This is a proposal, not a measurement**, and H-BAND-1 is what would force it. |
| A20 | There is no validated diary-free nap detector for adults in the open literature this review found. | **LIKELY** | 2026-08-20. GGIR — the reference open toolkit for raw wrist accelerometry — ships nap detection whose own documentation states it has been evaluated only in pre-schoolers and explicitly asks for community support to evaluate it in adults. Absence of evidence in one toolkit's documentation is not proof; it is the strongest available signal, and it is enough to keep nap detection off the roadmap. |
| A21 | A 20 Hz PPG waveform is at the published lower bound for RMSSD, and only with interpolation. | **LIKELY** | 2026-08-20. Béres & Hejjel report RMSSD within 5 % relative error at 50 Hz undecimated, and at 50 ms (20 Hz) sampling **with** interpolation; SDNN reaches 10 Hz with interpolation. Their subjects were healthy young volunteers with normal variability on clean signal, and they note the required rate rises where variability is reduced. Nocturnal RMSSD is frequently in the 20–50 ms range, which is the same order as 20 Hz's quantisation. **So the route is neither dead nor honest yet**, and card D-2 — a public-dataset experiment costing an afternoon and no watch — is what decides it. This row expires on any higher-rate PPG mode. |
| A22 | The timing of the nocturnal heart-rate nadir is a stable enough personal quantity to report. | **UNVERIFIED**, and likely to be refuted | 2026-08-20. On the one night available the 15-minute-smoothed nadir fell at **6 % of the night**, 14 minutes after sleep onset, with heart rate rising monotonically to +4 bpm by 07:16 and +11 bpm in the final hour. `index.csv` already carries `hr_min_at_pct`. Card C-3 sets the rule before the nights: IQR ≤ 20 points confirms, ≥ 40 refutes. |
| A23 | No recovery interpretation of any heart-rate quantity available here is supported by peer-reviewed free-living validation. | **LIKELY** | 2026-08-20. A literature search for validation of the nocturnal dip as a recovery marker returned vendor documentation and device marketing. Firstbeat's method — what Body Battery is — is documented in white papers rather than the peer-reviewed literature and rests on beat-to-beat intervals this device does not produce. Consequence: the dip, the nadir, the mean and the morning rise may be reported as **descriptions**, trended against the wearer's own nights, and none of them may carry the word *recovery*. |
| A24 | Simple heuristic and regression scorers are not behind modern machine-learning scorers on sleep-wake classification. | **LIKELY** | 2026-08-20, from the review literature rather than from reproduction. Patterson *et al.* (2023) report simple regression-based and heuristic algorithms slightly outperforming complex ML and deep models on sleep-wake classification and outcome estimation; Palotti *et al.* (2019) benchmarked both families on 1 743 participants without a rout of the classical methods. Card D-1 tests this on data where ground truth exists, on counts produced by **this app's own** `EpochCounter`, which is the only version of the question that binds here. |
| A25 | The most relevant modern challenger to Cole-Kripke on this device is a universal filter rather than a model. | **LIKELY** | 2026-08-20. Biegański *et al.* (2026) replace per-device smoothing coefficients with a universal low-pass filter *"applicable to wide ranges of recording hardware and sampling rates"*, verified on 1 635 overnight PSG co-registrations across three datasets, reporting significantly higher concordance than Cole-Kripke, Sazonov, Scripps, UCSD and Webster at p < 0.001 on the most relevant metrics. That is this app's exact problem — a device whose delivered rate is neither requested nor constant (S3, S17) — and `EpochCounter`'s per-sample re-coefficienting is an independent, earlier and **less complete** version of the same idea (S14). Not reproduced here; card D-1. |

### And one row for the ledger's §1 — platform and build

| # | Claim | Tag | Method | Date |
| --- | --- | --- | --- | --- |
| P17 | **Two widgets were drawn partly outside the round glass, and the text in them was wider than the panel at any position.** | **CONFIRMED**, and fixed | 2026-08-20. `UNAview_LS012` is a round 240×240: the framebuffer is square and the glass is not. `mLine[0]` (x 26–214 at y 30) lost 14.6 px each side against a visible chord of 40.6–199.4, and `mCaption` (x 20–220 at y 188–208) lost 17.4. **The clipping was the smaller half.** Measured against the advances in `Table_Poppins_Medium_16_2bpp.cpp`: `"UNCONFIRMED - no sleep data"` is 243 px in a 188 px box, `"INTERRUPTED - clock changed"` 239, and the caption **340 px in a 200 px box**. Compounded, the honesty line showed 159 px of 243 and the caption 167 px of 340 — so the caption rendered as about "movement & heart", naming the strip's inputs and dropping the disclaimer, which is the one thing it exists to do. Fixed by deriving every box from the chord (`gui/common/RoundPanel.hpp`), splitting the honesty line into marker and reason rows, and shortening `kCaption` to "not sleep stages"; `kMethod` deliberately untouched, so nights already on disk stay comparable. **Why it shipped:** `MainView.hpp` includes TouchGFX and the host suite does not have TouchGFX, so nothing could assert anything about the layout. The constants and the geometry are now in plain headers and `Tests/RoundPanel_test.cpp` pins them — asserting content against geometry rather than geometry against itself, the first version of that test having been tautological. Correctness rather than science, and therefore `EVALUATION-PROMPT.md`'s; recorded here because §11.4's answer to "what can this app honestly say" is a screen, and a screen that cannot draw its own caveat is not the answer it looks like. | 2026-08-20 |
| P16 | `night_report.py thresholds` cannot read either of the two recordings that exist to feed it. | **CONFIRMED** | 2026-08-20, by running it. Two independent blockers. (1) `load_nights()` applies `skip = {"index.csv", "watching.csv"}` to **every** path including ones named explicitly on the command line, while the comment three lines above it documents `--table ./nights/watching.csv` as the supported way to read a noise floor; the file is discarded and the script exits `no epoch rows found`, naming neither the file nor the reason. (2) Past that, the script correctly and loudly refuses the files because they are schema 2 and `EPOCH_COLS` tracks schema 3. The refusal is the app's own rule working as designed; the effect is that `ROLLOUT.md` phase 4 cannot be run on the only data it has. Correctness rather than science, and therefore `EVALUATION-PROMPT.md`'s — recorded here because it blocks the entire path from these recordings to the constants. | 2026-08-20 |
| P15 | `kEpochCsvSchema` and the epoch CSV's own header disagree. | **CONFIRMED** | 2026-08-20, desk. `NightStore.hpp:127` declares `kEpochCsvSchema = 2`; `NightStore.cpp:35` writes `"schema 3"` into the header comment and the column list carries `count_x/y/z`; `NightStore.cpp:719` writes the constant into every summary as `epochs_csv_schema`. So a reader obeying this app's own rule — refuse an unrecognised schema — will refuse a schema-3 file that declares itself schema 2, or accept it and silently miss three columns. Correctness rather than science, and therefore `EVALUATION-PROMPT.md`'s, but recorded because §15's rescore tool keys off that number. | 2026-08-20 |

---

## 15. `night_report.py rescore` — the tool proposal

`POST-MORTEM.md` G4 calls this the highest-value unbuilt tool and it is right,
and §1.3 and §1.4 make it more valuable than G4 assumed: it is no longer a
one-parameter sweep.

```
night_report.py rescore ./nights --diary diary.csv
                       [--scale 0.0005:0.05:log40]
                       [--input vm,x,y,z,maxaxis]
                       [--ceiling none,100,300,1000]
                       [--variant 1min,30sec-max]
                       [--report surface|best|per-night]
```

**What it must do.**

1. **Read a night and re-derive its scoring epochs from the CSV**, not from the
   summary — the summary is the output being questioned. Sum pairs of recording
   epochs for `1min`; take `max` of the pair for `30sec-max` (§1.4).
2. **Read schema 2 as well as schema 3**, keyed off each file's own header row,
   and refuse anything else while naming the file, the schema it found and the
   schema it expected. Two irreplaceable recordings are schema 2 (§1.11) and a
   tool that cannot read them is a tool that cannot set the constants they exist
   to set. See P15: it will encounter this on day one.
3. **Not discard a file the user named explicitly.** `load_nights()`'s
   `skip = {"index.csv", "watching.csv"}` currently applies to command-line paths
   as well as to directory scans, which contradicts the comment directly above it
   and makes the documented `--table ./nights/watching.csv` invocation fail with
   `no epoch rows found`. The skip set belongs on the directory-scan branch only.
   `rescore` must not inherit the same bug — and `thresholds` should be fixed
   first, because it is the tool phase 4 needs *now*.
4. **Re-implement Cole-Kripke and Webster from `PublishedConstants`**, read out of
   the header rather than transcribed a third time — a fourth copy of the
   constants is a fourth thing to get wrong. A ctest that scores one night with
   the tool and with the C++ engine and asserts they agree epoch-for-epoch is the
   only thing that keeps them honest, and the repo already has the shape for it
   (`night-log-export` plus a ctest parsing it with the real script).
5. **Sweep the five axes** and report the surface, not just the winner. A single
   number with no sense of how sharp its minimum is has told you nothing.
6. **Apply the pre-registered decision rule from C-4** and print it *before* the
   result — minimise the sum of absolute mean signed errors on onset and final
   wake, subject to IQR ≤ 1.5× the best anywhere, rejecting any point whose median
   efficiency exceeds 97 %. Print the rule, then the winner, then whether the
   winner satisfied the rule. A tool that prints the rule after the answer is a
   tool that will be argued with.
7. **Refuse to quote a calibration off fewer than ten nights**, exactly as
   `diary` already does, and exclude nights the worn gate suppressed, saying how
   many and why.
8. **Report Menghini's set**, not just mean signed error: sensitivity to sleep,
   specificity to wake, accuracy, and Bland-Altman bias and limits of agreement
   on total sleep time and WASO. This is the standard the field has agreed on and
   inventing a private one is a worse use of ten nights.
9. **Print what it could not do.** A night with no diary row, a night at an
   `acc_hz_x10` more than 20 % from the others (S14 — they are not comparable),
   a night that was resumed. Named, counted, excluded.

**What it must not do.** Choose the winner by anything not printed first; quote a
figure it does not have the nights for; or silently drop a night.

**Cost.** ~200 lines of standard-library Python, and it turns ten recorded nights
into an unbounded number of experiments — which is the difference between one
attempt at A9 and as many as you like.

---

## 16. Citations

Everything cited above. **Unverified entries are marked as such and were not
relied on for any decision rule.**

**Scoring algorithms**

- Cole RJ, Kripke DF, Gruen W, Mullaney DJ, Gillin JC. Automatic sleep/wake
  identification from wrist activity. *Sleep* 1992;15(5):461–469.
  <https://doi.org/10.1093/sleep/15.5.461> — **primary source not read.** Every
  claim about its contents here (weights, P, window, threshold direction, the
  three parameter sets, the count transform) is via the two reference
  implementations below, which both cite p. 466. This is ledger row A8 and card
  A-3.
- Webster JB, Kripke DF, Messin S, Mullaney DJ, Wyborney G. An activity-based
  sleep monitor system for ambulatory use. *Sleep* 1982;5(4):389–399.
  <https://doi.org/10.1093/sleep/5.4.389> — DOI from index metadata; **primary
  source not read.**
- Sadeh A, Sharkey M, Carskadon MA. Activity-based sleep-wake identification: an
  empirical test of methodological issues. *Sleep* 1994;17(3):201–207.
  <https://doi.org/10.1093/sleep/17.3.201>
- Kripke DF, Hahn EK, Grizas AP, *et al.* Wrist actigraphic scoring for sleep
  laboratory patients: algorithm development. *J Sleep Res*
  2010;19(4):612–619. <https://doi.org/10.1111/j.1365-2869.2010.00835.x>
- Oakley N. *Validation with polysomnography of the Sleepwatch sleep/wake scoring
  algorithm used by the Actiwatch activity monitoring system.* Technical report
  to Mini-Mitter Co., 1997. — **UNVERIFIED.** Widely cited; I could not obtain
  it. Every description of the algorithm I found was second-hand. §13 says do not
  implement it from those.
- van Hees VT, Sabia S, Anderson KN, *et al.* A novel, open access method to
  assess sleep duration using a wrist-worn accelerometer. *PLoS ONE*
  2015;10(11):e0142533. <https://doi.org/10.1371/journal.pone.0142533>
- van Hees VT, Sabia S, Jones SE, *et al.* Estimating sleep parameters using an
  accelerometer without sleep diary. *Sci Rep* 2018;8:12975.
  <https://doi.org/10.1038/s41598-018-31266-z>
- Sundararajan K, Georgievska S, te Lindert BHW, *et al.* Sleep classification
  from wrist-worn accelerometer data using random forests. *Sci Rep*
  2021;11:24. <https://doi.org/10.1038/s41598-020-79217-x>
- Biegański P, Duszyk-Bogorodzka A, Wołyńczyk-Gmaj D, Gmaj B, Durka P. Universal
  approach to actigraphic sleep/wake scoring, verified against 5 classic
  algorithms on 3 datasets. *Sci Rep* 2026;16(1):17878.
  <https://doi.org/10.1038/s41598-026-45568-0> (PMID 41998113) — abstract and a
  departmental summary read; **full text not read.**

**Benchmarks, reviews and reporting standards**

- Palotti J, Mall R, Aupetit M, *et al.* Benchmark on a large cohort for
  sleep-wake classification with machine learning techniques. *npj Digit Med*
  2019;2:50. <https://doi.org/10.1038/s41746-019-0126-9>
- Patterson MR, Nunes AAS, Gerstel D, *et al.* 40 years of actigraphy in sleep
  medicine and current state of the art algorithms. *npj Digit Med* 2023;6:51.
  <https://doi.org/10.1038/s41746-023-00802-1>
- Menghini L, Cellini N, Goldstone A, Baker FC, de Zambotti M. A standardized
  framework for testing the performance of sleep-tracking technology:
  step-by-step guidelines and open-source code. *Sleep* 2021;44(2):zsaa170.
  <https://doi.org/10.1093/sleep/zsaa170>
- Chinoy ED, Cuellar JA, Huwa KE, *et al.* Performance of seven consumer
  sleep-tracking devices compared with polysomnography. *Sleep*
  2021;44(5):zsaa291. <https://doi.org/10.1093/sleep/zsaa291>

**Data**

- Walch O, Huang Y, Forger D, Goldstein C. Sleep stage prediction with raw
  acceleration and photoplethysmography heart rate data derived from a consumer
  wearable device. *Sleep* 2019;42(12):zsz180. — article id verified; **DOI
  `10.1093/sleep/zsz180` inferred from the article id and not confirmed.**
  Dataset: *Motion and heart rate from a wrist-worn wearable and labeled sleep
  from polysomnography*, PhysioNet v1.0.0, 31 subjects.
  <https://physionet.org/content/sleep-accel/1.0.0/>

**Circadian, regularity and fragmentation**

- Van Someren EJW, Swaab DF, Colenda CC, Cohen W, McCall WV, Rosenquist PB.
  Bright light therapy: improved sensitivity to its effects on rest-activity
  rhythms in Alzheimer patients by application of nonparametric methods.
  *Chronobiol Int* 1999;16(4):505–518.
  <https://doi.org/10.3109/07420529908998724> — the source of IS, IV, RA, L5 and
  M10.
- Phillips AJK, Clerx WM, O'Brien CS, *et al.* Irregular sleep/wake patterns are
  associated with poorer academic performance and delayed circadian and
  sleep/wake timing. *Sci Rep* 2017;7:3216.
  <https://doi.org/10.1038/s41598-017-03171-4> — the Sleep Regularity Index.
- Wittmann M, Dinich J, Merrow M, Roenneberg T. Social jetlag: misalignment of
  biological and social time. *Chronobiol Int* 2006;23(1–2):497–509.
  <https://doi.org/10.1080/07420520500545979>
- Lim ASP, Yu L, Costa MD, *et al.* Quantification of the fragmentation of
  rest-activity patterns in elderly individuals using a state transition
  analysis. *Sleep* 2011;34(11):1569–1581.
  <https://doi.org/10.5665/sleep.1400> — kRA and kAR.
- Hu K, Van Someren EJW, Shea SA, Scheer FAJL. Reduction of scale invariance of
  activity fluctuations with aging and Alzheimer's disease: involvement of the
  circadian pacemaker. *PNAS* 2009;106(8):2490–2494.
  <https://doi.org/10.1073/pnas.0806087106>

**Sleep regulation and sleep health**

- Borbély AA. A two process model of sleep regulation. *Hum Neurobiol*
  1982;1(3):195–204. PMID 7185792. No DOI — the journal predates them.
- Daan S, Beersma DGM, Borbély AA (1984), the quantitative formulation of the
  two-process model — **UNVERIFIED.** I believe this paper exists in *Am J
  Physiol* and did not confirm it. §11.4 does not rest on it.
- Buysse DJ. Sleep health: can we define it? Does it matter? *Sleep*
  2014;37(1):9–17. <https://doi.org/10.5665/sleep.3298> — RU-SATED.

**PPG sampling rate**

- Béres S, Hejjel L. The minimal sampling frequency of the photoplethysmogram for
  accurate pulse rate variability parameters in healthy volunteers. *Biomed
  Signal Process Control* 2021;68:102589.
  <https://www.sciencedirect.com/science/article/pii/S1746809421001865> — journal,
  volume and article number verified; **DOI not directly confirmed** (it would
  conventionally be `10.1016/j.bspc.2021.102589`). Abstract read via secondary
  sources; **full text not read.**
- Béres S, Holczer L, Hejjel L. On the minimal adequate sampling frequency of the
  photoplethysmogram for pulse rate monitoring and heart rate variability
  analysis in mobile and wearable technology. *Meas Sci Rev* 2019;19(5):232–.
  <https://ui.adsabs.harvard.edu/abs/2019MeScR..19..232B/abstract> — the ≥ 5 Hz
  / ≥ 50 Hz recommendation.

**Implementations and documentation (not literature)**

- `actigraph.sleepr`, `R/apply_cole_kripke.R`.
  <https://github.com/dipetkov/actigraph.sleepr/blob/master/R/apply_cole_kripke.R>
  — the source of A13 and A14.
- pyActigraphy, `ScoringMixin.CK`.
  <https://ghammad.github.io/pyActigraphy/_autosummary/pyActigraphy.sleep.ScoringMixin.CK.html>
- GGIR nap detection vignette.
  <https://wadpac.github.io/GGIR/articles/NapDetection.html> — the source of A20.
  Documentation, not a paper.
- van Hees' non-wear criterion (per-axis SD and range over 60-minute windows on a
  15-minute step) — **UNVERIFIED.** Secondary sources disagree on the threshold,
  quoting both **3 mg** and **13 mg** for the standard deviation, with 50 mg for
  the range. Card B-3 must resolve which before implementing anything, and in
  any case the units do not transfer to this app's band-limited integral without
  recalibration.
- Firstbeat stress-and-recovery white papers. <https://www.firstbeat.com/en/science-and-physiology/white-papers-and-publications/>
  — **vendor documentation, not peer-reviewed.** Cited only to establish what
  Body Battery is built on, which is beat-to-beat intervals.
