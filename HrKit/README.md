# HrKit

Two decisions every app reading the heart-rate sensor has to make. Header-only,
free of SDK types, host-tested.

```cpp
#include "HrGate.hpp"

// On a sensor frame:
mHr.onReading(parser.getBpm(), parser.getTrustLevel(), mKernel.sys.getTimeMs());

// On the display or record tick:
const HrGate::Hold::View hr = mHr.view(mKernel.sys.getTimeMs());
guiSend(hr.bpm);                       // 0 means show no heart rate
fitRecord.set(Field::HEART_RATE, hr.measured);
```

## Why it exists

`Docs/HR-READING-CONVERGENCE.md` is the design record; this is the short
version.

Seven apps in this repository read the heart-rate sensor. Four of them carried
the same validity test character for character, with its four numbers as bare
literals:

```cpp
mHrCounter.getCurrent() > 20 && mTrackData.hrTrustLevel >= 1 && mTrackData.hrTrustLevel <= 3
```

And two of them had each written half of the staleness rule, neither aware of
the other. `Spin::HrHold` bridged seconds the kernel did not stand behind but
had nothing to say about the stream stopping; `Squash::Freshness` blanked a
reading that had stopped arriving but flickered on a one-second dip in
confidence. **Each app shipped exactly the half the other was missing**, which
is what `HrGate::Hold` is: both windows, and they are not interchangeable.

| Window | Guards against | Outranks |
|---|---|---|
| `kTrustHoldMs` = 10 s | a frame arriving with a value nobody believes | — |
| `kArrivalGateMs` = 3 s | frames not arriving at all | the trust hold; no hold survives the stream stopping |

## The two answers, because the screen and the file ask different things

`View::bpm` may be a held reading. `View::measured` may not. The distinction was
already written down in `Spin`, in a comment, and it is the reason `view()`
returns a struct:

> *What the screen should believe, which is not the same question as what was
> measured this second: a momentary loss of confidence holds the last reading
> rather than blanking it.*

A file that records a held second as measured has invented data. A screen that
blanks on every one-second dip is unreadable during exercise, which is the only
time anybody looks at it.

## What it does not own

- **Which sensor to subscribe to.** `HEART_RATE` is the stable two-field frame;
  `HEART_RATE_EX` is opt-in provenance with a stricter validity rule. The SDK's
  `Docs/ExternalSensors.md` says which is for what and this kit takes no view.
  The seven Services differ in when and how often they connect, in ways a shared
  subscriber would have to model and get wrong.
- **How far to believe a trusted reading.** That is a calibration question and
  `EffortKit::window` owns it, with `min_trust` and `min_trusted_pct` derived
  from the pulled recordings.
- **What zero means.** `EffortKit/src/hr.rs` owns that for the whole repository:
  zero is "no trusted reading" and never a heart rate. This kit returns booleans
  and a struct so it cannot quietly disagree.
- **Per-source readings.** Three consumers want them, in three shapes, and each
  is two lines of assignment from the EX parser.

## The numbers, and which of them are measured

`kArrivalGateMs` is measured: 6,101 inter-arrival gaps across every recording
under `Squash/Tests/pulled`, largest ever observed 2117 ms, none at or above
three seconds. Re-derive with `Tools/hr_analyse.py`.

`kTrustHoldMs` is **asserted**, carried over from `Spin::HrHold` unchanged. The
longest untrusted run ever recorded is four seconds, so ten is 2.5x it, but no
recording has ever been made with the watch deliberately removed mid-session --
which is the one that would settle it.

`kMinBpm`, `kMaxBpm`, `kMinTrust` and `kMaxTrust` are **inherited**. They arrived
with the UNA SDK's activity examples and appear in no documentation and no commit
message here. What is known is what the recordings contain: four rows of exactly
0 bpm and 6,104 between 64 and 160, at trust values only ever in {0,1,2,3}. So
`kMaxBpm` has never been approached and `kMinBpm` has only ever had to separate
zero from a real reading.

## Tests

```sh
UNA_SDK=/path/to/una-sdk cmake -S Tests -B Tests/build && cmake --build Tests/build
./Tests/build/hrkit-tests
```

They build against GoogleTest and nothing else. If that ever stops being true,
something has been added here that does not belong.
