# Shared health data

Health data belongs to the wearer, not to the app that measured it. One app records
sleep, another records workouts, and a third should be able to put them together, for
example to say that hard training days are followed by shorter nights. This document is
the convention the apps in this repository follow, so that such a reader can be written
without knowing any of their code.

It builds on una-sdk's `Docs/shared-data.md` (branch `docs/shared-data-directory`), which
owns the platform rules: the `../SharedData/` whitelist, ten open files watch-wide,
`rename()` never replacing a file, and `open()` returning the same `false` for "absent"
and "busy".

## Where

```
../SharedData/health/<domain>/
```

One directory per kind of data, named for the data, not the app: `health/sleep/`, not
`health/slumber/`. A reader asks for sleep and does not care which app measured it.

Every record carries `"source"`, the writing app's name. Two apps may write the same
domain; a reader picks one source or reconciles them, and a writer only ever reads back
its own lines.

## Two shapes of file

**History: `sessions.jsonl`.** One JSON object per line, one line per finished session,
appended and never rewritten.

- A line is complete only once it ends in `\n`. A write cut short leaves a last line
  without one; readers skip it, and the next writer closes it off before appending.
- A session's identity is `source` plus `start`.
- Lines are not guaranteed to be in time order across sources.
- An app syncs incrementally by remembering the largest `end` it has read for a source.

**State: `now.json`.** One JSON object, rewritten whole on a schedule.

- It is written the way the SDK's `RecordingMarker` writes:
  1. write `now.json.tmp`;
  2. remove `now.json.bak`;
  3. rename `now.json` to `now.json.bak`;
  4. rename the tmp to `now.json`.
- If `now.json` will not parse, read `now.json.bak`.
- `written` and `heartbeat_s` say how fresh it is. Past `written + 2 * heartbeat_s`, the
  writer has stopped, and the state is history.

## Fields every record has

| Field | Meaning |
| --- | --- |
| `schema` | The record's version in its domain. A reader refuses a schema it does not know. |
| `source` | The writing app. |
| `start`, `end` | UTC seconds, in session records. |
| `utc_off_min` | Local minus UTC when written, so a reader can place a session on the wearer's clock without knowing the time zone. |

Other conventions:

- Times are UTC seconds.
- Durations end in `_min` or `_s`.
- Percentages end in `_pct`.
- `null` means not known yet. A reader never sees an absent field with a meaning.

## Reading on the watch

The SDK's `JsonStreamReader` (`SDK/JSON/JsonStreamReader.hpp`) reads these files with
dotted queries, such as `last_night.start`. `Slumber/Tests/SharedSleep_test.cpp` reads
Slumber's files through it.

A sessions line is up to a few kilobytes, so read it a line at a time into a buffer.

Open, read and close at once: a reader holding a file open makes the writer's next append
fail.

## Domains

| Domain | Written by | Documented in |
| --- | --- | --- |
| `health/sleep/` | Slumber | `Slumber/README.md`, "Sharing sleep with other apps" |

A workout or intensity domain would follow the same shape: `health/activity/sessions.jsonl`
with `start`, `end`, `utc_off_min`, and its effort in named units.
