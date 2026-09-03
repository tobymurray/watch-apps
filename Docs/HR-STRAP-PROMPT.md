# Prompt: why does the heart-rate strap stop feeding after two minutes?

You are investigating a reproducible fault on the UNA Watch, across three
repositories and a firmware dump. It is **not** an app bug — that has been
excluded by measurement — so the work is kernel-side archaeology: SDK history,
example apps, the research branch, and the firmware image.

Read this whole document first. Every number in it was measured; every
exclusion below has evidence behind it, and re-deriving them is wasted time.

---

## 0. The honesty contract

This investigation has already produced **six** confidently-stated wrong
conclusions. They are listed in §5 so you do not repeat them. The lesson each
time was the same: a hypothesis that fits every observation is not thereby true,
and three clustered data points are not a constant.

So: separate what you measured from what you inferred, say which is which, and
when a hypothesis is excluded say what excluded it. `Squash/Docs/FEASIBILITY-LEDGER.md`
is the register — rows `H11`–`H18` are this fault. Add to it rather than
narrating in prose, and mark a row **REFUTED** rather than deleting it.

---

## 1. The fault

An external BLE heart-rate strap feeds `HEART_RATE_EX` normally, then stops,
permanently, after **100–121 seconds** — every session, four for four.

| Session | Last external reading | Duration |
| --- | --- | --- |
| `20260903-v0.6.0-8min-activity` | 120 457 ms | 8 min |
| `20260903-v0.6.0-10min-strap-drop` | 119 560 ms | 10 min |
| `20260903-v0.6.0-3min-strap-reacquire` | 120 465 ms | 3 min |
| `20260903-v0.6.0-7min-accessory-flap` | **101 373 ms** | 7 min |

All four are committed under `Squash/Tests/pulled/`, each with `squash.log`,
`sessions.csv`, and an `imu_*_hr.csv` carrying every reading with its
`trust`, `source`, `optical_x100` and `external_x100`. **Start there.** They are
the only primary evidence and they are cheap to re-read.

### What the signal does at the drop

`trust` runs `3,3,3,3` and then `1,1,1,1` (or `0,1,1,1`) in **one reading** —
full confidence to nothing, with no decay. `external_x100` goes to 0 and never
returns. `source` goes `2` (EXTERNAL) → `1` (OPTICAL).

That last detail matters: `ced21870`'s commit message states "the kernel arbiter
already reports source=None on a real dropout". It reported **optical**, not
none. So the arbiter did not believe the strap had dropped; it preferred optical.

### What the kernel never does

**No `EVENT_ACCESSORY_STATUS` at all.** The link goes `SEARCHING(2)` →
`CONNECTING(3)` → `CONNECTED(4)` at app launch and then reports nothing for the
rest of the session — including 660 seconds after a drop in one recording.
`Docs/ExternalSensors.md` promises `LOST` when a connected strap drops, plus a
kernel-driven re-acquire. Neither happens.

### What a forced re-acquire does

The app now sends `RequestRelease(HRM)` + `RequestPrepare(HRM)` 30 s after the
drop. The kernel obeys and scans, and then:

```
132s strap_reacquire      135s CONNECTING   150s SEARCHING
293s CONNECTING  293s SEARCHING     299s/300s     306s/307s
312s/313s   352s/353s   360s/361s   391s   413s
```

**Nine transitions to `CONNECTING`, none to `CONNECTED`**, over 330 seconds.
Each attempt fails in well under a second. So the strap is present, advertising,
and answers every connection attempt — and the connection never completes.

### The sharpest clue

**A cold connect succeeds; every reconnect fails.** At app launch the accessory
reached `CONNECTED` in 47 seconds and then fed 100+ readings. Minutes later,
nine attempts never got past `CONNECTING`. Same strap, same radio, same session.

Whatever prevents the reconnect is state that a fresh app launch does not have.

---

## 2. What is excluded, and by what

Do not re-open these without new evidence.

| Hypothesis | Excluded by |
| --- | --- |
| The app is doing something wrong | All five strap-capable SDK examples — Workout, Running, Cycling, Hiking, Treadmill — have a **byte-identical** accessory lifecycle to Squash's. None handles `LOST`, none re-prepares, none has a keep-alive. |
| The strap | The same straps run **hour-long sessions on a Garmin**. And after the drop the strap is still discoverable and answers every connection attempt, so it has not powered down. |
| The BLE link dropping | No `LOST` in 660 s, and `6bc7d8f3` measured the BLE supervision timeout at **~30 s on hardware**. A real drop would have surfaced. |
| Contention with the phone | The phone's Bluetooth was **off** for all four sessions. |
| A second central (a nearby Garmin paired to the same strap) | Garmin powered off / removed — it dropped anyway, at 101 s. |
| A stale link freed by the strap's own supervision timeout | 330 s and nine attempts, far past the 32 s BLE maximum. |
| Pausing the activity | Logged now: the first pause was two minutes *after* the drop. |

---

## 3. The leading hypothesis, and it is checkable

**`RequestSetCapabilities` may be an ABI mismatch between app and firmware.**

`4c42e3e3 feat(accessory): capability-driven pre-warm (WP-S4, SDK side)` says, in
its own commit message:

> Add `accessoryKinds` (bitmask of `SDK::Accessory::Kind`, default 0) to
> `RequestSetCapabilities` so an app can declare which external accessories it
> wants... the kernel now pre-warms from the capability and auto-releases on app
> stop.

**That field does not exist in the tree at that commit, at any later commit, or
at the pinned SDK revision `8cdb7314`.** Check it yourself:

```sh
git show 4c42e3e3:Libs/Header/SDK/Messages/CommandMessages.hpp | grep -c accessoryKinds   # 0
```

Workout still sends explicit `RequestPrepare`/`RequestRelease`, and the only
surviving trace of the feature is a stale comment at
`Examples/Apps/Workout/Software/Libs/Sources/Service.cpp:228` crediting an
"accessoryKinds capability (WP-S4)" that no header defines.

Why this could be the fault: `RequestSetCapabilities` carries
`static_assert(sizeof(RequestSetCapabilities) == 36)`. A field would have made it
40. **If the watch firmware was built against a version that has the field and
apps send the 36-byte struct, the kernel reads whatever follows as
`accessoryKinds`** — plausibly zero, meaning "no accessory wanted". A kernel that
pre-warms on the explicit message but only *holds* the acquisition for a
capability nobody declared would look exactly like this: works for a couple of
minutes, then quietly stops preferring the strap, and refuses to re-establish
because no lease exists.

**This is a hypothesis, not a finding.** It predicts three things, all testable:

1. The firmware's `RequestSetCapabilities` handler reads a 4th field. → §4.
2. There is a timeout constant near the accessory manager in the 100–120 s
   range. → §4.
3. An app that could declare the capability would keep the strap. Untestable
   without the field, but a **watch reboot or full app relaunch should restore
   the strap**, since a cold connect works — that is the cheap on-device test and
   it has not been run.

---

## 4. Where to look

### The firmware dump

Produced by [`FwDump`](../FwDump) — 4 MB of internal flash from `0x08000000`, in
32 chunks of 128 KB with a CRC-32 per chunk and one for the whole. **It is on
another machine**; get it, verify the manifest CRCs before trusting a byte, and
say in your write-up which revision it came from.

There is precedent for exactly this work, with tooling and a method:
`Docs/Investigations/2026-07-29-hardware-config-recovery/` on the SDK's
**`research`** branch, including `SEAM-HUNT-disassembly-prompt.md` and
`NEXT-SESSION-disassembly-prompt.md`. Read those before starting — they establish
how this firmware has been disassembled before and what was learnt.

What to look for:

- The `REQUEST_SET_CAPABILITIES` handler, and how many fields it reads.
- Anything near the accessory/HRM code with a constant of **100 000–121 000**
  (ms), or 100–121 (s), or a tick count that resolves to either.
- The arbiter that chooses optical over external, and what makes external stale.
- Whether `EVENT_ACCESSORY_STATUS` is emitted on a strap that stops notifying,
  or only on a link-layer disconnect. The absence of `LOST` is a documented
  contract being broken and is worth reporting on its own.

### The SDK history

All on `tobymurray/una-sdk`. The accessory work in order:

| Commit | What |
| --- | --- |
| `49c01b2a` | external-sensor messages + HR source parser (WP-S1) — the SDK-side contract |
| `0e29ff50` | Workout opts into external HR (WP-S3) |
| `4c42e3e3` | capability-driven pre-warm — **the message describes a field the tree does not contain** |
| `d261db60`, `621f1cb9` | pre-activity link status and strap name |
| `cacd8d63` | ported to Running |
| `9a43b240` | on-screen GPS/HR status indicator |
| `6bc7d8f3` | in-activity icon follows the live source — **measures BLE supervision at ~30 s**, and exists because the arbiter falls back before a link drops |
| `ced21870` | post-review: source reset, ABI asserts, `getSource` validation — **states the arbiter reports source=None on a real dropout** |
| `ef7a1802`, `f9856860`, `fcf0432b`, `3c0a3ed5` | ported to Cycling/Hiking/Treadmill |
| `a442aede` | redact accessory name from logs, guard the status handler |

`Docs/ExternalSensors.md` is the contract; the pinned revision is
`8cdb73140a3d97455b80c22d32e5012e26f72291`.

### The research branch

`git checkout research` on the SDK. Relevant:

- `Docs/Investigations/2026-06-15-heart-beat-vs-ppg/` — established that
  `HEART_BEAT` emits nothing and the PPG is 20 Hz single-channel. Same
  investigator, same watch, and the method to copy.
- `Docs/Investigations/2026-07-29-hardware-config-recovery/` — the disassembly
  precedent, plus `BLE-COMPANION-protocol-spec.md` and `btsnoop` transcripts.
- `Docs/Investigations/2026-08-04-rr-interval-contract-review/`.

### On-device, if you can get a session

`Squash` already logs everything relevant to `Debug/squash.log`: `accessory
state=N at t=Ns` on every transition, `hr_source A -> B at t=Ns`, `strap_lost`,
`strap_reacquire`, `pause`/`resume`. Kernel logs are only recoverable from
`Crash/dump_*.bin` — see `MapManager/README.md` for how that was done.

---

## 5. Conclusions already reached and abandoned

Stated confidently, acted on, wrong. Kept so they are not re-derived.

| # | Claimed | Killed by |
| --- | --- | --- |
| 1 | The strap was never used — the session reported optical | It reported the *majority* source; the strap fed 118 of 707 readings |
| 2 | `CLAUDE.md`'s 0.50/0.18 bpm steps are the strap, optical is integer | Both sources step by whole bpm. Those two rides remain unexplained |
| 3 | A pause caused the disconnect | Pauses are logged now; the first was two minutes after the drop |
| 4 | An abrupt BLE link drop | No `LOST`, and supervision is ~30 s |
| 5 | A hard timeout at 120 s, "spread under one second" | Three clustered points. The fourth was 101 s |
| 6 | A nearby Garmin holding the strap — it fit every observation | Garmin off; dropped anyway |

---

## 6. What would count as an answer

In descending order of value:

1. **A constant in the firmware** that explains 100–121 s, with the code path
   that uses it. That is a bug report UNAWatch can act on.
2. **The `RequestSetCapabilities` ABI question settled** either way: does the
   firmware read a fourth field?
3. **A workaround that survives a session.** The in-app re-acquire does not
   work; a relaunch or reboot is untested. If a relaunch restores the strap, an
   app can be made to do that, ugly as it is.
4. **A precise upstream report** even with no fix: four reproductions, no `LOST`
   against a documented contract, nine failed reconnects, strap proven good on
   other hardware. Note the precedent — `#220` (RR intervals) has sat open
   awaiting firmware answers, so calibrate expectations and make the report
   stand on its evidence.

And the outcome that is still a result: **that it cannot be fixed from an app.**
If so, say it plainly, and the honest consequence is that a squash session gets
two minutes of strap data per activity — which changes the recording protocol in
`Squash/Docs/RECORDING-PROTOCOL.md`, since group S needs both heart-rate channels
at once and its bouts are three minutes apart.
