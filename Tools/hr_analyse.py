#!/usr/bin/env python3
"""Measure the HEART_RATE_EX stream from the pulled squash recordings.

Answers, from this repository's own data, the four things the recovery gates
currently assume from other people's protocols:
  - the sample interval, and whether a 1 Hz consumer ever sees a gap
  - how often the kernel reports an untrusted reading, and in what run lengths
  - how far consecutive readings move, arbitrated and per sensor
  - whether the arbitrated stream looks smoothed relative to its own inputs
"""
import csv
import glob
import os
import statistics as st

ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "Squash", "Tests", "pulled")


def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            try:
                rows.append({
                    "t": int(r["t_ms"]),
                    "bpm": int(r["bpm_x100"]) / 100.0,
                    "trust": int(r["trust"]),
                    "src": int(r["source"]),
                    "opt": int(r["optical_x100"]) / 100.0,
                    "ext": int(r["external_x100"]) / 100.0,
                })
            except (ValueError, KeyError):
                continue
    return rows


def runs_of_untrusted(rows):
    out, n = [], 0
    for r in rows:
        if r["trust"] == 0:
            n += 1
        elif n:
            out.append(n)
            n = 0
    if n:
        out.append(n)
    return out


def deltas(vals):
    return [abs(b - a) for a, b in zip(vals, vals[1:])]


def summarise(name, rows):
    if len(rows) < 5:
        return None
    dts = [b["t"] - a["t"] for a, b in zip(rows, rows[1:])]
    trusted = [r for r in rows if r["trust"] > 0]
    untr = runs_of_untrusted(rows)

    # Deltas between CONSECUTIVE TRUSTED readings, which is what the detector
    # ever compares; and per-sensor, which is what "smoothed" is asked about.
    d_arb = deltas([r["bpm"] for r in trusted])
    d_opt = deltas([r["opt"] for r in rows if r["opt"] > 0])
    d_ext = deltas([r["ext"] for r in rows if r["ext"] > 0])

    src = {}
    for r in trusted:
        src[r["src"]] = src.get(r["src"], 0) + 1

    print(f"\n--- {name}  ({len(rows)} samples, {(rows[-1]['t']-rows[0]['t'])/1000:.0f} s) ---")
    print(f"  interval ms      median {st.median(dts):.0f}  "
          f"min {min(dts)}  max {max(dts)}  "
          f">1500ms gaps: {sum(1 for d in dts if d > 1500)}")
    print(f"  untrusted        {len(rows)-len(trusted)}/{len(rows)} = "
          f"{100*(len(rows)-len(trusted))/len(rows):.1f}%   "
          f"runs: {sorted(untr, reverse=True)[:8]}"
          f"{' (max %d)' % max(untr) if untr else ''}")
    print(f"  source counts    {src}   (1=optical 2=external)")
    for label, d in (("arbitrated", d_arb), ("optical", d_opt), ("external", d_ext)):
        if len(d) > 3:
            zero = 100.0 * sum(1 for x in d if x == 0) / len(d)
            print(f"  d/step {label:<11} mean {st.mean(d):.2f}  "
                  f"median {st.median(d):.2f}  p95 {sorted(d)[int(0.95*len(d))]:.2f}  "
                  f"max {max(d):.2f}  zero-steps {zero:.0f}%")
    if trusted:
        b = [r["bpm"] for r in trusted]
        print(f"  bpm              min {min(b):.0f}  max {max(b):.0f}  "
              f"mean {st.mean(b):.0f}")
    return {"rows": rows, "trusted": trusted, "untr": untr, "dts": dts,
            "d_arb": d_arb}


def biggest_fall(rows, window_s=60):
    """The largest fall over any `window_s` span of trusted readings."""
    best = None
    tr = [r for r in rows if r["trust"] > 0]
    for i, a in enumerate(tr):
        for b in tr[i + 1:]:
            span = (b["t"] - a["t"]) / 1000.0
            if span < window_s:
                continue
            if span > window_s + 2:
                break
            drop = a["bpm"] - b["bpm"]
            if best is None or drop > best[0]:
                best = (drop, a["bpm"], b["bpm"], span, a["t"] / 1000.0)
            break
    return best


def main():
    allrows, agg = [], []
    for d in sorted(glob.glob(os.path.join(ROOT, "*/"))):
        for p in sorted(glob.glob(os.path.join(d, "*_hr.csv"))):
            rows = load(p)
            s = summarise(os.path.basename(os.path.dirname(p)), rows)
            if s:
                agg.append(s)
                allrows += rows

    print("\n=== POOLED ACROSS ALL RECORDINGS ===")
    tr = [r for r in allrows if r["trust"] > 0]
    untr = [n for s in agg for n in s["untr"]]
    dts = [d for s in agg for d in s["dts"]]
    d_arb = [d for s in agg for d in s["d_arb"]]
    print(f"  samples          {len(allrows)}  ({len(allrows)/60:.0f} min)")
    print(f"  untrusted        {len(allrows)-len(tr)} = "
          f"{100*(len(allrows)-len(tr))/len(allrows):.1f}%")
    if untr:
        print(f"  untrusted runs   n={len(untr)}  median {st.median(untr):.0f}  "
              f"max {max(untr)}  "
              f">=6 long: {sum(1 for n in untr if n >= 6)}")
    print(f"  interval ms      median {st.median(dts):.0f}  "
          f"p95 {sorted(dts)[int(0.95*len(dts))]}  max {max(dts)}")
    print(f"  d/step arbitrated mean {st.mean(d_arb):.2f}  "
          f"median {st.median(d_arb):.2f}  "
          f"zero-steps {100*sum(1 for x in d_arb if x==0)/len(d_arb):.0f}%")

    print("\n=== LARGEST 60 s FALL IN EACH RECORDING ===")
    for d in sorted(glob.glob(os.path.join(ROOT, "*/"))):
        for p in sorted(glob.glob(os.path.join(d, "*_hr.csv"))):
            rows = load(p)
            b = biggest_fall(rows)
            name = os.path.basename(os.path.dirname(p))
            if b:
                print(f"  {name:<40} {b[0]:6.1f} bpm  "
                      f"({b[1]:.0f} -> {b[2]:.0f} over {b[3]:.1f}s at t={b[4]:.0f}s)")
            else:
                print(f"  {name:<40}   no 60 s trusted span")


if __name__ == "__main__":
    main()
