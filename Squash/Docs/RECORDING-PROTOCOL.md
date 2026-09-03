# The recordings this app needs, and what each one settles

Nothing in this app displays a number, because nothing has been measured. This
document is the shortest path out of that: **nine recordings, about four and a
half hours of court time**, after which every threshold in
[`EffortKit`](../../EffortKit) has a value and a provenance, or a stated reason
it cannot have one.

Read it before the first session, not after. The marking protocol has to be
decided in advance — the watch has one marker button and `kind` is always
`MANUAL`, so what a press *meant* comes from the protocol and from the labels
file, never from the device.

---

## 0. Before any of it — the hard constraint that shapes every session

**A recording stops after 30 minutes.** `ImuCsvRecorder::skDefaultMaxDurationMs`
is 30 minutes and `skDefaultMaxBytes` is 8 MB, whichever trips first, and the
Service uses the default overload so neither is configurable from the phone. A
row is 44 bytes typical and 53 worst case, so 100 Hz costs ~4.3 KiB/s and 30
minutes is ~7.9 MB — the two caps are matched to each other deliberately.

So **a session longer than 30 minutes is several recordings, not one.** Stop and
save the activity, start a new one, and carry on. Each file gets its own markers
starting at seq 1 and its own labels file. `phase-a` takes any number of files
at once, so nothing is lost by splitting — but a state that straddles the split
is cut in two, which matters for the long off-court intervals in group T below.
Plan the split for a moment you are on court, not off it.

## 0.4 The first check, which takes thirty seconds

Before the dry run, before a strap, before any recording: **open the app, back
out, plug in, and read one line.**

```sh
cat "/Volumes/UNA WATCH/Apps/Squash/Debug/squash.log"
```

```
0 <utc> launch v<version> abi=3384192379 expect=3384192379 ok=1 calibration=0
```

`Service::run()` writes that before it loads settings, before it reads the
profile and before it touches a sensor, so merely opening the app produces it.
Five things have to be true for it to appear as above, and each of them makes
everything after it pointless if it is wrong:

| What to read | What it proves |
| --- | --- |
| the file exists | The kernel launched the app — so it is registered, and its kernel interface version is one this firmware accepts |
| `v<version>` | It is the build you just installed, not an older one still registered |
| `ok=1` | The Rust engine linked and its struct layout matches the C++ side. `ok=0` means every session field would be misread, silently |
| `calibration=0` | The honesty gate is in the state it should be, with no threshold pretending to exist |
| any of it, at all | The log path works on FatFs, which no host test can show — they run against an in-memory fake |

**If the file is absent, stop.** The app did not launch, and nothing below is
worth trying until it does.

## 0.5 The ten-minute dry run, before you drive anywhere

Do this once, at home, with the strap on. It costs ten minutes and it is the
only thing standing between a format or a setting being wrong and finding that
out after four and a half hours of court time.

**Build and install a version above the one already on the watch** — the phone
matches by `appVersion`, so a locally built `0.1.0` will not offer to replace an
installed `0.6.0`:

```sh
BUILD_VERSION=0.7.0 Squash/Tools/docker-build.sh app
```

**Install it in this order, which is `Utilities/Scripts/Update-Watch-Apps.ps1`'s
and not negotiable.** Each step exists because of a specific silent failure:

1. **Check the `.uapp`'s CRC-32 footer before it touches the watch** —
   `crc32(file[:-4])` must equal its last four bytes read little-endian, and the
   app packer prints the same number. A file that fails CRC is dropped
   *silently* by the kernel, so the app never appears and nothing says why.
2. **Write the new `.uapp`** into `Apps/Squash/`.
3. **Read it back and compare the length** to the source.
4. **Only then delete every other `.uapp` in that folder.** Not tidiness: *the
   watch loads whichever it finds first, so leaving two keeps booting the old
   build.* Doing it in this order means a bad copy never leaves the folder
   without a working binary. The app's `settings.json`, `input.json`,
   `Activity/` and `Imu/` are untouched by all of this.
5. **Eject, reconnect, and verify by hash.** Hashing straight after writing
   reads the OS write cache and can report a false OK.
6. **Reboot the watch.** The launcher list and the app registry are rebuilt only
   at boot. Editing `Apps/app_list.json` by hand does nothing — the kernel
   overwrites it from its own table.

Step 4 is the one that has actually bitten: three successive builds sat inert in
that folder for days beside an older one that kept booting. Nothing announces
it — the app opens, an activity starts, and the only symptom is that the new
build's output is missing, which looks exactly like the new build's logging being
broken. **So read the version in `Debug/squash.log`'s `launch` line before
trusting anything else**, which is why §0.4 reads it first.

Then, on the watch: tick **Record raw IMU**, pair the strap, start an activity
and run a miniature of group S — three minutes standing, sixty seconds hard,
three minutes standing — pressing R2 at each of the two transitions plus a
bookend at each end. Save it.

Plug in over USB and check, in this order. Each line is a different failure, and
each one would have cost a whole session:

| Check | What it rules out |
| --- | --- |
| `Apps/Squash/Debug/squash.log` exists, and its `launch` line says `ok=1 calibration=0` | The two languages disagreeing about their shared structs, which would misread every session field silently |
| `Apps/Squash/Debug/sessions.csv` has a row | The profile path never running |
| `Imu/YYYYMM/` holds **three** files with the same stamp | `recordImu` reading as off — a JSON `"true"` instead of `true` is treated as absent, and absent is off |
| `imu_<stamp>_hr.csv` has more than a header | No heart rate reaching the sidecar at all |
| Its `source` column is **2**, and `external_x100` is populated | The strap not actually being used. If it says 1, the watch is on optical and every group S recording would be worthless |
| The sample file is roughly `seconds × 4.3 KiB` | The recorder stopping early on a cap |
| Free space on the drive is comfortably above **90 MB** | The later recordings silently truncating: eleven files at up to 8 MB each, and the SDK exposes no free-space query, so nothing on the watch will warn you |
| Battery drop over the ten minutes | A 30-minute recording flattening the watch mid-session. Nobody has measured this yet |

Then copy the three files off and run the analyser over them:

```sh
cd EffortKit
cargo run --features std --bin phase-a -- /path/to/imu_*.csv --report dryrun.md
```

**Any line under `### Warnings` means stop and fix it before playing.** A clean
run prints the cadence and quantisation tables, which is A1's first two answers
already in hand — and if the strap was on, a settling time from the one
transition, which tells you whether the full group S is worth the trip.

## 1. Turning it on, every time

1. On the phone, tick **Record raw IMU** on the Squash card. Or over USB, put
   `{"schema":1,"values":{"recordImu":true}}` in `Apps/Squash/input.json` — see
   [`input.example.json`](../input.example.json). A JSON `true`, not `"on"`:
   a value of the wrong type reads as absent, and absent is off.
2. Pair the chest strap where the recording calls for one, and confirm the
   sensor status row shows it before starting.
3. Watch on the usual wrist, worn as usual. Group W is the one exception.
4. Start the activity **before** you walk on court, so the walking-on is in the
   file.

Each recording produces four files. Three the watch writes, sharing one clock:

| File | Written by | Carries |
| --- | --- | --- |
| `Imu/YYYYMM/imu_<stamp>.csv` | `ImuCsvRecorder` | 100 Hz six-axis, raw LSB |
| `imu_<stamp>_events.csv` | `ImuMarkerLog` | every R2 press, `t_ms,seq,kind` |
| `imu_<stamp>_hr.csv` | `HrCsvLog` | 1 Hz heart rate, arbitrated and per-source |

The fourth you write yourself afterwards — see §3.

## 2. The marking protocol

**Press R2 at every change of state, and nowhere else.**

That is the whole rule. A press is a boundary; it does not say what the state on
either side was. What each stretch was comes from the labels file, which is why
the press can stay this simple and this fast — one button, at a moment you are
already stopping.

Two presses bracket every recording:

- **The opening bookend**, pressed as the activity starts, before you move.
- **The closing bookend**, pressed as you stop.

Everything between them is a state boundary. In a match that is: the end of the
knock-up, then one press at the end of each rally and one just before each
serve. Both are moments you are stationary, and there are two of them in every
inter-rally gap, which at club level is ten to twenty seconds — comfortable.

**If you miss one, do not try to fix it on the watch.** A double-press is not a
correction; it is a second boundary that will silently shift every label after
it. Write it in the paper log instead ("missed the serve mark on the third rally
of game 2") and carry on. `seq` is 1-based and gap-free, so the analyser can
tell a truncated sidecar from a missed press, but it cannot tell a missed press
from a genuine state you forgot to mention.

**Keep a paper or phone log.** Wall-clock start time, opponent, which wrist,
strap or not, and anything that went wrong. It takes a minute and it is the only
thing that can reconstruct a recording whose labels stop making sense.

## 3. The labels file

Write it the same day, from the paper log, while you remember. It goes beside
the recording as `imu_<stamp>_labels.txt`.

One state name per line, in order, one line per stretch between markers. N
markers give N+1 stretches, counting from the start of the file to the first
marker and from the last marker to the end.

```
# imu_20260904T183000_labels.txt -- M1, club match vs Alex, strap on
idle          # walking on, before the opening bookend
knockup
alternate rally rest
```

`alternate <a> <b>` fills every remaining stretch by alternating the two, which
is what a match is after the knock-up. Everything before it is listed
explicitly. Lines beginning `#` are comments, and `#` also ends a line.

The names this build knows: `rally`, `rest`, `off_court`, `knockup`, `drill`,
`idle`. Anything else is read as `unknown`, reported as a warning, and excluded
from every distribution — which is the right outcome, but means a typo silently
shrinks a class. `phase-a` prints the label counts; check them against what you
expect before believing anything downstream.

---

## 4. The recordings

Nine, in three priorities. **Group S first** — it is the only one that can
declare a whole feature unbuildable, and finding that out after collecting four
hours of match data would be a waste of four hours.

### Group S — the heart-rate step test (2 recordings, ~45 min)

*Settles A1: the update cadence, the quantisation, the settling time, and
whether wrist optical is usable at all.*

| | S1 | S2 |
| --- | --- | --- |
| Where | On court | On court |
| Strap | **Yes**, paired and confirmed | **No** — optical only |
| Duration | 21 min | 21 min |
| Day | Any | A different day from S1 |
| Priority | **Required** | Strongly wanted |

**The protocol, three times over:**

1. Three minutes standing still. Press R2. *(label: `idle`)*
2. Sixty seconds of maximal ghosting — court sprints to all four corners, as
   hard as you can hold for a minute. Press R2. *(label: `drill`)*
3. Three minutes standing still, not walking, not stretching. Press R2.
   *(label: `idle`)*

**The intensity is the requirement, not the duration.** The bout must take you
to **at least 85% of your maximum heart rate**, or the step down is too small to
time against a signal that quantises. If the third bout does not get you there,
the recording has two usable steps, not three; say so in the log. A bout that
tops out at 70% is not a small measurement, it is not a measurement.

**Stand still in the rest.** Active recovery blunts the fall, and a walk-it-off
rest measures the walk. This is the single most important instruction in group S
and it is the one most easily forgotten while breathing hard.

S2 exists because S1's optical column is recorded *while a strap is present*,
and the kernel arbitrates differently when one is not. If only one of the two
gets recorded, make it S1: it carries both channels simultaneously, which is the
cleaner comparison.

### Group M — match play (3 recordings, ~2 hours)

*Settles A2 for rally against rest, which is the segmenter's central question.*

| | M1 | M2 | M3 |
| --- | --- | --- | --- |
| Opponent | One | **A different one** | **A third** |
| Strap | Yes | Yes | Optional |
| Play | ≥25 min | ≥25 min | ≥25 min |
| Include the knock-up | Yes, labelled `knockup` | Yes | Yes |

**Different opponents matter more than more matches.** Rally length and
intensity distribution depend heavily on opponent standard; three matches
against the same player calibrate the segmenter to that player. If only one
opponent is available, vary the standard instead — play one deliberately loose
and one flat out.

**Why three.** Expect 50 to 80 rallies per match, so three gives 150 to 240.
Epochs are the unit the segmenter thresholds on, and a 15-second rally is 15 of
them — but epochs inside one rally are highly correlated, so the effective
sample size is **the number of rallies, not the number of epochs**. At 150
rallies the 5th and 95th percentiles rest on seven or eight independent
observations each, which is the least that makes a percentile mean anything, and
the percentiles are exactly where a hysteresis machine's two levels come from.

### Group T — threes (2 recordings, ~1 hour)

*Settles whether off court is separable from resting between rallies at all —
the question §2 of the brief says a two-state model gets wrong.*

Three players rotating, so whole games are spent off court. Press R2 walking off
and again walking back on, as with any other boundary.

| | T1 | T2 |
| --- | --- | --- |
| Duration | 30 min (one recording's worth) | 30 min, a different day |
| Off court | At least two whole games | At least two |
| What you do off court | **Whatever you normally do** | Deliberately different — if you sat for T1, stand for T2 |

That last row is the point of having two. Off-court behaviour is not one thing:
sitting, standing watching, stretching and going for water look nothing alike to
a wrist accelerometer, and a level tuned on sitting will call standing a rest. If
the two sessions disagree, that is the answer — `OffCourtRule::Indistinguishable`
is the honest calibration and the app reports rest and off court merged, with
`off_court_separable` false.

Twenty or so off-court intervals across the two is enough to see **whether** a
level exists. It is not enough to tune one finely, so treat a clean separation
as provisional and a messy one as settled.

### Group D — drills (2 recordings, ~40 min)

*Settles what the app must say about a session that has no rally structure.*

| | D1 | D2 |
| --- | --- | --- |
| What | Continuous solo drives and boast-drive-drive | Feeding — one player feeds, the other moves |
| Duration | 20 min | 20 min |
| Rests | None; if you stop, mark it and label it `rest` | None |
| Label | `drill` throughout | `drill` throughout |

Without these, the rule in §7 of the brief — *report that the session was not
rally-structured rather than a rally count* — has nothing behind it. D2 matters
separately from D1 because feeding movement is unlike rally movement in a way
solo drilling is not: the feeder barely moves and the mover never plays a
defensive shot.

### Group N — worn but not playing (1 recording, ~15 min)

*The null class. Without it `idle` and `off_court` are indistinguishable by
construction, because nothing has ever been labelled `idle`.*

Fifteen minutes of: walking to the club, changing, standing and talking, sitting
down, tying a shoe, carrying a bag. Mark the boundaries you notice; label the
whole thing `idle` if you did not.

### Group W — the off-wrist control (1 recording, ~30 min)

*One match with the watch on the **non-racquet** wrist.*

The implementation brief says per-shot features are unavailable off wrist and
the app must detect that case rather than report degraded numbers. It also
predicts that movement and heart-rate analytics survive. This is the recording
that decides both. Label it exactly as a group M match and note the wrist in the
log — the analyser cannot tell.

---

## 5. What you have at the end

| Group | Files | Court time | Unlocks |
| --- | --- | --- | --- |
| S | 2 | 45 min | A1 entirely: cadence, quantisation, settling time, optical vs strap. Can declare recovery unbuildable. |
| M | 3 | 2 h | Rally vs rest levels and dwell times; the knock-up class. |
| T | 2 | 1 h | `OffCourtRule`, or the verdict that there is not one. |
| D | 2 | 40 min | The drill verdict, and the rally-vs-drill overlap. |
| N | 1 | 15 min | The `idle` class. |
| W | 1 | 30 min | Off-wrist behaviour of everything above. |
| **Total** | **11 files, 9 sessions** | **~4 h 30** | |

Five of them carry a strap, which is also the count the baselines need: no
comparison is offered until five admitted sessions exist
(`baseline::MIN_SESSIONS_FOR_COMPARISON`), and a session is admitted only with
ten minutes of active time and a trusted heart rate across 80% of it.

## 6. Checking a recording before relying on it

Run the analyser over everything collected so far:

```sh
cd EffortKit
cargo run --features std --bin phase-a -- /path/to/imu_*.csv \
    --report phase-a.md --epochs epochs.csv
```

The glob picks up the sidecars too; the analyser drops them and says how many,
because they are read from the recording they belong to rather than passed in.
Both outputs are files on purpose: a report that exists only in a terminal's
scrollback is a report nobody has when it matters.

Then check, in this order:

- [ ] **No warnings.** A marker seq gap means the sidecar is truncated or the
      presses were reordered; a malformed row means the file is damaged. Both
      are reported by name and line.
- [ ] **The label counts match what you remember.** `phase-a` prints an `n` per
      state. A typo in the labels file shows up here as a class that is smaller
      than it should be, or an `unknown` row that should not exist.
- [ ] **Epochs are complete.** The dumped `samples` column should be at or near
      100. Substantially fewer means the sensor dropped batches, and those
      epochs are computed over whatever arrived.
- [ ] **For group S: the heart rate actually got there.** Peak bpm in the file
      against 85% of your maximum. If not, the recording contributes to
      cadence and quantisation but not to the settling time.
- [ ] **The recording is as long as the session was.** A file that stops early
      hit a cap; `Stop::SIZE_LIMIT` or `DURATION_LIMIT` is in the log line the
      Service writes at save.

## 7. What happens next

`phase-a`'s report is the input to
[`PHASE-A.md`](PHASE-A.md), which records the numbers and the verdict. Only then
does a calibration get written: one `const` carrying a `Provenance` naming these
recordings and the date. Until that constant exists, `Calibration::Absent` is the
only value the watch build can construct, every metric returns
`Unavailable::NotCalibrated`, and the ledger in
[`FEASIBILITY-LEDGER.md`](FEASIBILITY-LEDGER.md) has no validated row.

That is the intended state today, and it is why nothing is on screen.
