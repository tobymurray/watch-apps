#!/usr/bin/env python3
"""What would a trend indicator have shown, measured from a ride's own seconds.

Answers the three questions Spin/Docs/HR-TREND-PROMPT.md 3 asks of an interval
session, from the per-second heart rate a Spin `.fit` already records:

  - how far the heart rate actually moves over a candidate horizon, in bpm and
    in zone widths, against the 1 bpm quantisation step it is measured in
  - whether two horizons ever disagree about the direction, which is what
    decides between one ghost mark and a trail of them
  - where the peak of each effort falls relative to the effort's own end, which
    is the lag the wearer is complaining about

It reads a `.fit` through python-fitparse -- which shares no code with the
writer -- or an `_hr.csv` of the shape Squash pulls, so it runs on the
recordings that exist today and on the ride when it lands.

    hr_trend.py RIDE.fit --max-hr 184 --zones 5
    hr_trend.py RIDE.fit --json ride.json      # for a later comparison
"""
import argparse
import csv
import json
import os
import statistics as st
import sys

# Either side of the floor Spin/Docs/HR-TREND-PROMPT.md derives.
HORIZONS = (5, 10, 15, 20, 30, 45)

# Wider than this is a pause, because Service::processTrack() writes a record
# only while the clock runs.
PAUSE_GAP_S = 2

# bpm; below this the reading is the quantisation step rather than a movement.
FLAT_BPM = 2


def load_fit(path):
    """(second, bpm) per record, plus the laps, from a Spin activity file."""
    try:
        from fitparse import FitFile
    except ImportError:
        sys.exit("python-fitparse is not installed: pip install fitparse")

    samples, laps = [], []
    for msg in FitFile(path).get_messages():
        d = {f.name: f.value for f in msg.fields}
        if msg.name == "record" and d.get("timestamp") is not None:
            samples.append((d["timestamp"].timestamp(), d.get("heart_rate"),
                            d.get("hr_source")))
        elif msg.name == "lap" and d.get("timestamp") is not None:
            laps.append((d["start_time"].timestamp() if d.get("start_time") else None,
                         d["timestamp"].timestamp()))
    samples.sort(key=lambda s: s[0])
    return samples, laps


def load_csv(path):
    """The same, from the `_hr.csv` an IMU capture writes; it has no laps."""
    samples = []
    with open(path) as f:
        for r in csv.DictReader(f):
            try:
                bpm = int(r["bpm_x100"]) / 100.0
                trust = int(r["trust"])
                samples.append((int(r["t_ms"]) / 1000.0,
                                bpm if trust > 0 and bpm > 0 else None,
                                int(r["source"])))
            except (ValueError, KeyError):
                continue
    return samples, []


def load(path):
    return load_csv(path) if path.endswith(".csv") else load_fit(path)


def to_series(samples):
    """A dict of whole second -> bpm, and the set of seconds a pause crosses.

    A window is only honest if the wearer was riding throughout it, so every
    second on the far side of a gap is fenced off rather than bridged.
    """
    series, breaks = {}, []
    prev = None
    for t, bpm, _src in samples:
        sec = int(round(t))
        if prev is not None and sec - prev > PAUSE_GAP_S:
            breaks.append((prev, sec))
        prev = sec
        if bpm is not None and bpm > 0:
            series[sec] = bpm
    return series, breaks


def spans_break(a, b, breaks):
    return any(a <= start and end <= b for start, end in breaks)


def deltas_at(series, breaks, horizon):
    """Signed bpm moved over `horizon` seconds, for every second that has one."""
    out = []
    for sec, bpm in series.items():
        past = series.get(sec - horizon)
        if past is None or spans_break(sec - horizon, sec, breaks):
            continue
        out.append((sec, bpm - past))
    return sorted(out)


def pct(vals, q):
    return sorted(vals)[min(int(q * len(vals)), len(vals) - 1)]


def direction(delta):
    """What a mark would show: flat inside the quantisation step, else a side."""
    if abs(delta) < FLAT_BPM:
        return 0
    return 1 if delta > 0 else -1


def horizon_table(series, breaks, zone_width):
    rows = []
    for h in HORIZONS:
        d = deltas_at(series, breaks, h)
        if len(d) < 10:
            continue
        mags = [abs(x) for _, x in d]
        row = {
            "horizon_s": h,
            "n": len(d),
            "p50": st.median(mags),
            "p90": pct(mags, 0.90),
            "max": max(mags),
            "flat_pct": 100.0 * sum(1 for m in mags if m < FLAT_BPM) / len(mags),
        }
        if zone_width:
            row["p50_zw"] = row["p50"] / zone_width
            row["p90_zw"] = row["p90"] / zone_width
        rows.append(row)
    return rows


def agreement(series, breaks):
    """Whether a second mark at another horizon would ever say anything new.

    Two numbers, because "agree" has two readings and they answer different
    design questions. `agree_pct` counts flat as a third state, so it measures
    how redundant a second mark is overall. `conflict_pct` counts only the
    seconds where one horizon is clearly up and the other clearly down -- the
    only case a trail of marks can show that one mark cannot.
    """
    by_h = {h: dict(deltas_at(series, breaks, h)) for h in HORIZONS}
    out = []
    for i, a in enumerate(HORIZONS):
        for b in HORIZONS[i + 1:]:
            both = set(by_h[a]) & set(by_h[b])
            if len(both) < 10:
                continue
            da = {s: direction(by_h[a][s]) for s in both}
            db = {s: direction(by_h[b][s]) for s in both}
            same = sum(1 for s in both if da[s] == db[s])
            moving = [s for s in both if da[s] and db[s]]
            opposed = sum(1 for s in moving if da[s] != db[s])
            out.append({
                "a_s": a, "b_s": b, "n": len(both),
                "agree_pct": 100.0 * same / len(both),
                "both_moving": len(moving),
                "conflict_pct": 100.0 * opposed / len(moving) if moving else None,
            })
    return out


def effort_bounds(series, breaks, laps):
    """What to call an effort: the laps if the ride was lapped, else the blocks
    of riding the pauses cut it into."""
    lapped = [(a, b) for a, b in laps if a is not None]
    if len(lapped) > 1:
        return lapped, "lap"
    secs = sorted(series)
    if not secs:
        return [], "lap"
    bounds, start = [], secs[0]
    for a, b in breaks:
        bounds.append((start, a))
        start = b
    bounds.append((start, secs[-1]))
    return bounds, "riding block"


def efforts(series, breaks, laps):
    """Where each effort's peak heart rate fell relative to the effort's end.

    A peak landing at or after the effort stopped is the body's own lag, which
    no display can take back.
    """
    out = []
    bounds, _kind = effort_bounds(series, breaks, laps)
    for start, end in bounds:
        inside = {s: v for s, v in series.items()
                  if int(start) <= s <= int(end)}
        if len(inside) < 10:
            continue
        peak_s = max(inside, key=lambda s: inside[s])
        out.append({
            "duration_s": int(end - start),
            "hr_start": inside[min(inside)],
            "hr_peak": inside[peak_s],
            "peak_at_s": int(peak_s - start),
            "peak_after_end_s": int(peak_s - end),
            "hr_end": inside[max(inside)],
        })
    return out


def report(name, samples, laps, max_hr, zones):
    series, breaks = to_series(samples)
    if len(series) < 30:
        print(f"\n--- {name}: too few readings ({len(series)}) ---")
        return None

    zone_width = (max_hr - max_hr / 2.0) / zones if max_hr and zones else None
    span = max(series) - min(series)
    lo, hi = min(series.values()), max(series.values())

    print(f"\n=== {name} ===")
    print(f"  {len(series)} seconds with a reading over {span} s elapsed, "
          f"{len(breaks)} pause(s)")
    print(f"  bpm {lo:.0f}-{hi:.0f}" +
          (f"   zone width {zone_width:.1f} bpm "
           f"(max {max_hr}, {zones} zones)" if zone_width else
           "   (no --max-hr: bpm only)"))

    rows = horizon_table(series, breaks, zone_width)
    head = "  window      n   p50 |d|    p90     max   reads flat"
    if zone_width:
        head += "    p50 zw   p90 zw"
    print("\n" + head)
    for r in rows:
        line = (f"  {r['horizon_s']:>4} s  {r['n']:>5}   {r['p50']:>6.1f} "
                f"{r['p90']:>6.1f}  {r['max']:>6.1f}   {r['flat_pct']:>8.0f}%")
        if zone_width:
            line += f"    {r['p50_zw']:>6.2f}   {r['p90_zw']:>6.2f}"
        print(line)

    ag = agreement(series, breaks)
    print(f"\n  what a second mark at another horizon would add "
          f"(flat below {FLAT_BPM} bpm):")
    print("     pair        same state   both moving   opposite sides")
    for a in ag:
        conflict = ("       -" if a["conflict_pct"] is None
                    else f"{a['conflict_pct']:>7.0f}%")
        print(f"    {a['a_s']:>2} s vs {a['b_s']:>2} s   "
              f"{a['agree_pct']:>8.0f}%   {a['both_moving']:>11}   {conflict}")

    ef = efforts(series, breaks, laps)
    _, kind = effort_bounds(series, breaks, laps)
    if ef:
        print(f"\n  each {kind}'s peak, against its own end ({len(ef)} of them):")
        for i, e in enumerate(ef, 1):
            print(f"    {kind} {i:>2}  {e['duration_s']:>4} s   "
                  f"{e['hr_start']:>3.0f} -> peak {e['hr_peak']:>3.0f} bpm at "
                  f"{e['peak_at_s']:>4} s   "
                  f"peak {e['peak_after_end_s']:+d} s from its end")
    else:
        print("\n  nothing long enough to read an effort from")

    return {"name": name, "seconds": len(series), "elapsed_s": span,
            "bpm_min": lo, "bpm_max": hi, "pauses": len(breaks),
            "max_hr": max_hr, "zones": zones, "zone_width_bpm": zone_width,
            "horizons": rows, "agreement": ag, "laps": ef}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help=".fit activity files, or _hr.csv captures")
    ap.add_argument("--max-hr", type=int, default=0,
                    help="the maximum the ride was recorded against, for the zone-width unit")
    ap.add_argument("--zones", type=int, default=5, help="how many zones that ladder had")
    ap.add_argument("--json", metavar="OUT", help="write the same numbers out for a later comparison")
    args = ap.parse_args()

    out = []
    for p in args.paths:
        samples, laps = load(p)
        r = report(os.path.basename(p), samples, laps, args.max_hr, args.zones)
        if r:
            out.append(r)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(out, f, indent=2)
        print(f"\nwritten to {args.json}")


if __name__ == "__main__":
    main()
