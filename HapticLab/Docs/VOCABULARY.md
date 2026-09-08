# The haptic vocabulary

The actual product of this work: the set of patterns a wearer can reliably tell
apart on this watch, each entry with its parameterisation, its measured
duration, its worst pairwise confusion and the trials behind it. Then, and only
then, a proposed mapping onto `Nudge`'s semantics.

---

## There is no vocabulary yet

**No session has been run.** Not a probe, not a sweep, not a trial. Every table
below is empty on purpose, and each one names what would fill it.

This file exists now rather than after the first session so that the shape of
the answer is fixed before any data arrives. A vocabulary written to fit a
result is not a vocabulary.

The question it answers is not "how many effects does the watch support", which
is 127 by the wire format's own account and it means nothing. It is: **how many
patterns can a wearer reliably identify, on this wrist, without looking?**

## What has to exist before an entry can

| Prerequisite | Where it comes from | State |
| --- | --- | --- |
| Whether the effect enum's gaps are an intensity axis | Phase 0, probes `V1 CLICK 1-2-3` and its siblings | Not run; ledger row V6 |
| A measured duration and relative energy for every reachable effect | Phase 1, two independent sweeps | Not run; ledger rows S6 and S7 |
| Pairwise confusion with at least twelve trials behind every pair | A wrist session at the default pass count | Not run; ledger row H1 |
| A no-contact control at chance | A `"contact": "table"` run of the same protocol | Not run; ledger row H3 |
| A masked block that does not collapse | A `"masking": "masked"` run | Not run; ledger row H4 |

An entry that is missing any of these is not an entry. In particular a
vocabulary built without the no-contact control is a vocabulary about hearing
until proven otherwise.

## The recommended set

Empty until a report says otherwise. The columns are the ones an entry has to
carry, so nothing can be added without them.

| Pattern | Parameterisation | Measured duration | Worst pair inside the set | Trials behind it |
| --- | --- | --- | --- | --- |
| - | - | - | - | - |

Filled from `hapticlab-analyse`'s output, not from recollection. The
"maximally distinguishable set" section of the report is the source for the
membership and the worst-pair column; `effects.csv` is the source for the
duration; the pairwise table is the source for the trial counts.

## The proposed mapping onto `Nudge`

Empty, and it stays empty until the set above has members. `Nudge` needs
left, right, off-route, back-on-route and arriving, which is five, and whether
five is available is exactly what is unknown.

| `Nudge` meaning | Pattern | Why this one |
| --- | --- | --- |
| left | - | - |
| right | - | - |
| off route | - | - |
| back on route | - | - |
| arriving | - | - |

**If the answer comes out at four, `Nudge` gets designed for four.** The set
does not get stretched to reach five, and a pattern is not promoted into it
because a meaning needs filling. A meaning with no pattern is a meaning `Nudge`
delivers some other way or not at all.

Two things worth deciding here rather than later, once there are numbers:

- **Reaction time matters as much as accuracy for this product.** A pattern
  identified correctly but slowly is a poor navigation cue, and the report
  reports reaction time by pattern for that reason. An entry that is reliably
  identified in 1.5 s is not interchangeable with one identified in 0.6 s.
- **Off-route and back-on-route are the pair a confusion costs most.** If the
  set has a marginal pair, it should not be those two.

## The limitation, stated plainly

Whatever this file eventually says will be **one wrist's answer**. The protocol
exists so a second wrist can be added: the condition, the seed, the candidate
set and its content hash are all in every run's `session.json`, so a second
wrist is a second run directory and the report generator pools them.

Until there is a second wrist, n=1, and the report says so at the top of every
page it prints.
