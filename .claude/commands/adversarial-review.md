---
argument-hint: [target] [--rounds N] [--tier N] [--fix]
description: Review a change, adversarially refute the findings, and alternate until every finding is either measured on a build or a watch or killed — then propose the smallest fix that completely closes each one. Produces a durable evidence bundle under tmp/adversarial-review/.
allowed-tools: Read, Grep, Glob, Bash, Write, Edit, Agent, SendMessage, EnterWorktree, ExitWorktree
---

# Adversarial Review

Alternate a reviewer and a refuter over one change until the finding set stops moving. Two agents
arguing converges on whichever is more articulate, so this command converges on **evidence tier**,
not on argument. Each round pushes a finding up the ladder below or kills it — a claim that cannot
be promoted is not a finding.

This repo already works this way by hand: it settled an arc-sampling threshold by counting unpainted
pixels and an HR hold window from dropout statistics across two real rides. This command is that
habit applied to a diff, with an adversary attached.

## Arguments

- Target: a branch, a PR number, a path, or nothing (= current uncommitted diff). **The target may
  live in `una-sdk` rather than here** — much of what this repo depends on is SDK code, and a review
  of an SDK branch measures its effect through the apps in this tree. Say which repo the diff is in,
  in the first line of the output.
- `--rounds N` — cap on **finding** rounds (reviewer↔refuter exchanges over claims). Default 3. Fix
  rounds are bounded separately (see the fix-revision bound) and never count against this cap.
- `--tier N` — stop promoting at this tier (default 3). `--tier 1` is a fast code-only pass.
- `--fix` — leave the agreed fix in the working tree. Without it, fixes are reported as patches and
  the tree is left clean.

**Settle `--fix` before round 1.** Reporting patches and then getting "now apply them" forces a full
stop-and-restart. If the flag is absent and the target looks like the caller's own branch, ask once
at the start and never again.

A finding round and a fix round are different work with different yields — merging their budgets is
how a review ships an unattacked fix. Round yield tracks **whether the round is the first look at a
given artifact**, not the round number: the first attack on the diff and the first attack on the fix
both pay; a second attack on the same artifact is where returns collapse. Budget by artifact, not by
count.

## The evidence ladder

| Tier | What it is | What it takes |
|---|---|---|
| **E0** | "I read the code and it looks wrong" | Nothing. Not a finding yet — a hypothesis. |
| **E1** | The code is actually built, linked and reachable | `arm-none-eabi-size`, the linker map, or the packed `.uapp` showing the symbol or section is really there, with the toolchain fingerprint printed in the same invocation |
| **E2** | A host test fails on the branch and passes on the base, or a build measurement differs across the two | Both directions run, same toolchain image, same SDK ref |
| **E3** | The fault is visible on the watch | A device run — a `Debug/*.log` line, an `app_list.json` row, a simulator screenshot pair — one tree, one build, only the diff varying |

**Never report a finding above the tier its evidence actually reached.** An E0 claim written up in E3
language ("this crashes the watch") is the specific failure this command exists to prevent.

E3 is the goal because it's the only tier that isn't hypothetical, and this codebase's own design
records keep saying so — `Docs/TEXT.md` closes with "Still only the watch can settle" and lists what
that leaves open. Two pairings, chosen by what the finding claims:

- **The diff introduces a fault** → the base behaves, the branch doesn't. Same tree, same toolchain,
  swap only the file: `git show <base>:path/to/file.cpp > path/to/file.cpp`, build, install, capture,
  then `git checkout path/to/file.cpp` and repeat.
- **The diff fails to fix what it claims** → the branch before the triggering action vs. after it.

State which pairing a device capture is — they prove different things and are easy to conflate.

**When the fault has no device path, E2 is the ceiling — say so and stop climbing.** A link-graph
claim, a size regression, a wire-format field number: the honest top tier is a build measurement or a
host test that goes both ways. Do not burn a round installing an app to photograph something a
committed test already proves — a host test that runs in CI is *better* evidence than a screenshot.
Report it as E2 with "no device path exists", never as a failed E3.

**An E3 run costs a watch cycle, and the watch is a shared, slow instrument.** Installing takes a
real power cycle and can silently not register (see Environment). Batch every capture a round needs
into one install, and record in the ledger which build is on the watch — a later refuter reading a
`Debug/` log has no way to tell which build wrote it unless the app logs its own `BUILD_VERSION`.

## Environment — non-negotiable

Read `Docs/INSTALLING.md` before putting anything on a watch, and `CLAUDE.md` before writing a line
of fix. The rules that specifically bite *this* command:

- **There is no `cmake` and no `arm-none-eabi-gcc` on the Mac. Every build happens in Docker.** Run
  the image **by ID, not by tag** — a local-only tag makes `docker run` try to pull and fail, and
  `--platform linux/amd64` makes it re-resolve and fail too.

  ```sh
  docker run --rm -v <repo>:/apps -v <sdk>:/sdk -e UNA_SDK=/sdk \
    -w /apps/<App>/Software/Apps/<App>-CMake <image-id> bash -lc \
    "cmake -B build -G 'Unix Makefiles' -DBUILD_VERSION=X.Y.Z . && cmake --build build -j\$(nproc)"
  ```

- **Re-read the pin every run.** `.github/workflows/app-build.yml` holds `TOOLCHAIN` (an image
  digest) and `SDK_REF` (a commit). Both move. A cached image ID from a previous session is the most
  common way this command measures two things that were never comparable.
- **The toolchain image and the SDK ref are this command's instrument fingerprint.** Print them once,
  paste them into the ledger, and every E1+ claim inherits them. A measurement whose fingerprint
  doesn't match the ledger's is not evidence — it is a different instrument. Re-print after any
  change of image, SDK ref, or worktree.
- **`CMAKE_RUNTIME_OUTPUT_DIRECTORY` is hardcoded to `<App>-CMake/build`.** Configuring into `build2`
  still writes its ELFs into `build/`, so a before/after comparison must **move the first ELF aside**,
  not rely on a second build directory. A stale `build/CMakeCache.txt` may have another path baked in
  and will hard-error; `build/` is gitignored regenerable output, so move it aside and let cmake
  configure fresh, which is what CI does anyway.
- **Check the tool is on PATH before believing its silence.** `arm-none-eabi-size` is on PATH in the
  current image; `arm-none-eabi-nm` has not always been. A `grep` over `nm` output that returns
  nothing may mean the tool was not found, not that the symbol is absent — confirm the symbol
  positively first, or run `llvm-nm` on the Mac against the ELF the container left in the tree.
- **A negative result is not evidence until the instrument is proven.** Before writing "the symbol is
  gone" or "the size did not change", show the same command finding something it should find in the
  same invocation.
- **Installing is where this command loses afternoons.** Per `Docs/INSTALLING.md`: check the CRC-32
  footer before the file touches the watch, write the new file first, compare lengths, only then
  delete stale `.uapp`s, eject and reconnect before hashing (hashing straight after writing reads the
  OS write cache and reports a false OK), then a **real power cycle** — a USB replug prunes registry
  rows but adds none, so a copied-in app that was only replugged has vanished from the launcher.
  Finally, confirm the app got a row in `Apps/app_list.json`; a perfect file that never registered
  looks exactly like a broken build.
- **Plugging in USB terminates every running app**, so a watch on charge records nothing. Any device
  capture that depends on the app running must happen off the cable.
- **`diskutil unmount` on the watch takes 159 seconds and then fails**, because Spotlight dissents it.
  `Docs/INSTALLING.md` has the markers that turn indexing off. Budget for this or disable it first.
- **A tree you create is a tree you tear down.** See the Teardown step under Output.

Shell state does not persist between Bash calls, so every snippet resolves paths fresh:
`git rev-parse --path-format=absolute --git-common-dir` gives the main `.git` dir from inside any
worktree, and its parent is the checkout root. Never hardcode a path.

## Choosing the instrument

Pick deliberately and record the choice in the ledger. The wrong instrument produces confident
nonsense in both directions:

| Finding depends on | Instrument |
|---|---|
| Pure logic — a calculation, a parser, a state machine | Host test under the app's `Tests/`, run in the toolchain image. Fastest and the only tier that survives into CI. |
| What gets linked, how big it is, what the packer emits | A build pair, same image and SDK ref, ELF moved aside between the two. `arm-none-eabi-size -A` plus the linker map. |
| Kernel behaviour, sensors, buttons, the panel, timing | The watch. Nothing else settles these, and this repo's docs say so repeatedly. |
| What a screen looks like | The TouchGFX simulator where the app has one, else the watch. The simulator's asset generator **clobbers `generated/images/src/*.cpp`** with the wrong rotation — `git checkout` that directory afterwards and rebuild the `.uapp` before shipping it. |

**A host test cannot reproduce a link-graph or codegen fault, and a build measurement cannot
reproduce a behavioural one.** A claim that "doesn't reproduce" on the wrong instrument is not a
refutation, it is an unusable instrument. Say which one you used.

## Evidence bundle — durable, in the main checkout

Everything lands in `<main-checkout>/tmp/adversarial-review/<slug>/`, where `<slug>` is a short
branch slug and `<main-checkout>` is resolved as above. `mkdir -p` it first.

This is the **main checkout** deliberately: worktrees get torn down and take their `tmp/` with them.
The bundle holds `diff.patch`, `ledger.md`, `instrument.txt` (the toolchain digest and SDK ref every
claim inherits), the `size -A` output for each build, any linker maps that findings rest on, and any
device logs or screenshots.

`tmp/` is gitignored. **The bundle is scratch, not the record.** Where a finding produces a number
this repo will want later, the durable home is the app's `README.md` or a dated directory under
`<App>/Docs/Investigations/` — that is where this codebase keeps measurements, and `CLAUDE.md` asks
for the measurement and what was measured, not a pointer into someone's `tmp/`.

## Wall-clock discipline

Tool execution is not the bottleneck; wall clock goes to serialized agents and model turns, and here
also to Docker builds. Cheap rules:

- **Every agent spawns with `run_in_background: true`.** That flag — not same-message spawning — buys
  the overlap. While an agent runs, do ledger, census and bundle work; never sit idle on a blocking
  spawn.
- **One container per probe cluster.** A `docker run` costs real startup, and a cold cmake configure
  costs more. Write a probe cluster as one `bash -lc` heredoc and run it once rather than paying that
  per question.
- **Long build matrices run in the background.** A delete-a-hunk matrix over a firmware app is a
  sequential loop of full rebuilds that can block the foreground for many minutes. Start it with
  `run_in_background` and write the ledger while it runs.
- **Name captures `<slug>-f<finding-id>-<before|after>-<what>.png`** — finding id, not step number:
  rounds reorder, and a pair must stay identifiable as a pair.
- **Cite every capture and every log as a full absolute path, one per line, in a code block.**

## Flow

### Round 0 — the link census (do this first, it is the cheapest thing in the command)

Before any agent runs, establish who is actually affected. For a change to shared code — the SDK, a
shared crate like `TextKit` or `MapKit`, a header used by several apps — that means: which apps
compile the touched translation unit, and which shipped `.uapp`s carry the symbol at all.

```sh
# which apps name the touched source in their build
grep -rln '<touched-file-or-var>' */Software/Apps/*/CMakeLists.txt */Software/Libs/libs.cmake
# what is actually in the packed apps, on a watch or in Output/
python3 - <<'PY'
import glob, os
for f in sorted(glob.glob("/Volumes/UNA WATCH/Apps/*/*.uapp")):
    d = open(f, "rb").read()
    if d:
        print(os.path.basename(f), len(d), d.count(b"<marker>"))
PY
```

Paste the census into the ledger as a table, and **give it to the round-1 agent in its prompt.** A
finder holding the census self-censors; a finder without it optimises for plausibility and spends a
whole refuter round on apps the change cannot reach. The apps a change *doesn't* touch are the
majority of what a code-only reviewer will hand you.

### Round 1 — find (E0 → E1)

Spawn a reviewer agent against `diff.patch`, with the census. Every finding needs a concrete failure
scenario: specific inputs or state → wrong output, crash, corrupt file, or a wrong number on the
glass. Drop scenario-less claims before they reach the ledger.

Do not add a "batch your tool calls into one message" clause to any agent prompt here. Tested and
rejected: agents given the clause still issue one tool call per message. Shard work across concurrent
agents instead.

Require the agent to open its report with a **mechanical facts** section: the actual source lines,
SDK interface names and field numbers anything rests on. Does this message exist in
`CommandMessages.hpp`? Is this field 19 or 20? What does `Sections.ld` map into `.text`? Copy that
section into the ledger verbatim. Later rounds read it instead of re-deriving it — two agents
independently reading the same source is the single largest avoidable cost in this command.

**E1 is a gate, not a step.** Promote each finding yourself before round 2: one build or one `size`/
`nm`/map query per finding, asking only "is this code actually built and reachable in an app that
ships?", and all of them in one container invocation. A finding whose code is not linked anywhere
does not enter round 2; it enters the ledger as `killed` with the census as its epitaph. Never pay a
refuter to argue about a path that is not compiled.

### Round 2 — refute (fresh agent, every time)

**Shard the live findings across two or three fresh background agents.** They are independent — each
gets its own subset, the diff, the ledger and the same instrument fingerprint — so this is wall-clock
free and buys freshness: an agent attacking three findings is more thorough per finding than one
attacking nine. One shard also takes the "what did the reviewer miss?" question. Below about four
live findings, one agent is fine.

> For each live finding, try to prove it wrong. Read the actual code paths and the actual SDK headers
> — do not reason from the claim's plausibility. Return per finding: `REFUTED` (cite the code that
> makes it impossible), `NARROWED` (real only under stated conditions), or `CONFIRMED` (state what you
> checked and why it held). Default to `REFUTED` when you cannot construct the failure yourself.
> Separately: what did the reviewer miss? For findings already at E3, you may only attack the setup.

**Never `SendMessage` the same refuter twice.** An agent that conceded a point in round N is reluctant
to attack it in round N+1; freshness is the entire reason this beats one agent self-checking.

### Round 3 — defend (E2/E3)

`SendMessage` the **original reviewer** — context intact, so it defends its reasoning instead of
re-deriving it — with the refutations. Per contested finding it must concede, narrow to the surviving
conditions, or promote a tier: write the failing host test (E2), run the build pair (E2), or drive the
device capture (E3). Concessions are permanent.

Close this round with one more refuter — still round 3, not a fourth — pointed **only at the previous
refuter's own additions**, which are the new artifact. Do not re-attack claims round 2 already ruled
on. If round 2 added nothing, skip it and go to the fix.

### Attacking a measurement or a device capture

At E2/E3 the argument is over and only the instrument is in question. The refuter checks, in order:

1. **Different instrument** — same toolchain digest and same `SDK_REF` on both sides? Two builds
   across a pin change measure the pin, not the diff.
2. **Stale build** — was the ELF actually rebuilt, or did `build/` hand back the previous one? Was the
   first ELF moved aside, given the hardcoded output directory?
3. **Wrong artifact** — is the number from the right blob? A `.uapp` holds a Service blob and a GUI
   blob, and `.text` in the packed header includes `.rodata` and the font sections per
   `Sections.ld`. Comparing a GUI `.text` against a Service `.text` proves nothing.
4. **Which build is on the watch?** A `Debug/` log line is evidence only if the app logs its own
   `BUILD_VERSION`, and `app_list.json` is the kernel's output — an app can be present on disk,
   hash-perfect, and unregistered.
5. **The capture doesn't show the claim** — does the visible difference actually demonstrate the
   stated failure, or merely differ?

A pair that survives all five is done. Stop promoting it and stop arguing about it.

**A pair that fails any of the five is not at that tier.** Demote the finding to the tier its
remaining evidence actually reached and move it out of the demonstrated section — attacks 1–4 usually
mean the instrument was wrong and the pair can be re-run; attack 5 usually means the claim was wrong,
and a capture whose difference does not show the stated failure supports nothing. Record the demotion
with the attack that landed.

### Disposition — decide what happens to each surviving finding (before any fix work)

Every finding that survives to E1 or above gets exactly one disposition, with a mechanical test:

| Disposition | Means | The test |
|---|---|---|
| `fix-here` | The diff introduces it, and the complete fix fits this review's remit | The E3 "introduces a fault" pairing landed, or the E2 test/measurement goes one way on the branch and the other on the base |
| `pre-existing` | Real fault, but the diff didn't cause it | Same repro, same instrument, run against the base: the base fails the same way |
| `follow-up` | Real, caused by the diff or not, but the complete fix exceeds this review | Fix scope failed the dominator test at a reachable altitude, or the complete fix needs a refactor, an SDK change, or a firmware behaviour this repo cannot change |

**`pre-existing` is a verdict, not a dismissal.** It means "do not grow this change"; it still owes
the reader a named follow-up and a prior-art result. The one thing it must never become is a silent
drop.

Run the base-side check explicitly rather than inferring it from the ladder. The E2 row and the
"introduces a fault" pairing both *assume* the base is clean, so a pre-existing fault looks like a
broken instrument if you only read the tier: both directions fail, and it is easy to write that up as
"the measurement didn't hold". It held — the answer is `pre-existing`. Record both directions.

**A `fix-here` finding whose fix later fails its scope tests is re-dispositioned to `follow-up`**,
with the containment (if any) shipped and labelled. That is the only permitted disposition change,
and it happens once.

### Prior art — required before naming any follow-up

This repo's backlog is its design records, not a tracker. Most of what a reviewer rediscovers here has
already been measured once and written down, and `Docs/INSTALLING.md` exists as a table of four wrong
explanations precisely so nobody re-derives them. Before naming a follow-up or filing a `pre-existing`
finding, search:

- the app's own `README.md` and `<App>/Docs/`, and the top-level `Docs/` design records
- `<App>/Docs/Investigations/` and, in the SDK research checkout, `RESEARCH-INDEX.md`
- `git log --all --oneline -S'<symbol>'` and `git log --all --grep '<mechanism>'` — a reverted commit
  is the most valuable hit in the set, because it means this was tried

Per finding, record one of:

- `documented in <path>` — do not re-file; cite it, and if the record lacks this review's number, the
  useful output is a one-line addition to that record rather than a new one.
- `related to <path>` — name the record and what this adds.
- `no prior art (searched: <terms>)` — the searched terms are part of the record; without them the
  next session cannot tell a real gap from a lazy search.

**Dedupe within this review's own findings too.** Two findings that share a mechanism are one finding.

**Do not commit new design records as part of this command.** Report the verdict and a ready-to-paste
body and let the caller decide where it lands. A review that ends by committing documents nobody asked
for is a worse outcome than one that hands over the text.

### Fix rounds — propose, attack, revise (outside the `--rounds` cap)

Only for findings dispositioned `fix-here`. A `pre-existing` or `follow-up` finding is not fixed here
by definition, and an E1 finding has no demonstrated fault, so there is nothing to verify a fix
against — proposing one is guessing.

**The fix-revision bound**, separate from the one for findings:

- **Every fix revision gets exactly one attack.** Never two — the second look at the same fix is where
  yield collapses.
- **At most two revisions.** If the third proposal still draws a landing attack, the fix is out of this
  review's depth: ship the narrowest containment, label it as containment, and name the follow-up.
- **A revision that only adds tests, comments or renames does not earn an attack.** Re-run
  delete-a-hunk and move on.
- **A landing attack must change the artifact or be recorded as not landing.** "Valid concern, noted"
  is how a fix round produces no decision.

The reviewer proposes; **two fresh background refuters** attack the scope from opposite sides:

> (1) **Incompleteness** — name a concrete input or code path that still reaches this fault after the
> fix. Grep the callers; do not reason from the diff alone.

> (2) **Excess** — name a hunk whose removal leaves the fault closed, or a blast radius wider than
> necessary (a shared header or SDK source where one app's file would do).

Each must report its own attack or state that it attempted and found nothing. Both are required before
the fix is settled; one silent half is an unattacked fix.

Settle it with evidence, not discussion: apply the fix, re-run the pair (now broken-then-fixed), then
run delete-a-hunk per hunk.

## Fix scope — smallest complete

Two opposite failures. Be vicious about both:

**Too small** — the fix closes the observed path and leaves the others open. Patching the one app that
reproduced it, when the invariant is broken in the shared crate, means the next app re-opens it.

**Too big** — the fix closes the fault and then keeps going: an extraction, a rename, a guard for a
null that cannot occur, an adjacent smell.

The target is the narrowest change closing **every** path to the demonstrated fault, and nothing else.
Both tests are mechanical — run them, don't discuss them:

**Completeness — the dominator test.** Enumerate the paths that reach the fault by grepping callers,
not by reasoning about them. The fix must sit at a point all of them pass through. List the paths in
the ledger; "I checked the callers" without the list isn't a check.

**Minimality — the hunk-deletion test.** Revert each hunk individually and re-run the host test or the
build measurement. If the fault stays closed, that hunk was not the fix — drop it. Per hunk, not once
over the whole diff. Drive it from a background script; every row is a rebuild.

**Every fault-closing hunk must be defended by a test that lives in the repo.** Reverting it has to
fail a *committed* example under `Tests/`, not an ad-hoc probe you deleted. `CLAUDE.md` is explicit
that logic worth testing goes somewhere it can be tested without a kernel — a Rust module in the app's
crate or a header-only file free of SDK types. If reverting a hunk fails nothing in the repo's own
suite, that hunk is untested: a defect in the fix, not a pass.

Where the fault genuinely cannot be reached without a kernel — a link-graph fact, a size, a packer
output — the defence is a **recorded measurement with what would falsify it**, per `CLAUDE.md`'s
carve-out, not a manufactured assertion. Say which of the two a hunk got.

**Delete-a-hunk also validates a new test — run it in that direction too.** A regression test you just
wrote is an unverified artifact: revert the hunk it defends and confirm the test *fails*. A test that
passes either way tests nothing and is worse than none, because it looks like coverage.

### Minimal means narrowest blast radius, not fewest lines

A one-line change to an SDK header is a **bigger** change than twenty lines inside one app. Rank
candidate fixes by what they can break:

| Narrower | Wider |
|---|---|
| One app, one screen | A shared crate every GUI links |
| A new branch at one call site | Changing an SDK interface's contract |
| One app's `Service.cpp` | `Libs/Source/AppSystem/` — every app, both blobs |

**A fix in the SDK is a different repo and a different pull request.** Say so, and measure its effect
through at least two apps in this tree that link the touched code — one is not a sample, and the same
change can save ten kilobytes in one app and nothing in another.

### When the complete fix is bigger than this review

Say so and stop. Two acceptable outputs: **(a)** the complete fix, or **(b)** a narrow, explicitly
labelled containment plus a named follow-up. A symptom patch written up as a resolution is the one
outcome that is never acceptable.

If a clean fix needs a refactor, that's two commits, not one: refactor at identical behaviour, then the
functional change against a clean baseline.

### Scope that hides in this codebase

- **Never bump `appVersion` by hand.** CI bumps the version and cuts the release from the commit type,
  so a hand-edited version is a broken release, and a fix that includes one has grown a second change.
- **Conventional commits, one logical change per commit, staged deliberately.** A `git add <App>` once
  swept an unrelated change into a commit whose message described only the intended fix.
- **Comments are governed.** `CLAUDE.md` sets the rule and `/cleanup-comments` is the same rule applied
  after the fact. A fix that ships a comment this file cannot keep true has grown scope in the one
  place reviewers don't look.
- **The `.uapp` is an output.** `*.uapp` and `*.elf.elf.map` are gitignored; a fix that commits one is
  wrong regardless of its content.
- **The simulator clobbers generated image sources.** If a device capture used the simulator, `git
  checkout` the `generated/images/src/` directory before measuring anything, or the `.uapp` CRC moves
  for reasons unrelated to the fix.
- **Reformatting is not free.** A formatter over a touched file can rewrite lines the fix never needed,
  burying it. Check the diff for hunks you didn't intend.

## The ledger

`<main-checkout>/tmp/adversarial-review/<slug>/ledger.md`, append-only for verdicts:

| id | file:line | claim | failure scenario | tier | instrument (image digest, SDK ref, host/build/device) | evidence | verdicts (round: R/A) | state | disposition | prior art |
|----|-----------|-------|------------------|------|--------------------------------------------------------|----------|----------------------|-------|-------------|-----------|

`state` ∈ `live` / `narrowed` / `killed` / `conceded`. **A killed or conceded finding never returns to
`live`** — to revive it, enter it as a new id with a new failure scenario. That rule is the convergence
guarantee; without it rounds oscillate forever.

`killed` and `conceded` are not the same settlement: `killed` means the code isn't linked or a refuter
cited code making it impossible; `conceded` means the **finder itself** withdrew after reading the
cited code. A concession is the stronger record — it cannot be re-litigated by suggesting the refuter
misread something.

**Re-entry is capped.** A revived claim may be entered once. A third id for the same mechanism is the
cycle the append-only rule exists to prevent, wearing a new number; record it as `killed` citing the
earlier ids.

Once fix rounds run, a second table:

| id | fix location (file:line) | repo | altitude | blast radius | paths dominated | hunks dropped | apps measured | scope verdict |
|----|--------------------------|------|----------|--------------|-----------------|---------------|---------------|---------------|

`scope verdict` ∈ `minimal-complete` / `contained + follow-up` / `unresolved`. An empty "paths
dominated" cell means the dominator test wasn't run, which means the fix is unverified — not that it
passed.

### Stop

**Stop when there is nothing new to attack.** Each round should target an artifact that has not been
attacked yet — the diff, then the claims, then the fix, then a revised fix. When every artifact in play
has had its one look, the review is done regardless of how many rounds that took; when a *new* artifact
appears, it gets a look regardless of how many have run, subject to the fix-revision bound.

A full exchange that changes no verdict, adds no finding, promotes no tier and lands no scope attack is
equilibrium. But do not read a quiet round as convergence if the reason it was quiet is that it
re-attacked something already attacked — that is a wasted round, and the two are easy to confuse in the
write-up.

Report which condition ended it. "Converged", "every artifact had its look", "hit the round cap" and
"hit `--tier`" are different results. The cap in particular means the disagreement is *unresolved* —
name the contested findings as such, and say explicitly if the shipped fix is one that was never
attacked.

## Failure modes to guard against

- **Correlated error.** Two agents you spawned can agree because both misread the same header.
  Agreement is not evidence — it's why the ladder exists. Verify each surviving finding's scenario
  against the code yourself before reporting it.
- **Drift toward agreement.** Later rounds are gentler. A `CONFIRMED` verdict that doesn't say what was
  checked is not a verdict; re-run that finding with a new refuter.
- **False refutation from the wrong instrument.** "Didn't reproduce" on a host test, for a fault that
  only exists in the link graph, refutes nothing. Check the instrument before accepting a negative.
- **A silent tool.** A `grep` over the output of a binary that wasn't on PATH returns the same nothing
  as a real absence. Prove the tool ran.
- **Two builds that were never comparable.** Different toolchain digest, different `SDK_REF`, a stale
  `build/`, or the ELF not moved aside. This is the most likely reason a size number is wrong.
- **Reading `.text` as code.** `Sections.ld` folds `*(.rodata*)` and the font sections into `.text`, so
  a `.text` figure is code *plus* every glyph table and const. A conclusion about code size drawn from
  it is unsupported unless the read separated them.
- **Reading a `.bss` figure as file size.** The framebuffer is a 57,600-byte static in `.bss` and
  contributes nothing to the packed `.uapp`. Confusing the two invents both faults and savings.
- **Believing an install.** A hash-verified `.uapp` with no row in `app_list.json` is not installed, and
  a replug is not a reboot. Confirm registration and the app's own reported version, not the file.
- **Attacking an artifact that has since changed.** Another session — or the author — can amend or
  commit the fix while a refuter reads it. Before spawning any attack on a fix, confirm the in-tree
  diff is byte-identical to the patch you are about to describe, and say so in the prompt.
- **Editing the tree while an agent reads it.** Do not touch the reviewed files while an attack is
  running — do ledger and bundle work instead, or wait.
- **Reporting the bundle instead of the findings.** The calling session cannot read the ledger; a bundle
  path plus a gist is indistinguishable from having found nothing. Section 0 in full, every time.
- **Letting `pre-existing` swallow a finding.** "It's not from this diff" answers whether the change
  grows, not whether the fault is real.
- **Reviewing the SDK as if it were this repo.** An SDK finding is fixed in another repo, on another
  branch, against another review's standards. Say which repo every finding lives in.

## Output

**The bundle is the archive; this output is the deliverable.** The calling session cannot see the ledger
and will not go read it — everything a caller needs to decide what to do next has to be in the response.
Do not summarise the summary. Do not replace any section below with "see the ledger".

Section 0 is the default and is never omitted, including when the answer is "nothing survived".

### 0. Verified findings — the summary table

Every finding at E1 or above, most severe first:

| id | claim (one line) | repo | file:line | tier | disposition | prior art | fix status |
|----|------------------|------|-----------|------|-------------|-----------|------------|

- **One row per finding, no exceptions** — a demonstrated fault omitted because it predates the branch
  is the specific information loss this section exists to prevent.
- **`disposition`** is `fix-here` / `pre-existing` / `follow-up`. Never hedge it; if the base-side check
  wasn't run, the cell reads `undetermined — base check not run`, which is a defect in the review.
- **`fix status`** is `fixed in tree` / `patch proposed` / `containment only` / `not fixed —
  <disposition>`.

Follow the table with one line stating what ended the review, the instrument fingerprint (toolchain
digest and SDK ref) that E1+ claims inherit, and whether `--fix` was in effect. Then:

1. **Demonstrated (E3)** — claim, `file:line`, the capture paths, which pairing it is, and confirmation
   that it withstood **all five** setup attacks. A finding that failed one belongs in section 2 at its
   demoted tier, never here with the failure as a caveat.
2. **Supported (E1–E2)** — claim, tier, the measurement or test both ways, and precisely what would
   raise it to E3 — or that no device path exists.
3. **Fix per `fix-here` finding** — the patch, the paths it dominates, the hunks delete-a-hunk removed,
   its blast radius, which apps it was measured through, and **the committed test that fails without
   each hunk** (or, for a link-graph hunk, the recorded measurement and its falsifier). State how many
   revisions it went through and whether the shipped version is one that was attacked.
4. **Not fixed here** — every `pre-existing` and `follow-up` finding, each with which disposition test
   decided it, the prior-art verdict with the terms searched, and either the existing record or a
   ready-to-paste body. Say plainly that these leave the change with known live faults — a normal and
   correct outcome, and burying it is not.
5. **Killed** — one line each: claim + why it died, and whether `killed` or `conceded`. Keep this; it is
   what stops settled points being re-argued next session.
6. **Rejected alternatives** — for each fix, the other shape you considered and why not. Name the shape
   so it is actionable.
7. **Contested** — only if the cap ended it: what the two sides disagree about, and what evidence would
   settle it.
8. **Bundle path** — the absolute directory, and a note that anything left in a worktree dies with it
   while the bundle does not. Where a number belongs in a design record, say which file.
9. **Teardown** — one line per worktree this review created and what happened to it, after the bundle is
   written. Trees you did not create are not yours to remove — say which you left and why. Also say
   whether the watch was left with a review build installed, and which: leaving a measurement build on
   the wearer's watch is a leak, and the next session has no way to know.
10. **Label the pull request** — only when the target was a PR, and only when the label already exists:

    ```
    gh label list --search adversarial --repo <owner>/<repo>
    gh pr edit <number> --repo <owner>/<repo> --add-label <exact-existing-name>
    gh pr view <number> --repo <owner>/<repo> --json labels --jq '.labels[].name'
    ```

    If the search comes back empty, say so and ask before creating anything rather than inventing a
    label name on a shared repo. Read the label back and report it; an `--add-label` on a name the repo
    does not have fails quietly enough to look like success.

    The label goes on **whether or not anything was found** — a clean review is exactly the result a
    later reader most wants to find, and labelling only on findings turns "was this reviewed?" into
    "did this have bugs?".

    Nothing else about the PR is yours to change. Do not add reviewers, edit the title or body, approve,
    request changes, or post the findings as a comment unless the caller asked — the response is the
    deliverable, and section 0 goes to the caller, not to the PR.
