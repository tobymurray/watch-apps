#!/usr/bin/env python3
"""Reads a copy of a watch's Apps/Slumber/ and prints what it recorded.

    python3 Slumber/Tools/analyse.py <copy of Apps/Slumber>

Rest, wrist, periods and the card come from nights.cpp, compiled here from the
headers the watch runs, so this never re-derives what the watch decides. Every
number it prints comes from the copy; nothing is written into the repository.
"""

import csv
import os
import statistics
import subprocess
import sys
import tempfile
from collections import defaultdict
from datetime import datetime, timedelta, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
HEADERS = os.path.join(HERE, '..', 'Software', 'Libs', 'Header')

# SleepPeriods' kWornTrustX10 and kSensorFloorCount; `check_constants` holds them to the header.
WORN_TRUST_X10 = 20
SENSOR_FLOOR = 250
EPOCH_S = 30


def check_constants():
    text = open(os.path.join(HEADERS, 'Sleep', 'SleepPeriods.hpp')).read()
    for name, value in (('kWornTrustX10', WORN_TRUST_X10), ('kSensorFloorCount', SENSOR_FLOOR)):
        if f'{name} = {value};' not in text:
            sys.exit(f'{name} in SleepPeriods.hpp is no longer {value}; update analyse.py')


def build_nights(tmp):
    exe = os.path.join(tmp, 'nights')
    subprocess.run(['c++', '-std=c++17', '-O2', '-I', os.path.join(HEADERS, 'Sleep'),
                    '-I', os.path.join(HEADERS, 'Recorder'), os.path.join(HERE, 'nights.cpp'),
                    '-o', exe], check=True)
    return exe


def read_rows(path):
    with open(path) as f:
        lines = [l for l in f if not l.startswith('#') and l.strip()]
    rows = []
    for r in csv.DictReader(lines):
        try:
            rows.append({k: int(v) for k, v in r.items()})
        except (TypeError, ValueError):
            pass  # a row cut short by a crash
    return rows


def local(utc, off_min):
    return datetime.fromtimestamp(utc, timezone(timedelta(minutes=off_min)))


def hm(utc, off_min):
    return local(utc, off_min).strftime('%m-%d %H:%M')


def pct(values, *ps):
    v = sorted(values)
    if not v:
        return ['-'] * len(ps)
    return [v[min(len(v) - 1, int(p / 100 * len(v)))] for p in ps]


def longest_run(flags):
    best = cur = 0
    for f in flags:
        cur = cur + 1 if f else 0
        best = max(best, cur)
    return best


def section(title):
    print(f'\n== {title}')


def recording(rows, off):
    section('Recording')
    first, last = rows[0], rows[-1]
    hours = (last['utc'] - first['utc'] + EPOCH_S) / 3600
    print(f"{len(rows)} rows, {hm(first['utc'] - EPOCH_S, off)} to {hm(last['utc'], off)} ({hours:.2f} h)")
    gaps = [(a, b) for a, b in zip(rows, rows[1:]) if b['utc'] - a['utc'] > 3 * EPOCH_S]
    for a, b in gaps:
        print(f"  gap {hm(a['utc'], off)} -> {hm(b['utc'], off)}: {(b['utc'] - a['utc']) / 60:.0f} min")
    restarts = sum(b['uptime_ms'] < a['uptime_ms'] for a, b in zip(rows, rows[1:]))
    print(f"restarts {restarts}, slots lost {sum(r['slots_lost'] for r in rows)}, "
          f"write fails {max(r['write_fails'] for r in rows)} (the most any run reached)")
    print(f"acc samples p50 {pct([r['acc_samples'] for r in rows], 50)[0]}, "
          f"batches p50 {pct([r['acc_batches'] for r in rows], 50)[0]}, "
          f"wakes p50 {pct([r['wakes'] for r in rows], 50)[0]}, "
          f"max gap ms p99 {pct([r['acc_max_gap_ms'] for r in rows], 99)[0]}")


def battery(rows, off):
    section('Battery')
    known = [r for r in rows if r['batt_mah'] >= 0]
    if not known:
        print('no batt_mah readings')
        return
    stretch = [known[0]]
    for r in known[1:] + [None]:
        if r is not None and r['charging'] <= 0 and r['batt_mah'] <= stretch[-1]['batt_mah'] + 1:
            stretch.append(r)
            continue
        a, b = stretch[0], stretch[-1]
        h = (b['utc'] - a['utc']) / 3600
        if h >= 1:
            print(f"{hm(a['utc'], off)} -> {hm(b['utc'], off)}: {a['batt_mah']} -> {b['batt_mah']} mAh "
                  f"over {h:.1f} h = {(a['batt_mah'] - b['batt_mah']) / h:.2f} mA (whole watch)")
        if r is not None:
            stretch = [r]


def lifecycle(path):
    section('Lifecycle log')
    if not os.path.exists(path):
        print('no Debug/lifecycle.log')
        return
    wanted = ('start', 'stop', 'history', 'night', 'write-fail', 'card-fail', 'clock')
    lines = [l.rstrip() for l in open(path, errors='replace')]
    # The log covers every run since install; the latest are what a new pull adds.
    for l in lines[-40:]:
        parts = l.split(' ', 2)
        if len(parts) > 1 and parts[1] in wanted:
            print(' ', l)


def nights(exe, day_files, off):
    out = subprocess.run([exe] + day_files, check=True, capture_output=True, text=True).stdout
    epochs, periods, card = {}, [], None
    for line in out.splitlines():
        f = line.split(',')
        if f[0] == 'epoch':
            epochs[int(f[1])] = (f[2] == '1', f[3])
        elif f[0] == 'period':
            periods.append(dict(start=int(f[1]), end=int(f[2]), rest=int(f[3]), missing=int(f[4]),
                                longest=int(f[5]), spells=int(f[6]), worn=f[7], worn_pct=int(f[8]),
                                hr_cover=int(f[9]), settled=f[10] == '1'))
        elif f[0] == 'card':
            card = (f[1], int(f[2]), int(f[3]))
        elif f[0] == 'skip':
            print(f'  skipped {os.path.basename(f[1])}: another schema')
    section('Periods (what the watch finds)')
    print(f"{'start':<12} {'end':<6} {'hours':>5} {'rest':>5} {'longest':>7} {'spells':>6} "
          f"{'worn%':>5} {'hr%':>4}  verdict")
    for p in periods:
        print(f"{hm(p['start'], off):<12} {hm(p['end'], off)[6:]:<6} {(p['end'] - p['start']) / 3600:5.1f} "
              f"{p['rest']:5} {p['longest']:7} {p['spells']:6} {p['worn_pct']:5} {p['hr_cover']:4}  "
              f"{p['worn']}{'' if p['settled'] else ' (not settled)'}")
    if card:
        print(f"card at the last row: {card[0]} {hm(card[1], off)}-{hm(card[2], off)[6:]}")
    return epochs, periods


def night_log(path):
    section('Nights.csv (what the watch logged)')
    if not os.path.exists(path):
        print('none yet: a night is logged once it settles, an hour after its last rest')
        return
    with open(path) as f:
        lines = [l for l in f if not l.startswith('#') and l.strip()]
    for r in csv.DictReader(lines):
        print(f"  {r['start_local']} -> {r['end_local'][11:]} rest {r['rest_min']} min, longest "
              f"{r['longest_rest_min']}, spells {r['restless_spells']}, {r['worn']}")


def read_marks(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        lines = [l for l in f if not l.startswith('#') and l.strip()]
    return [dict(utc=int(r['utc']), label=r['label']) for r in csv.DictReader(lines)]


def marks_against_periods(marks, periods, off):
    section('Night edges against the marks')
    worn = [p for p in periods if p['worn'] == 'worn']
    if not worn or not marks:
        print('no worn periods or no marks')
        return
    for p in worn:
        before = [m for m in marks if m['label'] == 'Lights out' and abs(m['utc'] - p['start']) < 3 * 3600]
        after = [m for m in marks if m['label'] == 'Awake in bed' and abs(m['utc'] - p['end']) < 3 * 3600]
        s = f"{hm(p['start'], off)}-{hm(p['end'], off)[6:]}:"
        if before:
            m = min(before, key=lambda m: abs(m['utc'] - p['start']))
            s += f" rest began {(p['start'] - m['utc']) / 60:+.0f} min from Lights out;"
        if after:
            m = min(after, key=lambda m: abs(m['utc'] - p['end']))
            s += f" rest ended {(p['end'] - m['utc']) / 60:+.0f} min from Awake in bed"
        print(' ', s)


def states(rows, marks, epochs, off):
    section('Marked states')
    if not marks:
        print('no marks')
        return
    spans = defaultdict(list)
    for m, nxt in zip(marks, marks[1:] + [None]):
        until = nxt['utc'] if nxt else rows[-1]['utc']
        spans[m['label']].append([r for r in rows if m['utc'] < r['utc'] - EPOCH_S and r['utc'] <= until])
    print(f"{'state':<14} {'min':>5} {'longest rest':>12} {'count p10/50/90':>20} "
          f"{'turn° p50/90':>13} {'hr p10/50/90':>16} {'trust<2':>7} {'silent':>6}")
    for label, groups in spans.items():
        rs = [r for g in groups for r in g]
        if not rs:
            continue
        runs = [longest_run(epochs.get(r['utc'] - EPOCH_S, (False,))[0] for r in g) for g in groups]
        hr = [r['hr_mean_x10'] / 10 for r in rs if r['hr_n'] > 0]
        print(f"{label:<14} {len(rs) / 2:5.0f} {max(runs) / 2:10.1f} m "
              f"{'/'.join(str(x) for x in pct([r['count'] for r in rs], 10, 50, 90)):>20} "
              f"{'/'.join(str(x / 10) if x != '-' else x for x in pct([r['turn_sum_dx10'] for r in rs], 50, 90)):>13} "
              f"{'/'.join(f'{x:.0f}' if x != '-' else x for x in pct(hr, 10, 50, 90)):>16} "
              f"{sum(0 < r['hr_n'] and r['hr_trust_x10'] < WORN_TRUST_X10 for r in rs) * 100 // len(rs):6}% "
              f"{sum(r['hr_n'] == 0 for r in rs) * 100 // len(rs):5}%")
    print('A night needs a rest run of 30 min; a state that reaches it can be mistaken for sleep.')


def floor_check(rows, periods, off):
    section(f'Low trust against the sensor floor ({SENSOR_FLOOR})')
    def stretches(inside):
        out, cur = [], []
        for r in rows:
            low = r['hr_n'] > 0 and r['hr_trust_x10'] < WORN_TRUST_X10
            if low and inside(r['utc'] - EPOCH_S):
                cur.append(r)
            else:
                if len(cur) >= 10:
                    out.append(cur)
                cur = []
        if len(cur) >= 10:
            out.append(cur)
        return out
    def within(kind):
        return lambda t: any(p['start'] <= t < p['end'] and p['worn'] == kind for p in periods)
    for kind, wrong in (('worn', 'at or under'), ('not-worn', 'over')):
        found = stretches(within(kind))
        print(f'{kind} periods: {len(found)} low-trust stretches of 5 min or more')
        for s in found:
            counts = [r['count'] for r in s]
            bad = sum(c <= SENSOR_FLOOR for c in counts) if kind == 'worn' else sum(c > SENSOR_FLOOR for c in counts)
            print(f"  {hm(s[0]['utc'] - EPOCH_S, off)}-{hm(s[-1]['utc'], off)[6:]} {len(s) / 2:.0f} min, "
                  f"count p10/50/90 {'/'.join(str(x) for x in pct(counts, 10, 50, 90))}, "
                  f"{bad} of {len(s)} epochs {wrong} the floor")
    print('A worn stretch with many epochs at or under the floor, or a table with many over it, '
          'argues against the cutoff.')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = sys.argv[1]
    days_dir = os.path.join(root, 'Days')
    day_files = sorted(os.path.join(days_dir, n) for n in os.listdir(days_dir)
                       if len(n) == 12 and n.endswith('.csv'))
    if not day_files:
        sys.exit(f'no day files in {days_dir}')
    check_constants()
    rows = []
    for path in day_files:
        rs = read_rows(path)
        if rs and 'turn_sum_dx10' in rs[0]:
            rows += rs
    rows = [r for r in rows if r['utc'] > 0]
    if not rows:
        sys.exit('no rows under the current schema')
    off = rows[-1]['utc_off_min']

    with tempfile.TemporaryDirectory() as tmp:
        exe = build_nights(tmp)
        recording(rows, off)
        lifecycle(os.path.join(root, 'Debug', 'lifecycle.log'))
        epochs, periods = nights(exe, day_files, off)
    night_log(os.path.join(root, 'Nights.csv'))
    marks = read_marks(os.path.join(root, 'Marks.csv'))
    marks_against_periods(marks, periods, off)
    states(rows, [m for m in marks if m['utc'] >= rows[0]['utc'] - EPOCH_S], epochs, off)
    floor_check(rows, periods, off)
    battery(rows, off)


if __name__ == '__main__':
    main()
