# Adversarial review, 2026-08-18

An attempt to find every way SleepLab is wrong before it costs a night. Everything
below was found at a desk. No night was spent, and the point of the exercise is
that none needed to be.

Findings are ranked by **invisibility × consequence**, not by severity label. A
crash is cheap: you see it and fix it. A one-minute bias in every night survives
review and contaminates the calibration the app's whole credibility rests on.

Every correctness finding has a test that failed before the fix and passes after
it, committed in that order.

---

## The single most useful thing built

**Nothing exercised the recorder's own path.** The engine had unit tests over
synthetic `ScoringInput`s, the store had tests over synthetic `Epoch`s, and the
simulator had a screen and no sensors. The stretch between a sample arriving and a
summary being written — the epoch grid, the 30 s/60 s pairing, the pre-roll ring,
the backdate, the segmenter, the resume classification, the alarm, the files — was
reachable only by wearing the watch for eight hours and looking in the morning.

`Tests/NightHarness.hpp` runs a night in 110 ms. It drives the real
`Service::run()` unmodified by *being* the kernel: `StubAppComm::getMessage` is
virtual, so the harness answers the sensor layer's resolve/connect/disconnect
handshake, delivers batches on a schedule, advances uptime by exactly the timeout
the loop asked to sleep for, and finally hands back an `APP_STOP`. It captures every
`SLEEP_REPORT` the service publishes, so an assertion is an assertion about what a
person would have been shown.

**Why that and not a seam.** Extracting a `poll()` as `MapManager`'s service has
was the obvious alternative and was rejected: the loop is one of the things under
suspicion. Its grid advance, its sleep-to-next-deadline arithmetic and its oversleep
catch-up are exactly where a night gets quietly compressed — and finding #10 below
is a bug in the loop, which a test that replaced the loop could not have found.

The one concession the app makes is `SleepLab::setWallClockSource()`. Uptime was
already injectable through `SDK::Interface::ISystem`; the wall clock was
`std::time(nullptr)` at six call sites, which meant the bedtime window, the window
exit, the alarm and every time-of-day label could only be tested at whatever
o'clock the suite happened to run.

**Eleven of its first fifteen scenarios failed.** Twenty-seven scenarios now pass,
plus 103 engine tests and 43 store tests.

---

## Findings

### 1. Every reported sleep onset and final wake was fifteen minutes early

*What was wrong.* `NightSummary`'s onset and final-wake are indices into the scoring
array. `closeNight` turned them into times of day by counting minutes from
`mNightStartUtc` — which is the *backdated* session start, fifteen minutes earlier
than the array's first entry. The two were on different axes.

*The input that demonstrates it.* A generated night still from 21:50 to 02:50.
Reported wake: 02:35.

*What it would do to a real night.* Bias every reported bedtime and wake time
fifteen minutes early, every night, in the same direction. And then poison the fix:
the diary calibration of `kCountScale` compares the app's onset and final wake
against hand-recorded times, so a constant offset would have been absorbed into the
constant and the app would have looked calibrated.

*How I know.* `NightClock.ReportedWakeTimeIsWhenTheSleeperStoppedBeingStill`.

### 2. The fifteen backdated minutes were recorded and never scored

Same root cause, separate consequence. `flushPreRoll` wrote the pre-roll ring to the
CSV and stopped there — those minutes counted towards time in bed and could not
count towards sleep, because the scorer never saw them. **A generated night with no
awakenings at all reported 95 % efficiency**, and `provenance.epochs` (408) and
`sleep.time_in_bed_min` (423) disagreed by exactly the backdate, so nothing reading
the file could place the indices it quoted.

Fixed by pairing the ring into the scoring array through the same `Service::fold`
the live path uses — one function, so a night's first quarter hour cannot be summed
by slightly different arithmetic from the rest of it.

### 3. After a USB interruption, times were out by the whole pre-restart duration

`ResumeState` carries `startUtc` — the wall clock when the session opened —
specifically because "a resumed night's start is otherwise simply lost". The
recorder used `wallUtc`, the clock at the last flush *before* the restart.

**Measured: a night interrupted at 00:15 and resumed at 00:35 reported its final
wake at 06:58 against a sleeper who got up at 04:30. 148 minutes.** On the
interruption ledger row P8 says is the normal one — plugging the watch in
terminates every running app.

Two further gaps in the same arithmetic: the epochs already on disk were not on the
summary's axis, and the minutes the app spent not running were counted by nobody.
`readState` already computed that outage to classify the restart; it now reports it.

### 4. A night that failed the worn gate drew its full epoch strip

The honesty contract's central claim, broken on the most-glanced surface. The
numbers are suppressed and the first line says NOT WORN — and underneath, 100
buckets of per-epoch sleep, wake and restfulness for a night the app has just said
it cannot report on, under a caption telling the reader it came from their movement
and heart rate. **A picture of the claim is the claim.**

And a night *in progress* drew the previous night's strip — or on a fresh install one
built from zeroed memory. `Verdict::Sleep` and `Restfulness::Unknown` are both zero,
which the widget draws as a solid block of the most settled tone: a first-ever night
reported itself, live, as unbroken deepest sleep for every minute so far.

### 5. One non-finite accelerometer sample silenced the rest of the night

The filter state deliberately survives an epoch boundary, which is right — 960
settling transients a night is a signal. It also means one bad value reaches all
five poles, and every epoch after it integrates to NaN, whose conversion to
`uint32_t` is undefined and in practice **zero**.

Zero is the most dangerous number this code can produce: it is what a nightstand
looks like and it is what the soundest sleep of the night looks like. Measured, from
one NaN a third of the way in: count and peak both exactly 0 for every subsequent
epoch, with the sample count perfectly healthy — so neither the data-gap flag nor
the thin-epoch guard fires. Whether the driver can deliver a NaN is unmeasured and
now does not matter: three compares a sample against a failure that would cost a
night and would not look like one.

### 6. Webster's rule 4 was transcribed wrong

It is "a sleep bout of ≤ 6 minutes surrounded by at least **15** minutes of wake";
it was 10. Ten is looser, so the rule fired on patterns the published one leaves
alone and short sleep bouts became wake. The direction is *against* actigraphy's own
bias, so it would have read as conservatism rather than as a bug.

Found by checking the whole constant block against two reference implementations,
which is what ledger row A8 asked for. Cole-Kripke's weights, P, window and
threshold direction all came out clean.

### 7. Webster's rescoring stepped over wake epochs it had not written

After rescoring a sleep block that ran out before the rule did, the cursor advanced
by the whole rule length instead of by what it had rewritten — landing inside the
next wake run and measuring it short. On 15 wake / 1 sleep / 11 wake the 11-minute
run reads as 8, drops from rule 2 to rule 1, and two minutes the published algorithm
calls wake are called sleep. Same direction as the bias again.

### 8. A refused write left no trace anywhere

`appendEpoch` returns a bool and neither call site read it. A volume that fills at
03:00 stopped recording while the night carried on counting minutes in RAM, and the
morning summary described a night whose record stops a third of the way through with
nothing saying which third. `finishNight` returns a bool too, and nothing read that
either — so a summary and an index row that both failed still cleared the state file
and the night was gone.

A failed `close()` was also reported as a successful write. FatFs keeps the FIL and
its lock-table entry when the sync under `f_close` fails, so that is both a row that
was never committed and a lock slot that does not come back.

*A correction in the middle of this.* The first fix kept `night_state.txt` when the
index row failed, so the night would not be lost. Following it through: the relaunch
resumes into a night already summarised, and 07:00 is inside a 21:00–11:00 window,
so the session stays open all morning appending breakfast to last night's CSV and
files a "night" that ran until eleven. Splicing a morning onto a night is worse than
a missing history row. The remedy is a loud failure, not a resumable one.

### 9. A daytime charge marked the following night INTERRUPTED

The interruption bits were not cleared when a night opened — deliberately, because
charging seen in the minutes about to be backdated *is* part of the night. Nothing
bounded how far back that reached, and the flags are cleared only when a night
*closes*. A charger on the desk at six in the evening therefore reported the night
five hours later as "INTERRUPTED — was charging", which is both false and the first
line of the summary. So did the thin first epoch of any launch.

A flag that cries wolf is a flag nobody reads on the night it matters, and these
flags are the app's only way of saying a night has a hole in it. The flags are now
read off the pre-roll epochs themselves, each of which carries its own `charging`
and its own sample count.

### 10. An overslept loop silently shortened the night

The grid advances by whole epochs and skips forward when the loop wakes late: one
recording epoch absorbs the whole overshoot, with a `span_ms` far past 30 000 and a
*healthy* sample count — so the thin-epoch guard never fires — and the slots the grid
stepped over never existed. Time in bed is an epoch count. **Measured: 418 minutes
reported against the 423 the same night reported unstalled.**

Fixed twice over. The lost minutes are counted and flagged. And times of day no
longer come from index arithmetic at all: every scoring epoch is stamped with the
clock the recording epoch that closed it read for itself, which makes findings 1, 3
and 10 all structurally impossible rather than each separately patched.

### 11. A session could run for ever, and its length could wrap

`SleepWakeScorer.hpp` says the segmenter is what catches a sixteen-hour night
"rather than an array bound silently truncating it". It had no such rule. Leaving the
window is the only thing that ends a long session and that needs a readable wall
clock, which an open session deliberately does not require. **Measured: still open
after 40 000 epochs, with `sessionEpochs` round a uint16 to 4 479** — after which it
is below `minSessionMin` and cannot close on activity either, while the CSV grows
without bound.

`resumeOpen(65535)` from a corrupt state file needed one increment to do the same.

### 12. The glance was never sent anything, and the morning widget never appeared

Setting a control's text invalidates it; the carousel's tick is the only thing that
sends the form; the tick sends only when the form reports itself invalid. And
`glanceRefresh()` marked the form **valid** at the end of building it. So every tick
found nothing to send. Not stale content — none. All five of the SDK's own Glance
examples put that call after the send.

`pumpWidget()` was called from the GUI handlers and from `openNight()` and not from
`closeNight()`, so the only way to get a morning widget was to open the app and
close it again — the one thing the widget exists to avoid.

Ledger row T2 was LIKELY on the grounds that "the code builds". It did.

### 13. The history listed every night in the Americas under tomorrow's date

A night's identity is the local evening it began — that names its file, normatively.
`publishHistory` sent `startUtc / 86400`, whole UTC days, and the GUI renders that
with `gmtime` **under a comment asserting the value is already a whole local day**.
Nothing computed it. Verified under `TZ=America/New_York`: a night filed as
`20250818T233000.csv` was listed as the 19th.

A history whose dates disagree with the filenames cannot be matched to a diary, and
matching it to a diary is the only thing that can turn any row in the validation
table from *synthetic-only* into *diary-validated*.

### 14. `night_state.txt`'s path was used verbatim

Read with `%63s`, never checked against anything the app could have written, then
used to test existence, to append every epoch to, and — with `.csv` swapped for
`.json` — to build the summary path in a buffer of the same 64 bytes, which a
63-character path overflows by one because `.json` is a byte longer. The file is the
app's own and inside its sandbox, so what this guards is corruption rather than
attack: it is rewritten 1 900 times a night and the power can go at any of them.

### 15. The count scale was never related to a physical quantity

Not a bug — a gap that made every threshold unarguable. Measured: at ~48 Hz a 60 s
epoch counts about 30 per 0.001 g of 1 Hz sinusoidal amplitude. So **every threshold
in this app lives between 0.3 mg and 9 mg of mean band-limited wrist acceleration**:
micro-movement floor 0.3 mg, band "settled" 0.7 mg, movement floor 1.3 mg,
stillness-to-open 2 mg, activity-to-close 8 mg, and Cole-Kripke's own boundary about
9 mg.

That is the same order as a wrist IMU's own in-band noise. **The dangerous direction
is high noise: if the sensor's noise floor exceeds ~2 mg, no night ever opens — and
from the outside that looks exactly like a user who never went to bed.** It is the
first thing a night should establish and it is cheap to read off one.

### 16. The count is not independent of the delivered rate

The ledger's validation table claimed "independence from the delivered sample rate
across a 4× span". Measured, counts per 60 s epoch against a fixed 0.5 Hz sinusoid:
291 at 96 Hz, 286 at 48, 277 at 25, 259 at 12.5, 221 at 6, 212 at 4.8, 149 at 2.
Halving the rate costs ~9 % of every count; a tenth of it costs 26 % — **in the
direction that reads as a quieter night, which reads as more sleep**.

The dt-weighted integral does prevent the proportional failure its header warns
about. The residual is the filters: a one-pole high-pass re-coefficiented as
`tau/(tau+dt)` stops approximating its continuous form once dt approaches tau.

Not fixed. A bilinear-transform biquad is the proper answer and the header rejected
it on cost. Mitigated instead: the delivered rate is now in every summary, so two
nights can be checked for comparability rather than assumed to be comparable.

### 17. The thin-epoch guards cannot notice delivery degrading

`kMinSamplesPerRecordingEpoch` is 60 and `kMinSamplesPerEpoch` is 120, both set
against a **nominal 25 Hz** while the hardware delivers ~48. They fire only below
about 4 % of the delivered rate, while the counts are already 26 % low at 10 % of it.

**Deliberately not changed.** Tightening to a fraction of 48 Hz would blank every
night if the hardware ever honoured the 25 Hz it was asked for, which is exactly the
"too tight and real nights are suppressed" failure. The measurement that would set
them is the delivered-rate distribution across the first two nights, which is what
both TODOs already say.

### 18. One threshold's TODO named the wrong instrument

`WornGate::kMicroMovementFloor`'s TODO said to set it from a worn night and a table
night recorded "with the Tier 0 probe". The probe records `acc_n`,
`acc_ts_span_ms`, `acc_max_gap_ms`, `acc_batches` — delivery statistics — and **no
activity counts at all**, so there is no count distribution in a probe night to put
a floor between. `kMinPlausiblePct` inherited it by reference.

*A correction to my own first version of this finding.* I read that TODO, confirmed
the probe records no counts, and wrote it up as though the whole probe-first plan
were misconceived — "four constants pointed at a night that cannot set them", plus a
recommendation to drop the probe's nights. That was wrong twice over, and I found it
only when asked the obvious operational question: *do I still start with the probe?*

- **`ROLLOUT.md` phases 3 and 4 already assign the movement thresholds to SleepLab
  nights**, fed through `night_report.py thresholds`. The plan was right. One comment
  was wrong, and two of the four constants I named never mentioned the probe at all —
  they said "a diary-validated recording", which is what let the confusion spread
  rather than what caused it.
- **The probe's nights are not droppable.** It uniquely records `batt_mv`,
  `batt_ma_x10`, `batt_avg_ma_x10` and `batt_mah` — SleepLab records battery
  *percent* only — and `wakes`/`msgs`, which is the whole of ledger row S10. So S2 is
  answerable coarsely from SleepLab and precisely only from the probe, and S10 not at
  all. My first night sequence dropped both.

The lesson is the one the ledger's convention exists for, turned on myself: I
verified that the probe records no counts, and then *read* a conclusion about the
rollout plan off a code comment instead of reading the plan. Ledger rows S13 and S16.

All four count thresholds now name SleepLab, the `count` column and the rollout
phase, and `kMinWornPct` says explicitly that the probe *is* the right instrument for
it — `touch_n`, `touch_worn_n` and `touch_edges` are exactly a worn fraction and a
flicker rate. The distinction is written where somebody would get it wrong again.

### 19. Nothing on the volume said why a night had not happened

The service logs through `LOG_INFO`, which needs a UART capture and a dev tool
attached to the watch. Nobody has one attached at 03:00. So a night that produced no
epoch CSV left **nothing**: no file, no history row, no clue, and the only honest
next step was another night.

`Debug/sleeplab.log` is ~20 lines and ~2 KB a night. Its most valuable line is the
resolved-driver block, in the probe's own single-letter form, because that block is
what turned over two ledger rows in two minutes on hardware.

Writing it found a twentieth thing: `openNight` logged "recording to RAM only" when
the night file could not be created, and then recorded nothing at all — the scoring
array, the alarm, the phase, the widget and the glance were all gated on
`mStore.isOpen()`, which is a question about the *file*. A night whose CSV cannot be
created is still a night.

---

## What I could not check, and why

- **Anything about a person.** Every fixture is generated. They prove the code
  computes what it claims to compute and nothing whatever about sleep. Nothing here
  moves any row in the validation table off *synthetic-only*.
- **Anything about this hardware.** No watch was involved. Every §2 sensor row that
  was UNVERIFIED still is, except the ones this review refuted by reading.
- **`ROLLOUT.md`'s phase order, until it was pointed out to me.** Finding #18 is a
  correction to my own earlier write-up: I read a conclusion about the rollout plan
  off a code comment instead of reading the plan. It is the same failure the ledger's
  convention is designed to catch, and it caught it — one question later than it
  should have.
- **Cole *et al.* 1992 and Webster *et al.* 1982 themselves.** I checked against two
  reference implementations that cite them and agree with each other. That is why A8
  is LIKELY and not CONFIRMED, and it is why finding #6 should be re-checked by
  somebody with the papers.
- **Whether the kernel honours a `Utility` app's glance and home widget.** Finding
  #12 fixes the app's half. The kernel's half is untested and ledger row T2 says so.
- **Whether mute silences an app-requested alarm** (T1). Unreachable from any file:
  the app's record ends at `send()`.
- **BLE file retrieval** (T4). `Tools/pull_nights.py` has still never been run
  against a watch.
- **Power.** Nothing here measures a milliamp. The HR duty cycle is exercised across
  the uptime wrap and its cost is unknown.
- **Whether a real still wrist's counts land above or below `stillnessCountMax`.**
  Finding #15 gives the conversion; only a night gives the number. This is the one
  gap that could stop the app working at all rather than working wrongly.

---

## The shortest sequence of nights

Ordered so each night settles the most, and every night has a success criterion
decidable in the morning rather than after a week of analysis.

**This section replaces an earlier version that dropped the probe's nights.** See
finding #18: doing so would have lost ledger row S10 entirely and reduced S2 to a
battery-percentage estimate. `ROLLOUT.md`'s phase order is right. What follows is that
order with a success criterion added to each night, because a night should be callable
wasted the morning after rather than after a week of analysis.

**Before any of them, at a desk, minutes each:** promote P1 by reading the firmware
revision; run `docker-build.sh app`, `tests` and `sim-run`, all three, because each
catches things the other two cannot (P14); mute the watch, set `alarm_at` two minutes
ahead inside the window, and wait — that settles T1, and it is five minutes rather
than a night.

### Probe nights — `ROLLOUT.md` phase 1, unchanged

Read the probe's screen for thirty seconds before bed each time: upper case is a
resolved driver, lower case is not. Delete `Apps/SleepProbe/` before installing
SleepLab; both autostart and neither has a stop button.

**Probe night 1 — worn, unplugged, defaults.** S1, S3, S5, S7, S9, S10, and the
expensive half of S2.
*Success criterion:* `probe_report.py` shows one run spanning the night with no
`uptime_ms` gap over 90 s. A run boundary mid-night is the charger (P8) and the night
is void.

**Probe night 2 — worn, unplugged, `"hr": "off"`.** The cheap half of S2. The
difference in `batt_avg_ma_x10` is what heart rate costs.
*Success criterion:* one run, and a measurably different average current from night 1.

**Probe night 3 — on a nightstand, defaults.** The false-worn half of S7 — whether
`TOUCH_DETECT` reports worn for a watch on furniture.
*Success criterion:* `touch_worn_n` against `touch_n` answers it either way. No
failing outcome here, only an informative one.

**Stop if the answer is bad.** Delivery dying at 02:00, or continuous HR costing most
of the battery, is a finding about the design rather than a wasted night.

### SleepLab nights — phases 3 to 5, with criteria

**Night 4 — worn, unplugged, HR continuous, diary kept.** The worn-night count
distribution for finding #15, plus diary night 1 of 10.
*Success criterion, decidable at breakfast:* a summary JSON exists; `worn` is `worn`;
`acc_hz_x10` within 20 % of 480; no epoch row has `samples` 0; `interruption` is 0.
Anything else and the night is diagnostic rather than calibration data — still worth
having, but say so before analysing it.

**Night 5 — on a nightstand, HR continuous.** The table-night count distribution:
the other half of finding #15 and of A5.
*Success criterion:* the report says NOT WORN, **and** this night's counts' 95th
percentile is below night 4's 5th. If they overlap, `kMicroMovementFloor` cannot be
set from movement alone and the gate has to lean on heart rate — a design finding, not
a threshold. `night_report.py thresholds` says so in those words.

**Nights 6–14 — worn, unplugged, HR continuous, diary kept.** Nine more diary
nights. Not experiments: the sample the A9 calibration needs. Ordinary nights.
*Success criterion per night:* a summary exists and `worn` is `worn`. A night that
fails is replaced, not analysed.

**Then, at a desk:** build `night_report.py rescore` (gap G4) and sweep `kCountScale`
against the ten diary nights. That is the one number standing between "cites a real
paper" and "is validated", and the only thing on this list that can move a
validation-table row.

**What can share:** the three probe nights cannot share with each other or with
SleepLab's, because a different app is installed. Night 5 cannot share with anything.
Nights 6–14 share everything. **Total 14 nights** — which is what `ROLLOUT.md`
already implied. The two-night saving my first version claimed was not real. What
would make it real is the merge in `POST-MORTEM.md` § "Should the probe and SleepLab
stay separate?", which collapses probe nights 1–3 into SleepLab nights 4–5. It is
unbuilt.

---

## Verdict

**Yes, it is ready to spend a night on — and it was not before this branch.**

Every finding above except #16, #17 and #18 is fixed, with a test that failed first.
The three that are not fixed are all *documented rather than silent*, which is the
distinction that matters: a night can now say what rate it was recorded at, so a
degraded night is identifiable in the morning rather than mistaken for a quiet one.

What changed the answer, in order of how much:

1. **The offline harness.** Eleven of its first fifteen scenarios failed. Without it,
   findings 1, 2, 3, 4, 8, 9, 10 and 12 would each have taken a night to notice — if
   they were noticed at all, which four of them would not have been, because they
   produce plausible numbers.
2. **Findings 1, 2 and 3.** A fifteen-minute offset in every night, and a
   two-and-a-half-hour offset after every charge, in the figures the diary
   calibration compares against. Spending nights on the old build would have
   produced ten nights of data that calibrated the constant to absorb a bug.
3. **Finding 19.** A first night that fails to record is likely, not unlikely — it is
   the first time this app has met a wrist. Before the diagnostic log, such a night
   cost eight hours and bought nothing at all.

What would still make me hesitate, and what to do about it:

- **Finding #15 is the one that can make the first night worthless**, and it is not
  a bug — it is that four thresholds and the scorer's operating point all sit within
  a decade of a wrist IMU's own noise floor, and nobody has measured where that
  floor lands. If it is above ~2 mg in the 0.25–3 Hz band, **no night will ever
  open**, and the symptom is indistinguishable from a wearer who did not go to bed.
  Mitigation: check `Debug/sleeplab.log` after the first hour of night 1. A `launch`
  line and a `sensors` line with no `open` line, an hour into the bedtime window
  while lying still, is that failure — and it is visible in an hour rather than in a
  morning. If it happens, read the counts out of the CSV that the pre-roll would
  have written and set `stillnessCountMax` from them. That is a five-minute fix on a
  night that would otherwise have been wasted.
- **Nothing here is validated against a person**, and the app says so in every
  summary JSON, on the report screen, and in the README. That posture is the thing
  most worth not weakening as the calibration arrives.

The shortest path to a first night is the desk work at the top of the sequence
above: read the firmware revision, run the probe's screen for two minutes, run all
three builds, and test the alarm awake. That is under an hour, and it is the
difference between a night that produces evidence and a night that produces a
question.
