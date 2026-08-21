#!/usr/bin/env python3
"""Diff two SensorLab profiles into a change list.

    profile_diff.py old.json new.json [-o CHANGES.md]

This is the firmware-drift tool, and it is what makes "profiling changes over
time with firmware updates" a command rather than a project.

Keyed on `claim_id` and nothing else. Five kinds of change, in the order they
matter:

  **appeared**    -- a claim exists in the new profile and not the old. Usually
                     a catalogue version bump rather than news; the header says
                     which, because if the catalogue versions match then a claim
                     that appeared is genuinely new.
  **disappeared** -- gone from the new profile. Same caveat, opposite sign.
  **verdict**     -- UNVERIFIED -> CONFIRMED is somebody doing work; CONFIRMED ->
                     REFUTED is the device changing under us. Both are reported,
                     and the direction is stated.
  **value**       -- moved by more than its own spread. A p50 that shifted
                     inside its own p05..p95 has not changed in any sense a
                     reader should act on; one that shifted outside it has.
                     Where no spread exists, a relative tolerance is used and
                     the report says so.
  **conformance** -- MATCHES -> DIFFERS is a regression against a documented
                     claim, which is the single most actionable line this tool
                     can print.

Exit status: 0 when nothing changed, 1 when something did, 2 on an error. So it
works in a gate.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SCHEMA = 1

# Relative tolerance for a value change when the claim carries no spread.
#
# Two percent. Justification: it is comfortably above the quantisation of a
# seven-significant-digit mantissa and of a 1 ms histogram bin at any rate this
# device delivers, and comfortably below the smallest change anyone would call a
# regression -- the accelerometer's measured ~48 Hz against a requested 25 Hz is
# a 92 % discrepancy, and the 4 -> 2 field-count shrink in `RUNNING_CADENCE` is
# 50 %. TODO: replace with the measured run-to-run spread of each claim once
# several profiles of the same firmware exist; until then this is reasoned, not
# measured, and a change just under it will be missed.
DEFAULT_TOLERANCE = 0.02


def number(v):
    """Decode a `[mantissa, exponent]` pair, a non-finite name, or None."""
    if v is None:
        return None
    if isinstance(v, str):
        named = {"nan": float("nan"), "+inf": float("inf"),
                 "-inf": float("-inf"), "zero": 0.0}
        if v in named:
            return named[v]
        try:
            return float(v)
        except ValueError:
            return None
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, list) and len(v) == 2:
        # Pre-2026-08-21 schema-1 profiles carried [mantissa, exponent].
        return float(v[0]) * (10.0 ** int(v[1]))
    return None


def fmt(v):
    n = number(v)
    if n is None:
        return "--"
    if n != n:
        return "NaN"
    if n in (float("inf"), float("-inf")):
        return "+inf" if n > 0 else "-inf"
    if n == int(n) and abs(n) < 1e15:
        return str(int(n))
    return f"{n:.6g}"


def load(path: Path) -> dict:
    with path.open(encoding="utf-8") as fh:
        doc = json.load(fh)
    if doc.get("schema") != SCHEMA:
        raise SystemExit(
            f"{path}: schema {doc.get('schema')}, this tool understands {SCHEMA}"
        )
    return doc


def claims(doc: dict) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for s in doc["sensors"]:
        for c in s["claims"]:
            out[c["claim_id"]] = c
    for c in doc.get("platform_claims", []):
        out[c["claim_id"]] = c
    return out


def sensor_names(doc: dict) -> dict[str, str]:
    return {s["type"]: s["name"] for s in doc["sensors"]}


def value_moved(old: dict, new: dict) -> tuple[bool, str]:
    """Did the value move by more than its own spread?

    Returns (moved, how it was judged). The second half is not decoration: a
    reader has to know whether a change was judged against a measured spread or
    against a fixed tolerance, because only one of those is a property of the
    sensor.
    """
    a, b = number(old.get("value")), number(new.get("value"))
    if a is None and b is None:
        return False, ""
    if a is None or b is None:
        return True, "one profile has no value"

    # NaN is never equal to itself; treat "both NaN" as unchanged and a
    # transition either way as a change, because a statistic becoming NaN is
    # exactly the kind of thing this tool exists to surface.
    if a != a or b != b:
        return (a != a) != (b != b), "non-finite"

    sp = old.get("spread") or new.get("spread")
    if sp:
        lo, hi = number(sp.get("p05")), number(sp.get("p95"))
        if lo is not None and hi is not None and hi > lo:
            band = hi - lo
            return abs(b - a) > band, f"moved more than its own p05..p95 ({band:.4g})"

    if a == 0.0:
        return b != 0.0, "was zero"
    rel = abs(b - a) / abs(a)
    return (rel > DEFAULT_TOLERANCE,
            f"{rel * 100:.1f} % against a {DEFAULT_TOLERANCE * 100:.0f} % tolerance "
            f"(no spread recorded)")


def diff(old: dict, new: dict) -> tuple[list[dict], list[str]]:
    """Return (changes, warnings)."""
    a, b = claims(old), claims(new)
    changes: list[dict] = []
    warnings: list[str] = []

    om, nm = old["manifest"], new["manifest"]
    if om.get("catalogue_version") != nm.get("catalogue_version"):
        warnings.append(
            f"The catalogue versions differ ({om.get('catalogue_version')} vs "
            f"{nm.get('catalogue_version')}), so `appeared` and `disappeared` "
            f"below mostly mean *this build could not measure it*, not *the "
            f"device changed*."
        )
    if om.get("app_version") != nm.get("app_version"):
        warnings.append(
            f"Different SensorLab versions wrote these "
            f"(`{om.get('app_version')}` vs `{nm.get('app_version')}`)."
        )
    for m, label in ((om, "old"), (nm, "new")):
        if not m.get("firmware_read_from_kernel"):
            warnings.append(
                f"The {label} profile's firmware version was declared rather "
                f"than read from the kernel, so what this diff attributes to a "
                f"firmware change may not be one."
            )
        if m.get("end") not in (None, "completed"):
            warnings.append(
                f"The {label} profile's run ended `{m.get('end')}`, so its "
                f"distributions are shorter than the other's."
            )

    for cid in sorted(set(a) | set(b)):
        oc, nc = a.get(cid), b.get(cid)

        if oc is None:
            if nc["verdict"] != "UNVERIFIED":
                changes.append({"kind": "appeared", "id": cid, "new": nc,
                                "detail": f"now {nc['verdict']}, "
                                          f"{fmt(nc.get('value'))} "
                                          f"{nc.get('unit', '')}".strip()})
            continue
        if nc is None:
            if oc["verdict"] != "UNVERIFIED":
                changes.append({"kind": "disappeared", "id": cid, "old": oc,
                                "detail": f"was {oc['verdict']}, "
                                          f"{fmt(oc.get('value'))} "
                                          f"{oc.get('unit', '')}".strip()})
            continue

        if oc["verdict"] != nc["verdict"]:
            changes.append({
                "kind": "verdict", "id": cid, "old": oc, "new": nc,
                "detail": f"{oc['verdict']} -> {nc['verdict']}",
            })

        oconf = oc.get("conformance", "NO_CLAIM")
        nconf = nc.get("conformance", "NO_CLAIM")
        if oconf != nconf:
            changes.append({
                "kind": "conformance", "id": cid, "old": oc, "new": nc,
                "detail": f"{oconf} -> {nconf}"
                          + (f", expected {fmt(nc.get('expected'))} from "
                             f"`{nc.get('expected_source')}`"
                             if nc.get("expected") is not None else ""),
            })

        # Only compare values where both were actually promoted: a value that
        # moved while both rows were UNVERIFIED is two incomplete measurements
        # disagreeing, which is noise.
        if oc["verdict"] in ("CONFIRMED", "LIKELY") \
                and nc["verdict"] in ("CONFIRMED", "LIKELY"):
            moved, how = value_moved(oc, nc)
            if moved:
                changes.append({
                    "kind": "value", "id": cid, "old": oc, "new": nc,
                    "detail": f"{fmt(oc.get('value'))} -> {fmt(nc.get('value'))} "
                              f"{nc.get('unit', '')}".strip() + f" ({how})",
                })

    order = {"conformance": 0, "verdict": 1, "value": 2,
             "disappeared": 3, "appeared": 4}
    changes.sort(key=lambda c: (order.get(c["kind"], 9), c["id"]))
    return changes, warnings


def render(old: dict, new: dict, oldPath: Path, newPath: Path,
           changes: list[dict], warnings: list[str]) -> str:
    om, nm = old["manifest"], new["manifest"]
    L: list[str] = []

    L.append(f"# Sensor profile change list: "
             f"{om.get('firmware') or '?'} -> {nm.get('firmware') or '?'}")
    L.append("")
    L.append(f"`{oldPath.name}` -> `{newPath.name}`, keyed on `claim_id`.")
    L.append("")

    oc = old["completeness"]["overall"]
    nc = new["completeness"]["overall"]
    L.append(f"Completeness moved from {oc['percent']} % "
             f"({oc['answered']}/{oc['applicable']}) to {nc['percent']} % "
             f"({nc['answered']}/{nc['applicable']}). "
             f"**A change list from an incomplete pair of profiles is itself "
             f"incomplete** -- a claim nobody measured in either run cannot show "
             f"a change.")
    L.append("")

    if warnings:
        L.append("## Read this before the change list")
        L.append("")
        for w in warnings:
            L.append(f"- {w}")
        L.append("")

    if not changes:
        L.append("## No changes")
        L.append("")
        L.append("Every claim answered in both profiles has the same verdict, "
                 "the same conformance, and a value within its own spread.")
        return "\n".join(L) + "\n"

    L.append(f"## {len(changes)} changes")
    L.append("")

    names = sensor_names(new) | sensor_names(old)
    by_kind: dict[str, list[dict]] = {}
    for c in changes:
        by_kind.setdefault(c["kind"], []).append(c)

    headings = {
        "conformance": ("Conformance changed",
                        "A claim that agreed with a documented figure and now "
                        "does not, or the reverse. The most actionable lines "
                        "here."),
        "verdict": ("Verdict changed",
                    "`UNVERIFIED -> CONFIRMED` is somebody having done the work. "
                    "`CONFIRMED -> REFUTED` is the device having changed."),
        "value": ("Value moved beyond its spread",
                  "Judged against the claim's own p05..p95 where one exists, and "
                  "against a fixed relative tolerance where none does -- the "
                  "`how` column says which."),
        "disappeared": ("Claims that disappeared",
                        "Answered in the old profile and not in the new."),
        "appeared": ("Claims that appeared",
                     "Answered in the new profile and not in the old."),
    }

    for kind in ("conformance", "verdict", "value", "disappeared", "appeared"):
        rows = by_kind.get(kind)
        if not rows:
            continue
        title, blurb = headings[kind]
        L.append(f"### {title} ({len(rows)})")
        L.append("")
        L.append(blurb)
        L.append("")
        L.append("| Claim | Sensor | Change | Method | n (old -> new) |")
        L.append("| --- | --- | --- | --- | --- |")
        for c in rows:
            scope = c["id"].split(".")[0]
            name = names.get(scope, "platform" if scope == "platform" else "?")
            ref = c.get("new") or c.get("old")
            n_old = c.get("old", {}).get("n", "--")
            n_new = c.get("new", {}).get("n", "--")
            L.append(f"| `{c['id']}` | {name} | {c['detail']} "
                     f"| `{ref.get('method_id', '')}` | {n_old} -> {n_new} |")
        L.append("")

    return "\n".join(L) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old", type=Path)
    ap.add_argument("new", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    args = ap.parse_args()

    old, new = load(args.old), load(args.new)
    changes, warnings = diff(old, new)
    text = render(old, new, args.old, args.new, changes, warnings)

    if args.out:
        args.out.write_text(text, encoding="utf-8")
        print(f"wrote {args.out}: {len(changes)} changes", file=sys.stderr)
    else:
        sys.stdout.write(text)

    # 0 = nothing changed, 1 = something did. Usable in a gate.
    return 1 if changes else 0


if __name__ == "__main__":
    raise SystemExit(main())
