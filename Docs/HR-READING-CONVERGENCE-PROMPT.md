# Seven apps read the heart-rate sensor. Work out what should exist instead.

Every activity app in this repository reads heart rate, and each one does it in
its own `Service.cpp`. The code is short, near-identical in intent, and disagrees
on a point of correctness that the SDK documents and that five of the seven get
wrong — or appear to.

**Your job is not to write an `HrReader`.** It is to work out from the evidence
whether one should exist, what belongs in it, which language it is written in,
and what it would cost to adopt across apps that work today and that you cannot
test. The answer may be "a shared reader", "a documented rule and no code", or
something neither.

There is a precedent for this exact shape of problem in this repository:
[`RECOVERY-CONVERGENCE-PROMPT.md`](RECOVERY-CONVERGENCE-PROMPT.md) and the
[`RECOVERY-CONVERGENCE.md`](RECOVERY-CONVERGENCE.md) it produced, which merged
two independent implementations of one measurement into `EffortKit`. Read both
before you start. The outcome there was a merge; that is not evidence this one
should be.

---

## 0. The honesty contract, which outranks everything here

This repository's standing rule, from `CLAUDE.md`:

> **Prefer measurement over assertion.** If you are choosing a number, measure it
> and record what you measured.

A recommendation of the form "a shared reader is cleaner" is worth nothing here.
One of the form "a shared reader, because X apps do Y and the recordings in
`Squash/Tests/pulled` show Z" is worth something. Where you cannot measure, say
you are asserting, and say what would settle it.

**Do not trust this document's framing.** It was written immediately after
shipping the seventh implementation and then finding that the sixth had already
solved the problem correctly. That is a conflict of interest in the direction of
over-valuing convergence. It also contains one unexplained observation, in §2.3,
which if resolved the other way removes most of the motivation for acting at all.

---

## 1. What is actually duplicated

### 1.1 The heart-rate read

Seven Services subscribe to a heart-rate sensor. Which one they subscribe to:

| App | `HEART_RATE` (0x41) | `HEART_RATE_EX` (0x43) |
|---|---|---|
| BikeMap | | ✓ |
| GpsLab | | ✓ |
| HikeMap | | ✓ |
| RunMap | | ✓ |
| Spin | | ✓ |
| SleepLab | ✓ | ✓ |
| Squash | ✓ | ✓ |

The SDK's own `Docs/ExternalSensors.md` says which is which:

> `HEART_RATE` is unchanged — it stays the stable 2-field (BPM, trust) frame …
> Source provenance is exposed separately, opt-in, via the `HEART_RATE_EX`
> sensor type … Use this to label records or log separate FIT series … **do not
> gate HR display on it.** Apps that only need BPM should stay on `HEART_RATE`
> and ignore `HEART_RATE_EX` entirely.

The two parsers also disagree about what a valid frame is —
`HeartRateEx::isDataValid()` wants `getFieldCount() >= 7`,
`HeartRate::isDataValid()` wants `== 2` — so when EX is not producing, every
getter returns `0.f` and an absent reading is indistinguishable from a heart
rate of zero.

### 1.2 Where the pattern came from

It is not seven independent decisions. The SDK's own activity examples subscribe
to `HEART_RATE_EX` only — `Cycling`, `Hiking`, `Running`, `Treadmill`, `Workout`
— while its HR-display examples (`GlanceHR`, `HRMonitor`) use `HEART_RATE`.
Squash's README records that it is "derived from the UNA SDK's MIT-licensed
Workout example"; the map apps share an ancestor too. **Every app inherited the
pattern from a sample app, and the sample apps contradict the SDK's own
documentation.**

That changes the question. This is not seven authors making the same mistake; it
is one template propagating. Whatever you recommend has to say what happens the
next time somebody copies `Workout`.

### 1.3 Two apps reached a different answer independently

`SleepLab` subscribes to both and says why, in
`SleepLab/Docs/IMPLEMENTATION-PROMPT.md`:

> `HEART_RATE_EX` — Arbitrated + source (optical/external) + per-source BPM and
> trust — use it so HR provenance is recorded, not assumed.

`Squash` arrived at the same split on 2026-09-13, from the SDK docs, after a
live bug — and did not find SleepLab's answer first. Two apps, the same
conclusion, no shared code and no cross-reference.

### 1.4 Freshness

A reading that stops arriving must stop being displayed. Two implementations,
neither aware of the other:

| Where | Threshold | Derived from |
|---|---|---|
| `RustGuiPoc/Software/Apps/CustomGUI/Gui.hpp` | `kStaleAfterMs = 2500` | the sensor layer's ~1 s aggregation |
| `Squash/Software/Libs/Header/Freshness.hpp` | 3 s | 6,101 HR gaps in `Squash/Tests/pulled` |

`RustGuiPoc/README.md` states the rule both encode:

> **`Gui.cpp` decides whether a sample is fresh; the renderer never guesses.**
> Past `kStaleAfterMs` the screen shows `NO DATA` rather than the last number it
> saw.

The five other HR apps have no freshness check at all. Establish whether that
matters for them before assuming it does: an app that writes a FIT record per
second and never displays a live number has a different exposure from an
instrument being read mid-rally.

### 1.5 The diagnostic log

Three files, three implementations: `Debug/mapmanager_verify.log`,
`Debug/sleeplab.log`, `Debug/squash.log`. Same motivation each time — `LOG_INFO`
needs a dev tool attached and nobody has one in the field. Different shapes,
because each answers a different question. In scope only as far as §3.5.

---

## 2. Settled, or deliberately not — do not re-derive these

### 2.1 The stream, from `Squash/Tests/pulled/*_hr.csv`

Seven recordings, 6,101 inter-sample gaps, one wearer:

| Measured | Value |
|---|---|
| Nominal cadence | ~1005 ms, 96.7% of gaps |
| One tick missed | ~2010 ms, 3.0% of gaps |
| Largest gap ever observed | 2117 ms |
| Gaps ≥ 3 s | none |
| Untrusted seconds | 5.3% |
| Every value | a whole bpm; 65% of consecutive steps are exactly zero |

`EffortKit/src/hr.rs` already records the quantisation finding and what it
forbids. Re-derive with `python3 Tools/hr_analyse.py`.

### 2.2 What already exists in Rust

`EffortKit/src/hr.rs` models `HrSource`, `HrSample`, `SourcePolicy`, and the
rule that zero means "no trusted reading" and is never a heart rate. It is
`no_std`, allocation-free, SDK-free and host-tested. **Whatever a reader decides
about trust and source, this crate has already defined the vocabulary for it.**

### 2.3 The observation this document cannot explain

Squash 0.6.0 subscribed to `HEART_RATE_EX` **only** and produced correct
readings — `Squash/Tests/pulled/20260913-v0.6.0-70min-match/` has 4,087 samples,
mean 109.5 bpm, from that build. The rewritten Squash subscribed to EX only and
showed nothing on a worn watch. Adding `HEART_RATE` fixed it. The other five apps
still use EX only and, as far as anyone knows, work.

**Nobody knows what changed.** Candidate explanations, none verified:

- Something else in the old build caused EX to produce — it also subscribed to
  `BATTERY_LEVEL` and `BATTERY_METRICS`, which the rewrite dropped.
- EX is produced only when some other subscriber is present, making it genuinely
  a companion in a way the docs do not state.
- A firmware or kernel change between 2026-09-03 and 2026-09-13.
- Something about the CustomGUI process model versus TouchGFX.

**Settle this first.** If EX alone is fine, the five apps have no bug, the
motivation drops to ordinary duplication, and the right answer may be a
documented rule rather than code. If EX alone is unreliable, five shipped apps
can silently lose heart rate and that is the whole priority.

The cheapest experiment: build Squash with EX only plus the two battery
subscriptions, wear it, read `Debug/squash.log`. One session settles it.

---

## 3. The questions that are open

### 3.1 Is there anything to fix, or only something to document?

See §2.3. If the answer is "document", say exactly where — the SDK's sample apps
are upstream and not this repository's to change.

### 3.2 What would a shared reader actually own?

Candidates, and each needs an argument for inclusion rather than an assumption:

- which sensors to subscribe to, and the arbitrated-vs-provenance split
- freshness, and whether one threshold can serve a 1 Hz instrument and a
  once-a-second FIT record
- the trust floor, which `EffortKit::window` already treats as a calibration knob
- per-source readings, which only apps writing a sidecar or a FIT series want

A reader that owns all four is a framework. A reader that owns the first is
fifteen lines. Say which, and why the line falls there.

### 3.3 Which language?

The repository has both kinds of shared code and the split is not dogma:

| Rust | C++ |
|---|---|
| `EffortKit`, `TextKit`, `InscribedDisc`, `PanicKit` | `MapKit`, `SettingsKit` |

The observable pattern is that pure decisions that can be tested without a
kernel are Rust, and anything holding SDK types is C++. An HR read straddles it:
subscribing and parsing are SDK types; deciding what a reading means is not, and
`EffortKit/src/hr.rs` already models it.

So the real question is not "Rust or C++" but **where the seam goes** — and
whether a second C ABI boundary is worth it for logic this small, when
`EffortKit`'s README already records what a shim costs and why each app owns its
own staticlib. A thin C++ reader that produces `EffortKit`'s vocabulary is one
answer. A C++-only kit alongside `SettingsKit` is another. Pick, and price it.

### 3.4 Adoption, across apps nobody can test

Five apps work today. Changing them means editing code that cannot be verified
without wearing the watch through five activities, and the failure mode is
silent — a lost heart rate looks like a quiet sensor.

Options, with the cost of each: leave them and document; change only apps being
touched for other reasons; change all of them behind one hardware session per
app; or change none and put the rule in `CLAUDE.md`.

### 3.5 Does the diagnostic log converge too?

Three implementations, one genuinely generic core — append, bounded, restart at
a cap, never fail the session — and three different payloads. Worth folding in,
or a separate question? Answer it in one paragraph; do not design it here.

---

## 4. Traps

- **Do not build a sensor framework.** Seven Services differ in real ways. A
  shared abstraction over all of them is how you get a layer nobody can change.
- **The five apps are not known to be broken.** §2.3 is unresolved. Treating them
  as buggy without settling it is the same error this document was written after.
- **`HEART_RATE_EX` is not a superset in practice.** Its validity rule is
  stricter, so it fails differently, not less.
- **Zero is not a heart rate.** `EffortKit` is explicit; a reader that returns
  `0.f` for "no frame" and for "no beats" has destroyed the distinction.
- **A threshold is not a preference.** 2500 ms and 3 s were derived from
  different evidence. If they converge, say which evidence wins; if they do not,
  say why one sensor differs from another.
- **Upstream is not ours.** The sample apps propagate the pattern and live in the
  SDK. Any recommendation that depends on changing them is a recommendation to
  open a PR there, and should say so.

---

## 5. What to produce

1. **An answer to §2.3 first**, or an explicit statement that you could not
   settle it and what it would take. Everything else is contingent on it.
2. **A recommendation**, in one paragraph, that someone could act on.
3. **What it owns and what it does not**, as a list, with the argument for each
   boundary.
4. **The language and the seam**, with the cost of the ABI if there is one.
5. **What is given up.** If the answer is "nothing", you have not understood one
   of the seven.
6. **A migration**, app by app, each with what would verify it on hardware.
7. **The questions you could not settle**, and the session or recording that
   would settle each.

Do not write the implementation. This is a design pass.

---

## 6. Out of scope

- HRV and rMSSD; the beat-to-beat pathway is not in a released SDK.
- What the `.fit` contains. Field numbers are verified against two independent
  copies of the FIT profile and a wrong one is silent and permanent.
- The recovery detector. `EffortKit` owns it and
  [`RECOVERY-CONVERGENCE.md`](RECOVERY-CONVERGENCE.md) records why.
- Anything phrased as advice about a person.

## 7. Where the evidence is

```sh
# Who subscribes to what, locally and in the SDK's own examples
for d in */Software/Libs/Sources/Service.cpp; do
  grep -oE 'Type::HEART_RATE[_A-Z]*' "$d" | sort -u | sed "s|^|$(dirname $d) |"
done
grep -rl HEART_RATE "$UNA_SDK"/Examples/Apps/*/Software/Libs/Sources/Service.cpp

# What the SDK says the two sensors are for
sed -n '55,80p' "$UNA_SDK"/Docs/ExternalSensors.md

# The stream itself
python3 Tools/hr_analyse.py
cargo run --features std --bin phase-a -- Squash/Tests/pulled/*/imu_*.csv

# The vocabulary that already exists
EffortKit/src/hr.rs            EffortKit/README.md
RustGuiPoc/README.md           # the freshness rule, stated
SleepLab/Docs/IMPLEMENTATION-PROMPT.md   # the split, reached independently

# The precedent for this kind of pass
Docs/RECOVERY-CONVERGENCE-PROMPT.md   Docs/RECOVERY-CONVERGENCE.md
```
