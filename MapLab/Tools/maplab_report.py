#!/usr/bin/env python3
"""Read MapLab's log off the watch and answer the four gates with it.

The app writes `maplab_log.csv` into its own folder (`Apps/MapLab/` on the USB
volume). This is the other half of that contract: the normative description of
the format is the class comment on `Software/Libs/Header/BenchLog.hpp`, and
this script parses exactly that and nothing else.

    Tools/maplab_report.py maplab_log.csv            # the newest run
    Tools/maplab_report.py maplab_log.csv --all      # every run in the file
    Tools/maplab_report.py maplab_log.csv --raw      # every row, no verdicts

What it will not do is fill in a gap. A bench that did not run prints as
`not measured`, never as a zero, because the whole point of the exercise is to
stop arguing from numbers nobody took.
"""

import argparse
import csv
import sys
from collections import OrderedDict

SCHEMA = 1

# Gate C's budget, from MapKit/Docs/VECTOR-PIPELINE-PROMPT.md § 3: a redraw
# that misses a frame is fine, one that misses a second is not.
FRAME_BUDGET_MS = 100

# What the raster path already measured, for cross-checking the harness rather
# than for grading it. Sources: MapKit/README.md and the rawtiles device proof.
KNOWN = {
    "I06": ("64 KiB read", "6-9 ms per 64 KiB tile measured on the raster path"),
    "I02": ("cold fs touch", "~113 ms first touch after app start, ~4 ms after"),
}

# Which benches put a throughput figure in column `a`. The rest use `a` for
# something bench-specific -- bytes written, a tile's decoded size, an entry
# count -- and printing all of them as KB/s is how a report starts lying.
THROUGHPUT_IDS = {"I03", "I04", "I05", "I06", "I07", "I09"}


def ms(us):
    return us / 1000.0


def fmt_time(us):
    if us is None:
        return "not measured"
    if us >= 10000:
        return f"{ms(us):.1f} ms"
    if us >= 1000:
        return f"{ms(us):.2f} ms"
    return f"{us} us"


def load(path):
    runs = OrderedDict()
    header_seen = False
    with open(path, newline="") as handle:
        for row in csv.reader(handle):
            if not row:
                continue
            kind = row[0]
            if kind == "H":
                header_seen = True
                schema = int(row[1])
                if schema != SCHEMA:
                    sys.exit(
                        f"{path}: log schema {schema}, this script reads {SCHEMA}. "
                        "Columns are matched by name, never by position -- update "
                        "the script rather than trusting the alignment."
                    )
            elif kind == "R":
                run = row[1]
                runs.setdefault(run, {"uptime": row[2], "wall": row[3],
                                      "build": row[4], "subject": row[5], "rows": []})
            elif kind == "B":
                run = row[1]
                entry = runs.setdefault(run, {"uptime": "?", "wall": "?", "build": "?",
                                              "subject": "?", "rows": []})
                entry["rows"].append({
                    "uptime": int(row[2]),
                    "group": row[3],
                    "id": row[4],
                    "name": row[5],
                    "iterations": int(row[6]),
                    "elapsed_ms": int(row[7]),
                    "us": int(row[8]),
                    "valid": row[9] == "1",
                    "a": int(row[10]),
                    "b": int(row[11]),
                    "c": int(row[12]),
                    "note": row[13] if len(row) > 13 else "",
                })
    if not header_seen:
        print(f"warning: {path} has no H row; it may be truncated", file=sys.stderr)
    return runs


def by_id(rows):
    out = {}
    for row in rows:
        # Later rows win: a bench re-run in the same session is a correction.
        out[row["id"]] = row
    return out


def print_rows(rows):
    print(f"  {'id':<5} {'bench':<14} {'cost':>12}  {'iters':>7}  notes")
    for row in rows:
        if row["group"] == "W":
            # A staircase step's subject is the block itself, so the elapsed
            # time is the measurement and the per-pass cost is noise.
            cost = f"{row['elapsed_ms']} ms" if row["valid"] else "not measured"
        else:
            cost = fmt_time(row["us"]) if row["valid"] else "not measured"
        extra = []
        if row["id"] in THROUGHPUT_IDS and row["a"] > 0:
            extra.append(f"{row['a']} KB/s")
        elif row["id"] == "I00" and row["a"] > 0:
            extra.append(f"{row['a'] // 1024} KiB written")
        elif row["id"] == "I11" and row["a"] > 0:
            extra.append(f"{row['a'] // 1024} KiB tile, {row['b']} tiles in pack")
        elif row["group"] == "W" and row["a"] > 0:
            extra.append(f"target {row['a']} ms")
        if row["group"] == "B" and row["b"] > 0:
            extra.append(f"{row['b']} KB/s")
        if row["note"]:
            extra.append(row["note"])
        print(f"  {row['id']:<5} {row['name']:<14} {cost:>12}  {row['iterations']:>7}  "
              f"{'  '.join(extra)}")


def verdicts(rows):
    idx = by_id(rows)
    print("\n  GATES")

    # --- Gate C: does the specified map render inside a frame budget? -------
    render = idx.get("R08")
    blit = idx.get("B01")
    if render and render["valid"]:
        total_us = render["us"] + (blit["us"] if blit and blit["valid"] else 0)
        verdict = "PASS" if total_us <= FRAME_BUDGET_MS * 1000 else "FAIL"
        detail = f"render {fmt_time(render['us'])}"
        if blit and blit["valid"]:
            detail += f" + blit {fmt_time(blit['us'])}"
        else:
            detail += " (blit not measured, so this is a lower bound)"
        print(f"  C  time     {verdict:<6} {detail} vs {FRAME_BUDGET_MS} ms budget")
        if render["b"] > 0:
            per_point = render["us"] / render["b"]
            print(f"              city scene: {render['a']} features, {render['b']} points, "
                  f"{per_point:.2f} us/point, {render['c']} B encoded")
        if render["note"] == "INCOMPLETE":
            print("              INCOMPLETE: the render dropped geometry, so this "
                  "timing is not a measurement of the specified map")
    else:
        print("  C  time     not measured   (R08 did not run)")

    # --- the decode/raster split -------------------------------------------
    decode = idx.get("R05")
    if decode and decode["valid"] and render and render["valid"]:
        share = 100.0 * decode["us"] / render["us"] if render["us"] else 0
        print(f"  -  decode   {share:.0f}% of the city render is decode+transform; "
              f"the rest is rasterising")
        print("              a faster wire format can only ever buy back that share")

    # --- X7: the restyle LUT, never measured on hardware before ------------
    lut = idx.get("R09")
    if lut and lut["valid"]:
        share = 100.0 * lut["us"] / (render["us"] + lut["us"]) if render and render["valid"] else None
        tail = f", {share:.0f}% on top of a city render" if share is not None else ""
        print(f"  X7 restyle  {fmt_time(lut['us'])} per full-screen LUT pass{tail}")
        print("              (day/night/contrast/trail were proven in simulation only)")

    # --- the canvas architecture against the shipped raster path -----------
    mosaic = idx.get("B02")
    if blit and blit["valid"] and mosaic and mosaic["valid"]:
        ratio = mosaic["us"] / blit["us"] if blit["us"] else 0
        print(f"  -  blit     canvas {fmt_time(blit['us'])} vs 2x2 raster mosaic "
              f"{fmt_time(mosaic['us'])} ({ratio:.2f}x)")

    # --- the layer directory's access pattern ------------------------------
    seq = idx.get("I06")
    rnd = idx.get("I07")
    if seq and seq["valid"] and rnd and rnd["valid"]:
        print(f"  -  io       64 KiB sequential {fmt_time(seq['us'])} ({seq['a']} KB/s); "
              f"512 B random seek {fmt_time(rnd['us'])}")
        print("              the second is the cost the per-tile layer directory pays "
              "per (tile, layer)")
    for key, (label, known) in KNOWN.items():
        row = idx.get(key)
        if row and row["valid"]:
            print(f"  -  {key}      {label}: {fmt_time(row['us'])}  [previously: {known}]")

    # --- the watchdog ------------------------------------------------------
    stair = [r for r in rows if r["id"] == "W01"]
    survived = [r for r in stair if r["note"] == "survived"]
    intents = [r for r in stair if r["note"] == "about-to-block"]
    if intents:
        longest_ok = max((r["a"] for r in survived), default=0)
        attempted = max((r["a"] for r in intents), default=0)
        if attempted > longest_ok:
            print(f"  W  watchdog longest survived block {longest_ok} ms; "
                  f"{attempted} ms was attempted and did not report back")
            print("              that missing row IS the finding -- the device went "
                  "down inside it")
        else:
            print(f"  W  watchdog survived {longest_ok} ms of blocked GUI thread "
                  "(no step has failed yet)")

    print("\n  Gate B (RAM) is a build-time measurement, not a runtime one: "
          "run Tools/gate_b_link_test.sh.")
    print("  Gate D (legibility) is the card suite, and is settled by eyes and a "
          "camera, not by this script.")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log")
    parser.add_argument("--all", action="store_true", help="every run, not just the newest")
    parser.add_argument("--raw", action="store_true", help="rows only, no verdicts")
    args = parser.parse_args()

    runs = load(args.log)
    if not runs:
        sys.exit("no runs in the log")

    chosen = list(runs.items()) if args.all else [list(runs.items())[-1]]
    for run_id, run in chosen:
        print(f"\nrun {run_id}  build {run['build']}  subject {run['subject']}  "
              f"uptime {run['uptime']} ms  wall {run['wall']}")
        print_rows(run["rows"])
        if not args.raw:
            verdicts(run["rows"])

    if len(runs) > 1 and not args.all:
        print(f"\n({len(runs) - 1} earlier run(s) in this file; --all to see them. "
              "Uptime jumping backwards between runs is a device reboot; climbing "
              "is an app restart, which a USB connection causes.)")


if __name__ == "__main__":
    main()
