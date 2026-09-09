# The operator's procedure

On the host, not on the watch. Four buttons and 200 px of line width is no
place for a procedure, and the watch says only what the wearer needs in the
moment.

Read [`../README.md`](../README.md) for why the protocol is shaped this way,
and [`LEDGER.md`](LEDGER.md) for what is and is not known about the platform it
runs on. **No session has been run yet**, so this file is a plan and the first
run is what corrects it.

---

## Before anything: the order the phases go in

**Phase 0 first, on hardware, before any perceptual session.** It decides
whether the intensity axis exists, and therefore which candidate set is worth
running. It takes minutes.

**Phase 1 second**, for the same reason plus a practical one: the presentation
window is derived from the modelled effect durations, and until the sweep has
run every effect is modelled at its 1000 ms ceiling. A measured table shortens
the window and the session with it. The sweep takes about thirteen minutes and
saves considerably more than that.

Only then the perceptual sessions. Which set to run is a decision rather than a
default, and `Docs/LEDGER.md` row S7a is what it turns on:

- `rhythm-and-family`, twelve patterns, about 42 minutes. The main measurement.
- `bump-ladder`, three patterns, about ten minutes at twelve passes. The only
  amplitude ladder the actuator has, and the cheapest real experiment here.
  Set `"test_passes": 12`, not the default: three patterns make a pass one
  triple, so the default leaves each pair at exactly the analysis floor.
- `with-amplitude`, sixteen patterns, over an hour. **Not worth running**: the
  click ladder it is built on measures flat.

In this order:

1. **Wrist, unmasked.** The measurement.
2. **Table, unmasked.** The no-contact control. If it comes out above chance,
   stop and find the leak; the contact data is invalid by exactly that much.
3. **Wrist, masked.** If accuracy collapses here, what was measured is hearing.

Each is a separate run directory and a separate config. The report generator
refuses to pool a table run with a wrist run, so they cannot be averaged
together by accident.

---

## Setting up

### On the host

Write `config.json` into the app's folder over USB. Copy
[`../config.example.json`](../config.example.json) and change three things:

- `contact`: `wrist` or `table`. **This is the field a mistake is most
  expensive in.** The menu draws it in amber before the run starts; look at it.
- `masking`: `unmasked` or `masked`.
- `seed`: anything non-zero, or leave it at 0 and the app takes the wall clock
  and records what it took. A seed makes the run replannable.

Fill in the operator notes too: `wrist`, `watch_position`, `strap_tightness`,
`arm_posture` and `ambient_temp_c`. They cost one line each and they are the
fields nobody will be able to reconstruct in six months.

### On the wrist

- Watch on the wrist the config says, positioned where the config says.
  "One finger above the ulnar head" is a description that can be repeated;
  "as usual" is not.
- Strap snug enough that the case does not move against the skin, and record
  how snug in words that mean something to whoever reads it next.
- Arm down at the side between trials, raised only to answer. The same posture
  every trial, because the actuator couples differently through a tensed arm.
- For a masked block: masking noise over the ears at a level that covers the
  actuator's own sound. Check it by playing the loudest pattern from the
  learning screen and confirming you cannot hear it.
- For the table control: watch on a table, wearer's arm **off** the table and
  not touching it or the surface it stands on. Sitting with hands in the lap is
  the position to use.

### Unplug the cable

Plugging in USB terminates every running app (ledger row P11), so the cable
comes out before the run and stays out. A run that ends at a plug-in event is
recorded as `stopped-by-kernel` with whatever rows it had, which is honest but
is not a session.

---

## Phase 0: the probe

L1 from the menu. The probe list walks with L1 and L2, R1 runs the probe on
screen, R2 goes back. Every send is a row in `probe.csv` with its label, its
note list and its two clocks, so the run is data too.

Work through the list in order and write down what you felt beside each label.
The questions the screen asks are the ones the ledger needs answered:

| Probe | What to listen for | Ledger row |
| --- | --- | --- |
| `V1 CLICK 1-2-3`, `V1 SHARP 4-5-6`, `V1 BUMP 7-8-9` | Three pulses with a gap. Do the second and third feel *weaker* than the first, feel *different*, or not arrive at all? Weaker is the amplitude axis existing. | V6 |
| `V2 EFFECT 127` | Does anything play? | V8 |
| `V2 EFFECT 128`, `V2 EFFECT 255` | Nothing, something, or the same thing as a lower index? A value that plays as though clamped is different from one that is ignored. | V8 |
| `V3 PAUSE 1270` | Two clicks. Time the gap with a stopwatch against 1.27 s. | V5 |
| `V3 PAUSE 10` | Two clicks 10 ms apart. One buzz or two clicks? | V5 |
| `V4 QUEUE` | Two eight-note messages sent with no gap. Sixteen pulses or eight? | V7 |
| `V4 INTERRUPT` | An eight-note message cut off 300 ms in by a single strong buzz. Does the first one continue, stop, or finish first? | V7 |
| `V5 COUNT 12` | The message says it carries twelve notes and the array holds eight. Eight pulses, nothing, or a refusal? | V9 |

`V6-no-stop` needs no probe: the absence was confirmed from the checkout, and
the row says how.

**Then update `LEDGER.md`.** A probe run whose answers are not written down did
not happen. Each row gets its tag, the method, and the date.

**Stop and ask rather than guessing if a probe contradicts the ledger.** In
particular, if the unnamed indices turn out to be something other than
amplitude variants, the `with-amplitude` candidate set means something
different from what it was built to mean.

---

## Phase 1: the effect sweep

L2 from the menu. It runs unattended, and how long for is `sweep_repeats` in
`config.json`: one repeat of one effect is 3.1 s, so the whole effect space
costs about 6.6 minutes a repeat. Two repeats is 13 minutes and six is 39. R2
stops it.

Repeats are cheap and they are what turns a pair of numbers that might disagree
into a median with a spread. Six is a good default for an unattended run; the
first sweep used two and 43 of 122 playable effects came out with repeats that
disagreed. Raw samples cost about 18 KB a repeat an effect, so six repeats is
near 14 MB against a 64 MB cap.

- **The watch does not need to be worn, and is better off it.** Phase 1
  measures the actuator, not a sensation: a duration is a property of the
  waveform driven into the LRA, and an energy is only ever used relatively
  between effects measured under the same conditions. Nothing in the
  perceptual protocol depends on the sweep's absolute energy. Off the wrist the
  baseline is quieter, so the threshold drops and a weak effect is detected
  rather than lost under a breathing wrist.
- **On a pillow or a folded towel, screen up, touching nothing else.** Not a
  bare hard surface: a strong effect can skitter the watch across one, and a
  case that physically moves is motion the accelerometer cannot tell from the
  actuator. It merges bursts, which is the failure mode `kBurstGapMs` exists
  for. Foam damps that and adds no resonance of its own.
- **The same setup for both runs.** The second run exists for agreement across
  setups, so a run on the wrist and a run on a pillow answers nothing: two
  things changed at once.
- **Set `sweep_repeats` before you start.** It cannot be changed once the
  sweep is running, and a run at two repeats is a run you will want to do
  again.
- **Write the setup into `watch_position`.** The sweep's `session.json` records
  that field, and it is the only thing that will later say what the numbers
  were measured against. The field is named for a session rather than a sweep,
  which is a wart, and it is still the right place.
- An energy ratio is comparable within one sweep and not across a change of
  coupling. The ladder figures in ledger row S7a came from a worn sweep; a
  pillow run that reproduces them roughly is independent confirmation, and one
  that does not is the coupling rather than the finding failing.
- Run it twice, as two separate runs, and compare. The disagreement between two
  independent sweeps is the figure to report, not either one alone. Two repeats
  inside one run is not the same measurement: it says how steady one setup is,
  not how much the setup itself matters.
- **Check `hapticlab.log` afterwards.** A `run open` line with no matching
  `run close` means the app was killed mid-sweep and the run is partial.
  Whether the kernel stops an app while the watch sits idle and unworn is not
  known, and this is how it would show.
- Check `effects.csv` afterwards: the two effects whose names state a duration
  are the only independent check the sweep has, so their measured duration
  against their declared one is the first thing to look at. A large discrepancy
  there means the clocks do not agree (ledger row S5) or the detection
  threshold is wrong, and both invalidate the rest of the table.

Nothing the sweep produces is a waveform. Say so wherever the numbers appear.

---

## A perceptual session

R1 from the menu.

1. **Calibration.** Press each of the four buttons once; each lights its own
   label as it is pressed. When all four are lit, R1 begins. This is not a
   formality: if the wearer can be wrong about which button they pressed, every
   confusion number is contaminated by response error rather than perceptual
   error.
2. **Learning.** L1 and L2 walk the set, R1 replays the pattern on screen, R2
   begins the trials. Take as long as it takes. Absolute identification of a
   *learned* set is what is being measured, so a wearer who has not learned the
   set is measuring something else. Go round the set at least twice, and then
   once more testing yourself by name before looking.
3. **The training block.** Every pattern twice, with feedback after each
   response. The screen says whether the answer was right and names what it
   was. Accuracy here is reported separately, so learning stays visible rather
   than averaged into the result.
4. **The test blocks.** No feedback. WAIT, then PLAYING, then the three
   alternatives. Answer only when the alternatives are on the screen: a press
   during playback is recorded as such and is not taken as the response.
5. **The rests.** A rest screen between blocks says which block is next and how
   many trials it holds. R1 continues, R2 ends the run. **R2 during a block
   does not end the run**, because it means NOT FELT, so the rest screen is the only
   place a run can be stopped deliberately.
6. **Done.** The run is closed out and the files are complete. Plug in USB and
   pull `Runs/<run id>/`.

### What to do about a miss

Press R2, the NOT FELT key, when nothing arrived. Do not guess. A miss is recorded as
a miss and excluded from the pair evidence, which is the honest handling; a
guess is recorded as a confusion that never happened.

### What to do about a mistake

Nothing. Answer the next trial. Every press is in `events.csv` with its own
disposition, so a second press after the answer was already taken is in the
record rather than lost, and the analysis can be asked about it later.

---

## After the session

```sh
Tools/docker-build.sh analyse Runs/20260908T101500 > REPORT.md
```

Read the report before deciding anything. In order:

1. **The condition table at the top.** Confirm it says what the session
   actually was. A run whose `contact` is wrong is worse than no run.
2. **The no-contact control, if this is one.** Above chance means stop.
3. **Accuracy by block.** If the last test block is much better than the first,
   the wearer was still learning and the earlier blocks are measuring that
   rather than discriminability.
4. **The pairwise table.** Any pair marked "not reported" needs more trials,
   and the report says how many. Do not read a rate off a cell that refused.
5. **The maximally distinguishable set.** If it comes out at two or fewer,
   **stop and ask** rather than reporting it: either the patterns are not
   distinguishable on this wrist or something in the protocol is wrong, and the
   control and the event log are where to look first.
6. **The event table.** A large `during-playback` count means the presentation
   window is longer than it needs to be, which Phase 1 can fix.

Then, and only then, write the vocabulary: [`VOCABULARY.md`](VOCABULARY.md) is
the deliverable, and it takes its numbers from the report rather than from
anybody's recollection of the session.
