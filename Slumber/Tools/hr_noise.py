#!/usr/bin/env python3
"""How far a night's sleeping heart rate strays from the nights before it.

Sets Schedule::kNotableBpm. Each argument is a Slumber folder copied off a watch
(with ../SharedData/health/sleep/sessions.jsonl beside it, as on the watch) or a
folder of SleepLab session CSVs. Worn nights are taken in time order, measured
as Slumber measures them, and each is set against the median of up to 14 before
it.

    python3 Tools/hr_noise.py <dump>/Slumber <sleeplab>/Nights
"""

import csv
import json
import os
import statistics
import sys

TRUST_X10 = 20       # SleepPeriods::kWornTrustX10
WINDOW_S = 600       # NightSummary::kHrLowWindowS
MIN_READINGS = 15    # NightSummary::kHrLowMinReadings
PRIOR = 14           # Schedule::kHeartNights


def measure(epochs):
    """Median and lowest 10-minute mean of trusted readings, or None."""
    trusted = [(u, h) for u, h in epochs if h is not None]
    if not epochs or len(trusted) * 2 < len(epochs):
        return None
    median = statistics.median(h for _, h in trusted) / 10
    low, j, total, n = None, 0, 0, 0
    for i, (u, h) in enumerate(trusted):
        while j < len(trusted) and trusted[j][0] < u + WINDOW_S:
            total += trusted[j][1]
            n += 1
            j += 1
        if n >= MIN_READINGS and (low is None or total / n < low):
            low = total / n
        total -= h
        n -= 1
    return median, None if low is None else low / 10


def rows(path):
    with open(path) as f:
        return list(csv.DictReader(line for line in f if not line.startswith('#')))


def slumber_nights(root):
    days = {}
    for name in sorted(os.listdir(os.path.join(root, 'Days'))):
        for r in rows(os.path.join(root, 'Days', name)):
            ok = int(r['hr_n']) > 0 and int(r['hr_trust_x10']) >= TRUST_X10
            days[int(r['utc']) - 30] = int(r['hr_mean_x10']) if ok else None
    sessions = os.path.join(root, '..', 'SharedData', 'health', 'sleep', 'sessions.jsonl')
    for line in open(sessions):
        if not line.endswith('\n'):
            continue
        s = json.loads(line)
        if s['source'] == 'Slumber' and s['worn'] == 'worn':
            yield s['start'], [(u, h) for u, h in sorted(days.items()) if s['start'] <= u < s['end']]


def sleeplab_nights(folder):
    for name in sorted(os.listdir(folder)):
        if not name.endswith('.csv'):
            continue
        epochs = []
        table = rows(os.path.join(folder, name))
        if not table or 'hr_mean_x10' not in table[0]:
            continue
        for r in table:
            n = int(r.get('hr_samples') or r.get('hr_n') or 0)
            ok = n > 0 and int(r.get('hr_trust_x10') or 0) >= TRUST_X10 and int(r.get('worn_pct') or 100) > 50
            epochs.append((int(r.get('wall_utc') or r.get('utc')), int(r['hr_mean_x10']) if ok else None))
        if len(epochs) >= 4 * 120:
            yield epochs[0][0], epochs


def main():
    nights = []
    for arg in sys.argv[1:]:
        found = slumber_nights(arg) if os.path.isdir(os.path.join(arg, 'Days')) else sleeplab_nights(arg)
        for start, epochs in found:
            m = measure(epochs)
            if m:
                nights.append((start, *m))
    nights.sort()
    for k, name in ((1, 'median'), (2, 'lowest 10 min')):
        dev = []
        for i in range(3, len(nights)):
            prior = [x[k] for x in nights[max(0, i - PRIOR):i] if x[k] is not None]
            if len(prior) >= 3 and nights[i][k] is not None:
                dev.append(abs(nights[i][k] - statistics.median(prior)))
        if not dev:
            continue
        dev.sort()
        at = lambda q: dev[min(len(dev) - 1, int(q * len(dev)))]
        print(f'{name}: {len(dev)} nights against up to {PRIOR} before each; '
              f'|deviation| p50 {at(.5):.1f}, p90 {at(.9):.1f}, max {dev[-1]:.1f} bpm; '
              + ', '.join(f'{sum(d >= t for d in dev) / len(dev):.0%} reach {t}' for t in (3, 4, 5)))


if __name__ == '__main__':
    main()
