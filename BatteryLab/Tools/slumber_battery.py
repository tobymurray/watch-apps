#!/usr/bin/env python3
"""Measure the battery from Slumber's day files: the 100% clamp, restarts, the
counter against integrated current, voltage against remaining, and drain by hour.

    python3 BatteryLab/Tools/slumber_battery.py <copy of Apps/Slumber/Days>

Prints numbers only. The day files hold sleep and wear times, so they stay off
the repository; README.md records what this printed.
"""
import csv
import glob
import statistics as st
import sys
from collections import defaultdict
from datetime import datetime, timedelta, timezone

GAP_S = 120


def load(days_dir):
    rows = []
    for path in sorted(glob.glob(days_dir + '/*.csv')):
        with open(path) as fh:
            fh.readline()
            rows += [{k: int(v) for k, v in r.items()} for r in csv.DictReader(fh)]
    rows.sort(key=lambda r: r['utc'])
    return rows


def local(r):
    return datetime.fromtimestamp(r['utc'], timezone(timedelta(minutes=r['utc_off_min'])))


def breaks(rows):
    """Indexes where the process restarted or the log stopped for more than GAP_S."""
    return [i for i in range(1, len(rows))
            if rows[i]['uptime_ms'] < rows[i - 1]['uptime_ms'] or rows[i]['utc'] - rows[i - 1]['utc'] > GAP_S]


def segments(rows):
    edges = [0] + breaks(rows) + [len(rows)]
    out = []
    for a, b in zip(edges, edges[1:]):
        seg = [r for r in rows[a:b] if r['batt_mah'] >= 0 and r['batt_mv'] > 0]
        if len(seg) > 20:
            out.append(seg)
    return out


def clamp(rows):
    print('== the 100% clamp ==')
    valid = [r for r in rows if r['batt_mah'] >= 0 and r['batt_pct_x10'] >= 0]
    full = [r['batt_mah'] for r in valid if r['batt_pct_x10'] >= 999]
    print(f'pct >= 99.9 while remaining is {min(full)}..{max(full)} mAh')
    implied = [r['batt_mah'] * 1000 / r['batt_pct_x10'] for r in valid if 0 < r['batt_pct_x10'] < 990]
    print(f'below that, remaining / pct implies {min(implied):.1f}..{max(implied):.1f} mAh, '
          f'median {st.median(implied):.1f}, over {len(implied)} rows')


def restarts(rows):
    print('\n== restarts and gaps: last reading before, first after ==')
    for i in breaks(rows):
        before = next((x for x in reversed(rows[max(0, i - 40):i]) if x['batt_mah'] >= 0), None)
        after = next((x for x in rows[i:i + 80] if x['batt_mah'] >= 0), None)
        if before is None or after is None:
            continue
        gap = (after['utc'] - before['utc']) / 60
        print(f"{local(before):%m-%d %H:%M} {before['batt_mah']:3d} mAh {before['batt_mv']} mV -> "
              f"{local(after):%m-%d %H:%M} {after['batt_mah']:3d} mAh {after['batt_mv']} mV  "
              f"gap {gap:4.0f} min  change {after['batt_mah'] - before['batt_mah']:+d} mAh")
    print('rows with charging > 0:', sum(r['charging'] > 0 for r in rows))


def counter_vs_current(segs):
    print('\n== counter fall against integrated average current, per stretch ==')
    for s in segs:
        hours = (s[-1]['utc'] - s[0]['utc']) / 3600
        fall = s[0]['batt_mah'] - s[-1]['batt_mah']
        q = sum(-a['batt_avg_ma_x10'] / 10 * (b['utc'] - a['utc']) / 3600 for a, b in zip(s, s[1:]))
        ratio = f'{q / fall:.3f}' if fall >= 10 else '  -  '
        print(f"{local(s[0]):%m-%d %H:%M} {hours:5.1f} h  counter {s[0]['batt_mah']}->{s[-1]['batt_mah']} "
              f"fell {fall:3d}  integral {q:5.1f} mAh  ratio {ratio}")


def voltage_curve(segs):
    print('\n== voltage against remaining, rows more than an hour after a restart ==')
    by_mah = defaultdict(list)
    for s in segs:
        for r in s:
            if r['utc'] - s[0]['utc'] > 3600:
                by_mah[r['batt_mah']].append(r['batt_mv'])
    for m in sorted(by_mah)[::4]:
        xs = sorted(by_mah[m])
        print(f"{m:4d} mAh  n={len(xs):4d}  median {st.median(xs):5.0f} mV  "
              f"p10 {xs[len(xs) // 10]}  p90 {xs[len(xs) * 9 // 10]}")
    print('\n== after each restart: minutes:remaining/mV ==')
    for s in segs[1:]:
        marks = []
        for at in (0, 5, 30, 90):
            r = next((x for x in s if x['utc'] - s[0]['utc'] >= at * 60), None)
            if r is not None:
                marks.append(f"{at}m:{r['batt_mah']}/{r['batt_mv']}")
        print(f"{local(s[0]):%m-%d %H:%M}", ' '.join(marks))


def drain(segs):
    print('\n== drain by local hour: counter, and integrated average current ==')
    by_hour = defaultdict(lambda: [0.0, 0.0, 0.0])
    for s in segs:
        for a, b in zip(s, s[1:]):
            h = (b['utc'] - a['utc']) / 3600
            slot = by_hour[local(a).hour]
            slot[0] += a['batt_mah'] - b['batt_mah']
            slot[1] += -a['batt_avg_ma_x10'] / 10 * h
            slot[2] += h
    for hour in range(24):
        q, qi, h = by_hour[hour]
        if h:
            print(f'{hour:02d}  counter {q / h:4.2f} mA  current {qi / h:4.2f} mA  over {h:4.1f} h')


def main(days_dir):
    rows = load(days_dir)
    print(f'{len(rows)} rows, {local(rows[0]):%Y-%m-%d %H:%M} to {local(rows[-1]):%Y-%m-%d %H:%M}\n')
    segs = segments(rows)
    clamp(rows)
    restarts(rows)
    counter_vs_current(segs)
    voltage_curve(segs)
    drain(segs)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
