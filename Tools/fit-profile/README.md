# FIT field numbers

A field number written into a `.fit` file is wire format. Get one wrong and the
value lands in whatever field really has that number, and a decoder reports it
as *that* field — silently, in every file ever written, with nothing looking
wrong from the writing end.

`SDK/Fit/FitProfile.hpp` in the UNA SDK carries only the fields UNA's own apps
write. When an app here needs one that is not in it, the number comes from the
FIT profile itself and is checked against two independent copies before use.

## The profile is not in this repository, on purpose

Garmin's profile ships under the **Flexible and Interoperable Data Transfer
(FIT) Protocol License**, which is not an open-source licence. Three clauses
between them rule out committing it here:

- **§1** grants a licence to use the SDK "for Licensee's **internal business
  purposes**". A public repository is not that.
- **§2(c)** forbids making the Licensed Technology "available to **any third
  party for any reason**".
- **§2(d)** forbids distributing it "so that any part of it becomes subject to
  any license that ... **others have the right to modify it**" — which is
  exactly what this repository's MIT licence grants.
- **§4** additionally designates the SDK as Garmin's Confidential Information.

So [`lookup.py`](lookup.py) fetches the profile on demand, under whoever runs it
accepting Garmin's terms, and caches it only in the system temp directory.

The individual **numbers** below are a different matter: they are facts about a
public wire format that every open-source FIT decoder already carries, and the
SDK's own `FitProfile.hpp` records them the same way.

## Looking one up

```sh
Tools/fit-profile/lookup.py session time_in_hr_zone   # by name
Tools/fit-profile/lookup.py session 57                # what lives at a number
Tools/fit-profile/lookup.py lap                       # the whole message
```

Every row is cross-checked against a second, independently generated copy
(python-fitparse) and says `agrees` or `*** DISAGREES ***`. One source is a
lookup; two that agree is a check.

## What this repository relies on

Checked against Garmin FIT SDK 21.214.0Release and python-fitparse (SDK 20.8),
which agree on all of these — except `metabolic_calories`, which the older
fitparse profile does not carry at all, so it is the one row with a single
source. (`lookup.py` says `(not in cross-check)` rather than `agrees` for it,
and a file Spin writes decodes it as `unknown_196`.)

| Message | Field | Number | Type | Scale | Notes |
|---|---|---:|---|---:|---|
| `session` | `time_in_hr_zone` | **65** | uint32[] | 1000 | ms in the file |
| `lap` | `time_in_hr_zone` | **57** | uint32[] | 1000 | ms in the file |
| `session` | `total_work` | **48** | uint32 | 1 | **joules**, not kJ |
| `session` | `avg_power` | **20** | uint16 | 1 | watts |
| `lap` | `total_work` | 41 | uint32 | 1 | joules; nothing here writes it |
| `lap` | `avg_power` | 19 | uint16 | 1 | watts; nothing here writes it |
| `session` | `metabolic_calories` | 196 | uint16 | 1 | already in `FitProfile.hpp` |
| `session`/`lap` | `total_calories` | 11 | uint16 | 1 | already in `FitProfile.hpp` |

### The trap worth knowing about

**A field number is not shared between `lap` and `session`, and assuming it is
puts your value in a real field that belongs to something else.** Three of the
fields above are caught by this, and in every case the wrong number lands
somewhere plausible rather than somewhere obviously absurd:

| You meant | Right number | Wrong number | What actually lives there |
|---|---:|---:|---|
| `session.time_in_hr_zone` | 65 | 57 | `avg_temperature`, sint8, °C |
| `session.total_work` | 48 | 41 | `avg_stroke_count`, uint32, strokes/lap |
| `session.avg_power` | 20 | 19 | `max_cadence`, uint8, rpm |
| `lap.avg_power` | 19 | 20 | `max_power` — the average reported as the maximum |

The last one is the worst of the four: it is a watts value in a watts field
with a sane magnitude, so no decoder and no human would ever flag it. Spin
measures no temperature, no strokes and no cadence, so **nothing would look
wrong from the writing end** in any of these cases. The damage is entirely in
the reader.

`Spin/Tests/ActivityWriter_test.cpp` asserts the session wrote nothing into 57,
41, 19 or 21, which is the check that catches this whole class of mistake.

### Fields that do not exist

Worth recording so nobody goes looking twice:

- **`lap.resting_calories`** — there is no such field. `lap` has
  `total_calories` (11) and `total_fat_calories` (12) and nothing else in the
  family. Spin carries its lap resting figure as a developer field, which is
  self-describing and cannot collide with a native one.
