#!/usr/bin/env python3
"""Turn a night of `probe_log.csv` into the numbers the feasibility ledger wants.

Reads the log written by the Tier 0 probe (format spec: the file comment on
`SleepLab/Probe/Software/Libs/Header/ProbeLog.hpp`) and prints one section per
Tier 0 question. Standard library only -- no numpy, no pandas: this has to run
on whatever machine the watch happened to be plugged into.

    python3 probe_report.py /path/to/Apps/SleepProbe/probe_log.csv

By default it reports the newest *launch* in the file, because the log is
appended across every launch and every boot and a whole-file average would
smear a good night into a bad one. `--all-runs` reports each in turn;
`--run N` picks one.

Nothing here interprets sleep. The probe measures delivery and power; whether
those numbers are good enough to build on is a judgement that belongs in
`Docs/FEASIBILITY-LEDGER.md`, written by a person, with a confidence tag.
"""

import argparse
import csv
import sys
from datetime import datetime, timezone

SCHEMA = 1

# Column order of an `M` row, from kHeaderLine in ProbeLog.cpp. Kept as a
# literal list rather than read from the file's own `H` row on purpose: if the
# two ever disagree, that is a bug worth failing on, not one to paper over by
# trusting whichever copy the file happens to carry.
M_COLS = [
    "kind", "uptime_ms", "wall_utc", "local_min", "span_ms",
    "acc_n", "acc_ts_span_ms", "acc_max_gap_ms", "acc_batches",
    "touch_n", "touch_worn_n", "touch_edges",
    "motion_n", "motion_no", "motion_mot", "motion_sig",
    "ar_n", "ar_still", "ar_walk", "ar_run",
    "hr_n", "hr_mean_x10", "hr_min", "hr_max", "hr_trust_x10",
    "hrex_n", "hrex_opt", "hrex_ext", "hrex_unk",
    "beat_n", "ppg_n", "ppg_ts_span_ms",
    "spo2_n", "spo2_last_x10",
    "step_total", "step_delta",
    "batt_pct_x10", "charging", "usb", "batt_mv", "batt_ma_x10",
    "batt_avg_ma_x10", "batt_mah",
    "wakes", "msgs",
]

# A field at this value was never measured, as opposed to measured as zero.
# The distinction is the whole point of the sentinel: a heart-rate sensor that
# delivered nothing all night is a finding, one that was never subscribed is
# not, and an average that silently folds the second into the first is wrong.
ABSENT = -1


class Run:
    """One app launch: its `R` row and every `M` row up to the next `R`."""

    def __init__(self, index, start_uptime, start_wall, hr_mode):
        self.index = index
        self.start_uptime = start_uptime
        self.start_wall = start_wall
        self.hr_mode = hr_mode
        self.rows = []

    @property
    def duration_ms(self):
        """Uptime span the run covers.

        Uptime, never wall clock. The two disagree whenever the clock is
        changed under the watch, and only one of them is a duration.
        """
        if not self.rows:
            return 0
        return self.rows[-1]["uptime_ms"] - self.rows[0]["uptime_ms"] + self.rows[0]["span_ms"]


def parse(path):
    """Split the log into runs. Malformed lines are counted, not fatal."""
    runs = []
    header_seen = False
    skipped = 0

    with open(path, newline="") as fh:
        for parts in csv.reader(fh):
            if not parts:
                continue

            kind = parts[0]

            if kind == "H":
                header_seen = True
                # "H,schema,1,cols,..." -- refuse a schema this script does not
                # know rather than map columns by position and confidently
                # report the wrong sensor's counts.
                if len(parts) < 3 or parts[2] != str(SCHEMA):
                    got = parts[2] if len(parts) >= 3 else "?"
                    sys.exit(f"{path}: log schema {got}, this script reads {SCHEMA}")
                continue

            if kind == "R":
                # "R,<uptime>,<wall>,schema=N,hr=MODE"
                try:
                    uptime = int(parts[1])
                    wall = int(parts[2])
                except (IndexError, ValueError):
                    skipped += 1
                    continue
                hr_mode = "?"
                for f in parts[3:]:
                    if f.startswith("hr="):
                        hr_mode = f[3:]
                runs.append(Run(len(runs), uptime, wall, hr_mode))
                continue

            if kind == "M":
                if len(parts) != len(M_COLS):
                    # A truncated tail is normal: the process can be killed
                    # mid-write by the USB cable.
                    skipped += 1
                    continue
                try:
                    row = {name: int(v) for name, v in zip(M_COLS[1:], parts[1:])}
                except ValueError:
                    skipped += 1
                    continue
                if not runs:
                    # Rows before any R row: a log written by a build older
                    # than the run marker. Keep them under a synthetic run so
                    # the file is still readable.
                    runs.append(Run(0, row["uptime_ms"], row["wall_utc"], "?"))
                runs[-1].rows.append(row)
                continue

            skipped += 1

    if not header_seen:
        print(f"warning: {path} has no H row; assuming schema {SCHEMA}",
              file=sys.stderr)
    return runs, skipped


def present(rows, col):
    """Values of `col` across `rows`, dropping the never-measured sentinel."""
    return [r[col] for r in rows if r[col] != ABSENT]


def fmt_wall(utc):
    if utc <= 0:
        return "unknown"
    return datetime.fromtimestamp(utc, timezone.utc).strftime("%Y-%m-%d %H:%M:%SZ")


def fmt_dur(ms):
    s = ms // 1000
    return f"{s // 3600}h{(s % 3600) // 60:02d}m"


def report(run, verbose=False):
    rows = run.rows
    print(f"\n{'=' * 72}")
    print(f"RUN {run.index}   started {fmt_wall(run.start_wall)}   "
          f"uptime {run.start_uptime / 3600000:.1f}h   hr={run.hr_mode}")
    print(f"{'=' * 72}")

    if not rows:
        print("  no rows -- the launch wrote its R marker and then nothing.")
        print("  That is what a launch killed within its first minute looks like:")
        print("  most likely a USB connection. Check the next R row's uptime.")
        return

    print(f"  {len(rows)} rows over {fmt_dur(run.duration_ms)}")

    # ---- Did the night survive? -------------------------------------------
    #
    # The single most important question, and it is answered by gaps in
    # uptime_ms rather than by any sensor column: a service that stopped being
    # scheduled writes no rows at all, and the hole it leaves is the finding.
    print("\n-- continuity ------------------------------------------------------")
    gaps = []
    for a, b in zip(rows, rows[1:]):
        gap = b["uptime_ms"] - a["uptime_ms"]
        if gap > 90_000:  # 1.5x the nominal row period
            gaps.append((a["uptime_ms"], gap))
    if gaps:
        print(f"  {len(gaps)} gap(s) longer than 90 s:")
        for at, gap in gaps[:10]:
            print(f"    at uptime {at / 3600000:6.2f}h  missing {gap / 1000:.0f}s"
                  f"  ({gap / 60000:.1f} rows)")
        if len(gaps) > 10:
            print(f"    ... and {len(gaps) - 10} more")
    else:
        print("  no gaps: every row landed within 1.5x of its period.")

    spans = [r["span_ms"] for r in rows]
    over = [s for s in spans if s > 66_000]
    print(f"  row span: min {min(spans) / 1000:.1f}s  median "
          f"{sorted(spans)[len(spans) // 2] / 1000:.1f}s  max {max(spans) / 1000:.1f}s"
          f"  ({len(over)} over 66 s)")

    # ---- Delivered rates ---------------------------------------------------
    print("\n-- delivered rates -------------------------------------------------")
    for label, col in (("accel", "acc_n"), ("hr", "hr_n"), ("hr_ex", "hrex_n"),
                       ("touch", "touch_n"), ("motion", "motion_n"),
                       ("activity", "ar_n"), ("ppg", "ppg_n")):
        vals = present(rows, col)
        if not vals:
            print(f"  {label:9s} not subscribed")
            continue
        total = sum(vals)
        # Against uptime, so a row that overran does not inflate its own rate.
        hz = total / (run.duration_ms / 1000.0) if run.duration_ms else 0.0
        zero_rows = sum(1 for v in vals if v == 0)
        print(f"  {label:9s} {total:>9d} samples  {hz:7.2f} Hz mean"
              f"  {zero_rows:>4d}/{len(vals)} rows empty")

    gaps_ms = present(rows, "acc_max_gap_ms")
    if gaps_ms:
        # In ms, not seconds: at 25 Hz a healthy gap is tens of milliseconds,
        # and printing 0.0s for every good night would hide the one bad one.
        print(f"  accel worst inter-sample gap in any one row: {max(gaps_ms)} ms")

    # ---- The two yes/no questions ------------------------------------------
    print("\n-- HEART_BEAT and SPO2 ---------------------------------------------")
    beat = present(rows, "beat_n")
    if not beat:
        print("  HEART_BEAT  not subscribed")
    elif sum(beat) == 0:
        print(f"  HEART_BEAT  0 events in {fmt_dur(run.duration_ms)}"
              f"  -- consistent with PR #167 (no beat events)")
    else:
        print(f"  HEART_BEAT  {sum(beat)} EVENTS -- this contradicts PR #167.")
        print("              Re-run BeatProbe, and reopen the HRV clause in")
        print("              the ledger and in README's 'what it does not do'.")

    spo2 = present(rows, "spo2_n")
    if not spo2:
        print("  SPO2        not subscribed")
    elif sum(spo2) == 0:
        print(f"  SPO2        0 samples in {fmt_dur(run.duration_ms)}"
              f"  -- no firmware producer observed")
    else:
        last = present(rows, "spo2_last_x10")
        print(f"  SPO2        {sum(spo2)} samples, last {last[-1] / 10:.1f}%"
              f" -- a producer exists; SpO2 stops being speculative")

    # ---- Worn detection ----------------------------------------------------
    print("\n-- worn detection --------------------------------------------------")
    tn = present(rows, "touch_n")
    if tn:
        worn = sum(present(rows, "touch_worn_n"))
        total = sum(tn)
        edges = sum(present(rows, "touch_edges"))
        pct = 100.0 * worn / total if total else 0.0
        hours = run.duration_ms / 3600000.0
        print(f"  worn {worn}/{total} samples ({pct:.1f}%)")
        rate = f"  ({edges / hours:.1f}/hour)" if hours else ""
        print(f"  {edges} worn/not-worn transitions{rate}")
        rows_with_edges = sum(1 for r in rows if r["touch_edges"] > 0)
        print(f"  {rows_with_edges}/{len(rows)} rows contained a transition")
    else:
        print("  TOUCH_DETECT not subscribed")

    # ---- Power -------------------------------------------------------------
    print("\n-- power -----------------------------------------------------------")
    pct = present(rows, "batt_pct_x10")
    if len(pct) >= 2:
        drop = (pct[0] - pct[-1]) / 10.0
        hours = run.duration_ms / 3600000.0
        rate = f"  ({drop / hours:.2f} %/h)" if hours else ""
        print(f"  battery {pct[0] / 10:.1f}% -> {pct[-1] / 10:.1f}%"
              f"  = {drop:.1f}% over {hours:.2f}h{rate}")
        if hours:
            print(f"  extrapolated to 8 h: {drop / hours * 8:.1f}%")
    else:
        print("  no battery level readings")

    ma = present(rows, "batt_avg_ma_x10")
    if ma:
        mean = sum(ma) / len(ma) / 10.0
        print(f"  average current: mean {mean:.2f} mA"
              f"  min {min(ma) / 10:.2f}  max {max(ma) / 10:.2f}")
        # Sign is per the firmware contract and unverified, so report the
        # magnitude and say so rather than assert a direction.
        print("  (sign is 'per firmware contract' and unverified -- magnitude only)")

    mah = present(rows, "batt_mah")
    if len(mah) >= 2:
        print(f"  capacity {mah[0]} -> {mah[-1]} mAh  = {mah[0] - mah[-1]} mAh used")

    charging = [r["charging"] for r in rows if r["charging"] == 1]
    if charging:
        print(f"  *** {len(charging)} rows recorded CHARGING. A night on the cable")
        print("      is not a night: plugging in terminates every running app.")

    # ---- Loop cost ---------------------------------------------------------
    print("\n-- loop ------------------------------------------------------------")
    wakes = present(rows, "wakes")
    msgs = present(rows, "msgs")
    if wakes:
        print(f"  {sum(wakes)} wakes, {sum(msgs)} messages"
              f"  ({sum(wakes) / len(wakes):.0f} wakes/row)")
        if sum(wakes) / len(wakes) > 2000:
            print("  *** that is a spinning loop, not a sleeping one. An autostart")
            print("      service that spins costs battery for the device's whole life.")

    # ---- Storage -----------------------------------------------------------
    print("\n-- storage ---------------------------------------------------------")
    print(f"  {len(rows)} rows -> roughly {len(rows) * 150 / 1024.0:.0f} KiB at ~150 B/row")

    if verbose:
        print("\n-- rows ------------------------------------------------------------")
        for r in rows:
            print(f"  {fmt_wall(r['wall_utc'])}  up={r['uptime_ms'] / 3600000:6.2f}h"
                  f"  acc={r['acc_n']:5d} hr={r['hr_n']:4d} worn={r['touch_worn_n']:4d}"
                  f"/{r['touch_n']:<4d} batt={r['batt_pct_x10'] / 10:5.1f}%")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", help="path to probe_log.csv")
    ap.add_argument("--all-runs", action="store_true",
                    help="report every launch in the file, not just the newest")
    ap.add_argument("--run", type=int, default=None,
                    help="report one launch by index")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="also print every row")
    args = ap.parse_args()

    runs, skipped = parse(args.log)
    if not runs:
        sys.exit(f"{args.log}: no runs found")

    print(f"{args.log}: {len(runs)} launch(es), {sum(len(r.rows) for r in runs)} rows"
          + (f", {skipped} unparseable line(s)" if skipped else ""))

    # A file full of short launches is itself the finding, so name it before
    # descending into any one of them: it is what a night spent repeatedly
    # plugged in looks like.
    short = [r for r in runs if r.duration_ms < 600_000]
    if len(runs) > 1 and short:
        print(f"  {len(short)} of them ran under 10 minutes -- each boundary is a")
        print("  restart, and the usual cause is the USB cable.")

    if args.run is not None:
        if not 0 <= args.run < len(runs):
            sys.exit(f"--run {args.run} out of range 0..{len(runs) - 1}")
        report(runs[args.run], args.verbose)
    elif args.all_runs:
        for run in runs:
            report(run, args.verbose)
    else:
        # The longest run, not the last: the last is often a two-minute launch
        # after the cable came out in the morning, and the night is the one
        # before it.
        report(max(runs, key=lambda r: r.duration_ms), args.verbose)


if __name__ == "__main__":
    main()
