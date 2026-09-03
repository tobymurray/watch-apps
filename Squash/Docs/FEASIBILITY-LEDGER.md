# Squash feasibility ledger

One row per metric. A metric with no row does not ship; a row that does not say
**VALIDATED** is not displayed. The tags follow
[`SleepLab/Docs/FEASIBILITY-LEDGER.md`](../../SleepLab/Docs/FEASIBILITY-LEDGER.md),
which established the convention here.

| Tag | Means |
| --- | --- |
| **VALIDATED** | Measured on N real recordings from this repository, named in the row. Displayable. |
| **SYNTHETIC-ONLY** | The code computes what it claims, proven against generated data. Says nothing about a person or this hardware. Not displayable. |
| **UNVALIDATED** | Nobody has checked. The row says which recording would check it. Not displayable. |
| **REFUTED** | Checked and found false. Kept, because a claim once believed and now known wrong is worth more on the record than deleted. |

Rows are dated. A row with no date has not been checked since it was written.

**As of 2026-09-03 no metric row says VALIDATED, and nothing is on screen.**
There is no labelled court recording. The watch does carry about twelve minutes
of bench and desk recordings from a superseded build; they are excluded rather
than used, because a recording whose provenance is a version nobody can
reconstruct is not evidence. See [`PHASE-A.md`](PHASE-A.md) for the verdict and
[`RECORDING-PROTOCOL.md`](RECORDING-PROTOCOL.md) for what would change it.

---

## 1. Segmentation

| # | Metric | What it actually measures | Tag | What would validate it | Known failure modes | Date |
| --- | --- | --- | --- | --- | --- | --- |
| G1 | Rally count | Times the state machine entered `Rally`, which is a claim about a movement threshold and not about points played. | **UNVALIDATED** | M1–M3, ≥150 rallies against ≥2 opponents, with the rally-vs-rest error rate from A2. | A long rally with a quiet moment splits in two if the hysteresis band is too narrow; two quick rallies merge if the rest dwell is too long. Both look like plausible counts. | 2026-09-03 |
| G2 | Longest rally | Epochs in the longest `Rally` run. | **UNVALIDATED** | M1–M3. Directly checkable against the paper log, which is why it is the cheapest row to promote. | Inherits G1's splitting failure, and is *more* sensitive to it: one bad split removes the record holder. | 2026-09-03 |
| G3 | Total rally time / total rest time | Seconds in each state. | **UNVALIDATED** | M1–M3. | Off-court time contaminating rest, if G5 says the two are not separable. | 2026-09-03 |
| G4 | Work:rest ratio | G3's two figures divided, off-court time excluded from both. | **UNVALIDATED** | M1–M3. | Excluding off court is a choice: a session of threes would otherwise report a ratio dominated by games sat out. Right for a match, arguable for threes. | 2026-09-03 |
| G5 | Off court as a distinct state | Whether a level of the segmenting feature separates being off court from resting between rallies. | **UNVALIDATED** | T1–T2, ≥20 off-court intervals across two sessions with *different* off-court behaviour. | Sitting, standing and stretching off court look nothing alike; a level tuned on one calls the others rest. If T1 and T2 disagree, `OffCourtRule::Indistinguishable` is the answer and this row goes REFUTED rather than validated. | 2026-09-03 |
| G6 | The session was not rally-structured | Reporting that a drill session has no rally count rather than reporting a count. | **UNVALIDATED** | D1–D2 against M1–M3: the rally-vs-drill overlap in A2. | If drills are separable from rallies this row is unnecessary; if they are not, it is the only honest output for a drilling session. | 2026-09-03 |
| G7 | The state machine's own arithmetic | That rally + rest + off-court + dropped equals the epochs classified, and every rally window lies inside the session. | **SYNTHETIC-ONLY** | Nothing further — this is a property of the code, not of a person, and it is pinned by tests. | None. It is the check that catches a whole class of accounting error for free. | 2026-09-03 |

## 2. Heart-rate recovery

| # | Metric | What it actually measures | Tag | What would validate it | Known failure modes | Date |
| --- | --- | --- | --- | --- | --- | --- |
| R1 | Fall over a rest between rallies | bpm at the start of a rest minus bpm N seconds later, where N is A1's to set. | **UNVALIDATED** | S1 for the settling time and the window, then M1–M3 for the rests to measure across. **May become REFUTED**: if A1's time constant is comparable to a 10–20 s rest, this is the filter settling and not the wearer. | Active recovery blunts the fall, so someone who walks it off scores worse than someone who stands still. Depends strongly on the heart rate at cessation, so two windows are not interchangeable. | 2026-09-03 |
| R2 | Fall over a whole game off court | The same, over minutes rather than seconds. | **UNVALIDATED** | S1 and T1–T2. | Same confounders as R1, plus: what the wearer did off court is unobserved and dominates the result. | 2026-09-03 |
| R3 | Within-session recovery trend | Mean fall across the first half of qualifying between-rally windows against the second half. | **UNVALIDATED** | R1 first; then ≥4 qualifying windows in a session, which M1–M3 should provide. | A trend over four windows is a line through noise; `MIN_WINDOWS_FOR_TREND` is the floor, not a sufficiency. Rally intensity drifts across a match independently of recovery. | 2026-09-03 |
| R4 | Discarded-window count, by reason | How many rests failed which criterion. | **SYNTHETIC-ONLY** | Nothing further for the counting itself; the criteria it counts against are R1's. | None. It is the row that lets a session with no recovery figure say why, which is the point. | 2026-09-03 |
| R5 | Recovery from wrist optical | Whether R1 and R2 mean anything without a strap. | **UNVALIDATED** | S1's two simultaneous channels, then S2. | Grip tension, impact shock and the watch moving on the wrist all corrupt optical during play, and a smoothed corrupted signal still looks smooth. The outcome is `SourcePolicy`, and `ExternalOnly` is a legitimate answer. | 2026-09-03 |

## 3. Baselines and the profile

| # | Metric | What it actually measures | Tag | What would validate it | Known failure modes | Date |
| --- | --- | --- | --- | --- | --- | --- |
| B1 | Heart-rate baselines (mean, max) | Median and MAD of the wearer's own admitted sessions. Needs no segmentation, so it is the one family of rows a recording-free build can honestly fill. | **UNVALIDATED** | Any five sessions meeting the admission gate — ten minutes active, trusted HR across 80%. Five of the protocol's recordings carry a strap for exactly this. | Comparable only against this wearer. A change of strap, or a season, moves it legitimately and the baseline follows by design. | 2026-09-03 |
| B2 | Rally-derived baselines | The same, over G1–G4. | **UNVALIDATED** | B1's five sessions *and* segmentation validated, since an unsegmented session votes on nothing here. | `RallyCount` is a total, not a rate: it compares across sessions only because a wearer's own typical session length is itself stable. It is the row to distrust first when session shapes change. | 2026-09-03 |
| B3 | The warm-up statement | Saying "no comparison yet, N of 5 sessions" instead of a percentile. | **SYNTHETIC-ONLY** | Nothing further; it is behaviour, and it is pinned by tests. | None. Showing a percentile from three sessions is the failure this row exists to prevent. | 2026-09-03 |
| B4 | Robustness to one wild session | That one two-hour session of threes, one knock-up, or one session where the strap fell off does not redefine normal. | **SYNTHETIC-ONLY** | Real session-to-session variance from the protocol's recordings, which would say whether the 10% per-session bound is loose or tight. | The bound is a policy choice with an arithmetic rationale, not a measurement: 0.9⁷ = 0.48, so a halving takes seven sessions. If real variance is much larger the bound will feel sluggish; if much smaller it never binds. | 2026-09-03 |
| B5 | The profile survives a bad file | That absent, truncated, garbage or future-schema files yield an empty profile and never stop the app starting. | **SYNTHETIC-ONLY** | A real file written on the watch and read back off it, which is the only thing that tests FatFs rather than a memory buffer. | A schema this build does not know is refused whole rather than partly read, so a forward-compatible writer costs the wearer their history. That is deliberate: a wrong baseline is worse than a warm-up. | 2026-09-03 |

## 4. Never shipped

Not a tag — these are ruled out on their own terms, and the row exists so nobody
proposes them again without reading why.

| Thing | Why not |
| --- | --- |
| Distance or court coverage in metres | Wrist pedestrian dead reckoning on a 6.4 m court is not credible, and one fabricated number discredits the honest ones beside it. |
| Who won a point | A wrist IMU cannot know. |
| A percentile or ranking from fewer than five sessions | `baseline::MIN_SESSIONS_FOR_COMPARISON`. Below it the MAD is degenerate and a single outlier moves the median. |
| A recovery figure from a window that did not qualify | It is not a small number; it is not a measurement. |
| Any comparison against other people | Every baseline here is the wearer's own. There are no population norms in this crate and no way to add one without changing what it claims. |
| HRV / rMSSD | `RR_INTERVAL` has no firmware producer; the SDK-side contract sits on an unlanded PR. Not designed around. |

## 5. Platform

| # | Claim | Tag | Method / what would change it | Date |
| --- | --- | --- | --- | --- |
| P1 | The five host suites build and pass against an SDK carrying both `SDK::AppConfig` and `ImuFusionSource`. | **VALIDATED** | `Tools/docker-build.sh tests`: 5/5, including `squash-filesink-tests`, which self-skipped before. The SDK is `8cdb7314` (the revision `app-build.yml` pins) with `5df2033e` cherry-picked. | 2026-09-03 |
| P2 | The TouchGFX simulator builds. | **VALIDATED** | `Tools/docker-build.sh sim`, 9 245 896 bytes. It failed before on `SDK/AppConfig/AppConfig.hpp: No such file or directory` — nothing to do with the IMU source, which is what the README said. | 2026-09-03 |
| P3 | The `.uapp` packs with the heart-rate sidecar, the Rust engine and the file log linked in. | **VALIDATED** | `Tools/docker-build.sh app`: 439 944 bytes, ID `9E526672EAFB61B6`, CRC `0x356DD339`. The engine and the log together cost 35 560 bytes against the 404 384 the app packed to without them. | 2026-09-03 |
| P4 | `EffortKit` and `squash_engine` are genuinely `no_std` and allocation-free. | **VALIDATED** | `cargo build --release --target thumbv8m.main-none-eabihf` for both. It is the check that catches anything testable creeping in behind `feature = "std"`. | 2026-09-03 |
| P4a | Rust links into a **Service** ELF, which nothing in this repository had done before. | **VALIDATED** | `una_app_build_service()` takes no library argument, but the target it creates is ordinary, so `target_link_libraries` after the call is the whole of it. No SDK change. The GUI-side apps had already established the toolchain. | 2026-09-03 |
| P4b | The simulator links the same engine, built for its own host target. | **VALIDATED** | Its image has no cargo, so `Tools/docker-build.sh sim` builds the archive in the one that does; both are `linux/amd64`. The pre-built `core` for a host target is compiled to unwind, so it references `rust_eh_personality` even though every profile here aborts — the symbol is defined and never called. | 2026-09-03 |
| P4c | The simulator runs headless with the engine in it. | **VALIDATED** | `SDL_VIDEODRIVER=dummy ./build/bin/simulator.out` reaches `Profile: load 1, 0 sessions` — `Load::ABSENT` on a first run — with the ABI fingerprint check silent, which is how it reports agreement. | 2026-09-03 |
| P4d | A struct that drifts between the two languages fails loudly. | **VALIDATED** | `squash_engine_abi_fingerprint()` hashes the sizes and every offset the C++ side reads; both sides carry `3384192379` and the Service checks it at start-up. The one drift it cannot catch is the two `Metric` lists being reordered independently, which is why they are written in the same order and the comment says so. | 2026-09-03 |
| P6a | Starting an activity hardfaulted the Service: a session object was built on the stack. | **CONFIRMED, and fixed** | `CRASH: HARDFAULT PC=0x20167D40 LR=0x20162495 CFSR=0x00100000 task=Squash.SRV` in `Crash/dump_403795_0001_20260903T101640_1.4.0.bin`. `CFSR` bit 20 is `STKOF`, the Cortex-M33's stack-overflow UsageFault. The three log lines before it are the recording sinks opening, and the next statement in `startTrack()` was `squash_engine_begin()`, which built a 12 656-byte `Session` as a value to move into a `static`; the Service's stack is 10 240. Now `const`-initialised in place and reset field-wise, with `finish_in_place()` so the 7 KB segmentation is borrowed rather than moved. | 2026-09-03 |
| P6b | Host tests structurally cannot see that class of bug. | **CONFIRMED** | 69 tests passed against it. A host thread's stack is 8 MB. `a_session_starts_within_the_services_stack` runs the whole ABI path on a thread sized to the Service's 10 KiB, and was checked by reinstating the by-value construction: "thread has overflowed its stack, fatal runtime error". | 2026-09-03 |
| P7 | The kernel owns `Apps/app_list.json` and rewrites it at boot; editing it does nothing. | **CONFIRMED** | Edited the Squash entry to point at a locally copied `0.6.99`, launched a 5-second activity, and read it back: reverted to `0.2.1`, and the activity ran the old build — `activity_20260903T092731.fit` exists and no `Debug/squash.log` does, which only a build carrying `SquashLog` writes. Kira's README says the same from the other side: "the launcher list is rebuilt only at boot". The edit was unnecessary as well as futile — see P8 for what actually goes wrong. | 2026-09-03 |
| P8 | A stale `.uapp` left beside the new one keeps the old build booting. | **CONFIRMED** | `Update-Watch-Apps.ps1` in the SDK, and Kira's README restating it: "The watch loads whichever it finds first, so leaving two can keep booting the old build." That is why `Squash_0.3.0.uapp` and `Squash_0.4.0.uapp` sat inert beside `0.2.1` from 2026-09-01, and why the hand-copied `0.6.99` did too. It was written here once as "the whole explanation", which overstated it — P7a is the other half. | 2026-09-03 |
| P7a | `Apps/app_list.json` is the kernel's **output**, rebuilt at boot, and no installer writes it. | **CONFIRMED** | `grep` for `app_list` across the whole SDK and the whole of Kira finds nothing: neither `Update-Watch-Apps.ps1` nor Kira touches that file, and both end by telling the operator to reboot so the launcher list is rebuilt. Watched directly as well — deleting `Squash_0.6.0.uapp` left the registry with 26 entries and no Squash at all, pruning the row whose file had gone without adding a row for the `0.6.1` sitting beside it. So a copy is a complete install and the registration is the reboot's job; editing the file is not a shortcut, it is a no-op that reads as an install failure. | 2026-09-03 |
| P8a | The install ordering that works, from `Utilities/Scripts/Update-Watch-Apps.ps1`. | **CONFIRMED as the SDK's own procedure** | Check the `.uapp` CRC-32 footer first — `crc32(file[:-4])` against the last four bytes little-endian — because a file failing CRC is dropped *silently* by the kernel and the app simply never appears. Then write the new file, read back and compare length, and **only then** delete the stale `.uapp`s, so a bad copy never leaves the folder without a working binary. Then eject, reconnect and re-verify by hash, because hashing straight after writing reads the OS write cache and can report a false OK. Then reboot. | 2026-09-03 |
| P9 | The stale-file deletion after the `0.6.0` install was step 4 of P8a. | **LIKELY** | `Apps/Squash/` held exactly one `.uapp` afterwards, which is what that ordering produces by design. Written up here twice before it was right: first as the phone's doing, which was wrong because no phone install took place, then as unexplained. The remaining uncertainty is only whether the installing host ran the script or did the same steps by hand, and nothing downstream depends on which. | 2026-09-03 |
| P10 | An APP_ID the kernel has never registered can be installed. | **CONFIRMED** | `0.6.0` carries `9E526672EAFB61B6`, which appeared in no file on the volume beforehand, and it is registered and running. So the APP_ID change in `4f0993a` was never the obstacle; only the method of installing was. | 2026-09-03 |
| P5 | A recording stops at 30 minutes or 8 MB, whichever trips first. | **UNVALIDATED on hardware** | Enforced and tested against a memory sink; not confirmed against the activity partition's real free space, because the SDK file-system interface exposes no free-space query. The caps are self-defence, not device awareness. A `TODO` in `ImuCsvRecorder.hpp` owns this. | 2026-09-03 |
| P4e | Everything worth knowing about a session is on the volume, not down a UART. | **VALIDATED** | `Debug/squash.log` and `Debug/sessions.csv`, written by `SquashLog`. A simulator run produces `launch v1.2.3 abi=3384192379 expect=3384192379 ok=1 calibration=0` and `profile load=1 sessions=0` — the ABI check, the calibration state and the profile load, none of which `LOG_INFO` shows without a dev tool attached. 8 host tests cover the append (`open(write, override=false)` starts at offset 0, so the seek is what makes it an append), the rotation at 64 KiB, and the CSV's column order. | 2026-09-03 |
| P5a | The heart-rate sidecar exists at all, so A1 is measurable. | **VALIDATED** | Before this branch there was no heart rate on the recording's clock; the `.fit` has a series on its own timebase, which cannot be lined up with a labelled transition without correlating two clocks. `HrCsvLog` writes from the same tick, in hundredths of a bpm so the 0.50 and 0.18 bpm steps `CLAUDE.md` records survive. 12 host tests. | 2026-09-03 |
| P6 | The three sidecars share the sample clock exactly. | **SYNTHETIC-ONLY** | All three are begun from the same sensor tick and stamped by unsigned subtraction from it, pinned by tests including across the 32-bit wrap. Not yet confirmed on a recording taken on a watch, which is what P5's session would also settle. | 2026-09-03 |
