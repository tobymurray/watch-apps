# Build `HapticLab`: how many haptic patterns can one wrist tell apart?

A self-contained brief. Everything you need about the platform is stated below or named
with the file that owns it; verify each claim against the checkout rather than trusting
this document, and correct it if the checkout disagrees.

## The question

`Nudge` is an eventual product: route navigation you feel, screen off, where left, right,
off-route, back-on-route and arriving are distinct haptic phrases rather than distinct
lengths of the same buzz. Whether that product can exist depends on one number nobody
has: **how many haptic patterns can a wearer reliably identify on this watch, on this
wrist, without looking?**

`HapticLab` is the instrument that answers it. Its deliverable is not an app someone
uses; it is a vocabulary, with the pairwise confusion behind every entry, plus the
protocol that produced it so another wrist can be added later.

The honest form of the answer is a **maximally distinguishable set**: the largest group
of patterns whose members are mutually confused below a stated rate, at a stated
confidence, on n=1 wrist. Not "the watch supports 127 effects".

## What the platform actually gives you

### The only haptic surface an app has

`SDK::Interface::IVibro` (`Libs/Header/SDK/Interfaces/IVibro.hpp`) is **not reachable
from an app.** `IKIP::IntfID` enumerates `IID_SYSTEM`, `IID_LOGGER`, `IID_APP_MEMORY`,
`IID_APP_COMM` and `IID_FILESYSTEM`, and there is no vibro identifier. The entire
app-facing surface is one message, `RequestVibroPlay`
(`Libs/Header/SDK/Messages/CommandMessages.hpp`):

```
static const uint32_t skMaxNotes = 8;

struct Note {
    uint8_t  effect;    // 1 - 127,  0 - for pause
    uint32_t pause;     // Pause duration in ms. 0 - if effect specified.
};

uint32_t notesCount;
Note notes[skMaxNotes];
```

**The message is narrower than the kernel interface, and this shapes the whole design.**
`IVibro::Note` carries a per-note `loop` (documented 1-3) and `IVibro::play` takes an
outer `loop` (1-6, 0 none, 7 infinite). `RequestVibroPlay` has neither, and the
simulator's dispatcher hardcodes `melody[i].loop = 0` and passes no outer loop
(`Libs/Source/Simulator/App/KernelMessageDispatcher.cpp`, the `REQUEST_VIBRO_PLAY` case).
So: **eight notes, played once, and every repeat or ramp longer than eight steps must be
composed by the app from consecutive messages.** Whether consecutive messages queue
seamlessly or interrupt each other is unknown and is one of the first things to probe.

`IVibro::Note`'s comment gives the kernel-side pause constraint that the message's own
comment omits: `1 - 1270`, in ms, **step 10 ms**. Treat 10 ms as the timing quantum
until measured otherwise.

The named effects are the 100% amplitude entries of what looks like the TI/Immersion ROM
waveform library, and **the enum skips indices in a pattern consistent with the skipped
ones being lower-amplitude variants of the same waveform** (1, 4, 7, 10 named; 2, 3, 5,
6, 8, 9 unnamed). If that holds, the intensity axis you want comes free. It is a
hypothesis, not a fact, and settling it is Phase 0's most valuable output.

There is also `RequestBuzzerPlay`: ten notes of `{time ms, volume}` where volume is
documented as 0-100% but "currently supported only 4 levels (0, 33, 66, 100)". No
frequency control, so it is a rhythm-and-loudness channel, not a tone channel. Include it
as a secondary modality only after the vibro answer exists; a buzzer is audible to
everyone in the room, which is the opposite of what `Nudge` is for.

### Input, and why the response design is constrained

`EventButton` gives four buttons and seven event codes:

```
Id:    SW1 (L1, top left)   SW2 (R1, top right)   SW3 (L2, bottom left)   SW4 (R2, bottom right)
Event: PRESS RELEASE CLICK LONG_PRESS HOLD_1S HOLD_5S HOLD_10S
```

It carries its own `uint32_t timestamp`. **Use that for reaction time**, not the app's
own clock read at handling time, so queue delay is not charged to the wearer.

Two hazards. `SW2` is annotated `PWR_ON_1V8_L`, so it is the power button and holds on it
may not be yours. And `CLAUDE.md` records, as hardware behaviour proven on the watch, that
`enMusicControl` makes the system eat `HOLD_1S`. Do not build the response scheme on a
hold unless you have disabled music control through `RequestSetCapabilities` and confirmed
the event arrives.

### The rest of the app environment

- App type `utility`, not autostart. One `Software/Apps/HapticLab-CMake` project finding
  the SDK through `$UNA_SDK`, which **must point at an `apps-v1.4.0` checkout**: an app
  built against a newer SDK than the running kernel installs cleanly and silently never
  launches. See the root `README.md`.
- GUI ticks at **10 fps**, whole frames only, 21-24 ms to push a 57,600-byte framebuffer,
  leaving roughly 78 ms of slack per frame. 240x240, 8bpp ABGR2222, four levels a
  channel, no touch. Measured in `RustGuiPoc/Docs/FINDINGS.md`. The screen is round and
  that is a hard constraint, not a styling note; see **The screen** below.
- **A GUI process cannot reach a sensor.** `SDK::Kernel` is `{sys, log, mem, comm, fs}`;
  `SDK::Sensor::Connection` works only from the Service half. Phases 0 and 2-4 need no
  sensor and can be GUI-only, which is much simpler. Phase 1 needs the accelerometer and
  therefore both halves plus a custom message; `RustGuiPoc` is the worked example of that
  shape.
- TouchGFX or a Rust `no_std` renderer behind CustomGUI are both established here. This
  app draws text and a few boxes, so pick whichever you can get building fastest and say
  which you picked. `Docs/TEXT.md` and `TextKit/README.md` own the typeface decision;
  do not re-litigate it.
- Files are sandbox-relative. No absolute `N:/` path resolves on the device.
- Nothing reaches a running app from the phone. Session configuration comes from a JSON
  file written over USB, the way `SleepLab` and `Barcode` do it, or from
  `SDK::AppConfig` at install time.

### The simulator can prove exactly one thing here

`SDK::Simulator::Mock::Vibro` is a logging stub: `play()` writes a line and returns, and
the dispatcher signals `SUCCESS` immediately after it returns. So the simulator can
verify that a pattern is well formed, that the protocol state machine advances, and that
the CSV comes out right. **It can say nothing about any sensation, any duration, or any
confusion rate.** Every perceptual number in the deliverable must come from a hardware
session, and the document must say so before it says anything else.

Note also what that stub implies for the device: **completion of `RequestVibroPlay` means
accepted, not finished.** The app cannot learn when a pattern ended, so pattern duration
must be modelled and then verified by an independent method (Phase 1).

## The screen: round, four levels a channel, and a simulator that lies about both

Every one of these was paid for on hardware by an app in this repository. None of it is
negotiable and none of it is visible in the simulator.

### Only the inscribed disc is glass

The panel is a Sharp LS012B7DD06, 240x240 at 0.126 mm pitch, and the corners are behind
the bezel. `TextKit` clips to `BarcodeLayout::pixelIsLit`'s rule: **a pixel counts as lit
only if its centre is within 119.5 dot pitches of the centre**, half a pitch inside the
datasheet's active area, deliberately. Reuse that rule; do not invent a second one.

**The simulator draws the full 240x240 square with no bezel, so clipping is invisible
there, and in `Barcode` it reached the watch twice before it was caught.** So:

- Mask every simulator screenshot with the disc before believing it. The recipe, the exact
  disc formula and the ink-outside-the-bezel measurement are in `Barcode/README.md`,
  section "Fitting the id on a round screen". Read the warning there about ImageMagick's
  `-fx`: a malformed expression yields a uniform image, every measurement then reads 0,
  and it looks like a pass.
- **Carry a geometry test that renders every screen and every string and asserts no pixel
  behind the bezel**, the equivalent of `Barcode`'s `nothing_is_drawn_outside_the_bezel`.
  It must cover the longest instruction, the longest pattern name and the widest response
  row, not a representative sample. This is the single test most likely to save a
  hardware session.
- Usable width shrinks fast away from the vertical centre. `Barcode` records roughly
  130 px of width still available at the height its pager row sits, against 240 at the
  middle. Anything wide belongs near the centre line.
- **Nothing may be anchored to a corner.** There are no corners. A response layout that
  puts one alternative per screen corner to mirror the four buttons is the obvious design
  and it is the wrong one here.

### Button affordances must be drawn, never blitted

`Barcode` has **deliberately no on-screen button hints** because the bezel indicator
bitmaps, 23x35 ABGR2222, **blit corrupt on device**: a smear of horizontal dashes where
the arc should be. It was worked around, reintroduced, and moved the artifact to a
different edge before being fixed again. `SleepLab` and `Squash` got real hints back only
by *drawing* them, with `touchgfx::Circle` and `PainterABGR2222` instead of a bitmap
container.

This matters more here than in any app in the repository, because a three-alternative
forced choice **is** a button-mapping problem: the wearer must know without hesitation
which button means which alternative. So:

- Draw the mapping with solid fills and drawn arcs. Solid colour straight into the
  framebuffer is the path that renders correctly on device.
- Put each alternative's label physically near its button's edge of the disc, so the
  mapping is spatial rather than something the wearer has to remember, but keep the label
  itself inside the disc and off the corners.
- Prove the mapping before trusting a single trial: a calibration screen where each
  button press just lights its own label. If the wearer can be wrong about which button
  they pressed, every confusion number is contaminated by response error rather than
  perceptual error.

### Colour: three greys, and one of them washes out

`LCD8bpp_ABGR2222` has two bits a channel, so the only greys are **85, 170 and 255**, and
**85 is the first thing to wash out in daylight**. `Barcode` uses 170 rather than
something dimmer for its unlit pager marks for exactly that reason: a mark whose unlit
state vanishes has lost the one thing it says.

And the hard one, from `RustGuiPoc/Docs/FINDINGS.md`: **bright text on a dark ground is
the only kind proven to render.** A black-on-white readout came back as a blank white band
on hardware. Do not design a light-background screen.

So the palette for this app is: white for what must be read, 170 for what is present but
inactive, dark ground everywhere, and never 85 for anything load-bearing.

## Instructions that actually read, on a wrist, mid-experiment

The wearer is standing up, arm down between trials, possibly wearing masking headphones,
and has to absorb each instruction in one glance. Instruction quality is not polish here;
an ambiguous prompt shows up later as noise you cannot separate from perceptual error.

### The prose box is already measured, so use it

`Docs/TEXT.md` records `Barcode`'s prompt layout, measured rather than chosen: **up to
four wrapped lines, Poppins Regular 18, x 20, width 200, top 72, 24 px line pitch,
centred, white.** The widest wrapped line is 200 px and the worst screen is four lines,
about 90 glyphs. That is the proven envelope for prose on this panel. Start there and
justify any departure.

Also from the same measurements: **22 to 26 px em is confirmed readable on this glass and
11 to 12 px is detection only**, so nothing the wearer must read goes small. And light
autohinting snaps vertically only, so Regular's stems at 16 to 20 px land as an 85 plus
170 column pair while SemiBold's at 20 and 24 px are two solid columns: **prefer SemiBold
for anything small or anything that must survive daylight.**

### Use TextKit's machinery rather than hand-placing text

`TextKit/README.md` is the crate's record; the parts that matter here:

- **`wrap`** is greedy by word into caller-owned line slots, never splits a word, and
  **reports how many lines the text needed**. Treat that count as a value you check, so an
  instruction that overflows is a number you see rather than a line that silently
  vanishes. A test should assert every literal in the app fits its slot count.
- **`pick`**, the size ladder, returns the first face whose advance fits. Use it instead
  of choosing a size by eye.
- **`Measure::missing`** counts characters with no glyph, so a screen can refuse to draw
  rather than show hollow boxes. Adopt `Barcode`'s refuse-rather-than-guess rule for any
  operator-supplied string, such as a pattern name from the session file.
- Faces are `static`s per weight, size and character set, listed in `Tools/faces.json`; a
  face not listed does not exist and naming one fails to compile, and the linker drops the
  ones you do not reference. So listing what you need costs nothing you do not use.
- Measure widths, never count characters. Poppins is proportional: `WWWWWWWWWWWW` is
  twelve characters and 240 px while sixteen `1`s are 208.

### What the instructions must and must not say

- **Keep every instruction string as a literal in one place**, the way `Barcode` keeps its
  prompts in one file, so the round-screen test and the wrap test can enumerate all of
  them exhaustively.
- **A trial screen must never name, hint at, or narrow the pattern about to be played.**
  This is a scientific requirement, not a preference. Anything that leaks the answer
  invalidates the trial, and a screen that shows the three alternatives in a *fixed* order
  regardless of which was presented is the way to avoid leaking it through ordering.
- **Separate the learning screen from the trial screen** so leakage is structurally
  impossible. The training stage is allowed to name a pattern and replay it on demand, and
  should, because absolute identification of a learned set is what is being measured. The
  trial stage cannot reach that code path at all.
- Say what to do, not what is happening: "Which one was it?" beats "Trial 14 of 60".
  Progress belongs on a between-blocks screen where the wearer is resting, not on the
  screen where they are responding.
- Every stage needs a visible, unambiguous state: waiting to start, playing, awaiting
  response, feedback (training only), resting, done. A wearer who cannot tell "playing"
  from "awaiting response" will answer during playback, and you will have logged a
  response to a pattern that had not finished.
- Write the operator's setup instructions, the ones about strap tightness and masking, on
  the host and not on the watch. Four buttons and 200 px of line width is no place for a
  procedure.

## Phase 0: probe the surface before designing anything

Small, ugly, and first. A screen that plays whatever `{effect, pause}` sequence the
buttons dial in, and a log. Settle:

1. **Do the unnamed indices work, and are they amplitude variants?** Play 1, 2, 3 back to
   back. Then 4, 5, 6. Then 7, 8, 9. Report whether they differ at all and in what way.
   This is the intensity axis existing or not existing.
2. **Where does the range end?** Try 127, then 128, then 255. Does an out-of-range effect
   get clamped, ignored, or rejected?
3. **Does `effect = 0` with a nonzero `pause` actually pause**, and is the 10 ms step
   real? Time a long single pause against the wall clock.
4. **Do consecutive messages queue or interrupt?** Send two eight-note patterns with no
   gap. Then send one and interrupt it mid-flight with another. This decides whether
   patterns longer than eight notes are possible at all.
5. **What does `notesCount` above 8 do?** The simulator clamps and warns; the device is
   unverified.
6. **Is there any way to stop a pattern early?** `IVibro::stop()` exists and is not in the
   message set. Confirm the absence rather than assuming it.

Record each as CONFIRMED / LIKELY / UNVERIFIED / REFUTED with the method, in
`Docs/LEDGER.md`, following the convention `SensorLab/Docs/LEDGER.md` uses.

## Phase 1: characterise the effects objectively, using the watch's own IMU

Subjective data alone leaves you unable to say whether two patterns were confused because
they feel alike or because one barely played. So measure the actuator.

**The BMI270 can hear the LRA through the case.** Subscribe to `ACCELEROMETER_RAW`
(0x11) in the Service half while the GUI plays each effect, and log the samples against
the play command. `SensorLab` measured the delivered rate at 48.48 Hz, dt 20.625 ms, so
this gives you **onset, offset, total duration and a relative energy proxy** from
sample-to-sample variance. It does **not** give you the waveform: an LRA carrier is well
above 48 Hz and will alias. Say that in the write-up rather than implying a waveform was
captured.

Deliverable: a table of all reachable effect indices with measured duration and relative
energy, which turns "effect 3" into a physical quantity. Two independent runs, and report
the disagreement between them.

If you want more bandwidth than 48 Hz, that is a separate piece of work with its own
risks; see the ODR discussion in `SensorLab/Docs/EXPECTED.md`. Do not attempt it here.

## Phase 2: build the candidate pattern set

Vary along these axes, and keep every pattern's parameterisation as data rather than as
code, so the set can be regenerated and a row in the results file is self-describing:

| Axis | Range | Notes |
| --- | --- | --- |
| Waveform character | click, tick, bump, buzz, pulsing, alert | The qualitative families the enum names |
| Amplitude | whatever Phase 0 found | Skip this axis entirely if Phase 0 refuted it |
| Pulse count | 1, 2, 3 | Above 3, counting becomes a task rather than a percept |
| Rhythm | even, short-short-long, long-short-short | Temporal structure, not intensity |
| Ramp | rising, falling, flat | Needs the amplitude axis to exist |
| Duration | brief, sustained | Bounded by 8 notes and a 1270 ms pause cap |

**Expect rhythm to carry most of the discriminability and intensity to carry very
little**, since intensity typically supports only a couple of usable levels on the wrist.
That is a hypothesis stated so the results can refute it. Do not design the set to
confirm it: include intensity-only pairs precisely so the experiment can say they fail.

Cap the candidate pool at something a wearer can learn in one sitting. Roughly a dozen to
twenty is the working range; justify whatever you choose.

## Phase 3: the protocol

**Absolute identification, three alternatives plus a miss key.** Three patterns are
offered on screen in a fixed order mapped to three buttons; one is played; the wearer
presses the button of the one they believe it was. The fourth button is **"did not feel
it"**, which exists so a missed presentation is recorded as a miss rather than forced
into a wrong answer. Four alternatives with a hold-to-miss is the alternative design and
it runs into the `HOLD_1S` hazard above; if you prefer it, disable music control first
and prove the event arrives.

To get past three, run **many overlapping triples** drawn from the candidate pool,
balanced so every pair co-occurs a comparable number of times, then build the pairwise
confusion matrix from the trial record. The answer is then the largest set of patterns
whose every internal pair confuses below threshold, which is a maximum clique on a small
graph and is brute-forceable. Compute it in a host-testable module, not on the watch.

The three alternatives are shown in a **fixed** screen order every trial, regardless of
which was presented, and the button mapping never changes within a run. Both are recorded
per trial anyway, because a mapping that was supposed to be fixed and was not is a bug you
will only find in the data. See **The screen** above for how the mapping is drawn and why
it must not be blitted, and run the button calibration screen before every session.

Required structure:

- **A training block with feedback**, because absolute identification of a learned set is
  the thing being measured, then **test blocks without feedback**. Report accuracy by
  block so learning is visible rather than averaged away.
- **Randomised presentation order**, and a seed recorded in the session file so a session
  is reproducible.
- **A no-contact control condition, mandatory.** Watch on a table, wearer's arm off it,
  same protocol. Any above-chance accuracy there is leakage through hearing or sight, and
  it invalidates the contact data by exactly that much. This is the cheapest and most
  decisive control in the whole design.
- **A masked condition.** An LRA at different amplitudes makes different *sound*, and a
  rigid case against skin is a speaker. Run the protocol with masking noise over the ears
  and without. If accuracy collapses under masking, you measured hearing.
- **A fixed inter-trial interval** long enough to avoid adaptation, and rest breaks.

Record per session, as a header the trials point at: firmware, app version, strap
tightness, watch position on the wrist, which wrist, arm posture, ambient temperature,
whether masking was worn, seed, and the candidate set id.

## Recording: a hardware session happens once, so write down everything

A wrist session is expensive and unrepeatable. Assume that in six months you will want to
ask a question of this data that nobody has thought of yet, and that the only thing you
will have is the files. **The default is to record it.** Anything cheap enough to write
and conceivably informative gets written, and the analysis decides later what matters.

Prefer CSV over JSON: `SDK::JsonStreamWriter::add(int32_t)` writes negatives as their
unsigned reinterpretation on 64-bit builds and `add(int64_t)` formats through `%g`, both
recorded in `SensorLab/Docs/FINDINGS.md`. ARM is unaffected, which is exactly why it
survives, but host tests would hit it.

### One directory per run, not one file

Research inputs go **outside** any activity tree, the way `Squash` keeps its IMU
recordings next to but deliberately not inside `Activity/`, so they do not ride along with
whatever syncs workouts. Name files so they pair up by run id and timestamp.

| File | What it holds |
| --- | --- |
| `session.json` | The run header: run id, schema version, app version and build, firmware, kernel interface version, seed, candidate-set id and hash, condition (contact or no-contact, masked or unmasked), which wrist, watch position, strap tightness, arm posture, ambient temperature, operator note key, and both wall clock and monotonic tick at start and end. |
| `patterns.csv` | The candidate set **as actually played**, one row per pattern, fully parameterised down to the note list. The set must be recoverable from the run alone, so a later change to the generator cannot orphan old data. |
| `trials.csv` | One row per trial, self-describing. |
| `events.csv` | **Every** `EventButton`, not only the ones that were responses. |
| `commands.csv` | **Every** `RequestVibroPlay` actually sent, note by note, with its timing and result. |
| `imu/` | Phase 1's raw accelerometer rows, per effect, with the sensor's own timestamps. |
| `probe.csv` | Phase 0's answers, in the same shape, so the probe run is data too. |

### The four clocks, all of them, on every row

There are four different times in a trial and they will disagree. Record all four rather
than the difference between two of them, so any later question about drift or queue delay
is answerable:

1. The monotonic tick before `RequestVibroPlay` is sent.
2. The tick when its completion is signalled, which means **accepted, not finished**.
3. The app's own modelled end of the pattern, so it can be checked against Phase 1's
   measured duration.
4. `EventButton`'s own `timestamp` for the response, which is the one reaction time is
   computed from.

Also record wall clock alongside the monotonic tick on every row, because wall clock can
jump and the tick cannot, and you want to be able to notice that it did.

### `trials.csv`

Run id, schema version, block index, block kind (training or test), trial index within
block and within run, condition, seed, the presented pattern id, the alternatives offered
and the order they were shown in, the button-to-alternative mapping in force, the response
button, the response alternative, correct flag, miss flag, all four clocks, reaction time,
and the presented pattern's full parameterisation repeated inline. The redundancy against
`patterns.csv` is deliberate: a row should be readable without a join.

### `events.csv`, and why the discarded presses matter

Log every button event with its id, its event code and its own timestamp, including the
ones the trial logic ignored: presses during playback, second presses after a response was
already taken, presses on the unmapped button, presses between trials, and holds. A press
during playback is evidence the wearer decided before the pattern finished, which is a
real result about that pattern and is invisible if only accepted responses are kept. Mark
each event with whether it was taken as the trial's response and, if not, why.

### Durability

- **Append and flush per row.** Never buffer a block. A session that crashes at trial 50
  must leave 49 usable trials, and this app has no second chance at that wrist-hour.
- **Close the run out on abandon as well as on completion**, following `Squash`: throwing
  away the session does not make the samples less real. A run that ended early is a run
  with an end reason recorded, not a missing file.
- **The SDK exposes no free-space query**, so caps are self-defence rather than device
  awareness. Set a byte and duration cap per file, write a row only if its worst case
  still fits, and record the cap and whether it was hit in `session.json`. State the
  budget arithmetic in the README the way `Squash` does.
- Put the run id and schema version in the first line of every file, so an analysis script
  can read a run recorded before it was written.

## Phase 4: analysis, and refusing to overclaim

A `Tools/` script regenerates the whole report from the files, so every number is
recomputable and disagreeable-with. It must produce:

- The pairwise confusion matrix, each cell with a **Wilson interval**, not a bare rate.
- **Information transfer in bits** for the subsets actually run, which is what "how many
  can be told apart" means formally.
- The maximum distinguishable set at a stated confusion threshold.
- Accuracy by block, so learning is separable from discriminability.
- Reaction time by pattern, because a pattern that is identified slowly is a poor
  navigation cue even when it is identified correctly.

**Refuse to report a confusion rate with too few trials behind it.** Say how many more
are needed instead, the way `SleepLab` refuses a heart-rate figure it has not earned a
baseline for. And state the n=1 limitation plainly: this is one wrist's answer, and the
protocol exists so a second wrist can be added.

For sanity only, and **not as a target**: wrist and forearm tactile displays in the
literature tend to land somewhere around 2 to 3 bits of information transfer, which is
roughly four to eight reliably identified items. If your result is far outside that, look
for a confound before believing it. Do not tune toward it, do not cite it as this
device's number, and do not let it into the deliverable as anything but a sanity note
with its status marked.

## Phase 5: the vocabulary, and only then `Nudge`

`Docs/VOCABULARY.md` is the actual product of this work: the recommended set, each entry
with its parameterisation, its measured duration from Phase 1, its worst pairwise
confusion and interval from Phase 4, and the trials behind it. Then, and only then, a
proposed mapping onto `Nudge`'s semantics: left, right, off-route, back-on-route,
arriving, and whatever else survives.

If the answer turns out to be four, that is a real and useful answer, and `Nudge` should
be designed for four. Do not stretch the set to reach a number that sounds better.

## Conventions

Read `CLAUDE.md` first and follow it, particularly the comment rule and "prefer
measurement over assertion". Beyond that:

- **Logic worth testing goes where it can be tested without a kernel**: the trial
  scheduler, the balancing, the confusion matrix, the interval arithmetic, the
  information-transfer computation and the clique search are all pure and all belong in
  the app's own `Tests/`, as `Squash`, `Chrono` and `MapManager` do it. Not under the SDK
  tree, where a subtree split would miss them.
- **Every claim carries its method and its tag.** CONFIRMED / LIKELY / UNVERIFIED /
  REFUTED, in `Docs/LEDGER.md`.
- **No inherited numbers.** If a figure is not from this app's own run or from a named
  file in this repository, it does not go in.
- **Keep the inputs, not only the conclusions.** Raw trial rows stay, so a statistic can
  be recomputed and disputed.
- **Two tests are not optional**: no pixel behind the bezel, across every screen and every
  string; and every instruction literal fits its line slots, with the wrap count asserted
  rather than eyeballed.
- **Every simulator screenshot in a document is disc-masked** before it is committed, with
  the ink-outside-the-bezel figure quoted next to it. An unmasked screenshot is not
  evidence about this panel.
- No em dashes anywhere in prose, comments or commit messages. No counts of tests or
  cases in any document.
- Conventional commits, one logical change each, and never bump `appVersion` by hand.

## Order of work

Phase 0 on hardware before any design work, because it decides whether the intensity axis
exists. Then Phase 2 and 3 buildable and simulator-verified end to end, with disc-masked
screenshots of every screen and a passing bezel test, before the first real session.
Phase 1 can run in parallel with Phase 3's construction since it shares no code. The
recording layer and Phase 4's analysis script should both exist before the first session
produces data: the files are the only thing a wrist-hour leaves behind, and the first
session's data should be readable the moment it lands.

Stop and ask rather than guessing if: a probe in Phase 0 contradicts this document,
the no-contact control shows above-chance accuracy you cannot explain, or the maximum
distinguishable set comes out at two or fewer.
