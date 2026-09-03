#!/usr/bin/env python3
"""How often does the arbitrated source change inside a 60 s window?

TrainKit's Recovery struct records no source at all. If the kernel switches
between the strap and the wrist mid-window, hr0 and hr_end can come from
different sensors, and the difference between them is then partly a difference
between instruments.
"""
import csv
import glob
import os
import statistics as st

ROOT = ("/Users/tobymurray/git/watch-apps/.claude/worktrees/effortkit/"
        "Squash/Tests/pulled")


def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            try:
                rows.append((int(r["t_ms"]), int(r["bpm_x100"]) / 100.0,
                             int(r["trust"]), int(r["source"])))
            except (ValueError, KeyError):
                pass
    return rows


flips_per_window, mixed, total, spans = [], 0, 0, []
disagree = []

for d in sorted(glob.glob(os.path.join(ROOT, "*/"))):
    for p in sorted(glob.glob(os.path.join(d, "*_hr.csv"))):
        rows = [r for r in load(p) if r[2] > 0]
        if len(rows) < 60:
            continue
        # Every 60 s window that starts on a trusted sample.
        for i, a in enumerate(rows):
            end = None
            for b in rows[i + 1:]:
                if (b[0] - a[0]) / 1000.0 >= 60:
                    end = b
                    break
                if (b[0] - a[0]) / 1000.0 > 62:
                    break
            if end is None:
                continue
            seg = [r for r in rows[i:] if r[0] <= end[0]]
            srcs = {r[3] for r in seg}
            n_flips = sum(1 for x, y in zip(seg, seg[1:]) if x[3] != y[3])
            total += 1
            flips_per_window.append(n_flips)
            if len(srcs) > 1:
                mixed += 1
            if a[3] != end[3]:
                disagree.append((a[3], end[3], a[1] - end[1]))

print(f"60 s windows examined:        {total}")
print(f"  containing >1 source:       {mixed} = {100*mixed/total:.0f}%")
print(f"  flips per window  mean {st.mean(flips_per_window):.1f}  "
      f"median {st.median(flips_per_window):.0f}  max {max(flips_per_window)}")
print(f"  hr0 and hr_end from DIFFERENT sensors: {len(disagree)} = "
      f"{100*len(disagree)/total:.0f}%")
if disagree:
    drops = [d[2] for d in disagree]
    print(f"     their falls: mean {st.mean(drops):+.1f} bpm  "
          f"min {min(drops):+.1f}  max {max(drops):+.1f}")

# How far apart are the two sensors when both are reporting?
gap = []
for d in sorted(glob.glob(os.path.join(ROOT, "*/"))):
    for p in sorted(glob.glob(os.path.join(d, "*_hr.csv"))):
        with open(p) as f:
            for r in csv.DictReader(f):
                try:
                    o = int(r["optical_x100"]) / 100.0
                    e = int(r["external_x100"]) / 100.0
                except (ValueError, KeyError):
                    continue
                if o > 0 and e > 0:
                    gap.append(abs(o - e))
if gap:
    print(f"\noptical vs external, both present ({len(gap)} samples):")
    print(f"  |difference| mean {st.mean(gap):.1f}  median {st.median(gap):.1f}  "
          f"p95 {sorted(gap)[int(0.95*len(gap))]:.1f}  max {max(gap):.1f} bpm")
