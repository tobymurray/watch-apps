# Prompt: find out why map-pack verification keeps starting over

Map Manager's on-device log shows verification scans beginning many times and finishing
once. If that is what it looks like — the service losing its state and restarting — then a
pack large enough to need a real scan may never finish verifying, and the app's whole
purpose is to finish verifying packs. Find out what is actually happening.

This is a diagnosis task. **Do not fix anything until you can say what the cause is.** A
plausible-looking fix to a misdiagnosed problem is worse than no fix, because it makes the
symptom go away without the cause.

## 0. Ground rules

- **Verify, don't infer.** Two claims in this file's own history were wrong because someone
  read a truncated listing as fact. Check the thing itself.
- Label every conclusion `CONFIRMED` (traced to a log, a measurement, or code you read) or
  `PLAUSIBLE` (reasoned). For each `PLAUSIBLE`, say what evidence would settle it.
- **Say what you could not determine.** An honest "the log cannot distinguish these two
  cases" is a result; it tells the next person to add instrumentation rather than re-read.
- Never state counts of tests or experiments as a measure of effort. Name what you covered.
- Do not post anything to GitHub. Do not open issues or PRs.

## 1. The observation

From `Apps/MapManager/Debug/mapmanager_verify.log` on the watch's USB volume (the copy from
before 2026-08-13 has been rotated to `mapmanager_verify.log.old-binary`; a fresh log is
accumulating from the current build):

- **Scans started: many. Scans completed: one.** The single completion is
  `step() DONE .../athens.rawtiles elapsed=2567800ms declaredCrc=0x2F206C4E
  computedCrc=0x2F206C4E -> Verified, marker written` — 42.8 minutes for a 201 MB pack at
  roughly 77 KB/s. Every other `start() ... priorMarker=Absent -> beginning scan from 0`
  has no matching `DONE`.
- **Every rescan rediscovers every pack as new.** Repeated
  `scanForNewPacks() N new pack(s), N tracked total` where the two numbers are equal and
  equal to the total pack count. If tracking persisted, a later scan should report zero new.
- **The gaps between scan summaries are irregular**: roughly 6.5 h, then 26 min, then
  27 min, then **4.3 s**, then **25 s**.

That last point is the sharp one. `kRescanPeriodMs` is **30000** (see `Service.hpp`), and
`scanForNewPacks()` is throttled by it. **Two full scans 4.3 seconds apart cannot happen
inside one process** unless the throttle is not doing what it appears to do — or unless that
second scan was the *first* scan of a new process, which is what a restart looks like.

## 2. Two things about the evidence that will mislead you

**The log has no run boundaries.** `ManagerLog` is append-only, opens/appends/closes per
call, and appends forever across every boot. Nothing marks where one process ended and the
next began, so a restart is invisible except through its symptoms.

**The absence of "Started" in that file proves nothing.** `Service::run()` logs `Started`
through `LOG_INFO`, which goes to the **kernel logger**, not to `ManagerLog`'s file sink.
The file only ever receives `mLog.logf(...)` calls. Do not conclude the service never
started from its absence there.

**Timestamps come from `mKernel.sys.getTimeMs()`.** Establish what that actually measures —
uptime, wall clock, whether it advances across deep sleep — before reading anything into a
6.5-hour gap. That one fact changes the interpretation of every interval above.

## 3. Hypotheses to discriminate

At least these. They are not mutually exclusive and the log as it stands cannot separate
them, which is the problem.

1. **The service process restarts.** Fresh `mEntries` each time, so everything is
   rediscovered and any in-flight scan is lost. If so: why? A crash, an out-of-memory kill,
   a kernel policy for `Utility`/`APP_AUTOSTART` services, a watchdog, or the kernel
   stopping and relaunching it around some other event.
2. **The process survives but its state does not.** Something clears or reconstructs
   `mEntries`, or the path comparison that dedupes entries fails so every scan re-adds
   them. Read `scanForNewPacks()` against its own dedupe carefully.
3. **The rescan throttle is not throttling.** `mScannedOnce` / `mLastScanAtMs` not behaving
   as intended would produce scans far more often than every 30 s.
4. **Verification is being interrupted by something other than a restart** — the entry being
   re-armed, the cursor rewound, or `driveCurrentEntry()` moving on before completion.

Note the current build changed this area substantially (re-arm on size change, drop on
disappearance, cursor rewind on structural change, a 64 KB slice budget). **The log quoted
above came from the previous build.** Establish which behaviours still occur under the
build now installed before explaining behaviours that may no longer exist.

## 4. Evidence available

- **The rotated log** (`mapmanager_verify.log.old-binary`) and the **fresh log** from the
  current build. Compare: the new build writes `N new, N re-armed, N tracked total`, the old
  wrote `N new pack(s), N tracked total`, so the format tells you which binary produced a
  line.
- **`Crash/` at the root of the watch volume.** Check it. If the service is crashing, this is
  the first place a cause would appear.
- **Other apps' `Debug/` logs**, for whether anything else shows the same pattern at the same
  times — that would point at a device-wide cause rather than one in this app.
- **The kernel log over the debug UART.** There is a documented FTDI FT231X monitor for this
  device (921600 baud, debug jumper, dev-tool power rather than a data cable). `LOG_INFO`
  output — including `Started` — goes there. **This is the most direct way to see restarts**,
  and probably worth setting up before guessing.
- **The source**, on branch `feat/mapmanager`: `Service.cpp`/`Service.hpp`,
  `PackCrcVerifier.*`, `ManagerLog.hpp`.
- **Host tests** under `MapManager/Tests` — they can drive `poll()` directly and already
  cover scan orchestration. If a hypothesis is about logic rather than lifecycle, it should
  be reproducible there, and if it is not reproducible there that itself narrows the cause to
  the environment.

## 5. The decisive experiment, and the confound that blocks it

**Nothing on the device currently needs scanning.** Every pack in
`Apps/SharedData/maps/` has a hand-written `.trust` marker, so the service resolves each one
via its cached marker and skips the scan. You will see nothing until you create work.

To create it: **delete one pack's `.trust` marker** — ideally a large one, since the failure
matters in proportion to scan duration — then let the watch run untouched and watch whether
that scan reaches `DONE` or restarts from zero. Deleting the marker is safe: the marker is
regenerated by design, and the pack is untouched.

Remember the volume must be unmounted and the watch unplugged for the service to run at all;
the watch cannot read its own filesystem while a host holds it as USB mass storage.

Run it both with a large pack and a small one. If small packs complete and large ones never
do, the cause is duration-dependent (restarts on a timer, a watchdog) rather than
event-driven.

## 6. What a good answer looks like

- A statement of what is happening, labelled `CONFIRMED` or `PLAUSIBLE`, with the evidence.
- If it is restarts: what triggers them, and whether they are periodic, load-related, or tied
  to some event.
- Whether the current build still exhibits it, since it changed this code substantially.
- Whether it is specific to this app or device-wide.
- **Whether a persisted resume checkpoint is the right response.** The README lists its
  absence as a known rough edge and argues the throughput fix demoted it, on the reasoning
  that a scan now takes single-digit minutes rather than 42. That reasoning holds only if
  interruptions are rare relative to a scan. If they are frequent, the checkpoint moves from
  "revisit if pack sizes grow" to required — and that is a design decision worth stating
  explicitly rather than reaching by default.

## 7. Recording it

Write findings into `MapManager/README.md` — extend the "Known rough edges" section, or add
a short section if the cause deserves one — and commit to the `feat/mapmanager` branch of
`watch-apps`.

Commit conventions: author and committer `Toby Murray <toby.murray@protonmail.com>` via
`git -c user.email=... -c user.name=... commit`. Terse subject, body mostly *why*. **No AI or
assistant attribution anywhere** — no `Co-Authored-By`, no "generated with". Hard project
rule. Scope every `git add` to `MapManager/` explicitly: the working tree carries a large,
unrelated in-flight effort under `Squash/` that must not be swept in.
