# Claim identifiers

`claim_id` is what `profile_diff.py` keys on. **Renaming one silently breaks
every firmware comparison that spans the rename**, and the break does not look
like a break — it looks like one claim disappearing and another appearing, which
is a legitimate thing for a diff to report. So the scheme is fixed here, once,
and the rules below are not style preferences.

## The scheme

```
<scope>.<layer>.<metric>
```

| Part | Rule |
| --- | --- |
| `<scope>` | The sensor type's value in **lower-case hex with an `0x` prefix and no padding** — `0x10`, `0x43`, `0x150`. Or the literal `platform` for a claim that is not about one sensor. |
| `<layer>` | One of `existence`, `frame`, `liveness`, `timing`, `value`, `control`, `physical`, `consistency`. These are probe layers 1 to 8 in order. |
| `<metric>` | `snake_case`. For a per-field metric, `f<index>_<metric>` — `f0_lsb`, `f2_stuck_max_run`. |

Examples:

```
0x10.existence.default_resolves
0x10.timing.dt_ms
0x10.value.f0_lsb
0x140.liveness.classification
0x120.consistency.vs_metrics_capacity_pct
platform.consistency.uptime_vs_wall_drift_ppm
```

## Why the scope is the value and not the name

A type renamed upstream keeps its value, and the value is what the kernel
dispatches on. Keying on the enumerator name would break every historical
comparison the day UNA tidied a name — and they have already moved one: `ACTIVITY`
used to mean 0xE0 and now aliases 0xE1.

The trade is that a claim id is less readable. `profile_report.py` prints the
enumerator name alongside every row, which is where readability belongs.

`Tests/CatalogueGeneration_test.cpp`'s `ClaimIdsUseTheValueNotTheEnumeratorName`
pins this.

## The rules

1. **Never rename a claim id.** If a metric's *meaning* changes, that is a new
   claim with a new id, and the old one disappears — which is exactly what a diff
   should report, because a reader comparing two profiles across the change needs
   to see that the question changed.

2. **Never reuse one.** A retired id stays retired. Reusing it would make two
   different measurements the same row in every diff for ever.

3. **Append, never insert, in `Metric` and `FieldMetric`.** Those enums are the
   claim store's memory layout: inserting an enumerator renumbers every claim
   index after it. Harmless in RAM, and not harmless if a `state.json` written by
   an older build is ever resumed against a newer one.

4. **Renumbering the catalogue is a `kCatalogueVersion` bump.** Every profile and
   every run manifest carries it. Two profiles from different catalogue versions
   are still diffable claim by claim; a claim missing from one of them means
   *this build could not measure it* rather than *the device changed*, and only
   the version says which. `profile_diff.py` prints a warning when they differ.

5. **A metric's unit lives in the catalogue, not in the measurement.** A number
   whose unit came from the run is a number nobody can check. Changing a unit is
   therefore a new claim id — `dt_ms` never becomes `dt_us`.

## What is enforced, and where

| Rule | Where it is checked |
| --- | --- |
| No two claims share an id | `Tests/CatalogueGeneration_test.cpp`, `NoTwoClaimsShareAnId` — over all ~1 974 of them |
| Every id formats without truncation | same test; `kClaimIdMax` is 64 |
| Every index decomposes back to itself | `EveryClaimIndexDecomposesBackToItself` |
| Every claim names a method that would settle it | `EveryClaimHasAMethodIdNamingTheProbeThatWouldSettleIt` |
| Every metric has a unit or is deliberately dimensionless | `EveryMetricHasAUnitOrIsDeliberatelyDimensionless` |
| Scope is the value, not the name | `ClaimIdsUseTheValueNotTheEnumeratorName` |

None of these catches a *rename*, because nothing can: a rename is
indistinguishable from a retirement plus an addition. That is why it is a written
rule rather than a test, and why this file exists.

## Method identifiers

Every claim also carries a `method_id`, which is how it was measured:

```
P<layer>.<method>
```

`P1.request-default`, `P4.dt-histogram`, `P7.six-face-static`,
`P8.midnight-crossing`. The layer number matches the `<layer>` word in the claim
id. These are grouped in the report's to-do list, because a method run once
answers every claim under it — which is the actual unit of work.

Method ids follow the same never-rename rule, for the weaker reason that the
report groups by them.
