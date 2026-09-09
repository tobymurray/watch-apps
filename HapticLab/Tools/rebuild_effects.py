#!/usr/bin/env python3
"""Rebuild a sweep's effect table from its own raw samples.

    python3 HapticLab/Tools/rebuild_effects.py HapticLab/Runs/<run>

The watch writes `effects.csv` itself, and once the Service files samples by
their own timestamp that file is the table. This exists for the runs recorded
before it did: their summary columns are wrong, their raw samples are not, and
the run is worth more than the forty minutes it took.

It is a reconstruction rather than a second method. It applies the arithmetic in
`SweepStats.hpp` to the same samples, split at the same play instants, so a run
it rebuilds and a run the fixed watch measures are comparable. The play instants
come from the damaged `effects.csv`: `play_tick_us` was read straight off
`ISystem::getTimeMs` before the send and was never in doubt, and the sensor's
clock shares its base (Docs/LEDGER.md row S11).

Where the two disagree the watch is right and this file is the suspect, which is
why it writes `effects-rebuilt.csv` beside the original rather than over it.
"""
import csv
import os
import sys

# The same constants as SweepStats.hpp. Duplicated deliberately: this script has
# to be able to disagree with a later version of the header, or comparing an old
# run against a new one would be comparing two different rules.
FACTOR = 4
FLOOR_LSB = 24
RUN_IN = 3
GAP_US = 250 * 1000
OFFSET_PEAK_PERCENT = 10

# The Service's own window, in microseconds either side of the play instant.
SETTLE_US = 600 * 1000
CAPTURE_US = 2500 * 1000

HEADER = ("schema,run_id,effect,name,family,repeat,"
          "play_tick_us,accepted_tick_us,send_ok,result_success,"
          "baseline_delta,had_baseline,threshold,"
          "onset_us,offset_us,duration_us,energy_proxy,peak_delta,samples,"
          "window_samples,overflowed,declared_duration_ms")


def deltas(rows):
    """Sample-to-sample motion, as SweepStats.hpp computes it: the L1 change
    across the three accelerometer axes. The first sample of a window has
    nothing to difference against and contributes nothing."""
    out = []
    prev = None
    for t, ax, ay, az in rows:
        if prev is not None:
            out.append((t, abs(ax - prev[0]) + abs(ay - prev[1]) + abs(az - prev[2])))
        prev = (ax, ay, az)
    return out


def measure(window, play):
    """One repeat, as `SweepWindow::finish` measures it."""
    d = deltas(window)
    quiet = [v for t, v in d if t < play]
    had_baseline = len(quiet) > 0
    baseline = sum(quiet) // len(quiet) if had_baseline else 0
    threshold = max(baseline * FACTOR, FLOOR_LSB)

    after = [(t, v) for t, v in d if t >= play]
    onset = None
    burst = []
    run = []
    for t, v in after:
        if v > threshold:
            run.append((t, v))
            if onset is None and len(run) >= RUN_IN:
                onset = run[0][0]
                burst = list(run)
            elif onset is not None:
                burst.append((t, v))
        else:
            run = []
            if onset is not None and burst and t - burst[-1][0] >= GAP_US:
                break

    # Where the burst ends, against its own peak rather than the wrist's quiet
    # level, so the ring-down is not counted as drive. `SweepStats.hpp` explains
    # what settles the figure.
    if burst:
        cut = max(v for _, v in burst) * OFFSET_PEAK_PERCENT // 100
        while len(burst) > 1 and burst[-1][1] < cut:
            burst.pop()
    offset = burst[-1][0] if burst else None

    return {
        "baseline_delta": baseline,
        "had_baseline": 1 if had_baseline else 0,
        "threshold": threshold,
        "onset_us": onset - play if onset is not None else -1,
        "offset_us": offset - play if offset is not None else -1,
        "duration_us": offset - onset if onset is not None else -1,
        "energy_proxy": sum(v for _, v in burst) // len(burst) if burst else 0,
        "peak_delta": max((v for _, v in burst), default=0),
        "samples": len(burst),
        "window_samples": len(d),
        "overflowed": 0,
    }


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: rebuild_effects.py <run-dir>")
    run = sys.argv[1]
    original = list(csv.DictReader(open(os.path.join(run, "effects.csv"))))
    if not original:
        raise SystemExit("%s/effects.csv is empty" % run)

    out = open(os.path.join(run, "effects-rebuilt.csv"), "w")
    out.write(HEADER + "\n")

    samples = {}
    for row in original:
        effect = int(row["effect"])
        if effect not in samples:
            leaf = os.path.join(run, "imu", "e%03d.csv" % effect)
            samples[effect] = [(int(r["t_us"]), int(r["ax"]), int(r["ay"]), int(r["az"]))
                               for r in csv.DictReader(open(leaf))]

        play = int(row["play_tick_us"])
        window = [s for s in samples[effect] if play - SETTLE_US <= s[0] <= play + CAPTURE_US]
        m = measure(window, play)

        out.write("%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n" % (
            row["schema"], row["run_id"], row["effect"], row["name"], row["family"],
            row["repeat"], row["play_tick_us"], row["accepted_tick_us"], row["send_ok"],
            row["result_success"], m["baseline_delta"], m["had_baseline"], m["threshold"],
            m["onset_us"], m["offset_us"], m["duration_us"], m["energy_proxy"], m["peak_delta"],
            m["samples"], m["window_samples"], m["overflowed"], row["declared_duration_ms"]))
    out.close()
    print("%s/effects-rebuilt.csv" % run)


if __name__ == "__main__":
    main()
