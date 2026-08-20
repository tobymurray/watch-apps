#!/usr/bin/env python3
"""Read recorded nights, and turn them into the two things the app still needs.

The Tier 0 probe answers whether an all-night app is *possible*. This answers
whether this one is *right*, and it is the only path from "synthetic-only" to
"diary-validated" in `Docs/FEASIBILITY-LEDGER.md`.

Two jobs, and they are separate on purpose:

    thresholds   Where the movement constants should go, from the distribution
                 of activity counts in nights you actually recorded. Every one
                 of them is currently a guess with a TODO against it.

    diary        The honest accuracy number. Mean signed error and spread on
                 sleep onset and final wake, against times you wrote down by
                 hand. This is what the README's accuracy statement should say
                 once ten nights exist, and it cannot be derived from anything
                 the watch produces on its own.

    python3 night_report.py thresholds --worn nights/worn/ --table nights/table/
    python3 night_report.py diary nights/ --diary diary.csv

Standard library only.

---------------------------------------------------------------------------
The diary file

Two columns of times you wrote down before sleeping and after waking, one row
per night. Nothing derived from the watch, or it is not evidence.

    date,lights_out,woke
    2026-08-19,23:10,06:35
    2026-08-20,22:55,06:50

`date` is the evening you went to bed -- the same convention the night files
use, so they pair by name without anybody having to think about it.

Write them to the nearest five minutes and no finer. You do not know when you
fell asleep, and a diary that claims 23:07 invites a comparison it cannot
support. What the diary is good for is lights-out and final wake, which you do
know, and those are the two numbers compared here.
"""

import argparse
import csv
import glob
import json
import math
import os
import sys
from datetime import datetime, timedelta, timezone

# Column order of a SleepLab epoch CSV, from NightStore.cpp's kEpochHeader.
# A literal rather than a read of the file's own header line: if the two
# disagree that is a bug worth failing on, not one to paper over.
# Schema 3. The delivery and power columns that used to be the Tier 0 probe's
# alone are still here -- see NightStore.cpp, which this list has to track
# exactly, because a reader that maps columns by position across a schema change
# reports one sensor's numbers under another sensor's name.
# count_x/y/z are schema 3: the per-axis integrals `count` is the vector
# magnitude of, for telling an isotropic noise floor from directed movement.
EPOCH_COLS = [
    "uptime_ms", "wall_utc", "span_ms", "count", "peak", "samples",
    "count_x", "count_y", "count_z",
    "motion", "sig_motion", "step_delta",
    "hr_mean_x10", "hr_min_x10", "hr_samples", "hr_source",
    "worn_pct", "worn_edges", "batt_pct_x10", "charging",
    "rmssd_x10", "sdnn_x10", "rr_count",
    "acc_batches", "acc_max_gap_ms", "touch_n", "hr_trust_x10",
    "hrex_opt", "hrex_ext", "hrex_unk",
    "batt_mv", "batt_ma_x10", "batt_avg_ma_x10", "batt_mah",
    "wakes", "msgs",
]

ABSENT = -1

# One recording epoch, in seconds. Engine::kEpochMs.
EPOCH_SEC = 30
# One scoring epoch. Engine::kScoringEpochMs.
SCORING_SEC = 60


class Night:
    """One recorded night: its epoch rows, and its summary if there is one."""

    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.rows = []
        self.summary = None

    @property
    def counts(self):
        return [r["count"] for r in self.rows]

    @property
    def scoring_counts(self):
        """Recording epochs summed into scoring epochs, as the engine does.

        Summed rather than averaged: a count is an integral, so the integral
        over a minute is the sum over its halves. Averaging here would halve
        every number and put the suggested thresholds at half their right
        value -- which would look entirely plausible.
        """
        per = SCORING_SEC // EPOCH_SEC
        return [sum(r["count"] for r in self.rows[i:i + per])
                for i in range(0, len(self.rows) - per + 1, per)]

    @property
    def worn_fraction(self):
        if not self.rows:
            return 0.0
        return sum(1 for r in self.rows if r["worn_pct"] >= 50) / len(self.rows)


def load_night(path):
    """Parse one epoch CSV, and its sibling .json if present."""
    night = Night(path)
    with open(path, newline="") as fh:
        for parts in csv.reader(fh):
            if not parts or parts[0].startswith("#"):
                continue
            if parts[0] == "uptime_ms":       # the column header
                if parts != EPOCH_COLS:
                    sys.exit(f"{path}: unexpected columns.\n"
                             f"  file:   {','.join(parts)}\n"
                             f"  expects {','.join(EPOCH_COLS)}\n"
                             "This script and NightStore.cpp have gone out of step.")
                continue
            if len(parts) != len(EPOCH_COLS):
                continue                       # a row cut short by a kill
            try:
                night.rows.append({k: int(v) for k, v in zip(EPOCH_COLS, parts)})
            except ValueError:
                continue

    sidecar = path[:-4] + ".json"
    if os.path.exists(sidecar):
        try:
            with open(sidecar) as fh:
                night.summary = json.load(fh)
        except (OSError, json.JSONDecodeError):
            pass
    return night


def load_nights(paths):
    """Every `*.csv` under the given files or directories, excluding the index."""
    files = []
    for p in paths:
        if os.path.isdir(p):
            files.extend(sorted(glob.glob(os.path.join(p, "**", "*.csv"),
                                          recursive=True)))
        else:
            files.append(p)
    # `index.csv` is the history and `watching.csv` is what the segmenter was
    # looking at while *no* night was open. Neither is a night, so neither is swept
    # up by a directory scan -- but `watching.csv` shares the night format exactly,
    # so naming it explicitly works and is how you read a noise floor off it:
    #
    #   night_report.py thresholds --worn ./nights --table ./nights/watching.csv
    skip = {"index.csv", "watching.csv"}
    files = [f for f in files if os.path.basename(f) not in skip]

    nights = [load_night(f) for f in files]
    nights = [n for n in nights if n.rows]
    if not nights:
        sys.exit(f"no epoch rows found under {', '.join(paths)}")
    return nights


def percentile(values, pct):
    """Nearest-rank percentile, on a copy."""
    if not values:
        return None
    s = sorted(values)
    k = max(0, min(len(s) - 1, int(math.ceil(pct / 100.0 * len(s))) - 1))
    return s[k]


# -- thresholds ----------------------------------------------------------------

def cmd_thresholds(args):
    worn = load_nights(args.worn)
    print(f"WORN: {len(worn)} night(s), "
          f"{sum(len(n.rows) for n in worn)} recording epochs")
    for n in worn:
        print(f"  {n.name}: {len(n.rows)} epochs, "
              f"{n.worn_fraction * 100:.0f}% reporting worn")

    worn_counts = [c for n in worn for c in n.scoring_counts]

    table = None
    table_counts = []
    if args.table:
        table = load_nights(args.table)
        print(f"\nTABLE: {len(table)} night(s), "
              f"{sum(len(n.rows) for n in table)} recording epochs")
        for n in table:
            print(f"  {n.name}: {len(n.rows)} epochs, "
                  f"{n.worn_fraction * 100:.0f}% reporting worn")
        table_counts = [c for n in table for c in n.scoring_counts]

    def dist(label, counts):
        if not counts:
            return
        print(f"\n{label} counts per 60 s scoring epoch")
        for p in (1, 5, 25, 50, 75, 95, 99):
            print(f"  p{p:<3} {percentile(counts, p):>8}")
        print(f"  max  {max(counts):>8}")

    dist("WORN", worn_counts)
    dist("TABLE", table_counts)

    print("\n" + "=" * 68)
    print("SUGGESTED CONSTANTS")
    print("=" * 68)
    print("Suggestions, not answers. Each is a defensible place to put a line;")
    print("none of them is validated against sleep. Put the value in the code")
    print("AND the reasoning in Docs/FEASIBILITY-LEDGER.md, or the next person")
    print("inherits another unexplained number.\n")

    if table_counts:
        # The floor separating "something alive is here" from "this is
        # furniture". Above the table night's 95th percentile so ordinary
        # sensor noise does not read as life; below the worn night's 5th so a
        # settled sleeper still does. If those two cross, the two
        # distributions overlap and no threshold separates them -- which is a
        # finding, not a failure of this script.
        lo = percentile(table_counts, 95)
        hi = percentile(worn_counts, 5)
        print(f"WornGate::kMicroMovementFloor")
        print(f"  table p95 = {lo}, worn p5 = {hi}")
        if lo < hi:
            print(f"  -> {(lo + hi) // 2}   (midway; the distributions separate)")
        else:
            print(f"  -> NO CLEAN VALUE. The distributions overlap, so no floor")
            print(f"     separates a wrist from a table on this evidence. Record")
            print(f"     more nights, or accept that the plausibility check rests")
            print(f"     on heart rate alone -- and say so in the ledger.")
    else:
        print("WornGate::kMicroMovementFloor")
        print("  needs --table: a night on a nightstand, which is the only way")
        print("  to see what furniture looks like to this pipeline.")

    if worn_counts:
        move = percentile(worn_counts, 75)
        still = percentile(worn_counts, 50)
        # Getting up is not the 95th percentile of a night in bed -- it is well
        # off the top of that distribution. p99 is the closest a night spent
        # asleep can get to it, and even that is a floor rather than an answer.
        active = percentile(worn_counts, 99)

        print(f"\nNightAnalyser::kMovementFloor  (did the sleeper move?)")
        print(f"  -> {move}   (worn p75)")
        print(f"\nSegmenterConfig::stillnessCountMax  (settled enough to open a night)")
        print(f"  -> {still}   (worn p50)")
        print(f"\nSegmenterConfig::activityCountMin  (active enough to close one)")
        print(f"  -> {active}   (worn p99)")

        # These two bracket the segmenter's hysteresis, and if they sit close
        # together the state machine chatters around one boundary -- opening and
        # closing a night on adjacent epochs. The code comment says they must be
        # well apart; this is where that gets checked against real numbers
        # rather than left as an instruction nobody reads.
        if active < still * 3:
            print(f"\n  ** These two are close ({still} and {active}). The segmenter")
            print(f"     will chatter around one boundary and a night may open and")
            print(f"     close repeatedly. A night spent asleep does not contain the")
            print(f"     evidence for 'active enough to be up' -- record a few minutes")
            print(f"     of getting up and walking about, and take activityCountMin")
            print(f"     from that instead of from a percentile of lying down.")

    samples = [r["samples"] for n in worn for r in n.rows]
    if samples:
        med = percentile(samples, 50)
        print(f"\nDelivered accelerometer samples per 30 s epoch: "
              f"p5 {percentile(samples, 5)}, p50 {med}, p95 {percentile(samples, 95)}")
        print(f"  = {med / EPOCH_SEC:.1f} Hz delivered, against 25 Hz requested.")
        print(f"  kMinSamplesPerRecordingEpoch -> {max(1, med // 5)}   (a fifth of median)")
        print(f"  SleepWakeScorer::kMinSamplesPerEpoch -> {max(1, med * 2 // 5)}")
        print("  Both are 'this epoch is too thin to believe' guards, and they")
        print("  must be a fraction of the DELIVERED rate, not the requested one.")


# -- diary ----------------------------------------------------------------------

def parse_diary(path):
    """`date,lights_out,woke` rows, keyed by date."""
    out = {}
    with open(path, newline="") as fh:
        for row in csv.reader(fh):
            if not row or row[0].startswith("#") or row[0].strip() == "date":
                continue
            if len(row) < 3:
                continue
            date, lights, woke = (c.strip() for c in row[:3])
            try:
                t_lights = datetime.strptime(f"{date} {lights}", "%Y-%m-%d %H:%M")
                t_woke = datetime.strptime(f"{date} {woke}", "%Y-%m-%d %H:%M")
            except ValueError:
                print(f"  skipping unparseable diary row: {row}", file=sys.stderr)
                continue

            # `date` is the evening you went to bed, so waking is almost always
            # the following calendar day. Without this every final-wake error
            # comes out about 24 hours wrong -- which is obvious once seen and
            # entirely invisible in a mean, since it is the same offset every
            # night and the spread still looks fine.
            #
            # Compared rather than assumed, because somebody going to bed at
            # 01:00 and waking at 08:00 is on one date and must not be shifted.
            if t_woke <= t_lights:
                t_woke += timedelta(days=1)

            out[date] = (t_lights, t_woke)
    return out


def night_date(night):
    """The evening a night began, as YYYY-MM-DD, from its filename."""
    stem = night.name.split(".")[0]
    if len(stem) >= 8 and stem[:8].isdigit():
        return f"{stem[0:4]}-{stem[4:6]}-{stem[6:8]}"
    return None


def cmd_diary(args):
    nights = load_nights(args.nights)
    diary = parse_diary(args.diary)
    if not diary:
        sys.exit(f"{args.diary}: no usable rows")

    print(f"{len(nights)} recorded night(s), {len(diary)} diary entrie(s)\n")

    onset_err = []
    wake_err = []
    unmatched = []
    ungated = []

    for n in sorted(nights, key=lambda x: x.name):
        date = night_date(n)
        if date is None or date not in diary:
            unmatched.append(n.name)
            continue
        if not n.summary:
            unmatched.append(n.name + " (no .json)")
            continue

        sleep = n.summary.get("sleep", {})
        if not sleep.get("reported"):
            worn = n.summary.get("provenance", {}).get("worn", "?")
            ungated.append(f"{n.name}  {worn}")
            continue

        # Epoch indices are relative to the night's own start. The first row's
        # wall clock is the anchor -- uptime cannot be turned into a time of
        # day, and the whole app is careful never to try.
        start = datetime.fromtimestamp(n.rows[0]["wall_utc"], timezone.utc)
        onset_idx = sleep.get("onset_epoch")
        wake_idx = sleep.get("final_wake_epoch")
        if onset_idx is None or wake_idx is None:
            ungated.append(n.name + "  (no onset)")
            continue

        # Diary times are local and naive; the anchor is UTC. Compare in local
        # wall-clock minutes-of-day rather than as instants, which is what the
        # diary actually recorded.
        d_lights, d_woke = diary[date]
        # Onset is the start of its epoch; final wake is the END of its epoch,
        # hence the +1 -- `final_wake_epoch` is the last epoch scored as sleep,
        # so waking happened at its far edge. Without the +1 every final-wake
        # error here would carry a constant one-minute bias, which is small
        # enough to survive a review and large enough to matter once the mean
        # of ten nights is being quoted as an accuracy figure.
        app_onset = start.timestamp() + onset_idx * SCORING_SEC
        app_wake = start.timestamp() + (wake_idx + 1) * SCORING_SEC

        o = (app_onset - d_lights.timestamp()) / 60.0
        w = (app_wake - d_woke.timestamp()) / 60.0
        onset_err.append(o)
        wake_err.append(w)
        print(f"  {date}  onset {o:+7.1f} min   final wake {w:+7.1f} min")

    def stats(label, errs):
        if not errs:
            print(f"\n{label}: nothing to compare")
            return
        mean = sum(errs) / len(errs)
        sd = math.sqrt(sum((e - mean) ** 2 for e in errs) / len(errs)) \
            if len(errs) > 1 else 0.0
        print(f"\n{label} (n={len(errs)})")
        print(f"  mean signed error {mean:+.1f} min")
        print(f"  spread (sd)        {sd:.1f} min")
        print(f"  worst             {max(errs, key=abs):+.1f} min")

    stats("SLEEP ONSET vs lights-out", onset_err)
    stats("FINAL WAKE vs diary wake", wake_err)

    if ungated:
        print(f"\n{len(ungated)} night(s) reported no sleep numbers:")
        for u in ungated:
            print(f"  {u}")
        print("  Correctly excluded -- a night the gate suppressed has nothing")
        print("  to compare, and folding it in as zero error would flatter this.")

    if unmatched:
        print(f"\n{len(unmatched)} night(s) with no diary entry: "
              f"{', '.join(unmatched[:6])}"
              + (" ..." if len(unmatched) > 6 else ""))

    n = len(onset_err)
    print("\n" + "=" * 68)
    if n < 10:
        print(f"{n} of the 10 nights the ledger asks for.")
        print("Below ten, the mean is one bad night away from being wrong, and")
        print("the metrics stay tagged synthetic-only.")
    else:
        print(f"{n} nights. Enough to state an accuracy number.")
        print("Put the mean signed error and spread above into README's accuracy")
        print("table and into the ledger's validation table, and change those")
        print("metrics from synthetic-only to diary-validated.")
        print("\nSay what it is measured against: a self-reported diary, not")
        print("polysomnography. The diary knows lights-out and final wake. It")
        print("does not know when you fell asleep, so onset error here is error")
        print("against a proxy, and the ledger should keep saying so.")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    t = sub.add_parser("thresholds", help="where the movement constants should go")
    t.add_argument("--worn", nargs="+", required=True,
                   help="nights recorded on a wrist")
    t.add_argument("--table", nargs="+",
                   help="nights recorded on a nightstand")
    t.set_defaults(func=cmd_thresholds)

    d = sub.add_parser("diary", help="accuracy against hand-recorded times")
    d.add_argument("nights", nargs="+", help="recorded nights, or a directory")
    d.add_argument("--diary", required=True, help="date,lights_out,woke CSV")
    d.set_defaults(func=cmd_diary)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
