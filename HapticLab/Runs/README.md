# Runs pulled off the watch

One directory per run, exactly as the watch wrote it, copied here so the
statistics can be recomputed and disputed against the rows rather than against
a summary.

| Run | What it is |
| --- | --- |
| `20260909T024347` | Phase 0, the first time the app ran on hardware. Nine of the eleven probes answered; the answers are in `Docs/LEDGER.md` §1, not here, because a wearer's report is not a file the watch can write. `session.json` is 0 bytes, which is ledger row I9. |

The probe answers live in the ledger rather than in this directory on purpose.
`probe.csv` records what was *sent* and its clocks; what a wrist *felt* is the
operator's, and pairing the two is the ledger's job.
