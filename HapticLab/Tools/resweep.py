#!/usr/bin/env python3
"""Rebuild the effect table from a sweep's raw samples, ignoring its phase labels.

    python3 HapticLab/Tools/resweep.py HapticLab/Runs/<run>/imu

This exists because of one damaged run and should not be needed again. In run
`20260909T031146` the Service flipped to its capture phase only after the
blocking vibro send, so an intermittently delivered batch put the start of the
vibration into the window the baseline was computed from, and every fourth or
fifth effect lost a phase boundary entirely: 61 of 127 files came out with
something other than two clean settle-capture pairs. Ledger row S8 has the
mechanism.

The samples themselves are fine and in time order, which is the whole reason
the raw rows are kept rather than only the summary. So this reads each file as
one continuous stream, finds the loud stretches without believing any label,
and reports a duration and a relative energy per burst.

It is a recovery tool, not a second implementation of the sweep: once the
Service is fixed, `effects.csv` is the table and this script has nothing to add.
"""
import csv
import glob
import os
import sys

# Sample-to-sample motion as the sum of the three axes' absolute changes, which
# is what SweepStats.hpp computes on the watch.
def deltas(rows):
    out = []
    prev = None
    for r in rows:
        v = (int(r["ax"]), int(r["ay"]), int(r["az"]))
        if prev is not None:
            out.append((int(r["t_us"]), sum(abs(a - b) for a, b in zip(v, prev))))
        prev = v
    return out


# The quiet level, taken as a low percentile of the whole file rather than from
# a window that might contain the vibration. Two repeats of roughly 600 ms quiet
# and 2500 ms capture means well over half of any file is quiet even when a
# phase boundary was lost.
QUIET_PERCENTILE = 0.25

# Same shape as the on-watch threshold: a multiple of the quiet level, with a
# floor so an implausibly still wrist cannot make every sample an onset.
FACTOR = 4
FLOOR_LSB = 24

# Consecutive samples over the threshold before a burst is believed, and the
# gap that ends one. A single stray sample yields two large deltas, so three is
# the minimum that rejects a knock.
RUN_IN = 3
GAP_MS = 250

# A burst counts as the effect only if it carries a real share of the strongest
# burst's energy. The two repeats of one effect should be comparable, so a
# stretch at a fraction of the strongest is wrist motion: effect 16's third
# stretch came out at a mean delta of 238 against 3338 and 3467 for the two
# real ones.
ENERGY_SHARE = 0.25


def bursts(d):
    if not d:
        return [], 0
    ordered = sorted(v for _, v in d)
    quiet = ordered[int(len(ordered) * QUIET_PERCENTILE)]
    threshold = max(quiet * FACTOR, FLOOR_LSB)

    loud = [(t, v) for t, v in d if v > threshold]
    if not loud:
        return [], threshold

    groups = []
    for t, v in loud:
        if groups and t - groups[-1][-1][0] <= GAP_MS * 1000:
            groups[-1].append((t, v))
        else:
            groups.append([(t, v)])

    out = []
    for g in groups:
        if len(g) < RUN_IN:
            continue
        out.append({
            "start_us": g[0][0],
            "duration_us": g[-1][0] - g[0][0],
            "samples": len(g),
            "mean_delta": sum(v for _, v in g) // len(g),
            "peak_delta": max(v for _, v in g),
        })

    if out:
        strongest = max(b["mean_delta"] for b in out)
        out = [b for b in out if b["mean_delta"] >= strongest * ENERGY_SHARE]
    return out, threshold


def main():
    imu = sys.argv[1] if len(sys.argv) > 1 else "."
    files = sorted(glob.glob(os.path.join(imu, "e*.csv")))
    if not files:
        raise SystemExit("no e*.csv under %s" % imu)

    print("effect,burst,duration_us,mean_delta,peak_delta,samples,threshold")
    for leaf in files:
        effect = int(os.path.basename(leaf)[1:4])
        rows = list(csv.DictReader(open(leaf)))
        found, threshold = bursts(deltas(rows))
        if not found:
            print("%d,-,-,-,-,%d,%d" % (effect, len(rows), threshold))
            continue
        for i, b in enumerate(found):
            print("%d,%d,%d,%d,%d,%d,%d" % (effect, i, b["duration_us"], b["mean_delta"],
                                            b["peak_delta"], b["samples"], threshold))


if __name__ == "__main__":
    main()
