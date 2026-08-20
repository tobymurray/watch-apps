# Prompt: what a better instrument would do with this night

You are a sleep scientist who also writes embedded C++. You have been handed a
working actigraphy recorder and one night of real data from it, and asked the
question its author cannot ask himself: **is this the right instrument, and is it
extracting what is extractable?**

This is the companion to [`EVALUATION-PROMPT.md`](EVALUATION-PROMPT.md), and the
two do not overlap. That one asks *is the code correct*. This one asks *is the
code computing the right things* — and takes correctness as given, because a
better metric computed wrongly is a separate report.

**The output is not an essay. It is a catalogue of experiments**, each with a
hypothesis it can refute, a cost, a decision rule written before the run, and the
list of things that become buildable or permanently forbidden depending on how it
comes out. §2 defines that form; read it before you read anything else, because
every section after it is asking you to fill it in.

The app is `SleepLab/` in this repository. Read
[`README.md`](../README.md), [`Docs/FEASIBILITY-LEDGER.md`](FEASIBILITY-LEDGER.md)
and [`Docs/ROLLOUT.md`](ROLLOUT.md) first. The ledger is the important one: it is
a list of what has been measured on this hardware and what has only been
assumed, and **it is the boundary of what you are allowed to propose against.**

---

## 0. The five questions

1. **Is Cole-Kripke (1992) still the right scorer for this data in 2026**, or is
   the app citing a paper because it is easy to cite? (§4)
2. **What would an academic sleep lab compute from the files already on this
   disk** that the app does not compute? (§5)
3. **Nap detection** — possible here, worth it, and what does it cost? (§6)
4. **Body-battery-style recovery insight** — how rejuvenating was the night —
   with no RR intervals, ever. Is there an honest version? (§7)
5. **Where is this implementation already at or ahead of the state of the art**,
   and where is it behind a £30 supermarket band? (§8)

None of the five is answerable by reading alone. Each one resolves into a set of
hypotheses, and each hypothesis into an experiment that could refute it — at a
desk, in a host test, in two minutes on a wrist, or across ten nights with a
diary. **Your job is to produce those experiments, ordered by what they cost
against what they settle**, and to say for each possible outcome what it unlocks
and what it closes for good.

---

## 1. The device, as constraints rather than as background

These are measured, not assumed. **A proposal that violates one of them is not a
proposal**, and the most common way to waste this review is to design an
experiment that needs beat-to-beat intervals.

| Fact | Status | Consequence |
| --- | --- | --- |
| `HEART_BEAT` (0x40) has **no driver at all** — `connect()` is refused | CONFIRMED on hardware, twice | **No RR intervals. No HRV. Ever, while this holds.** The `rmssd_x10`, `sdnn_x10` and `rr_count` columns exist in the epoch CSV and are `-1` in every row of every night. |
| `RR_INTERVAL` has no firmware producer | CONFIRMED | The kernel parses and discards RR values a chest strap sends. A Polar H10 worn overnight yields none. |
| `SPO2` (0xF1) has **no driver** | REFUTED on hardware | No oximetry, no desaturation index, no apnoea proxy. |
| PPG raw waveform: claimed 20 Hz single-channel; **whether it is app-reachable at all is untested** | UNVERIFIED (S6) | The single highest-leverage unknown in this document, and a two-minute experiment — see §7. |
| Accelerometer delivers **~48–49 Hz** regardless of the 25 Hz requested, and ignores its batch latency (195 ms delivered against 5000 ms requested) | CONFIRMED, REFUTED respectively (S3, S17) | Raw acceleration at ~48 Hz **is available** — opt-in, ~60 MB a night. Anything in the modern raw-accelerometer literature is therefore reachable in principle. |
| The activity count is **not** rate-independent below ~half the delivered rate | REFUTED as stated (S14) | Counts from two nights are comparable only if their delivered rates are close. Every summary records the rate. |
| `TOUCH_DETECT` is an **event** sensor: one sample in 507 rows, zero transitions all night | CONFIRMED (S7, S12) | Worn detection cannot be a per-epoch duty cycle. It is sticky state plus a plausibility check. |
| Delivery survived **8 h 26 m with no gap** — 0/507 empty rows, median row span 60.0 s | CONFIRMED (S1) | An all-night recorder is possible on this device. This was the question everything else waited on. |
| The whole app including continuous optical HR cost **10 mAh over 8.45 h — ~4.6 % of the pack** | CONFIRMED (S2) | 24-hour recording is not obviously unaffordable. Do the arithmetic in §6 rather than assuming either way. |
| No phone, no companion app, no cloud, and no free-space query | CONFIRMED | All analysis is on-watch or offline over USB/BLE. Settings arrive as a JSON file written over USB. |
| Plugging in **terminates every running app** | CONFIRMED | A night on the charger is not a night. Any multi-night protocol has to say when the watch charges. |
| Storage: ~121 KB a night, ~44 MB a decade, epochs always, raw never by default | Measured | Ten years of nights fit. Ten nights of raw do not fit alongside much else. |
| One night exists with a **hand diary and a Garmin Fenix 6 Pro worn alongside** — and it is unscoreable, because the probe records no activity counts | CONFIRMED (S13) | The best-referenced night this project is likely to get was half-wasted. Design so that does not happen twice. |

The MCU budget is not stated here on purpose: **establish it from the code** —
`Software/Libs/` and the CMake files — and say what it is, because half the
proposals in §5 turn on whether the arithmetic runs on the watch or in `Tools/`.

---

## 2. Every answer is an experiment

An experiment here is a **card**, and a card with a field missing is not ready to
run. This is the deliverable's atom; §9 says how they are assembled.

| Field | What it must contain |
| --- | --- |
| **Hypothesis** | One falsifiable sentence in the ledger's voice — the form is "X does Y", stated so that a result can come back *no*. "Investigate the PPG" is not a hypothesis. Name the ledger row it creates, or the existing row it would move. |
| **Instrument and cost** | One of: **desk** (a read, a grep, an arithmetic check — free); **host test** (a test in the existing suites — free, minutes); **offline harness night** (a whole night through the real recorder in 110 ms — free); **public-dataset experiment** (open PSG/accelerometry/PPG data, no watch — free, hours); **short hardware run** (minutes on the wrist, readable off the screen); **one night**; **N nights**; **N nights with a diary**; **N nights with the Fenix alongside**. State wall-clock and **calendar** cost separately: ten nights is ten days and nobody's sleep is a laboratory. |
| **Procedure** | Precise enough to execute at 23:00 without deciding anything. The `settings.json` values, which app, which wrist, what to write down, what to read off the screen *before* bed, how the artifact comes off the watch afterwards. If a step requires judgement in the moment, the design is not finished. |
| **The measurement** | The column, the file, and the statistic. Not "check whether PPG works" — "the case of `P` in the driver block on the screen, and `ppg_n` over one 60 s row". |
| **The decision rule, written before the run** | The numeric criterion for **confirm**, for **refute**, and for **inconclusive**. The third one is not optional: an experiment that cannot come back inconclusive is under-specified, and one whose inconclusive band you only discover afterwards is a night spent buying a coin flip. |
| **What confirming unlocks** | The metrics, features and further experiments that become buildable, by name. |
| **What refuting locks** | What becomes permanently forbidden, and the REFUTED ledger row that must be written so it cannot be quietly reopened six months later by somebody who forgot. **This is usually the more valuable half of the result** — a closed door stops work, and this project has already had two features saved by a refutation. |
| **Preempted by** | The cheaper experiment whose outcome would make this one pointless. If there is one, it runs first and this card says so. |
| **Expiry** | What would re-open the question even after it is settled. Firmware is the usual answer: UNA have described a higher-rate PPG mode and on-chip HRV as things they are working on, so **every HRV-adjacent CONFIRMED row has an expiry date**, and a row without one is claiming the firmware will never change. |

Four rules over the catalogue as a whole:

**Kill branches first.** Order by *what a refutation would save*, not by cost
alone. A two-minute run that could invalidate an entire section of this document
outranks a cheap desk check that changes nothing either way.

**A night is a fixed cost — fill it.** The question is never "is this worth a
night", it is "how many hypotheses can this night carry". Produce a night plan:
for each night, its settings, what is worn, what is written down, and the list of
hypotheses it settles. **A night that answers one thing when it could have
answered four is the failure mode this project has already suffered once** (S13).

**Say which hypotheses cannot share a night.** `hr=continuous` and `hr=off` are
mutually exclusive; so are raw recording and a ten-night storage budget. Name
every such pair rather than producing a plan that cannot be executed.

**No experiment whose result changes nothing.** For each card, name the decision
it feeds. If both outcomes lead to the same next action, delete it — and if a
whole section produces no such card, say that too, because "nothing here is worth
a night" is a finding.

### The shape, worked once

Not a proposal — an illustration of the form, so that the fields are unambiguous.
The real version of this card belongs in §7 and should be better than this one.

> **H-PPG-1 — `PPG` resolves an app-facing driver on 1.4 firmware.**
> *Instrument:* short hardware run, ~3 minutes, calendar cost zero.
> *Procedure:* `"ppg": "on"` in the probe's settings, launch, read the driver
> block on the screen after one row has been written, pull `probe_log.csv`.
> *Measurement:* the case of `P` in the driver block; `ppg_n` for the first row.
> *Decision rule:* upper-case `P` **and** `ppg_n` > 0 → confirm. Lower-case `p`
> → refute. Upper-case `P` with `ppg_n` == 0 → inconclusive, and the follow-up is
> a longer run, because a driver that resolves and delivers nothing is
> `HEART_BEAT`'s failure wearing a different hat.
> *Confirming unlocks:* H-PRV-1 (can 20 Hz support a nocturnal RMSSD at all —
> which is a **public-dataset** experiment and therefore free), the waveform
> columns, and the whole of §7.2. *Nothing about recovery is unlocked by this
> alone*; it unlocks the right to ask.
> *Refuting locks:* every PRV, HRV and cardiac-staging route on this hardware,
> permanently. Ledger row S6 flips to REFUTED and §7 collapses to §7.2 and §7.3.
> *Preempted by:* nothing — this is the cheapest thing in the document.
> *Expiry:* any firmware bump; UNA have said a higher-rate PPG mode is in
> progress.

Note what the example does: it spends three minutes to decide whether a whole
section of work is possible, and its *refutation* is the outcome that saves the
most time. Aim for cards like that.

---

## 3. What exists now, and where the raw material is

Scoring is **Cole-Kripke (1992)** at 60 s scoring epochs (pairs of 30 s
recording epochs) with **Webster (1982)** rescoring. Counts are band-limited
(0.25–3 Hz) dt-weighted integrals per axis, combined as the vector magnitude —
ActiGraph-style vector-magnitude counts. `kCountScale`, the constant bridging
this device's count units to the units Cole-Kripke's coefficients were fitted
for, **is a guess** (ledger A9), and it is the single number between "cites a
real paper" and "is validated".

Derived and reported: total sleep time, time in bed still, onset and latency,
WASO, awakenings, efficiency, movement index, a worn verdict, a four-level
"restfulness" band that is explicitly not a hypnogram, nocturnal HR minimum and
mean, and deltas against the median of the wearer's own last 28 nights.

The epoch CSV — schema 3, one row per 30 s — is the raw material for everything
you might propose offline:

```
uptime_ms,wall_utc,span_ms,count,peak,samples,
count_x,count_y,count_z,
motion,sig_motion,step_delta,
hr_mean_x10,hr_min_x10,hr_samples,hr_source,
worn_pct,worn_edges,batt_pct_x10,charging,
rmssd_x10,sdnn_x10,rr_count,          <- always -1, see §1
acc_batches,acc_max_gap_ms,touch_n,hr_trust_x10,
hrex_opt,hrex_ext,hrex_unk,
batt_mv,batt_ma_x10,batt_avg_ma_x10,batt_mah,
wakes,msgs
```

`Nights/watching.csv` carries the same shape for the minutes inside the bedtime
window when **no** session was open. `Nights/index.csv` is one row per night and
is the only history the app has. `Raw/raw_<start>.csv` is integer
microgravities at the delivered rate, opt-in.

Read `Software/Libs/Header/NightStore.hpp` — the file comment is the normative
spec — before assuming a column means what its name suggests. Two rules that
constrain every proposal: **`-1` means not measured and never zero**, and **a
figure carries the method string that produced it**.

**Four instruments already exist and are free.** Any experiment you can push onto
one of them must not be pushed onto a night:

| Instrument | What it can settle |
| --- | --- |
| `Tests/` host suites, 193 tests | Arithmetic, thresholds, filter behaviour across the delivered rate range. |
| `Tests/NightHarness.hpp` — a whole night through the real `Service` in 110 ms | Any hypothesis about what the recorder *does* with a given input stream, including adversarial ones. Nights of known shape, at any rate, with any fault. |
| The 2026-08-19 probe night — 507 rows, diary, Fenix alongside | Re-analysis, without limit, at no cost. Every delivery, power and HR hypothesis you can answer from it is one you must not spend a night on. |
| `Tools/night_report.py` + public datasets | Candidate scorers, candidate metrics, and quantisation experiments — implemented off-watch and run against open data or against the existing CSVs before anything ships to the wrist. |

---

## 4. Is Cole-Kripke still the right scorer?

The app's own defence is that Cole-Kripke has published PSG validation, ships
with Webster rescoring because that is how its accuracy figures were measured,
and has two independent reference implementations to check the constants against.
That is a real argument. Test it against the obvious objections:

- It was fitted in 1992 against a **specific device's** counts, on a specific
  cohort, in a lab. `kCountScale` exists because this device is not that device
  — so the app is applying 34-year-old coefficients through a guessed
  transformation. **At what point is a scorer fitted on modern raw acceleration
  simply a better bet than a paper-accurate implementation of an old one driven
  through an unmeasured constant?** Argue it both ways and then pick.
- Actigraphy's specificity to wake is poor and the bias direction is known. Do
  any of the alternatives actually improve on that, *in free-living home
  conditions*, or only in lab re-analyses of the cohorts they were fitted on?
- Epoch length: 60 s because the coefficients require it. Which alternatives
  would let the app score at the 30 s it already records, and is that worth
  anything?

Candidates to evaluate — **a starting point, not the scope**, and some of these
names may be misremembered, so verify every one and report any that do not
exist:

Sadeh (1994); the Oakley/Actiwatch algorithm; the UCSD algorithm (Kripke et al.,
2010); van Hees' z-angle method (2015) and the diary-free HDCZA (2018) as
implemented in GGIR; Sundararajan et al. (2021) random forests on raw wrist
acceleration; the Palotti et al. (2019) benchmark of classical and ML scorers on
a large cohort; anything since that has held up.

Three things to be specific about:

1. **Which of these need raw acceleration rather than counts**, and therefore
   trade a guessed constant for 60 MB a night. That trade is the most
   interesting question in this section: a raw-signal algorithm sidesteps
   `kCountScale` entirely — it does not need the bridge, because it does not use
   the units the bridge exists to reach. Is that as decisive as it sounds, or
   does it just move the calibration somewhere less visible?
2. **Which are implementable on this MCU**, which need `Tools/`, and which are a
   trained model that cannot be honestly re-fitted by one person with a diary.
3. **What a scorer swap does to everything downstream** — the band, the alarm,
   the baseline, and the ten diary nights that are supposed to calibrate A9. A
   proposal that silently invalidates the calibration plan should say so.

### The experiments this section owes

Almost all of them are free, which is why this section should produce the largest
number of cards and the smallest number of nights. At minimum:

- **The scorer bake-off is a software experiment, not a hardware one.** Candidate
  scorers implemented in `Tools/`, run over the *same* epoch or raw CSV, compared
  epoch-by-epoch against each other and against the diary. State the decision
  rule: what margin of agreement, on how many nights, would justify a swap — and
  what result would leave Cole-Kripke standing.
- **The `kCountScale` sweep is a re-analysis, not ten new nights.** Ten
  diary nights are needed once; after that, every value of the constant is a
  re-score of files already on disk. The rescore tool that makes this true is
  named in the ledger's open threads as the highest-value unbuilt tool
  (`POST-MORTEM.md` G4). Say what it must do, and what pre-registered criterion
  picks the winning value — minimising mean signed error on onset and final wake
  is the obvious one and it has a failure mode; name it.
- **The A8 transcription check has a residue.** The constants were verified
  against two reference implementations and *not* the primary sources, which is
  why the row is LIKELY. That is a library errand with a decision rule
  ("CONFIRMED if p. 466 agrees, REFUTED with the correction if not"), and one of
  the five Webster rules was already found wrong this way.
- **Which candidate scorers can be falsified with public data alone**, before
  any of them touch this wrist. Open accelerometry-plus-PSG datasets exist; if a
  candidate cannot beat Cole-Kripke on data where ground truth is known, it does
  not deserve a night here.

---

## 5. What a lab would compute from these files and this app does not

This is the section most likely to produce something valuable, because it costs
**no nights**: it is arithmetic over files that already exist or will exist
anyway. Split every proposal into:

- **Free now** — computable from `index.csv` and the epoch CSVs already on disk.
- **Free later** — needs only more nights of the same recording, no code on the
  watch.
- **Costs recording** — needs a channel, a rate or a window the app does not
  currently record.

Leads worth checking, again as a starting point and again to be verified rather
than trusted:

**Circadian and regularity, from timing alone.** Non-parametric circadian
rhythm analysis — interdaily stability, intradaily variability, relative
amplitude, L5 and M10 (Van Someren et al., 1999 and the methodological
literature since). Cosinor rhythmometry. The Sleep Regularity Index (Phillips et
al., 2017). Sleep midpoint, chronotype and social jetlag (MCTQ; Wittmann &
Roenneberg, 2006). Note carefully which of these **require 24-hour data** and
therefore belong with §6 rather than here — several are routinely computed from
night-only data in the wild and are not valid that way.

**Fragmentation, beyond WASO and an awakening count.** State-transition
analysis — the kRA / kAR rest-to-activity transition probabilities (Lim et al.,
2011) — which is fitted per night from exactly the data in the `count` column
and is better-evidenced as a fragmentation measure than a count of awakenings.
Scale-invariance and detrended fluctuation analysis of activity fluctuations
(the Hu / Scheer line of work). Sleep fragmentation indices from the actigraphy
standards literature.

**The per-axis columns nobody is using yet.** Schema 3 records `count_x/y/z`
separately, on the argument that sensor noise is roughly isotropic and movement
is not. What else follows from that? Two candidates: the non-wear detection
method in the van Hees accelerometer literature (per-axis standard deviation
over long windows), which is an academic answer to the nightstand problem the
app currently solves with a bespoke plausibility gate; and body-position
estimation from the gravity vector, which is a real literature and would need the
raw file rather than the counts. Say whether either beats what the app does now
— and note that **both are testable against the existing offline harness and a
watch left on a table for an hour**, which is not a night.

**Heart rate, used as more than a minimum and a mean.** The timing of the
nocturnal HR nadir relative to sleep onset; the overnight dip as a percentage of
a daytime reference; the slope of the morning rise. Each of these is a real
quantity the epoch CSV can already produce, and each needs a claim about what it
*means* that this project may not be able to support. Distinguish "computable and
personal and trended over 28 nights" from "known to indicate something".

**Validation methodology, which is itself a literature.** The app has one night
with a hand diary **and a Garmin Fenix 6 Pro worn alongside** — the best
reference this project is likely to get. How should that comparison be reported?
Epoch-by-epoch agreement, sensitivity/specificity/accuracy, Bland-Altman on the
summary measures, and the standardised reporting frameworks for sleep-tracker
performance (Menghini et al., 2021 and the consensus statements around it). Then
the hard question: **what is a commercial device's output actually worth as a
reference**, given that it is a proprietary algorithm with its own biases and no
ground truth of its own? Say what a Fenix cross-reference can and cannot settle,
because the rollout plan currently leans on it — and if the answer is "less than
the plan assumes", that is a finding that changes the plan.

**And the one that should be asked back at the app:** the four-level
restfulness band is honest about what it is, and is *speculative* by its own
ledger row. Is there a better-evidenced relative index over the same two
channels, or should the band be deleted rather than improved? State the
experiment that would decide — and if no experiment available to this project can
decide it, that is the argument for deletion.

### The experiments this section owes

For every metric you propose, the card must say **which of the three classes it
falls in**, and for the "free now" ones the experiment is the re-analysis itself:
compute it over the existing night, show what it produces, and state the
pre-registered criterion for whether the output is informative or noise. A metric
that needs 14 consecutive nights before its first value is a **calendar**
commitment; say the number out loud, because the difference between "free now"
and "free in a fortnight" is what decides the order these get built in.

---

## 6. Nap detection

The app records only inside a bedtime window (`21:00`–`11:00` by default) and
opens a session only after sustained stillness while worn. So it cannot see a
nap, and it does not claim to.

Answer four things:

1. **Is a nap distinguishable from sedentary stillness on a wrist accelerometer
   at all**, without a diary and without a phone to say the wearer sat down? What
   does the literature say the false-positive rate is — a still evening in front
   of a screen is the obvious adversary, and it is the daytime equivalent of the
   nightstand problem. If there is a diary-free daytime-sleep detection method
   with real validation (GGIR has acquired nap detection; check what it is
   validated on and in whom), name it.
2. **What does 24-hour recording cost here?** 10 mAh for 8.45 h with continuous
   HR is the measured figure (S2). Do the arithmetic, including the part where
   the wearer must charge the watch *sometime*, and every charge kills the app.
   Storage is easy; the charge schedule may not be.
3. **What does it unlock that naps themselves do not?** Several §5 metrics need
   24-hour data to be valid — non-parametric circadian analysis, the daytime HR
   reference for the dip, total 24-hour sleep rather than nocturnal sleep. If
   round-the-clock recording is the enabling change for a whole class of metrics
   rather than one feature, say so, because that changes its priority
   enormously.
4. **What is the smallest version?** The app already writes `watching.csv` for
   in-window minutes with no session open. Is a "day log" at reduced fidelity —
   coarser epochs, HR duty-cycled — enough to detect naps and support the
   circadian metrics, at a fraction of the cost? Give the parameters.

### The experiments this section owes

- **The battery question is arithmetic first and a run second.** Extrapolate from
  S2, state the predicted daily cost and the charge cadence it implies, and *then*
  specify the one-day run that confirms or refutes the extrapolation — with the
  decision rule stated as a percentage of pack per 24 h, and an inconclusive band
  for the case where the wearer's day was unusually active.
- **The daytime false-positive rate is measurable without a nap.** A day of
  ordinary wear with a written log of every deliberate stillness — a film, a long
  meeting, a train — is a labelled adversarial dataset for whatever detector you
  propose, and it costs a *day*, not a night's sleep. Specify it.
- **The reduced-fidelity day log is a harness experiment.** Decimate the existing
  night's rows to the coarser epoch you propose and re-run the detector: if it
  survives decimation, the cheap version is enough, and no hardware was
  involved.
- Say explicitly **what a confirmed daytime capability unlocks** (which §5
  metrics become valid, which become newly computable) and **what refuting it
  locks** (which circadian metrics can then never be honestly reported by this
  app, and should therefore not be built).

---

## 7. Recovery, rejuvenation, and the thing that needs RR intervals

The user's question is the one every wearable answers and this device cannot:
**how restorative was that night?** Garmin's Body Battery is a Firstbeat model
and Firstbeat's method is built on beat-to-beat intervals. This device produces
none, and two independent CONFIRMED ledger rows say it never will while the
firmware stands.

So this section has the highest bar in the document, because it is where an
overclaim is most tempting and most rewarded. Work it in this order, which is
also the order of the experiments:

**7.1 Is the PPG route real?** Ledger S6 is UNVERIFIED and it is the hinge — the
worked card in §2 is this experiment, and it costs three minutes. Run it first in
your ordering whatever else you conclude, because a lower-case `p` deletes
everything below it.

**7.2 If the waveform exists, can 20 Hz carry a nocturnal RMSSD?** This is a
**public-dataset experiment and it needs no watch at all**: take open PPG or ECG
data with known beat-to-beat intervals, decimate to 20 Hz, apply the peak
detection and interpolation you would actually implement, and measure the error
in RMSSD and SDNN against the truth. 50 ms quantisation against a nocturnal RMSSD
that may itself be 20–50 ms is the whole problem. There is published work on the
minimum PPG sampling frequency for pulse-rate variability; find it, read the
error bars rather than the abstract, and then reproduce the bound yourself,
because the answer decides a feature. State the decision rule in advance: what
error, in what units, would make a derived recovery figure honest, and what error
makes it decoration?

**If that experiment says no, it is the most valuable single result in this
review**, because it closes a door the author has left ajar in three documents,
at the cost of an afternoon and zero nights, and it stops a feature that would
otherwise have been built and believed.

**7.3 What can be said with HR-only, no variability at all?** Nocturnal HR
minimum, its timing, the dip against a personal daytime reference, the morning
rise. These are recorded or nearly recorded. For each, state whether the
literature supports a *recovery* interpretation or only a *description*, and be
strict about the difference: a number that tracks something real, trended against
the wearer's own 28 nights, is a legitimate instrument. The same number with the
word "recovery" printed next to it may not be. The experiment here is mostly
free — these are columns already on disk — but the *validation* is not, and the
honest answer may be that nothing available to this project can validate them.
Say so if so.

**7.4 What can be said from sleep timing and duration alone?** The two-process
model (Borbély, 1982; Daan, Beersma & Borbély, 1984) predicts sleep pressure
from wake and sleep history and needs nothing but timing — which this app has,
or would have with §6's day log. Is a homeostatic-pressure trace an honest
"how rested are you" instrument? What is it validated against, and what happens
to it when the underlying sleep/wake scoring is biased high, as this app's
explicitly is? Also consider the multidimensional sleep-health framings (the
RU-SATED / Buysse, 2014 line) — several of their dimensions are already
computable here, and one of them, *satisfaction*, is self-report and cannot be.

**7.5 Then answer the question as asked.** Can this app tell its wearer how
rejuvenating a night was? If it cannot, what is the nearest thing it *can* say
that a person would find useful, and how should the screen say it so that it does
not read as a recovery score? The app's whole premise is refusing to print what
it cannot know; a proposal here that quietly relaxes that is worse than no
proposal.

---

## 8. Where is it already good, and where is it behind a cheap band?

Two lists, both specific, and the second one honestly.

**At or ahead of the state of the art.** Candidates the author believes and you
should test: dt-weighted counts re-coefficiented per sample, which handles a
device that ignores its requested rate; per-axis integrals combined as a vector
magnitude rather than filtering `|a|`; suppression rather than annotation for
nights that fail the worn gate; the method string travelling with every figure
into every file; recording epochs finer than scoring epochs so the file outlives
the algorithm; the summary carrying the constants that scored its night, so an
old night is re-scorable. For each: is it actually better than what shipping
research tools and commercial devices do, or is it just unusually well
documented? Several of these are checkable against the existing host tests or
against a reference implementation's source, which makes them desk cards.

**Behind.** A supermarket band prints stages, a readiness score, a respiratory
rate and a nap timeline, and it is wrong about most of them — but it is also
better than this app at some real things. Which? Candidates: no automatic
retrospective re-analysis, no multi-night trend beyond a 28-night median, a
history that is a single index file, a strip that is 100 buckets on a 240×240
panel, no export format anyone else can read (no FIT sleep messages exist on
this firmware), and one wearer's worth of calibration data forever. Say which of
these actually matter to a person using it, and which are the price of the
honesty and worth paying.

---

## 9. Deliverables

The first three are the point of the document. The rest support them.

1. **The experiment catalogue.** Every card from §2's template, **ordered for
   execution**, with the ordering justified by what a refutation saves rather
   than by cost alone. Group by instrument — desk, host test, harness,
   public-dataset, short run, night — so that the free ones are visibly first and
   the nights are visibly last and few.

2. **The night plan.** A calendar. For each night: the settings, the wrist, the
   diary discipline, whether the Fenix is on, and **the list of hypotheses that
   night settles**. Plus the mutually-exclusive pairs, so the plan is executable.
   State the total: *this catalogue costs N nights and M of them need a diary.*
   If that number is large, say what a reduced plan would give up.

3. **The unlock/lock map.** A table or a decision tree over the outcomes: for
   each experiment, what each result makes buildable and what it forbids. It must
   have **terminal nodes** — the features that no result from any experiment in
   this catalogue could ever unlock on this hardware, so that they can be written
   into the ledger as closed rather than remaining as someone's someday. Sleep
   staging is presumably one; say what else joins it and under what condition each
   would ever reopen.

4. **A ranked table of candidate additions.** What, mechanism, citation, inputs,
   free-now / free-later / costs-recording, cost, and **the experiment ID that
   gates it**. Rank by *evidence per night spent*, not by how interesting it is.

5. **A "do not build" list**, with the reason each one fails — hardware,
   evidence, or honesty — and the experiment that would have to come back
   differently for it to move.

6. **Ledger rows**, in `FEASIBILITY-LEDGER.md`'s exact format
   (CONFIRMED / LIKELY / UNVERIFIED / REFUTED, with the method behind the tag),
   for every claim your review turns on — including the ones that are UNVERIFIED
   precisely because your catalogue has not been run yet. Any existing row your
   reading of the literature contradicts, name it and say why.

7. **One re-analysis tool proposal** that runs offline over the epoch CSVs and
   computes your top-ranked free-now metrics — as a `Tools/night_report.py`
   subcommand, matching the existing scripts' discipline of refusing to quote a
   figure it does not have the nights to support.

8. **A citation list with DOIs or stable URLs.** Every citation in your review
   appears here. Anything you could not verify is marked as unverified, in the
   list, explicitly.

---

## 10. Rules

- **Do not invent citations.** A fabricated reference in this document would be
  laundered into a ledger row and then into a README, and the whole point of this
  codebase is that its claims are traceable. If you remember a paper but cannot
  confirm it, write "unverified — I believe there is work by X on Y" and leave it
  for someone to check. The leads in §4 and §5 were written from memory and some
  may be wrong; correcting them is part of the job.
- **No experiment without a decision rule, and no decision rule written after
  the result.** The criterion goes in the card before the run. An experiment
  whose interpretation is decided afterwards is a story, not a measurement.
- **Every experiment must be able to fail.** If you cannot describe the result
  that would refute the hypothesis, you have not written a hypothesis.
- **Distinguish "published" from "validated", and both from "validated in
  free-living conditions on a wrist, in a population that includes this
  wearer".** Most actigraphy validation is lab PSG on cohorts that are not one
  middle-aged individual at home.
- **Never propose relabelling.** The four-level band must not become a stage,
  a relative index must not become a score, and a personal delta must not become
  a population judgement. If a metric would need a new name to be honest, the
  new name is part of the proposal.
- **Respect the ledger's REFUTED rows.** They were measured. A proposal that
  needs one of them to be true is a proposal to re-measure it, and must say so
  and say how.
- **Prefer what one person with one watch and a paper diary can actually
  validate.** A method needing 200 participants and a PSG lab is worth naming as
  the reason a claim will never be made here, and nothing more.
- **A night costs a night.** Anything answerable from the files on disk, from the
  host tests, from the harness, from public data, or from reading must not be
  answered by a night. If your catalogue ends with "run these three nights", each
  of those nights must settle something no desk work could — and must settle as
  many such things at once as it can carry.
